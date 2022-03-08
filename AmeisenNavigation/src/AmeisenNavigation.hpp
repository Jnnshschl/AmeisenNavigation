#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../recastnavigation/Detour/Include/DetourCommon.h"
#include "../../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "Clients/AmeisenNavClient.hpp"

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

enum class MMAP_FORMAT
{
    UNKNOWN,
    TC335A,
    SF548
};

struct MmapTileHeader
{
    unsigned int mmapMagic;
    unsigned int dtVersion;
    unsigned int mmapVersion;
    unsigned int size;
    char usesLiquids;
    char padding[3];
};

class AmeisenNavigation
{
private:
    std::string MmapFolder;
    int MaxPathNodes;
    int MaxSearchNodes;

    std::unordered_map<int, std::pair<std::mutex, dtNavMesh*>> NavMeshMap;
    std::unordered_map<int, AmeisenNavClient*> Clients;

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

        for (auto& client : NavMeshMap)
        {
            if (client.second.second)
            {
                const std::lock_guard<std::mutex> lock(client.second.first);
                dtFreeNavMesh(client.second.second);
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
    void NewClient(int id, CLIENT_VERSION version) noexcept;

    /// <summary>
    /// Call this to free a client.
    /// </summary>
    /// <param name="id">Uinique id of the client.</param>
    void FreeClient(int id) noexcept;

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
    bool GetPath(int clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize) noexcept;

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
    bool GetRandomPath(int clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize, float maxRandomDistance) noexcept;

    /// <summary>
    /// Try to move towards a specific location.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="startPosition">Start position vector.</param>
    /// <param name="endPosition">End position vector.</param>
    /// <param name="positionToGoTo">Target position.</param>
    /// <returns>True when a path was found, false if not.</returns>
    bool MoveAlongSurface(int clientId, int mapId, const float* startPosition, const float* endPosition, float* positionToGoTo) noexcept;

    /// <summary>
    /// Get a random point anywhere on the map.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="position">Random position.</param>
    /// <returns>True when a point has been found, fals eif not.</returns>
    bool GetRandomPoint(int clientId, int mapId, float* position) noexcept;

    /// <summary>
    /// Get a random point within x meters of the start position.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="startPosition">Start position vector.</param>
    /// <param name="radius">Max distance to search for a random position.</param>
    /// <param name="position">Random position.</param>
    /// <returns>True when a point has been found, fals eif not.</returns>
    bool GetRandomPointAround(int clientId, int mapId, const float* startPosition, float radius, float* position) noexcept;

    /// <summary>
    /// Cast a movement ray along the mesh and see whether it collides with a wall or not.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="startPosition">Start position vector.</param>
    /// <param name="endPosition">End position vector.</param>
    /// <param name="raycastHit">Detour raycast result.</param>
    /// <returns>True when the ray hited no wall, false if it did.</returns>
    bool CastMovementRay(int clientId, int mapId, const float* startPosition, const float* endPosition, dtRaycastHit* raycastHit) noexcept;

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
    /// Load the MMAPs for a map.
    /// </summary>
    /// <param name="mapId">Id of the mmaps to load.</param>
    /// <returns>True if loaded, false if something went wrong.</returns>
    bool LoadMmaps(int mapId) noexcept;

private:
    /// <summary>
    /// Try to find the nearest poly for a given position.
    /// </summary>
    /// <param name="clientId">Id of the client to run this on.</param>
    /// <param name="mapId">The map id to search a path on.</param>
    /// <param name="position">Current position.</param>
    /// <param name="closestPointOnPoly">Closest point on the found poly.</param>
    /// <returns>Reference to the found poly if found, else 0.</returns>
    inline dtPolyRef GetNearestPoly(int clientId, int mapId, float* position, float* closestPointOnPoly) const noexcept
    {
        dtPolyRef polyRef;
        float extents[3] = { 6.0f, 6.0f, 6.0f };
        const auto& client = Clients.at(clientId);
        bool result = dtStatusSucceed(client->GetNavmeshQuery(mapId)->findNearestPoly(position, extents, &client->QueryFilter(), &polyRef, closestPointOnPoly));
        return result ? polyRef : 0;
    }

    /// <summary>
    /// Helper function to insert a vector3 into a float buffer.
    /// </summary>
    inline void InsertVector3(float* target, int& index, const float* vec, int offset) const noexcept
    {
        target[index] = vec[offset + 0];
        target[index + 1] = vec[offset + 1];
        target[index + 2] = vec[offset + 2];
        index += 3;
    }

    /// <summary>
    /// Helper function to scale two vectors and add them. 
    /// Used by the smoothing algorithms.
    /// </summary>
    inline void ScaleAndAddVector3(const float* vec0, float fac0, const float* vec1, float fac1, float* s0, float* s1, float* output) const noexcept
    {
        dtVscale(s0, vec0, fac0);
        dtVscale(s1, vec1, fac1);
        dtVadd(output, s0, s1);
    }

    /// <summary>
    /// Convert the recast and detour coordinates to wow coordinates.
    /// </summary>
    inline void RDToWowCoords(float* pos) const noexcept
    {
        float oz = pos[2];
        pos[2] = pos[1];
        pos[1] = pos[0];
        pos[0] = oz;
    }

    /// <summary>
    /// Convert the wow coordinates to recast and detour coordinates.
    /// </summary>
    inline void WowToRDCoords(float* pos) const noexcept
    {
        float ox = pos[0];
        pos[0] = pos[1];
        pos[1] = pos[2];
        pos[2] = ox;
    }

    /// <summary>
    /// Returns true when th mmaps are loaded for the given continent.
    /// </summary>
    inline bool IsMmapLoaded(int mapId) noexcept { return NavMeshMap[mapId].second != nullptr; }

    /// <summary>
    /// Returns true when the given client id is valid.
    /// </summary>
    inline bool IsValidClient(int clientId) noexcept { return Clients[clientId] != nullptr; }

    /// <summary>
    /// Initializes a NavMeshQuery for the given client.
    /// </summary>
    bool InitQueryAndLoadMmaps(int clientId, int mapId) noexcept;

    /// <summary>
    /// Used by the GetPath and GetRandomPath methods to generate a path.
    /// </summary>
    bool CalculateNormalPath(int clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize, dtPolyRef* visited = nullptr) noexcept;

    /// <summary>
    /// Used to detect the mmap file format.
    /// </summary>
    /// <returns>Mmap format detected or MMAP_FORMAT::UNKNOWN</returns>
    MMAP_FORMAT TryDetectMmapFormat() noexcept;
};
