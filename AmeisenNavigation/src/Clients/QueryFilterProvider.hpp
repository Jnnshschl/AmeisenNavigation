#pragma once

#include <unordered_map>

#include "../../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "ClientState.hpp"

#include "335a/NavArea335a.hpp"
#include "548/NavArea548.hpp"

#include "../Mmap/MmapFormat.hpp"

/// <summary>
/// Helper to provide default dtQueryFilter's. Clients may use their own filters to optimize movement,
/// prefer moving through water or whatever.
/// </summary>
class QueryFilterProvider
{
    std::unordered_map <MmapFormat, std::unordered_map<ClientState, dtQueryFilter*>> Filters;

public:
    QueryFilterProvider(float waterCost = 1.3f, float badLiquidCost = 4.0f) noexcept
        : Filters{}
    {
        for (const auto format : { MmapFormat::TC335A, MmapFormat::SF548 })
        {
            Filters[format][ClientState::NORMAL] = new dtQueryFilter();
            Filters[format][ClientState::NORMAL_ALLIANCE] = new dtQueryFilter();
            Filters[format][ClientState::NORMAL_HORDE] = new dtQueryFilter();
            Filters[format][ClientState::DEAD] = new dtQueryFilter();
        }

        Tc335aConfig(waterCost, badLiquidCost);
        Sf548Config(waterCost, badLiquidCost);
    }

    inline void Tc335aConfig(float waterCost, float badLiquidCost) noexcept
    {
        char includeFlags = static_cast<char>(NavArea335a::GROUND) 
            | static_cast<char>(NavArea335a::WATER) 
            | static_cast<char>(NavArea335a::MAGMA_SLIME);

        char excludeFlags = static_cast<char>(NavArea335a::EMPTY) 
            | static_cast<char>(NavArea335a::GROUND_STEEP);

        Filters[MmapFormat::TC335A][ClientState::NORMAL]->setIncludeFlags(includeFlags);
        Filters[MmapFormat::TC335A][ClientState::NORMAL]->setExcludeFlags(excludeFlags);
        Filters[MmapFormat::TC335A][ClientState::NORMAL]->setAreaCost(static_cast<char>(NavArea335a::WATER), waterCost);
        Filters[MmapFormat::TC335A][ClientState::NORMAL]->setAreaCost(static_cast<char>(NavArea335a::MAGMA_SLIME), badLiquidCost);

        Filters[MmapFormat::TC335A][ClientState::NORMAL_ALLIANCE]->setIncludeFlags(includeFlags);
        Filters[MmapFormat::TC335A][ClientState::NORMAL_ALLIANCE]->setExcludeFlags(excludeFlags);
        Filters[MmapFormat::TC335A][ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(NavArea335a::WATER), waterCost);
        Filters[MmapFormat::TC335A][ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(NavArea335a::MAGMA_SLIME), badLiquidCost);

        Filters[MmapFormat::TC335A][ClientState::NORMAL_HORDE]->setIncludeFlags(includeFlags);
        Filters[MmapFormat::TC335A][ClientState::NORMAL_HORDE]->setExcludeFlags(excludeFlags);
        Filters[MmapFormat::TC335A][ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(NavArea335a::WATER), waterCost);
        Filters[MmapFormat::TC335A][ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(NavArea335a::MAGMA_SLIME), badLiquidCost);

        Filters[MmapFormat::TC335A][ClientState::DEAD]->setIncludeFlags(includeFlags);
        Filters[MmapFormat::TC335A][ClientState::DEAD]->setExcludeFlags(excludeFlags);
    }

    inline void Sf548Config(float waterCost, float badLiquidCost) noexcept
    {
        char includeFlags = static_cast<char>(NavArea548::GROUND) 
            | static_cast<char>(NavArea548::WATER) 
            | static_cast<char>(NavArea548::MAGMA) 
            | static_cast<char>(NavArea548::SLIME);

        char excludeFlags = static_cast<char>(NavArea548::EMPTY);

        Filters[MmapFormat::SF548][ClientState::NORMAL]->setIncludeFlags(includeFlags);
        Filters[MmapFormat::SF548][ClientState::NORMAL]->setExcludeFlags(excludeFlags);
        Filters[MmapFormat::SF548][ClientState::NORMAL]->setAreaCost(static_cast<char>(NavArea548::WATER), waterCost);
        Filters[MmapFormat::SF548][ClientState::NORMAL]->setAreaCost(static_cast<char>(NavArea548::MAGMA), badLiquidCost);
        Filters[MmapFormat::SF548][ClientState::NORMAL]->setAreaCost(static_cast<char>(NavArea548::SLIME), badLiquidCost);

        Filters[MmapFormat::SF548][ClientState::NORMAL_ALLIANCE]->setIncludeFlags(includeFlags);
        Filters[MmapFormat::SF548][ClientState::NORMAL_ALLIANCE]->setExcludeFlags(excludeFlags);
        Filters[MmapFormat::SF548][ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(NavArea548::WATER), waterCost);
        Filters[MmapFormat::SF548][ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(NavArea548::MAGMA), badLiquidCost);
        Filters[MmapFormat::SF548][ClientState::NORMAL_ALLIANCE]->setAreaCost(static_cast<char>(NavArea548::SLIME), badLiquidCost);

        Filters[MmapFormat::SF548][ClientState::NORMAL_HORDE]->setIncludeFlags(includeFlags);
        Filters[MmapFormat::SF548][ClientState::NORMAL_HORDE]->setExcludeFlags(excludeFlags);
        Filters[MmapFormat::SF548][ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(NavArea548::WATER), waterCost);
        Filters[MmapFormat::SF548][ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(NavArea548::MAGMA), badLiquidCost);
        Filters[MmapFormat::SF548][ClientState::NORMAL_HORDE]->setAreaCost(static_cast<char>(NavArea548::SLIME), badLiquidCost);

        Filters[MmapFormat::SF548][ClientState::DEAD]->setIncludeFlags(includeFlags);
        Filters[MmapFormat::SF548][ClientState::DEAD]->setExcludeFlags(excludeFlags);
    }

    ~QueryFilterProvider() noexcept
    {
        for (auto& [format, filter] : Filters)
        {
            for (auto& [state, f] : filter)
            {
                delete f;
            }
        }
    }

    inline dtQueryFilter* Get(MmapFormat format, ClientState state) noexcept { return Filters[format][state]; }
};
