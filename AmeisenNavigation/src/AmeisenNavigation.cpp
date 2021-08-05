#include "AmeisenNavigation.hpp"

void AmeisenNavigation::NewClient(int id) noexcept
{
    if (IsValidClient(id)) { return; }

    ANAV_DEBUG_ONLY(std::cout << ">> New Client: " << id << std::endl);
    Clients[id] = new AmeisenNavClient(id, MaxPathNodes);
}

void AmeisenNavigation::FreeClient(int id) noexcept
{
    if (!IsValidClient(id)) { return; }

    delete Clients[id];
    Clients[id] = nullptr;

    ANAV_DEBUG_ONLY(std::cout << ">> Freed Client: " << id << std::endl);
}

bool AmeisenNavigation::GetPath(int clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize) noexcept
{
    if (!IsValidClient(clientId) || !InitQueryAndLoadMmaps(clientId, mapId)) { return false; }

    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetPath (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    if (CalculateNormalPath(clientId, mapId, startPosition, endPosition, path, pathSize))
    {
        for (int i = 0; i < *pathSize; i += 3)
        {
            RDToWowCoords(path + i);
        }

        return true;
    }

    return false;
}

bool AmeisenNavigation::GetRandomPath(int clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize, float maxRandomDistance) noexcept
{
    if (!IsValidClient(clientId) || !InitQueryAndLoadMmaps(clientId, mapId)) { return false; }

    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetRandomPath (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    dtPolyRef* visitedBuffer = Clients[clientId]->GetMiscPathBuffer();

    if (CalculateNormalPath(clientId, mapId, startPosition, endPosition, path, pathSize, visitedBuffer))
    {
        dtPolyRef randomRef;

        for (int i = 0; i < *pathSize; i += 3)
        {
            if (i > 0 && i < *pathSize - 3)
            {
                dtStatus randomPointStatus = Clients[clientId]->GetNavmeshQuery(mapId)->findRandomPointAroundCircle
                (
                    visitedBuffer[i / 3],
                    path + i,
                    maxRandomDistance,
                    &QueryFilter,
                    GetRandomFloat,
                    &randomRef,
                    path + i
                );

                if (dtStatusFailed(randomPointStatus))
                {
                    ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call findRandomPointAroundCircle: " << randomPointStatus << std::endl);
                }
            }

            RDToWowCoords(path + i);
        }

        return true;
    }

    return false;
}

bool AmeisenNavigation::MoveAlongSurface(int clientId, int mapId, const float* startPosition, const float* endPosition, float* positionToGoTo) noexcept
{
    if (!IsValidClient(clientId) || !InitQueryAndLoadMmaps(clientId, mapId)) { return false; }

    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] MoveAlongSurface (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    float rdStart[3];
    dtVcopy(rdStart, startPosition);
    WowToRDCoords(rdStart);

    float rdEnd[3];
    dtVcopy(rdEnd, endPosition);
    WowToRDCoords(rdEnd);

    int visitedCount;
    dtPolyRef visited[16]{};
    dtStatus moveAlongSurfaceStatus = Clients[clientId]->GetNavmeshQuery(mapId)->moveAlongSurface
    (
        GetNearestPoly(clientId, mapId, rdStart, rdStart),
        rdStart,
        rdEnd,
        &QueryFilter,
        positionToGoTo,
        visited,
        &visitedCount,
        sizeof(visited) / sizeof(dtPolyRef)
    );

    if (dtStatusSucceed(moveAlongSurfaceStatus))
    {
        RDToWowCoords(positionToGoTo);
        ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] moveAlongSurface: " << PRINT_VEC3(positionToGoTo) << std::endl);
        return true;
    }

    ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call moveAlongSurface: " << moveAlongSurfaceStatus << std::endl);
    return false;
}

bool AmeisenNavigation::GetRandomPoint(int clientId, int mapId, float* position) noexcept
{
    if (!IsValidClient(clientId) || !InitQueryAndLoadMmaps(clientId, mapId)) { return false; }

    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetRandomPoint (" << mapId << ")" << std::endl);

    dtPolyRef polyRef;
    dtStatus findRandomPointStatus = Clients[clientId]->GetNavmeshQuery(mapId)->findRandomPoint
    (
        &QueryFilter,
        GetRandomFloat,
        &polyRef,
        position
    );

    if (dtStatusSucceed(findRandomPointStatus))
    {
        RDToWowCoords(position);
        ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] findRandomPoint: " << PRINT_VEC3(position) << std::endl);
        return true;
    }

    ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call findRandomPoint: " << findRandomPointStatus << std::endl);
    return false;
}

