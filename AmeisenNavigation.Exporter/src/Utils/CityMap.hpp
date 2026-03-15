#pragma once

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

#include "Vector3.hpp"

/// Stores city area coverage as world-space rectangles in RD coordinates.
/// Each rect represents one MCNK chunk (CHUNKSIZE x CHUNKSIZE) within a city area.
/// The tile processor queries this to mark TERRAIN_GROUND spans as TERRAIN_CITY.
///
/// City detection uses AreaTable.dbc flags:
///   0x08 = Capital city (Stormwind, Orgrimmar, etc.)
///   0x20 = Slave capital / secondary town
struct CityMap
{
    struct Rect
    {
        float minX, maxX; // RD X bounds
        float minZ, maxZ; // RD Z bounds
    };

    std::vector<Rect> rects;
    std::mutex mutex;

    /// Add a city rectangle from WoW NW and SE corner positions (WoW coords).
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
};
