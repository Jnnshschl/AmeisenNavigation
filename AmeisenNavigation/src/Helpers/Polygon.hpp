#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

#include "../Utils/Vector3.hpp"
#include "../Utils/VectorUtils.hpp"

/// 2D polygon helpers (x/y plane), groundwork for the planned EXPLORE_POLY request.
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

/// Generate evenly spaced sample points inside a polygon using Bridson's Poisson disk sampling.
/// Writes at most maxPointCount points to pointBuffer. tempBuffer must also hold maxPointCount points.
inline void BridsonsPoissonDiskSampling(const Vector3* vertices, int vertexCount, Vector3* pointBuffer,
                                        int* pointCount, Vector3* tempBuffer, int maxPointCount, float minDistance,
                                        int numCandidates = 30) noexcept
{
    *pointCount = 0;

    if (vertexCount < 3 || maxPointCount <= 0 || !(minDistance > 0.0f))
    {
        return;
    }

    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();

    for (int i = 0; i < vertexCount; ++i)
    {
        minX = std::min(minX, vertices[i].x);
        minY = std::min(minY, vertices[i].y);
        maxX = std::max(maxX, vertices[i].x);
        maxY = std::max(maxY, vertices[i].y);
    }

    // Find a start point inside the polygon (bounded attempts for degenerate polygons).
    Vector3 initialPoint;
    bool foundStart = false;

    for (int attempt = 0; attempt < 1000 && !foundStart; ++attempt)
    {
        initialPoint.x = distribution(rng) * (maxX - minX) + minX;
        initialPoint.y = distribution(rng) * (maxY - minY) + minY;
        foundStart = IsInside2D(vertices, vertexCount, initialPoint);
    }

    if (!foundStart)
    {
        return;
    }

    const float cellSize = minDistance / std::numbers::sqrt2_v<float>;
    const int gridW = std::max(1, static_cast<int>(std::ceil((maxX - minX) / cellSize)) + 1);
    const int gridH = std::max(1, static_cast<int>(std::ceil((maxY - minY) / cellSize)) + 1);

    // Grid stores index + 1 of the sample in each cell (0 = empty), a cell holds at most one sample.
    std::vector<int> grid(static_cast<size_t>(gridW) * static_cast<size_t>(gridH), 0);

    const auto cellOf = [&](const Vector3& p, int& gx, int& gy) {
        gx = std::clamp(static_cast<int>((p.x - minX) / cellSize), 0, gridW - 1);
        gy = std::clamp(static_cast<int>((p.y - minY) / cellSize), 0, gridH - 1);
    };

    const auto farEnough = [&](const Vector3& p) {
        int gx = 0, gy = 0;
        cellOf(p, gx, gy);

        for (int y = std::max(0, gy - 2); y <= std::min(gridH - 1, gy + 2); ++y)
        {
            for (int x = std::max(0, gx - 2); x <= std::min(gridW - 1, gx + 2); ++x)
            {
                if (const int idx = grid[static_cast<size_t>(x + y * gridW)])
                {
                    const Vector3& q = pointBuffer[idx - 1];
                    const float dx = q.x - p.x;
                    const float dy = q.y - p.y;

                    if (dx * dx + dy * dy < minDistance * minDistance)
                    {
                        return false;
                    }
                }
            }
        }

        return true;
    };

    const auto addPoint = [&](const Vector3& p, int& activeCount) {
        int gx = 0, gy = 0;
        cellOf(p, gx, gy);
        pointBuffer[*pointCount] = p;
        tempBuffer[activeCount++] = p;
        grid[static_cast<size_t>(gx + gy * gridW)] = ++(*pointCount);
    };

    int activeCount = 0;
    addPoint(initialPoint, activeCount);

    while (activeCount > 0 && *pointCount < maxPointCount)
    {
        const int randomIndex = std::uniform_int_distribution<int>(0, activeCount - 1)(rng);
        const Vector3 currentPoint = tempBuffer[randomIndex];
        bool foundCandidate = false;

        for (int i = 0; i < numCandidates; ++i)
        {
            const float angle = distribution(rng) * 2.0f * std::numbers::pi_v<float>;
            const float distance = minDistance * (1.0f + distribution(rng));
            const Vector3 candidate(currentPoint.x + distance * std::cos(angle),
                                    currentPoint.y + distance * std::sin(angle), 0.0f);

            if (candidate.x >= minX && candidate.x <= maxX && candidate.y >= minY && candidate.y <= maxY
                && IsInside2D(vertices, vertexCount, candidate) && farEnough(candidate))
            {
                addPoint(candidate, activeCount);
                foundCandidate = true;
                break;
            }
        }

        if (!foundCandidate)
        {
            EraseVector3(tempBuffer, activeCount, randomIndex);
        }
    }
}
} // namespace PolygonMath