bool AmeisenNavigation::GetRandomPointAround(int clientId, int mapId, const float* startPosition, float radius, float* position) noexcept
{
    if (!IsValidClient(clientId) || !InitQueryAndLoadMmaps(clientId, mapId)) { return false; }

    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetRandomPointAround (" << mapId << ") startPosition: " << PRINT_VEC3(startPosition) << " radius: " << radius << std::endl);

    float rdStart[3];
    dtVcopy(rdStart, startPosition);
    WowToRDCoords(rdStart);

    dtPolyRef polyRef;
    dtStatus findRandomPointAroundStatus = Clients[clientId]->GetNavmeshQuery(mapId)->findRandomPointAroundCircle
    (
        GetNearestPoly(clientId, mapId, rdStart, rdStart),
        rdStart,
        radius,
        &QueryFilter,
        GetRandomFloat,
        &polyRef,
        position
    );

    if (dtStatusSucceed(findRandomPointAroundStatus))
    {
        RDToWowCoords(position);
        ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] findRandomPointAroundCircle: " << PRINT_VEC3(position) << std::endl);
        return true;
    }

    ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call findRandomPointAroundCircle: " << findRandomPointAroundStatus << std::endl);
    return false;
}

bool AmeisenNavigation::CastMovementRay(int clientId, int mapId, const float* startPosition, const float* endPosition, dtRaycastHit* raycastHit) noexcept
{
    if (!IsValidClient(clientId) || !InitQueryAndLoadMmaps(clientId, mapId)) { return false; }

    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] CastMovementRay (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    float rdStart[3];
    dtVcopy(rdStart, startPosition);
    WowToRDCoords(rdStart);

    float rdEnd[3];
    dtVcopy(rdEnd, endPosition);
    WowToRDCoords(rdEnd);

    dtStatus castMovementRayStatus = Clients[clientId]->GetNavmeshQuery(mapId)->raycast
    (
        GetNearestPoly(clientId, mapId, rdStart, rdStart),
        rdStart,
        rdEnd,
        &QueryFilter,
        0,
        raycastHit
    );

    if (dtStatusSucceed(castMovementRayStatus))
    {
        return raycastHit->t == FLT_MAX;
    }

    ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call raycast: " << castMovementRayStatus << std::endl);
    return false;
}

void AmeisenNavigation::SmoothPathChaikinCurve(const float* input, int inputSize, float* output, int* outputSize) const noexcept
{
    InsertVector3(output, *outputSize, input, 0);

    // buffers to scale and add vectors
    float s0[3];
    float s1[3];

    float result[3];

    // make sure we dont go over the buffer bounds
    // why 9: we add 6 floats in the loop and 3 for the end position
    const int maxIndex = MaxPathNodes - 9;

    for (int i = 0; i < inputSize - 3; i += 3)
    {
        if (*outputSize > maxIndex) { break; }

        ScaleAndAddVector3(input + i, 0.75f, input + i + 3, 0.25f, s0, s1, result);
        InsertVector3(output, *outputSize, result, 0);

        ScaleAndAddVector3(input + i, 0.25f, input + i + 3, 0.75f, s0, s1, result);
        InsertVector3(output, *outputSize, result, 0);
    }

    InsertVector3(output, *outputSize, input, inputSize - 3);
}

