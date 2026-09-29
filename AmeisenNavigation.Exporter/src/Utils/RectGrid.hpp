#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include "Vector3.hpp"

/// Axis aligned rectangle in RD coordinates (x/z plane).
struct RdRect
{
    float minX, maxX; // RD X bounds
    float minZ, maxZ; // RD Z bounds

    bool Overlaps(float qMinX, float qMinZ, float qMaxX, float qMaxZ) const noexcept
    {
        return maxX >= qMinX && minX <= qMaxX && maxZ >= qMinZ && minZ <= qMaxZ;
    }

    /// Build from WoW NW/SE corners (WoW x/y -> RD z/x), padded by eps to close float gaps between neighbors.
    static RdRect FromWow(const Vector3& wowNW, const Vector3& wowSE, float eps) noexcept
    {
        RdRect r;
        r.minX = std::min(wowNW.y, wowSE.y) - eps;
        r.maxX = std::max(wowNW.y, wowSE.y) + eps;
        r.minZ = std::min(wowNW.x, wowSE.x) - eps;
        r.maxZ = std::max(wowNW.x, wowSE.x) + eps;
        return r;
    }
};

/// Static uniform grid over a set of rects for fast "which rects overlap this box" queries.
///
/// Stored as CSR (one offsets array + one item array) so the index is compact and cache friendly.
/// Query() reports every overlapping rect exactly once, without any per-query allocation:
/// a rect spanning several cells is only reported from the first cell where it and the query overlap.
class RectGrid
{
    float CellSize = 1.0f;
    float OriginX = 0.0f;
    float OriginZ = 0.0f;
    int Width = 0;
    int Height = 0;
    std::vector<uint32_t> CellOffsets;
    std::vector<uint32_t> CellItems;

    int CellX(float x) const noexcept
    {
        return std::clamp(static_cast<int>(std::floor((x - OriginX) / CellSize)), 0, Width - 1);
    }

    int CellZ(float z) const noexcept
    {
        return std::clamp(static_cast<int>(std::floor((z - OriginZ) / CellSize)), 0, Height - 1);
    }

public:
    bool Empty() const noexcept { return Width == 0 || Height == 0; }

    template <typename Rect>
    void Build(const std::vector<Rect>& rects, float cellSize)
    {
        Width = 0;
        Height = 0;
        CellOffsets.clear();
        CellItems.clear();

        if (rects.empty() || !(cellSize > 0.0f))
        {
            return;
        }

        float minX = rects[0].minX, maxX = rects[0].maxX;
        float minZ = rects[0].minZ, maxZ = rects[0].maxZ;

        for (const auto& r : rects)
        {
            minX = std::min(minX, r.minX);
            maxX = std::max(maxX, r.maxX);
            minZ = std::min(minZ, r.minZ);
            maxZ = std::max(maxZ, r.maxZ);
        }

        CellSize = cellSize;
        OriginX = minX;
        OriginZ = minZ;
        Width = std::clamp(static_cast<int>(std::ceil((maxX - minX) / cellSize)) + 1, 1, 1 << 14);
        Height = std::clamp(static_cast<int>(std::ceil((maxZ - minZ) / cellSize)) + 1, 1, 1 << 14);

        // Pass 1: count, pass 2: fill (counting sort into CSR).
        CellOffsets.assign(static_cast<size_t>(Width) * Height + 1, 0);

        for (const auto& r : rects)
        {
            for (int z = CellZ(r.minZ), z1 = CellZ(r.maxZ); z <= z1; ++z)
            {
                for (int x = CellX(r.minX), x1 = CellX(r.maxX); x <= x1; ++x)
                {
                    CellOffsets[static_cast<size_t>(x + z * Width) + 1]++;
                }
            }
        }

        for (size_t i = 1; i < CellOffsets.size(); ++i)
        {
            CellOffsets[i] += CellOffsets[i - 1];
        }

        CellItems.resize(CellOffsets.back());
        std::vector<uint32_t> cursor(CellOffsets.begin(), CellOffsets.end() - 1);

        for (uint32_t i = 0; i < static_cast<uint32_t>(rects.size()); ++i)
        {
            const auto& r = rects[i];

            for (int z = CellZ(r.minZ), z1 = CellZ(r.maxZ); z <= z1; ++z)
            {
                for (int x = CellX(r.minX), x1 = CellX(r.maxX); x <= x1; ++x)
                {
                    CellItems[cursor[static_cast<size_t>(x + z * Width)]++] = i;
                }
            }
        }
    }

    /// Call fn(const Rect&) for every rect overlapping [minX, maxX] x [minZ, maxZ] (exactly once each).
    template <typename Rect, typename Fn>
    void Query(const std::vector<Rect>& rects, float minX, float minZ, float maxX, float maxZ, Fn&& fn) const
    {
        if (Empty())
        {
            return;
        }

        const int qx0 = CellX(minX), qx1 = CellX(maxX);
        const int qz0 = CellZ(minZ), qz1 = CellZ(maxZ);

        for (int z = qz0; z <= qz1; ++z)
        {
            for (int x = qx0; x <= qx1; ++x)
            {
                const size_t cell = static_cast<size_t>(x + z * Width);

                for (uint32_t k = CellOffsets[cell]; k < CellOffsets[cell + 1]; ++k)
                {
                    const auto& r = rects[CellItems[k]];

                    // Report each rect only from the first cell it shares with the query.
                    if (x != std::max(CellX(r.minX), qx0) || z != std::max(CellZ(r.minZ), qz0))
                    {
                        continue;
                    }

                    if (r.Overlaps(minX, minZ, maxX, maxZ))
                    {
                        fn(r);
                    }
                }
            }
        }
    }
};

/// Thread-safe collection of rects built during extraction (AddRect from many threads), then indexed once and
/// queried read-only by the tile builders.
template <typename Rect>
struct RectMap
{
    static constexpr float GRID_CELL_SIZE = 33.3334f; // ~CHUNKSIZE (TILESIZE / 16)

    std::vector<Rect> rects;
    std::mutex mutex;
    RectGrid grid;

    /// Rects from corrupt data (non-finite or absurd coordinates) are dropped, the grid math converts them to ints.
    void Add(const Rect& rect)
    {
        if (!IsPlausibleCoordinate(rect.minX) || !IsPlausibleCoordinate(rect.maxX)
            || !IsPlausibleCoordinate(rect.minZ) || !IsPlausibleCoordinate(rect.maxZ))
        {
            return;
        }

        std::lock_guard lock(mutex);
        rects.push_back(rect);
    }

    /// Must be called after all Add() calls and before any Query() call.
    void BuildSpatialIndex() { grid.Build(rects, GRID_CELL_SIZE); }

    template <typename Fn>
    void Query(float minX, float minZ, float maxX, float maxZ, Fn&& fn) const
    {
        if (grid.Empty())
        {
            // Not indexed (yet): linear scan.
            for (const auto& r : rects)
            {
                if (r.Overlaps(minX, minZ, maxX, maxZ))
                {
                    fn(r);
                }
            }

            return;
        }

        grid.Query(rects, minX, minZ, maxX, maxZ, std::forward<Fn>(fn));
    }
};
