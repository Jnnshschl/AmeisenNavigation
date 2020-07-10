#include "main.h"

int main()
{
	int mapId = 489; // warsong gulch
	std::string mmapsFolder = "C:\\Users\\Jannis Server\\Downloads\\mmaps\\";

	Vector3 startPositionCastMovementRay(916, 1434, 346);
	Vector3 endPositionCastMovementRay(1539, 1481, 352);

	Vector3 startPositionMoveAlongSurface(916, 1434, 346);
	Vector3 endPositionMoveAlongSurface(1539, 1481, 352);

	Vector3 startPositionGetPath(916, 1434, 346);
	Vector3 endPositionGetPath(1539, 1481, 352);

	std::cout << ">> Ameisen Navigation Demo" << std::endl;

	std::cout << ">> MMAPFolder: \"" << mmapsFolder << "\"" << std::endl;
	std::cout << ">> MapId: " << mapId << std::endl;

	AmeisenNavigation ameisenNavigation = AmeisenNavigation(mmapsFolder, 256, 32);

	TestLoadMmaps(mapId, ameisenNavigation);

	std::cout << std::endl << ">> ---- Testing CastMovementRay:" << std::endl;
	TestCastMovementRay(mapId, ameisenNavigation, startPositionCastMovementRay, Vector3(918, 1434, 346));

	std::cout << std::endl << ">> ---- Testing MoveAlongSurface:" << std::endl;
	TestMoveAlongSurface(mapId, ameisenNavigation, startPositionMoveAlongSurface, endPositionMoveAlongSurface);

	std::cout << std::endl << ">> ---- Testing GetPath:" << std::endl;
	TestGetPath(mapId, ameisenNavigation, startPositionGetPath, endPositionGetPath);

	std::cout << std::endl << ">> ---- Testing GetRandomPoint:" << std::endl;
	TestRandomPoint(mapId, ameisenNavigation);

	std::cout << std::endl << ">> ---- Testing GetRandomPointAround:" << std::endl;
	TestRandomPointAround(mapId, ameisenNavigation, startPositionGetPath);

	std::cout << std::endl << ">> Press a key to exit this Application";
	std::cin.get();
}

void TestCastMovementRay(const int mapId, AmeisenNavigation& ameisenNavigation, const Vector3& startPosition, const Vector3& endPosition)
{
	std::chrono::high_resolution_clock::time_point t1CastMovementRay = std::chrono::high_resolution_clock::now();
	bool result = ameisenNavigation.CastMovementRay(mapId, startPosition, endPosition);
	std::chrono::high_resolution_clock::time_point t2CastMovementRay = std::chrono::high_resolution_clock::now();

	std::string txt = result ? "no hit" : "hit wall";

	auto durationCastMovementRay = std::chrono::duration_cast<std::chrono::milliseconds>(t2CastMovementRay - t1CastMovementRay).count();
	std::cout << ">> CastMovementRay took " << durationCastMovementRay << " ms" << std::endl;
	std::cout << ">> Result: " << txt << std::endl;
}

void TestGetPath(const int mapId, AmeisenNavigation& ameisenNavigation, const Vector3& startPosition, const Vector3& endPosition)
{
	int pathSize = 0;
	Vector3 path[256];

	std::chrono::high_resolution_clock::time_point t1GetPath = std::chrono::high_resolution_clock::now();
	bool result = ameisenNavigation.GetPath(mapId, startPosition, endPosition, path, &pathSize);
	std::chrono::high_resolution_clock::time_point t2GetPath = std::chrono::high_resolution_clock::now();

	auto durationGetPath = std::chrono::duration_cast<std::chrono::milliseconds>(t2GetPath - t1GetPath).count();
	std::cout << ">> GetPath took " << durationGetPath << " ms" << std::endl;

	if (result)
	{
		std::cout << ">> Path size: " << pathSize << " Nodes" << std::endl;

		// print the nodes
		for (int i = 0; i < pathSize; ++i)
		{
			std::cout << std::fixed << std::setprecision(2) << ">> Node [" << i << "]: " << path[i] << std::endl;
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
	std::cout << ">> LoadMmapsForContinent took " << durationtLoadMmapsForContinent << " ms" << std::endl;
}

void TestMoveAlongSurface(const int mapId, AmeisenNavigation& ameisenNavigation, const Vector3& startPosition, const Vector3& endPosition)
{
	Vector3 moveAlongSurfacePoint;

	std::chrono::high_resolution_clock::time_point t1MoveAlongSurface = std::chrono::high_resolution_clock::now();
	ameisenNavigation.MoveAlongSurface(mapId, startPosition, endPosition, &moveAlongSurfacePoint);
	std::chrono::high_resolution_clock::time_point t2MoveAlongSurface = std::chrono::high_resolution_clock::now();

	auto durationMoveAlongSurface = std::chrono::duration_cast<std::chrono::milliseconds>(t2MoveAlongSurface - t1MoveAlongSurface).count();
	std::cout << ">> MoveAlongSurface took " << durationMoveAlongSurface << " ms" << std::endl;
	std::cout << std::fixed << std::setprecision(2) << ">> Target Position: " << moveAlongSurfacePoint << std::endl;
}

void TestRandomPoint(const int mapId, AmeisenNavigation& ameisenNavigation)
{
	Vector3 getRandomPointPoint;

	std::chrono::high_resolution_clock::time_point t1GetRandomPoint = std::chrono::high_resolution_clock::now();
	ameisenNavigation.GetRandomPoint(mapId, &getRandomPointPoint);
	std::chrono::high_resolution_clock::time_point t2GetRandomPoint = std::chrono::high_resolution_clock::now();

	auto durationGetRandomPoint = std::chrono::duration_cast<std::chrono::milliseconds>(t2GetRandomPoint - t1GetRandomPoint).count();
	std::cout << ">> GetRandomPoint took " << durationGetRandomPoint << " ms" << std::endl;
	std::cout << std::fixed << std::setprecision(2) << ">> Target Position: " << getRandomPointPoint << std::endl;
}

void TestRandomPointAround(const int mapId, AmeisenNavigation& ameisenNavigation, const Vector3& startPosition)
{
	Vector3 getRandomPointAroundPoint;

	std::chrono::high_resolution_clock::time_point t1GetRandomPointAround = std::chrono::high_resolution_clock::now();
	ameisenNavigation.GetRandomPointAround(mapId, startPosition, 32.f, &getRandomPointAroundPoint);
	std::chrono::high_resolution_clock::time_point t2GetRandomPointAround = std::chrono::high_resolution_clock::now();

	auto durationGetRandomPointAround = std::chrono::duration_cast<std::chrono::milliseconds>(t2GetRandomPointAround - t1GetRandomPointAround).count();
	std::cout << ">> GetRandomPointAround took " << durationGetRandomPointAround << " ms" << std::endl;
	std::cout << std::fixed << std::setprecision(2) << ">> Target Position: " << getRandomPointAroundPoint << std::endl;
}