#include "ameisennavigation.h"

std::string AmeisenNavigation::FormatTrailingZeros(const int number, const int zeroCount)
{
    std::stringstream ss;
    ss << std::setw(zeroCount) << std::setfill('0') << number;
    return ss.str();
}

bool AmeisenNavigation::IsMmapLoaded(const int map_id)
{
    return mNavMeshMap[map_id] != nullptr
        && mNavMeshQueryMap[map_id] != nullptr;
}

bool AmeisenNavigation::GetPath(const int mapId, const Vector3& startPosition, const Vector3& endPosition, Vector3* path, int* pathSize)
{
    D(std::cout << ">> GetPath (" << mapId << ") " << startPosition << " -> " << endPosition << std::endl);

    Vector3 startPositionRd = Vector3(startPosition);
    WowToRDCoords(startPositionRd);

    Vector3 endPositionRd = Vector3(endPosition);
    WowToRDCoords(endPositionRd);

    D(std::cout << ">> GetPath RD Coordinates (" << mapId << ") " << startPositionRd << " -> " << endPositionRd << std::endl);

    if (!PreparePathfinding(mapId))
    {
        *pathSize = 0;
        return false;
    }

    Vector3 closestPointStart;
    Vector3 closestPointEnd;

    dtPolyRef startPoly = GetNearestPoly(mapId, startPositionRd, &closestPointStart);
    dtPolyRef endPoly = GetNearestPoly(mapId, endPositionRd, &closestPointEnd);

    D(std::cout << ">> closestPointStart (" << mapId << ") " << closestPointStart << std::endl);
    D(std::cout << ">> closestPointEnd (" << mapId << ") " << closestPointEnd << std::endl);

    if (startPoly == endPoly)
    {
        // same poly, we don't need pathfinding here
        D(std::cout << ">> Start and End positions are on the same poly, returning end position" << std::endl);

        *pathSize = 1;
        path[0] = endPosition;
        return true;
    }
    else
    {
        const std::unique_ptr<dtPolyRef> polypath(new dtPolyRef[maxPolyPath]);
        int polypathSize = 0;

        if (dtStatusSucceed(mNavMeshQueryMap[mapId]->findPath(startPoly, endPoly, reinterpret_cast<const float*>(&closestPointStart), reinterpret_cast<const float*>(&closestPointEnd), &mQueryFilter, polypath.get(), &polypathSize, maxPolyPath)))
        {
            D(std::cout << ">> PolyPath size: " << polypathSize << "/" << maxPolyPath << std::endl);

            if (dtStatusSucceed(mNavMeshQueryMap[mapId]->findStraightPath(reinterpret_cast<const float*>(&closestPointStart), reinterpret_cast<const float*>(&closestPointEnd), polypath.get(), polypathSize, reinterpret_cast<float*>(path), nullptr, nullptr, pathSize, maxPointPath)))
            {
                D(std::cout << ">> PointPath size: " << (*pathSize) << "/" << maxPointPath << std::endl);

                // convert to Recast and Detour coordinates to Wow coordinates
                for (int i = 0; i < (*pathSize); ++i)
                {
                    RDToWowCoords(path[i]);
                }

                return true;
            }
            else
            {
                std::cerr << ">> findStraightPath failed (mapId: " << mapId << ")" << std::endl;
            }
        }
        else
        {
            std::cerr << ">> findPath failed (mapId: " << mapId << ")" << std::endl;
        }

        *pathSize = 0;
        path = nullptr;
        return false;
    }
}

bool AmeisenNavigation::PreparePathfinding(int mapId)
{
    if (!IsMmapLoaded(mapId))
    {
        D(std::cout << ">> Mesh for Continent " << mapId << " not loaded, loading it now" << std::endl);

        if (!LoadMmapsForContinent(mapId))
        {
            std::cerr << ">> Unable to load MMAPS for continent " << mapId << std::endl;

            return false;
        }
    }

    return true;
}

