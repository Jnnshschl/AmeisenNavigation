#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../../../recastnavigation/Recast/Include/Recast.h"

#include "../Utils/CityMap.hpp"
#include "../Utils/FactionMap.hpp"
#include "../Utils/RoadMap.hpp"
#include "../Utils/Tri.hpp"
#include "../Utils/WaterMap.hpp"

// ─────────────────────────────────────────────
// Area marking functions for compact heightfields.
// Each function marks spans with specific area IDs
// based on spatial queries against data maps.
// ─────────────────────────────────────────────

/// Mark spans that fall within water rectangles with the appropriate liquid area ID.
/// Uses direct rect-to-cell mapping: iterates water rects and directly computes which
/// compact heightfield cells they cover. This is more robust than per-cell WaterMap
/// queries because it eliminates all spatial index / lookup failure modes.
///
/// Must be called AFTER rcErodeWalkableArea/rcMedianFilterWalkableArea to restore
/// water areas that were cleared by erosion or median filter at boundaries.
inline void MarkWaterAreas(rcCompactHeightfield* chf, const WaterMap* waterMap, int stX, int stY, bool debug,
                           rcContext* ctx) noexcept
{
    if (!waterMap || waterMap->rects.empty())
        return;

    int dbgRectOverlap = 0, dbgMarked = 0;

    const float hfMaxX = chf->bmin[0] + chf->width * chf->cs;
    const float hfMaxZ = chf->bmin[2] + chf->height * chf->cs;

    for (const auto& rect : waterMap->rects)
    {
        // Skip rects that don't overlap with this sub-tile's heightfield
        if (rect.maxX < chf->bmin[0] || rect.minX > hfMaxX || rect.maxZ < chf->bmin[2] || rect.minZ > hfMaxZ)
            continue;

        dbgRectOverlap++;

        // Compute cell range covered by this rect (direct mapping, no spatial index)
        int minCX = std::max(0, static_cast<int>(floorf((rect.minX - chf->bmin[0]) / chf->cs)));
        int maxCX = std::min(chf->width - 1, static_cast<int>(floorf((rect.maxX - chf->bmin[0]) / chf->cs)));
        int minCZ = std::max(0, static_cast<int>(floorf((rect.minZ - chf->bmin[2]) / chf->cs)));
        int maxCZ = std::min(chf->height - 1, static_cast<int>(floorf((rect.maxZ - chf->bmin[2]) / chf->cs)));

        // Use max water height of the rect's 4 corners + tolerance.
        // The tolerance accounts for cell height quantization (ch ~ 0.2) and ensures
        // spans at the water surface are reliably caught.
        float maxWaterH = std::max({rect.heights[0], rect.heights[1], rect.heights[2], rect.heights[3]}) + 2.0f;

        for (int z = minCZ; z <= maxCZ; ++z)
        {
            for (int x = minCX; x <= maxCX; ++x)
            {
                const rcCompactCell& c = chf->cells[x + z * chf->width];
                for (int i = static_cast<int>(c.index), ni = static_cast<int>(c.index + c.count); i < ni; ++i)
                {
                    // Skip spans already correctly marked as liquid
                    unsigned char currentArea = chf->areas[i];
                    if (currentArea >= LIQUID_WATER && currentArea <= HORDE_LIQUID_SLIME)
                        continue;

                    float spanTop = chf->bmin[1] + chf->spans[i].y * chf->ch;
                    if (spanTop <= maxWaterH)
                    {
                        chf->areas[i] = rect.type;
                        dbgMarked++;
                    }
                }
            }
        }
    }

    if (debug && ctx && (dbgMarked > 0 || (stX == 0 && stY == 0)))
    {
        ctx->log(RC_LOG_PROGRESS, "Water ST(%d,%d): rectsOverlapping=%d spansMarked=%d", stX, stY, dbgRectOverlap,
                 dbgMarked);
    }
}

/// Mark walkable terrain spans that fall within road rectangles as TERRAIN_ROAD.
/// Uses direct rect-to-cell mapping for efficiency: iterates road rects and directly
/// computes which compact heightfield cells they cover.
/// Only overwrites TERRAIN_GROUND or RC_WALKABLE_AREA — never overwrites water/lava.
inline void MarkRoadAreas(rcCompactHeightfield* chf, const RoadMap* roadMap) noexcept
{
    if (!roadMap || roadMap->rects.empty())
        return;

    const float hfMaxX = chf->bmin[0] + chf->width * chf->cs;
    const float hfMaxZ = chf->bmin[2] + chf->height * chf->cs;

    for (const auto& rect : roadMap->rects)
    {
        // Skip rects that don't overlap with this sub-tile's heightfield
        if (rect.maxX < chf->bmin[0] || rect.minX > hfMaxX || rect.maxZ < chf->bmin[2] || rect.minZ > hfMaxZ)
            continue;

        // Compute cell range covered by this rect
        int minCX = std::max(0, static_cast<int>(floorf((rect.minX - chf->bmin[0]) / chf->cs)));
        int maxCX = std::min(chf->width - 1, static_cast<int>(floorf((rect.maxX - chf->bmin[0]) / chf->cs)));
        int minCZ = std::max(0, static_cast<int>(floorf((rect.minZ - chf->bmin[2]) / chf->cs)));
        int maxCZ = std::min(chf->height - 1, static_cast<int>(floorf((rect.maxZ - chf->bmin[2]) / chf->cs)));

        for (int z = minCZ; z <= maxCZ; ++z)
        {
            for (int x = minCX; x <= maxCX; ++x)
            {
                const rcCompactCell& c = chf->cells[x + z * chf->width];
                for (int i = static_cast<int>(c.index), ni = static_cast<int>(c.index + c.count); i < ni; ++i)
                {
                    unsigned char area = chf->areas[i];
                    if (area == RC_WALKABLE_AREA || area == TERRAIN_GROUND)
                    {
                        chf->areas[i] = TERRAIN_ROAD;
                    }
                }
            }
        }
    }
}

