#ifndef _H_MAIN
#define _H_MAIN

#include <chrono>
#include <utility>
#include <iomanip>

#include "../AmeisenNavigation/ameisennavigation.h"

void TestLoadMmaps(int mapId, AmeisenNavigation& ameisenNavigation);
void TestGetPath(int mapId, AmeisenNavigation& ameisenNavigation, float* startPosition, float* endPosition);
void TestCastMovementRay(int mapId, AmeisenNavigation& ameisenNavigation, float* startPosition, float* endPosition);
void TestMoveAlongSurface(int mapId, AmeisenNavigation& ameisenNavigation, float* startPosition, float* endPosition);

#endif // !_H_MAIN