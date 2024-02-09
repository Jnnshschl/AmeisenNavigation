#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../recastnavigation/Detour/Include/DetourCommon.h"
#include "../../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "Clients/AmeisenNavClient.hpp"
#include "Helpers/Polygon.hpp"
#include "NavSources/INavSource.hpp"
#include "NavSources/Anp/AnpNavSource.hpp"
#include "NavSources/Anp/AnpQueryFilterProvider.hpp"
#include "NavSources/Mmap/MmapNavSource.hpp"
#include "NavSources/Mmap/MmapQueryFilterProvider.hpp"
#include "Smoothing/BezierCurve.hpp"
#include "Smoothing/CatmullRomSpline.hpp"
#include "Smoothing/ChaikinCurve.hpp"
#include "Utils/Path.hpp"
#include "Utils/PolyPosition.hpp"
#include "Utils/Vector3.hpp"
#include "Utils/VectorUtils.hpp"

#ifdef _DEBUG
#define ANAV_DEBUG_ONLY(x) x
#define ANAV_ERROR_MSG(x) x
#else
#define ANAV_DEBUG_ONLY(x)
#define ANAV_ERROR_MSG(x)
#endif

// macro to print all values of a vector3
#define PRINT_VEC3(vec) "[" << vec.x << ", " << vec.y << ", " << vec.z << "]"


static float GetRandomFloat() noexcept
{
    static std::mt19937 rng{ std::random_device{}() };
    static std::uniform_real_distribution<float> dis{ 0.0f, 1.0f };
    return dis(rng);
}

class AmeisenNavigation
{
private:
    int MaxPolyPath;
    int MaxSearchNodes;
    INavSource* NavSource;
    IQueryFilterProvider* FilterProvider;
    std::unordered_map<size_t, AmeisenNavClient*> Clients;

public:
    /// <summary>
    /// Create a new instance of the AmeisenNavigation class to handle pathfinding.
    /// </summary>
    /// <param name="meshFolder">Folder where the navmesh files are stored.</param>
    /// <param name="maxSearchNodes">How many nodes detour path can have.</param>
    /// <param name="maxSearchNodes">How many nodes detour searches max.</param>
    /// <param name="useAnp">Use the .anp files generated by the AmeisenNavigation.Export tool instead of MMAPS.</param>
    AmeisenNavigation(const std::string& meshFolder, int maxPolyPath, int maxSearchNodes, bool useAnp = false) noexcept
        : MaxPolyPath(maxPolyPath),
        MaxSearchNodes(maxSearchNodes),
        Clients()
    {
        if (useAnp)
        {
            NavSource = new AnpNavSource(meshFolder.c_str());
            FilterProvider = new AnpQueryFilterProvider();
        }
        else
        {
            NavSource = new MmapNavSource(meshFolder.c_str(), MmapFormat::UNKNOWN);
            FilterProvider = new MmapQueryFilterProvider(reinterpret_cast<MmapNavSource*>(NavSource)->GetFormat());
        }
    }

    ~AmeisenNavigation()
    {
        delete NavSource;
        delete FilterProvider;

        for (const auto& client : Clients)
        {
            if (client.second)
            {
                delete client.second;
            }
        }
    }

    AmeisenNavigation(const AmeisenNavigation&) = delete;
    AmeisenNavigation& operator=(const AmeisenNavigation&) = delete;

    /// <summary>
    /// Call this to register a new client.
    /// </summary>
    /// <param name="id">Unique id for the client.</param>
    /// <param name="format">MMAP format of the client.</param>
    void NewClient(size_t clientId, MmapFormat format) noexcept;

    /// <summary>
    /// Call this to free a client.
    /// </summary>
    /// <param name="id">Unique id of the client.</param>
    void FreeClient(size_t clientId) noexcept;

    /// <summary>
    /// Get a client by its id, returns nullptr if client id is unknown.
    /// </summary>
    /// <param name="clientId">Unique id of the client.</param>
    /// <returns>Client if found.</returns>
    AmeisenNavClient* GetClient(size_t clientId) noexcept;

    /// <summary>
    /// Try to find a path from start to end.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="startPosition">Start position vector.</param>
    /// <param name="endPosition">End position vector.</param>
    /// <param name="path">Path buffer.</param>
    /// <returns>True when a path was found, false if not.</returns>
    bool GetPath(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition, Path& path) noexcept;

    /// <summary>
    /// Try to find a path from start to end but randomize the final positions by x meters.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="startPosition">Start position vector.</param>
    /// <param name="endPosition">End position vector.</param>
    /// <param name="path">Path buffer.</param>
    /// <param name="maxRandomDistance">Max distance to the original psoition.</param>
    /// <returns>True when a path was found, false if not.</returns>
    bool GetRandomPath(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition, Path& path, float maxRandomDistance) noexcept;

    /// <summary>
    /// Try to move towards a specific location.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="startPosition">Start position vector.</param>
    /// <param name="endPosition">End position vector.</param>
    /// <param name="positionToGoTo">Target position.</param>
    /// <returns>True when a path was found, false if not.</returns>
    bool MoveAlongSurface(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition, Vector3& positionToGoTo) noexcept;

    /// <summary>
    /// Get a random point anywhere on the map.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="position">Random position.</param>
    /// <returns>True when a point has been found, fals eif not.</returns>
    bool GetRandomPoint(size_t clientId, int mapId, Vector3& position) noexcept;

    /// <summary>
    /// Get a random point within x meters of the start position.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="startPosition">Start position vector.</param>
    /// <param name="radius">Max distance to search for a random position.</param>
    /// <param name="position">Random position.</param>
    /// <returns>True when a point has been found, fals eif not.</returns>
    bool GetRandomPointAround(size_t clientId, int mapId, const Vector3& startPosition, float radius, Vector3& position) noexcept;

