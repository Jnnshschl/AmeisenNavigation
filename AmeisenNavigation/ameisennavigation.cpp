#include "ameisennavigation.h"

AmeisenNavigation::AmeisenNavigation(std::string mmap_dir) {
	_mmap_dir = mmap_dir;
}

std::string AmeisenNavigation::format_trailing_zeros(int number, int total_count) {
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

void AmeisenNavigation::GetPath(int map_id, float* start, float* end, float** path, int* path_size)
{
	WoWToRDCoords(start);
	WoWToRDCoords(end);

	//// std::cout << "-> Generating Path (" << start[0] << "|" << start[1] << "|" << start[2] << ") -> (" << end[0] << "|" << end[1] << "|" << end[2] << ")\n";

	if (_meshmap[map_id] == nullptr
		|| _querymap[map_id] == nullptr)
	{
		std::cout << "-> Mesh for Continent " << map_id << " not loaded, loading it now\n\n";
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

	//// std::cout << "-> Start Poly " << start_poly << " found " << closest_point_start[0] << " " << closest_point_start[1] << " " << closest_point_start[2] << "\n";
	//// std::cout << "-> End Poly " << end_poly << " found " << closest_point_end[0] << " " << closest_point_end[1] << " " << closest_point_end[2] << " \n";

	dtPolyRef path_poly[1024];

	float hit = 0;
	float hit_normal[3];
	memset(hit_normal, 0, sizeof(hit_normal));

	_querymap[map_id]->raycast(start_poly, start, end, &_filter, &hit, hit_normal, path_poly, path_size, 1024);
	//// std::cout << "-> RaycastPath contains " << path_size << " Nodes\n";

	// check if we hit something
	if (hit != FLT_MAX)
	{
		std::cout << "-> Raycast hit something\n";
		if (!dtStatusSucceed(_querymap[map_id]->findPath(start_poly, end_poly, start, end, &_filter, path_poly, path_size, 1024)))
		{
			std::cout << "-> Path not found\n";
			return;
		}
		else 
		{
			// if the ray hits something our target is  not in a
			// straight line so we need to build a path to it
			std::cout << "-> Path found: " << (*path_size) << " Nodes\n";

			float path_a[1024 * 3];

			for (int i = 0; i < (*path_size); i++)
			{
				float closest_pos[3];
				bool pos_over_poly;
				_querymap[map_id]->closestPointOnPoly(path_poly[i], start, closest_pos, &pos_over_poly);

				float* closest_pos_wow = closest_pos;
				RDToWoWCoords(closest_pos_wow);

				//// std::cout << "-> Node " << i << " X: " << closest_pos_wow[0] << " Y: " << closest_pos_wow[1] << " Z: " << closest_pos_wow[2] << "\n";

				path_a[i * 3] = closest_pos_wow[0];
				path_a[i * 3 + 1] = closest_pos_wow[1];
				path_a[i * 3 + 2] = closest_pos_wow[2];
			}

			(*path) = path_a;
		}
	}
	else
	{
		// if the ray hits nothing our target is in a
		// straight line so only return the final pos
		//// std::cout << "-> Raycast hit nothing\n";
		RDToWoWCoords(end);
		(*path_size) = 1;
		(*path) = end;
	}

}

dtPolyRef AmeisenNavigation::GetNearestPoly(int map_id, float* pos, float* closest_point)
{
	dtPolyRef poly_ref;
	float extents[3] = { 8.0f, 8.0f, 8.0f };

	_querymap[map_id]->findNearestPoly(pos, extents, &_filter, &poly_ref, closest_point);

	return poly_ref;
}

bool AmeisenNavigation::LoadMmapsForContinent(int map_id)
{
	if (_loadingmap[map_id])
		return false;

	_loadingmap[map_id] = true;

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
		std::cout << "-> Error: could not read mmap file\n";
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
			mmaptile_stream.open(mmaptile_filename, std::ifstream::binary);

			MmapTileHeader fileHeader;
			mmaptile_stream.read((char*)&fileHeader, sizeof(fileHeader));

			unsigned char* data = (unsigned char*)dtAlloc(fileHeader.size, DT_ALLOC_PERM);
			mmaptile_stream.read((char*)data, fileHeader.size);

			dtTileRef tileRef = 0;

			if (!dtStatusSucceed(_meshmap[map_id]->addTile(data, fileHeader.size, DT_TILE_FREE_DATA, 0, &tileRef)))
			{
				std::cout << "-> Error at adding tile " << x << " " << y << " to navmesh\n";
			}

			//mmaptile_stream.close();
		}
	}

	_querymap[map_id] = dtAllocNavMeshQuery();
	if (dtStatusFailed(_querymap[map_id]->init(_meshmap[map_id], 2048)))
	{
		std::cout << "-> Failed to built NavMeshQuery " << "\n";
		dtFreeNavMeshQuery(_querymap[map_id]);
		return false;
	}
	else
	{
		return true;
	}
}