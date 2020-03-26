#include "ameisennavigation.h"

AmeisenNavigation::AmeisenNavigation(std::string mmapFolder)
{
	m_MmapFolder = mmapFolder;
}

std::string AmeisenNavigation::FormatTrailingZeros(int number, int zeroCount)
{
	std::stringstream ss;
	ss << std::setw(zeroCount) << std::setfill('0') << number;
	return ss.str();
}

void AmeisenNavigation::RDToWowCoords(float* pos)
{
	float orig_x = pos[0];
	float orig_y = pos[1];
	float orig_z = pos[2];

	pos[0] = orig_z;
	pos[1] = orig_x;
	pos[2] = orig_y;
}

void AmeisenNavigation::WowToRDCoords(float* pos)
{
	float orig_x = pos[0];
	float orig_y = pos[1];
	float orig_z = pos[2];

	pos[0] = orig_y;
	pos[1] = orig_z;
	pos[2] = orig_x;
}

bool AmeisenNavigation::IsMmapLoaded(int map_id)
{
	return m_NavMeshMap[map_id] != nullptr
		&& m_NavMeshQueryMap[map_id] != nullptr;
}

bool AmeisenNavigation::GetPath(int mapId, float* startPosition, float* endPosition, float* path, int* pathSize)
{
	D(std::cout << ">> Generating Path (" << startPosition[0] << ", " << startPosition[1] << ", " << startPosition[2] << ") -> (" << endPosition[0] << ", " << endPosition[1] << ", " << endPosition[2] << ")" << std::endl);

	WowToRDCoords(startPosition);
	WowToRDCoords(endPosition);

	if (!IsMmapLoaded(mapId))
	{
		D(std::cout << ">> Mesh for Continent " << mapId << " not loaded, loading it now" << std::endl);

		if (!LoadMmapsForContinent(mapId))
		{
			std::cerr << ">> Unable to load MMAPS for continent " << mapId << std::endl;

			*pathSize = 0;
			return false;
		}
	}

	float* closestPointStart = new float[3];
	float* closestPointEnd = new float[3];

	dtPolyRef startPoly = GetNearestPoly(mapId, startPosition, closestPointStart);
	dtPolyRef endPoly = GetNearestPoly(mapId, endPosition, closestPointEnd);

	if (startPoly == endPoly)
	{
		// same poly, we don't need pathfinding here
		D(std::cout << ">> Start and End positions are on the same poly, returning end position" << std::endl);
		RDToWowCoords(endPosition);

		*pathSize = 1;
		path[0] = endPosition[0];
		path[1] = endPosition[1];
		path[2] = endPosition[2];
		return true;
	}
	else
	{
		dtPolyRef* polypath = new dtPolyRef[MAX_PATH_LENGHT];
		int polypathSize = 0;

		if (dtStatusSucceed(m_NavMeshQueryMap[mapId]->findPath(startPoly, endPoly, closestPointStart, closestPointEnd, &m_QueryFilter, polypath, &polypathSize, MAX_PATH_LENGHT)))
		{
			D(std::cout << ">> PolyPath size: " << polypathSize << std::endl);

			if (dtStatusSucceed(m_NavMeshQueryMap[mapId]->findStraightPath(startPosition, endPosition, polypath, polypathSize, path, nullptr, nullptr, pathSize, MAX_PATH_LENGHT)))
			{
				D(std::cout << ">> PointPath size: " << pathSize << std::endl);

				// convert to Recast and Detour coordinates to Wow coordinates
				for (int i = 0; i < (*pathSize) * 3; i += 3)
				{
					RDToWowCoords(&path[i]);
				}

				return true;
			}
			else
			{
				std::cerr << ">> findStraightPath failed (mapId: " << mapId << ")" << std::endl;
			}
		}
		else
		{
			std::cerr << ">> findPath failed (mapId: " << mapId << ")" << std::endl;
		}

		*pathSize = 0;
		path = nullptr;

		delete[] closestPointStart;
		delete[] closestPointEnd;
		delete[] polypath;
		return false;
	}
}

bool AmeisenNavigation::CastMovementRay(int mapId, float* startPosition, float* endPosition)
{
	if (!IsMmapLoaded(mapId))
	{
		D(std::cout << ">> Mesh for Continent " << mapId << " not loaded, loading it now" << std::endl);

		if (!LoadMmapsForContinent(mapId))
		{
			std::cerr << ">> Unable to load MMAPS for continent " << mapId << std::endl;
			return false;
		}
	}

	dtPolyRef startPoly = GetNearestPoly(mapId, startPosition, nullptr);
	dtRaycastHit raycastHit;

	dtStatus result = m_NavMeshQueryMap[mapId]->raycast(startPoly, startPosition, endPosition, &m_QueryFilter, 0, &raycastHit);

	return dtStatusSucceed(result) && raycastHit.t != FLT_MAX;
}

