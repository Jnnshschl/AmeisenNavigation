#include "TestFramework.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
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
        }
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
    ServerFixture()
    {
        AmeisenNavConfig config;
        config.mmapsPath = TestWorld::Get().meshDir.string();
        config.useAnpFileFormat = true;
        config.port = 0;
        config.maxPointPath = 256;

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

TEST_CASE(Server_MalformedRequestsAlwaysGetAnAnswer)
{
    ServerFixture server;
    RawClient client(server.Port());
    REQUIRE(client.Connected());

    const char tiny = 0;

    for (const MessageType type : {MessageType::PATH, MessageType::RANDOM_PATH, MessageType::MOVE_ALONG_SURFACE,
                                   MessageType::CAST_RAY, MessageType::RANDOM_POINT, MessageType::RANDOM_POINT_AROUND,
                                   MessageType::GET_HEIGHT, MessageType::CONFIGURE_FILTER, MessageType::CAST_RAY_EX})
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
