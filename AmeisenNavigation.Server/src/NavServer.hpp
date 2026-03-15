#pragma once

#include "AnTcpServer.hpp"
#include "AmeisenNavigation.hpp"
#include "Protocol.hpp"
#include "Config/Config.hpp"

#include <mutex>
#include <unordered_map>

/// Owns all server state: TCP server, navigation engine, config, and per-client path buffers.
/// A single global pointer (g_NavServer) is used by C-style callbacks to reach this state.
class NavServer
{
public:
    NavServer(AmeisenNavConfig* config)
        : config_(config)
        , nav_(new AmeisenNavigation(
              config->mmapsPath, config->maxPolyPath, config->maxSearchNodes,
              config->useAnpFileFormat, config->factionDangerCost))
        , server_(new AnTcpServer(config->ip, config->port))
    {
    }

    ~NavServer()
    {
        delete server_;
        delete nav_;
        delete config_;
    }

    NavServer(const NavServer&) = delete;
    NavServer& operator=(const NavServer&) = delete;

    // ── Accessors ────────────────────────────────────────────────────

    AnTcpServer* Server() noexcept { return server_; }
    AmeisenNavigation* Nav() noexcept { return nav_; }
    AmeisenNavConfig* Config() noexcept { return config_; }

    // ── Client path buffer management ────────────────────────────────

    void AllocClientBuffers(size_t clientId) noexcept
    {
        std::lock_guard lock(bufferMutex_);
        clientBuffers_[clientId] =
            std::make_pair(new Path(config_->maxPointPath), new Path(config_->maxPointPath));
    }

    void FreeClientBuffers(size_t clientId) noexcept
    {
        std::lock_guard lock(bufferMutex_);
        auto it = clientBuffers_.find(clientId);
        if (it == clientBuffers_.end())
            return;

        delete[] it->second.first->points;
        delete it->second.first;
        delete[] it->second.second->points;
        delete it->second.second;

        clientBuffers_.erase(it);
    }

    bool HasClientBuffers(size_t clientId) noexcept
    {
        std::lock_guard lock(bufferMutex_);
        return clientBuffers_.find(clientId) != clientBuffers_.end();
    }

    /// Get the path buffer pair for a client. Caller must ensure clientId is valid.
    std::pair<Path*, Path*> GetClientBuffers(size_t clientId) noexcept
    {
        std::lock_guard lock(bufferMutex_);
        return clientBuffers_[clientId];
    }

    // ── Server lifecycle ─────────────────────────────────────────────

    void RegisterCallbacks() noexcept;
    void Run() noexcept { server_->Run(); }
    void Stop() noexcept { server_->Stop(); }

private:
    AmeisenNavConfig* config_;
    AmeisenNavigation* nav_;
    AnTcpServer* server_;

    std::mutex bufferMutex_;
    std::unordered_map<size_t, std::pair<Path*, Path*>> clientBuffers_;
};

/// Single global access point for C-style callbacks.
inline NavServer* g_NavServer = nullptr;
