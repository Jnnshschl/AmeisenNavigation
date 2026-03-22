#pragma once

#include <memory>
#include <shared_mutex>

#include "../../../recastnavigation/Detour/Include/DetourCommon.h"
#include "../../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "ClientState.hpp"
#include "../NavSources/IQueryFilterProvider.hpp"

/// Custom deleter for dtNavMeshQuery allocated by Detour.
struct NavMeshQueryDeleter
{
    void operator()(dtNavMeshQuery* q) const noexcept { dtFreeNavMeshQuery(q); }
};

using NavMeshQueryPtr = std::unique_ptr<dtNavMeshQuery, NavMeshQueryDeleter>;

class AmeisenNavClient
{
    size_t Id;
    ClientState State;
    IQueryFilterProvider* FilterProvider;

    /// Protects CustomFilter, FilterCustomizations, and State from concurrent access.
    /// QueryFilter() (read path) takes shared lock; UpdateQueryFilter() (write path) takes exclusive lock.
    mutable std::shared_mutex FilterMutex;

    // set per client area costs, to prioritize water movement for example
    std::unique_ptr<dtQueryFilter> CustomFilter;
    std::unordered_map<char, float> FilterCustomizations;

    // Holds a dtNavMeshQuery for every map
    std::unordered_map<int, NavMeshQueryPtr> NavMeshQuery;

    // dtPolyRef buffer for path calculation
    int PolyPathBufferSize;
    std::unique_ptr<dtPolyRef[]> PolyPathBuffer;

public:
    AmeisenNavClient(size_t id, ClientState state, IQueryFilterProvider* filterProvider, int polyPathBufferSize = 512) noexcept
        : Id(id),
        State(state),
        FilterProvider(filterProvider),
        CustomFilter(nullptr),
        FilterCustomizations(),
        NavMeshQuery(),
        PolyPathBufferSize(polyPathBufferSize),
        PolyPathBuffer(nullptr)
    {}

    ~AmeisenNavClient() = default;

    AmeisenNavClient(const AmeisenNavClient&) = delete;
    AmeisenNavClient& operator=(const AmeisenNavClient&) = delete;

    constexpr inline size_t GetId() const noexcept { return Id; }
    constexpr inline const ClientState& GetClientState() const noexcept { return State; }

    inline dtQueryFilter* QueryFilter() noexcept
    {
        std::shared_lock lock(FilterMutex);
        return CustomFilter ? CustomFilter.get() : FilterProvider->Get(State);
    }

    inline dtNavMeshQuery* GetNavmeshQuery(int mapId) noexcept { auto it = NavMeshQuery.find(mapId); return it != NavMeshQuery.end() ? it->second.get() : nullptr; }
    inline void SetNavmeshQuery(int mapId, dtNavMeshQuery* query) { NavMeshQuery[mapId].reset(query); }

    constexpr inline int GetPolyPathBufferSize() const noexcept { return PolyPathBufferSize; }

    inline dtPolyRef* GetPolyPathBuffer()
    {
        if (!PolyPathBuffer) PolyPathBuffer = std::make_unique<dtPolyRef[]>(PolyPathBufferSize);
        return PolyPathBuffer.get();
    }

    inline void ResetQueryFilter() noexcept
    {
        std::unique_lock lock(FilterMutex);
        FilterCustomizations.clear();
    }

    inline void ConfigureQueryFilter(char areaId, float cost)
    {
        std::unique_lock lock(FilterMutex);
        FilterCustomizations[areaId] = cost;
    }

    inline void UpdateQueryFilter(ClientState state)
    {
        std::unique_lock lock(FilterMutex);
        State = state;

        if (!FilterCustomizations.empty())
        {
            const auto baseFilter = FilterProvider->Get(State);
            if (!baseFilter) return;
            auto newFilter = std::make_unique<dtQueryFilter>(*baseFilter);

            for (const auto& [areaId, cost] : FilterCustomizations)
            {
                newFilter->setAreaCost(areaId, cost);
            }

            CustomFilter = std::move(newFilter);
        }
    }
};