dtPolyRef AmeisenNavigation::GetNearestPoly(int mapId, float* position, float* closestPointOnPoly)
{
	float extents[3] = { 8.0f, 8.0f, 8.0f };

	dtPolyRef polyRef;
	m_NavMeshQueryMap[mapId]->findNearestPoly(position, extents, &m_QueryFilter, &polyRef, closestPointOnPoly);

	return polyRef;
}

bool AmeisenNavigation::LoadMmapsForContinent(int mapId)
{
	// build the *.mmap filename (example: 001.mmap or 587.mmap)
	std::string mmapFilename = m_MmapFolder;
	mmapFilename += FormatTrailingZeros(mapId, 3);
	mmapFilename += ".mmap";

	// we can't load non existent files
	if (!std::filesystem::exists(mmapFilename))
	{
		std::cerr << ">> Unable to find mmap file " << " (mapId: " << mapId << ", mmapFilename: \"" << mmapFilename << "\")" << std::endl;
		return false;
	}

	std::ifstream mmapStream;
	mmapStream.open(mmapFilename, std::ifstream::binary);

	// read the dtNavMeshParams
	dtNavMeshParams params;
	mmapStream.read(reinterpret_cast<char*>(&params), sizeof(dtNavMeshParams));
	mmapStream.close();

	// allocate and init the NavMesh
	m_NavMeshMap[mapId] = dtAllocNavMesh();
	if (dtStatusFailed(m_NavMeshMap[mapId]->init(&params)))
	{
		std::cerr << ">> Unable to init NavMesh " << " (mapId: " << mapId << ", mmapFilename: \"" << mmapFilename << "\")" << std::endl;
		dtFreeNavMesh(m_NavMeshMap[mapId]);
		return false;
	}

	// load every NavMesh Tile from 1, 1 to 64, 64
	for (int x = 1; x <= 64; ++x)
	{
		for (int y = 1; y <= 64; ++y)
		{
			std::string mmapTileFilename = m_MmapFolder;
			mmapTileFilename += FormatTrailingZeros(mapId, 3);
			mmapTileFilename += FormatTrailingZeros(x, 2);
			mmapTileFilename += FormatTrailingZeros(y, 2);
			mmapTileFilename += ".mmtile";

			// we can't load non existent files
			if (!std::filesystem::exists(mmapTileFilename))
			{
				continue;
			}

			D(std::cout << ">> Reading dtTile " << mmapTileFilename.c_str() << std::endl);

			std::ifstream mmapTileStream;
			mmapTileStream.open(mmapTileFilename, std::ifstream::binary);

			// read the mmap header
			MmapTileHeader mmapTileHeader;
			mmapTileStream.read(reinterpret_cast<char*>(&mmapTileHeader), sizeof(MmapTileHeader));

			// read the NavMesh Tile data
			void* mmapTileData = dtAlloc(mmapTileHeader.size, DT_ALLOC_PERM);
			mmapTileStream.read(reinterpret_cast<char*>(mmapTileData), mmapTileHeader.size);
			mmapTileStream.close();

			// add the Tile to our NavMesh
			dtTileRef tileRef;
			if (dtStatusFailed(m_NavMeshMap[mapId]->addTile(reinterpret_cast<unsigned char*>(mmapTileData), mmapTileHeader.size, DT_TILE_FREE_DATA, 0, &tileRef)))
			{
				std::cerr << ">> Failed to add dtTile " << x << " " << y << " to NavMesh (mapId: " << mapId << ")" << std::endl;

				dtFree(mmapTileData);
				dtFreeNavMesh(m_NavMeshMap[mapId]);
				return false;
			}
		}
	}

	// init the NavMeshQuery
	m_NavMeshQueryMap[mapId] = dtAllocNavMeshQuery();
	if (dtStatusFailed(m_NavMeshQueryMap[mapId]->init(m_NavMeshMap[mapId], MAX_PATH_LENGHT)))
	{
		std::cerr << ">> Failed to init NavMeshQuery (mapId: " << mapId << ")" << std::endl;

		dtFreeNavMeshQuery(m_NavMeshQueryMap[mapId]);
		dtFreeNavMesh(m_NavMeshMap[mapId]);
		return false;
	}

	return true;
}