#pragma once

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

#include "Vector3.hpp"

/// <summary>
/// Stores road coverage as world-space rectangles in RD coordinates.
/// Each rect represents a single terrain sub-cell marked as road.
/// The tile processor queries this to mark walkable spans on roads.
///
/// After all rects are added, call BuildSpatialIndex() before querying.
/// This builds a grid-based spatial index for O(1) cell lookup instead of O(n) linear scan.
/// </summary>
struct RoadMap
{
    struct Rect
    {
        float minX, maxX; // RD X bounds
        float minZ, maxZ; // RD Z bounds
    };

    std::vector<Rect> rects;
    std::mutex mutex;

    // Spatial index (grid-based, built after all rects are added)
    static constexpr float GRID_CELL_SIZE = 33.3334f; // ~CHUNKSIZE (TILESIZE/16)
    float gridMinX = 0.0f, gridMinZ = 0.0f;
    int gridW = 0, gridH = 0;
    std::vector<std::vector<uint32_t>> gridCells;
    bool spatialIndexBuilt = false;

    /// <summary>
    /// Add a road rectangle from WoW NW and SE corner positions (WoW coords).
    /// </summary>
    inline void AddRect(const Vector3& wowNW, const Vector3& wowSE) noexcept
    {
        // Convert WoW XY corners to RD XZ
        // ToRDCoords: (wowX, wowY, wowZ) -> (wowY, wowZ, wowX)
        float rdX_NW = wowNW.y;
        float rdX_SE = wowSE.y;
        float rdZ_NW = wowNW.x;
        float rdZ_SE = wowSE.x;

        Rect r;
        constexpr float eps = 0.01f;
        r.minX = std::min(rdX_NW, rdX_SE) - eps;
        r.maxX = std::max(rdX_NW, rdX_SE) + eps;
        r.minZ = std::min(rdZ_NW, rdZ_SE) - eps;
        r.maxZ = std::max(rdZ_NW, rdZ_SE) + eps;

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

        gridMinX = rects[0].minX;
        gridMinZ = rects[0].minZ;
        float gridMaxX = rects[0].maxX;
        float gridMaxZ = rects[0].maxZ;

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
    /// Query road coverage at a world position in RD coordinates.
    /// Uses spatial index for fast O(1) lookup when available.
    /// </summary>
    inline bool QueryRD(float rdX, float rdZ) const noexcept
    {
        if (spatialIndexBuilt)
        {
            int cx = GridCellX(rdX);
            int cz = GridCellZ(rdZ);

            if (cx >= 0 && cx < gridW && cz >= 0 && cz < gridH)
            {
                const auto& cell = gridCells[cx + cz * gridW];
                for (uint32_t idx : cell)
                {
                    const auto& r = rects[idx];
                    if (rdX >= r.minX && rdX <= r.maxX && rdZ >= r.minZ && rdZ <= r.maxZ)
                        return true;
                }
            }
            return false;
        }

        // No spatial index: full linear scan
        for (const auto& r : rects)
        {
            if (rdX >= r.minX && rdX <= r.maxX && rdZ >= r.minZ && rdZ <= r.maxZ)
                return true;
        }
        return false;
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
};
