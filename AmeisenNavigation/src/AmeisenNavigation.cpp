#include "AmeisenNavigation.hpp"

#include <cfloat>
#include <cmath>

#ifdef _WIN32
#include <excpt.h>
#include <windows.h>

/// Isolate findStraightPath in its own function so Windows SEH (__try/__except) can catch access violations
/// caused by corrupt navmesh tile data. SEH and C++ destructors cannot coexist in the same function under /EHsc,
/// this function only has POD locals.
static dtStatus SafeFindStraightPath(const dtNavMeshQuery* query, const float* startPos, const float* endPos,
                                     const dtPolyRef* polyPath, int polyPathCount, float* straightPath,
                                     unsigned char* straightPathFlags, dtPolyRef* straightPathRefs,
                                     int* straightPathCount, int maxStraightPath) noexcept
{
    __try
    {
        return query->findStraightPath(startPos, endPos, polyPath, polyPathCount, straightPath, straightPathFlags,
                                       straightPathRefs, straightPathCount, maxStraightPath);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        return DT_FAILURE;
    }
}
#else
static dtStatus SafeFindStraightPath(const dtNavMeshQuery* query, const float* startPos, const float* endPos,
                                     const dtPolyRef* polyPath, int polyPathCount, float* straightPath,
                                     unsigned char* straightPathFlags, dtPolyRef* straightPathRefs,
                                     int* straightPathCount, int maxStraightPath) noexcept
{
    return query->findStraightPath(startPos, endPos, polyPath, polyPathCount, straightPath, straightPathFlags,
                                   straightPathRefs, straightPathCount, maxStraightPath);
}
#endif

AmeisenNavigation::AmeisenNavigation(const AmeisenNavigationSettings& settings) : Settings(settings)
{
    Settings.maxPolyPath = std::max(Settings.maxPolyPath, 1);
    Settings.maxPointPath = std::max(Settings.maxPointPath, 2);
    Settings.maxSearchNodes = std::clamp(Settings.maxSearchNodes, 1, 65535);

    if (Settings.useAnp)
    {
        NavSource = std::make_unique<AnpNavSource>(Settings.meshFolder);
        FilterProvider = std::make_unique<AnpQueryFilterProvider>(1.6f, 4.0f, 0.75f, Settings.factionDangerCost);
    }
    else
    {
        auto mmapSource =
            std::make_unique<MmapNavSource>(Settings.meshFolder, Settings.mmapFormat, Settings.customMmapPatterns);
        Settings.mmapFormat = mmapSource->GetFormat();
        FilterProvider = std::make_unique<MmapQueryFilterProvider>(Settings.mmapFormat);
        NavSource = std::move(mmapSource);
    }
}

MmapFormat AmeisenNavigation::GetMmapFormat() const noexcept
{
    return Settings.useAnp ? MmapFormat::UNKNOWN : Settings.mmapFormat;
}

bool AmeisenNavigation::NewClient(size_t clientId)
{
    auto client = std::make_shared<AmeisenNavClient>(clientId, FilterProvider.get(), Settings.maxPolyPath,
                                                     Settings.maxPointPath);

    std::unique_lock lock(ClientsMutex);
    const auto [it, inserted] = Clients.try_emplace(clientId, std::move(client));
    LogD("New client: ", clientId);
    return inserted;
}

void AmeisenNavigation::FreeClient(size_t clientId)
{
    std::shared_ptr<AmeisenNavClient> client;

    {
        std::unique_lock lock(ClientsMutex);
        const auto it = Clients.find(clientId);

        if (it == Clients.end())
        {
            return;
        }

        // Destroy outside the lock (frees all queries/buffers).
        client = std::move(it->second);
        Clients.erase(it);
    }

    LogD("Freed client: ", clientId);
}

std::shared_ptr<AmeisenNavClient> AmeisenNavigation::GetClient(size_t clientId) const
{
    std::shared_lock lock(ClientsMutex);
    const auto it = Clients.find(clientId);
    return it != Clients.end() ? it->second : nullptr;
}

