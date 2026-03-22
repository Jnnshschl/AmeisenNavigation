#pragma once

#include <memory>
#include <unordered_map>

#include "../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "../../AmeisenNavigation.Exporter/src/Utils/Tri.hpp"

#include "../../Clients/ClientState.hpp"
#include "../IQueryFilterProvider.hpp"

/// <summary>
/// Helper to provide default dtQueryFilter's for ANP format navmeshes.
///
/// All 27 TriAreaId values get explicit costs. Faction-specific filters
/// multiply enemy faction area costs by factionDangerCost, making bots
/// avoid enemy territory (e.g., Horde bot avoids Northshire Abbey).
///
/// Clients may override these defaults via ConfigureFilter messages.
/// </summary>
class AnpQueryFilterProvider : public IQueryFilterProvider
{
    std::unordered_map<ClientState, std::unique_ptr<dtQueryFilter>> Filters;

    /// Set costs for all 27 area IDs on a filter.
    /// allyMult/hordeMult: multiplier applied to Alliance/Horde faction areas.
    static void SetAllAreaCosts(dtQueryFilter* f, float ground, float road, float water,
                                float badLiquid, float allyMult, float hordeMult) noexcept
    {
        // Terrain ground (+ city, WMO, doodad use same base cost)
        f->setAreaCost(TERRAIN_GROUND, ground);
        f->setAreaCost(ALLIANCE_TERRAIN_GROUND, ground * allyMult);
        f->setAreaCost(HORDE_TERRAIN_GROUND, ground * hordeMult);

        f->setAreaCost(TERRAIN_ROAD, road);
        f->setAreaCost(ALLIANCE_TERRAIN_ROAD, road * allyMult);
        f->setAreaCost(HORDE_TERRAIN_ROAD, road * hordeMult);

        f->setAreaCost(TERRAIN_CITY, ground);
        f->setAreaCost(ALLIANCE_TERRAIN_CITY, ground * allyMult);
        f->setAreaCost(HORDE_TERRAIN_CITY, ground * hordeMult);

        f->setAreaCost(WMO, ground);
        f->setAreaCost(ALLIANCE_WMO, ground * allyMult);
        f->setAreaCost(HORDE_WMO, ground * hordeMult);

        f->setAreaCost(DOODAD, ground);
        f->setAreaCost(ALLIANCE_DOODAD, ground * allyMult);
        f->setAreaCost(HORDE_DOODAD, ground * hordeMult);

        // Water
        f->setAreaCost(LIQUID_WATER, water);
        f->setAreaCost(ALLIANCE_LIQUID_WATER, water * allyMult);
        f->setAreaCost(HORDE_LIQUID_WATER, water * hordeMult);

        f->setAreaCost(LIQUID_OCEAN, water);
        f->setAreaCost(ALLIANCE_LIQUID_OCEAN, water * allyMult);
        f->setAreaCost(HORDE_LIQUID_OCEAN, water * hordeMult);

        // Bad liquid (lava/slime) - always expensive regardless of faction
        f->setAreaCost(LIQUID_LAVA, badLiquid);
        f->setAreaCost(ALLIANCE_LIQUID_LAVA, badLiquid);
        f->setAreaCost(HORDE_LIQUID_LAVA, badLiquid);

        f->setAreaCost(LIQUID_SLIME, badLiquid);
        f->setAreaCost(ALLIANCE_LIQUID_SLIME, badLiquid);
        f->setAreaCost(HORDE_LIQUID_SLIME, badLiquid);
    }

public:
    AnpQueryFilterProvider(float waterCost = 1.6f, float badLiquidCost = 4.0f, float roadCost = 0.75f,
                           float factionDangerCost = 3.0f)
        : Filters{}
    {
        Filters[ClientState::NORMAL] = std::make_unique<dtQueryFilter>();
        Filters[ClientState::NORMAL_ALLIANCE] = std::make_unique<dtQueryFilter>();
        Filters[ClientState::NORMAL_HORDE] = std::make_unique<dtQueryFilter>();
        Filters[ClientState::DEAD] = std::make_unique<dtQueryFilter>();

        char includeFlags = static_cast<char>(TriFlag::NAV_LAVA_SLIME) | static_cast<char>(TriFlag::NAV_WATER) |
                            static_cast<char>(TriFlag::NAV_GROUND) | static_cast<char>(TriFlag::NAV_ROAD) |
                            static_cast<char>(TriFlag::NAV_ALLIANCE) | static_cast<char>(TriFlag::NAV_HORDE);

        char excludeFlags = static_cast<char>(TriFlag::NAV_EMPTY);

        // NORMAL - no faction bias, all areas at base cost
        Filters[ClientState::NORMAL]->setIncludeFlags(includeFlags);
        Filters[ClientState::NORMAL]->setExcludeFlags(excludeFlags);
        SetAllAreaCosts(Filters[ClientState::NORMAL].get(), 1.0f, roadCost, waterCost, badLiquidCost, 1.0f, 1.0f);

        // NORMAL_ALLIANCE - Alliance bot: Horde areas are dangerous (hordeMult = factionDangerCost)
        Filters[ClientState::NORMAL_ALLIANCE]->setIncludeFlags(includeFlags);
        Filters[ClientState::NORMAL_ALLIANCE]->setExcludeFlags(excludeFlags);
        SetAllAreaCosts(Filters[ClientState::NORMAL_ALLIANCE].get(), 1.0f, roadCost, waterCost, badLiquidCost,
                        1.0f, factionDangerCost);

        // NORMAL_HORDE - Horde bot: Alliance areas are dangerous (allyMult = factionDangerCost)
        Filters[ClientState::NORMAL_HORDE]->setIncludeFlags(includeFlags);
        Filters[ClientState::NORMAL_HORDE]->setExcludeFlags(excludeFlags);
        SetAllAreaCosts(Filters[ClientState::NORMAL_HORDE].get(), 1.0f, roadCost, waterCost, badLiquidCost,
                        factionDangerCost, 1.0f);

        // DEAD - ghost mode, all areas cost 1.0 (free movement)
        Filters[ClientState::DEAD]->setIncludeFlags(includeFlags);
        Filters[ClientState::DEAD]->setExcludeFlags(excludeFlags);
        SetAllAreaCosts(Filters[ClientState::DEAD].get(), 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    ~AnpQueryFilterProvider() = default;

    virtual dtQueryFilter* Get(ClientState state) const noexcept override
    {
        auto it = Filters.find(state);
        return it != Filters.end() ? it->second.get() : nullptr;
    }
};
