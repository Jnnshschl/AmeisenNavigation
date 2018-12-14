#pragma once

#ifndef _H_AMEISENNAVIGATION
#define _H_AMEISENNAVIGATION

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <vector>

#include <windows.h>

#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

constexpr int MMAP_MAGIC = 0x4d4d4150;
constexpr int MMAP_VERSION = 6;

enum NavTerrain
{
	NAV_EMPTY = 0x00,
	NAV_GROUND = 0x01,
	NAV_MAGMA = 0x02,
	NAV_SLIME = 0x04,
	NAV_WATER = 0x08,
	NAV_UNUSED1 = 0x10,
	NAV_UNUSED2 = 0x20,
	NAV_UNUSED3 = 0x40,
	NAV_UNUSED4 = 0x80
};

struct MmapTileHeader {
	unsigned int mmapMagic;
	unsigned int dtVersion;
	unsigned int mmapVersion;
	unsigned int size;
	char usesLiquids;
	char padding[3];
};

struct Vector3
{
	float x;
	float y;
	float z;
};

class AmeisenNavigation {
private:
	std::string _mmap_dir;
	std::map<int, dtNavMesh*> _meshmap;
	std::map<int, dtNavMeshQuery*> _querymap;
	dtQueryFilter _filter;

	std::string format_trailing_zeros(int number, int total_count);

public:
	AmeisenNavigation(std::string mmap_dir);
	std::vector<Vector3> GetPath(int map_id, float* start, float* end);
	bool LoadMmapsForContinent(int map_id);
};

#endif // !_H_AMEISENNAVIGATION
