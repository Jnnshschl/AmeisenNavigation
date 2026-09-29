#include "AnTcpServer.hpp"

#include <algorithm>
#include <climits>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")

namespace {
using PollFd = WSAPOLLFD;
constexpr short POLL_READ = POLLRDNORM;
constexpr int SEND_FLAGS = 0;

inline int PollSockets(PollFd* fds, unsigned long count, int timeoutMs) noexcept { return WSAPoll(fds, count, timeoutMs); }
inline void CloseSocket(AnTcpSocket s) noexcept { closesocket(s); }
inline void ShutdownSocket(AnTcpSocket s) noexcept { shutdown(s, SD_BOTH); }
inline bool IsInterrupted() noexcept { return WSAGetLastError() == WSAEINTR; }

struct NetworkSession
{
    bool ok;
    NetworkSession() noexcept
    {
        WSADATA wsaData{};
        ok = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    }
    ~NetworkSession()
    {
        if (ok)
        {
            WSACleanup();
        }
    }
};
} // namespace
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <unistd.h>

namespace {
using PollFd = pollfd;
constexpr short POLL_READ = POLLIN;
#ifdef MSG_NOSIGNAL
constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#else
constexpr int SEND_FLAGS = 0;
#endif

inline int PollSockets(PollFd* fds, unsigned long count, int timeoutMs) noexcept
{
    return poll(fds, static_cast<nfds_t>(count), timeoutMs);
}
inline void CloseSocket(AnTcpSocket s) noexcept { close(s); }
inline void ShutdownSocket(AnTcpSocket s) noexcept { shutdown(s, SHUT_RDWR); }
inline bool IsInterrupted() noexcept { return errno == EINTR; }

struct NetworkSession
{
    bool ok = true;
};
} // namespace
#endif

namespace {
constexpr int ACCEPT_POLL_INTERVAL_MS = 100;
#ifdef _WIN32
// shutdown() doesn't wake a WSAPoll()/recv() that is already waiting, Disconnect()/Stop() rely on this interval.
constexpr long long CLIENT_POLL_INTERVAL_MS = 100;
#else
// shutdown() wakes a waiting poll() right away, the interval only bounds how late a changed idle timeout applies.
constexpr long long CLIENT_POLL_INTERVAL_MS = 1000;
#endif
constexpr size_t HEADER_SIZE = sizeof(AnTcpSizeType);
constexpr size_t RECEIVE_BUFFER_SIZE = 2 * (HEADER_SIZE + ANTCP_MAX_PACKET_SIZE);

inline AnTcpSizeType ReadLittleEndian32(const char* data) noexcept
{
    const auto* b = reinterpret_cast<const unsigned char*>(data);
    return static_cast<AnTcpSizeType>(static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8)
                                      | (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24));
}

inline void WriteLittleEndian32(char* out, uint32_t value) noexcept
{
    out[0] = static_cast<char>(value & 0xFF);
    out[1] = static_cast<char>((value >> 8) & 0xFF);
    out[2] = static_cast<char>((value >> 16) & 0xFF);
    out[3] = static_cast<char>((value >> 24) & 0xFF);
}

inline unsigned short PortOf(const sockaddr_storage& address) noexcept
{
    if (address.ss_family == AF_INET)
    {
        return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
    }

    if (address.ss_family == AF_INET6)
    {
        return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
    }

    return 0;
}

inline void ConfigureClientSocket(AnTcpSocket socket) noexcept
{
    int flag = 1;
    // Disable Nagle's algorithm, responses are small and latency matters.
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
    // Detect dead peers (bot crashed without closing the connection).
    setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&flag), sizeof(flag));
#ifdef SO_NOSIGPIPE
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&flag), sizeof(flag));
#endif

    // A peer that stops reading would block send() forever once the buffers are full (keepalive doesn't help, the
    // peer still ACKs with a zero window): give up after 30s and drop the client.
#ifdef _WIN32
    const DWORD sendTimeout = 30000;
#else
    const timeval sendTimeout{30, 0};
#endif
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeout), sizeof(sendTimeout));

    // Probe after 60s of silence, every 10s, give up after 6 unanswered probes: a vanished peer frees its thread
    // after ~2 minutes instead of the OS default of ~2 hours.
    const int keepIdle = 60;
    const int keepInterval = 10;
    const int keepCount = 6;
