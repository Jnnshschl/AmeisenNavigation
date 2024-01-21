#include "AmeisenNavigation.hpp"

void AmeisenNavigation::NewClient(size_t clientId, MmapFormat version) noexcept
{
    if (IsValidClient(clientId)) { return; }

    ANAV_DEBUG_ONLY(std::cout << ">> New Client: " << clientId << std::endl);
    Clients[clientId] = new AmeisenNavClient(clientId, version, MaxPathNodes);
}

void AmeisenNavigation::FreeClient(size_t clientId) noexcept
{
    if (!IsValidClient(clientId)) { return; }

    delete Clients[clientId];
    Clients[clientId] = nullptr;
    ANAV_DEBUG_ONLY(std::cout << ">> Freed Client: " << clientId << std::endl);
}

bool AmeisenNavigation::GetPath(size_t clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize) noexcept
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

bool AmeisenNavigation::GetRandomPath(size_t clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize, float maxRandomDistance) noexcept
{
    if (!IsValidClient(clientId) || !InitQueryAndLoadMmaps(clientId, mapId)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetRandomPath (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    const auto client = Clients.at(clientId);
    auto visitedBuffer = client->GetMiscPathBuffer();

    if (CalculateNormalPath(clientId, mapId, startPosition, endPosition, path, pathSize, visitedBuffer))
    {
        for (int i = 0; i < *pathSize; i += 3)
        {
            if (i > 0 && i < *pathSize - 3)
            {
                dtPolyRef randomRef;
                dtStatus randomPointStatus = client->GetNavmeshQuery(mapId)->findRandomPointAroundCircle
                (
                    visitedBuffer[i / 3],
                    path + i,
                    maxRandomDistance,
                    &client->QueryFilter(),
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

bool AmeisenNavigation::MoveAlongSurface(size_t clientId, int mapId, const float* startPosition, const float* endPosition, float* positionToGoTo) noexcept
{
    if (!IsValidClient(clientId) || !InitQueryAndLoadMmaps(clientId, mapId)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] MoveAlongSurface (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    float rdStart[3];
    CopyWowToRDCoords(startPosition, rdStart);

    const auto client = Clients.at(clientId);

    if (dtPolyRef polyStart; dtStatusSucceed(GetNearestPoly(client, mapId, rdStart, rdStart, &polyStart)))
    {
        float rdEnd[3];
        CopyWowToRDCoords(endPosition, rdEnd);

        int visitedCount;
        dtPolyRef visited[16]{ 0 };
        dtStatus moveAlongSurfaceStatus = client->GetNavmeshQuery(mapId)->moveAlongSurface
        (
            polyStart,
            rdStart,
            rdEnd,
            &client->QueryFilter(),
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
        else
        {
            ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call moveAlongSurface: " << moveAlongSurfaceStatus << std::endl);
        }
    }

    return false;
}

bool AmeisenNavigation::GetRandomPoint(size_t clientId, int mapId, float* position) noexcept
{
    if (!IsValidClient(clientId) || !InitQueryAndLoadMmaps(clientId, mapId)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetRandomPoint (" << mapId << ")" << std::endl);

    const auto client = Clients.at(clientId);

    dtPolyRef polyRef;
    dtStatus findRandomPointStatus = client->GetNavmeshQuery(mapId)->findRandomPoint
    (
        &client->QueryFilter(),
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

bool AmeisenNavigation::GetRandomPointAround(size_t clientId, int mapId, const float* startPosition, float radius, float* position) noexcept
{
    if (!IsValidClient(clientId) || !InitQueryAndLoadMmaps(clientId, mapId)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetRandomPointAround (" << mapId << ") startPosition: " << PRINT_VEC3(startPosition) << " radius: " << radius << std::endl);

    float rdStart[3];
    CopyWowToRDCoords(startPosition, rdStart);

    const auto client = Clients.at(clientId);

    if (dtPolyRef polyStart; dtStatusSucceed(GetNearestPoly(client, mapId, rdStart, rdStart, &polyStart)))
    {
        dtPolyRef polyRef;
        dtStatus findRandomPointAroundStatus = client->GetNavmeshQuery(mapId)->findRandomPointAroundCircle
        (
            polyStart,
            rdStart,
            radius,
            &client->QueryFilter(),
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
        else
        {
            ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call findRandomPointAroundCircle: " << findRandomPointAroundStatus << std::endl);
        }
    }

    return false;
}

bool AmeisenNavigation::CastMovementRay(size_t clientId, int mapId, const float* startPosition, const float* endPosition, dtRaycastHit* raycastHit) noexcept
{
    if (!IsValidClient(clientId) || !InitQueryAndLoadMmaps(clientId, mapId)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] CastMovementRay (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    float rdStart[3];
    CopyWowToRDCoords(startPosition, rdStart);

    const auto client = Clients.at(clientId);

    if (dtPolyRef polyStart; dtStatusSucceed(GetNearestPoly(client, mapId, rdStart, rdStart, &polyStart)))
    {
        float rdEnd[3];
        CopyWowToRDCoords(endPosition, rdEnd);

        dtStatus castMovementRayStatus = client->GetNavmeshQuery(mapId)->raycast
        (
            polyStart,
            rdStart,
            rdEnd,
            &client->QueryFilter(),
            0,
            raycastHit
        );

        if (dtStatusSucceed(castMovementRayStatus))
        {
            return raycastHit->t == FLT_MAX;
        }
        else
        {
            ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call raycast: " << castMovementRayStatus << std::endl);
        }
    }

    return false;
}

bool AmeisenNavigation::GetPolyExplorationPath(size_t clientId, int mapId, const float* polyPoints, int inputSize, float* output, int* outputSize, float* tmpBuffer, const float* startPosition, float viewDistance) noexcept
{
    if (!IsValidClient(clientId) || !InitQueryAndLoadMmaps(clientId, mapId)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetExplorePolyPath (" << mapId << ") " << PRINT_VEC3(startPosition) << ": " << inputSize << " points" << std::endl);

    float rdStart[3];
    CopyWowToRDCoords(startPosition, rdStart);

    const auto client = Clients.at(clientId);

    PolygonMath::BridsonsPoissonDiskSampling(polyPoints, inputSize, output, outputSize, tmpBuffer, MaxPathNodes * 3, viewDistance);

    ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call ...: " << "" << std::endl);
    return false;
}

void AmeisenNavigation::SmoothPathChaikinCurve(const float* input, int inputSize, float* output, int* outputSize) const noexcept
{
    ChaikinCurve::SmoothPath(input, inputSize, output, outputSize, MaxPathNodes);
}

void AmeisenNavigation::SmoothPathCatmullRom(const float* input, int inputSize, float* output, int* outputSize, int points, float alpha) const noexcept
{
    const int maxIndex = MaxPathNodes - 3;

    // Ensure that there are enough input points
    if (inputSize < 9 || *outputSize > maxIndex)
        return;

    // Insert the first point
    InsertVector3(output, *outputSize, input, 0);

    float c[3]{};

    // Reuse tDelta calculation
    const float tDelta = 1.0f / points;

    for (int i = 3; i < inputSize - 6; i += 3)
    {
        const auto p0 = input + i - 3;
        const auto p1 = p0 + 3;
        const auto p2 = p1 + 3;
        const auto p3 = p2 + 3;

        for (int j = 0; j < points; ++j)
        {
            const float t = j * tDelta;

            for (int d = 0; d < 3; ++d)
            {
                c[d] = CatmullRomInterpolate(p0[d], p1[d], p2[d], p3[d], t, alpha);
            }

            InsertVector3(output, *outputSize, c, 0);

            if (*outputSize > MaxPathNodes)
            {
                break;
            }
        }
    }
}

void AmeisenNavigation::SmoothPathBezier(const float* input, int inputSize, float* output, int* outputSize, int points) const noexcept
{
    BezierCurve::SmoothPath(input, inputSize, output, outputSize, MaxPathNodes, points);
}

bool AmeisenNavigation::LoadMmaps(MmapFormat& mmapFormat, int mapId) noexcept
{
    if (mmapFormat == MmapFormat::UNKNOWN)
    {
        mmapFormat = TryDetectMmapFormat();

        if (mmapFormat == MmapFormat::UNKNOWN)
        {
            return false;
        }
    }

    const std::lock_guard<std::mutex> lock(NavMeshMap[mmapFormat][mapId].first);

    if (IsMmapLoaded(mmapFormat, mapId)) { return true; }

    const auto& filenameFormat = MmapFormatPatterns.at(mmapFormat);

    std::filesystem::path mmapFile(MmapFolder);
    std::string filename = std::vformat(filenameFormat.first, std::make_format_args(mapId));
    mmapFile.append(filename);

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

    NavMeshMap[mmapFormat][mapId].second = dtAllocNavMesh();

    if (!NavMeshMap[mmapFormat][mapId].second)
    {
        ANAV_ERROR_MSG(std::cout << ">> Failed to allocate NavMesh for map '" << mapId << "'" << std::endl);
        return false;
    }

    dtStatus initStatus = NavMeshMap[mmapFormat][mapId].second->init(&params);

    if (dtStatusFailed(initStatus))
    {
        dtFreeNavMesh(NavMeshMap[mmapFormat][mapId].second);
        ANAV_ERROR_MSG(std::cout << ">> Failed to init NavMesh for map '" << mapId << "': " << initStatus << std::endl);
        return false;
    }

#ifndef _DEBUG
#pragma omp parallel for schedule(dynamic)
#endif
    for (int i = 1; i <= 64 * 64; ++i)
    {
        const auto x = i / 64;
        const auto y = i % 64;

        std::filesystem::path mmapTileFile(MmapFolder);
        mmapTileFile.append(std::vformat(filenameFormat.second, std::make_format_args(mapId, x, y)));

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
            continue;
        }

        if (mmapTileHeader.mmapVersion < MMAP_VERSION)
        {
            ANAV_ERROR_MSG(std::cout << ">> Wrong MMAP version for map '" << mapId << "': " << mmapTileHeader.mmapVersion << " < " << MMAP_VERSION << std::endl);
        }

        void* mmapTileData = malloc(mmapTileHeader.size);
        mmapTileStream.read(static_cast<char*>(mmapTileData), mmapTileHeader.size);
        mmapTileStream.close();

        dtStatus addTileStatus = NavMeshMap[mmapFormat][mapId].second->addTile
        (
            static_cast<unsigned char*>(mmapTileData),
            mmapTileHeader.size,
            DT_TILE_FREE_DATA,
            0,
            nullptr
        );

        if (dtStatusFailed(addTileStatus))
        {
            free(mmapTileData);
            ANAV_ERROR_MSG(std::cout << ">> Adding Tile to NavMesh for map '" << mapId << "' failed: " << addTileStatus << std::endl);
        }
    }

    return true;
}

bool AmeisenNavigation::InitQueryAndLoadMmaps(size_t clientId, int mapId) noexcept
{
    const auto client = Clients.at(clientId);
    auto& format = client->GetMmapFormat();

    // we already have a query
    if (client->GetNavmeshQuery(mapId)) { return true; }

    // we need to allocate a new query, but first check wheter we need to load a map or not
    if (!LoadMmaps(format, mapId))
    {
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed load MMAPs for map '" << mapId << "'" << std::endl);
        return false;
    }

    // allocate and init the new query
    auto navmeshQuery = dtAllocNavMeshQuery();

    if (!navmeshQuery)
    {
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed allocate NavMeshQuery for map '" << mapId << "'" << std::endl);
        return false;
    }

    const auto& navMesh = NavMeshMap[format][mapId].second;

    if (!navMesh)
    {
        dtFreeNavMeshQuery(navmeshQuery);
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to init NavMeshQuery for map '" << mapId << "': navMesh is null" << std::endl);
        return false;
    }

    dtStatus initQueryStatus = navmeshQuery->init(navMesh, MaxSearchNodes);

    if (dtStatusFailed(initQueryStatus))
    {
        dtFreeNavMeshQuery(navmeshQuery);
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to init NavMeshQuery for map '" << mapId << "': " << initQueryStatus << std::endl);
        return false;
    }

    client->SetNavmeshQuery(mapId, navmeshQuery);
    return true;
}

bool AmeisenNavigation::CalculateNormalPath(size_t clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize, dtPolyRef* visited) noexcept
{
    float rdStart[3];
    CopyWowToRDCoords(startPosition, rdStart);

    const auto client = Clients.at(clientId);

    if (dtPolyRef polyStart; dtStatusSucceed(GetNearestPoly(client, mapId, rdStart, rdStart, &polyStart)))
    {
        float rdEnd[3];
        CopyWowToRDCoords(endPosition, rdEnd);

        if (dtPolyRef polyEnd; dtStatusSucceed(GetNearestPoly(client, mapId, rdEnd, rdEnd, &polyEnd)))
        {
            dtStatus polyPathStatus = client->GetNavmeshQuery(mapId)->findPath
            (
                polyStart,
                polyEnd,
                rdStart,
                rdEnd,
                &client->QueryFilter(),
                client->GetPolyPathBuffer(),
                pathSize,
                MaxPathNodes
            );

            if (dtStatusSucceed(polyPathStatus))
            {
                ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] findPath: " << *pathSize << "/" << MaxPathNodes << std::endl);

                dtStatus straightPathStatus = client->GetNavmeshQuery(mapId)->findStraightPath
                (
                    rdStart,
                    rdEnd,
                    client->GetPolyPathBuffer(),
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
        }
    }

    return false;
}
