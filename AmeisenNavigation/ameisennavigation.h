#ifndef _H_AMEISENNAVIGATION
#define _H_AMEISENNAVIGATION

#include <unordered_map>
#include <vector>

#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>

#include "Vector3.h"

#include "../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#ifdef _DEBUG
#define D(x) x
#else
#define D(x)
#endif

constexpr int MMAP_MAGIC = 0x4D4D4150;
constexpr int MMAP_VERSION = 12;

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
	int maxPolyPath;
	int maxPointPath;
	std::string mMmapFolder;
	dtQueryFilter mQueryFilter;
	std::unordered_map<int, dtNavMesh*> mNavMeshMap;
	std::unordered_map<int, dtNavMeshQuery*> mNavMeshQueryMap;

	Vector3 RDToWowCoords(const Vector3& pos);
	Vector3 WowToRDCoords(const Vector3& pos);
	bool PreparePathfinding(const int mapId);
	std::string FormatTrailingZeros(const int number, const int zeroCount);

public:
	AmeisenNavigation(const std::string& mmapFolder, int maxPolyPathLenght, int maxPointPathLenght);

	bool LoadMmapsForContinent(const int mapId);
	bool IsMmapLoaded(const int mapId);

	dtPolyRef GetNearestPoly(const int mapId, const Vector3& position, Vector3* closestPointOnPoly);
	bool GetPath(int mapId, const Vector3& startPosition, const Vector3& endPosition, Vector3* path, int* pathSize);
	bool MoveAlongSurface(const int mapId, const Vector3& startPosition, const Vector3& endPosition, Vector3* positionToGoTo);
	bool CastMovementRay(const int mapId, const Vector3& startPosition, const Vector3& endPosition);
	bool GetRandomPoint(const int mapId, Vector3* position);
	bool GetRandomPointAround(const int mapId, const Vector3& startPosition, float radius, Vector3* position);
};

#endif
