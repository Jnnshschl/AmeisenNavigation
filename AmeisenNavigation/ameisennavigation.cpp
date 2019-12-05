#include "ameisennavigation.h"

AmeisenNavigation::AmeisenNavigation(std::string mmap_dir)
{
	_mmap_dir = mmap_dir;
}

std::string AmeisenNavigation::format_trailing_zeros(int number, int total_count)
{
	std::stringstream ss;
	ss << std::setw(total_count) << std::setfill('0') << number;
	return ss.str();
}

void AmeisenNavigation::RDToWoWCoords(float pos[])
{
	float orig_x = pos[0];
	float orig_y = pos[1];
	float orig_z = pos[2];

	pos[0] = orig_z;
	pos[1] = orig_x;
	pos[2] = orig_y;
}

void AmeisenNavigation::WoWToRDCoords(float pos[])
{
	float orig_x = pos[0];
	float orig_y = pos[1];
	float orig_z = pos[2];

	pos[0] = orig_y;
	pos[1] = orig_z;
	pos[2] = orig_x;
}

bool AmeisenNavigation::IsMapLoaded(int map_id)
{
	return _mesh_map[map_id] != nullptr
		&& _query_map[map_id] != nullptr;
}

float random_float()
{
	return static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
}

void AmeisenNavigation::GetPath(int map_id, float* start, float* end, float** path, int* path_size)
{
	WoWToRDCoords(start);
	WoWToRDCoords(end);

#ifdef DEBUG
	std::cout << "-> Generating Path (" << start[0] << "|" << start[1] << "|" << start[2] << ") -> (" << end[0] << "|" << end[1] << "|" << end[2] << ")\n";
#endif

	if (!IsMapLoaded(map_id))
	{

#ifdef DEBUG
		std::cout << "-> Mesh for Continent " << map_id << " not loaded, loading it now\n\n";
#endif		

		if (!LoadMmapsForContinent(map_id))
		{
			std::cout << "-> Mesh or Query could not be loaded\n";
			return;
		}
	}

	float closest_point_start[3];
	float closest_point_end[3];

	dtPolyRef start_poly = GetNearestPoly(map_id, start, closest_point_start);
	dtPolyRef end_poly = GetNearestPoly(map_id, end, closest_point_end);

#ifdef DEBUG
	std::cout << "-> Start Poly " << start_poly << " found " << closest_point_start[0] << " " << closest_point_start[1] << " " << closest_point_start[2] << "\n";
	std::cout << "-> End Poly " << end_poly << " found " << closest_point_end[0] << " " << closest_point_end[1] << " " << closest_point_end[2] << " \n";
#endif

	// Raycast stuff, currently disabled. 
	// Need to find a sweetspot to trust the raycast
	// because if we use it across  ahole it may give
	// wrong results.

	//// float hit = 0;
	//// float hit_normal[3];
	//// memset(hit_normal, 0, sizeof(hit_normal));
	//// _query_map[map_id]->raycast(start_poly, start, end, &_filter, &hit, hit_normal, path_poly, path_size, 1024);

	dtPolyRef path_poly[1024];
	if (!dtStatusSucceed(_query_map[map_id]->findPath(start_poly, end_poly, start, end, &_filter, path_poly, path_size, MAX_PATH_LENGHT)))
	{
#ifdef DEBUG
		std::cout << "-> Path not found\n";
#endif
		return;
	}
	else
	{
#ifdef DEBUG
		std::cout << "-> Path found: " << (*path_size) << " Nodes\n";
#endif

		float path_a[MAX_PATH_LENGHT * 3];

		dtPolyRef visited_polys[MAX_PATH_LENGHT];
		float result_pos[3];

		result_pos[0] = end[0];
		result_pos[1] = end[1];
		result_pos[2] = end[2];

		for (int i = 0; i < (*path_size); ++i)
		{
			float start_poly_pos[3];
			_query_map[map_id]->closestPointOnPolyBoundary(path_poly[i], result_pos, result_pos);

			RDToWoWCoords(result_pos);

			path_a[i * 3] = result_pos[0];
			path_a[i * 3 + 1] = result_pos[1];
			path_a[i * 3 + 2] = result_pos[2];
		}

		(*path) = path_a;
	}
}

dtPolyRef AmeisenNavigation::GetNearestPoly(int map_id, float* pos, float* closest_point)
{
	dtPolyRef poly_ref;
	float extents[3] = { 8.0f, 8.0f, 8.0f };

	_query_map[map_id]->findNearestPoly(pos, extents, &_filter, &poly_ref, closest_point);

	return poly_ref;
}

bool AmeisenNavigation::LoadMmapsForContinent(int map_id)
{
	std::string mmap_filename = _mmap_dir + format_trailing_zeros(map_id, 3) + ".mmap";
	std::ifstream mmap_stream;
	dtNavMeshParams params;

	_mesh_map[map_id] = dtAllocNavMesh();

	mmap_stream.open(mmap_filename);
	mmap_stream.read((char*)&params, sizeof(params));
	mmap_stream.close();

	if (dtStatusFailed(_mesh_map[map_id]->init(&params)))
	{
		dtFreeNavMesh(_mesh_map[map_id]);
		std::cout << "-> Error: could not init map navmesh, file: " << mmap_filename << "\n";
		return false;
	}

	for (int x = 1; x <= 64; ++x)
	{
		for (int y = 1; y <= 64; ++y)
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

#ifdef DEBUG
			std::cout << "-> Reading Tile " << mmaptile_filename.c_str() << "\n";
#endif

			std::ifstream mmaptile_stream;
			mmaptile_stream.open(mmaptile_filename, std::ifstream::binary);

			MmapTileHeader fileHeader;
			mmaptile_stream.read((char*)&fileHeader, sizeof(fileHeader));

			unsigned char* data = (unsigned char*)dtAlloc(fileHeader.size, DT_ALLOC_PERM);
			mmaptile_stream.read((char*)data, fileHeader.size);

			dtTileRef tileRef = 0;

			if (!dtStatusSucceed(_mesh_map[map_id]->addTile(data, fileHeader.size, DT_TILE_FREE_DATA, 0, &tileRef)))
			{
				std::cout << "-> Error at adding tile " << x << " " << y << " to navmesh\n";
			}

			mmaptile_stream.close();
		}
	}

	_query_map[map_id] = dtAllocNavMeshQuery();
	if (dtStatusFailed(_query_map[map_id]->init(_mesh_map[map_id], MAX_PATH_LENGHT)))
	{
		std::cout << "-> Failed to built NavMeshQuery " << "\n";
		dtFreeNavMeshQuery(_query_map[map_id]);
		return false;
	}
	else
	{
		return true;
	}
}