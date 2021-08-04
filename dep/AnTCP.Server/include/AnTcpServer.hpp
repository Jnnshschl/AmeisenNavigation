#pragma once

// toggle debug output here
#if 1
#define DEBUG_ONLY(x) x
#define BENCHMARK(x) x
#else
#define DEBUG_ONLY(x)
#define BENCHMARK(x)
#endif

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

constexpr auto ANTCP_SERVER_VERSION = "1.1.0.0";
constexpr auto ANTCP_BUFFER_LENGTH = 512;
constexpr auto ANTCP_MAX_PACKET_SIZE = 256;

// type used in the payload to specify the size of a packet
typedef int AnTcpSizeType;

// type used to identy the the type of a message
typedef char AnTcpMessageType;

enum class AnTcpError
{
    Success,
    Win32WsaStartupFailed,
    GetAddrInfoFailed,
    SocketCreationFailed,
    SocketBindingFailed,
    SocketListeningFailed
};

class ClientHandler
{
private:
    size_t Id;
    SOCKET Socket;
    SOCKADDR_IN SocketInfo;
    std::atomic<bool>& ShouldExit;
    std::unordered_map <AnTcpMessageType, std::function<void(ClientHandler*, AnTcpMessageType, const void*, int)>>* Callbacks;

    unsigned int UniqueId;
    bool IsActive;
    std::thread* Thread;

    std::function<void(ClientHandler*)>* OnClientConnected;
    std::function<void(ClientHandler*)>* OnClientDisconnected;

public:
    /// <summary>
    /// Create a new client handler, which processes incoming data and fires callbacks.
    /// </summary>
    /// <param name="id">Id of the handler, used for logging.</param>
    /// <param name="socket">Socket where the client was accepted on.</param>
    /// <param name="socketInfo">Information about the socket, used for logging.</param>
    /// <param name="shouldExit">Atomic bool to notify the handler that the server is going to shutdown.</param>
    /// <param name="callbacks">Pointer to the server callback map.</param>
    ClientHandler
    (
        size_t id,
        SOCKET socket,
        const SOCKADDR_IN& socketInfo,
        std::atomic<bool>& shouldExit,
        std::unordered_map <AnTcpMessageType, std::function<void(ClientHandler*, AnTcpMessageType, const void*, int)>>* callbacks,
        std::function<void(ClientHandler*)>* onClientConnected = nullptr,
        std::function<void(ClientHandler*)>* onClientDisconnected = nullptr
    )
        : IsActive(true),
        Id(id),
        Socket(socket),
        SocketInfo(socketInfo),
        UniqueId(static_cast<unsigned int>(socketInfo.sin_addr.S_un.S_addr + socketInfo.sin_port)),
        ShouldExit(shouldExit),
        Callbacks(callbacks),
        Thread(new std::thread(&ClientHandler::Listen, this)),
        OnClientConnected(onClientConnected),
        OnClientDisconnected(onClientDisconnected)
    {}

    ~ClientHandler()
    {
        DEBUG_ONLY(std::cout << "[" << Id << "] " << "Deleting Handler: " << Id << std::endl);

        Disconnect();

        Thread->join();
        delete Thread;
    }

    ClientHandler(const ClientHandler&) = delete;
    ClientHandler& operator=(const ClientHandler&) = delete;

    /// <summary>
    /// Get the AnTCP handler id.
    /// </summary>
    constexpr int GetId() noexcept { return Id; }

    /// <summary>
    /// Get a unique id based on ip and port of the handler.
    /// </summary>
    constexpr unsigned int GetUniqueId() noexcept { return UniqueId; }

    /// <summary>
    /// Used to delete old disconnected clients.
    /// </summary>
    constexpr bool IsConnected() const noexcept { return !IsActive; }

    /// <summary>
    /// Send data to the client. Size will be sizeof(T).
    /// Use this to send single primitives not structs.
    /// Use the pointer method for structs to avoid them 
    /// beeing copied instead of this one.
    /// </summary>
    /// <param name="type">Message type (1 byte)</param>
    /// <param name="data">Data to send.</param>
    /// <returns>True if data was sent, false if not.</returns>
    template<typename T>
    constexpr bool SendDataVar(AnTcpMessageType type, const T data) const noexcept
    {
        return SendData(type, &data, sizeof(T));
    }

