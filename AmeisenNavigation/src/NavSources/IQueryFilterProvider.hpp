#pragma once

#include "../../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "../Clients/ClientState.hpp"

class IQueryFilterProvider
{
public:
    virtual ~IQueryFilterProvider() noexcept {}

    virtual dtQueryFilter* Get(ClientState state) const noexcept = 0;
};
