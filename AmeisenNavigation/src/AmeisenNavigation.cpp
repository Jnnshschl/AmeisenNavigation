#include "AmeisenNavigation.hpp"

bool AmeisenNavigation::GetPath(int mapId, Vector3 startPosition, Vector3 endPosition, Vector3* path, int* pathSize) noexcept
{
    if (!LoadMapIfNeeded(mapId))
    {
        *pathSize = 0;
        path = nullptr;
        return false;
    }

    DEBUG_ONLY(std::cout << ">> GetPath (" << mapId << ") " << startPosition << " -> " << endPosition << std::endl);

    WowToRDCoords(&startPosition);
    WowToRDCoords(&endPosition);

    dtPolyRef startPoly = GetNearestPoly(mapId, startPosition, &startPosition);
    dtPolyRef endPoly = GetNearestPoly(mapId, endPosition, &endPosition);

    DEBUG_ONLY(std::cout << ">> Closest point start: " << startPosition << std::endl);
    DEBUG_ONLY(std::cout << ">> Closest point end: " << endPosition << std::endl);

    if (dtStatusSucceed(NavMeshQueryMap[mapId]->findPath(startPoly, endPoly, startPosition, endPosition, &QueryFilter, PolyPathBuffer, pathSize, MaxPolyPath)))
    {
        DEBUG_ONLY(std::cout << ">> PolyPath size: " << *pathSize << "/" << MaxPolyPath << std::endl);

        if (dtStatusSucceed(NavMeshQueryMap[mapId]->findStraightPath(startPosition, endPosition, PolyPathBuffer, *pathSize, reinterpret_cast<float*>(path), nullptr, nullptr, pathSize, MaxPointPath)))
        {
            DEBUG_ONLY(std::cout << ">> PointPath size: " << (*pathSize) << "/" << MaxPointPath << std::endl);

            for (int i = 0; i < (*pathSize); ++i)
            {
                RDToWowCoords(&path[i]);
            }

            return true;
        }
    }

    *pathSize = 0;
    path = nullptr;
    return false;
}

bool AmeisenNavigation::MoveAlongSurface(int mapId, Vector3 startPosition, Vector3 endPosition, Vector3* positionToGoTo) noexcept
{
    if (!LoadMapIfNeeded(mapId)) { return false; }

    DEBUG_ONLY(std::cout << ">> MoveAlongSurface (" << mapId << ") " << startPosition << " -> " << endPosition << std::endl);

    WowToRDCoords(&startPosition);
    WowToRDCoords(&endPosition);

    int visitedCount;
    dtPolyRef visited[16];

    if (dtStatusSucceed(NavMeshQueryMap[mapId]->moveAlongSurface(GetNearestPoly(mapId, startPosition, nullptr), startPosition, endPosition, &QueryFilter, reinterpret_cast<float*>(positionToGoTo), visited, &visitedCount, 16)))
    {
        RDToWowCoords(positionToGoTo);
        DEBUG_ONLY(std::cout << ">> Found position to go to: " << (*positionToGoTo) << std::endl);
        return true;
    }

    return false;
}

bool AmeisenNavigation::GetRandomPoint(int mapId, Vector3* position) noexcept
{
    if (!LoadMapIfNeeded(mapId)) { return false; }

    DEBUG_ONLY(std::cout << ">> GetRandomPoint (" << mapId << ")" << std::endl);

    dtPolyRef polyRef;
    static auto randomProvider = []() { return static_cast<float>(rand()) / MAX_RAND_F; };

    if (dtStatusSucceed(NavMeshQueryMap[mapId]->findRandomPoint(&QueryFilter, randomProvider, &polyRef, reinterpret_cast<float*>(position))))
    {
        RDToWowCoords(position);
        DEBUG_ONLY(std::cout << ">> Found random position: " << *position << std::endl);
        return true;
    }

    return false;
}

bool AmeisenNavigation::GetRandomPointAround(int mapId, Vector3 startPosition, float radius, Vector3* position) noexcept
{
    if (!LoadMapIfNeeded(mapId)) { return false; }

    DEBUG_ONLY(std::cout << ">> GetRandomPointAround (" << mapId << ") startPosition: " << startPosition << " radius: " << radius << std::endl);

    WowToRDCoords(&startPosition);

    dtPolyRef polyRef;
    static auto randomProvider = []() { return static_cast<float>(rand()) / MAX_RAND_F; };

    if (dtStatusSucceed(NavMeshQueryMap[mapId]->findRandomPointAroundCircle(GetNearestPoly(mapId, startPosition, &startPosition), startPosition, radius, &QueryFilter, randomProvider, &polyRef, reinterpret_cast<float*>(position))))
    {
        RDToWowCoords(position);
        DEBUG_ONLY(std::cout << ">> Found random position: " << *position << std::endl);
        return true;
    }

    return false;
}

