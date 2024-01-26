#pragma once

#include "../../recastnavigation/Detour/Include/DetourCommon.h"
#include "../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "Mmap/MmapFormat.hpp"

#include "335a/NavArea335a.hpp"
#include "548/NavArea548.hpp"

class AmeisenNavClient
{
    size_t Id;
    MmapFormat Format;
    dtQueryFilter Filter;

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
    /// <param name="polyPathBufferSize">Size of the polypath buffer for recast and detour.</param>
    AmeisenNavClient(size_t id, MmapFormat mmapFormat, int polyPathBufferSize = 512) noexcept
        : Id(id),
        Format(mmapFormat),
        NavMeshQuery(),
        PolyPathBufferSize(polyPathBufferSize),
        PolyPathBuffer(nullptr),
        Filter()
    {
        switch (mmapFormat)
        {
        case MmapFormat::TC335A:
            Filter.setIncludeFlags(static_cast<char>(NavArea335a::GROUND) | static_cast<char>(NavArea335a::WATER));
            Filter.setExcludeFlags(static_cast<char>(NavArea335a::EMPTY) | static_cast<char>(NavArea335a::GROUND_STEEP) | static_cast<char>(NavArea335a::MAGMA_SLIME));
            break;

        case MmapFormat::SF548:
            Filter.setIncludeFlags(static_cast<char>(NavArea548::GROUND) | static_cast<char>(NavArea548::WATER));
            Filter.setExcludeFlags(static_cast<char>(NavArea548::EMPTY) | static_cast<char>(NavArea548::MAGMA) | static_cast<char>(NavArea548::SLIME));
            break;

        default:
            break;
        }
    }

    ~AmeisenNavClient() noexcept
    {
        for (const auto& query : NavMeshQuery)
        {
            dtFreeNavMeshQuery(query.second);
        }

        if (PolyPathBuffer) delete[] PolyPathBuffer;
    }

    AmeisenNavClient(const AmeisenNavClient&) = delete;
    AmeisenNavClient& operator=(const AmeisenNavClient&) = delete;

    constexpr inline size_t GetId() const noexcept { return Id; }

    constexpr inline MmapFormat& GetMmapFormat() noexcept { return Format; }

    constexpr inline dtQueryFilter& QueryFilter() noexcept { return Filter; }

    inline dtNavMeshQuery* GetNavmeshQuery(int mapId) noexcept { return NavMeshQuery[mapId]; }
    inline void SetNavmeshQuery(int mapId, dtNavMeshQuery* query) noexcept { NavMeshQuery[mapId] = query; }

    constexpr inline int GetPolyPathBufferSize() const noexcept { return PolyPathBufferSize; }
    constexpr inline dtPolyRef* GetPolyPathBuffer() noexcept { return PolyPathBuffer ? PolyPathBuffer : PolyPathBuffer = new dtPolyRef[PolyPathBufferSize]; }
};