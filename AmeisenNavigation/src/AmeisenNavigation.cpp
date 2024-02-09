#include "AmeisenNavigation.hpp"

void AmeisenNavigation::NewClient(size_t clientId, MmapFormat format) noexcept
{
    if (IsValidClient(clientId)) { return; }

    ANAV_DEBUG_ONLY(std::cout << ">> New Client: " << clientId << std::endl);
    Clients[clientId] = new AmeisenNavClient(clientId, ClientState::NORMAL, FilterProvider, MaxPolyPath);
}

void AmeisenNavigation::FreeClient(size_t clientId) noexcept
{
    if (!IsValidClient(clientId)) { return; }

    delete Clients[clientId];
    Clients[clientId] = nullptr;
    ANAV_DEBUG_ONLY(std::cout << ">> Freed Client: " << clientId << std::endl);
}

AmeisenNavClient* AmeisenNavigation::GetClient(size_t clientId) noexcept
{
    return IsValidClient(clientId) ? Clients[clientId]: nullptr;
}

bool AmeisenNavigation::GetPath(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition, Path& path) noexcept
{
    AmeisenNavClient* client;
    dtNavMeshQuery* query;

    if (!TryGetClientAndQuery(clientId, mapId, client, query)) { return false; }
    ANAV_DEBUG_ONLY(std::cout << ">> [" << clientId << "] GetPath (" << mapId << ") " << PRINT_VEC3(startPosition) << " -> " << PRINT_VEC3(endPosition) << std::endl);

    if (CalculateNormalPath(query, client->QueryFilter(), client->GetPolyPathBuffer(), client->GetPolyPathBufferSize(), startPosition, endPosition, path))
    {
        path.ToWowCoords();
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
                dtStatus randomPointStatus = query->findRandomPointAroundCircle
                (
                    polyPathBuffer[i],
                    path[i],
                    maxRandomDistance,
                    client->QueryFilter(),
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

    if (PolyPosition start; dtStatusSucceed(GetNearestPoly(query, client->QueryFilter(), startPosition, start)))
    {
        Vector3 rdEnd;
        endPosition.CopyToRDCoords(rdEnd);

        int visitedCount = 0;
        dtPolyRef visited[8];
        dtStatus moveAlongSurfaceStatus = query->moveAlongSurface
        (
            start.poly,
            start.pos,
            rdEnd,
            client->QueryFilter(),
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
    dtStatus findRandomPointStatus = query->findRandomPoint
    (
        client->QueryFilter(),
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

    if (PolyPosition start; dtStatusSucceed(GetNearestPoly(query, client->QueryFilter(), startPosition, start)))
    {
        dtPolyRef polyRef;
        dtStatus findRandomPointAroundStatus = query->findRandomPointAroundCircle
        (
            start.poly,
            start.pos,
            radius,
            client->QueryFilter(),
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

    if (PolyPosition start; dtStatusSucceed(GetNearestPoly(query, client->QueryFilter(), startPosition, start)))
    {
        Vector3 rdEnd;
        endPosition.CopyToRDCoords(rdEnd);

        dtStatus castMovementRayStatus = query->raycast
        (
            start.poly,
            start.pos,
            rdEnd,
            client->QueryFilter(),
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

    for (int i = 0; i < input.pointCount; ++i)
    {
        if (PolyPosition start; dtStatusSucceed(GetNearestPoly(query, client->QueryFilter(), input.points[i], start)))
        {
            Vector3 closest;
            bool posOverPoly = false;

            if (dtStatusSucceed(query->closestPointOnPoly(start.poly, start.pos, closest, &posOverPoly)))
            {
                closest.ToWowCoords();
                output.Append(&closest);
                if (output.IsFull()) break;
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

    Vector3 lastPos = input.points[0];
    output.TryAppend(&lastPos);
    const float maxDistance = 25.0f;

    for (int i = 1; i < input.pointCount; ++i)
    {
        if (PolyPosition start; dtStatusSucceed(GetNearestPoly(query, client->QueryFilter(), lastPos, start)))
        {
            Vector3 rdEnd;
            input.points[i].CopyToRDCoords(rdEnd);

            float distance = dtVdist(start.pos, rdEnd);

            if (distance > maxDistance)
            {
                Vector3 velocity;
                dtVsub(velocity, rdEnd, start.pos);
                dtVnormalize(velocity);
                dtVscale(velocity, velocity, distance / std::ceilf(distance / maxDistance));
                dtVadd(rdEnd, start.pos, velocity);
                i--;
            }

            int visitedCount = 0;
            dtPolyRef visited[8];
            Vector3 positionToGoTo;

            dtStatus moveAlongSurfaceStatus = query->moveAlongSurface
            (
                start.poly,
                start.pos,
                rdEnd,
                client->QueryFilter(),
                positionToGoTo,
                visited,
                &visitedCount,
                sizeof(visited) / sizeof(dtPolyRef)
            );

            if (dtStatusSucceed(moveAlongSurfaceStatus))
            {
                positionToGoTo.ToWowCoords();
                output.Append(&positionToGoTo);
                lastPos = positionToGoTo;

                if (output.IsFull()) break;
            }
        }
    }

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

bool AmeisenNavigation::TryGetClientAndQuery(size_t clientId, int mapId, AmeisenNavClient*& client, dtNavMeshQuery*& query) noexcept
{
    if (!IsValidClient(clientId)) { return false; }

    client = Clients.at(clientId);

    // we already have a query
    if (query = client->GetNavmeshQuery(mapId)) { return true; }

    const auto navMesh = NavSource->Get(mapId);

    // we need to allocate a new query, but first check wheter we need to load a map or not
    if (!navMesh)
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
    dtQueryFilter* filter,
    dtPolyRef* polyPathBuffer,
    int maxPolyPathCount,
    const Vector3& startPosition,
    const Vector3& endPosition,
    Path& path,
    dtPolyRef* visited
) noexcept
{
    if (PolyPosition start; dtStatusSucceed(GetNearestPoly(query, filter, startPosition, start)))
    {
        if (PolyPosition end; dtStatusSucceed(GetNearestPoly(query, filter, endPosition, end)))
        {
            int polyPathCount = 0;
            dtStatus polyPathStatus = query->findPath
            (
                start.poly,
                end.poly,
                start.pos,
                end.pos,
                filter,
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
