#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
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

    /// Hands a leased query back to its pool.
    struct Returner
    {
        NavMeshQueryPool* pool = nullptr;
        void operator()(dtNavMeshQuery* query) const noexcept { pool->Return(NavMeshQueryPtr(query)); }
    };

public:
    /// A borrowed query, returned to its pool when destroyed.
    using Lease = std::unique_ptr<dtNavMeshQuery, Returner>;

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
                dtNavMeshQuery* query = Idle.back().release();
                Idle.pop_back();
                return Lease(query, Returner{this});
            }
        }

        NavMeshQueryPtr query(dtAllocNavMeshQuery());

        if (!query || dtStatusFailed(query->init(NavMesh, MaxNodes)))
        {
            return {};
        }

        Created.fetch_add(1, std::memory_order_relaxed);
        return Lease(query.release(), Returner{this});
    }

    /// Number of queries created so far (peak concurrency, minus reuse).
    size_t GetCreatedCount() const noexcept { return Created.load(std::memory_order_relaxed); }

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
