#pragma once

#ifndef _H_AMEISENNAVIGATION
#define _H_AMEISENNAVIGATION

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

#include <windows.h>

#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"


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

struct Path
{
	Vector3 nodes[];
};

class AmeisenNavigation {
public:
	Path GetPath(int map_id, Vector3 start_pos, Vector3 end_pos);
	void LoadMmapsForContinent(int map_id, std::string mmap_dir, dtNavMesh* mesh, dtNavMeshQuery* query);
};

#endif // !_H_AMEISENNAVIGATION
