#pragma once

#include <unordered_map>

#include "../../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "../IQueryFilterProvider.hpp"
#include "335a/NavArea335a.hpp"
#include "548/NavArea548.hpp"
#include "MmapFormat.hpp"
#include "../../Clients/ClientState.hpp"

/// <summary>
/// Helper to provide default dtQueryFilter's. Clients may use their own filters to optimize movement,
/// prefer moving through water or whatever.
/// </summary>
class MmapQueryFilterProvider : public IQueryFilterProvider
{
    std::unordered_map<ClientState, dtQueryFilter*> Filters;

public:
    MmapQueryFilterProvider(MmapFormat format, float waterCost = 1.3f, float badLiquidCost = 4.0f) noexcept
        : Filters{}
    {
        Filters[ClientState::NORMAL] = new dtQueryFilter();
        Filters[ClientState::NORMAL_ALLIANCE] = new dtQueryFilter();
        Filters[ClientState::NORMAL_HORDE] = new dtQueryFilter();
        Filters[ClientState::DEAD] = new dtQueryFilter();

        switch (format)
        {
        case MmapFormat::UNKNOWN:
            Tc335aConfig(waterCost, badLiquidCost);
            break;

        case MmapFormat::TC335A:
            Tc335aConfig(waterCost, badLiquidCost);
            break;

        case MmapFormat::SF548:
            Sf548Config(waterCost, badLiquidCost);
            break;

        default:
            break;
        }
    }

    inline void Tc335aConfig(float waterCost, float badLiquidCost) noexcept
    {
        char includeFlags = static_cast<char>(NavArea335a::GROUND)
            | static_cast<char>(NavArea335a::WATER)
            | static_cast<char>(NavArea335a::MAGMA_SLIME);

        char excludeFlags = static_cast<char>(NavArea335a::EMPTY)
            | static_cast<char>(NavArea335a::GROUND_STEEP);

        Filters[ClientState::NORMAL]->setIncludeFlags(includeFlags);
        Filters[ClientState::NORMAL]->setExcludeFlags(excludeFlags);
        Filters[ClientState::NORMAL]->setAreaCost(static_cast<char>(NavArea335a::WATER), waterCost);
        Filters[ClientState::NORMAL]->setAreaCost(static_cast<char>(NavArea335a::MAGMA_SLIME), badLiquidCost);

        Filters[ClientState::NORMAL_ALLIANCE]->setIncludeFlags(includeFlags);
        Filters[ClientState::NORMAL_ALLIANCE]->setExcludeFlags(excludeFlags);
        Filters[ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(NavArea335a::WATER), waterCost);
        Filters[ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(NavArea335a::MAGMA_SLIME), badLiquidCost);

        Filters[ClientState::NORMAL_HORDE]->setIncludeFlags(includeFlags);
        Filters[ClientState::NORMAL_HORDE]->setExcludeFlags(excludeFlags);
        Filters[ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(NavArea335a::WATER), waterCost);
        Filters[ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(NavArea335a::MAGMA_SLIME), badLiquidCost);

        Filters[ClientState::DEAD]->setIncludeFlags(includeFlags);
        Filters[ClientState::DEAD]->setExcludeFlags(excludeFlags);
    }

    inline void Sf548Config(float waterCost, float badLiquidCost) noexcept
    {
        char includeFlags = static_cast<char>(NavArea548::GROUND)
            | static_cast<char>(NavArea548::WATER)
            | static_cast<char>(NavArea548::MAGMA)
            | static_cast<char>(NavArea548::SLIME);

        char excludeFlags = static_cast<char>(NavArea548::EMPTY);

        Filters[ClientState::NORMAL]->setIncludeFlags(includeFlags);
        Filters[ClientState::NORMAL]->setExcludeFlags(excludeFlags);
        Filters[ClientState::NORMAL]->setAreaCost(static_cast<char>(NavArea548::WATER), waterCost);
        Filters[ClientState::NORMAL]->setAreaCost(static_cast<char>(NavArea548::MAGMA), badLiquidCost);
        Filters[ClientState::NORMAL]->setAreaCost(static_cast<char>(NavArea548::SLIME), badLiquidCost);

        Filters[ClientState::NORMAL_ALLIANCE]->setIncludeFlags(includeFlags);
        Filters[ClientState::NORMAL_ALLIANCE]->setExcludeFlags(excludeFlags);
        Filters[ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(NavArea548::WATER), waterCost);
        Filters[ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(NavArea548::MAGMA), badLiquidCost);
        Filters[ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(NavArea548::SLIME), badLiquidCost);

        Filters[ClientState::NORMAL_HORDE]->setIncludeFlags(includeFlags);
        Filters[ClientState::NORMAL_HORDE]->setExcludeFlags(excludeFlags);
        Filters[ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(NavArea548::WATER), waterCost);
        Filters[ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(NavArea548::MAGMA), badLiquidCost);
        Filters[ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(NavArea548::SLIME), badLiquidCost);

        Filters[ClientState::DEAD]->setIncludeFlags(includeFlags);
        Filters[ClientState::DEAD]->setExcludeFlags(excludeFlags);
    }

    ~MmapQueryFilterProvider() noexcept
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
