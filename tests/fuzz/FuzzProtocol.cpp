// Fuzz the request handlers of the real server on the synthetic test world. The input is a sequence of
// packets: u8 type | u16 payload size | payload. Invariant: every packet is answered with exactly one well formed
// response of the same type (the client would wait forever otherwise).

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../TestWorld.hpp"
#include "NavServer.hpp"
#include "Utils/Logger.hpp"

namespace {
struct Environment
{
    std::unique_ptr<NavServer> server;
    std::unique_ptr<ClientHandler> client;
    int peer = -1;
};

Environment& Env()
{
    static Environment env = [] {
        Environment e;
        AmeisenNavConfig config;
        config.mmapsPath = TestWorld::Get().meshDir.string();
        config.useAnpFileFormat = true;
        config.port = 0;
        config.maxPointPath = 256;

        std::vector<std::string> errors, warnings;
        NavServer::ValidateConfig(config, errors, warnings);

        if (!errors.empty())
        {
            std::abort();
        }

        e.server = std::make_unique<NavServer>(config);

        int sv[2]{};

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        {
            std::abort();
        }

        // Big enough for the largest response (256 points).
        const int buffer = 1 << 20;
        setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer));
        setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer));
        fcntl(sv[1], F_SETFL, fcntl(sv[1], F_GETFL) | O_NONBLOCK);

        sockaddr_storage address{};
        e.client = std::make_unique<ClientHandler>(1, sv[0], address, &e.server->Server());
        e.server->Nav().NewClient(1);
        e.peer = sv[1];
        e.server->Nav().PreloadMap(TestWorld::MAP_ID);
        return e;
    }();
    return env;
}

bool ReadExact(int fd, void* out, size_t size)
{
    auto* p = static_cast<char*>(out);

    while (size > 0)
    {
        const auto n = read(fd, p, size);

        if (n <= 0)
        {
            return false;
        }

        p += n;
        size -= static_cast<size_t>(n);
    }

    return true;
}
} // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    Logger::SetQuiet(true);
    Env();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    auto& env = Env();
    std::vector<char> packet;

    // Fresh client state (filter) per input, so crashes reproduce from the input alone.
    env.server->Nav().FreeClient(1);
    env.server->Nav().NewClient(1);

    // Limit the work per input, EXPLORE_POLY and long paths are comparatively expensive.
    for (int n = 0; n < 8 && size >= 3; ++n)
    {
        const uint8_t type = data[0];
        const size_t payload = std::min<size_t>(static_cast<size_t>(data[1]) | (static_cast<size_t>(data[2]) << 8),
                                                size - 3);
        packet.assign(reinterpret_cast<const char*>(data + 3), reinterpret_cast<const char*>(data + 3 + payload));
        packet.insert(packet.begin(), static_cast<char>(type));
        data += 3 + payload;
        size -= 3 + payload;

        if (!env.server->Server().Dispatch(env.client.get(), packet.data(), static_cast<AnTcpSizeType>(packet.size())))
        {
            continue; // the receive loop would drop the client
        }

        // Exactly one response with the request's type.
        int32_t responseSize = 0;
        unsigned char responseType = 0;

        if (!ReadExact(env.peer, &responseSize, sizeof(responseSize)) || responseSize < 1
            || !ReadExact(env.peer, &responseType, 1) || responseType != type)
        {
            __builtin_trap();
        }

        std::vector<char> body(static_cast<size_t>(responseSize - 1));

        if (!body.empty() && !ReadExact(env.peer, body.data(), body.size()))
        {
            __builtin_trap();
        }

        char extra = 0;

        if (read(env.peer, &extra, 1) > 0)
        {
            __builtin_trap(); // more than one response
        }
    }

    return 0;
}