void AmeisenNavigation::SmoothPathCatmullRom(const float* input, int inputSize, float* output, int* outputSize, int points, float alpha) const noexcept
{
    InsertVector3(output, *outputSize, input, 0);

    // buffers to scale and add vectors
    float s0[3];
    float s1[3];

    float A1[3];
    float A2[3];
    float A3[3];

    float B1[3];
    float B2[3];

    float C[3];

    // make sure we dont go over the buffer bounds
    // why 3: we add 3 floats in the loop
    const int maxIndex = MaxPathNodes - 3;

    for (int i = 3; i < inputSize - 6; i += 3)
    {
        const float* p0 = input + i - 3;
        const float* p1 = input + i;
        const float* p2 = input + i + 3;
        const float* p3 = input + i + 6;

        const float t0 = 0.0f;

        const float t1 = std::powf(std::powf(p1[0] - p0[0], 2.0f) + std::powf(p1[1] - p0[1], 2.0f) + std::powf(p1[2] - p0[2], 2.0f), alpha * 0.5f) + t0;
        const float t2 = std::powf(std::powf(p2[0] - p1[0], 2.0f) + std::powf(p2[1] - p1[1], 2.0f) + std::powf(p2[2] - p1[2], 2.0f), alpha * 0.5f) + t1;
        const float t3 = std::powf(std::powf(p3[0] - p2[0], 2.0f) + std::powf(p3[1] - p2[1], 2.0f) + std::powf(p3[2] - p2[2], 2.0f), alpha * 0.5f) + t2;

        for (float t = t1; t < t2; t += ((t2 - t1) / static_cast<float>(points)))
        {
            ScaleAndAddVector3(p0, (t1 - t) / (t1 - t0), p1, (t - t0) / (t1 - t0), s0, s1, A1);
            ScaleAndAddVector3(p1, (t2 - t) / (t2 - t1), p2, (t - t1) / (t2 - t1), s0, s1, A2);
            ScaleAndAddVector3(p2, (t3 - t) / (t3 - t2), p3, (t - t2) / (t3 - t2), s0, s1, A3);

            ScaleAndAddVector3(A1, (t2 - t) / (t2 - t0), A2, (t - t0) / (t2 - t0), s0, s1, B1);
            ScaleAndAddVector3(A2, (t3 - t) / (t3 - t1), A3, (t - t1) / (t3 - t1), s0, s1, B2);

            ScaleAndAddVector3(B1, (t2 - t) / (t2 - t1), B2, (t - t1) / (t2 - t1), s0, s1, C);

            if (!std::isnan(C[0]) && !std::isnan(C[1]) && !std::isnan(C[2]))
            {
                if (*outputSize > maxIndex) { return; }

                InsertVector3(output, *outputSize, C, 0);
            }
        }
    }
}

bool AmeisenNavigation::LoadMmaps(int mapId) noexcept
{
    const std::lock_guard<std::mutex> lock(NavMeshMap[mapId].first);

    if (IsMmapLoaded(mapId)) { return true; }

    // build the *.mmap filename (example: 001.mmap or 587.mmap)
    std::stringstream mmapFilename;
    mmapFilename << std::setw(3) << std::setfill('0') << mapId << ".mmap";

    std::filesystem::path mmapFile(MmapFolder);
    mmapFile.append(mmapFilename.str());

    if (!std::filesystem::exists(mmapFile))
    {
        ANAV_ERROR_MSG(std::cout << ">> MMAP file not found for map '" << mapId << "': " << mmapFile << std::endl);
        return false;
    }

    std::ifstream mmapStream;
    mmapStream.open(mmapFile, std::ifstream::binary);

    dtNavMeshParams params{};
    mmapStream.read(reinterpret_cast<char*>(&params), sizeof(dtNavMeshParams));
    mmapStream.close();

    NavMeshMap[mapId].second = dtAllocNavMesh();

    if (!NavMeshMap[mapId].second)
    {
        ANAV_ERROR_MSG(std::cout << ">> Failed to allocate NavMesh for map '" << mapId << "'" << std::endl);
        return false;
    }

    dtStatus initStatus = NavMeshMap[mapId].second->init(&params);

    if (dtStatusFailed(initStatus))
    {
        dtFreeNavMesh(NavMeshMap[mapId].second);
        ANAV_ERROR_MSG(std::cout << ">> Failed to init NavMesh for map '" << mapId << "': " << initStatus << std::endl);
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

            ANAV_DEBUG_ONLY(std::cout << ">> Reading dtTile for map '" << mapId << "': " << mmapTileFile << std::endl);

            std::ifstream mmapTileStream;
            mmapTileStream.open(mmapTileFile, std::ifstream::binary);

            MmapTileHeader mmapTileHeader{};
            mmapTileStream.read(reinterpret_cast<char*>(&mmapTileHeader), sizeof(MmapTileHeader));

            if (mmapTileHeader.mmapMagic != MMAP_MAGIC)
            {
                ANAV_ERROR_MSG(std::cout << ">> Wrong MMAP header for map '" << mapId << "': " << mmapTileFile << std::endl);
                return false;
            }

            if (mmapTileHeader.mmapVersion < MMAP_VERSION)
            {
                ANAV_ERROR_MSG(std::cout << ">> Wrong MMAP version for map '" << mapId << "': " << mmapTileHeader.mmapVersion << " < " << MMAP_VERSION << std::endl);
            }

            void* mmapTileData = dtAlloc(mmapTileHeader.size, DT_ALLOC_PERM);
            mmapTileStream.read(static_cast<char*>(mmapTileData), mmapTileHeader.size);
            mmapTileStream.close();

            dtTileRef tileRef;
            dtStatus addTileStatus = NavMeshMap[mapId].second->addTile(static_cast<unsigned char*>(mmapTileData), mmapTileHeader.size, DT_TILE_FREE_DATA, 0, &tileRef);

            if (dtStatusFailed(addTileStatus))
            {
                dtFree(mmapTileData);
                dtFreeNavMesh(NavMeshMap[mapId].second);
                ANAV_DEBUG_ONLY(std::cout << ">> Adding Tile to NavMesh for map '" << mapId << "' failed: " << addTileStatus << std::endl);
                return false;
            }
        }
    }

    return true;
}

