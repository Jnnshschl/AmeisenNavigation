#pragma once

#include <algorithm>
#include <mutex>
#include <vector>

#include "Tri.hpp"
#include "Vector3.hpp"

/// <summary>
/// Stores water coverage as a list of world-space rectangles in RD coordinates.
/// Each rect represents a single liquid sub-cell with its 4 corner heights and type.
/// The tile processor queries this to mark walkable spans below water.
/// </summary>
struct WaterMap
{
    struct Rect
    {
        float minX, maxX; // RD X bounds
        float minZ, maxZ; // RD Z bounds
        // Water surface heights at the 4 corners (in RD Y / WoW Z).
        // Indexed as: [0]=minX,minZ  [1]=maxX,minZ  [2]=minX,maxZ  [3]=maxX,maxZ
        float heights[4];
        TriAreaId type; // LIQUID_WATER, LIQUID_OCEAN, LIQUID_LAVA, LIQUID_SLIME
    };

    struct QueryResult
    {
        float height; // Bilinearly interpolated water height at query position
        TriAreaId type;
    };

    std::vector<Rect> rects;
    std::mutex mutex;

    /// <summary>
    /// Add a water rectangle from WoW NW and SE corner positions with 4 corner heights.
    /// Heights are in WoW Z (= RD Y). Corners: h_nw_nw, h_nw_se = NW/SE of the water cell
    /// in WoW coords, mapped to RD axes.
    /// </summary>
    inline void AddRect(const Vector3& wowNW, const Vector3& wowSE, float hNW, float hNE, float hSW, float hSE,
                        TriAreaId type) noexcept
    {
        // Convert WoW XY corners to RD XZ
        // ToRDCoords: (wowX, wowY, wowZ) -> (wowY, wowZ, wowX)
        // So RD-X = wowY, RD-Z = wowX
        float rdX_NW = wowNW.y; // NW has higher wowY
        float rdX_SE = wowSE.y; // SE has lower wowY
        float rdZ_NW = wowNW.x; // NW has higher wowX
        float rdZ_SE = wowSE.x; // SE has lower wowX

        Rect r;
        constexpr float eps = 0.01f;
        r.minX = std::min(rdX_NW, rdX_SE) - eps;
        r.maxX = std::max(rdX_NW, rdX_SE) + eps;
        r.minZ = std::min(rdZ_NW, rdZ_SE) - eps;
        r.maxZ = std::max(rdZ_NW, rdZ_SE) + eps;

        // Map corner heights to the RD-aligned rect corners:
        // In WoW: NW=(highX, highY), NE=(highX, lowY), SW=(lowX, highY), SE=(lowX, lowY)
        // In RD:  rdX=wowY, rdZ=wowX
        //   WoW NW(highX,highY) -> RD(highX=maxZ, highY=maxX) = [maxX, maxZ] = index 3
        //   WoW NE(highX,lowY)  -> RD(highX=maxZ, lowY=minX)  = [minX, maxZ] = index 2
        //   WoW SW(lowX,highY)  -> RD(lowX=minZ, highY=maxX)  = [maxX, minZ] = index 1
        //   WoW SE(lowX,lowY)   -> RD(lowX=minZ, lowY=minX)   = [minX, minZ] = index 0
        r.heights[0] = hSE; // minX, minZ = WoW SE (lowX, lowY)
        r.heights[1] = hSW; // maxX, minZ = WoW SW (lowX, highY)
        r.heights[2] = hNE; // minX, maxZ = WoW NE (highX, lowY)
        r.heights[3] = hNW; // maxX, maxZ = WoW NW (highX, highY)

        r.type = type;

        std::lock_guard<std::mutex> lock(mutex);
        rects.push_back(r);
    }

    /// <summary>
    /// Query water at a world position in RD coordinates.
    /// Returns interpolated height and type, or nullptr if no water at this position.
    /// </summary>
    inline bool QueryRD(float rdX, float rdZ, QueryResult& result) const noexcept
    {
        for (const auto& r : rects)
        {
            if (rdX >= r.minX && rdX <= r.maxX && rdZ >= r.minZ && rdZ <= r.maxZ)
            {
                // Bilinear interpolation of water height within the rect
                float tx = (r.maxX > r.minX) ? (rdX - r.minX) / (r.maxX - r.minX) : 0.5f;
                float tz = (r.maxZ > r.minZ) ? (rdZ - r.minZ) / (r.maxZ - r.minZ) : 0.5f;

                // Clamp to [0, 1]
                tx = std::max(0.0f, std::min(1.0f, tx));
                tz = std::max(0.0f, std::min(1.0f, tz));

                // Bilinear: lerp along X at minZ, lerp along X at maxZ, then lerp along Z
                float hAtMinZ = r.heights[0] * (1.0f - tx) + r.heights[1] * tx; // minZ edge
                float hAtMaxZ = r.heights[2] * (1.0f - tx) + r.heights[3] * tx; // maxZ edge
                result.height = hAtMinZ * (1.0f - tz) + hAtMaxZ * tz;
                result.type = r.type;
                return true;
            }
        }
        return false;
    }
};