#if defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL) && defined(TCP_KEEPCNT)
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPIDLE, reinterpret_cast<const char*>(&keepIdle), sizeof(keepIdle));
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPINTVL, reinterpret_cast<const char*>(&keepInterval),
               sizeof(keepInterval));
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPCNT, reinterpret_cast<const char*>(&keepCount), sizeof(keepCount));
#elif defined(TCP_KEEPALIVE) && !defined(_WIN32)
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPALIVE, reinterpret_cast<const char*>(&keepIdle), sizeof(keepIdle));
    (void)keepInterval;
    (void)keepCount;
#else
    (void)keepIdle;
    (void)keepInterval;
    (void)keepCount;
#endif
}
} // namespace

// ── ClientHandler ────────────────────────────────────────────────────────────

ClientHandler::ClientHandler(size_t id, AnTcpSocket socket, const sockaddr_storage& address,
                             AnTcpServer* server) noexcept
    : Id(id),
      Socket(socket),
      Address(address),
      Server(server),
      Running(true),
      Finished(false)
{
}

ClientHandler::~ClientHandler()
{
    Disconnect();

    if (Thread.joinable())
    {
        if (Thread.get_id() == std::this_thread::get_id())
        {
            Thread.detach();
        }
        else
        {
            Thread.join();
        }
    }

    if (Socket != ANTCP_INVALID_SOCKET)
    {
        CloseSocket(Socket);
        Socket = ANTCP_INVALID_SOCKET;
    }
}

void ClientHandler::Start()
{
    // Started only after the handler is fully constructed and registered.
    Thread = std::thread(&ClientHandler::Listen, this);
}

void ClientHandler::Disconnect() noexcept
{
    // shutdown() tells the peer right away and wakes a waiting poll() on POSIX, on Windows Listen() notices
    // Running == false within one poll interval. The socket itself is closed in the destructor after the thread was joined, so the
    // descriptor can't be reused while the thread still uses it.
    if (Running.exchange(false, std::memory_order_acq_rel) && Socket != ANTCP_INVALID_SOCKET)
    {
        ShutdownSocket(Socket);
    }
}

std::string ClientHandler::GetIpAddress() const
{
    char buffer[INET6_ADDRSTRLEN]{};

    if (Address.ss_family == AF_INET)
    {
        const auto* addr = reinterpret_cast<const sockaddr_in*>(&Address);
        inet_ntop(AF_INET, &addr->sin_addr, buffer, sizeof(buffer));
    }
    else if (Address.ss_family == AF_INET6)
    {
        const auto* addr = reinterpret_cast<const sockaddr_in6*>(&Address);
        inet_ntop(AF_INET6, &addr->sin6_addr, buffer, sizeof(buffer));
    }

    return buffer;
}

unsigned short ClientHandler::GetPort() const noexcept { return PortOf(Address); }

bool ClientHandler::SendAll(const char* data, size_t size) noexcept
{
    while (size > 0)
    {
        const int chunk = static_cast<int>(std::min<size_t>(size, INT_MAX));
        const auto sent = send(Socket, data, chunk, SEND_FLAGS);

        if (sent <= 0)
        {
            if (sent < 0 && IsInterrupted())
            {
                continue;
            }

            return false;
        }

        data += sent;
        size -= static_cast<size_t>(sent);
    }

    return true;
}

bool ClientHandler::SendData(AnTcpMessageType type, const void* data, size_t size) noexcept
{
    const size_t packetSize = size + sizeof(AnTcpMessageType);

    if (packetSize > ANTCP_MAX_RESPONSE_SIZE || (size > 0 && !data))
    {
        return false;
    }

    bool sent = false;

    try
    {
        const std::lock_guard lock(SendMutex);

        // Header and payload go out with a single send() so small responses are one TCP segment.
        SendBuffer.resize(HEADER_SIZE + packetSize);
        WriteLittleEndian32(SendBuffer.data(), static_cast<uint32_t>(packetSize));
        SendBuffer[HEADER_SIZE] = static_cast<char>(type);

        if (size > 0)
        {
            std::memcpy(SendBuffer.data() + HEADER_SIZE + sizeof(AnTcpMessageType), data, size);
        }

        sent = SendAll(SendBuffer.data(), SendBuffer.size());
    }
    catch (...)
    {
    }

    // The client never gets this response (or only part of it, the stream is out of sync) and a peer that stopped
    // reading would hold its thread forever: drop it. Handlers don't need to check the result.
    if (!sent)
    {
        Disconnect();
    }

    return sent;
}

