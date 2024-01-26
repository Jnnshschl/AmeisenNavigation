#pragma once

#include "../../../recastnavigation/Detour/Include/DetourNavMesh.h"

#include "Vector3.hpp"

struct PolyPosition
{
    dtPolyRef poly{ 0 };
    Vector3 pos{ 0.0f, 0.0f, 0.0f };
};
