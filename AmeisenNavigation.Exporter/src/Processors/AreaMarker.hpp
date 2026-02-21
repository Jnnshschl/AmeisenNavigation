#pragma once

#include <cstdint>

#include "../../../recastnavigation/Recast/Include/Recast.h"

#include "../Utils/RoadMap.hpp"
#include "../Utils/Tri.hpp"
#include "../Utils/WaterMap.hpp"

// ─────────────────────────────────────────────
// Area marking functions for compact heightfields.
// Each function marks spans with specific area IDs
// based on spatial queries against data maps.
// ─────────────────────────────────────────────

/// Mark spans that fall within water rectangles with the appropriate liquid area ID.
/// Must be called AFTER rcErodeWalkableArea/rcMedianFilterWalkableArea.
inline void MarkWaterAreas(rcCompactHeightfield* chf, const WaterMap* waterMap, int stX, int stY, bool debug,
                           rcContext* ctx) noexcept
{
    if (!waterMap)
        return;

    int dbgTotal = 0, dbgRectMatch = 0, dbgHeightMatch = 0;

    for (int z = 0; z < chf->height; ++z)
    {
        for (int x = 0; x < chf->width; ++x)
        {
            const rcCompactCell& c = chf->cells[x + z * chf->width];
            for (int i = (int)c.index, ni = (int)(c.index + c.count); i < ni; ++i)
            {
                dbgTotal++;

                float worldX = chf->bmin[0] + (x + 0.5f) * chf->cs;
                float worldZ = chf->bmin[2] + (z + 0.5f) * chf->cs;
                float spanTop = chf->bmin[1] + chf->spans[i].y * chf->ch;

                WaterMap::QueryResult water;
                if (waterMap->QueryRD(worldX, worldZ, water))
                {
                    dbgRectMatch++;
                    if (spanTop <= water.height + 1.0f)
                    {
                        dbgHeightMatch++;
                        chf->areas[i] = water.type;
                    }
                }
            }
        }
    }

    if (debug && ctx && (dbgRectMatch > 0 || (stX == 0 && stY == 0)))
    {
        ctx->log(RC_LOG_PROGRESS, "Water ST(%d,%d): spans=%d rectMatch=%d heightMatch=%d", stX, stY, dbgTotal,
                 dbgRectMatch, dbgHeightMatch);
    }
}

/// Mark walkable terrain spans that fall within road rectangles as TERRAIN_ROAD.
/// Only overwrites TERRAIN_GROUND or RC_WALKABLE_AREA — never overwrites water/lava.
inline void MarkRoadAreas(rcCompactHeightfield* chf, const RoadMap* roadMap) noexcept
{
    if (!roadMap)
        return;

    for (int z = 0; z < chf->height; ++z)
    {
        for (int x = 0; x < chf->width; ++x)
        {
            const rcCompactCell& c = chf->cells[x + z * chf->width];
            for (int i = (int)c.index, ni = (int)(c.index + c.count); i < ni; ++i)
            {
                unsigned char area = chf->areas[i];
                if (area == RC_WALKABLE_AREA || area == TERRAIN_GROUND)
                {
                    float worldX = chf->bmin[0] + (x + 0.5f) * chf->cs;
                    float worldZ = chf->bmin[2] + (z + 0.5f) * chf->cs;

                    if (roadMap->QueryRD(worldX, worldZ))
                    {
                        chf->areas[i] = TERRAIN_ROAD;
                    }
                }
            }
        }
    }
}
