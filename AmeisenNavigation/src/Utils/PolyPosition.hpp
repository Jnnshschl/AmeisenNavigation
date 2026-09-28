#pragma once

#include <DetourNavMesh.h>

#include "Vector3.hpp"

/// A position on the navmesh (RD coordinates) and the polygon containing it.
struct PolyPosition
{
    dtPolyRef poly{0};
    Vector3 pos{0.0f, 0.0f, 0.0f};
};
