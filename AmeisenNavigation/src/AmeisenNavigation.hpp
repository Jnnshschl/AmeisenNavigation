#ifndef _H_AMEISENNAVIGATION
#define _H_AMEISENNAVIGATION

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

enum class SmoothingAlgorithm
{
    NONE = 0,
    CHAIKIN_CURVE = 1,
    CATMULL_ROM_SPLINE = 2
};

enum NavArea
{
    NAV_AREA_EMPTY = 0,
    NAV_AREA_GROUND = 11,
    NAV_AREA_GROUND_STEEP = 10,
    NAV_AREA_WATER = 9,
    NAV_AREA_MAGMA_SLIME = 8,
};

enum NavTerrainFlag
{
    NAV_EMPTY = 0x00,
    NAV_GROUND = 1 << (NAV_AREA_GROUND - NAV_AREA_GROUND),
    NAV_GROUND_STEEP = 1 << (NAV_AREA_GROUND - NAV_AREA_GROUND_STEEP),
    NAV_WATER = 1 << (NAV_AREA_GROUND - NAV_AREA_WATER),
    NAV_MAGMA_SLIME = 1 << (NAV_AREA_GROUND - NAV_AREA_MAGMA_SLIME)
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
    int MaxPolyPath;
    int MaxPointPath;
    std::string MmapFolder;
    dtQueryFilter QueryFilter;

    std::unordered_map<int, std::mutex> MapLoadingLocks;
    std::unordered_map<int, dtNavMesh*> NavMeshMap;
    std::unordered_map<int, std::unordered_map<int, dtNavMeshQuery*>*> NavMeshQueryMap;
    std::unordered_map<int, dtPolyRef*> PolyPathBuffers;

public:
    AmeisenNavigation(const std::string& mmapFolder, int maxPolyPathLenght, int maxPointPathLenght)
        : MaxPolyPath(maxPolyPathLenght),
        MaxPointPath(maxPointPathLenght * 3),
        MmapFolder(mmapFolder),
        QueryFilter(),
        MapLoadingLocks(),
        NavMeshMap(),
        NavMeshQueryMap(),
        PolyPathBuffers()
    {
        QueryFilter.setIncludeFlags(NAV_GROUND | NAV_WATER);
        QueryFilter.setExcludeFlags(NAV_EMPTY | NAV_GROUND_STEEP | NAV_MAGMA_SLIME);

        // seed the random generator
        srand(static_cast<unsigned int>(time(0)));
    }

    ~AmeisenNavigation()
    {
        for (const auto& polyPathBuffer : PolyPathBuffers)
        {
            if (polyPathBuffer.second)
            {
                delete[] polyPathBuffer.second;
            }
        }

        for (auto& client : NavMeshQueryMap)
        {
            if (client.second)
            {
                for (auto& query : client.second[client.first])
                {
                    if (query.second)
                    {
                        dtFreeNavMeshQuery(query.second);
                    }
                }

                delete client.second;
            }
        }

        for (auto& client : NavMeshMap)
        {
            if (client.second)
            {
                dtFreeNavMesh(client.second);
            }
        }
    }

    AmeisenNavigation(const AmeisenNavigation&) = delete;
    AmeisenNavigation& operator=(const AmeisenNavigation&) = delete;

    void NewClient(int id) noexcept;
    void FreeClient(int id) noexcept;

    bool GetPath(int clientId, int mapId, const float* startPosition, const float* endPosition, float* path, int* pathSize) noexcept;
    bool MoveAlongSurface(int clientId, int mapId, const float* startPosition, const float* endPosition, float* positionToGoTo) noexcept;
    bool GetRandomPoint(int clientId, int mapId, float* position) noexcept;
    bool GetRandomPointAround(int clientId, int mapId, const float* startPosition, float radius, float* position) noexcept;
    bool CastMovementRay(int clientId, int mapId, const float* startPosition, const float* endPosition, dtRaycastHit* raycastHit) noexcept;

    void SmoothPathChaikinCurve(const float* input, int inputSize, float* output, int* outputSize) noexcept;
    void SmoothPathCatmullRom(const float* input, int inputSize, float* output, int* outputSize, int points, float alpha) noexcept;

    bool LoadMmaps(int mapId) noexcept;
    bool InitQuery(int clientId, int mapId) noexcept;

private:
    inline dtPolyRef GetNearestPoly(int clientId, int mapId, float* position, float* closestPointOnPoly) noexcept
    {
        dtPolyRef polyRef;
        float extents[3] = { 6.0f, 6.0f, 6.0f };
        bool result = dtStatusSucceed((*NavMeshQueryMap[clientId])[mapId]->findNearestPoly(position, extents, &QueryFilter, &polyRef, closestPointOnPoly));
        return result ? polyRef : 0;
    }

    inline void InsertVector3(float* target, int& index, const float* vec, int offset) const noexcept
    {
        target[index] = vec[offset + 0];
        target[index + 1] = vec[offset + 1];
        target[index + 2] = vec[offset + 2];
        index += 3;
    }

    inline void ScaleAndAddVector3(const float* vec0, float fac0, const float* vec1, float fac1, float* s0, float* s1, float* output) const noexcept
    {
        dtVscale(s0, vec0, fac0);
        dtVscale(s1, vec1, fac1);
        dtVadd(output, s0, s1);
    }

    inline void RDToWowCoords(float* pos) const noexcept
    {
        float oz = pos[2];
        pos[2] = pos[1];
        pos[1] = pos[0];
        pos[0] = oz;
    }

    inline void WowToRDCoords(float* pos) const noexcept
    {
        float ox = pos[0];
        pos[0] = pos[1];
        pos[1] = pos[2];
        pos[2] = ox;
    }

    inline bool IsMmapLoaded(int mapId) noexcept
    {
        return NavMeshMap[mapId];
    }

    inline bool IsValidClient(int clientId) noexcept
    {
        return NavMeshQueryMap[clientId] && PolyPathBuffers[clientId];
    }
};

#endif