    /// <summary>
    /// Send data to the client. Size will be sizeof(T).
    /// Use this to send structures.
    /// Do not use this with arrays as only the first
    /// element will be sent, use the method with the 
    /// size parameter instead.
    /// </summary>
    /// <param name="type">Message type (1 byte)</param>
    /// <param name="data">Data to send.</param>
    /// <returns>True if data was sent, false if not.</returns>
    template<typename T>
    constexpr bool SendDataPtr(AnTcpMessageType type, const T* data) const noexcept
    {
        return SendData(type, data, sizeof(T));
    }

    /// <summary>
    /// Send data to the client.
    /// </summary>
    /// <param name="type">Message type (1 byte)</param>
    /// <param name="data">Data to send.</param>
    /// <param name="size">Size of the data to send.</param>
    /// <returns>True if data was sent, false if not.</returns>
    inline bool SendData(AnTcpMessageType type, const void* data, size_t size) const noexcept
    {
        const int packetSize = size + static_cast<int>(sizeof(AnTcpMessageType));
        return send(Socket, reinterpret_cast<const char*>(&packetSize), sizeof(decltype(packetSize)), 0) != SOCKET_ERROR
            && send(Socket, &type, sizeof(AnTcpMessageType), 0) != SOCKET_ERROR
            && send(Socket, static_cast<const char*>(data), size, 0) != SOCKET_ERROR;
    }

    /// <summary>
    /// Disconnect the client.
    /// </summary>
    inline void Disconnect() noexcept
    {
        if (OnClientDisconnected)
        {
            (*OnClientDisconnected)(this);
        }

        closesocket(Socket);
        Socket = INVALID_SOCKET;
        IsActive = false;
    }

    /// <summary>
    /// Get the clients ip address.
    /// </summary>
    /// <returns>IP address as string.</returns>
    inline std::string GetIpAddress() noexcept
    {
        char ipAddressBuffer[128]{ 0 };
        inet_ntop(AF_INET, &SocketInfo.sin_addr, ipAddressBuffer, 128);
        return std::string(ipAddressBuffer);
    }

    /// <summary>
    /// Get the client connection port.
    /// </summary>
    constexpr unsigned short GetPort() noexcept
    {
        return SocketInfo.sin_port;
    }

    constexpr unsigned short GetAddressFamily() noexcept
    {
        return SocketInfo.sin_family;
    }

private:
    /// <summary>
    /// Routine for new clients, packets will be reassembled
    /// and processed in here.
    /// </summary>
    void Listen() noexcept;

    /// <summary>
    /// Search whether there is a callback or not, 
    /// if there is one, fire it with the data.
    /// </summary>
    /// <param name="type">Message type.</param>
    /// <param name="data">Data received.</param>
    /// <returns>True if callback was fired, false if not.</returns>
    inline bool ProcessPacket(const char* data, int size) noexcept
    {
        const AnTcpMessageType msgType = *reinterpret_cast<const AnTcpMessageType*>(data);

        if ((*Callbacks).contains(msgType))
        {
            // measure packet processing time in debug mode
            BENCHMARK(const auto packetStart = std::chrono::high_resolution_clock::now());

            // fire the callback with the raw data
            (*Callbacks)[msgType](this, msgType, data + static_cast<int>(sizeof(AnTcpMessageType)), size - static_cast<int>(sizeof(AnTcpMessageType)));

            BENCHMARK(std::cout << "[" << Id << "] " << "Processing packet of type \""
                << std::to_string(*reinterpret_cast<const AnTcpMessageType*>(data)) << "\" took: "
                << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - packetStart) << std::endl);

            return true;
        }

        DEBUG_ONLY(std::cout << "[" << Id << "] " << "\"" << std::to_string(*reinterpret_cast<const AnTcpMessageType*>(data))
            << "\" is an unknown message type, disconnecting client..." << std::endl);

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
    std::vector<ClientHandler*> Clients;
    std::unordered_map <AnTcpMessageType, std::function<void(ClientHandler*, AnTcpMessageType, const void*, int)>> Callbacks;

