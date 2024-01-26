#include "AmeisenNavigation.hpp"

void AmeisenNavigation::NewClient(size_t clientId, MmapFormat version) noexcept
{
    if (IsValidClient(clientId)) { return; }

    ANAV_DEBUG_ONLY(std::cout << ">> New Client: " << clientId << std::endl);
    Clients[clientId] = new AmeisenNavClient(clientId, version, MaxPolyPath);
}

void AmeisenNavigation::FreeClient(size_t clientId) noexcept
{
    if (!IsValidClient(clientId)) { return; }

    delete Clients[clientId];
    Clients[clientId] = nullptr;
    ANAV_DEBUG_ONLY(std::cout << ">> Freed Client: " << clientId << std::endl);
}

bool AmeisenNavigation::GetPath(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition, Path& path) noexcept
{
    AmeisenNavClient* client;
    dtNavMeshQuery* query;

    if (!TryGetClientAndQuery(clientId, mapId, client, query)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetPath (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    if (CalculateNormalPath(query, client->QueryFilter(), client->GetPolyPathBuffer(), client->GetPolyPathBufferSize(), startPosition, endPosition, path))
    {
        for (auto& point : path)
        {
            point.ToWowCoords();
        }

        return true;
    }

    return false;
}

bool AmeisenNavigation::GetRandomPath(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition, Path& path, float maxRandomDistance) noexcept
{
    AmeisenNavClient* client;
    dtNavMeshQuery* query;

    if (!TryGetClientAndQuery(clientId, mapId, client, query)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetRandomPath (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    auto polyPathBuffer = client->GetPolyPathBuffer();

    if (CalculateNormalPath(query, client->QueryFilter(), polyPathBuffer, client->GetPolyPathBufferSize(), startPosition, endPosition, path, polyPathBuffer))
    {
        for (int i = 0; i < path.pointCount; ++i)
        {
            if (i > 0 && i < path.pointCount - 1)
            {
                dtPolyRef randomRef;
                dtStatus randomPointStatus = client->GetNavmeshQuery(mapId)->findRandomPointAroundCircle
                (
                    polyPathBuffer[i],
                    path[i],
                    maxRandomDistance,
                    &client->QueryFilter(),
                    GetRandomFloat,
                    &randomRef,
                    path[i]
                );

                if (dtStatusFailed(randomPointStatus))
                {
                    ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call findRandomPointAroundCircle: " << randomPointStatus << std::endl);
                }
            }

            path[i].ToWowCoords();
        }

        return true;
    }

    return false;
}

bool AmeisenNavigation::MoveAlongSurface(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition, Vector3& positionToGoTo) noexcept
{
    AmeisenNavClient* client;
    dtNavMeshQuery* query;

    if (!TryGetClientAndQuery(clientId, mapId, client, query)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] MoveAlongSurface (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    if (PolyPosition start; dtStatusSucceed(GetNearestPoly(query, &client->QueryFilter(), startPosition, start)))
    {
        Vector3 rdEnd;
        endPosition.CopyToRDCoords(rdEnd);

        int visitedCount = 0;
        dtPolyRef visited[16]{};
        dtStatus moveAlongSurfaceStatus = query->moveAlongSurface
        (
            start.poly,
            start.pos,
            rdEnd,
            &client->QueryFilter(),
            positionToGoTo,
            visited,
            &visitedCount,
            sizeof(visited) / sizeof(dtPolyRef)
        );

        if (dtStatusSucceed(moveAlongSurfaceStatus))
        {
            positionToGoTo.ToWowCoords();
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

bool AmeisenNavigation::GetRandomPoint(size_t clientId, int mapId, Vector3& position) noexcept
{
    AmeisenNavClient* client;
    dtNavMeshQuery* query;

    if (!TryGetClientAndQuery(clientId, mapId, client, query)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetRandomPoint (" << mapId << ")" << std::endl);

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
        position.ToWowCoords();
        ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] findRandomPoint: " << PRINT_VEC3(position) << std::endl);
        return true;
    }

    ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call findRandomPoint: " << findRandomPointStatus << std::endl);
    return false;
}

bool AmeisenNavigation::GetRandomPointAround(size_t clientId, int mapId, const Vector3& startPosition, float radius, Vector3& position) noexcept
{
    AmeisenNavClient* client;
    dtNavMeshQuery* query;

    if (!TryGetClientAndQuery(clientId, mapId, client, query)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetRandomPointAround (" << mapId << ") startPosition: " << PRINT_VEC3(startPosition) << " radius: " << radius << std::endl);

    if (PolyPosition start; dtStatusSucceed(GetNearestPoly(query, &client->QueryFilter(), startPosition, start)))
    {
        dtPolyRef polyRef;
        dtStatus findRandomPointAroundStatus = client->GetNavmeshQuery(mapId)->findRandomPointAroundCircle
        (
            start.poly,
            start.pos,
            radius,
            &client->QueryFilter(),
            GetRandomFloat,
            &polyRef,
            position
        );

        if (dtStatusSucceed(findRandomPointAroundStatus))
        {
            position.ToWowCoords();
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

bool AmeisenNavigation::CastMovementRay(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition, dtRaycastHit* raycastHit) noexcept
{
    AmeisenNavClient* client;
    dtNavMeshQuery* query;

    if (!TryGetClientAndQuery(clientId, mapId, client, query)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] CastMovementRay (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    if (PolyPosition start; dtStatusSucceed(GetNearestPoly(query, &client->QueryFilter(), startPosition, start)))
    {
        Vector3 rdEnd;
        endPosition.CopyToRDCoords(rdEnd);

        dtStatus castMovementRayStatus = query->raycast
        (
            start.poly,
            start.pos,
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

bool AmeisenNavigation::GetPolyExplorationPath
(
    size_t clientId,
    int mapId,
    const Vector3* polyPoints,
    int inputSize,
    Path& path,
    Path& tmpPath,
    const Vector3& startPosition,
    float viewDistance
) noexcept
{
    AmeisenNavClient* client;
    dtNavMeshQuery* query;

    if (!TryGetClientAndQuery(clientId, mapId, client, query)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetExplorePolyPath (" << mapId << ") " << PRINT_VEC3(startPosition) << ": " << inputSize << " points" << std::endl);

    Vector3 rdStart;
    startPosition.CopyToRDCoords(rdStart);

    PolygonMath::BridsonsPoissonDiskSampling(polyPoints, inputSize, path.points, &path.pointCount, tmpPath.points, path.GetSpace(), viewDistance);

    ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to call ...: " << "" << std::endl);
    return false;
}

bool AmeisenNavigation::PostProcessClosestPointOnPoly(size_t clientId, int mapId, const Path& input, Path& output) noexcept
{
    AmeisenNavClient* client;
    dtNavMeshQuery* query;

    if (!TryGetClientAndQuery(clientId, mapId, client, query)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] PostProcessClosestPointOnPoly (" << mapId << ") " << std::endl);

    for (const auto& point : input)
    {
        if (PolyPosition start; dtStatusSucceed(GetNearestPoly(query, &client->QueryFilter(), point, start)))
        {
            Vector3 closest;
            bool posOverPoly = false;

            dtStatus closestPointOnPolyStatus = query->closestPointOnPoly(start.poly, start.pos, closest, &posOverPoly);

            if (dtStatusSucceed(closestPointOnPolyStatus))
            {
                closest.ToWowCoords();
                InsertVector3(output.points, output.pointCount, &closest);
            }
        }
    }

    return true;
}

bool AmeisenNavigation::PostProcessMoveAlongSurface(size_t clientId, int mapId, const Path& input, Path& output) noexcept
{
    AmeisenNavClient* client;
    dtNavMeshQuery* query;

    if (!TryGetClientAndQuery(clientId, mapId, client, query)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] PostProcessMoveAlongSurface (" << mapId << ") " << std::endl);

    InsertVector3(output.points, output.pointCount, &input.points[0]);

    Vector3 lastPos = input.points[0];

    for (int i = 1; i < input.pointCount; ++i)
    {
        if (PolyPosition start; dtStatusSucceed(GetNearestPoly(query, &client->QueryFilter(), lastPos, start)))
        {
            Vector3 rdEnd;
            input.points[i].CopyToRDCoords(rdEnd);

            int visitedCount = 0;
            dtPolyRef visited[16]{};
            Vector3 positionToGoTo;

            dtStatus moveAlongSurfaceStatus = query->moveAlongSurface
            (
                start.poly,
                start.pos,
                rdEnd,
                &client->QueryFilter(),
                positionToGoTo,
                visited,
                &visitedCount,
                sizeof(visited) / sizeof(dtPolyRef)
            );

            if (dtStatusSucceed(moveAlongSurfaceStatus))
            {
                positionToGoTo.ToWowCoords();
                InsertVector3(output.points, output.pointCount, &positionToGoTo);
                lastPos = positionToGoTo;
            }
        }
    }

    InsertVector3(output.points, output.pointCount, &input.points[input.pointCount - 1]);
    return true;
}

void AmeisenNavigation::SmoothPathChaikinCurve(const Path& input, Path& output) const noexcept
{
    ChaikinCurve::SmoothPath(input.points, input.pointCount, output.points, &output.pointCount, output.GetSpace());
}

void AmeisenNavigation::SmoothPathCatmullRom(const Path& input, Path& output, int points, float alpha) const noexcept
{
    CatmullRomSpline::SmoothPath(input.points, input.pointCount, output.points, &output.pointCount, output.GetSpace(), points, alpha);
}

void AmeisenNavigation::SmoothPathBezier(const Path& input, Path& output, int points) const noexcept
{
    BezierCurve::SmoothPath(input.points, input.pointCount, output.points, &output.pointCount, output.GetSpace(), points);
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

#pragma omp parallel for schedule(dynamic)
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

bool AmeisenNavigation::TryGetClientAndQuery(size_t clientId, int mapId, AmeisenNavClient*& client, dtNavMeshQuery*& query) noexcept
{
    if (!IsValidClient(clientId)) { return false; }

    client = Clients.at(clientId);
    auto& format = client->GetMmapFormat();

    // we already have a query
    if (query = client->GetNavmeshQuery(mapId)) { return true; }

    // we need to allocate a new query, but first check wheter we need to load a map or not
    if (!LoadMmaps(format, mapId))
    {
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed load MMAPs for map '" << mapId << "'" << std::endl);
        return false;
    }

    // allocate and init the new query
    query = dtAllocNavMeshQuery();

    if (!query)
    {
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed allocate NavMeshQuery for map '" << mapId << "'" << std::endl);
        return false;
    }

    const auto& navMesh = NavMeshMap[format][mapId].second;

    if (!navMesh)
    {
        dtFreeNavMeshQuery(query);
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to init NavMeshQuery for map '" << mapId << "': navMesh is null" << std::endl);
        return false;
    }

    dtStatus initQueryStatus = query->init(navMesh, MaxSearchNodes);

    if (dtStatusFailed(initQueryStatus))
    {
        dtFreeNavMeshQuery(query);
        ANAV_ERROR_MSG(std::cout << ">> [" << clientId << "] Failed to init NavMeshQuery for map '" << mapId << "': " << initQueryStatus << std::endl);
        return false;
    }

    client->SetNavmeshQuery(mapId, query);
    return true;
}

bool AmeisenNavigation::CalculateNormalPath
(
    dtNavMeshQuery* query,
    dtQueryFilter& filter,
    dtPolyRef* polyPathBuffer,
    int maxPolyPathCount,
    const Vector3& startPosition,
    const Vector3& endPosition,
    Path& path,
    dtPolyRef* visited
) noexcept
{
    if (PolyPosition start; dtStatusSucceed(GetNearestPoly(query, &filter, startPosition, start)))
    {
        if (PolyPosition end; dtStatusSucceed(GetNearestPoly(query, &filter, endPosition, end)))
        {
            int polyPathCount = 0;
            dtStatus polyPathStatus = query->findPath
            (
                start.poly,
                end.poly,
                start.pos,
                end.pos,
                &filter,
                polyPathBuffer,
                &polyPathCount,
                maxPolyPathCount
            );

            if (dtStatusSucceed(polyPathStatus) && polyPathCount > 0)
            {
                ANAV_DEBUG_ONLY(std::cout << ">> findPath: " << polyPathCount << "/" << maxPolyPathCount << std::endl);

                dtStatus straightPathStatus = query->findStraightPath
                (
                    start.pos,
                    end.pos,
                    polyPathBuffer,
                    polyPathCount,
                    reinterpret_cast<float*>(path.points),
                    nullptr,
                    visited,
                    &path.pointCount,
                    path.GetSpace()
                );

                if (dtStatusSucceed(straightPathStatus) && path.pointCount > 0)
                {
                    ANAV_DEBUG_ONLY(std::cout << ">> findStraightPath: " << path.pointCount << "/" << path.maxSize << std::endl);
                    return true;
                }
                else
                {
                    ANAV_ERROR_MSG(std::cout << ">> Failed to call findStraightPath: " << straightPathStatus << std::endl);
                }
            }
            else
            {
                ANAV_ERROR_MSG(std::cout << ">> Failed to call findPath: " << polyPathStatus << std::endl);
            }
        }
    }

    return false;
}
