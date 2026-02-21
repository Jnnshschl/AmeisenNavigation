#pragma once

#include <algorithm>
#include <mutex>
#include <vector>

#include "Vector3.hpp"

/// <summary>
/// Stores road coverage as world-space rectangles in RD coordinates.
/// Each rect represents a single terrain sub-cell marked as road.
/// The tile processor queries this to mark walkable spans on roads.
/// </summary>
struct RoadMap
{
    struct Rect
    {
        float minX, maxX; // RD X bounds
        float minZ, maxZ; // RD Z bounds
    };

    struct QueryResult
    {
        bool isRoad;
    };

    std::vector<Rect> rects;
    std::mutex mutex;

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
    /// Query road coverage at a world position in RD coordinates.
    /// </summary>
    inline bool QueryRD(float rdX, float rdZ) const noexcept
    {
        for (const auto& r : rects)
        {
            if (rdX >= r.minX && rdX <= r.maxX && rdZ >= r.minZ && rdZ <= r.maxZ)
                return true;
        }
        return false;
    }
};
