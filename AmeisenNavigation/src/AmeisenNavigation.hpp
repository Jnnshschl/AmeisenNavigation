#pragma once

#include <format>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "../../recastnavigation/Detour/Include/DetourCommon.h"
#include "../../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "Clients/AmeisenNavClient.hpp"
#include "NavSources/Anp/AnpNavSource.hpp"
#include "NavSources/Anp/AnpQueryFilterProvider.hpp"
#include "NavSources/INavSource.hpp"
#include "NavSources/Mmap/MmapNavSource.hpp"
#include "NavSources/Mmap/MmapQueryFilterProvider.hpp"
#include "Smoothing/BezierCurve.hpp"
#include "Smoothing/CatmullRomSpline.hpp"
#include "Smoothing/ChaikinCurve.hpp"
#include "Utils/Path.hpp"
#include "Utils/PolyPosition.hpp"
#include "Utils/Vector3.hpp"
#include "Utils/VectorUtils.hpp"
#include "Utils/Logger.hpp"

// Debug-only tracing (compiled out in Release)
#ifdef _DEBUG
#define ANAV_DEBUG_ONLY(...) LogD(__VA_ARGS__)
#else
#define ANAV_DEBUG_ONLY(...)
#endif

// Error logging - always compiled in, errors must be visible in production
#define ANAV_ERROR_MSG(...) LogE(__VA_ARGS__)

inline float GetRandomFloat() noexcept
{
    thread_local std::mt19937 rng{ std::random_device{}() };
    thread_local std::uniform_real_distribution<float> dis{ 0.0f, 1.0f };
    return dis(rng);
}

/// Search neighborhood half-extents for dtNavMeshQuery::findNearestPoly (in RD coordinates).
constexpr float NEAREST_POLY_EXTENTS[3] = {6.0f, 6.0f, 6.0f};

/// Larger vertical search extents for height queries where the input Z is unknown/unreliable.
constexpr float HEIGHT_QUERY_EXTENTS[3] = {6.0f, 2500.0f, 6.0f};

/// Maximum distance per chunk in PostProcessMoveAlongSurface before subdividing.
constexpr float MOVE_ALONG_SURFACE_MAX_CHUNK = 25.0f;

/// Size of the visited polygon buffer for moveAlongSurface calls.
constexpr int MOVE_ALONG_SURFACE_VISITED_SIZE = 8;

class AmeisenNavigation
{
private:
    int MaxPolyPath;
    int MaxSearchNodes;
    std::unique_ptr<INavSource> NavSource;
    std::unique_ptr<IQueryFilterProvider> FilterProvider;
    mutable std::shared_mutex ClientsMutex;
    std::unordered_map<size_t, std::unique_ptr<AmeisenNavClient>> Clients;

public:
    AmeisenNavigation(const std::string& meshFolder, int maxPolyPath, int maxSearchNodes, bool useAnp = false,
                       float factionDangerCost = 3.0f)
        : MaxPolyPath(maxPolyPath), MaxSearchNodes(maxSearchNodes)
    {
        if (useAnp)
        {
            NavSource = std::make_unique<AnpNavSource>(meshFolder.c_str());
            FilterProvider = std::make_unique<AnpQueryFilterProvider>(1.6f, 4.0f, 0.75f, factionDangerCost);
        }
        else
        {
            auto mmapSource = std::make_unique<MmapNavSource>(meshFolder.c_str(), MmapFormat::UNKNOWN);
            FilterProvider = std::make_unique<MmapQueryFilterProvider>(mmapSource->GetFormat());
            NavSource = std::move(mmapSource);
        }
    }

    ~AmeisenNavigation()
    {
        std::unique_lock lock(ClientsMutex);
        Clients.clear();
    }

    AmeisenNavigation(const AmeisenNavigation&) = delete;
    AmeisenNavigation& operator=(const AmeisenNavigation&) = delete;

    void NewClient(size_t clientId, MmapFormat format);
    void FreeClient(size_t clientId);
    AmeisenNavClient* GetClient(size_t clientId);

    /// Find a path from start to end. Returns true on success, populates path.
    bool GetPath(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition,
                 Path& path);

    /// Find a path with randomized intermediate waypoints (within maxRandomDistance).
    bool GetRandomPath(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition, Path& path,
                       float maxRandomDistance);

    bool MoveAlongSurface(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition,
                          Vector3& positionToGoTo);

    bool GetRandomPoint(size_t clientId, int mapId, Vector3& position);

    bool GetRandomPointAround(size_t clientId, int mapId, const Vector3& startPosition, float radius,
                              Vector3& position);

    /// Cast a ray; returns true if the path is clear (no wall hit).
    bool CastMovementRay(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition,
                         dtRaycastHit* raycastHit);

    /// Get the navmesh terrain height at a position. Returns true on success, populates height.
    bool GetHeight(size_t clientId, int mapId, const Vector3& position, Vector3& out);

    /// Snap each path point to the nearest poly surface (validates smoothed paths).
    bool PostProcessClosestPointOnPoly(size_t clientId, int mapId, const Path& input, Path& output);

    /// Walk the path along the navmesh surface (validates smoothed paths with chunking).
    bool PostProcessMoveAlongSurface(size_t clientId, int mapId, const Path& input, Path& output);

    void SmoothPathChaikinCurve(const Path& input, Path& output) const noexcept;

    void SmoothPathCatmullRom(const Path& input, Path& output, int points, float alpha) const noexcept;

    void SmoothPathBezier(const Path& input, Path& output, int points) const noexcept;

private:
    inline dtStatus GetNearestPoly(const dtNavMeshQuery* query, const dtQueryFilter* filter, const Vector3& position,
                                   Vector3& closestPointOnPoly, dtPolyRef* polyRef,
                                   bool convertWowToRd = true) const noexcept
    {
        if (convertWowToRd)
        {
            position.CopyToRDCoords(closestPointOnPoly);
            return query->findNearestPoly(closestPointOnPoly, NEAREST_POLY_EXTENTS, filter, polyRef, closestPointOnPoly);
        }

        return query->findNearestPoly(position, NEAREST_POLY_EXTENTS, filter, polyRef, closestPointOnPoly);
    }

    inline dtStatus GetNearestPoly(const dtNavMeshQuery* query, const dtQueryFilter* filter, const Vector3& position,
                                   PolyPosition& poly, bool convertWowToRd = true) const noexcept
    {
        return GetNearestPoly(query, filter, position, poly.pos, &poly.poly, convertWowToRd);
    }

    inline bool IsNavmeshLoaded(int mapId) noexcept { return NavSource->Get(mapId) != nullptr; }

    bool TryGetClientAndQuery(size_t clientId, int mapId, AmeisenNavClient*& client, dtNavMeshQuery*& query);

    bool CalculateNormalPath(dtNavMeshQuery* query, dtQueryFilter* filter, dtPolyRef* polyPathBuffer,
                             int maxPolyPathCount, const Vector3& startPosition, const Vector3& endPosition, Path& path,
                             dtPolyRef* visited = nullptr) noexcept;
};