    /// <summary>
    /// Cast a movement ray along the mesh and see whether it collides with a wall or not.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="startPosition">Start position vector.</param>
    /// <param name="endPosition">End position vector.</param>
    /// <param name="raycastHit">Detour raycast result.</param>
    /// <returns>True when the ray hited no wall, false if it did.</returns>
    bool CastMovementRay(size_t clientId, int mapId, const Vector3& startPosition, const Vector3& endPosition, dtRaycastHit* raycastHit) noexcept;

    /// <summary>
    /// Create a path to explore a polygon. Path will cover the area of the polygon and be created using a TSP algorithm to ensure efficiency.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="polyPoints">Input polygon points.</param>
    /// <param name="inputSize">Input polygon point count.</param>
    /// <param name="path">Path.</param>
    /// <param name="tmpPath">Temporary path.</param>
    /// <param name="startPosition">From where to start exploring the polygon.</param>
    /// <param name="viewDistance">How far can the agent see, lower values will result in more path points.</param>
    /// <returns>True when a path was found, false if not.</returns>
    bool GetPolyExplorationPath(size_t clientId, int mapId, const Vector3* polyPoints, int inputSize, Path& path, Path& tmpPath, const Vector3& startPosition, float viewDistance) noexcept;

    /// <summary>
    /// Post process a path using closestPointOnPoly. Used to "validate" smoothed paths.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="input">Input path.</param>
    /// <param name="output">Output path.</param>
    bool PostProcessClosestPointOnPoly(size_t clientId, int mapId, const Path& input, Path& output) noexcept;

    /// <summary>
    /// Post process a path using moveAlongSurface. Used to "validate" smoothed paths.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="input">Input path.</param>
    /// <param name="output">Output path.</param>
    bool PostProcessMoveAlongSurface(size_t clientId, int mapId, const Path& input, Path& output) noexcept;

    /// <summary>
    /// Smooth a path using the chaikin-curve algorithm.
    /// </summary>
    /// <param name="input">Input path.</param>
    /// <param name="inputSize">Input path size.</param>
    /// <param name="output">Output path.</param>
    /// <param name="outputSize">Output paths size.</param>
    void SmoothPathChaikinCurve(const Path& input, Path& output) const noexcept;

    /// <summary>
    /// Smooth a path using the catmull-rom-spline algorithm.
    /// </summary>
    /// <param name="input">Input path.</param>
    /// <param name="inputSize">Input path size.</param>
    /// <param name="output">Output path.</param>
    /// <param name="outputSize">Output paths size.</param>
    /// <param name="points">How many points to generate, the more the smoother the path will be.</param>
    /// <param name="alpha">Alpha to use for the catmull-rom-spline algorithm.</param>
    void SmoothPathCatmullRom(const Path& input, Path& output, int points, float alpha) const noexcept;

    /// <summary>
    /// Smooth a path using the bezier-curve algorithm.
    /// </summary>
    /// <param name="input">Input path.</param>
    /// <param name="inputSize">Input path size.</param>
    /// <param name="output">Output path.</param>
    /// <param name="outputSize">Output paths size.</param>
    /// <param name="points">How many points to generate, the more the smoother the path will be.</param>
    void SmoothPathBezier(const Path& input, Path& output, int points) const noexcept;

private:
    /// <summary>
    /// Try to find the nearest poly for a given position.
    /// </summary>
    /// <param name="client">The client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="position">Current position.</param>
    /// <param name="closestPointOnPoly">Closest point on the found poly.</param>
    /// <param name="polyRef">dtPolyRef of the found poly.</param>
    /// <returns>Reference to the found poly if found, else 0.</returns>
    inline dtStatus GetNearestPoly(const dtNavMeshQuery* query, const dtQueryFilter* filter, const Vector3& position, Vector3& closestPointOnPoly, dtPolyRef* polyRef, bool convertWowToRd = true) const noexcept
    {
        const float extents[3] = { 6.0f, 6.0f, 6.0f };

        if (convertWowToRd)
        {
            position.CopyToRDCoords(closestPointOnPoly);
            return query->findNearestPoly(closestPointOnPoly, extents, filter, polyRef, closestPointOnPoly);
        }

        return query->findNearestPoly(position, extents, filter, polyRef, closestPointOnPoly);
    }

    inline dtStatus GetNearestPoly(const dtNavMeshQuery* query, const dtQueryFilter* filter, const Vector3& position, PolyPosition& poly, bool convertWowToRd = true) const noexcept
    {
        return GetNearestPoly(query, filter, position, poly.pos, &poly.poly, convertWowToRd);
    }

    /// <summary>
    /// Returns true when th mmaps are loaded for the given continent.
    /// </summary>
    inline bool IsNavmeshLoaded(int mapId) noexcept { return NavSource->Get(mapId) != nullptr; }

    /// <summary>
    /// Returns true when the given client id is valid.
    /// </summary>
    inline bool IsValidClient(size_t clientId) noexcept { return Clients[clientId] != nullptr; }

    /// <summary>
    /// Initializes a NavMeshQuery for the given client.
    /// </summary>
    bool TryGetClientAndQuery(size_t clientId, int mapId, AmeisenNavClient*& client, dtNavMeshQuery*& query) noexcept;

    /// <summary>
    /// Used by the GetPath and GetRandomPath methods to generate a path.
    /// </summary>
    bool CalculateNormalPath(dtNavMeshQuery* query, dtQueryFilter* filter, dtPolyRef* polyPathBuffer, int maxPolyPathCount, const Vector3& startPosition, const Vector3& endPosition, Path& path, dtPolyRef* visited = nullptr) noexcept;
};