void ClientHandler::Listen() noexcept
{
    try
    {
        if (Server->OnClientConnected)
        {
            Server->OnClientConnected(this);
        }

        std::vector<char> buffer(RECEIVE_BUFFER_SIZE);
        size_t filled = 0;
        bool ok = true;
        auto lastActivity = std::chrono::steady_clock::now();

        while (ok && Running.load(std::memory_order_acquire) && !Server->IsStopping())
        {
            long long timeoutMs = CLIENT_POLL_INTERVAL_MS;

            if (const long long idleTimeoutMs = Server->IdleTimeoutMs.load(std::memory_order_relaxed);
                idleTimeoutMs > 0)
            {
                const long long idleMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - lastActivity)
                                             .count();

                if (idleMs >= idleTimeoutMs)
                {
                    break; // idle for too long
                }

                timeoutMs = std::min(timeoutMs, idleTimeoutMs - idleMs);
            }

            // Wait for data with a timeout instead of blocking in recv(), see CLIENT_POLL_INTERVAL_MS.
            PollFd pfd{};
            pfd.fd = Socket;
            pfd.events = POLL_READ;
            const int ready = PollSockets(&pfd, 1, static_cast<int>(timeoutMs));

            if (ready == 0 || (ready < 0 && IsInterrupted()))
            {
                continue;
            }

            if (ready < 0 || (pfd.revents & (POLLERR | POLLNVAL)))
            {
                break;
            }

            const auto received =
                recv(Socket, buffer.data() + filled, static_cast<int>(buffer.size() - filled), 0);

            if (received <= 0)
            {
                if (received < 0 && IsInterrupted())
                {
                    continue;
                }

                break; // closed by peer, shutdown or error
            }

            filled += static_cast<size_t>(received);
            lastActivity = std::chrono::steady_clock::now();

            // Dispatch every complete packet in the buffer (clients may pipeline requests).
            size_t offset = 0;

            while (filled - offset >= HEADER_SIZE)
            {
                const AnTcpSizeType packetSize = ReadLittleEndian32(buffer.data() + offset);

                if (packetSize <= 0 || packetSize > ANTCP_MAX_PACKET_SIZE)
                {
                    ok = false; // protocol violation, drop the client
                    break;
                }

                if (filled - offset - HEADER_SIZE < static_cast<size_t>(packetSize))
                {
                    break; // incomplete, wait for more data
                }

                if (!Server->Dispatch(this, buffer.data() + offset + HEADER_SIZE, packetSize))
                {
                    ok = false;
                    break;
                }

                offset += HEADER_SIZE + static_cast<size_t>(packetSize);
            }

            if (offset > 0)
            {
                std::memmove(buffer.data(), buffer.data() + offset, filled - offset);
                filled -= offset;
            }
        }
    }
    catch (...)
    {
    }

    Running.store(false, std::memory_order_release);

    try
    {
        if (Server->OnClientDisconnected)
        {
            Server->OnClientDisconnected(this);
        }
    }
    catch (...)
    {
    }

    Finished.store(true, std::memory_order_release);
}

// ── AnTcpServer ──────────────────────────────────────────────────────────────

AnTcpServer::AnTcpServer(const std::string& ip, unsigned short port) : AnTcpServer(ip, std::to_string(port)) {}

AnTcpServer::AnTcpServer(const std::string& ip, const std::string& port)
    : Ip(ip),
      Port(port),
      ShouldExit(false),
      BoundPort(0),
      NextClientId(1),
      ListenSocket(ANTCP_INVALID_SOCKET)
{
}

AnTcpServer::~AnTcpServer()
{
    Stop();
    CleanupClients(true);
}

size_t AnTcpServer::GetClientCount()
{
    const std::lock_guard lock(ClientsMutex);
    return static_cast<size_t>(std::count_if(Clients.begin(), Clients.end(),
                                             [](const auto& c) { return !c->IsDisconnected(); }));
}

bool AnTcpServer::Dispatch(ClientHandler* handler, const char* packet, AnTcpSizeType size) noexcept
{
    if (!handler || !packet || size < static_cast<AnTcpSizeType>(sizeof(AnTcpMessageType)))
    {
        return false;
    }

    const auto type = static_cast<AnTcpMessageType>(packet[0]);
    const auto it = Callbacks.find(type);

    if (it == Callbacks.end())
    {
        // Unknown request: answer with an empty payload so the client doesn't wait forever.
        return handler->SendData(type, nullptr, 0);
    }

    try
    {
        it->second(handler, type, packet + sizeof(AnTcpMessageType),
                   static_cast<int>(size - static_cast<AnTcpSizeType>(sizeof(AnTcpMessageType))));
        return handler->Running.load(std::memory_order_acquire); // false if sending the response failed
    }
    catch (...)
    {
        return false;
    }
}

