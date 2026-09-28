#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <Recast.h>

#include "../Utils/CityMap.hpp"
#include "../Utils/FactionMap.hpp"
#include "../Utils/RoadMap.hpp"
#include "../Utils/Tri.hpp"
#include "../Utils/WaterMap.hpp"

// ─────────────────────────────────────────────
// Area marking functions for compact heightfields.
// Each function marks spans with specific area IDs based on spatial queries against the data maps.
// Only the rects overlapping the heightfield are visited (spatial index), so the cost per sub-tile
// is independent of the map size.
// ─────────────────────────────────────────────

namespace AreaMarkerDetail {
/// Cell range of the heightfield covered by an RD rect (clamped to the heightfield).
struct CellRange
{
    int minX, maxX, minZ, maxZ;

    bool Empty() const noexcept { return minX > maxX || minZ > maxZ; }
};

inline CellRange ToCellRange(const rcCompactHeightfield* chf, const RdRect& rect) noexcept
{
    return {std::max(0, static_cast<int>(std::floor((rect.minX - chf->bmin[0]) / chf->cs))),
            std::min(chf->width - 1, static_cast<int>(std::floor((rect.maxX - chf->bmin[0]) / chf->cs))),
            std::max(0, static_cast<int>(std::floor((rect.minZ - chf->bmin[2]) / chf->cs))),
            std::min(chf->height - 1, static_cast<int>(std::floor((rect.maxZ - chf->bmin[2]) / chf->cs)))};
}

/// Visit every rect of the map overlapping the heightfield.
template <typename Map, typename Fn>
void ForEachOverlappingRect(const rcCompactHeightfield* chf, const Map* map, Fn&& fn)
{
    if (!map || map->rects.empty())
    {
        return;
    }

    const float hfMaxX = chf->bmin[0] + chf->width * chf->cs;
    const float hfMaxZ = chf->bmin[2] + chf->height * chf->cs;
    map->Query(chf->bmin[0], chf->bmin[2], hfMaxX, hfMaxZ, std::forward<Fn>(fn));
}

/// Visit every span index of the cells covered by the rect.
template <typename Fn>
void ForEachSpan(const rcCompactHeightfield* chf, const RdRect& rect, Fn&& fn)
{
    const CellRange range = ToCellRange(chf, rect);

    if (range.Empty())
    {
        return;
    }

    for (int z = range.minZ; z <= range.maxZ; ++z)
    {
        for (int x = range.minX; x <= range.maxX; ++x)
        {
            const rcCompactCell& c = chf->cells[x + z * chf->width];

            for (int i = static_cast<int>(c.index), end = static_cast<int>(c.index + c.count); i < end; ++i)
            {
                fn(i, end);
            }
        }
    }
}
} // namespace AreaMarkerDetail

/// Mark spans that lie below the surface of water rects with the rect's liquid area id.
///
/// Must be called AFTER rcErodeWalkableArea/rcMedianFilterWalkableArea to restore water areas that were cleared by
/// erosion or the median filter at shores. Returns the number of marked spans.
inline int MarkWaterAreas(rcCompactHeightfield* chf, const WaterMap* waterMap) noexcept
{
    int marked = 0;

    AreaMarkerDetail::ForEachOverlappingRect(chf, waterMap, [&](const WaterRect& rect) {
        // Use the max water height of the rect's 4 corners + tolerance. The tolerance accounts for cell height
        // quantization (ch ~ 0.2) so spans at the water surface are reliably caught, larger values would mark
        // shoreline terrain as water.
        const float maxWaterH = rect.MaxHeight() + 0.5f;

        AreaMarkerDetail::ForEachSpan(chf, rect, [&](int i, int spanEnd) {
            const unsigned char currentArea = chf->areas[i];

            // Skip spans already marked as liquid and never overwrite structural geometry (bridges, docks,
            // WMO buildings, doodads) with water, those stay walkable ground even below the water level.
            if (IsLiquidArea(currentArea) || IsStructureArea(currentArea))
            {
                return;
            }

            const float spanTop = chf->bmin[1] + chf->spans[i].y * chf->ch;

            if (spanTop > maxWaterH)
            {
                return;
            }

            // Bridge check: if structural geometry sits directly above this span (within 3 cells), the span is
            // part of the bridge/dock surface and must not become water. Terrain further below still becomes
            // water so agents can swim under bridges.
            for (int j = i + 1; j < spanEnd; ++j)
            {
                if (IsStructureArea(chf->areas[j]))
                {
                    const float aboveTop = chf->bmin[1] + chf->spans[j].y * chf->ch;

                    if (aboveTop - spanTop <= chf->ch * 3.0f)
                    {
                        return;
                    }
                }
            }

            chf->areas[i] = rect.type;
            marked++;
        });
    });

    return marked;
}

/// Mark walkable terrain spans inside road rects as TERRAIN_ROAD.
/// Only overwrites TERRAIN_GROUND or RC_WALKABLE_AREA, never water/lava/structures.
inline void MarkRoadAreas(rcCompactHeightfield* chf, const RoadMap* roadMap) noexcept
{
    AreaMarkerDetail::ForEachOverlappingRect(chf, roadMap, [&](const RdRect& rect) {
        AreaMarkerDetail::ForEachSpan(chf, rect, [&](int i, int) {
            const unsigned char area = chf->areas[i];

            if (area == RC_WALKABLE_AREA || area == TERRAIN_GROUND)
            {
                chf->areas[i] = TERRAIN_ROAD;
            }
        });
    });
}

/// Mark walkable terrain spans inside city rects as TERRAIN_CITY.
/// Only upgrades TERRAIN_GROUND or RC_WALKABLE_AREA, never roads, water, WMO or doodads.
///
/// Must be called AFTER MarkRoadAreas so that roads within cities remain TERRAIN_ROAD, and BEFORE
/// MarkFactionAreas so that TERRAIN_CITY gets its faction variant.
inline void MarkCityAreas(rcCompactHeightfield* chf, const CityMap* cityMap) noexcept
{
    AreaMarkerDetail::ForEachOverlappingRect(chf, cityMap, [&](const RdRect& rect) {
        AreaMarkerDetail::ForEachSpan(chf, rect, [&](int i, int) {
            const unsigned char area = chf->areas[i];

            if (area == RC_WALKABLE_AREA || area == TERRAIN_GROUND)
            {
                chf->areas[i] = TERRAIN_CITY;
            }
        });
    });
}

/// Upgrade neutral area ids inside faction rects to their Alliance/Horde variant.
/// Must be called AFTER all other marking so that all base area ids are final.
inline void MarkFactionAreas(rcCompactHeightfield* chf, const FactionMap* factionMap) noexcept
{
    AreaMarkerDetail::ForEachOverlappingRect(chf, factionMap, [&](const FactionRect& rect) {
        AreaMarkerDetail::ForEachSpan(chf, rect, [&](int i, int) {
            const unsigned char area = chf->areas[i];

            if (IsValidAnpArea(area) && GetAreaFaction(area) == AreaFaction::Neutral)
            {
                chf->areas[i] = WithFaction(area, rect.faction);
            }
        });
    });
}
