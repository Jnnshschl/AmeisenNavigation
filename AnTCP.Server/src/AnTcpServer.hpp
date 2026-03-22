#pragma once

// toggle debug output here
#if 0
#define DEBUG_ONLY(x) x
#define BENCHMARK(x) x
#else
#define DEBUG_ONLY(x)
#define BENCHMARK(x)
#endif

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

constexpr auto ANTCP_SERVER_VERSION = "1.2.1.0";
constexpr auto ANTCP_MAX_PACKET_SIZE = 8192;

// type used in the payload to specify the size of a packet
typedef int AnTcpSizeType;

// type used to identify the type of a message
typedef unsigned char AnTcpMessageType;

enum class AnTcpError
{
    Success,
    Win32WsaStartupFailed,
    GetAddrInfoFailed,
    SocketCreationFailed,
    SocketBindingFailed,
    SocketListeningFailed
};

// Forward declaration
class ClientHandler;

/// Function pointer types - no std::function overhead on the hot path.
using AnTcpMessageCallback = void(*)(ClientHandler*, AnTcpMessageType, const void*, int);
using AnTcpClientCallback = void(*)(ClientHandler*);

class ClientHandler
{
private:
    size_t Id;
    SOCKET Socket;
    SOCKADDR_IN SocketInfo;
    std::atomic<bool>& ShouldExit;
    std::unordered_map<AnTcpMessageType, AnTcpMessageCallback>* Callbacks;

    std::atomic<bool> IsActive;
    std::unique_ptr<std::thread> Thread;

    AnTcpClientCallback OnClientConnected;
    AnTcpClientCallback OnClientDisconnected;

public:
    ClientHandler
    (
        SOCKET socket,
        const SOCKADDR_IN& socketInfo,
        std::atomic<bool>& shouldExit,
        std::unordered_map<AnTcpMessageType, AnTcpMessageCallback>* callbacks,
        AnTcpClientCallback onClientConnected = nullptr,
        AnTcpClientCallback onClientDisconnected = nullptr
    )
        : Id(static_cast<unsigned int>(socketInfo.sin_addr.S_un.S_addr + socketInfo.sin_port)),
        Socket(socket),
        SocketInfo(socketInfo),
        ShouldExit(shouldExit),
        Callbacks(callbacks),
        IsActive(true),
        Thread(std::make_unique<std::thread>(&ClientHandler::Listen, this)),
        OnClientConnected(onClientConnected),
        OnClientDisconnected(onClientDisconnected)
    {
    }

    ~ClientHandler()
    {
        DEBUG_ONLY(std::cout << "[" << Id << "] " << "Deleting Handler: " << Id << std::endl);

        Disconnect();

        if (Thread && Thread->joinable())
        {
            Thread->join();
        }
    }

    ClientHandler(const ClientHandler&) = delete;
    ClientHandler& operator=(const ClientHandler&) = delete;

    constexpr auto GetId() const noexcept { return Id; }

    bool IsDisconnected() const noexcept { return !IsActive.load(std::memory_order_acquire); }

    /// Send a single value (by copy). Use SendDataPtr for structs.
    template<typename T>
    bool SendDataVar(AnTcpMessageType type, const T data) const noexcept
    {
        return SendData(type, &data, sizeof(T));
    }

    /// Send a struct (by pointer, sizeof(T) bytes). For arrays use SendData with explicit size.
    template<typename T>
    bool SendDataPtr(AnTcpMessageType type, const T* data) const noexcept
    {
        return SendData(type, data, sizeof(T));
    }

    /// Send raw data to the client. Coalesced into a single send() call.
    inline bool SendData(AnTcpMessageType type, const void* data, size_t size) const noexcept
    {
        const AnTcpSizeType packetSize = static_cast<AnTcpSizeType>(size + sizeof(AnTcpMessageType));
        constexpr size_t HEADER_SIZE = sizeof(AnTcpSizeType) + sizeof(AnTcpMessageType);
        const size_t totalSize = HEADER_SIZE + size;

        if (size > ANTCP_MAX_PACKET_SIZE)
            return false;

        // Stack buffer for small packets; heap fallback for large path data
        constexpr size_t STACK_THRESHOLD = 512;
        char stackBuf[HEADER_SIZE + STACK_THRESHOLD];
        char* heapBuf = nullptr;
        char* buf;

        if (totalSize <= sizeof(stackBuf))
        {
            buf = stackBuf;
        }
        else
        {
            heapBuf = new (std::nothrow) char[totalSize];
            if (!heapBuf)
                return false;
            buf = heapBuf;
        }

        memcpy(buf, &packetSize, sizeof(AnTcpSizeType));
        memcpy(buf + sizeof(AnTcpSizeType), &type, sizeof(AnTcpMessageType));
        memcpy(buf + HEADER_SIZE, data, size);

        bool ok = send(Socket, buf, static_cast<int>(totalSize), 0) != SOCKET_ERROR;
        delete[] heapBuf;
        return ok;
    }

