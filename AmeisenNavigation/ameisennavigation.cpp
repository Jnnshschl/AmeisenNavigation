#include "ameisennavigation.h"

Path AmeisenNavigation::GetPath(int map_id, Vector3 start_pos, Vector3 end_pos)
{
	return Path();
}

std::string format_trailing_zeros(int number, int total_count) {
	std::stringstream ss;
	ss << std::setw(total_count) << std::setfill('0') << number;
	return ss.str();
}

std::pair<dtNavMesh*, dtNavMeshQuery*> AmeisenNavigation::LoadMmapsForContinent(int map_id, std::string mmap_dir)
{
	dtNavMesh* mesh;
	dtNavMeshQuery* query;

	std::string mmap_filename = mmap_dir + format_trailing_zeros(map_id, 3) + ".mmap";

	std::ifstream mmap_stream;
	mmap_stream.open(mmap_filename);

	std::cout << "-> Reading " << mmap_filename.c_str() << "\n";

	dtNavMeshParams params;
	mmap_stream.read((char*)&params, sizeof(params));
	mmap_stream.close();

	mesh = dtAllocNavMesh();
	if (dtStatusFailed(mesh->init(&params)))
	{
		dtFreeNavMesh(mesh);
		std::cout << "-> Error: could not read mmap file\n";
	}

	for (int x = 1; x <= 64; x++)
	{
		for (int y = 1; y <= 64; y++)
		{
			std::string mmaptile_filename =
				mmap_dir
				+ format_trailing_zeros(map_id, 3)
				+ format_trailing_zeros(x, 2)
				+ format_trailing_zeros(y, 2)
				+ ".mmtile";

			GetFileAttributes(mmaptile_filename.c_str());
			if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(mmaptile_filename.c_str()) && GetLastError() == ERROR_FILE_NOT_FOUND)
			{
				continue;
			}

			//std::cout << "-> Reading Tile " << mmaptile_filename.c_str() << "\n";

			std::ifstream mmaptile_stream;
			mmaptile_stream.open(mmaptile_filename, std::ios::binary);

			MmapTileHeader fileHeader;
			mmaptile_stream.read((char*)&fileHeader, sizeof(fileHeader));

			unsigned char* data = new unsigned char[fileHeader.size];
			mmaptile_stream.read((char*)data, fileHeader.size);

			dtMeshHeader* header = (dtMeshHeader*)data;
			dtTileRef tileRef = 0;

			if (!dtStatusSucceed(mesh->addTile(data, fileHeader.size, DT_TILE_FREE_DATA, 0, &tileRef)))
			{
				std::cout << "--> Error at adding tile " << x << " " << y << " to navmesh\n";
			}
			mmaptile_stream.close();
		}
	}

	query = dtAllocNavMeshQuery();
	if (dtStatusFailed(query->init(mesh, 1024)))
	{
		std::cout << "-> Failed to built NavMeshQuery " << "\n";
		dtFreeNavMeshQuery(query);
		return std::pair<dtNavMesh*, dtNavMeshQuery*>(nullptr, nullptr);
	}
	else
	{
		std::cout << "-> Sucessfully built NavMeshQuery " << "\n";
		return std::pair<dtNavMesh*, dtNavMeshQuery*>(mesh, query);
	}
}