bool AmeisenNavigation::MoveAlongSurface(const int mapId, const Vector3& startPosition, const Vector3& endPosition, Vector3* positionToGoTo)
{
    D(std::cout << ">> MoveAlongSurface (" << mapId << ") " << startPosition << " -> " << endPosition << std::endl);

    Vector3 startPositionRd = Vector3(startPosition);
    WowToRDCoords(startPositionRd);

    Vector3 endPositionRd = Vector3(endPosition);
    WowToRDCoords(endPositionRd);

    if (!PreparePathfinding(mapId))
    {
        return false;
    }

    int visitedCount;
    dtPolyRef visited[64];
    dtPolyRef startPoly = GetNearestPoly(mapId, startPositionRd, nullptr);

    if (dtStatusSucceed(mNavMeshQueryMap[mapId]->moveAlongSurface(startPoly, reinterpret_cast<const float*>(&startPositionRd), reinterpret_cast<const float*>(&endPositionRd), &mQueryFilter, reinterpret_cast<float*>(positionToGoTo), visited, &visitedCount, 64)))
    {
        RDToWowCoords(*positionToGoTo);
        D(std::cout << ">> Found position to go to " << (*positionToGoTo) << std::endl);

        return true;
    }

    return false;
}

bool AmeisenNavigation::GetRandomPoint(const int mapId, Vector3* position)
{
    D(std::cout << ">> GetRandomPoint (" << mapId << ")" << std::endl);

    if (!PreparePathfinding(mapId))
    {
        return false;
    }

    dtPolyRef polyRef;

    if (dtStatusSucceed(mNavMeshQueryMap[mapId]->findRandomPoint(&mQueryFilter, []() { return (float)rand() / (float)(RAND_MAX); }, &polyRef, reinterpret_cast<float*>(position))))
    {
        RDToWowCoords(*position);
        D(std::cout << ">> Found random position " << *position << std::endl);
        return true;
    }

    return false;
}

bool AmeisenNavigation::GetRandomPointAround(const int mapId, const Vector3& startPosition, float radius, Vector3* position)
{
    D(std::cout << ">> GetRandomPointAround (" << mapId << ") startPosition: " << startPosition << " radius: " << radius << std::endl);

    if (!PreparePathfinding(mapId))
    {
        return false;
    }

    Vector3 startPositionRd = Vector3(startPosition);
    WowToRDCoords(startPositionRd);

    Vector3 closestPoint;
    dtPolyRef startPoly = GetNearestPoly(mapId, startPositionRd, &closestPoint);

    D(std::cout << ">> GetRandomPointAround (" << mapId << ") closestPoint: " << closestPoint << " startPoly: " << startPoly << std::endl);

    dtPolyRef polyRef;

    if (dtStatusSucceed(mNavMeshQueryMap[mapId]->findRandomPointAroundCircle(startPoly, reinterpret_cast<const float*>(&closestPoint), radius, &mQueryFilter, []() { return (float)rand() / (float)(RAND_MAX); }, &polyRef, reinterpret_cast<float*>(position))))
    {
        RDToWowCoords(*position);
        D(std::cout << ">> Found random position " << *position << std::endl);
        return true;
    }

    return false;
}

bool AmeisenNavigation::CastMovementRay(const int mapId, const Vector3& startPosition, const Vector3& endPosition)
{
    D(std::cout << ">> CastMovementRay (" << mapId << ") " << startPosition << " -> " << endPosition << std::endl);

    Vector3 startPositionRd = Vector3(startPosition);
    WowToRDCoords(startPositionRd);

    Vector3 endPositionRd = Vector3(endPosition);
    WowToRDCoords(endPositionRd);

    if (!PreparePathfinding(mapId))
    {
        return false;
    }

    dtPolyRef startPoly = GetNearestPoly(mapId, startPositionRd, nullptr);
    dtRaycastHit raycastHit;

    dtStatus result = mNavMeshQueryMap[mapId]->raycast(startPoly, reinterpret_cast<const float*>(&startPositionRd), reinterpret_cast<const float*>(&endPositionRd), &mQueryFilter, 0, &raycastHit);

    return dtStatusSucceed(result) && raycastHit.t == FLT_MAX;
}

dtPolyRef AmeisenNavigation::GetNearestPoly(const int mapId, const Vector3& position, Vector3* closestPointOnPoly)
{
    float extents[3] = { 12.0f, 12.0f, 12.0f };

    dtPolyRef polyRef;
    mNavMeshQueryMap[mapId]->findNearestPoly(reinterpret_cast<const float*>(&position), extents, &mQueryFilter, &polyRef, reinterpret_cast<float*>(closestPointOnPoly));

    return polyRef;
}

