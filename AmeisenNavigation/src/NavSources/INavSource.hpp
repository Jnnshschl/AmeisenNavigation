#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "../../../AmeisenNavigation.Pack/src/DetourUtils.hpp"

/// Provides (lazily loaded) navmeshes per map id. Implementations must be thread-safe.
/// Returned navmeshes stay valid for the lifetime of the source and are never modified after publishing,
/// so any number of dtNavMeshQuery objects may use them concurrently.
class INavSource
{
public:
    virtual ~INavSource() noexcept = default;

    /// Returns the navmesh for mapId (loading it on first use) or nullptr if it doesn't exist.
    virtual dtNavMesh* Get(int mapId) noexcept = 0;
};

/// Thread-safe "load once per map" cache shared by the nav sources.
///
/// Each map gets its own slot with its own mutex, so loading a big map never blocks queries for
/// maps that are already loaded. Failed loads are remembered and not retried.
class NavMeshCache
{
public:
    /// Map ids are bounded so arbitrary ids (from requests) can't grow the cache without limit.
    static constexpr int MAX_MAP_ID = 65535;

private:
    struct Slot
    {
        std::mutex loadMutex;
        std::atomic<bool> attempted{false};
        NavMeshPtr navMesh;
    };

    std::mutex SlotsMutex;
    std::unordered_map<int, std::unique_ptr<Slot>> Slots;

public:
    template <typename Loader>
    dtNavMesh* GetOrLoad(int mapId, Loader&& loader) noexcept
    {
        if (mapId < 0 || mapId > MAX_MAP_ID)
        {
            return nullptr;
        }

        Slot* slot = nullptr;

        try
        {
            const std::lock_guard lock(SlotsMutex);
            auto& entry = Slots[mapId];

            if (!entry)
            {
                entry = std::make_unique<Slot>();
            }

            slot = entry.get();
        }
        catch (...)
        {
            return nullptr;
        }

        // Fast path: already loaded (or failed). navMesh is written before attempted is released.
        if (slot->attempted.load(std::memory_order_acquire))
        {
            return slot->navMesh.get();
        }

        const std::lock_guard loadLock(slot->loadMutex);

        if (!slot->attempted.load(std::memory_order_relaxed))
        {
            try
            {
                slot->navMesh = loader(mapId);
            }
            catch (...)
            {
                slot->navMesh.reset();
            }

            slot->attempted.store(true, std::memory_order_release);
        }

        return slot->navMesh.get();
    }
};
