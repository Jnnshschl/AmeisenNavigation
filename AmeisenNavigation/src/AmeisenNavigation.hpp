#ifndef _H_AMEISENNAVIGATION
#define _H_AMEISENNAVIGATION

#include <unordered_map>
#include <vector>

#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>

#include "Vector/Vector3.hpp"

#include "../../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"
#include "../../recastnavigation/Detour/Include/DetourCrowd.h"

#ifdef _DEBUG
#define DEBUG_ONLY(x) x
#else
#define DEBUG_ONLY(x)
#endif

constexpr int MMAP_MAGIC = 0x4D4D4150;
constexpr int MMAP_VERSION = 15;

constexpr float MAX_RAND_F = static_cast<float>(RAND_MAX);

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

    std::unordered_map<int, dtNavMesh*> NavMeshMap;
    std::unordered_map<int, dtNavMeshQuery*> NavMeshQueryMap;

    dtPolyRef* PolyPathBuffer;

public:
    AmeisenNavigation(const std::string& mmapFolder, int maxPolyPathLenght, int maxPointPathLenght)
        : MaxPolyPath(maxPolyPathLenght),
        MaxPointPath(maxPointPathLenght),
        MmapFolder(mmapFolder),
        QueryFilter(),
        NavMeshMap(),
        NavMeshQueryMap(),
        PolyPathBuffer(new dtPolyRef[maxPolyPathLenght])
    {
        QueryFilter.setIncludeFlags(NAV_GROUND | NAV_WATER);
        QueryFilter.setExcludeFlags(NAV_EMPTY | NAV_GROUND_STEEP | NAV_MAGMA_SLIME);

        // seed the random generator
        srand(static_cast<unsigned>(time(0)));
    }

    ~AmeisenNavigation()
    {
        delete[] PolyPathBuffer;
    }

    bool LoadMmapsForContinent(int mapId) noexcept;
    dtPolyRef GetNearestPoly(int mapId, const Vector3& position, Vector3* closestPointOnPoly) noexcept;
    bool GetPath(int mapId, Vector3 startPosition, Vector3 endPosition, Vector3* path, int* pathSize) noexcept;
    bool MoveAlongSurface(int mapId, Vector3 startPosition, Vector3 endPosition, Vector3* positionToGoTo) noexcept;
    bool CastMovementRay(int mapId, Vector3 startPosition, Vector3 endPosition, dtRaycastHit* raycastHit) noexcept;
    bool GetRandomPoint(int mapId, Vector3* position) noexcept;
    bool GetRandomPointAround(int mapId, Vector3 startPosition, float radius, Vector3* position) noexcept;

    inline bool IsMmapLoaded(int mapId) noexcept
    {
        return NavMeshMap[mapId] != nullptr
            && NavMeshQueryMap[mapId] != nullptr;
    }

private:
    constexpr void RDToWowCoords(Vector3* pos) noexcept
    {
        float oz = pos->z;
        pos->z = pos->y;
        pos->y = pos->x;
        pos->x = oz;
    }

    constexpr void WowToRDCoords(Vector3* pos) noexcept
    {
        float ox = pos->x;
        pos->x = pos->y;
        pos->y = pos->z;
        pos->z = ox;
    }

    inline bool LoadMapIfNeeded(int mapId) noexcept
    {
        return IsMmapLoaded(mapId) || LoadMmapsForContinent(mapId);
    }
};

#endif