size_t AmeisenNavigation::GetClientCount() const
{
    std::shared_lock lock(ClientsMutex);
    return Clients.size();
}

bool AmeisenNavigation::PreloadMap(int mapId) noexcept { return NavSource->Get(mapId) != nullptr; }

bool AmeisenNavigation::GetPath(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition,
                                Path& path)
{
    path.Clear();
    const auto ctx = GetQueryContext(clientId, mapId);

    if (!ctx)
    {
        return false;
    }

    LogD("[", clientId, "] GetPath (", mapId, ") ", startPosition, " -> ", endPosition);

    if (!CalculateNormalPath(ctx.query, ctx.filter, *ctx.client, startPosition, endPosition, path))
    {
        return false;
    }

    path.ToWowCoords();
    return true;
}

bool AmeisenNavigation::GetRandomPath(size_t clientId, int mapId, const Vector3& startPosition,
                                      const Vector3& endPosition, Path& path, float maxRandomDistance)
{
    path.Clear();
    const auto ctx = GetQueryContext(clientId, mapId);

    if (!ctx)
    {
        return false;
    }

    LogD("[", clientId, "] GetRandomPath (", mapId, ") ", startPosition, " -> ", endPosition);

    // The straight path corner refs need their own buffer, the poly path buffer is still being read while
    // findStraightPath writes them.
    dtPolyRef* cornerRefs = ctx.client->GetStraightPathRefBuffer();

    if (!CalculateNormalPath(ctx.query, ctx.filter, *ctx.client, startPosition, endPosition, path, cornerRefs))
    {
        return false;
    }

    if (maxRandomDistance > 0.0f)
    {
        // Keep start and end, jitter every corner in between on the navmesh.
        for (int i = 1; i < path.pointCount - 1; ++i)
        {
            if (!cornerRefs[i])
            {
                continue;
            }

            dtPolyRef randomRef = 0;
            Vector3 randomPoint;
            const dtStatus status = ctx.query->findRandomPointAroundCircle(
                cornerRefs[i], path[i], maxRandomDistance, ctx.filter, GetRandomFloat, &randomRef, randomPoint);

            if (dtStatusSucceed(status) && randomRef)
            {
                path[i] = randomPoint;
            }
        }
    }

    path.ToWowCoords();
    return true;
}

bool AmeisenNavigation::MoveAlongSurface(size_t clientId, int mapId, const Vector3& startPosition,
                                         const Vector3& endPosition, Vector3& positionToGoTo)
{
    const auto ctx = GetQueryContext(clientId, mapId);

    if (!ctx)
    {
        return false;
    }

    LogD("[", clientId, "] MoveAlongSurface (", mapId, ") ", startPosition, " -> ", endPosition);

    PolyPosition start;

    if (!FindNearestPolyWow(ctx.query, ctx.filter, startPosition, start))
    {
        return false;
    }

    Vector3 rdEnd;
    endPosition.CopyToRDCoords(rdEnd);

    int visitedCount = 0;
    dtPolyRef visited[MOVE_ALONG_SURFACE_VISITED_SIZE]{};
    Vector3 result;

    const dtStatus status = ctx.query->moveAlongSurface(start.poly, start.pos, rdEnd, ctx.filter, result, visited,
                                                        &visitedCount, MOVE_ALONG_SURFACE_VISITED_SIZE);

    if (dtStatusFailed(status))
    {
        LogE("[", clientId, "] moveAlongSurface failed: 0x", std::format("{:08X}", status));
        return false;
    }

    // moveAlongSurface doesn't project the result onto the surface, fix the height.
    const dtPolyRef resultPoly = visitedCount > 0 ? visited[visitedCount - 1] : start.poly;
    float height = 0.0f;

    if (dtStatusSucceed(ctx.query->getPolyHeight(resultPoly, result, &height)))
    {
        result.y = height;
    }

    positionToGoTo = result.ToWowCoords();
    return true;
}

