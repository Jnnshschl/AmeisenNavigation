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

#ifdef DEBUG
#define D(x) x
#else
#define D(x)
#endif

constexpr int MMAP_MAGIC = 0x4D4D4150;
constexpr int MMAP_VERSION = 10;
constexpr int MAX_PATH_LENGHT = 1024;

enum NavTerrain
{
	NAV_EMPTY = 0,
	NAV_MAGMA_SLIME = 61,
	NAV_WATER = 62,
	NAV_GROUND = 63
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
	std::string m_MmapFolder;
	dtQueryFilter m_QueryFilter;
	std::unordered_map<int, dtNavMesh*> m_NavMeshMap;
	std::unordered_map<int, dtNavMeshQuery*> m_NavMeshQueryMap;

	Vector3 RDToWowCoords(const Vector3& pos);
	Vector3 WowToRDCoords(const Vector3& pos);
	bool PreparePathfinding(const int mapId, int* pathSize);
	std::string FormatTrailingZeros(const int number, const int zeroCount);

public:
	AmeisenNavigation(const std::string& mmapFolder);

	bool LoadMmapsForContinent(const int mapId);
	bool IsMmapLoaded(const int mapId);

	dtPolyRef GetNearestPoly(const int mapId, const Vector3& position, Vector3* closestPointOnPoly);
	bool GetPath(int mapId, const Vector3& startPosition, const Vector3& endPosition, Vector3* path, int* pathSize);
	bool MoveAlongSurface(const int mapId, const Vector3& startPosition, const Vector3& endPosition, Vector3* positionToGoTo);
	bool CastMovementRay(const int mapId, const Vector3& startPosition, const Vector3& endPosition);
};

#endif
