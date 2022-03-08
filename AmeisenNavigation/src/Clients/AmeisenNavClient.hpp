#pragma once

#include "../../recastnavigation/Detour/Include/DetourCommon.h"
#include "../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "335a/NavArea335a.hpp"
#include "548/NavArea548.hpp"

#include "ClientVersion.hpp"


class AmeisenNavClient
{
    int Id;
    CLIENT_VERSION CVersion;
    dtQueryFilter Filter;
    std::unordered_map<int, dtNavMeshQuery*> NavMeshQuery;
    size_t BufferSize;
    dtPolyRef* PolyPathBuffer;
    dtPolyRef* MiscPathBuffer;

public:

    AmeisenNavClient(int id, CLIENT_VERSION version, size_t polypathBufferSize)
        : Id(id),
        CVersion(version),
        BufferSize(polypathBufferSize),
        NavMeshQuery(),
        PolyPathBuffer(new dtPolyRef[polypathBufferSize]),
        MiscPathBuffer(nullptr),
        Filter()
    {
        switch (version)
        {
        case CLIENT_VERSION::V335A:
            Filter.setIncludeFlags(static_cast<char>(NavArea335a::GROUND) | static_cast<char>(NavArea335a::WATER));
            Filter.setExcludeFlags(static_cast<char>(NavArea335a::EMPTY) | static_cast<char>(NavArea335a::GROUND_STEEP) | static_cast<char>(NavArea335a::MAGMA_SLIME));
            break;

        case CLIENT_VERSION::V548:
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

    inline int GetId() noexcept { return Id; }
    inline dtQueryFilter& QueryFilter() noexcept { return Filter; }
    inline dtNavMeshQuery* GetNavmeshQuery(int mapId) noexcept { return NavMeshQuery[mapId]; }
    inline dtPolyRef* GetPolyPathBuffer() noexcept { return PolyPathBuffer; }
    inline dtPolyRef* GetMiscPathBuffer() noexcept { return MiscPathBuffer ? MiscPathBuffer : MiscPathBuffer = new dtPolyRef[BufferSize]; }

    inline void SetNavmeshQuery(int mapId, dtNavMeshQuery* query) noexcept { NavMeshQuery[mapId] = query; }
};