bool AmeisenNavigation::GetRandomPoint(size_t clientId, int mapId, Vector3& position)
{
    const auto ctx = GetQueryContext(clientId, mapId);

    if (!ctx)
    {
        return false;
    }

    dtPolyRef polyRef = 0;
    const dtStatus status = ctx.query->findRandomPoint(ctx.filter, GetRandomFloat, &polyRef, position);

    if (dtStatusFailed(status) || !polyRef)
    {
        LogE("[", clientId, "] findRandomPoint failed: 0x", std::format("{:08X}", status));
        return false;
    }

    position.ToWowCoords();
    return true;
}

bool AmeisenNavigation::GetRandomPointAround(size_t clientId, int mapId, const Vector3& startPosition, float radius,
                                             Vector3& position)
{
    const auto ctx = GetQueryContext(clientId, mapId);

    if (!ctx || !std::isfinite(radius) || radius < 0.0f)
    {
        return false;
    }

    PolyPosition start;

    if (!FindNearestPolyWow(ctx.query, ctx.filter, startPosition, start))
    {
        return false;
    }

    dtPolyRef polyRef = 0;
    const dtStatus status = ctx.query->findRandomPointAroundCircle(start.poly, start.pos, radius, ctx.filter,
                                                                   GetRandomFloat, &polyRef, position);

    if (dtStatusFailed(status) || !polyRef)
    {
        return false;
    }

    position.ToWowCoords();
    return true;
}

bool AmeisenNavigation::GetHeight(size_t clientId, int mapId, const Vector3& position, Vector3& out)
{
    const auto ctx = GetQueryContext(clientId, mapId);

    if (!ctx)
    {
        return false;
    }

    Vector3 rdPos;
    position.CopyToRDCoords(rdPos);

    // Collect every polygon in the vertical column and pick the surface closest to the input height.
    // This handles stacked floors (bridges, buildings) better than a plain nearest poly query.
    constexpr int MAX_COLUMN_POLYS = 64;
    constexpr float COLUMN_EXTENTS[3] = {0.5f, HEIGHT_QUERY_EXTENTS[1], 0.5f};
    dtPolyRef polys[MAX_COLUMN_POLYS]{};
    int polyCount = 0;

    if (dtStatusSucceed(ctx.query->queryPolygons(rdPos, COLUMN_EXTENTS, ctx.filter, polys, &polyCount,
                                                 MAX_COLUMN_POLYS)))
    {
        bool found = false;
        float bestHeight = 0.0f;

        for (int i = 0; i < polyCount; ++i)
        {
            float h = 0.0f;

            if (dtStatusSucceed(ctx.query->getPolyHeight(polys[i], rdPos, &h))
                && (!found || std::fabs(h - rdPos.y) < std::fabs(bestHeight - rdPos.y)))
            {
                bestHeight = h;
                found = true;
            }
        }

        if (found)
        {
            out = Vector3(rdPos.x, bestHeight, rdPos.z).ToWowCoords();
            return true;
        }
    }

    // Not above any polygon (e.g. slightly off-mesh): fall back to the closest point on the mesh.
    PolyPosition nearest;

    if (FindNearestPoly(ctx.query, ctx.filter, rdPos, nearest, HEIGHT_QUERY_EXTENTS))
    {
        out = nearest.pos.ToWowCoords();
        return true;
    }

    return false;
}

