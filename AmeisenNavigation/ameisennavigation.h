#pragma once

#ifndef _H_AMEISENNAVIGATION
#define _H_AMEISENNAVIGATION

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <vector>
#include <chrono>

#include <windows.h>

#include "../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

constexpr int MMAP_MAGIC = 0x4d4d4150;
constexpr int MMAP_VERSION = 6;

constexpr int MAX_PATH_LENGHT = 1024;

#ifdef DEBUG 
#define D(x) x
#else 
#define D(x)
#endif

enum NavTerrain
{
	NAV_EMPTY = 0x00,
	NAV_GROUND = 0x01,
	NAV_MAGMA = 0x02,
	NAV_SLIME = 0x04,
	NAV_WATER = 0x08
};

struct MmapTileHeader {
	unsigned int mmapMagic;
	unsigned int dtVersion;
	unsigned int mmapVersion;
	unsigned int size;
	char usesLiquids;
	char padding[3];
};

class AmeisenNavigation {
private:
	std::string _mmap_dir;
	std::map<int, dtNavMesh*> _mesh_map;
	std::map<int, dtNavMeshQuery*> _query_map;

	dtQueryFilter _filter;

	std::string format_trailing_zeros(int number, int total_count);

	void RDToWowCoords(float pos[]);
	void WowToRDCoords(float pos[]);

public:
	AmeisenNavigation(std::string mmap_dir);

	void GetPath(int map_id, float* start, float* end, float** path, int* path_size);
	dtPolyRef GetNearestPoly(int map_id, float* pos, float* closest_point);

	bool LoadMmapsForContinent(int map_id);
	bool IsMapLoaded(int map_id);
};

#endif
