#pragma once

#include "../../../recastnavigation/Detour/Include/DetourNavMesh.h"

class INavSource
{
public:
    virtual ~INavSource() noexcept {}

    virtual dtNavMesh* Get(size_t mapId) noexcept = 0;
};
