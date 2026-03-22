#pragma once

#include "AnTcpServer.hpp"
#include "AmeisenNavigation.hpp"
#include "Protocol.hpp"
#include "Config/Config.hpp"

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

/// Owns all server state: TCP server, navigation engine, config, and per-client path buffers.
/// A single global pointer (g_NavServer) is used by C-style callbacks to reach this state.
class NavServer
{
public:
    NavServer(std::unique_ptr<AmeisenNavConfig> config)
        : config_(std::move(config))
        , nav_(std::make_unique<AmeisenNavigation>(
              config_->mmapsPath, config_->maxPolyPath, config_->maxSearchNodes,
              config_->useAnpFileFormat, config_->factionDangerCost))
        , server_(std::make_unique<AnTcpServer>(config_->ip, config_->port))
    {
    }

    ~NavServer() = default;

    NavServer(const NavServer&) = delete;
    NavServer& operator=(const NavServer&) = delete;

    // ── Accessors ────────────────────────────────────────────────────

    AnTcpServer* Server() noexcept { return server_.get(); }
    AmeisenNavigation* Nav() noexcept { return nav_.get(); }
    AmeisenNavConfig* Config() noexcept { return config_.get(); }

    // ── Client path buffer management ────────────────────────────────

    void AllocClientBuffers(size_t clientId)
    {
        std::unique_lock lock(bufferMutex_);
        clientBuffers_[clientId] =
            std::make_pair(std::make_unique<Path>(config_->maxPointPath), std::make_unique<Path>(config_->maxPointPath));
    }

    void FreeClientBuffers(size_t clientId) noexcept
    {
        std::unique_lock lock(bufferMutex_);
        clientBuffers_.erase(clientId);
    }

    bool HasClientBuffers(size_t clientId) noexcept
    {
        std::shared_lock lock(bufferMutex_);
        return clientBuffers_.find(clientId) != clientBuffers_.end();
    }

    /// Get the path buffer pair for a client. Caller must ensure clientId is valid.
    std::pair<Path*, Path*> GetClientBuffers(size_t clientId) noexcept
    {
        std::shared_lock lock(bufferMutex_);
        auto it = clientBuffers_.find(clientId);
        if (it == clientBuffers_.end()) return { nullptr, nullptr };
        return { it->second.first.get(), it->second.second.get() };
    }

    // ── Server lifecycle ─────────────────────────────────────────────

    void RegisterCallbacks();
    void Run() noexcept { server_->Run(); }
    void Stop() noexcept { server_->Stop(); }

private:
    std::unique_ptr<AmeisenNavConfig> config_;
    std::unique_ptr<AmeisenNavigation> nav_;
    std::unique_ptr<AnTcpServer> server_;

    std::shared_mutex bufferMutex_;
    std::unordered_map<size_t, std::pair<std::unique_ptr<Path>, std::unique_ptr<Path>>> clientBuffers_;
};

/// Single global access point for C-style callbacks.
inline std::unique_ptr<NavServer> g_NavServer;
