#pragma once

#include "RectGrid.hpp"
#include "Tri.hpp"
#include "Vector3.hpp"

/// One liquid sub-cell with its 4 corner heights and liquid type, in RD coordinates.
struct WaterRect : RdRect
{
    // Water surface heights at the 4 corners (in RD Y / WoW Z).
    // Indexed as: [0]=minX,minZ  [1]=maxX,minZ  [2]=minX,maxZ  [3]=maxX,maxZ
    float heights[4];
    TriAreaId type; // LIQUID_WATER, LIQUID_OCEAN, LIQUID_LAVA, LIQUID_SLIME

    float MaxHeight() const noexcept { return std::max({heights[0], heights[1], heights[2], heights[3]}); }
};

/// Water coverage of a map. The tile processor queries it to mark spans below the water surface.
struct WaterMap : RectMap<WaterRect>
{
    /// Add a water rectangle from WoW NW and SE corner positions with its 4 corner heights (WoW Z).
    void AddRect(const Vector3& wowNW, const Vector3& wowSE, float hNW, float hNE, float hSW, float hSE,
                 TriAreaId type)
    {
        // Pad rects slightly to avoid floating-point gaps between adjacent sub-cells.
        // 0.05 is about 1/4 of a navmesh cell (cs ~ 0.2083), so cell centers at the boundary
        // always fall within at least one rect.
        WaterRect r;
        static_cast<RdRect&>(r) = RdRect::FromWow(wowNW, wowSE, 0.05f);

        // Map corner heights to the RD-aligned rect corners (rdX = wowY, rdZ = wowX):
        //   WoW SE (lowX, lowY)   -> [minX, minZ] = index 0
        //   WoW SW (lowX, highY)  -> [maxX, minZ] = index 1
        //   WoW NE (highX, lowY)  -> [minX, maxZ] = index 2
        //   WoW NW (highX, highY) -> [maxX, maxZ] = index 3
        r.heights[0] = hSE;
        r.heights[1] = hSW;
        r.heights[2] = hNE;
        r.heights[3] = hNW;
        r.type = type;

        Add(r);
    }
};