    inline void Disconnect() noexcept
    {
        if (!IsActive.load(std::memory_order_acquire))
            return;

        IsActive.store(false, std::memory_order_release);

        if (OnClientDisconnected)
        {
            OnClientDisconnected(this);
        }

        closesocket(Socket);
        Socket = INVALID_SOCKET;
    }

    inline std::string GetIpAddress() const
    {
        char ipAddressBuffer[128]{ 0 };
        inet_ntop(AF_INET, &SocketInfo.sin_addr, ipAddressBuffer, 128);
        return std::string(ipAddressBuffer);
    }

    constexpr unsigned short GetPort() const noexcept
    {
        return SocketInfo.sin_port;
    }

    constexpr unsigned short GetAddressFamily() const noexcept
    {
        return SocketInfo.sin_family;
    }

private:
    /// Client receive loop: reassembles packets and dispatches to callbacks.
    void Listen() noexcept;

    /// Dispatch a complete packet to its registered callback.
    inline bool ProcessPacket(const char* data, AnTcpSizeType size) noexcept
    {
        auto msgType = static_cast<AnTcpMessageType>(data[0]);
        auto it = Callbacks->find(msgType);

        if (it != Callbacks->end())
        {
            BENCHMARK(const auto packetStart = std::chrono::high_resolution_clock::now());

            it->second(this, msgType, data + sizeof(AnTcpMessageType), size - sizeof(AnTcpMessageType));

            BENCHMARK(std::cout << "[" << Id << "] " << "Processing packet of type \""
                << std::to_string(msgType) << "\" took: "
                << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - packetStart) << std::endl);

            return true;
        }

        DEBUG_ONLY(std::cout << "[" << Id << "] " << "\"" << std::to_string(msgType)
            << "\" is an unknown message type..." << std::endl);

        return false;
    }
};

class AnTcpServer
{
private:
    std::string Ip;
    std::string Port;
    std::atomic<bool> ShouldExit;
    SOCKET ListenSocket;
    std::vector<std::unique_ptr<ClientHandler>> Clients;
    std::unordered_map<AnTcpMessageType, AnTcpMessageCallback> Callbacks;

    AnTcpClientCallback OnClientConnected;
    AnTcpClientCallback OnClientDisconnected;

public:
    AnTcpServer(const std::string& ip, unsigned short port)
        : Ip(ip),
        Port(std::to_string(port)),
        ShouldExit(false),
        ListenSocket(INVALID_SOCKET),
        Clients(),
        Callbacks(),
        OnClientConnected(nullptr),
        OnClientDisconnected(nullptr)
    {
    }

    AnTcpServer(const std::string& ip, const std::string& port)
        : Ip(ip),
        Port(port),
        ShouldExit(false),
        ListenSocket(INVALID_SOCKET),
        Clients(),
        Callbacks(),
        OnClientConnected(nullptr),
        OnClientDisconnected(nullptr)
    {
    }

    AnTcpServer(const AnTcpServer&) = delete;
    AnTcpServer& operator=(const AnTcpServer&) = delete;

    inline void SetOnClientConnected(AnTcpClientCallback handlerFunction)
    {
        OnClientConnected = handlerFunction;
    }

    inline void SetOnClientDisconnected(AnTcpClientCallback handlerFunction)
    {
        OnClientDisconnected = handlerFunction;
    }

    inline bool AddCallback(AnTcpMessageType type, AnTcpMessageCallback callback)
    {
        auto [it, inserted] = Callbacks.try_emplace(type, callback);
        return inserted;
    }

    inline bool RemoveCallback(AnTcpMessageType type) noexcept
    {
        return Callbacks.erase(type) > 0;
    }

    inline void Stop() noexcept
    {
        ShouldExit = true;
        SocketCleanup();
    }

    /// Starts the server (blocking). Returns error code on failure.
    AnTcpError Run() noexcept;

private:
    constexpr void SocketCleanup() noexcept
    {
        if (ListenSocket != INVALID_SOCKET)
        {
            closesocket(ListenSocket);
            ListenSocket = INVALID_SOCKET;
        }
    }

    void ClientCleanup() noexcept
    {
        std::erase_if(Clients, [](const auto& c) { return c && c->IsDisconnected(); });
    }
};