bool AmeisenNavigation::CastMovementRay(size_t clientId, int mapId, const Vector3& startPosition,
                                        const Vector3& endPosition, RaycastResult* result)
{
    RaycastResult local;
    RaycastResult& res = result ? *result : local;
    res = RaycastResult{};

    const auto ctx = GetQueryContext(clientId, mapId);

    if (!ctx)
    {
        return false;
    }

    LogD("[", clientId, "] CastMovementRay (", mapId, ") ", startPosition, " -> ", endPosition);

    PolyPosition start;

    if (!FindNearestPolyWow(ctx.query, ctx.filter, startPosition, start))
    {
        return false;
    }

    Vector3 rdEnd;
    endPosition.CopyToRDCoords(rdEnd);

    // dtRaycastHit has no constructor, path/maxPath MUST be initialized or Detour writes through garbage.
    dtRaycastHit hit{};
    hit.path = nullptr;
    hit.maxPath = 0;

    const dtStatus status = ctx.query->raycast(start.poly, start.pos, rdEnd, ctx.filter, 0, &hit);

    if (dtStatusFailed(status))
    {
        LogE("[", clientId, "] raycast failed: 0x", std::format("{:08X}", status));
        return false;
    }

    if (hit.t == FLT_MAX)
    {
        res.hit = false;
        res.t = 1.0f;
        res.hitPosition = endPosition;
        return true;
    }

    Vector3 rdHit;
    dtVlerp(rdHit, start.pos, rdEnd, hit.t);

    res.hit = true;
    res.t = hit.t;
    res.hitPosition = rdHit.ToWowCoords();
    res.hitNormal = Vector3(hit.hitNormal).ToWowCoords();
    return false;
}

bool AmeisenNavigation::PostProcessClosestPointOnPoly(size_t clientId, int mapId, const Path& input, Path& output)
{
    output.Clear();
    const auto ctx = GetQueryContext(clientId, mapId);

    if (!ctx)
    {
        return false;
    }

    for (const auto& point : input)
    {
        PolyPosition nearest;

        if (FindNearestPolyWow(ctx.query, ctx.filter, point, nearest))
        {
            if (!output.TryAppendUnique(nearest.pos.ToWowCoords()))
            {
                break;
            }
        }
    }

    return output.pointCount > 0;
}

bool AmeisenNavigation::PostProcessMoveAlongSurface(size_t clientId, int mapId, const Path& input, Path& output)
{
    output.Clear();
    const auto ctx = GetQueryContext(clientId, mapId);

    if (!ctx || input.pointCount == 0)
    {
        return false;
    }

    PolyPosition current;

    if (!FindNearestPolyWow(ctx.query, ctx.filter, input[0], current))
    {
        return false;
    }

    Vector3 wowStart = current.pos;
    output.TryAppend(wowStart.ToWowCoords());

    dtPolyRef visited[MOVE_ALONG_SURFACE_VISITED_SIZE]{};

    for (int i = 1; i < input.pointCount && !output.IsFull(); ++i)
    {
        Vector3 target;
        input[i].CopyToRDCoords(target);

        const Vector3 segmentStart = current.pos;
        const float distance = dtVdist(segmentStart, target);
        const int steps = std::max(1, static_cast<int>(std::ceil(distance / MOVE_ALONG_SURFACE_MAX_CHUNK)));

        for (int s = 1; s <= steps && !output.IsFull(); ++s)
        {
            Vector3 stepTarget;
            dtVlerp(stepTarget, segmentStart, target, static_cast<float>(s) / static_cast<float>(steps));

            int visitedCount = 0;
            Vector3 result;

            const dtStatus status = ctx.query->moveAlongSurface(current.poly, current.pos, stepTarget, ctx.filter,
                                                                result, visited, &visitedCount,
                                                                MOVE_ALONG_SURFACE_VISITED_SIZE);

            if (dtStatusFailed(status))
            {
                return output.pointCount > 0;
            }

            if (visitedCount > 0)
            {
                current.poly = visited[visitedCount - 1];
            }

            float height = 0.0f;

            if (dtStatusSucceed(ctx.query->getPolyHeight(current.poly, result, &height)))
            {
                result.y = height;
            }

            current.pos = result;

            Vector3 wow = result;
            output.TryAppendUnique(wow.ToWowCoords());
        }
    }

    return true;
}

void AmeisenNavigation::SmoothPathChaikinCurve(const Path& input, Path& output) const noexcept
{
    ChaikinCurve::SmoothPath(input.points, input.pointCount, output);
}

void AmeisenNavigation::SmoothPathCatmullRom(const Path& input, Path& output, int points, float alpha) const noexcept
{
    CatmullRomSpline::SmoothPath(input.points, input.pointCount, output, points, alpha);
}

