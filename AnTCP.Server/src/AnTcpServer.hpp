#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using AnTcpSocket = SOCKET;
constexpr AnTcpSocket ANTCP_INVALID_SOCKET = INVALID_SOCKET;
#else
#include <netinet/in.h>
#include <sys/socket.h>
using AnTcpSocket = int;
constexpr AnTcpSocket ANTCP_INVALID_SOCKET = -1;
#endif

/// AnTCP: minimal request/response protocol over TCP.
///
/// Every message in both directions is framed as:
///   int32 size (little endian, counts type + payload) | uint8 type | payload[size - 1]
///
/// The server dispatches each request to the callback registered for its type. Every connection has its own
/// thread, requests of one connection are processed strictly in order.

constexpr auto ANTCP_SERVER_VERSION = "2.0.0.0";

/// Maximum size (type + payload) of an incoming packet.
constexpr int ANTCP_MAX_PACKET_SIZE = 8192;

/// Maximum size (type + payload) of an outgoing packet.
constexpr size_t ANTCP_MAX_RESPONSE_SIZE = 64u * 1024u * 1024u;

// type used in the payload to specify the size of a packet
using AnTcpSizeType = int32_t;

// type used to identify the type of a message
using AnTcpMessageType = unsigned char;

enum class AnTcpError
{
    Success,
    Win32WsaStartupFailed,
    GetAddrInfoFailed,
    SocketCreationFailed,
    SocketBindingFailed,
    SocketListeningFailed
};

class ClientHandler;
class AnTcpServer;

using AnTcpMessageCallback = std::function<void(ClientHandler*, AnTcpMessageType, const void*, int)>;
using AnTcpClientCallback = std::function<void(ClientHandler*)>;
using AnTcpRejectCallback = std::function<void(size_t activeClients)>;

/// One connected client. Owns the socket and the receive thread.
class ClientHandler
{
    friend class AnTcpServer;

    size_t Id;
    AnTcpSocket Socket;
    sockaddr_storage Address;
    AnTcpServer* Server;
    std::atomic<bool> Running;
    std::atomic<bool> Finished;
    std::mutex SendMutex;
    std::vector<char> SendBuffer;
    std::thread Thread;

public:
    ClientHandler(size_t id, AnTcpSocket socket, const sockaddr_storage& address, AnTcpServer* server) noexcept;
    ~ClientHandler();

    ClientHandler(const ClientHandler&) = delete;
    ClientHandler& operator=(const ClientHandler&) = delete;

    /// Unique id for the lifetime of the server (never reused).
    size_t GetId() const noexcept { return Id; }

    /// True once the receive loop has ended (connection closed or Disconnect() called).
    bool IsDisconnected() const noexcept { return Finished.load(std::memory_order_acquire); }

    /// Send a single value (by copy).
    template <typename T>
    bool SendDataVar(AnTcpMessageType type, const T data) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return SendData(type, &data, sizeof(T));
    }

    /// Send a struct (by pointer, sizeof(T) bytes). For arrays use SendData with explicit size.
    template <typename T>
    bool SendDataPtr(AnTcpMessageType type, const T* data) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return SendData(type, data, sizeof(T));
    }

    /// Send one framed packet (header + payload in a single send call). Thread-safe. A failed send (peer gone or
    /// not reading for 30s) disconnects the client.
    bool SendData(AnTcpMessageType type, const void* data, size_t size) noexcept;

    /// Close the connection. The receive thread ends and the disconnect callback fires on it.
    void Disconnect() noexcept;

    std::string GetIpAddress() const;
    unsigned short GetPort() const noexcept;
    unsigned short GetAddressFamily() const noexcept { return Address.ss_family; }

private:
    void Start();

    /// Client receive loop: reassembles packets and dispatches them to the callbacks.
    void Listen() noexcept;

    bool SendAll(const char* data, size_t size) noexcept;
};

