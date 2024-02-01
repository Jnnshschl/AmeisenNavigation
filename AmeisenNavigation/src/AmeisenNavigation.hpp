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
#include "Mmap/MmapFormat.hpp"
#include "Mmap/MmapTileHeader.hpp"
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

constexpr auto MMAP_MAGIC = 0x4D4D4150;
constexpr auto MMAP_VERSION = 15;

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
    std::filesystem::path MmapFolder;
    int MaxPolyPath;
    int MaxSearchNodes;

    std::unordered_map<MmapFormat, std::unordered_map<size_t, std::pair<std::mutex, dtNavMesh*>>> NavMeshMap;
    std::unordered_map<size_t, AmeisenNavClient*> Clients;
    QueryFilterProvider* FilterProvider;

    std::map<MmapFormat, std::pair<std::string_view, std::string_view>> MmapFormatPatterns
    {
        { MmapFormat::TC335A, std::make_pair("{:03}.mmap", "{:03}{:02}{:02}.mmtile") },
        { MmapFormat::SF548, std::make_pair("{:04}.mmap", "{:04}_{:02}_{:02}.mmtile") },
    };

public:
    /// <summary>
    /// Create a new instance of the AmeisenNavigation class to handle pathfinding.
    /// </summary>
    /// <param name="mmapFolder">Folder twhere the MMAPs are stored.</param>
    /// <param name="maxSearchNodes">Hoaw many nodes detour searches max.</param>
    AmeisenNavigation(const std::string& mmapFolder, int maxPolyPath, int maxSearchNodes) noexcept
        : MmapFolder(mmapFolder),
        MaxPolyPath(maxPolyPath),
        MaxSearchNodes(maxSearchNodes),
        NavMeshMap(),
        Clients(),
        FilterProvider(new QueryFilterProvider())
    {}

    ~AmeisenNavigation()
    {
        delete FilterProvider;

        for (const auto& client : Clients)
        {
            if (client.second)
            {
                delete client.second;
            }
        }

        for (auto& [format, map] : NavMeshMap)
        {
            for (auto& [id, mtxNavMesh] : map)
            {
                const std::lock_guard<std::mutex> lock(mtxNavMesh.first);

                if (mtxNavMesh.second)
                {
                    dtFreeNavMesh(mtxNavMesh.second);
                }
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
    /// Set a custom MMAP filename format. Patterns need to be in std::format style.
    /// </summary>
    /// <param name="mmapPattern">Pattern for the .map files.</param>
    /// <param name="mmtilePattern">Pattern for the .mmtile files.</param>
    /// <param name="format">Format to change, default is -1 (CUSTOM)</param>
    constexpr inline void SetCustomMmapFormat(const std::string& mmapPattern, const std::string& mmtilePattern, MmapFormat format = MmapFormat::CUSTOM) noexcept
    {
        MmapFormatPatterns[format] = std::make_pair(mmapPattern, mmtilePattern);
    }

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

    /// <summary>
    /// Load the MMAPs for a map.
    /// </summary>
    /// <param name="mmapFormat">Format of the MMAPs, may be 0 (auto).</param>
    /// <param name="mapId">Id of the mmaps to load.</param>
    /// <returns>True if loaded, false if something went wrong.</returns>
    bool LoadMmaps(MmapFormat& mmapFormat, int mapId) noexcept;

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
    inline bool IsMmapLoaded(MmapFormat format, int mapId) noexcept { return NavMeshMap[format][mapId].second != nullptr; }

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

    /// <summary>
    /// Used to detect the mmap file format.
    /// </summary>
    /// <returns>Mmap format detected or MMAP_FORMAT::UNKNOWN</returns>
    inline MmapFormat TryDetectMmapFormat() noexcept
    {
        for (const auto& [format, pattern] : MmapFormatPatterns)
        {
            if (std::filesystem::exists(MmapFolder / std::vformat(pattern.second, std::make_format_args(0, 27, 27))))
            {
                ANAV_DEBUG_ONLY(std::cout << ">> MMAP format: " << static_cast<int>(format) << std::endl);
                return format;
            }
        }

        ANAV_DEBUG_ONLY(std::cout << ">> MMAP format unknown" << std::endl);
        return MmapFormat::UNKNOWN;
    }
};
