#pragma once

#include <filesystem>
#include <memory>
#include <random>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include "Clients/AmeisenNavClient.hpp"
#include "NavSources/Anp/AnpNavSource.hpp"
#include "NavSources/Anp/AnpQueryFilterProvider.hpp"
#include "NavSources/INavSource.hpp"
#include "NavSources/Mmap/MmapNavSource.hpp"
#include "NavSources/Mmap/MmapQueryFilterProvider.hpp"
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

/// Size of the visited polygon buffer for moveAlongSurface calls.
constexpr int MOVE_ALONG_SURFACE_VISITED_SIZE = 32;

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

    /// Find a path from start to end. Returns true on success, populates path (WoW coordinates).
    bool GetPath(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition, Path& path);

    /// Find a path with randomized intermediate waypoints (within maxRandomDistance).
    bool GetRandomPath(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition,
                       Path& path, float maxRandomDistance);

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
    struct QueryContext
    {
        std::shared_ptr<AmeisenNavClient> client;
        dtNavMeshQuery* query = nullptr;
        const dtQueryFilter* filter = nullptr;

        explicit operator bool() const noexcept { return client && query && filter; }
    };

    QueryContext GetQueryContext(size_t clientId, int mapId);

    static bool FindNearestPoly(const dtNavMeshQuery* query, const dtQueryFilter* filter, const Vector3& rdPosition,
                                PolyPosition& result, const float* extents = NEAREST_POLY_EXTENTS) noexcept
    {
        result.poly = 0;
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

    /// Straight path in RD coordinates, optionally storing the poly ref of each corner.
    bool CalculateNormalPath(dtNavMeshQuery* query, const dtQueryFilter* filter, AmeisenNavClient& client,
                             const Vector3& startPosition, const Vector3& endPosition, Path& path,
                             dtPolyRef* straightPathRefs = nullptr) noexcept;
};