/// Blocking TCP server. Run() serves until Stop() is called (Stop is async-signal-safe).
class AnTcpServer
{
    friend class ClientHandler;

    std::string Ip;
    std::string Port;
    std::atomic<bool> ShouldExit;
    std::atomic<unsigned short> BoundPort;
    std::atomic<size_t> NextClientId;
    std::atomic<size_t> MaxClients{0};
    std::atomic<long long> IdleTimeoutMs{0};
    std::atomic<size_t> RejectedClients{0};
    AnTcpSocket ListenSocket;

    std::mutex ClientsMutex;
    std::list<std::unique_ptr<ClientHandler>> Clients;

    std::unordered_map<AnTcpMessageType, AnTcpMessageCallback> Callbacks;
    AnTcpClientCallback OnClientConnected;
    AnTcpClientCallback OnClientDisconnected;
    AnTcpRejectCallback OnClientRejected;

public:
    AnTcpServer(const std::string& ip, unsigned short port);
    AnTcpServer(const std::string& ip, const std::string& port);
    ~AnTcpServer();

    AnTcpServer(const AnTcpServer&) = delete;
    AnTcpServer& operator=(const AnTcpServer&) = delete;

    // Callbacks must be registered before Run() and must not change while the server is running.

    void SetOnClientConnected(AnTcpClientCallback handler) { OnClientConnected = std::move(handler); }
    void SetOnClientDisconnected(AnTcpClientCallback handler) { OnClientDisconnected = std::move(handler); }

    /// Called (on the accept thread) when a connection is refused because of the client limit.
    void SetOnClientRejected(AnTcpRejectCallback handler) { OnClientRejected = std::move(handler); }

    /// Maximum number of connected clients, connections beyond it are closed right after accept (0 = unlimited).
    /// Every client costs a thread, the limit keeps a connection flood from exhausting them.
    void SetMaxClients(size_t maxClients) noexcept { MaxClients.store(maxClients, std::memory_order_relaxed); }

    /// Disconnect clients that haven't sent anything for this long (0 = never). Vanished peers are detected by
    /// TCP keepalive (~2 minutes) regardless, this also drops connected but silent clients.
    void SetIdleTimeout(std::chrono::milliseconds timeout) noexcept
    {
        IdleTimeoutMs.store(std::max<long long>(0, timeout.count()), std::memory_order_relaxed);
    }

    /// Connections refused because of the client limit.
    size_t GetRejectedCount() const noexcept { return RejectedClients.load(std::memory_order_relaxed); }

    bool AddCallback(AnTcpMessageType type, AnTcpMessageCallback callback)
    {
        return Callbacks.try_emplace(type, std::move(callback)).second;
    }

    bool RemoveCallback(AnTcpMessageType type) noexcept { return Callbacks.erase(type) > 0; }

    /// Request the server to stop. Returns immediately, Run() returns within ~100ms.
    /// Only touches an atomic flag, so it is safe to call from signal handlers and other threads.
    void Stop() noexcept { ShouldExit.store(true, std::memory_order_release); }

    bool IsStopping() const noexcept { return ShouldExit.load(std::memory_order_acquire); }

    /// Port the server is listening on (useful when started with port 0), 0 if not listening.
    unsigned short GetBoundPort() const noexcept { return BoundPort.load(std::memory_order_acquire); }

    size_t GetClientCount();

    /// Starts the server (blocking). Returns an error code if the server couldn't be started. Returns right away
    /// if Stop() was already called.
    AnTcpError Run() noexcept;

    /// Process one packet (type byte + payload) for a client exactly like the receive loop does: calls the
    /// registered callback or answers unknown types with an empty payload. Returns false if the client should be
    /// dropped. Public for in-process use (tests, fuzzing).
    bool Dispatch(ClientHandler* handler, const char* packet, AnTcpSizeType size) noexcept;

private:
    void CleanupClients(bool all) noexcept;
};