bool AmeisenNavigation::InitQueryAndLoadMmaps(int clientId, int mapId) noexcept
{
    // we already have a query
    if (Clients[clientId]->GetNavmeshQuery(mapId)) { return true; }

    // we need to allocate a new query, but first check wheter we need to load a map or not
    if (!LoadMmaps(mapId))
    {
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed load MMAPs for map '" << mapId << "'" << std::endl);
        return false;
    }

    // allocate and init the new query
    Clients[clientId]->SetNavmeshQuery(mapId, dtAllocNavMeshQuery());

    if (!Clients[clientId]->GetNavmeshQuery(mapId))
    {
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed allocate NavMeshQuery for map '" << mapId << "'" << std::endl);
        return false;
    }

    dtStatus initQueryStatus = Clients[clientId]->GetNavmeshQuery(mapId)->init(NavMeshMap[mapId].second, MaxSearchNodes);

    if (dtStatusFailed(initQueryStatus))
    {
        dtFreeNavMeshQuery(Clients[clientId]->GetNavmeshQuery(mapId));
        Clients[clientId]->SetNavmeshQuery(mapId, nullptr);
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to init NavMeshQuery for map '" << mapId << "': " << initQueryStatus << std::endl);
        return false;
    }

    return true;
}

bool AmeisenNavigation::CalculateNormalPath(int clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize, dtPolyRef* visited) noexcept
{
    float rdStart[3];
    dtVcopy(rdStart, startPosition);
    WowToRDCoords(rdStart);

    float rdEnd[3];
    dtVcopy(rdEnd, endPosition);
    WowToRDCoords(rdEnd);

    dtStatus polyPathStatus = Clients[clientId]->GetNavmeshQuery(mapId)->findPath
    (
        GetNearestPoly(clientId, mapId, rdStart, rdStart),
        GetNearestPoly(clientId, mapId, rdEnd, rdEnd),
        rdStart,
        rdEnd,
        &QueryFilter,
        Clients[clientId]->GetPolyPathBuffer(),
        pathSize,
        MaxPathNodes
    );

    if (dtStatusSucceed(polyPathStatus))
    {
        ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] findPath: " << *pathSize << "/" << MaxPathNodes << std::endl);

        dtStatus straightPathStatus = Clients[clientId]->GetNavmeshQuery(mapId)->findStraightPath
        (
            rdStart,
            rdEnd,
            Clients[clientId]->GetPolyPathBuffer(),
            *pathSize,
            path,
            nullptr,
            visited,
            pathSize,
            MaxPathNodes * 3
        );

        if (dtStatusSucceed(straightPathStatus))
        {
            ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] findStraightPath: " << (*pathSize) << "/" << MaxPathNodes << std::endl);
            (*pathSize) *= 3;
            return true;
        }
        else
        {
            ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call findStraightPath: " << straightPathStatus << std::endl);
        }
    }
    else
    {
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call findPath: " << polyPathStatus << std::endl);
    }

    return false;
}
