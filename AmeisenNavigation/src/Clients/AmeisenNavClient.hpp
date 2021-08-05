#pragma once

#include "../../recastnavigation/Detour/Include/DetourCommon.h"
#include "../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

class AmeisenNavClient
{
    int Id;
    std::unordered_map<int, dtNavMeshQuery*> NavMeshQuery;
    size_t BufferSize;
    dtPolyRef* PolyPathBuffer;
    dtPolyRef* MiscPathBuffer;

public:
    AmeisenNavClient(int id, size_t polypathBufferSize)
        : Id(id),
        BufferSize(polypathBufferSize),
        NavMeshQuery(),
        PolyPathBuffer(new dtPolyRef[polypathBufferSize]),
        MiscPathBuffer(nullptr)
    {}

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
    inline dtNavMeshQuery* GetNavmeshQuery(int mapId) noexcept { return NavMeshQuery[mapId]; }
    inline dtPolyRef* GetPolyPathBuffer() noexcept { return PolyPathBuffer; }
    inline dtPolyRef* GetMiscPathBuffer() noexcept { return MiscPathBuffer ? MiscPathBuffer : MiscPathBuffer = new dtPolyRef[BufferSize]; }

    inline void SetNavmeshQuery(int mapId, dtNavMeshQuery* query) noexcept { NavMeshQuery[mapId] = query; }
};