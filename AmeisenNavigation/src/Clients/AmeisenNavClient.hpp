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
    std::unordered_map<int, dtNavMeshQuery*> NavMeshQuery;
    size_t BufferSize;
    dtPolyRef* PolyPathBuffer;
    dtPolyRef* MiscPathBuffer;

public:
    AmeisenNavClient(size_t id, MmapFormat mmapFormat, size_t polypathBufferSize)
        : Id(id),
        Format(mmapFormat),
        BufferSize(polypathBufferSize),
        NavMeshQuery(),
        PolyPathBuffer(new dtPolyRef[polypathBufferSize]),
        MiscPathBuffer(nullptr),
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

    ~AmeisenNavClient()
    {
        for (const auto& query : NavMeshQuery)
        {
            dtFreeNavMeshQuery(query.second);
        }

        delete[] PolyPathBuffer;

        if (MiscPathBuffer)
        {
            delete[] MiscPathBuffer;
        }
    }

    AmeisenNavClient(const AmeisenNavClient&) = delete;
    AmeisenNavClient& operator=(const AmeisenNavClient&) = delete;

    constexpr size_t GetId() const noexcept { return Id; }
    constexpr MmapFormat& GetMmapFormat() noexcept { return Format; }
    constexpr dtQueryFilter& QueryFilter() noexcept { return Filter; }
    constexpr dtPolyRef* GetPolyPathBuffer() noexcept { return PolyPathBuffer; }
    constexpr dtPolyRef* GetMiscPathBuffer() noexcept { return MiscPathBuffer ? MiscPathBuffer : MiscPathBuffer = new dtPolyRef[BufferSize]; }

    inline dtNavMeshQuery* GetNavmeshQuery(int mapId) noexcept { return NavMeshQuery[mapId]; }

    inline void SetNavmeshQuery(int mapId, dtNavMeshQuery* query) noexcept { NavMeshQuery[mapId] = query; }
};