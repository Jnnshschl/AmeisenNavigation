#include "main.h"


int main()
{
	std::string mmap_dir = "C:\\Users\\Administrator\\Desktop\\mmaps\\";

	dtNavMesh mesh;
	dtNavMeshQuery query;

	AmeisenNavigation* ameisen_nav = new AmeisenNavigation();
	ameisen_nav->LoadMmapsForContinent(0, mmap_dir, &mesh, &query);

	std::cin.get();
	return 0;
}