bool AmeisenNavigation::LoadMmapsForContinent(const int mapId)
{
    // build the *.mmap filename (example: 001.mmap or 587.mmap)
    std::string mmapFilename = mMmapFolder;
    mmapFilename += FormatTrailingZeros(mapId, 3);
    mmapFilename += ".mmap";

    // we can't load non existent files
    if (!std::filesystem::exists(mmapFilename))
    {
        std::cerr << ">> Unable to find mmap file " << " (mapId: " << mapId << ", mmapFilename: \"" << mmapFilename << "\")" << std::endl;
        return false;
    }

    std::ifstream mmapStream;
    mmapStream.open(mmapFilename, std::ifstream::binary);

    // read the dtNavMeshParams
    dtNavMeshParams params;
    mmapStream.read(reinterpret_cast<char*>(&params), sizeof(dtNavMeshParams));
    mmapStream.close();

    // allocate and init the NavMesh
    mNavMeshMap[mapId] = dtAllocNavMesh();

    if (dtStatusFailed(mNavMeshMap[mapId]->init(&params)))
    {
        std::cerr << ">> Unable to init NavMesh " << " (mapId: " << mapId << ", mmapFilename: \"" << mmapFilename << "\")" << std::endl;
        dtFreeNavMesh(mNavMeshMap[mapId]);
        return false;
    }

    // load every NavMesh Tile from 1, 1 to 64, 64
    for (int x = 1; x <= 64; ++x)
    {
        for (int y = 1; y <= 64; ++y)
        {
            std::string mmapTileFilename = mMmapFolder;
            mmapTileFilename += FormatTrailingZeros(mapId, 3);
            mmapTileFilename += FormatTrailingZeros(x, 2);
            mmapTileFilename += FormatTrailingZeros(y, 2);
            mmapTileFilename += ".mmtile";

            // we can't load non existent files
            if (!std::filesystem::exists(mmapTileFilename))
            {
                continue;
            }

            D(std::cout << ">> Reading dtTile " << mmapTileFilename.c_str() << std::endl);

            std::ifstream mmapTileStream;
            mmapTileStream.open(mmapTileFilename, std::ifstream::binary);

            // read the mmap header
            MmapTileHeader mmapTileHeader;
            mmapTileStream.read(reinterpret_cast<char*>(&mmapTileHeader), sizeof(MmapTileHeader));

            if (mmapTileHeader.mmapMagic != MMAP_MAGIC)
            {
                std::cerr << ">> Wrong MMAP Magic (got:" << mmapTileHeader.mmapMagic << ", expected:" << MMAP_MAGIC << ") dtTile " << x << " " << y << " (mapId: " << mapId << ")" << std::endl;
                return false;
            }

            if (mmapTileHeader.mmapVersion < MMAP_VERSION)
            {
                std::cerr << ">> Wrong MMAP version (got:" << mmapTileHeader.mmapVersion << ", expected:" << MMAP_VERSION << ") dtTile " << x << " " << y << " (mapId: " << mapId << ")" << std::endl;
                return false;
            }

            // read the NavMesh Tile data
            void* mmapTileData = dtAlloc(mmapTileHeader.size, DT_ALLOC_PERM);
            mmapTileStream.read(reinterpret_cast<char*>(mmapTileData), mmapTileHeader.size);
            mmapTileStream.close();

            // add the Tile to our NavMesh
            dtTileRef tileRef;

            if (dtStatusFailed(mNavMeshMap[mapId]->addTile(reinterpret_cast<unsigned char*>(mmapTileData), mmapTileHeader.size, DT_TILE_FREE_DATA, 0, &tileRef)))
            {
                std::cerr << ">> Failed to add dtTile " << x << " " << y << " to NavMesh (mapId: " << mapId << ")" << std::endl;

                dtFree(mmapTileData);
                dtFreeNavMesh(mNavMeshMap[mapId]);
                return false;
            }
        }
    }

    // init the NavMeshQuery
    mNavMeshQueryMap[mapId] = dtAllocNavMeshQuery();

    if (dtStatusFailed(mNavMeshQueryMap[mapId]->init(mNavMeshMap[mapId], 65535)))
    {
        std::cerr << ">> Failed to init NavMeshQuery (mapId: " << mapId << ")" << std::endl;

        dtFreeNavMeshQuery(mNavMeshQueryMap[mapId]);
        dtFreeNavMesh(mNavMeshMap[mapId]);
        return false;
    }

    return true;
}