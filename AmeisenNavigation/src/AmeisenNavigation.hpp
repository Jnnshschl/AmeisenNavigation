#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include "Clients/AmeisenNavClient.hpp"
#include "Helpers/Polygon.hpp"
#include "Mmap/MmapFormat.hpp"
#include "Mmap/MmapTileHeader.hpp"
#include "Smoothing/BezierCurve.hpp"
#include "Smoothing/CatmullRomSpline.hpp"
#include "Smoothing/ChaikinCurve.hpp"
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

constexpr auto MAX_RAND_F = static_cast<float>(RAND_MAX);
inline float GetRandomFloat() { return static_cast<float>(rand()) / MAX_RAND_F; };

// macro to print all values of a vector3
#define PRINT_VEC3(vec) "[" << vec[0] << ", " << vec[1] << ", " << vec[2] << "]"

class AmeisenNavigation
{
private:
    std::filesystem::path MmapFolder;
    int MaxPathNodes;
    int MaxSearchNodes;

    std::unordered_map<MmapFormat, std::unordered_map<size_t, std::pair<std::mutex, dtNavMesh*>>> NavMeshMap;
    std::unordered_map<size_t, AmeisenNavClient*> Clients;

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
    /// <param name="maxPathNodes">How many nodes a path can have.</param>
    /// <param name="maxSearchNodes">Hoaw many nodes detour searches max.</param>
    AmeisenNavigation(const std::string& mmapFolder, int maxPathNodes, int maxSearchNodes)
        : MmapFolder(mmapFolder),
        MaxPathNodes(maxPathNodes),
        MaxSearchNodes(maxSearchNodes),
        NavMeshMap(),
        Clients()
    {
        // seed the random generator
        srand(static_cast<unsigned int>(time(0)));
    }

