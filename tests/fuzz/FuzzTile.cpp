// Fuzz Detour tile validation: whatever ValidateTileData accepts must be safe to add to a navmesh and query.

#include <cstdint>
#include <cstring>

#include "DetourUtils.hpp"

namespace {
float Random01()
{
    static uint32_t state = 12345;
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 16777216.0f;
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (!ValidateTileData(data, size))
    {
        return 0;
    }

    dtMeshHeader header;
    std::memcpy(&header, data, sizeof(header));

    // A grid whose cell (header.x, header.y) is this tile, so position based lookups find it.
    dtNavMeshParams params{};
    const float width = std::max(header.bmax[0] - header.bmin[0], 1.0f);
    const float height = std::max(header.bmax[2] - header.bmin[2], 1.0f);
    params.orig[0] = header.bmin[0] - static_cast<float>(header.x) * width;
    params.orig[1] = header.bmin[1];
    params.orig[2] = header.bmin[2] - static_cast<float>(header.y) * height;
    params.tileWidth = width;
    params.tileHeight = height;
    params.maxTiles = 4;
    params.maxPolys = 1 << 16;

    NavMeshPtr navMesh(dtAllocNavMesh());

    if (!navMesh || dtStatusFailed(navMesh->init(&params)))
    {
        return 0;
    }

    auto* tile = static_cast<unsigned char*>(dtAlloc(size, DT_ALLOC_PERM));
    std::memcpy(tile, data, size);

    if (dtStatusFailed(navMesh->addTile(tile, static_cast<int>(size), DT_TILE_FREE_DATA, 0, nullptr)))
    {
        dtFree(tile);
        return 0;
    }

    NavMeshQueryPtr query(dtAllocNavMeshQuery());

    if (!query || dtStatusFailed(query->init(navMesh.get(), 256)))
    {
        return 0;
    }

    dtQueryFilter filter;
    const float center[3]{(header.bmin[0] + header.bmax[0]) * 0.5f, (header.bmin[1] + header.bmax[1]) * 0.5f,
                          (header.bmin[2] + header.bmax[2]) * 0.5f};
    const float extents[3]{width * 0.5f + 1.0f, (header.bmax[1] - header.bmin[1]) * 0.5f + 1.0f,
                           height * 0.5f + 1.0f};

    dtPolyRef startRef = 0;
    dtPolyRef endRef = 0;
    float startPos[3]{};
    float endPos[3]{};
    query->findNearestPoly(center, extents, &filter, &startRef, startPos);
    query->findNearestPoly(header.bmin, extents, &filter, &endRef, endPos);

    dtPolyRef polys[64]{};
    int polyCount = 0;
    query->queryPolygons(center, extents, &filter, polys, &polyCount, 64);

    if (!startRef || !endRef)
    {
        return 0;
    }

    dtPolyRef path[64]{};
    int pathCount = 0;
    query->findPath(startRef, endRef, startPos, endPos, &filter, path, &pathCount, 64);

    if (pathCount > 0)
    {
        float straight[64 * 3]{};
        unsigned char flags[64]{};
        dtPolyRef refs[64]{};
        int straightCount = 0;
        query->findStraightPath(startPos, endPos, path, pathCount, straight, flags, refs, &straightCount, 64);
    }

    dtRaycastHit hit{};
    hit.path = path;
    hit.maxPath = 64;
    query->raycast(startRef, startPos, endPos, &filter, 0, &hit);

    float result[3]{};
    dtPolyRef visited[16]{};
    int visitedCount = 0;
    query->moveAlongSurface(startRef, startPos, endPos, &filter, result, visited, &visitedCount, 16);

    float h = 0.0f;
    query->getPolyHeight(startRef, startPos, &h);

    float closest[3]{};
    bool overPoly = false;
    query->closestPointOnPoly(startRef, center, closest, &overPoly);

    float hitDist = 0.0f;
    float hitPos[3]{};
    float hitNormal[3]{};
    query->findDistanceToWall(startRef, startPos, 5.0f, &filter, &hitDist, hitPos, hitNormal);

    dtPolyRef randomRef = 0;
    float randomPt[3]{};
    query->findRandomPoint(&filter, Random01, &randomRef, randomPt);
    query->findRandomPointAroundCircle(startRef, startPos, 10.0f, &filter, Random01, &randomRef, randomPt);
    return 0;
}
