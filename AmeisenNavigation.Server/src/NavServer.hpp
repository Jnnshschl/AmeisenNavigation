#pragma once

#include <memory>
#include <string>
#include <vector>

#include "AmeisenNavigation.hpp"
#include "AnTcpServer.hpp"
#include "Config/Config.hpp"
#include "Protocol.hpp"

constexpr auto AMEISENNAV_VERSION = "1.9.0.0";

/// Owns the TCP server, the navigation engine and the config, and implements all request handlers.
class NavServer
{
    AmeisenNavConfig Cfg;
    std::unique_ptr<AmeisenNavigation> Navigation;
    std::unique_ptr<AnTcpServer> TcpServer;

public:
    explicit NavServer(const AmeisenNavConfig& config);

    NavServer(const NavServer&) = delete;
    NavServer& operator=(const NavServer&) = delete;

    /// Validate and normalize a config. Fatal problems are returned in errors, auto-corrected ones in warnings.
    static void ValidateConfig(AmeisenNavConfig& config, std::vector<std::string>& errors,
                               std::vector<std::string>& warnings);

    /// Map the config's iMmapFormat value to MmapFormat.
    static MmapFormat ToMmapFormat(int configValue) noexcept;

    AnTcpServer& Server() noexcept { return *TcpServer; }
    AmeisenNavigation& Nav() noexcept { return *Navigation; }
    const AmeisenNavConfig& Config() const noexcept { return Cfg; }

    /// Load the navmeshes listed in sPreloadMaps.
    void PreloadMaps();

    /// Serve until Stop() is called (blocking).
    AnTcpError Run() noexcept { return TcpServer->Run(); }

    /// Async-signal-safe.
    void Stop() noexcept { TcpServer->Stop(); }

private:
    void RegisterCallbacks();

    void OnClientConnect(ClientHandler* handler);
    void OnClientDisconnect(ClientHandler* handler);

    void HandlePath(ClientHandler* handler, AnTcpMessageType type, const void* data, int size, PathType pathType);
    void HandleMoveAlongSurface(ClientHandler* handler, AnTcpMessageType type, const void* data, int size);
    void HandleCastRay(ClientHandler* handler, AnTcpMessageType type, const void* data, int size);
    void HandleCastRayEx(ClientHandler* handler, AnTcpMessageType type, const void* data, int size);
    void HandleRandomPoint(ClientHandler* handler, AnTcpMessageType type, const void* data, int size);
    void HandleRandomPointAround(ClientHandler* handler, AnTcpMessageType type, const void* data, int size);
    void HandleConfigureFilter(ClientHandler* handler, AnTcpMessageType type, const void* data, int size);
    void HandleGetHeight(ClientHandler* handler, AnTcpMessageType type, const void* data, int size);
    void HandleGetConfig(ClientHandler* handler, AnTcpMessageType type, const void* data, int size);
    void HandleExplorePoly(ClientHandler* handler, AnTcpMessageType type, const void* data, int size);

    /// Apply smoothing/validation flags to a raw path. Returns the buffer holding the final result.
    Path* ApplyPathFlags(size_t clientId, int mapId, int flags, PathType pathType, Path& path, Path& scratch);
};