bool AmeisenNavigation::CastMovementRay(int mapId, Vector3 startPosition, Vector3 endPosition, dtRaycastHit* raycastHit) noexcept
{
    if (!LoadMapIfNeeded(mapId)) { return false; }

    DEBUG_ONLY(std::cout << ">> CastMovementRay (" << mapId << ") " << startPosition << " -> " << endPosition << std::endl);

    WowToRDCoords(&startPosition);
    WowToRDCoords(&endPosition);

    return dtStatusSucceed(NavMeshQueryMap[mapId]->raycast(GetNearestPoly(mapId, startPosition, nullptr), startPosition, endPosition, &QueryFilter, 0, raycastHit))
        && raycastHit->t == FLT_MAX;
}

dtPolyRef AmeisenNavigation::GetNearestPoly(int mapId, const Vector3& position, Vector3* closestPointOnPoly) noexcept
{
    static float extents[3] = { 6.0f, 6.0f, 6.0f };
    dtPolyRef polyRef;

    return dtStatusSucceed(NavMeshQueryMap[mapId]->findNearestPoly(position, extents, &QueryFilter, &polyRef, reinterpret_cast<float*>(closestPointOnPoly))) ?
        polyRef : 0;
}

bool AmeisenNavigation::LoadMmapsForContinent(int mapId) noexcept
{
    // build the *.mmap filename (example: 001.mmap or 587.mmap)
    std::stringstream mmapFilename;
    mmapFilename << std::setw(3) << std::setfill('0') << mapId << ".mmap";

    std::filesystem::path mmapFile(MmapFolder);
    mmapFile.append(mmapFilename.str());

    if (!std::filesystem::exists(mmapFile))
    {
        return false;
    }

    std::ifstream mmapStream;
    mmapStream.open(mmapFile, std::ifstream::binary);

    dtNavMeshParams params;
    mmapStream.read(reinterpret_cast<char*>(&params), sizeof(dtNavMeshParams));
    mmapStream.close();

    NavMeshMap[mapId] = dtAllocNavMesh();

    if (dtStatusFailed(NavMeshMap[mapId]->init(&params)))
    {
        dtFreeNavMesh(NavMeshMap[mapId]);
        return false;
    }

    for (int x = 1; x <= 64; ++x)
    {
        for (int y = 1; y <= 64; ++y)
        {
            std::stringstream mmapTileFilename;
            mmapTileFilename << std::setfill('0') << std::setw(3) << mapId << std::setw(2) << x << std::setw(2) << y << ".mmtile";

            std::filesystem::path mmapTileFile(MmapFolder);
            mmapTileFile.append(mmapTileFilename.str());

            if (!std::filesystem::exists(mmapTileFile))
            {
                continue;
            }

            DEBUG_ONLY(std::cout << ">> Reading dtTile " << mmapTileFile << std::endl);

            std::ifstream mmapTileStream;
            mmapTileStream.open(mmapTileFile, std::ifstream::binary);

            MmapTileHeader mmapTileHeader;
            mmapTileStream.read(reinterpret_cast<char*>(&mmapTileHeader), sizeof(MmapTileHeader));

            if (mmapTileHeader.mmapMagic != MMAP_MAGIC
                || mmapTileHeader.mmapVersion < MMAP_VERSION)
            {
                return false;
            }

            char* mmapTileData = reinterpret_cast<char*>(dtAlloc(mmapTileHeader.size, DT_ALLOC_PERM));
            mmapTileStream.read(mmapTileData, mmapTileHeader.size);
            mmapTileStream.close();

            dtTileRef tileRef;

            if (dtStatusFailed(NavMeshMap[mapId]->addTile(reinterpret_cast<unsigned char*>(mmapTileData), mmapTileHeader.size, DT_TILE_FREE_DATA, 0, &tileRef)))
            {
                dtFree(mmapTileData);
                dtFreeNavMesh(NavMeshMap[mapId]);
                return false;
            }
        }
    }

    NavMeshQueryMap[mapId] = dtAllocNavMeshQuery();

    if (dtStatusFailed(NavMeshQueryMap[mapId]->init(NavMeshMap[mapId], 65535)))
    {
        dtFreeNavMeshQuery(NavMeshQueryMap[mapId]);
        dtFreeNavMesh(NavMeshMap[mapId]);
        return false;
    }

    return true;
}