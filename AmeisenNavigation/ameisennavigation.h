#ifndef _H_AMEISENNAVIGATION
#define _H_AMEISENNAVIGATION

#include <map>
#include <vector>

#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>

#include "../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#ifdef DEBUG
#define D(x) x
#else
#define D(x)
#endif

constexpr int MMAP_MAGIC = 0x4D4D4150;
constexpr int MMAP_VERSION = 6;
constexpr int MAX_PATH_LENGHT = 1024;

enum NavTerrain
{
	NAV_EMPTY = 0x00,
	NAV_GROUND = 0x01,
	NAV_MAGMA = 0x02,
	NAV_SLIME = 0x04,
	NAV_WATER = 0x08
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
	std::map<int, dtNavMesh*> m_NavMeshMap;
	std::map<int, dtNavMeshQuery*> m_NavMeshQueryMap;

	void RDToWowCoords(float* pos);
	void WowToRDCoords(float* pos);
	bool PreparePathfinding(int mapId, int* pathSize);
	std::string FormatTrailingZeros(int number, int zeroCount);

public:
	AmeisenNavigation(std::string mmapFolder);

	bool LoadMmapsForContinent(int mapId);
	bool IsMmapLoaded(int mapId);

	dtPolyRef GetNearestPoly(int mapId, float* position, float* closestPointOnPoly);
	bool GetPath(int mapId, float* startPosition, float* endPosition, float* path, int* pathSize);
	bool MoveAlongSurface(int mapId, float* startPosition, float* endPosition, float* positionToGoTo);
	bool CastMovementRay(int mapId, float* startPosition, float* endPosition);
};

#endif
