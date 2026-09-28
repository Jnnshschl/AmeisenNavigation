#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <random>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>

#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include "Clients/AmeisenNavClient.hpp"
#include "Helpers/Polygon.hpp"
#include "Helpers/Tour.hpp"
#include "NavSources/Anp/AnpNavSource.hpp"
#include "NavSources/Anp/AnpQueryFilterProvider.hpp"
#include "NavSources/INavSource.hpp"
#include "NavSources/Mmap/MmapNavSource.hpp"
#include "NavSources/Mmap/MmapQueryFilterProvider.hpp"
#include "NavSources/NavMeshQueryPool.hpp"
#include "Smoothing/BezierCurve.hpp"
#include "Smoothing/CatmullRomSpline.hpp"
#include "Smoothing/ChaikinCurve.hpp"
#include "Utils/Logger.hpp"
#include "Utils/Path.hpp"
#include "Utils/PolyPosition.hpp"
#include "Utils/Vector3.hpp"
#include "Utils/VectorUtils.hpp"

/// Random float in [0, 1) for Detour's random point functions. One generator per thread.
inline float GetRandomFloat() noexcept
{
    thread_local std::mt19937 rng{std::random_device{}()};
    thread_local std::uniform_real_distribution<float> dis{0.0f, 1.0f};
    return dis(rng);
}

/// Search neighborhood half-extents for dtNavMeshQuery::findNearestPoly (in RD coordinates).
constexpr float NEAREST_POLY_EXTENTS[3] = {6.0f, 6.0f, 6.0f};

/// Larger vertical search extents for height queries where the input Z is unknown/unreliable.
constexpr float HEIGHT_QUERY_EXTENTS[3] = {6.0f, 2500.0f, 6.0f};

/// Maximum distance per step in PostProcessMoveAlongSurface before subdividing.
constexpr float MOVE_ALONG_SURFACE_MAX_CHUNK = 25.0f;

/// Size of the visited polygon buffer for moveAlongSurface calls. Detour's search uses its 64 node tiny pool, with
/// a smaller buffer the list is cut and its last entry isn't the polygon holding the result.
constexpr int MOVE_ALONG_SURFACE_VISITED_SIZE = 64;

/// ExplorePolygon limits: outline vertices, waypoints (the spacing grows to stay below it), minimum spacing,
/// size of the polygon's bounding box and how far a waypoint may move when it is snapped to the navmesh.
constexpr int MAX_EXPLORE_POLYGON_POINTS = 256;
constexpr int MAX_EXPLORE_WAYPOINTS = 1024;
constexpr float MIN_EXPLORE_SPACING = 2.0f;
constexpr float MAX_EXPLORE_EXTENT = 10000.0f;
constexpr float MAX_EXPLORE_SNAP_DISTANCE = 32.0f;

/// Request limits. The WoW world spans +-17067 yards; bigger or non-finite coordinates are rejected before they
/// reach Detour, whose float -> int tile conversions are undefined for them (and loop for ages on ARM64).
/// Map ids are bounded so arbitrary ids can't grow the per-map caches without limit.
constexpr float MAX_COORDINATE = 100000.0f;
constexpr float MAX_QUERY_RADIUS = 5000.0f;
constexpr int MAX_MAP_ID = 65535;

/// True for finite positions inside the accepted coordinate range.
inline bool IsValidPosition(const Vector3& v) noexcept
{
    return v.IsFinite() && std::fabs(v.x) <= MAX_COORDINATE && std::fabs(v.y) <= MAX_COORDINATE
           && std::fabs(v.z) <= MAX_COORDINATE;
}

constexpr bool IsValidMapId(int mapId) noexcept { return mapId >= 0 && mapId <= MAX_MAP_ID; }

