#include "TestFramework.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <optional>
#include <thread>
#include <vector>

#include "NavServer.hpp"
#include "TestWorld.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
using TestWorld::Wow;

/// Minimal blocking AnTCP client for the tests.
class RawClient
{
#ifdef _WIN32
    SOCKET Socket = INVALID_SOCKET;
#else
    int Socket = -1;
#endif

public:
    explicit RawClient(unsigned short port)
    {
#ifdef _WIN32
        static const bool wsa = [] {
            WSADATA data{};
            return WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }();
        (void)wsa;
#endif
        Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (connect(Socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            Close();
            return;
        }

        // Fail a test instead of hanging it when the server never answers.
#ifdef _WIN32
        const DWORD timeout = 15000;
#else
        const timeval timeout{15, 0};
#endif
        setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    }

    ~RawClient() { Close(); }

    RawClient(const RawClient&) = delete;
    RawClient& operator=(const RawClient&) = delete;

    bool Connected() const noexcept
    {
#ifdef _WIN32
        return Socket != INVALID_SOCKET;
#else
        return Socket >= 0;
#endif
    }

    void Close()
    {
        if (!Connected())
        {
            return;
        }
#ifdef _WIN32
        closesocket(Socket);
        Socket = INVALID_SOCKET;
#else
        close(Socket);
        Socket = -1;
#endif
    }

    bool SendRaw(const void* data, size_t size)
    {
        const char* p = static_cast<const char*>(data);

        while (size > 0)
        {
            const auto sent = send(Socket, p, static_cast<int>(size), 0);

            if (sent <= 0)
            {
                return false;
            }

            p += sent;
            size -= static_cast<size_t>(sent);
        }

        return true;
    }

    bool RecvExact(void* data, size_t size)
    {
        char* p = static_cast<char*>(data);

        while (size > 0)
        {
            const auto received = recv(Socket, p, static_cast<int>(size), 0);

            if (received <= 0)
            {
                return false;
            }

            p += received;
            size -= static_cast<size_t>(received);
        }

        return true;
    }

    bool SendRequest(unsigned char type, const void* payload, size_t size)
    {
        const int32_t packetSize = static_cast<int32_t>(size + 1);
        std::vector<char> packet(reinterpret_cast<const char*>(&packetSize),
                                 reinterpret_cast<const char*>(&packetSize) + 4);
        packet.push_back(static_cast<char>(type));

        if (size > 0)
        {
            const auto* bytes = static_cast<const char*>(payload);
            packet.insert(packet.end(), bytes, bytes + size);
        }

        return SendRaw(packet.data(), packet.size());
    }

    /// Receive one response, returns the payload (without the type byte).
    std::optional<std::vector<char>> ReadResponse(unsigned char expectedType)
    {
        int32_t size = 0;

        if (!RecvExact(&size, 4) || size < 1)
        {
            return std::nullopt;
        }

        std::vector<char> data(static_cast<size_t>(size));

        if (!RecvExact(data.data(), data.size()) || static_cast<unsigned char>(data[0]) != expectedType)
        {
            return std::nullopt;
        }

        return std::vector<char>(data.begin() + 1, data.end());
    }

    template <typename T>
    std::optional<std::vector<char>> Request(MessageType type, const T& payload)
    {
        if (!SendRequest(static_cast<unsigned char>(type), &payload, sizeof(T)))
        {
            return std::nullopt;
        }

        return ReadResponse(static_cast<unsigned char>(type));
    }
};

std::vector<Vector3> AsVectors(const std::vector<char>& data)
{
    std::vector<Vector3> result(data.size() / sizeof(Vector3));
    std::memcpy(result.data(), data.data(), result.size() * sizeof(Vector3));
    return result;
}

/// Runs a NavServer on an ephemeral port for the duration of a test.
class ServerFixture
{
    std::unique_ptr<NavServer> Server;
    std::thread Thread;

public:
    explicit ServerFixture(const std::function<void(AmeisenNavConfig&)>& customize = {})
    {
        AmeisenNavConfig config;
        config.mmapsPath = TestWorld::Get().meshDir.string();
        config.useAnpFileFormat = true;
        config.port = 0;
        config.maxPointPath = 256;

        if (customize)
        {
            customize(config);
        }

        std::vector<std::string> errors, warnings;
        NavServer::ValidateConfig(config, errors, warnings);
        REQUIRE(errors.empty());

        Server = std::make_unique<NavServer>(config);
        Thread = std::thread([this] { Server->Run(); });

        for (int i = 0; i < 500 && Server->Server().GetBoundPort() == 0; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (Server->Server().GetBoundPort() == 0)
        {
            Server->Stop();
            Thread.join();
        }

        REQUIRE(Server->Server().GetBoundPort() != 0);
    }

    ~ServerFixture()
    {
        Server->Stop();
        Thread.join();
    }

    unsigned short Port() const { return Server->Server().GetBoundPort(); }
    NavServer& Get() { return *Server; }
};

PathRequestData MakePathRequest(int flags = 0)
{
    PathRequestData request{};
    request.mapId = TestWorld::MAP_ID;
    request.flags = flags;
    request.start = Wow(-700.0f, 0.0f, -300.0f);
    request.end = Wow(-200.0f, 0.0f, -300.0f);
    return request;
}
} // namespace

TEST_CASE(Server_PathRequestRoundTrip)
{
    ServerFixture server;
    RawClient client(server.Port());
    REQUIRE(client.Connected());

    const auto response = client.Request(MessageType::PATH, MakePathRequest());
    REQUIRE(response.has_value());

    const auto points = AsVectors(*response);
    REQUIRE(points.size() >= 3);
    CHECK_NEAR(points.back().x, MakePathRequest().end.x, 1.5);
    CHECK_NEAR(points.back().y, MakePathRequest().end.y, 1.5);
}

TEST_CASE(Server_AllSmoothingAndValidationFlags)
{
    ServerFixture server;
    RawClient client(server.Port());
    REQUIRE(client.Connected());

    const int smoothFlags[] = {0, 1 << 0, 1 << 1, 1 << 2};
    const int validateFlags[] = {0, 1 << 3, 1 << 4};

    for (const MessageType type : {MessageType::PATH, MessageType::RANDOM_PATH})
    {
        for (const int smooth : smoothFlags)
        {
            for (const int validate : validateFlags)
            {
                const auto response = client.Request(type, MakePathRequest(smooth | validate));
                REQUIRE(response.has_value());
                const auto points = AsVectors(*response);
                CHECK(points.size() >= 2);
                CHECK(points.size() <= 256);

                for (const auto& p : points)
                {
                    CHECK(p.IsFinite());
                }
            }
        }
    }
}

TEST_CASE(Server_CastRayAndCastRayEx)
{
    ServerFixture server;
    RawClient client(server.Port());
    REQUIRE(client.Connected());

    CastRayData blocked{TestWorld::MAP_ID, Wow(-400.0f, 0.0f, -300.0f), Wow(-200.0f, 0.0f, -300.0f)};
    CastRayData open{TestWorld::MAP_ID, Wow(-400.0f, 0.0f, -50.0f), Wow(-200.0f, 0.0f, -50.0f)};

    // Legacy CAST_RAY: end position if clear, zero if blocked.
    auto response = client.Request(MessageType::CAST_RAY, blocked);
    REQUIRE(response && response->size() == sizeof(Vector3));
    CHECK(AsVectors(*response)[0].IsZero());

    response = client.Request(MessageType::CAST_RAY, open);
    REQUIRE(response && response->size() == sizeof(Vector3));
    CHECK(AsVectors(*response)[0] == open.end);

    response = client.Request(MessageType::CAST_RAY_EX, blocked);
    REQUIRE(response && response->size() == sizeof(CastRayExResponse));
    CastRayExResponse ex{};
    std::memcpy(&ex, response->data(), sizeof(ex));
    CHECK_EQ(ex.hit, 1);
    CHECK(ex.t > 0.0f && ex.t < 1.0f);
    CHECK_NEAR(ex.position.y, TestWorld::WALL_X, 4.0);
}

TEST_CASE(Server_FilterHeightAndPoints)
{
    ServerFixture server;
    RawClient client(server.Port());
    REQUIRE(client.Connected());

    // CONFIGURE_FILTER with two entries.
    std::vector<char> filter(sizeof(ConfigureFilterHeader) + 2 * sizeof(FilterConfig));
    ConfigureFilterHeader header{ClientState::NORMAL_ALLIANCE, 2};
    FilterConfig entries[2]{{TERRAIN_ROAD, 0.5f}, {LIQUID_WATER, 5.0f}};
    std::memcpy(filter.data(), &header, sizeof(header));
    std::memcpy(filter.data() + sizeof(header), entries, sizeof(entries));

    REQUIRE(client.SendRequest(static_cast<unsigned char>(MessageType::CONFIGURE_FILTER), filter.data(),
                               filter.size()));
    auto response = client.ReadResponse(static_cast<unsigned char>(MessageType::CONFIGURE_FILTER));
    REQUIRE(response && response->size() == 1);
    CHECK((*response)[0] == 1);

    // Count claims more entries than sent -> rejected, connection stays usable.
    header.filterConfigCount = 5;
    std::memcpy(filter.data(), &header, sizeof(header));
    REQUIRE(client.SendRequest(static_cast<unsigned char>(MessageType::CONFIGURE_FILTER), filter.data(),
                               filter.size()));
    response = client.ReadResponse(static_cast<unsigned char>(MessageType::CONFIGURE_FILTER));
    REQUIRE(response && response->size() == 1);
    CHECK((*response)[0] == 0);

    GetHeightData height{TestWorld::MAP_ID, Wow(-600.0f, 3.0f, -200.0f)};
    response = client.Request(MessageType::GET_HEIGHT, height);
    REQUIRE(response && response->size() == sizeof(Vector3));
    CHECK_NEAR(AsVectors(*response)[0].z, 0.0f, 0.6);

    const int mapId = TestWorld::MAP_ID;
    response = client.Request(MessageType::RANDOM_POINT, mapId);
    REQUIRE(response && response->size() == sizeof(Vector3));
    CHECK(!AsVectors(*response)[0].IsZero());

    RandomPointAroundData around{TestWorld::MAP_ID, Wow(-700.0f, 0.0f, -300.0f), 15.0f};
    response = client.Request(MessageType::RANDOM_POINT_AROUND, around);
    REQUIRE(response && response->size() == sizeof(Vector3));

    MoveRequestData move{TestWorld::MAP_ID, Wow(-320.0f, 0.0f, -300.0f), Wow(-280.0f, 0.0f, -300.0f)};
    response = client.Request(MessageType::MOVE_ALONG_SURFACE, move);
    REQUIRE(response && response->size() == sizeof(Vector3));
    CHECK(AsVectors(*response)[0].y < TestWorld::WALL_X);
}

namespace {
std::vector<char> ExploreRequest(int pointCount, const std::vector<Vector3>& polygon, int flags = 0)
{
    ExplorePolyRequestHeader header{};
    header.mapId = TestWorld::MAP_ID;
    header.flags = flags;
    header.start = Wow(-380.0f, 0.0f, -440.0f);
    header.spacing = 40.0f;
    header.pointCount = pointCount;

    std::vector<char> packet(sizeof(header) + polygon.size() * sizeof(Vector3));
    std::memcpy(packet.data(), &header, sizeof(header));
    std::memcpy(packet.data() + sizeof(header), polygon.data(), polygon.size() * sizeof(Vector3));
    return packet;
}
} // namespace

TEST_CASE(Server_ExplorePoly)
{
    ServerFixture server;
    RawClient client(server.Port());
    REQUIRE(client.Connected());

    const std::vector<Vector3> outline{Wow(-390.0f, 0.0f, -450.0f), Wow(-210.0f, 0.0f, -450.0f),
                                       Wow(-210.0f, 0.0f, -150.0f), Wow(-390.0f, 0.0f, -150.0f)};
    const auto type = static_cast<unsigned char>(MessageType::EXPLORE_POLY);

    for (const int flags : {0, static_cast<int>(PathRequestFlags::SMOOTH_CATMULLROM)
                                   | static_cast<int>(PathRequestFlags::VALIDATE_MAS)})
    {
        const auto packet = ExploreRequest(4, outline, flags);
        REQUIRE(client.SendRequest(type, packet.data(), packet.size()));
        const auto response = client.ReadResponse(type);
        REQUIRE(response.has_value());

        const auto points = AsVectors(*response);
        CHECK(points.size() > 20);
        CHECK(points.size() <= 256);

        for (const auto& p : points)
        {
            CHECK(p.IsFinite());
            CHECK_NEAR(p.z, 0.0f, 0.6);
        }
    }

    // Too few points, count larger than the payload, absurd count: a zero vector, connection stays usable.
    for (const auto& packet : {ExploreRequest(2, {outline[0], outline[1]}), ExploreRequest(5, outline),
                               ExploreRequest(1 << 20, outline)})
    {
        REQUIRE(client.SendRequest(type, packet.data(), packet.size()));
        const auto response = client.ReadResponse(type);
        REQUIRE(response.has_value());
        CHECK(AsVectors(*response).size() == 1 && AsVectors(*response)[0].IsZero());
    }
}

TEST_CASE(Server_RequireCompletePaths)
{
    ServerFixture server;
    RawClient client(server.Port());
    REQUIRE(client.Connected());

    const int requireComplete = static_cast<int>(PathRequestFlags::REQUIRE_COMPLETE);

    // Reachable: flag changes nothing.
    auto response = client.Request(MessageType::PATH, MakePathRequest(requireComplete));
    REQUIRE(response.has_value());
    CHECK(AsVectors(*response).size() >= 3);

    // The platform deck can't be reached from the ground.
    PathRequestData toDeck = MakePathRequest();
    toDeck.start = Wow(-600.0f, 0.0f, -425.0f);
    toDeck.end = Wow(-425.0f, TestWorld::PLATFORM_Y, -425.0f);

    response = client.Request(MessageType::PATH, toDeck);
    REQUIRE(response.has_value());
    CHECK(AsVectors(*response).size() >= 2);

    toDeck.flags = requireComplete;

    for (const MessageType type : {MessageType::PATH, MessageType::RANDOM_PATH})
    {
        response = client.Request(type, toDeck);
        REQUIRE(response.has_value());
        CHECK(AsVectors(*response).size() == 1 && AsVectors(*response)[0].IsZero());
    }
}

TEST_CASE(Server_GetConfigReportsVersion)
{
    ServerFixture server;
    RawClient client(server.Port());
    REQUIRE(client.Connected());

    REQUIRE(client.SendRequest(static_cast<unsigned char>(MessageType::GET_CONFIG), nullptr, 0));
    const auto response = client.ReadResponse(static_cast<unsigned char>(MessageType::GET_CONFIG));
    REQUIRE(response.has_value());
    REQUIRE(response->size() >= sizeof(GetConfigResponseHeader));

    GetConfigResponseHeader header{};
    std::memcpy(&header, response->data(), sizeof(header));
    CHECK_EQ(header.useAnpFileFormat, 1);

    const size_t trailerOffset = sizeof(header) + static_cast<size_t>(header.pathLength);
    REQUIRE(response->size() >= trailerOffset + sizeof(GetConfigResponseTrailer));
    CHECK(std::string(response->data() + sizeof(header), static_cast<size_t>(header.pathLength))
          == TestWorld::Get().meshDir.string());

    GetConfigResponseTrailer trailer{};
    std::memcpy(&trailer, response->data() + trailerOffset, sizeof(trailer));
    CHECK_EQ(trailer.protocolVersion, PROTOCOL_VERSION);
    CHECK_EQ(trailer.maxPointPath, 256);
    REQUIRE(response->size() == trailerOffset + sizeof(trailer) + static_cast<size_t>(trailer.versionLength));
    CHECK(std::string(response->data() + trailerOffset + sizeof(trailer), static_cast<size_t>(trailer.versionLength))
          == AMEISENNAV_VERSION);
}

TEST_CASE(Server_MalformedRequestsAlwaysGetAnAnswer)
{
    ServerFixture server;
    RawClient client(server.Port());
    REQUIRE(client.Connected());

    const char tiny = 0;

    for (const MessageType type : {MessageType::PATH, MessageType::RANDOM_PATH, MessageType::MOVE_ALONG_SURFACE,
                                   MessageType::CAST_RAY, MessageType::RANDOM_POINT, MessageType::RANDOM_POINT_AROUND,
                                   MessageType::GET_HEIGHT, MessageType::CONFIGURE_FILTER, MessageType::CAST_RAY_EX,
                                   MessageType::EXPLORE_POLY})
    {
        REQUIRE(client.SendRequest(static_cast<unsigned char>(type), &tiny, 1));
        CHECK(client.ReadResponse(static_cast<unsigned char>(type)).has_value());
    }

    // Unknown message types get an empty answer instead of a disconnect.
    REQUIRE(client.SendRequest(200, nullptr, 0));
    const auto unknown = client.ReadResponse(200);
    REQUIRE(unknown.has_value());
    CHECK(unknown->empty());

    // Unknown map
    PathRequestData request = MakePathRequest();
    request.mapId = 4242;
    const auto response = client.Request(MessageType::PATH, request);
    REQUIRE(response.has_value());
    CHECK(AsVectors(*response).size() == 1 && AsVectors(*response)[0].IsZero());

    // Oversized packet header -> the server drops the connection.
    const int32_t hugeSize = 1 << 30;
    REQUIRE(client.SendRaw(&hugeSize, 4));
    char byte = 0;
    CHECK(!client.RecvExact(&byte, 1));
}

TEST_CASE(Server_PipelinedRequests)
{
    ServerFixture server;
    RawClient client(server.Port());
    REQUIRE(client.Connected());

    // Send 20 requests in one go, then read all answers.
    std::vector<char> batch;
    const PathRequestData request = MakePathRequest();

    for (int i = 0; i < 20; ++i)
    {
        const int32_t size = sizeof(request) + 1;
        batch.insert(batch.end(), reinterpret_cast<const char*>(&size), reinterpret_cast<const char*>(&size) + 4);
        batch.push_back(static_cast<char>(MessageType::PATH));
        batch.insert(batch.end(), reinterpret_cast<const char*>(&request),
                     reinterpret_cast<const char*>(&request) + sizeof(request));
    }

    REQUIRE(client.SendRaw(batch.data(), batch.size()));

    for (int i = 0; i < 20; ++i)
    {
        const auto response = client.ReadResponse(static_cast<unsigned char>(MessageType::PATH));
        REQUIRE(response.has_value());
        CHECK(AsVectors(*response).size() >= 3);
    }
}

TEST_CASE(Server_ConcurrentClientsGetConsistentResults)
{
    ServerFixture server;

    RawClient reference(server.Port());
    REQUIRE(reference.Connected());
    const auto expected = reference.Request(MessageType::PATH, MakePathRequest());
    REQUIRE(expected.has_value());

    constexpr int CLIENTS = 8;
    constexpr int REQUESTS = 50;
    std::atomic<int> mismatches{0};
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;

    for (int c = 0; c < CLIENTS; ++c)
    {
        threads.emplace_back([&]() {
            RawClient client(server.Port());

            if (!client.Connected())
            {
                failures++;
                return;
            }

            for (int i = 0; i < REQUESTS; ++i)
            {
                const auto response = client.Request(MessageType::PATH, MakePathRequest());

                if (!response)
                {
                    failures++;
                    return;
                }

                if (*response != *expected)
                {
                    mismatches++;
                }
            }
        });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    CHECK_EQ(failures.load(), 0);
    CHECK_EQ(mismatches.load(), 0);

    // All client states are released after the connections are closed.
    reference.Close();

    for (int i = 0; i < 200 && server.Get().Nav().GetClientCount() > 0; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK_EQ(server.Get().Nav().GetClientCount(), size_t{0});
}

TEST_CASE(Server_StopsWhileClientsAreConnected)
{
    auto server = std::make_unique<ServerFixture>();
    RawClient client(server->Port());
    REQUIRE(client.Connected());
    REQUIRE(client.Request(MessageType::PATH, MakePathRequest()).has_value());

    const auto start = std::chrono::steady_clock::now();
    server.reset(); // Stop + join while the client is still connected and idle
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));

    char byte = 0;
    CHECK(!client.RecvExact(&byte, 1)); // connection was closed by the server
}

namespace {
/// True if the server closed the connection within the socket's receive timeout.
bool ClosedByServer(RawClient& client)
{
    char byte = 0;
    return !client.RecvExact(&byte, 1);
}
} // namespace

TEST_CASE(Server_ClientLimit)
{
    ServerFixture server([](AmeisenNavConfig& config) { config.maxClients = 2; });

    RawClient first(server.Port());
    RawClient second(server.Port());
    REQUIRE(first.Connected() && second.Connected());

    // Both are served (the round trips also make sure the server registered them).
    CHECK(first.Request(MessageType::PATH, MakePathRequest()).has_value());
    CHECK(second.Request(MessageType::PATH, MakePathRequest()).has_value());

    // The third connection is accepted by the kernel and closed by the server right away.
    RawClient third(server.Port());
    REQUIRE(third.Connected());
    CHECK(ClosedByServer(third));
    CHECK(server.Get().Server().GetRejectedCount() >= 1);

    // A slot frees up when a client leaves.
    first.Close();

    bool servedAfterLeave = false;

    for (int attempt = 0; attempt < 50 && !servedAfterLeave; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        RawClient next(server.Port());
        servedAfterLeave = next.Connected() && next.Request(MessageType::PATH, MakePathRequest()).has_value();
    }

    CHECK(servedAfterLeave);
    CHECK(second.Request(MessageType::PATH, MakePathRequest()).has_value());
}

TEST_CASE(Server_IdleTimeout)
{
    ServerFixture server([](AmeisenNavConfig& config) { config.clientIdleTimeoutSec = 1; });

    RawClient idle(server.Port());
    RawClient busy(server.Port());
    REQUIRE(idle.Connected() && busy.Connected());

    // The busy client keeps sending for well over the timeout and stays connected.
    const auto start = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(2500))
    {
        REQUIRE(busy.Request(MessageType::PATH, MakePathRequest()).has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // The silent one was dropped after ~1s.
    CHECK(ClosedByServer(idle));
}

TEST_CASE(Server_StatsSummary)
{
    ServerFixture server;
    RawClient client(server.Port());
    REQUIRE(client.Connected());

    server.Get().TakeStatsSummary(1.0); // drop anything from connecting

    for (int i = 0; i < 3; ++i)
    {
        REQUIRE(client.Request(MessageType::PATH, MakePathRequest()).has_value());
    }

    GetHeightData height{TestWorld::MAP_ID, Wow(-600.0f, 3.0f, -200.0f)};
    REQUIRE(client.Request(MessageType::GET_HEIGHT, height).has_value());

    // A request is recorded after its response went out. Requests of one connection run strictly in order, so
    // once this answer arrives everything before it has been recorded.
    const int mapId = TestWorld::MAP_ID;
    REQUIRE(client.Request(MessageType::RANDOM_POINT, mapId).has_value());

    const std::string summary = server.Get().TakeStatsSummary(2.0);
    CHECK(summary.find("Path 3 avg") != std::string::npos);
    CHECK(summary.find("GetHeight 1 avg") != std::string::npos);
    CHECK(summary.find("1 clients") != std::string::npos);

    // Counters were reset.
    CHECK(server.Get().TakeStatsSummary(1.0).empty());
}

TEST_CASE(Server_StopBeforeRunIsNotLost)
{
    // SIGTERM while maps are still being preloaded calls Stop() before Run(): Run() must return right away.
    AmeisenNavConfig config;
    config.mmapsPath = TestWorld::Get().meshDir.string();
    config.useAnpFileFormat = true;
    config.port = 0;
    NavServer server(config);
    server.Stop();

    // Run on a thread so a regression fails the test instead of hanging it.
    std::promise<AnTcpError> result;
    auto future = result.get_future();
    std::thread runner([&] { result.set_value(server.Run()); });

    const bool returned = future.wait_for(std::chrono::seconds(3)) == std::future_status::ready;

    if (!returned)
    {
        server.Stop();
    }

    runner.join();
    CHECK(returned);
    CHECK(future.get() == AnTcpError::Success);
}

#ifndef _WIN32
TEST_CASE(AnTcp_FailedSendDropsTheClient)
{
    // Handlers ignore SendData's result: a failed send (peer gone, or not reading until the send timeout) has to
    // drop the client by itself, and pipelined requests behind it must not be processed.
    AnTcpServer server("127.0.0.1", 0);
    server.AddCallback(1, [](ClientHandler* handler, AnTcpMessageType type, const void*, int) {
        handler->SendData(type, nullptr, 0);
    });

    int sv[2]{};
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    sockaddr_storage address{};
    ClientHandler client(1, sv[0], address, &server);
    const char request = 1;

    CHECK(server.Dispatch(&client, &request, 1));

    close(sv[1]);
    CHECK(!server.Dispatch(&client, &request, 1));

    const char unknown = 2; // answered by Dispatch itself
    CHECK(!server.Dispatch(&client, &unknown, 1));
}
#endif
