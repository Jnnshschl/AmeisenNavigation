#include "main.h"


int main()
{
	//std::string mmap_dir = "C:\\Users\\Administrator\\Desktop\\mmaps\\";
	std::string mmap_dir = "H:\\mmaps\\";
	int map_id = 0;

	AmeisenNavigation* ameisen_nav = new AmeisenNavigation();
	std::pair<dtNavMesh*, dtNavMeshQuery*> navmesh_pair = ameisen_nav->LoadMmapsForContinent(0, mmap_dir);

	dtNavMesh* mesh = navmesh_pair.first;
	dtNavMeshQuery* query = navmesh_pair.second;
	dtQueryFilter* filter = new dtQueryFilter();

	float start[3] = { -8826.562500f, -371.839752f, 71.638428f };
	float end[3] = { -8847.150391f, -387.518677f, 72.575912f };

	float start_rd[3] = { -start[1], start[2], -start[0] };
	float end_rd[3] = { -end[1], end[2], -end[0] };

	float extents[3] = { 3.0f, 5.0f, 3.0f };
	float closest_point[3] = { 0.0f, 0.0f, 0.0f };

	float tile_loc[3] = { 130.297256f, 80.906364f, 8918.406250f };
	float random_point[3] = { 0,0,0 };

	std::cout << "-> Generating Path ("
		<< start_rd[0] << "|" << start_rd[1] << "|" << start_rd[2] << ") -> ("
		<< end_rd[0] << "|" << end_rd[1] << "|" << end_rd[2] << ")\n";

	std::cout << "-> TileLocation "<< random_point[0] << " " << random_point[1] << " " << random_point[2] << "\n";

	dtPolyRef start_poly;
	if (dtStatusSucceed(query->findNearestPoly(start_rd, extents, filter, &start_poly, closest_point)))
	{
		std::cout << "-> Start Poly found " << closest_point[0] << " " << closest_point[1] << " " << closest_point[2] << "\n";
	}
	else
	{
		std::cout << "-> Start Poly not found\n";
	}

	dtPolyRef end_poly;
	if (dtStatusSucceed(query->findNearestPoly(end_rd, extents, filter, &end_poly, closest_point)))
	{
		std::cout << "-> End Poly found " << end_poly << " \n";
	}
	else
	{
		std::cout << "-> End Poly not found\n";
	}

	/*int path_size = 0;
	dtPolyRef path;
	if (dtStatusSucceed(query->findPath(start_poly, end_poly, start_rd, end_rd, filter, &path, &path_size, 1024)))
	{
		std::cout << "-> Path found and contains " << point_count << " Nodes\n";
	}
	else
	{
		std::cout << "-> Path not found\n";
	}*/

	std::cin.get();
	return 0;
}