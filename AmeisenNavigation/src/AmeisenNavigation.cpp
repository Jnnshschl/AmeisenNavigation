#include "AmeisenNavigation.hpp"

void AmeisenNavigation::NewClient(int id) noexcept
{
    if (IsValidClient(id)) { return; }

    ANAV_DEBUG_ONLY(std::cout << ">> New Client: " << id << std::endl);
    NavMeshQueryMap[id] = new std::unordered_map<int, dtNavMeshQuery*>();
    PolyPathBuffers[id] = new dtPolyRef[MaxPolyPath];
}

void AmeisenNavigation::FreeClient(int id) noexcept
{
    if (!IsValidClient(id)) { return; }

    for (const auto& kv : *NavMeshQueryMap[id])
    {
        dtFreeNavMeshQuery(kv.second);
    }

    delete NavMeshQueryMap[id];
    NavMeshQueryMap[id] = nullptr;

    delete PolyPathBuffers[id];
    PolyPathBuffers[id] = nullptr;

    ANAV_DEBUG_ONLY(std::cout << ">> Freed Client: " << id << std::endl);
}

bool AmeisenNavigation::GetPath(int clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize) noexcept
{
    if (!InitQuery(clientId, mapId)) { return false; }

    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetPath (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    float rdStart[3];
    dtVcopy(rdStart, startPosition);
    WowToRDCoords(rdStart);

    float rdEnd[3];
    dtVcopy(rdEnd, endPosition);
    WowToRDCoords(rdEnd);

    dtStatus polyPathStatus = (*NavMeshQueryMap[clientId])[mapId]->findPath
    (
        GetNearestPoly(clientId, mapId, rdStart, rdStart),
        GetNearestPoly(clientId, mapId, rdEnd, rdEnd),
        rdStart, 
        rdEnd, 
        &QueryFilter, 
        PolyPathBuffers[clientId], 
        pathSize, 
        MaxPolyPath
    );

    if (dtStatusSucceed(polyPathStatus))
    {
        ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] findPath: " << *pathSize << "/" << MaxPolyPath << std::endl);

        dtStatus straightPathStatus = (*NavMeshQueryMap[clientId])[mapId]->findStraightPath
        (
            rdStart, 
            rdEnd, 
            PolyPathBuffers[clientId], 
            *pathSize, 
            path, 
            nullptr, 
            nullptr, 
            pathSize, 
            MaxPointPath
        );

        if (dtStatusSucceed(straightPathStatus))
        {
            ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] findStraightPath: " << (*pathSize) << "/" << MaxPointPath << std::endl);
            (*pathSize) *= 3;

            for (int i = 0; i < *pathSize; i += 3)
            {
                RDToWowCoords(path + i);
            }

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

bool AmeisenNavigation::MoveAlongSurface(int clientId, int mapId, const float* startPosition, const float* endPosition, float* positionToGoTo) noexcept
{
    if (!InitQuery(clientId, mapId)) { return false; }

    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] MoveAlongSurface (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    float rdStart[3];
    dtVcopy(rdStart, startPosition);
    WowToRDCoords(rdStart);

    float rdEnd[3];
    dtVcopy(rdEnd, endPosition);
    WowToRDCoords(rdEnd);

    int visitedCount;
    dtPolyRef visited[16]{};
    dtStatus moveAlongSurfaceStatus = (*NavMeshQueryMap[clientId])[mapId]->moveAlongSurface
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
    if (!InitQuery(clientId, mapId)) { return false; }

    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetRandomPoint (" << mapId << ")" << std::endl);

    dtPolyRef polyRef;
    dtStatus findRandomPointStatus = (*NavMeshQueryMap[clientId])[mapId]->findRandomPoint
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
    if (!InitQuery(clientId, mapId)) { return false; }

    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetRandomPointAround (" << mapId << ") startPosition: " << PRINT_VEC3(startPosition) << " radius: " << radius << std::endl);

    float rdStart[3];
    dtVcopy(rdStart, startPosition);
    WowToRDCoords(rdStart);

    dtPolyRef polyRef;
    dtStatus findRandomPointAroundStatus = (*NavMeshQueryMap[clientId])[mapId]->findRandomPointAroundCircle
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
    if (!InitQuery(clientId, mapId)) { return false; }

    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] CastMovementRay (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    float rdStart[3];
    dtVcopy(rdStart, startPosition);
    WowToRDCoords(rdStart);

    float rdEnd[3];
    dtVcopy(rdEnd, endPosition);
    WowToRDCoords(rdEnd);

    dtStatus castMovementRayStatus = (*NavMeshQueryMap[clientId])[mapId]->raycast
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

    ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to raycast: " << castMovementRayStatus << std::endl);
    return false;
}

void AmeisenNavigation::SmoothPathChaikinCurve(const float* input, int inputSize, float* output, int* outputSize) noexcept
{
    InsertVector3(output, *outputSize, input, 0);

    // buffers to scale and add vectors
    float s0[3];
    float s1[3];

    float result[3];

    for (int i = 0; i < inputSize - 3; i += 3)
    {
        ScaleAndAddVector3(input + i, 0.75f, input + i + 3, 0.25f, s0, s1, result);
        InsertVector3(output, *outputSize, result, 0);

        ScaleAndAddVector3(input + i, 0.25f, input + i + 3, 0.75f, s0, s1, result);
        InsertVector3(output, *outputSize, result, 0);
    }

    InsertVector3(output, *outputSize, input, inputSize - 3);
}

void AmeisenNavigation::SmoothPathCatmullRom(const float* input, int inputSize, float* output, int* outputSize, int points, float alpha) noexcept
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
                InsertVector3(output, *outputSize, C, 0);
            }
        }
    }
}

