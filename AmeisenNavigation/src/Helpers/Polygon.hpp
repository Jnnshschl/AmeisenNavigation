#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

#include "../Utils/Vector3.hpp"

/// 2D polygon helpers (x/y plane) used by EXPLORE_POLY.
namespace PolygonMath {
/// Even-odd point in polygon test in the x/y plane.
inline bool IsInside2D(const Vector3* vertices, int vertexCount, const Vector3& p) noexcept
{
    bool inside = false;

    for (int i = 0, j = vertexCount - 1; i < vertexCount; j = i++)
    {
        const Vector3& a = vertices[i];
        const Vector3& b = vertices[j];

        if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
        {
            inside = !inside;
        }
    }

    return inside;
}

/// Points of a hexagonal grid (`spacing` apart) inside a polygon. Every point of the polygon lies within
/// spacing / sqrt(3) of a sample, the densest coverage for the number of points. Deterministic.
/// Returns an empty vector if more than maxPointCount points would be needed. Polygons too narrow for
/// the grid get their vertex centroid (if it is inside).
inline std::vector<Vector3> HexGridSampling(std::span<const Vector3> polygon, float spacing, size_t maxPointCount)
{
    std::vector<Vector3> points;
    const int vertexCount = static_cast<int>(polygon.size());

    if (vertexCount < 3 || !(spacing > 0.0f) || !std::isfinite(spacing) || maxPointCount == 0)
    {
        return points;
    }

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    Vector3 centroid;

    for (const Vector3& v : polygon)
    {
        minX = std::min(minX, v.x);
        minY = std::min(minY, v.y);
        maxX = std::max(maxX, v.x);
        maxY = std::max(maxY, v.y);
        centroid.x += v.x / static_cast<float>(vertexCount);
        centroid.y += v.y / static_cast<float>(vertexCount);
    }

    const float rowHeight = spacing * std::numbers::sqrt3_v<float> / 2.0f;
    const double rows = std::floor((maxY - minY) / rowHeight) + 1.0;
    const double columns = std::floor((maxX - minX) / spacing) + 2.0;

    // Bail out before looping over a huge grid (the caller picks a bigger spacing).
    if (!std::isfinite(rows * columns) || rows * columns > static_cast<double>(maxPointCount) * 16.0 + 64.0)
    {
        return points;
    }

    // Center the grid inside the bounding box.
    const float y0 = minY + std::fmod(maxY - minY, rowHeight) / 2.0f;
    const float x0 = minX + std::fmod(maxX - minX, spacing) / 2.0f;

    for (int row = 0; row < static_cast<int>(rows); ++row)
    {
        const float y = y0 + static_cast<float>(row) * rowHeight;
        const float offset = (row % 2) ? spacing / 2.0f : 0.0f;

        for (int column = -1; column < static_cast<int>(columns); ++column)
        {
            const Vector3 p(x0 + offset + static_cast<float>(column) * spacing, y, 0.0f);

            if (p.x < minX || p.x > maxX || !IsInside2D(polygon.data(), vertexCount, p))
            {
                continue;
            }

            if (points.size() >= maxPointCount)
            {
                return {};
            }

            points.push_back(p);
        }
    }

    if (points.empty() && IsInside2D(polygon.data(), vertexCount, centroid))
    {
        points.push_back(centroid);
    }

    return points;
}
} // namespace PolygonMath