void AmeisenNavigation::SmoothPathBezier(const Path& input, Path& output, int points) const noexcept
{
    BezierCurve::SmoothPath(input.points, input.pointCount, output, points);
}

AmeisenNavigation::QueryContext AmeisenNavigation::GetQueryContext(size_t clientId, int mapId)
{
    QueryContext ctx;
    ctx.client = GetClient(clientId);

    if (!ctx.client)
    {
        LogE("Unknown client: ", clientId);
        return {};
    }

    ctx.filter = ctx.client->QueryFilter();

    if (!ctx.filter)
    {
        return {};
    }

    // Fast path: the client already has a query for this map.
    if ((ctx.query = ctx.client->GetNavmeshQuery(mapId)))
    {
        return ctx;
    }

    dtNavMesh* navMesh = NavSource->Get(mapId);

    if (!navMesh)
    {
        LogE("[", clientId, "] No navmesh available for map ", mapId);
        return {};
    }

    NavMeshQueryPtr query(dtAllocNavMeshQuery());

    if (!query)
    {
        LogE("[", clientId, "] Failed to allocate dtNavMeshQuery for map ", mapId);
        return {};
    }

    const dtStatus status = query->init(navMesh, Settings.maxSearchNodes);

    if (dtStatusFailed(status))
    {
        LogE("[", clientId, "] Failed to init dtNavMeshQuery for map ", mapId, ": 0x", std::format("{:08X}", status));
        return {};
    }

    ctx.query = query.get();
    ctx.client->SetNavmeshQuery(mapId, std::move(query));
    return ctx;
}

bool AmeisenNavigation::CalculateNormalPath(dtNavMeshQuery* query, const dtQueryFilter* filter,
                                            AmeisenNavClient& client, const Vector3& startPosition,
                                            const Vector3& endPosition, Path& path,
                                            dtPolyRef* straightPathRefs) noexcept
{
    path.Clear();

    if (!startPosition.IsFinite() || !endPosition.IsFinite())
    {
        return false;
    }

    PolyPosition start;
    PolyPosition end;

    if (!FindNearestPolyWow(query, filter, startPosition, start))
    {
        LogD("No poly near start position ", startPosition);
        return false;
    }

    if (!FindNearestPolyWow(query, filter, endPosition, end))
    {
        LogD("No poly near end position ", endPosition);
        return false;
    }

    dtPolyRef* polyPath = client.GetPolyPathBuffer();
    int polyPathCount = 0;

    const dtStatus polyPathStatus = query->findPath(start.poly, end.poly, start.pos, end.pos, filter, polyPath,
                                                    &polyPathCount, client.GetPolyPathBufferSize());

    if (dtStatusFailed(polyPathStatus) || polyPathCount <= 0)
    {
        LogE("findPath failed: 0x", std::format("{:08X}", polyPathStatus));
        return false;
    }

    if (dtStatusDetail(polyPathStatus, DT_PARTIAL_RESULT))
    {
        LogD("findPath: partial result (end not reachable), ", polyPathCount, " polys");
    }

    // For partial paths, aim at the closest point of the last reached polygon instead of the unreachable end.
    Vector3 endPos = end.pos;

    if (polyPath[polyPathCount - 1] != end.poly)
    {
        query->closestPointOnPoly(polyPath[polyPathCount - 1], end.pos, endPos, nullptr);
    }

    // Corner refs go into the client's buffer, never write more corners than it holds.
    const int maxCorners =
        straightPathRefs ? std::min(path.maxSize, client.GetStraightPathRefBufferSize()) : path.maxSize;

    const dtStatus straightPathStatus =
        SafeFindStraightPath(query, start.pos, endPos, polyPath, polyPathCount, reinterpret_cast<float*>(path.points),
                             nullptr, straightPathRefs, &path.pointCount, maxCorners);

    if (dtStatusFailed(straightPathStatus) || path.pointCount <= 0)
    {
        LogE("findStraightPath failed: 0x", std::format("{:08X}", straightPathStatus));
        path.Clear();
        return false;
    }

    return true;
}