bool AmeisenNavigation::LoadMmaps(int mapId) noexcept
{
    const std::lock_guard<std::mutex> lock(MapLoadingLocks[mapId]);

    if (IsMmapLoaded(mapId))
    {
        return true;
    }

    // build the *.mmap filename (example: 001.mmap or 587.mmap)
    std::stringstream mmapFilename;
    mmapFilename << std::setw(3) << std::setfill('0') << mapId << ".mmap";

    std::filesystem::path mmapFile(MmapFolder);
    mmapFile.append(mmapFilename.str());

    if (!std::filesystem::exists(mmapFile))
    {
        ANAV_ERROR_MSG(std::cout << ">> MMAP for map not found: " << mapId << std::endl);
        return false;
    }

    std::ifstream mmapStream;
    mmapStream.open(mmapFile, std::ifstream::binary);

    dtNavMeshParams params{};
    mmapStream.read(reinterpret_cast<char*>(&params), sizeof(dtNavMeshParams));
    mmapStream.close();

    NavMeshMap[mapId] = dtAllocNavMesh();

    if (!NavMeshMap[mapId])
    {
        ANAV_ERROR_MSG(std::cout << ">> Failed to allocate NavMesh: " << mapId << std::endl);
        return false;
    }

    if (dtStatusFailed(NavMeshMap[mapId]->init(&params)))
    {
        dtFreeNavMesh(NavMeshMap[mapId]);
        ANAV_ERROR_MSG(std::cout << ">> Failed to init NavMesh: " << mapId << std::endl);
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

            ANAV_DEBUG_ONLY(std::cout << ">> Reading dtTile " << mmapTileFile << std::endl);

            std::ifstream mmapTileStream;
            mmapTileStream.open(mmapTileFile, std::ifstream::binary);

            MmapTileHeader mmapTileHeader{};
            mmapTileStream.read(reinterpret_cast<char*>(&mmapTileHeader), sizeof(MmapTileHeader));

            if (mmapTileHeader.mmapMagic != MMAP_MAGIC)
            {
                ANAV_ERROR_MSG(std::cout << ">> Wrong MMAP header, file is invalid" << std::endl);
                return false;
            }

            if (mmapTileHeader.mmapVersion < MMAP_VERSION)
            {
                ANAV_ERROR_MSG(std::cout << ">> Wrong MMAP version: " << mmapTileHeader.mmapVersion << " < " << MMAP_VERSION << std::endl);
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

    return true;
}

bool AmeisenNavigation::InitQuery(int clientId, int mapId) noexcept
{
    if (!IsValidClient(clientId)) { return false; }

    if ((*NavMeshQueryMap[clientId]).find(mapId) != (*NavMeshQueryMap[clientId]).end()
        && (*NavMeshQueryMap[clientId])[mapId])
    {
        return true;
    }

    if (!LoadMmaps(mapId))
    {
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed load MMAPs" << std::endl);
        return false;
    }

    (*NavMeshQueryMap[clientId])[mapId] = dtAllocNavMeshQuery();

    if (!(*NavMeshQueryMap[clientId])[mapId])
    {
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed allocate NavMeshQuery" << std::endl);
        return false;
    }

    if (dtStatusFailed((*NavMeshQueryMap[clientId])[mapId]->init(NavMeshMap[mapId], 65535)))
    {
        dtFreeNavMeshQuery((*NavMeshQueryMap[clientId])[mapId]);
        (*NavMeshQueryMap[clientId])[mapId] = nullptr;
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to init NavMeshQuery" << std::endl);
        return false;
    }

    return true;
}
