#pragma once

#include "../../../../AmeisenNavigation.Pack/src/AnpFormat.hpp"
#include "../../Clients/ClientState.hpp"
#include "../IQueryFilterProvider.hpp"

/// Default dtQueryFilters for ANP navmeshes.
///
/// All 27 area ids get explicit costs. Faction-specific filters multiply enemy faction area costs by
/// factionDangerCost, making bots avoid enemy territory (e.g. a Horde bot avoids Northshire Abbey).
/// Clients may override these defaults via CONFIGURE_FILTER messages.
class AnpQueryFilterProvider : public IQueryFilterProvider
{
public:
    struct Costs
    {
        float ground = 1.0f;
        float road = 0.75f;
        float water = 1.6f;
        float badLiquid = 4.0f;
        float allianceMultiplier = 1.0f;
        float hordeMultiplier = 1.0f;
    };

    explicit AnpQueryFilterProvider(float waterCost = 1.6f, float badLiquidCost = 4.0f, float roadCost = 0.75f,
                                    float factionDangerCost = 3.0f) noexcept
    {
        const unsigned short includeFlags =
            NAV_LAVA_SLIME | NAV_WATER | NAV_GROUND | NAV_ROAD | NAV_ALLIANCE | NAV_HORDE;

        for (int i = 0; i < ClientStateCount; ++i)
        {
            auto& filter = Filter(static_cast<ClientState>(i));
            filter.setIncludeFlags(includeFlags);
            filter.setExcludeFlags(0);
        }

        const Costs base{1.0f, roadCost, waterCost, badLiquidCost, 1.0f, 1.0f};

        ApplyCosts(Filter(ClientState::NORMAL), base);

        // Alliance bot: Horde areas are dangerous.
        Costs alliance = base;
        alliance.hordeMultiplier = factionDangerCost;
        ApplyCosts(Filter(ClientState::NORMAL_ALLIANCE), alliance);

        // Horde bot: Alliance areas are dangerous.
        Costs horde = base;
        horde.allianceMultiplier = factionDangerCost;
        ApplyCosts(Filter(ClientState::NORMAL_HORDE), horde);

        // Ghost: everything costs the same.
        ApplyCosts(Filter(ClientState::DEAD), Costs{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
    }

    /// Set costs for all 27 area ids. Bad liquids are expensive regardless of faction.
    static void ApplyCosts(dtQueryFilter& f, const Costs& c) noexcept
    {
        for (unsigned int area = TERRAIN_GROUND; area < ANP_AREA_COUNT; ++area)
        {
            float cost = c.ground;

            switch (GetNeutralArea(area))
            {
                case TERRAIN_ROAD: cost = c.road; break;
                case LIQUID_WATER:
                case LIQUID_OCEAN: cost = c.water; break;
                case LIQUID_LAVA:
                case LIQUID_SLIME: cost = c.badLiquid; break;
                default: break;
            }

            const bool isBadLiquid = GetNeutralArea(area) == LIQUID_LAVA || GetNeutralArea(area) == LIQUID_SLIME;

            if (!isBadLiquid)
            {
                switch (GetAreaFaction(area))
                {
                    case AreaFaction::Alliance: cost *= c.allianceMultiplier; break;
                    case AreaFaction::Horde: cost *= c.hordeMultiplier; break;
                    default: break;
                }
            }

            f.setAreaCost(static_cast<int>(area), cost);
        }
    }
};
