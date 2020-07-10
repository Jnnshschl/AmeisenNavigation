#ifndef _H_MAIN
#define _H_MAIN

#include <chrono>
#include <utility>
#include <iomanip>

#include "../AmeisenNavigation/ameisennavigation.h"

void TestLoadMmaps(const int mapId, AmeisenNavigation& ameisenNavigation);
void TestGetPath(const int mapId, AmeisenNavigation& ameisenNavigation, const Vector3& startPosition, const Vector3& endPosition);
void TestCastMovementRay(const int mapId, AmeisenNavigation& ameisenNavigation, const Vector3& startPosition, const Vector3& endPosition);
void TestMoveAlongSurface(const int mapId, AmeisenNavigation& ameisenNavigation, const Vector3& startPosition, const Vector3& endPosition);
void TestRandomPoint(const int mapId, AmeisenNavigation& ameisenNavigation);
void TestRandomPointAround(const int mapId, AmeisenNavigation& ameisenNavigation, const Vector3& startPosition);

#endif // !_H_MAIN