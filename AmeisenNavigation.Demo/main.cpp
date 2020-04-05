#include "main.h"

int main()
{
	int mapId = 0; // Eastern Kingdoms
	std::string mmapsFolder = "H:\\WoW Stuff\\3.3.5a mmaps\\";

	Vector3 startPositionCastMovementRay(-8826.562500f, -371.839752f, 71.638428f);
	Vector3 endPositionCastMovementRay(-8918.406250f, -130.297256f, 80.906364f);

	Vector3 startPositionMoveAlongSurface(-8826.562500f, -371.839752f, 71.638428f);
	Vector3 endPositionMoveAlongSurface(-8918.406250f, -130.297256f, 80.906364f);

	Vector3 startPositionGetPath(-8826.562500f, -371.839752f, 71.638428f);
	Vector3 endPositionGetPath(-8918.406250f, -130.297256f, 80.906364f);

	// use this to test same-poly pathfinding
	Vector3 endPositionSamePoly(-8828.562500f, -371.839752f, 71.638428f);

	std::cout << ">> Ameisen Navigation Demo" << std::endl << std::endl;

	std::cout << ">> MMAPFolder: \t\t\t\"" << mmapsFolder << "\"" << std::endl;
	std::cout << ">> MapId: \t\t\t" << mapId << std::endl << std::endl;

	AmeisenNavigation ameisenNavigation = AmeisenNavigation(mmapsFolder);

	TestLoadMmaps(mapId, ameisenNavigation);
	TestCastMovementRay(mapId, ameisenNavigation, startPositionCastMovementRay, endPositionCastMovementRay);
	TestMoveAlongSurface(mapId, ameisenNavigation, startPositionMoveAlongSurface, endPositionMoveAlongSurface);
	TestGetPath(mapId, ameisenNavigation, startPositionGetPath, endPositionGetPath);

	std::cout << std::endl << ">> Press a key to exit this Application";
	std::cin.get();
}

void TestCastMovementRay(const int mapId, AmeisenNavigation& ameisenNavigation, const Vector3& startPosition, const Vector3& endPosition)
{
	std::chrono::high_resolution_clock::time_point t1CastMovementRay = std::chrono::high_resolution_clock::now();
	bool result = ameisenNavigation.CastMovementRay(mapId, startPosition, endPosition);
	std::chrono::high_resolution_clock::time_point t2CastMovementRay = std::chrono::high_resolution_clock::now();

	auto durationCastMovementRay = std::chrono::duration_cast<std::chrono::milliseconds>(t2CastMovementRay - t1CastMovementRay).count();
	std::cout << ">> CastMovementRay \t\t" << durationCastMovementRay << " ms" << std::endl;
	std::cout << ">> Result: \t\t\t" << result << std::endl << std::endl;
}

void TestGetPath(const int mapId, AmeisenNavigation& ameisenNavigation, const Vector3& startPosition, const Vector3& endPosition)
{
	int pathSize = 0;
	Vector3 path[MAX_PATH_LENGHT];

	std::chrono::high_resolution_clock::time_point t1GetPath = std::chrono::high_resolution_clock::now();
	bool result = ameisenNavigation.GetPath(mapId, startPosition, endPosition, path, &pathSize);
	std::chrono::high_resolution_clock::time_point t2GetPath = std::chrono::high_resolution_clock::now();

	auto durationGetPath = std::chrono::duration_cast<std::chrono::milliseconds>(t2GetPath - t1GetPath).count();
	std::cout << ">> GetPath \t\t\t" << durationGetPath << " ms" << std::endl << std::endl;

	if (result)
	{
		std::cout << ">> Path size: \t\t\t" << pathSize << " Nodes" << std::endl;
		std::cout << ">> Path Address: \t\t0x" << std::hex << path << "" << std::dec << std::endl << std::endl;

		// print the nodes
		for (int i = 0; i < pathSize; ++i)
		{
			std::cout << std::fixed << std::setprecision(2) << ">> Node [" << i << "]: \t\t\t" << path[i] << std::endl;
		}
	}
	else
	{
		std::cout << ">> Pathfinding failed" << std::endl;
	}
}

void TestLoadMmaps(int mapId, AmeisenNavigation& ameisenNavigation)
{
	std::chrono::high_resolution_clock::time_point t1LoadMmapsForContinent = std::chrono::high_resolution_clock::now();
	ameisenNavigation.LoadMmapsForContinent(mapId);
	std::chrono::high_resolution_clock::time_point t2LoadMmapsForContinent = std::chrono::high_resolution_clock::now();

	auto durationtLoadMmapsForContinent = std::chrono::duration_cast<std::chrono::milliseconds>(t2LoadMmapsForContinent - t1LoadMmapsForContinent).count();
	std::cout << ">> LoadMmapsForContinent \t" << durationtLoadMmapsForContinent << " ms" << std::endl << std::endl;
}

void TestMoveAlongSurface(const int mapId, AmeisenNavigation& ameisenNavigation, const Vector3& startPosition, const Vector3& endPosition)
{
	Vector3 moveAlongSurfacePoint;

	std::chrono::high_resolution_clock::time_point t1MoveAlongSurface = std::chrono::high_resolution_clock::now();
	ameisenNavigation.MoveAlongSurface(mapId, startPosition, endPosition, &moveAlongSurfacePoint);
	std::chrono::high_resolution_clock::time_point t2MoveAlongSurface = std::chrono::high_resolution_clock::now();

	auto durationMoveAlongSurface = std::chrono::duration_cast<std::chrono::milliseconds>(t2MoveAlongSurface - t1MoveAlongSurface).count();
	std::cout << ">> MoveAlongSurface \t\t" << durationMoveAlongSurface << " ms" << std::endl;
	std::cout << std::fixed << std::setprecision(2) << ">> Target Position: \t\t" << moveAlongSurfacePoint << std::endl << std::endl;
}