/// Mark walkable terrain spans that fall within city rectangles as TERRAIN_CITY.
/// Uses direct rect-to-cell mapping. Only upgrades TERRAIN_GROUND or RC_WALKABLE_AREA —
/// never overwrites roads, water, WMO, or doodad areas.
///
/// Must be called AFTER MarkRoadAreas so that roads within cities remain TERRAIN_ROAD.
/// Must be called BEFORE MarkFactionAreas so that TERRAIN_CITY gets its faction variant.
inline void MarkCityAreas(rcCompactHeightfield* chf, const CityMap* cityMap) noexcept
{
    if (!cityMap || cityMap->rects.empty())
        return;

    const float hfMaxX = chf->bmin[0] + chf->width * chf->cs;
    const float hfMaxZ = chf->bmin[2] + chf->height * chf->cs;

    for (const auto& rect : cityMap->rects)
    {
        // Skip rects that don't overlap with this sub-tile's heightfield
        if (rect.maxX < chf->bmin[0] || rect.minX > hfMaxX || rect.maxZ < chf->bmin[2] || rect.minZ > hfMaxZ)
            continue;

        // Compute cell range covered by this rect
        int minCX = std::max(0, static_cast<int>(floorf((rect.minX - chf->bmin[0]) / chf->cs)));
        int maxCX = std::min(chf->width - 1, static_cast<int>(floorf((rect.maxX - chf->bmin[0]) / chf->cs)));
        int minCZ = std::max(0, static_cast<int>(floorf((rect.minZ - chf->bmin[2]) / chf->cs)));
        int maxCZ = std::min(chf->height - 1, static_cast<int>(floorf((rect.maxZ - chf->bmin[2]) / chf->cs)));

        for (int z = minCZ; z <= maxCZ; ++z)
        {
            for (int x = minCX; x <= maxCX; ++x)
            {
                const rcCompactCell& c = chf->cells[x + z * chf->width];
                for (int i = static_cast<int>(c.index), ni = static_cast<int>(c.index + c.count); i < ni; ++i)
                {
                    unsigned char area = chf->areas[i];
                    if (area == RC_WALKABLE_AREA || area == TERRAIN_GROUND)
                    {
                        chf->areas[i] = TERRAIN_CITY;
                    }
                }
            }
        }
    }
}

/// Mark walkable spans in faction-controlled areas with faction-specific area IDs.
/// Upgrades neutral base area IDs to their Alliance (+1) or Horde (+2) variant.
///
/// TriAreaId values follow a repeating pattern of 3: base, Alliance, Horde.
/// A neutral base area has (areaId - 1) % 3 == 0. This function adds the
/// faction offset (1 or 2) to convert it to the corresponding faction variant.
///
/// Must be called AFTER MarkWaterAreas and MarkRoadAreas so that all base
/// area IDs are finalized before faction upgrading.
inline void MarkFactionAreas(rcCompactHeightfield* chf, const FactionMap* factionMap) noexcept
{
    if (!factionMap || factionMap->rects.empty())
        return;

    const float hfMaxX = chf->bmin[0] + chf->width * chf->cs;
    const float hfMaxZ = chf->bmin[2] + chf->height * chf->cs;

    for (const auto& rect : factionMap->rects)
    {
        // Skip rects that don't overlap with this sub-tile's heightfield
        if (rect.maxX < chf->bmin[0] || rect.minX > hfMaxX || rect.maxZ < chf->bmin[2] || rect.minZ > hfMaxZ)
            continue;

        // Compute cell range covered by this rect
        int minCX = std::max(0, static_cast<int>(floorf((rect.minX - chf->bmin[0]) / chf->cs)));
        int maxCX = std::min(chf->width - 1, static_cast<int>(floorf((rect.maxX - chf->bmin[0]) / chf->cs)));
        int minCZ = std::max(0, static_cast<int>(floorf((rect.minZ - chf->bmin[2]) / chf->cs)));
        int maxCZ = std::min(chf->height - 1, static_cast<int>(floorf((rect.maxZ - chf->bmin[2]) / chf->cs)));

        for (int z = minCZ; z <= maxCZ; ++z)
        {
            for (int x = minCX; x <= maxCX; ++x)
            {
                const rcCompactCell& c = chf->cells[x + z * chf->width];
                for (int i = static_cast<int>(c.index), ni = static_cast<int>(c.index + c.count); i < ni; ++i)
                {
                    unsigned char area = chf->areas[i];

                    // Skip null/unwalkable areas and already-faction-marked areas.
                    // Valid TriAreaId range: 1 (TERRAIN_GROUND) to 27 (HORDE_LIQUID_SLIME).
                    // Neutral base areas satisfy: (area - 1) % 3 == 0
                    if (area == 0 || area > HORDE_LIQUID_SLIME)
                        continue;

                    if ((area - 1) % 3 == 0)
                    {
                        // Upgrade neutral base to faction variant
                        chf->areas[i] = area + rect.faction;
                    }
                }
            }
        }
    }
}
