#pragma once

#include "../../../recastnavigation/Detour/Include/DetourCommon.h"
#include "../../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "ClientState.hpp"
#include "QueryFilterProvider.hpp"

#include "../Mmap/MmapFormat.hpp"

class AmeisenNavClient
{
    size_t Id;
    MmapFormat Format;
    ClientState State;
    QueryFilterProvider* FilterProvider;

    // set per client area costs, to prioritize water movement for example
    dtQueryFilter* CustomFilter;
    std::unordered_map<char, float> FilterCustomizations;

    // Holds a dtNavMeshQuery for every map
    std::unordered_map<int, dtNavMeshQuery*> NavMeshQuery;

    // dtPolyRef buffer for path calculation
    int PolyPathBufferSize;
    dtPolyRef* PolyPathBuffer;

public:
    /// <summary>
    /// Initializes a new AmeisenNavClient, that is used to perform queries on the navmesh.
    /// </summary>
    /// <param name="id">Id of the client.</param>
    /// <param name="mmapFormat">MMAP format to use.</param>
    /// <param name="state">Current ClientState.</param>
    /// <param name="filterProvider">QueryFilterProvider to use.</param>
    /// <param name="polyPathBufferSize">Size of the polypath buffer for recast and detour.</param>
    AmeisenNavClient(size_t id, MmapFormat mmapFormat, ClientState state, QueryFilterProvider* filterProvider, int polyPathBufferSize = 512) noexcept
        : Id(id),
        Format(mmapFormat),
        State(state),
        FilterProvider(filterProvider),
        CustomFilter(nullptr),
        FilterCustomizations(),
        NavMeshQuery(),
        PolyPathBufferSize(polyPathBufferSize),
        PolyPathBuffer(nullptr)
    {}

    ~AmeisenNavClient() noexcept
    {
        for (const auto& query : NavMeshQuery)
        {
            dtFreeNavMeshQuery(query.second);
        }

        if (PolyPathBuffer) delete[] PolyPathBuffer;
        if (CustomFilter) delete CustomFilter;
    }

    AmeisenNavClient(const AmeisenNavClient&) = delete;
    AmeisenNavClient& operator=(const AmeisenNavClient&) = delete;

    constexpr inline size_t GetId() const noexcept { return Id; }
    constexpr inline MmapFormat& GetMmapFormat() noexcept { return Format; }
    constexpr inline ClientState& GetClientState() noexcept { return State; }

    inline dtQueryFilter* QueryFilter() noexcept { return CustomFilter ? CustomFilter : FilterProvider->Get(Format, State); }

    inline dtNavMeshQuery* GetNavmeshQuery(int mapId) noexcept { return NavMeshQuery[mapId]; }
    inline void SetNavmeshQuery(int mapId, dtNavMeshQuery* query) noexcept { NavMeshQuery[mapId] = query; }

    constexpr inline int GetPolyPathBufferSize() const noexcept { return PolyPathBufferSize; }
    constexpr inline dtPolyRef* GetPolyPathBuffer() noexcept { return PolyPathBuffer ? PolyPathBuffer : PolyPathBuffer = new dtPolyRef[PolyPathBufferSize]; }

    inline void ResetQueryFilter() noexcept { FilterCustomizations.clear(); }
    inline void ConfigureQueryFilter(char areaId, float cost) noexcept { FilterCustomizations[areaId] = cost; }

    inline void UpdateQueryFilter(ClientState state) noexcept
    {
        State = state;

        if (!FilterCustomizations.empty())
        {
            const auto baseFilter = FilterProvider->Get(Format, State);
            dtQueryFilter* newFilter = new dtQueryFilter(*baseFilter);

            for (const auto& [areaId, cost] : FilterCustomizations)
            {
                newFilter->setAreaCost(areaId, cost);
            }

            dtQueryFilter* oldFilter = CustomFilter;
            CustomFilter = newFilter;

            if (oldFilter && CustomFilter != oldFilter) delete oldFilter;
        }
    }
};
