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

void AmeisenNavigation::RDToWowCoords(float pos[])
{
	float orig_x = pos[0];
	float orig_y = pos[1];
	float orig_z = pos[2];

	pos[0] = orig_z;
	pos[1] = orig_x;
	pos[2] = orig_y;
}

void AmeisenNavigation::WowToRDCoords(float pos[])
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

void AmeisenNavigation::GetPath(int map_id, float* start, float* end, float** path, int* path_size)
{
	WowToRDCoords(start);
	WowToRDCoords(end);

	D(std::cout << ">> Generating Path (" << start[0] << "|" << start[1] << "|" << start[2] << ") >> (" << end[0] << "|" << end[1] << "|" << end[2] << ")\n";)

	if (!IsMapLoaded(map_id))
	{
		D(std::cout << ">> Mesh for Continent " << map_id << " not loaded, loading it now\n\n";)

		if (!LoadMmapsForContinent(map_id))
		{
			D(std::cerr << ">> Mesh or Query could not be loaded\n";)
			return;
		}
	}

	float closest_point_start[3];
	dtPolyRef start_poly = GetNearestPoly(map_id, start, closest_point_start);

	float closest_point_end[3];
	dtPolyRef end_poly = GetNearestPoly(map_id, end, closest_point_end);

	if (start_poly == end_poly) 
	{
		// same poly, we don't need pathfinding here
		D(std::cout << ">> Start and End positions are on the same poly, returning end position\n";)
		(*path_size) = 1;
		(*path) = end;
		return;
	}

	dtPolyRef polypath[MAX_PATH_LENGHT];
	int polypath_size = 0;
	if(dtStatusFailed(_query_map[map_id]->findPath(start_poly, end_poly, closest_point_start, closest_point_end, &_filter, polypath, &polypath_size, MAX_PATH_LENGHT)))
	{
		// pathfinding failed
		D(std::cerr << ">> No PolyPath found...\n";)
		(*path_size) = 0;
		return;
	}

	float pointpath[MAX_PATH_LENGHT * 3];
	int pointpath_size = 0;
	if (dtStatusFailed(_query_map[map_id]->findStraightPath(closest_point_start, closest_point_end, polypath, polypath_size, pointpath, nullptr, nullptr, &pointpath_size, MAX_PATH_LENGHT)))
	{
		// pathfinding failed
		D(std::cerr << ">> Failed to calculate PointPath...\n";)
		(*path_size) = 0;
		return;
	}

	// convert to Recast and Detour coordinates to Wow coordinates
	for (int i = 0; i < pointpath_size; i += 3)
	{
		RDToWowCoords(&pointpath[i]);
	}

	(*path_size) = pointpath_size;
	(*path) = pointpath;
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
	// build the *.mmap filename (example: 001.mmap or 587.mmap)
	std::string mmap_filename = _mmap_dir + format_trailing_zeros(map_id, 3) + ".mmap";

	dtNavMeshParams params;

	// read the dtNavMeshParams
	std::ifstream mmap_stream;
	mmap_stream.open(mmap_filename, std::ifstream::binary);
	mmap_stream.read((char*)&params, sizeof(params));
	mmap_stream.close();

	// allocate and init the NavMesh
	_mesh_map[map_id] = dtAllocNavMesh();
	if (dtStatusFailed(_mesh_map[map_id]->init(&params)))
	{
		dtFreeNavMesh(_mesh_map[map_id]);
		D(std::cerr << ">> Could not init map navmesh file: " << mmap_filename << "\n";)
		return false;
	}

	// load every NavMesh Tile from 1, 1 to 64, 64
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

			// we can't load non existent files
			GetFileAttributes(mmaptile_filename.c_str());
			if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(mmaptile_filename.c_str()) 
				&& GetLastError() == ERROR_FILE_NOT_FOUND)
			{
				continue;
			}

			D(std::cout << ">> Reading Tile " << mmaptile_filename.c_str() << "\n";)

			std::ifstream mmaptile_stream;
			mmaptile_stream.open(mmaptile_filename, std::ifstream::binary);

			// read the mmap header
			MmapTileHeader fileHeader;
			mmaptile_stream.read((char*)&fileHeader, sizeof(fileHeader));

			// read the NavMesh Tile data
			unsigned char* data = (unsigned char*)dtAlloc(fileHeader.size, DT_ALLOC_PERM);
			mmaptile_stream.read((char*)data, fileHeader.size);

			// add the Tile to our NavMesh
			dtTileRef tileRef;
			if (!dtStatusSucceed(_mesh_map[map_id]->addTile(data, fileHeader.size, DT_TILE_FREE_DATA, 0, &tileRef)))
			{
				D(std::cerr << ">> Failed adding tile " << x << " " << y << " to navmesh\n";)
			}

			mmaptile_stream.close();
		}
	}

	// init the NavMeshQuery
	_query_map[map_id] = dtAllocNavMeshQuery();
	if (dtStatusFailed(_query_map[map_id]->init(_mesh_map[map_id], MAX_PATH_LENGHT)))
	{
		D(std::cerr << ">> Failed to init NavMeshQuery " << "\n";)
		dtFreeNavMeshQuery(_query_map[map_id]);
		return false;
	}
	else
	{
		return true;
	}
}