struct AmeisenNavigationSettings
{
    std::filesystem::path meshFolder;
    bool useAnp = false;
    MmapFormat mmapFormat = MmapFormat::UNKNOWN;
    MmapNavSource::Patterns customMmapPatterns{};
    int maxPolyPath = 2048;
    int maxPointPath = 512;
    int maxSearchNodes = 65535;
    float factionDangerCost = 3.0f;
    float waterCost = 1.6f;     // water and ocean
    float badLiquidCost = 4.0f; // lava and slime
    float roadCost = 0.75f;     // ANP roads (< 1 prefers roads)
    size_t maxIdleQueriesPerMap = 0; // dtNavMeshQuery objects kept per map between requests, 0 = automatic
};

/// Details of an ExplorePolygon run.
struct ExploreResult
{
    float spacing = 0.0f;  // spacing actually used (grown if the polygon needed too many waypoints)
    int waypoints = 0;     // waypoints on the navmesh inside the polygon
    int reached = 0;       // waypoints included in the route (the rest was unreachable)
    bool truncated = false; // the route was cut at the path buffer size
};

/// Result of CastMovementRay (positions in WoW coordinates).
struct RaycastResult
{
    bool hit = false;           // true if a wall was hit before reaching the end position
    float t = 0.0f;             // hit parameter along start->end (1 if no hit)
    Vector3 hitPosition{};      // position where the wall was hit (end position if no hit)
    Vector3 hitNormal{};        // wall normal (zero if no hit)
};

/// Thread-safe navigation engine. Any number of clients can issue queries concurrently, but each
/// individual client id must only be used by one thread at a time.
class AmeisenNavigation
{
    AmeisenNavigationSettings Settings;
    std::unique_ptr<INavSource> NavSource;
    std::unique_ptr<IQueryFilterProvider> FilterProvider;

    mutable std::shared_mutex ClientsMutex;
    std::unordered_map<size_t, std::shared_ptr<AmeisenNavClient>> Clients;

    // Query pools per map, created with the first request for a map and never removed (navmeshes aren't either).
    mutable std::shared_mutex QueryPoolsMutex;
    std::unordered_map<int, std::unique_ptr<NavMeshQueryPool>> QueryPools;

public:
    explicit AmeisenNavigation(const AmeisenNavigationSettings& settings);

    AmeisenNavigation(const AmeisenNavigation&) = delete;
    AmeisenNavigation& operator=(const AmeisenNavigation&) = delete;

    const AmeisenNavigationSettings& GetSettings() const noexcept { return Settings; }

    /// MMAP format in use (UNKNOWN for ANP navmeshes or when detection failed).
    MmapFormat GetMmapFormat() const noexcept;

    bool NewClient(size_t clientId);
    void FreeClient(size_t clientId);
    std::shared_ptr<AmeisenNavClient> GetClient(size_t clientId) const;
    size_t GetClientCount() const;

    /// Load a map's navmesh now instead of on the first request. Returns false if it doesn't exist.
    bool PreloadMap(int mapId) noexcept;

    /// dtNavMeshQuery objects created for a map so far (follows the peak number of concurrent requests).
    size_t GetQueryCount(int mapId) const;

    /// Find a path from start to end. Returns true on success, populates path (WoW coordinates).
    /// If the end isn't reachable the path leads as close as possible and *partial is set to true.
    bool GetPath(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition, Path& path,
                 bool* partial = nullptr);

    /// Find a path with randomized intermediate waypoints (within maxRandomDistance).
    bool GetRandomPath(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition,
                       Path& path, float maxRandomDistance, bool* partial = nullptr);

    /// Route that explores an area: waypoints on a hexagonal grid (`spacing` apart) inside the polygon
    /// (WoW x/y outline, the outline's z picks the floor), snapped to the navmesh, visited in a short tour
    /// from the start and connected by navmesh paths. Unreachable waypoints are skipped, the route is cut
    /// at the path's capacity. Returns false if no waypoint could be reached.
    bool ExplorePolygon(size_t clientId, int mapId, const Vector3& startPosition, std::span<const Vector3> polygon,
                        float spacing, Path& path, ExploreResult* result = nullptr);

    /// Move from start towards end along the navmesh surface (small deltas), result is snapped to the surface.
    bool MoveAlongSurface(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition,
                          Vector3& positionToGoTo);