    ~AmeisenNavigation()
    {
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
                if (mtxNavMesh.second)
                {
                    const std::lock_guard<std::mutex> lock(mtxNavMesh.first);
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
    /// <param name="version">Version of the client.</param>
    void NewClient(size_t clientId, MmapFormat version) noexcept;

    /// <summary>
    /// Call this to free a client.
    /// </summary>
    /// <param name="id">Uinique id of the client.</param>
    void FreeClient(size_t clientId) noexcept;

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
    /// <param name="pathSize">The paths size.</param>
    /// <returns>True when a path was found, false if not.</returns>
    bool GetPath(size_t clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize) noexcept;

    /// <summary>
    /// Try to find a path from start to end but randomize the final positions by x meters.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="startPosition">Start position vector.</param>
    /// <param name="endPosition">End position vector.</param>
    /// <param name="path">Path buffer.</param>
    /// <param name="pathSize">The paths size.</param>
    /// <param name="maxRandomDistance">Max distance to the original psoition.</param>
    /// <returns>True when a path was found, false if not.</returns>
    bool GetRandomPath(size_t clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize, float maxRandomDistance) noexcept;

    /// <summary>
    /// Try to move towards a specific location.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="startPosition">Start position vector.</param>
    /// <param name="endPosition">End position vector.</param>
    /// <param name="positionToGoTo">Target position.</param>
    /// <returns>True when a path was found, false if not.</returns>
    bool MoveAlongSurface(size_t clientId, int mapId, const float* startPosition, const float* endPosition, float* positionToGoTo) noexcept;

    /// <summary>
    /// Get a random point anywhere on the map.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="position">Random position.</param>
    /// <returns>True when a point has been found, fals eif not.</returns>
    bool GetRandomPoint(size_t clientId, int mapId, float* position) noexcept;

    /// <summary>
    /// Get a random point within x meters of the start position.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="startPosition">Start position vector.</param>
    /// <param name="radius">Max distance to search for a random position.</param>
    /// <param name="position">Random position.</param>
    /// <returns>True when a point has been found, fals eif not.</returns>
    bool GetRandomPointAround(size_t clientId, int mapId, const float* startPosition, float radius, float* position) noexcept;

    /// <summary>
    /// Cast a movement ray along the mesh and see whether it collides with a wall or not.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="startPosition">Start position vector.</param>
    /// <param name="endPosition">End position vector.</param>
    /// <param name="raycastHit">Detour raycast result.</param>
    /// <returns>True when the ray hited no wall, false if it did.</returns>
    bool CastMovementRay(size_t clientId, int mapId, const float* startPosition, const float* endPosition, dtRaycastHit* raycastHit) noexcept;

    /// <summary>
    /// Create a path to explore a polygon. Path will cover the area of the polygon and be created using a TSP algorithm to ensure efficiency.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="polyPoints">Input polygon points.</param>
    /// <param name="inputSize">Input polygon point count.</param>
    /// <param name="output">Output path.</param>
    /// <param name="outputSize">Output paths size.</param>
    /// <param name="startPosition">From where to start exploring the polygon.</param>
    /// <param name="viewDistance">How far can the agent see, lower values will result in more path points.</param>
    /// <returns>True when a path was found, false if not.</returns>
    bool GetPolyExplorationPath(size_t clientId, int mapId, const float* polyPoints, int inputSize, float* output, int* outputSize, float* tmpBuffer, const float* startPosition, float viewDistance) noexcept;

    /// <summary>
    /// Smooth a path using the chaikin-curve algorithm.
    /// </summary>
    /// <param name="input">Input path.</param>
    /// <param name="inputSize">Input path size.</param>
    /// <param name="output">Output path.</param>
    /// <param name="outputSize">Output paths size.</param>
    void SmoothPathChaikinCurve(const float* input, int inputSize, float* output, int* outputSize) const noexcept;

    /// <summary>
    /// Smooth a path using the catmull-rom-spline algorithm.
    /// </summary>
    /// <param name="input">Input path.</param>
    /// <param name="inputSize">Input path size.</param>
    /// <param name="output">Output path.</param>
    /// <param name="outputSize">Output paths size.</param>
    /// <param name="points">How many points to generate, the more the smoother the path will be.</param>
    /// <param name="alpha">Alpha to use for the catmull-rom-spline algorithm.</param>
    void SmoothPathCatmullRom(const float* input, int inputSize, float* output, int* outputSize, int points, float alpha) const noexcept;

    /// <summary>
    /// Smooth a path using the bezier-curve algorithm.
    /// </summary>
    /// <param name="input">Input path.</param>
    /// <param name="inputSize">Input path size.</param>
    /// <param name="output">Output path.</param>
    /// <param name="outputSize">Output paths size.</param>
    /// <param name="points">How many points to generate, the more the smoother the path will be.</param>
    void SmoothPathBezier(const float* input, int inputSize, float* output, int* outputSize, int points) const noexcept;

    /// <summary>
    /// Load the MMAPs for a map.
    /// </summary>
    /// <param name="mmapFormat">Format of the MMAPs, may be 0 (auto).</param>
    /// <param name="mapId">Id of the mmaps to load.</param>
    /// <returns>True if loaded, false if something went wrong.</returns>
    bool LoadMmaps(MmapFormat& mmapFormat, int mapId) noexcept;

private:
    /// <summary>
    /// Convert the recast and detour coordinates to wow coordinates.
    /// </summary>
    constexpr inline void RDToWowCoords(float* pos) const noexcept
    {
        std::swap(pos[2], pos[1]);
        std::swap(pos[1], pos[0]);
    }

    /// <summary>
    /// Convert the recast and detour coordinates to wow coordinates.
    /// </summary>
    constexpr inline void CopyRDToWowCoords(const float* in, float* out) const noexcept
    {
        out[0] = in[2];
        out[1] = in[0];
        out[2] = in[1];
    }

    /// <summary>
    /// Convert the wow coordinates to recast and detour coordinates.
    /// </summary>
    constexpr inline void WowToRDCoords(float* pos) const noexcept
    {
        std::swap(pos[0], pos[1]);
        std::swap(pos[1], pos[2]);
    }

    /// <summary>
    /// Convert the wow coordinates to recast and detour coordinates.
    /// </summary>
    constexpr inline void CopyWowToRDCoords(const float* in, float* out) const noexcept
    {
        out[0] = in[1];
        out[1] = in[2];
        out[2] = in[0];
    }

    /// <summary>
    /// Try to find the nearest poly for a given position.
    /// </summary>
    /// <param name="client">The client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="position">Current position.</param>
    /// <param name="closestPointOnPoly">Closest point on the found poly.</param>
    /// <param name="polyRef">dtPolyRef of the found poly.</param>
    /// <returns>Reference to the found poly if found, else 0.</returns>
    inline dtStatus GetNearestPoly(AmeisenNavClient* client, int mapId, float* position, float* closestPointOnPoly, dtPolyRef* polyRef) const noexcept
    {
        const float extents[3] = { 6.0f, 6.0f, 6.0f };
        return client->GetNavmeshQuery(mapId)->findNearestPoly(position, extents, &client->QueryFilter(), polyRef, closestPointOnPoly);
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
    bool InitQueryAndLoadMmaps(size_t clientId, int mapId) noexcept;

    /// <summary>
    /// Used by the GetPath and GetRandomPath methods to generate a path.
    /// </summary>
    bool CalculateNormalPath(size_t clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize, dtPolyRef* visited = nullptr) noexcept;

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