    std::function<void(ClientHandler*)> OnClientConnected;
    std::function<void(ClientHandler*)> OnClientDisconnected;

public:
    /// <summary>
    /// Create anew instace of the AnTcpServer to start a new server.
    /// </summary>
    /// <param name="ip">Ip address to bind the server to.</param>
    /// <param name="port">Port to listen on.</param>
    AnTcpServer(const std::string& ip, unsigned short port)
        : Ip(ip),
        Port(std::to_string(port)),
        ShouldExit(false),
        ListenSocket(INVALID_SOCKET),
        Clients(),
        Callbacks(),
        OnClientConnected(nullptr),
        OnClientDisconnected(nullptr)
    {}

    /// <summary>
    /// Create anew instace of the AnTcpServer to start a new server.
    /// </summary>
    /// <param name="ip">Ip address to bind the server to.</param>
    /// <param name="port">Port to listen on.</param>
    AnTcpServer(const std::string& ip, const std::string& port)
        : Ip(ip),
        Port(port),
        ShouldExit(false),
        ListenSocket(INVALID_SOCKET),
        Clients(),
        Callbacks(),
        OnClientConnected(nullptr),
        OnClientDisconnected(nullptr)
    {}

    AnTcpServer(const AnTcpServer&) = delete;
    AnTcpServer& operator=(const AnTcpServer&) = delete;

    /// <summary>
    /// Set the event handler for the OnClientConnected event.
    /// </summary>
    /// <param name="handlerFunction">Handler function, can be nulltpr</param>
    inline void SetOnClientConnected(std::function<void(ClientHandler*)> handlerFunction)
    {
        OnClientConnected = handlerFunction;
    }

    /// <summary>
    /// Set the event handler for the OnClientDisconnected event.
    /// </summary>
    /// <param name="handlerFunction">Handler function, can be nulltpr</param>
    inline void SetOnClientDisconnected(std::function<void(ClientHandler*)> handlerFunction)
    {
        OnClientDisconnected = handlerFunction;
    }

    /// <summary>
    /// Add a new callback for a message type, will be fired when the server received a message of that type.
    /// </summary>
    /// <param name="type">Message type.</param>
    /// <param name="callback">Function to handle the message.</param>
    /// <returns>True if callback was added, false if there is already a callback for this message type.</returns>
    inline bool AddCallback(AnTcpMessageType type, std::function<void(ClientHandler*, AnTcpMessageType, const void*, int)> callback) noexcept
    {
        if (!Callbacks.contains(type))
        {
            Callbacks[type] = callback;
            return true;
        }

        return false;
    }

    /// <summary>
    /// Remove a callback for the given message type.
    /// </summary>
    /// <param name="type">Message type.</param>
    /// <returns>True if callback was removed, false if there was no callback for this message type.</returns>
    inline bool RemoveCallback(AnTcpMessageType type) noexcept
    {
        if (Callbacks.contains(type))
        {
            Callbacks.erase(type);
            return true;
        }

        return false;
    }

    /// <summary>
    /// Stops the server.
    /// </summary>
    inline void Stop() noexcept
    {
        ShouldExit = true;
        closesocket(ListenSocket);
        ListenSocket = INVALID_SOCKET;
    }

    /// <summary>
    /// Starts the server, blocking. You may want to start it on a thread.
    /// </summary>
    /// <returns>Status of the server, 1 if an error occured, 0 if not</returns>
    AnTcpError Run() noexcept;

private:
    /// <summary>
    /// Delete old clients that are not running.
    /// </summary>
    constexpr void ClientCleanup() noexcept
    {
        for (size_t i = 0; i < Clients.size(); ++i)
        {
            if (Clients[i]->IsConnected())
            {
                if (Clients[i])
                {
                    if (OnClientDisconnected)
                    {
                        OnClientDisconnected(Clients[i]);
                    }

                    delete Clients[i];
                }

                Clients.erase(Clients.begin() + i);
            }
        }
    }
};