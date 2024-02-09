#pragma once

#include <unordered_map>

#include "../../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "../../../AmeisenNavigation.Exporter/src/Utils/Tri.hpp"

#include "../IQueryFilterProvider.hpp"
#include "../../Clients/ClientState.hpp"

/// <summary>
/// Helper to provide default dtQueryFilter's. Clients may use their own filters to optimize movement,
/// prefer moving through water or whatever.
/// </summary>
class AnpQueryFilterProvider : public IQueryFilterProvider
{
    std::unordered_map<ClientState, dtQueryFilter*> Filters;

public:
    AnpQueryFilterProvider(float waterCost = 1.3f, float badLiquidCost = 4.0f) noexcept
        : Filters{}
    {
        Filters[ClientState::NORMAL] = new dtQueryFilter();
        Filters[ClientState::NORMAL_ALLIANCE] = new dtQueryFilter();
        Filters[ClientState::NORMAL_HORDE] = new dtQueryFilter();
        Filters[ClientState::DEAD] = new dtQueryFilter();

        char includeFlags = static_cast<char>(TriFlag::NAV_LAVA_SLIME)
            | static_cast<char>(TriFlag::NAV_WATER)
            | static_cast<char>(TriFlag::NAV_GROUND)
            | static_cast<char>(TriFlag::NAV_ROAD)
            | static_cast<char>(TriFlag::NAV_ALLIANCE)
            | static_cast<char>(TriFlag::NAV_HORDE);

        char excludeFlags = static_cast<char>(TriFlag::NAV_EMPTY);

        Filters[ClientState::NORMAL]->setIncludeFlags(includeFlags);
        Filters[ClientState::NORMAL]->setExcludeFlags(excludeFlags);
        Filters[ClientState::NORMAL]->setAreaCost(static_cast<char>(TriAreaId::LIQUID_WATER), waterCost);
        Filters[ClientState::NORMAL]->setAreaCost(static_cast<char>(TriAreaId::LIQUID_OCEAN), waterCost);
        Filters[ClientState::NORMAL]->setAreaCost(static_cast<char>(TriAreaId::LIQUID_LAVA), badLiquidCost);
        Filters[ClientState::NORMAL]->setAreaCost(static_cast<char>(TriAreaId::LIQUID_SLIME), badLiquidCost);

        Filters[ClientState::NORMAL_ALLIANCE]->setIncludeFlags(includeFlags);
        Filters[ClientState::NORMAL_ALLIANCE]->setExcludeFlags(excludeFlags);
        Filters[ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(TriAreaId::LIQUID_WATER), waterCost);
        Filters[ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(TriAreaId::LIQUID_OCEAN), waterCost);
        Filters[ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(TriAreaId::LIQUID_LAVA), badLiquidCost);
        Filters[ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(TriAreaId::LIQUID_SLIME), badLiquidCost);

        Filters[ClientState::NORMAL_HORDE]->setIncludeFlags(includeFlags);
        Filters[ClientState::NORMAL_HORDE]->setExcludeFlags(excludeFlags);
        Filters[ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(TriAreaId::LIQUID_WATER), waterCost);
        Filters[ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(TriAreaId::LIQUID_OCEAN), waterCost);
        Filters[ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(TriAreaId::LIQUID_LAVA), badLiquidCost);
        Filters[ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(TriAreaId::LIQUID_SLIME), badLiquidCost);

        Filters[ClientState::DEAD]->setIncludeFlags(includeFlags);
        Filters[ClientState::DEAD]->setExcludeFlags(excludeFlags);
    }

    ~AnpQueryFilterProvider() noexcept
    {
        for (auto& [state, f] : Filters)
        {
            delete f;
        }
    }

    virtual dtQueryFilter* Get(ClientState state) const noexcept override
    {
        return Filters.at(state);
    }
};
