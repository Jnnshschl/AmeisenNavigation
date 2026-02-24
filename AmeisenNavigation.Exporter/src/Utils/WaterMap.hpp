#pragma once

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "Tri.hpp"
#include "Vector3.hpp"

/// <summary>
/// Stores water coverage as a list of world-space rectangles in RD coordinates.
/// Each rect represents a single liquid sub-cell with its 4 corner heights and type.
/// The tile processor queries this to mark walkable spans below water.
///
/// After all rects are added, call BuildSpatialIndex() before querying.
/// This builds a grid-based spatial index for O(1) cell lookup instead of O(n) linear scan.
/// When the spatial index misses but the query is within the global water bounds,
/// a brute-force linear scan is used as a fallback to guarantee correctness.
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

    // Spatial index (grid-based, built after all rects are added)
    static constexpr float GRID_CELL_SIZE = 33.3334f; // ~CHUNKSIZE (TILESIZE/16)
    float gridMinX = 0.0f, gridMinZ = 0.0f;
    float gridMaxX = 0.0f, gridMaxZ = 0.0f; // Global bounds for fallback
    int gridW = 0, gridH = 0;
    std::vector<std::vector<uint32_t>> gridCells;
    bool spatialIndexBuilt = false;

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
        // Pad rects slightly to avoid floating-point gaps between adjacent sub-cells.
        // 0.05 is about 1/4 of a navmesh cell (cs ≈ 0.2083), which ensures that cell
        // centers at the boundary always fall within at least one rect.
        constexpr float eps = 0.05f;
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
    /// Build a grid-based spatial index over all rects.
    /// Must be called after all AddRect() calls and before any QueryRD() calls.
    /// </summary>
    inline void BuildSpatialIndex() noexcept
    {
        if (rects.empty())
            return;

        // Compute global bounds from all rects
        gridMinX = rects[0].minX;
        gridMinZ = rects[0].minZ;
        gridMaxX = rects[0].maxX;
        gridMaxZ = rects[0].maxZ;

        for (const auto& r : rects)
        {
            gridMinX = std::min(gridMinX, r.minX);
            gridMinZ = std::min(gridMinZ, r.minZ);
            gridMaxX = std::max(gridMaxX, r.maxX);
            gridMaxZ = std::max(gridMaxZ, r.maxZ);
        }

        gridW = static_cast<int>(ceilf((gridMaxX - gridMinX) / GRID_CELL_SIZE)) + 1;
        gridH = static_cast<int>(ceilf((gridMaxZ - gridMinZ) / GRID_CELL_SIZE)) + 1;
        gridCells.resize(static_cast<size_t>(gridW) * gridH);

        // Insert each rect into all grid cells it overlaps.
        // Expand by 1 cell in each direction to handle floating-point boundary mismatches
        // between insertion and query (grid cell size ≈ chunk size, so accumulated float
        // error at chunk boundaries can cause rects to be indexed in a different cell than
        // the query maps to).
        for (uint32_t i = 0; i < static_cast<uint32_t>(rects.size()); ++i)
        {
            const auto& r = rects[i];
            int x0 = std::clamp(GridCellX(r.minX) - 1, 0, gridW - 1);
            int x1 = std::clamp(GridCellX(r.maxX) + 1, 0, gridW - 1);
            int z0 = std::clamp(GridCellZ(r.minZ) - 1, 0, gridH - 1);
            int z1 = std::clamp(GridCellZ(r.maxZ) + 1, 0, gridH - 1);

            for (int cx = x0; cx <= x1; ++cx)
                for (int cz = z0; cz <= z1; ++cz)
                    gridCells[cx + cz * gridW].push_back(i);
        }

        spatialIndexBuilt = true;
    }

    /// <summary>
    /// Query water at a world position in RD coordinates.
    /// Returns the highest interpolated water height and its type.
    /// Uses spatial index for fast lookup, with a brute-force linear scan fallback
    /// when the spatial index misses but the query is within the global water bounds.
    /// </summary>
    inline bool QueryRD(float rdX, float rdZ, QueryResult& result) const noexcept
    {
        if (spatialIndexBuilt)
        {
            int cx = GridCellX(rdX);
            int cz = GridCellZ(rdZ);

            // Fast path: spatial index lookup
            if (cx >= 0 && cx < gridW && cz >= 0 && cz < gridH)
            {
                const auto& cell = gridCells[cx + cz * gridW];
                bool found = false;

                for (uint32_t idx : cell)
                {
                    const auto& r = rects[idx];
                    if (rdX >= r.minX && rdX <= r.maxX && rdZ >= r.minZ && rdZ <= r.maxZ)
                    {
                        float h = InterpolateHeight(r, rdX, rdZ);
                        if (!found || h > result.height)
                        {
                            result.height = h;
                            result.type = r.type;
                            found = true;
                        }
                    }
                }

                if (found)
                    return true;
            }

            // Fallback: if the query is within the global water bounds but the spatial
            // index didn't find a match, do a linear scan. This catches any edge cases
            // from floating-point grid mapping issues.
            if (rdX >= gridMinX && rdX <= gridMaxX && rdZ >= gridMinZ && rdZ <= gridMaxZ)
            {
                return LinearScan(rdX, rdZ, result);
            }

            return false;
        }

        // No spatial index: full linear scan
        return LinearScan(rdX, rdZ, result);
    }

private:
    inline int GridCellX(float x) const noexcept
    {
        return static_cast<int>(floorf((x - gridMinX) / GRID_CELL_SIZE));
    }

    inline int GridCellZ(float z) const noexcept
    {
        return static_cast<int>(floorf((z - gridMinZ) / GRID_CELL_SIZE));
    }

    /// Linear scan of all rects — guaranteed correct but O(n).
    inline bool LinearScan(float rdX, float rdZ, QueryResult& result) const noexcept
    {
        bool found = false;
        for (const auto& r : rects)
        {
            if (rdX >= r.minX && rdX <= r.maxX && rdZ >= r.minZ && rdZ <= r.maxZ)
            {
                float h = InterpolateHeight(r, rdX, rdZ);
                if (!found || h > result.height)
                {
                    result.height = h;
                    result.type = r.type;
                    found = true;
                }
            }
        }
        return found;
    }

    static inline float InterpolateHeight(const Rect& r, float rdX, float rdZ) noexcept
    {
        float tx = (r.maxX > r.minX) ? (rdX - r.minX) / (r.maxX - r.minX) : 0.5f;
        float tz = (r.maxZ > r.minZ) ? (rdZ - r.minZ) / (r.maxZ - r.minZ) : 0.5f;

        tx = std::max(0.0f, std::min(1.0f, tx));
        tz = std::max(0.0f, std::min(1.0f, tz));

        // Bilinear: lerp along X at minZ, lerp along X at maxZ, then lerp along Z
        float hAtMinZ = r.heights[0] * (1.0f - tx) + r.heights[1] * tx; // minZ edge
        float hAtMaxZ = r.heights[2] * (1.0f - tx) + r.heights[3] * tx; // maxZ edge
        return hAtMinZ * (1.0f - tz) + hAtMaxZ * tz;
    }
};