void AnTcpServer::CleanupClients(bool all) noexcept
{
    std::list<std::unique_ptr<ClientHandler>> finished;

    {
        const std::lock_guard lock(ClientsMutex);

        for (auto it = Clients.begin(); it != Clients.end();)
        {
            if (all || (*it)->IsDisconnected())
            {
                finished.push_back(std::move(*it));
                it = Clients.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // Disconnect everything first so all threads wind down in parallel, then join (in the destructors).
    for (auto& client : finished)
    {
        client->Disconnect();
    }

    finished.clear();
}

AnTcpError AnTcpServer::Run() noexcept
{
    NetworkSession session;

    if (!session.ok)
    {
        return AnTcpError::Win32WsaStartupFailed;
    }

    // ShouldExit isn't reset here: a Stop() (e.g. SIGTERM while maps are preloaded) before Run() must not be lost.

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* addrResult = nullptr;

    if (getaddrinfo(Ip.empty() ? nullptr : Ip.c_str(), Port.c_str(), &hints, &addrResult) != 0 || !addrResult)
    {
        return AnTcpError::GetAddrInfoFailed;
    }

    AnTcpError error = AnTcpError::SocketCreationFailed;

    for (addrinfo* ai = addrResult; ai && ListenSocket == ANTCP_INVALID_SOCKET; ai = ai->ai_next)
    {
        AnTcpSocket s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);

        if (s == ANTCP_INVALID_SOCKET)
        {
            continue;
        }

        int flag = 1;
#ifdef _WIN32
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&flag), sizeof(flag));
#else
        // Allow quick restarts while old connections are in TIME_WAIT.
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&flag), sizeof(flag));
#endif

        if (bind(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) != 0)
        {
            error = AnTcpError::SocketBindingFailed;
            CloseSocket(s);
            continue;
        }

        if (listen(s, SOMAXCONN) != 0)
        {
            error = AnTcpError::SocketListeningFailed;
            CloseSocket(s);
            continue;
        }

        ListenSocket = s;
    }

    freeaddrinfo(addrResult);

    if (ListenSocket == ANTCP_INVALID_SOCKET)
    {
        return error;
    }

    sockaddr_storage bound{};
    socklen_t boundLen = sizeof(bound);

    if (getsockname(ListenSocket, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0)
    {
        BoundPort.store(PortOf(bound), std::memory_order_release);
    }

    while (!ShouldExit.load(std::memory_order_acquire))
    {
        PollFd pfd{};
        pfd.fd = ListenSocket;
        pfd.events = POLL_READ;

        // Poll with a timeout instead of blocking in accept(), so Stop() only needs to set a flag.
        const int ready = PollSockets(&pfd, 1, ACCEPT_POLL_INTERVAL_MS);

        CleanupClients(false);

        if (ready <= 0 || !(pfd.revents & POLL_READ))
        {
            continue;
        }

        sockaddr_storage clientAddress{};
        socklen_t addressSize = sizeof(clientAddress);
        const AnTcpSocket clientSocket =
            accept(ListenSocket, reinterpret_cast<sockaddr*>(&clientAddress), &addressSize);

        if (clientSocket == ANTCP_INVALID_SOCKET)
        {
            continue;
        }

        if (const size_t maxClients = MaxClients.load(std::memory_order_relaxed); maxClients > 0)
        {
            if (const size_t active = GetClientCount(); active >= maxClients)
            {
                RejectedClients.fetch_add(1, std::memory_order_relaxed);

                try
                {
                    if (OnClientRejected)
                    {
                        OnClientRejected(active);
                    }
                }
                catch (...)
                {
                }

                CloseSocket(clientSocket);
                continue;
            }
        }

        ConfigureClientSocket(clientSocket);

        ClientHandler* raw = nullptr;

        try
        {
            auto handler =
                std::make_unique<ClientHandler>(NextClientId.fetch_add(1), clientSocket, clientAddress, this);
            raw = handler.get();

            const std::lock_guard lock(ClientsMutex);
            Clients.push_back(std::move(handler));
        }
        catch (...)
        {
            // Allocation failed. If the handler exists its destructor already closed the socket.
            if (!raw)
            {
                CloseSocket(clientSocket);
            }

            continue;
        }

        try
        {
            raw->Start();
        }
        catch (...)
        {
            // Thread creation failed, let the next cleanup pass remove the handler.
            raw->Running.store(false, std::memory_order_release);
            raw->Finished.store(true, std::memory_order_release);
        }
    }

    CloseSocket(ListenSocket);
    ListenSocket = ANTCP_INVALID_SOCKET;
    BoundPort.store(0, std::memory_order_release);

    CleanupClients(true);
    return AnTcpError::Success;
}
