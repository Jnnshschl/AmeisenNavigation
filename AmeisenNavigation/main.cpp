#include "main.h"


int main()
{
	std::string mmap_dir = "C:\\Users\\Administrator\\Desktop\\mmaps\\";
	//std::string mmap_dir = "H:\\mmaps\\";
	int map_id = 0;

	AmeisenNavigation* ameisen_nav = new AmeisenNavigation(mmap_dir);

	float start[3] = { -8826.562500f, -371.839752f, 71.638428f };
	float end[3] = { -8847.150391f, -387.518677f, 72.575912f };

	float start_rd[3] = { -start[1], start[2], -start[0] };
	float end_rd[3] = { -end[1], end[2], -end[0] };

	float tile_loc[3] = { 130.297256f, 80.906364f, 8918.406250f };

	int x = floor((32 - (start[0] / 533.33333)));
	int y = floor((32 - (start[1] / 533.33333)));

	std::cout << "-> StartTile: X: " << x << " Y: " << y << "\n";

	ameisen_nav->GetPath(map_id, start_rd, end_rd);
	//ameisen_nav->GetPath(map_id, start_rd, end_rd);

	//std::cout << "-> TileLocation "<< tile_loc[0] << " " << tile_loc[1] << " " << tile_loc[2] << "\n";

	std::cin.get();
	return 0;
}