    bool GetRandomPoint(size_t clientId, int mapId, Vector3& position);

    bool GetRandomPointAround(size_t clientId, int mapId, const Vector3& startPosition, float radius,
                              Vector3& position);

    /// Cast a ray along the navmesh surface. Returns true if the path is clear (no wall hit).
    /// Returns false on hits and on errors, check result->hit to tell them apart.
    bool CastMovementRay(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition,
                         RaycastResult* result = nullptr);

    /// Get the navmesh surface position at (x, y). Z is only used to pick between stacked floors.
    bool GetHeight(size_t clientId, int mapId, const Vector3& position, Vector3& out);

    /// Snap each path point to the nearest poly surface (validates smoothed paths).
    bool PostProcessClosestPointOnPoly(size_t clientId, int mapId, const Path& input, Path& output);

    /// Walk the path along the navmesh surface (validates smoothed paths with chunking).
    bool PostProcessMoveAlongSurface(size_t clientId, int mapId, const Path& input, Path& output);

    void SmoothPathChaikinCurve(const Path& input, Path& output) const noexcept;
    void SmoothPathCatmullRom(const Path& input, Path& output, int points, float alpha) const noexcept;
    void SmoothPathBezier(const Path& input, Path& output, int points) const noexcept;

private:
    /// Everything a request needs. Holds the leased query, keep it alive while the query is used.
    struct QueryContext
    {
        std::shared_ptr<AmeisenNavClient> client;
        NavMeshQueryPool::Lease lease;
        dtNavMeshQuery* query = nullptr;
        const dtQueryFilter* filter = nullptr;

        explicit operator bool() const noexcept { return client && query && filter; }
    };

    QueryContext GetQueryContext(size_t clientId, int mapId);

    /// The map's query pool, nullptr if the map has no navmesh.
    NavMeshQueryPool* GetQueryPool(int mapId);

    /// Every position that reaches Detour goes through here or is checked with IsValidPosition first.
    static bool FindNearestPoly(const dtNavMeshQuery* query, const dtQueryFilter* filter, const Vector3& rdPosition,
                                PolyPosition& result, const float* extents = NEAREST_POLY_EXTENTS) noexcept
    {
        result.poly = 0;

        if (!IsValidPosition(rdPosition))
        {
            return false;
        }

        const dtStatus status = query->findNearestPoly(rdPosition, extents, filter, &result.poly, result.pos);
        return dtStatusSucceed(status) && result.poly != 0;
    }

    static bool FindNearestPolyWow(const dtNavMeshQuery* query, const dtQueryFilter* filter,
                                   const Vector3& wowPosition, PolyPosition& result) noexcept
    {
        Vector3 rd;
        wowPosition.CopyToRDCoords(rd);
        return FindNearestPoly(query, filter, rd, result);
    }

    /// Polygon holding a moveAlongSurface result: the last visited one, or (if the list was cut) looked up.
    static dtPolyRef MoveResultPoly(const dtNavMeshQuery* query, const dtQueryFilter* filter, dtStatus status,
                                    const dtPolyRef* visited, int visitedCount, const Vector3& rdResult,
                                    dtPolyRef fallback) noexcept
    {
        if (visitedCount > 0 && !dtStatusDetail(status, DT_BUFFER_TOO_SMALL))
        {
            return visited[visitedCount - 1];
        }

        constexpr float extents[3]{0.5f, 2.0f, 0.5f};
        PolyPosition nearest;
        return FindNearestPoly(query, filter, rdResult, nearest, extents) ? nearest.poly : fallback;
    }

    /// Straight path in RD coordinates, optionally storing the poly ref of each corner. *partial is set when
    /// the end isn't reachable (the path then leads to the closest reachable point).
    bool CalculateNormalPath(dtNavMeshQuery* query, const dtQueryFilter* filter, AmeisenNavClient& client,
                             const Vector3& startPosition, const Vector3& endPosition, Path& path,
                             dtPolyRef* straightPathRefs = nullptr, bool* partial = nullptr) noexcept;
};
