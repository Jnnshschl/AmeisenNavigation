#pragma once

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <DetourNavMeshQuery.h>

#include "../../../AmeisenNavigation.Pack/src/DetourUtils.hpp"

/// dtNavMeshQuery objects for one navmesh, leased for the duration of a single request.
///
/// A query owns an A* node pool (~2.7 MB with 65535 nodes). Queries don't carry state between requests, so
/// instead of one query per client and map, requests borrow one from the map's pool: the number of queries
/// follows the number of concurrently running requests, not the number of connected clients.
class NavMeshQueryPool
{
    const dtNavMesh* NavMesh;
    const int MaxNodes;
    const size_t MaxIdle;

    std::mutex Mutex;
    std::vector<NavMeshQueryPtr> Idle;
    std::atomic<size_t> Created{0};

public:
    /// Returns the query to its pool when destroyed.
    class Lease
    {
        NavMeshQueryPool* Pool = nullptr;
        NavMeshQueryPtr Query;

    public:
        Lease() noexcept = default;
        Lease(NavMeshQueryPool* pool, NavMeshQueryPtr query) noexcept : Pool(pool), Query(std::move(query)) {}

        Lease(Lease&& other) noexcept : Pool(std::exchange(other.Pool, nullptr)), Query(std::move(other.Query)) {}

        Lease& operator=(Lease&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                Pool = std::exchange(other.Pool, nullptr);
                Query = std::move(other.Query);
            }

            return *this;
        }

        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        ~Lease() { Release(); }

        dtNavMeshQuery* Get() const noexcept { return Query.get(); }
        explicit operator bool() const noexcept { return Query != nullptr; }

    private:
        void Release() noexcept
        {
            if (Pool && Query)
            {
                Pool->Return(std::move(Query));
            }

            Pool = nullptr;
            Query.reset();
        }
    };

    /// maxIdle: queries kept for reuse when no request needs them (0 = automatic, based on the core count).
    NavMeshQueryPool(const dtNavMesh* navMesh, int maxNodes, size_t maxIdle = 0) noexcept
        : NavMesh(navMesh),
          MaxNodes(maxNodes),
          MaxIdle(maxIdle > 0 ? maxIdle : std::max<size_t>(8, 2 * std::thread::hardware_concurrency()))
    {
    }

    NavMeshQueryPool(const NavMeshQueryPool&) = delete;
    NavMeshQueryPool& operator=(const NavMeshQueryPool&) = delete;

    /// Borrow a query (reused or newly created). An empty lease means the allocation/init failed.
    Lease Acquire() noexcept
    {
        {
            const std::lock_guard lock(Mutex);

            if (!Idle.empty())
            {
                NavMeshQueryPtr query = std::move(Idle.back());
                Idle.pop_back();
                return {this, std::move(query)};
            }
        }

        NavMeshQueryPtr query(dtAllocNavMeshQuery());

        if (!query || dtStatusFailed(query->init(NavMesh, MaxNodes)))
        {
            return {};
        }

        Created.fetch_add(1, std::memory_order_relaxed);
        return {this, std::move(query)};
    }

    /// Number of queries created so far (peak concurrency, minus reuse).
    size_t GetCreatedCount() const noexcept { return Created.load(std::memory_order_relaxed); }

    size_t GetIdleCount() noexcept
    {
        const std::lock_guard lock(Mutex);
        return Idle.size();
    }

private:
    void Return(NavMeshQueryPtr query) noexcept
    {
        try
        {
            const std::lock_guard lock(Mutex);

            if (Idle.size() < MaxIdle)
            {
                Idle.push_back(std::move(query));
            }
        }
        catch (...)
        {
            // push_back failed: the query is simply freed.
        }
    }
};
