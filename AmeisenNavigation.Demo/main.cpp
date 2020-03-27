#include "main.h"

int main()
{
	int mapId = 0; // Eastern Kingdoms
	std::string mmapsFolder = "H:\\WoW Stuff\\3.3.5a mmaps\\";

	float startPositionCastMovementRay[3] = { -8826.562500f, -371.839752f, 71.638428f };
	float endPositionCastMovementRay[3] = { -8918.406250f, -130.297256f, 80.906364f };

	float startPositionMoveAlongSurface[3] = { -8826.562500f, -371.839752f, 71.638428f };
	float endPositionMoveAlongSurface[3] = { -8918.406250f, -130.297256f, 80.906364f };

	float startPositionGetPath[3] = { -8826.562500f, -371.839752f, 71.638428f };
	float endPositionGetPath[3] = { -8918.406250f, -130.297256f, 80.906364f };

	// use this to test same-poly pathfinding
	float endPositionSamePoly[3] = { -8828.562500f, -371.839752f, 71.638428f };

	std::cout << ">> Ameisen Navigation Demo" << std::endl << std::endl;

	std::cout << ">> MMAPFolder: \t\t\"" << mmapsFolder << "\"" << std::endl;
	std::cout << ">> MapId: \t\t" << mapId << std::endl << std::endl;

	AmeisenNavigation ameisenNavigation = AmeisenNavigation(mmapsFolder);

	TestLoadMmaps(mapId, ameisenNavigation);
	TestCastMovementRay(mapId, ameisenNavigation, startPositionCastMovementRay, endPositionCastMovementRay);
	TestMoveAlongSurface(mapId, ameisenNavigation, startPositionMoveAlongSurface, endPositionMoveAlongSurface);
	TestGetPath(mapId, ameisenNavigation, startPositionGetPath, endPositionGetPath);

	std::cout << std::endl << ">> Press a key to exit this Application";
	std::cin.get();
}

void TestCastMovementRay(int mapId, AmeisenNavigation& ameisenNavigation, float* startPosition, float* endPosition)
{
	std::chrono::high_resolution_clock::time_point t1CastMovementRay = std::chrono::high_resolution_clock::now();
	bool result = ameisenNavigation.CastMovementRay(mapId, startPosition, endPosition);
	std::chrono::high_resolution_clock::time_point t2CastMovementRay = std::chrono::high_resolution_clock::now();

	auto durationCastMovementRay = std::chrono::duration_cast<std::chrono::nanoseconds>(t2CastMovementRay - t1CastMovementRay).count();
	std::cout << ">> CastMovementRay(" << mapId << ", 0x" << std::hex << startPosition << ", 0x" << endPosition << ") took " << durationCastMovementRay << "ns" << std::endl;
	std::cout << ">> Result: \t\t" << result << std::endl << std::endl;
}

void TestGetPath(int mapId, AmeisenNavigation& ameisenNavigation, float* startPosition, float* endPosition)
{
	int pathSize = 0;
	float* path = new float[MAX_PATH_LENGHT * 3];

	std::cout << ">> Calculating path (mapId: " << mapId << ")" << std::endl;
	std::chrono::high_resolution_clock::time_point t1GetPath = std::chrono::high_resolution_clock::now();
	bool result = ameisenNavigation.GetPath(mapId, startPosition, endPosition, path, &pathSize);
	std::chrono::high_resolution_clock::time_point t2GetPath = std::chrono::high_resolution_clock::now();

	auto durationGetPath = std::chrono::duration_cast<std::chrono::nanoseconds>(t2GetPath - t1GetPath).count();
	std::cout << ">> GetPath(" << mapId << ", 0x" << std::hex << startPosition << ", 0x" << endPosition << ", 0x" << path << ", 0x" << &pathSize << std::dec << ") took " << durationGetPath << "ns" << std::endl << std::endl;

	if (result)
	{
		std::cout << ">> Pathfinding successful" << std::endl;
		std::cout << ">> Path size: \t\t" << pathSize << " Nodes" << std::endl;
		std::cout << ">> Path Address: \t0x" << std::hex << path << "" << std::dec << std::endl << std::endl;

		// print the nodes
		for (int i = 0; i < pathSize * 3; i += 3)
		{
			std::cout << std::fixed << std::setprecision(2) << ">> Node [" << i / 3 << "]: \t\t(" << path[i] << ", " << path[i + 1] << ", " << path[i + 2] << ")" << std::endl;
		}
	}
	else
	{
		std::cout << ">> Pathfinding failed" << std::endl;
	}

	delete[] path;
}

void TestLoadMmaps(int mapId, AmeisenNavigation& ameisenNavigation)
{
	std::cout << ">> Preloading maps for continent (mapId: " << mapId << ")" << std::endl;
	std::chrono::high_resolution_clock::time_point t1LoadMmapsForContinent = std::chrono::high_resolution_clock::now();
	ameisenNavigation.LoadMmapsForContinent(mapId);
	std::chrono::high_resolution_clock::time_point t2LoadMmapsForContinent = std::chrono::high_resolution_clock::now();

	auto durationtLoadMmapsForContinent = std::chrono::duration_cast<std::chrono::milliseconds>(t2LoadMmapsForContinent - t1LoadMmapsForContinent).count();
	std::cout << ">> LoadMmapsForContinent(" << mapId << ") took " << durationtLoadMmapsForContinent << "ms" << std::endl << std::endl;
}

void TestMoveAlongSurface(int mapId, AmeisenNavigation& ameisenNavigation, float* startPosition, float* endPosition)
{
	float* moveAlongSurfacePoint = new float[3];

	std::chrono::high_resolution_clock::time_point t1MoveAlongSurface = std::chrono::high_resolution_clock::now();
	ameisenNavigation.MoveAlongSurface(mapId, startPosition, endPosition, moveAlongSurfacePoint);
	std::chrono::high_resolution_clock::time_point t2MoveAlongSurface = std::chrono::high_resolution_clock::now();

	auto durationMoveAlongSurface = std::chrono::duration_cast<std::chrono::nanoseconds>(t2MoveAlongSurface - t1MoveAlongSurface).count();
	std::cout << ">> MoveAlongSurface(" << mapId << ", 0x" << std::hex << startPosition << ", 0x" << endPosition << ", 0x" << moveAlongSurfacePoint << ") took " << durationMoveAlongSurface << "ns" << std::endl;
	std::cout << std::fixed << std::setprecision(2) << ">> Target Position: \t(" << moveAlongSurfacePoint[0] << ", " << moveAlongSurfacePoint[1] << ", " << moveAlongSurfacePoint[2] << ")" << std::endl << std::endl;

	delete[] moveAlongSurfacePoint;
}