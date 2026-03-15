#pragma once

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

#include "Vector3.hpp"

/// Stores per-MCNK-chunk faction coverage as world-space rectangles in RD coordinates.
/// Each rect represents one MCNK chunk (CHUNKSIZE x CHUNKSIZE) with its faction affiliation.
/// The tile processor queries this to upgrade neutral area IDs to faction-specific variants.
///
/// Faction values:
///   0 = Neutral/Contested (not stored — skip)
///   1 = Alliance
///   2 = Horde
struct FactionMap
{
    struct Rect
    {
        float minX, maxX; // RD X bounds
        float minZ, maxZ; // RD Z bounds
        unsigned char faction; // 1=Alliance, 2=Horde
    };

    std::vector<Rect> rects;
    std::mutex mutex;

    /// Add a faction rectangle from WoW NW and SE corner positions (WoW coords).
    /// faction: 1=Alliance, 2=Horde (0=neutral should not be added).
    inline void AddRect(const Vector3& wowNW, const Vector3& wowSE, unsigned char faction) noexcept
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
        r.faction = faction;

        std::lock_guard<std::mutex> lock(mutex);
        rects.push_back(r);
    }
};
