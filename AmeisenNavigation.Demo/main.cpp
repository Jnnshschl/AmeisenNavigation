#include "main.h"

int main()
{
	int mapId = 0;
	std::string mmapsFolder = "H:\\WoW Stuff\\3.3.5a mmaps\\";

	float startPosition[3] = { -8826.562500f, -371.839752f, 71.638428f };
	float endPosition[3] = { -8918.406250f, -130.297256f, 80.906364f };

	// use this to test same-poly pathfinding
	// float endPosition[3] = { -8828.562500f, -371.839752f, 71.638428f };

	std::cout << ">> Ameisen Navigation Demo" << std::endl << std::endl;

	std::cout << ">> MMAPFolder: \t\"" << mmapsFolder << "\"" << std::endl;
	std::cout << ">> MapId: \t" << mapId << std::endl << std::endl;

	std::cout << ">> Start Position: \t(" << startPosition[0] << ", " << startPosition[1] << ", " << startPosition[2] << ")" << std::endl;
	std::cout << ">> End Position: \t(" << endPosition[0] << ", " << endPosition[1] << ", " << endPosition[2] << ")" << std::endl << std::endl;

	AmeisenNavigation ameisenNavigation = AmeisenNavigation(mmapsFolder);

	// load the mmaps
	std::cout << ">> Preloading maps for continent (mapId: " << mapId << ")" << std::endl;
	std::chrono::high_resolution_clock::time_point t1LoadMmapsForContinent = std::chrono::high_resolution_clock::now();
	ameisenNavigation.LoadMmapsForContinent(mapId); // Eastern Kingdoms
	std::chrono::high_resolution_clock::time_point t2LoadMmapsForContinent = std::chrono::high_resolution_clock::now();

	auto durationtLoadMmapsForContinent = std::chrono::duration_cast<std::chrono::milliseconds>(t2LoadMmapsForContinent - t1LoadMmapsForContinent).count();
	std::cout << ">> LoadMmapsForContinent(" << mapId << ") took " << durationtLoadMmapsForContinent << "ms" << std::endl << std::endl;

	int pathSize = 0;
	float* path = new float[MAX_PATH_LENGHT * 3];

	// calculate the path
	std::cout << ">> Calculating path (mapId: " << mapId << ")" << std::endl;
	std::chrono::high_resolution_clock::time_point t1GetPath = std::chrono::high_resolution_clock::now();
	bool result = ameisenNavigation.GetPath(mapId, startPosition, endPosition, path, &pathSize);
	std::chrono::high_resolution_clock::time_point t2GetPath = std::chrono::high_resolution_clock::now();

	auto durationGetPath = std::chrono::duration_cast<std::chrono::milliseconds>(t2GetPath - t1GetPath).count();
	std::cout << ">> GetPath(" << mapId << ", 0x" << std::hex << startPosition << ", 0x" << endPosition << ", 0x" << path << ", 0x" << &pathSize << std::dec << ") took " << durationGetPath << "ms" << std::endl << std::endl;

	if (result)
	{
		std::cout << ">> Pathfinding successful" << std::endl;
		std::cout << ">> Path size: \t\t" << pathSize << " Nodes" << std::endl;
		std::cout << ">> Path Address: \t0x" << std::hex << path << "" << std::dec << std::endl << std::endl;

		// print the nodes
		for (int i = 0; i < pathSize * 3; i += 3)
		{
			std::cout << ">> Node [" << i / 3 << "]: \t(" << path[i] << ", " << path[i + 1] << ", " << path[i + 2] << ")" << std::endl;
		}
	}
	else
	{
		std::cout << ">> Pathfinding failed" << std::endl;
	}

	delete[] path;

	std::cout << std::endl << ">> Press a key to exit this Application";
	std::cin.get();
}