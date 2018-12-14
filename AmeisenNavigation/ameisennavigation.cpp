#include "ameisennavigation.h"


std::string AmeisenNavigation::format_trailing_zeros(int number, int total_count) {
	std::stringstream ss;
	ss << std::setw(total_count) << std::setfill('0') << number;
	return ss.str();
}

AmeisenNavigation::AmeisenNavigation(std::string mmap_dir)
{
	_mmap_dir = mmap_dir;

	short includeFlags = 0;
	includeFlags |= (NAV_GROUND | NAV_WATER);

	_filter.setIncludeFlags(includeFlags);
	_filter.setExcludeFlags(0);
}

std::vector<Vector3> AmeisenNavigation::GetPath(int map_id, float* start, float* end)
{
	std::cout << "-> Generating Path (" << start[0] << "|" << start[1] << "|" << start[2]
		<< ") -> (" << end[0] << "|" << end[1] << "|" << end[2] << ")\n";

	int path_size = 0;

	float extents[3] = { 8.0f, 8.0f, 8.0f };
	float closest_point[3] = { 0.0f, 0.0f, 0.0f };

	dtPolyRef start_poly;
	dtPolyRef end_poly;
	dtPolyRef path_poly;

	std::vector<Vector3> path;

	if (_meshmap[map_id] == nullptr
		|| _querymap[map_id] == nullptr)
	{
		std::cout << "--> Mesh for Continent " << map_id << " not loaded, loading it now\n";
		if (!LoadMmapsForContinent(map_id))
		{
			std::cout << "--> Mesh or Query could not be loaded\n";
			return path;
		}
	}

	if (dtStatusSucceed(_querymap[map_id]->findNearestPoly(start, extents, &_filter, &start_poly, closest_point)))
	{
		std::cout << "--> Start Poly " << start_poly << " found " << closest_point[0] << " " << closest_point[1] << " " << closest_point[2] << "\n";
	}
	else
	{
		std::cout << "--> Start Poly not found\n";
	}

	if (dtStatusSucceed(_querymap[map_id]->findNearestPoly(end, extents, &_filter, &end_poly, closest_point)))
	{
		std::cout << "--> End Poly " << end_poly << " found " << closest_point[0] << " " << closest_point[1] << " " << closest_point[2] << " \n";
	}
	else
	{
		std::cout << "--> End Poly not found\n";
	}

	if (dtStatusSucceed(_querymap[map_id]->findPath(start_poly, end_poly, start, end, &_filter, &path_poly, &path_size, 1024)))
	{
		std::cout << "--> Path found and contains " << path_size << " Nodes\n";
	}
	else
	{
		std::cout << "--> Path not found\n";
	}

	return path;
}

bool AmeisenNavigation::LoadMmapsForContinent(int map_id)
{
	std::string mmap_filename = _mmap_dir + format_trailing_zeros(map_id, 3) + ".mmap";
	std::ifstream mmap_stream;
	dtNavMeshParams params;

	_meshmap[map_id] = dtAllocNavMesh();

	mmap_stream.open(mmap_filename);
	mmap_stream.read((char*)&params, sizeof(params));
	mmap_stream.close();

	if (dtStatusFailed(_meshmap[map_id]->init(&params)))
	{
		dtFreeNavMesh(_meshmap[map_id]);
		std::cout << "--> Error: could not read mmap file\n";
		return false;
	}

	for (int x = 1; x <= 64; x++)
	{
		for (int y = 1; y <= 64; y++)
		{
			std::string mmaptile_filename =
				_mmap_dir
				+ format_trailing_zeros(map_id, 3)
				+ format_trailing_zeros(x, 2)
				+ format_trailing_zeros(y, 2)
				+ ".mmtile";

			GetFileAttributes(mmaptile_filename.c_str());
			if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(mmaptile_filename.c_str()) && GetLastError() == ERROR_FILE_NOT_FOUND)
			{
				continue;
			}

			//std::cout << "--> Reading Tile " << mmaptile_filename.c_str() << "\n";

			std::ifstream mmaptile_stream;
			mmaptile_stream.open(mmaptile_filename, std::ios::binary);

			MmapTileHeader fileHeader;
			mmaptile_stream.read((char*)&fileHeader, sizeof(fileHeader));
			std::cout << "--> Header Size: " << fileHeader.size << " usesLiquids: " << (int)fileHeader.usesLiquids << "\n";

			unsigned char* data = new unsigned char[fileHeader.size];
			mmaptile_stream.read((char*)data, fileHeader.size);

			dtMeshHeader* header = (dtMeshHeader*)data;
			dtTileRef tileRef;

			if (!dtStatusSucceed(_meshmap[map_id]->addTile(data, fileHeader.size, 0, 0, &tileRef)))
			{
				std::cout << "--> Error at adding tile " << x << " " << y << " to navmesh\n";
			}
			else
			{
				std::cout << "--> tileRef: " << tileRef << "\n";
			}

			mmaptile_stream.close();
		}
	}

	_querymap[map_id] = dtAllocNavMeshQuery();
	if (dtStatusFailed(_querymap[map_id]->init(_meshmap[map_id], 1024)))
	{
		std::cout << "--> Failed to built NavMeshQuery " << "\n";
		dtFreeNavMeshQuery(_querymap[map_id]);
		return false;
	}
	else
	{
		std::cout << "--> Sucessfully built NavMeshQuery " << "\n";
		return true;
	}
}