#pragma once

#include <cmath>
#include <numbers>
#include <random>


#include "../Utils/Vector3.hpp"
#include "../Utils/VectorUtils.hpp"


namespace PolygonMath {
constexpr inline bool IsInside2D(const Vector3* vertices, int vertexCount, const Vector3& p) noexcept
{
    int count = 0;

    for (int i = 0; i < vertexCount; ++i)
    {
        int next = (i + 1) % vertexCount;

        if (((vertices[i].y <= p.y && p.y < vertices[next].y) || (vertices[next].y <= p.y && p.y < vertices[i].y)) &&
            (p.x < (vertices[next].x - vertices[i].x) * (p.y - vertices[i].y) / (vertices[next].y - vertices[i].y) +
                       vertices[i].x))
        {
            count++;
        }
    }

    return count % 2 == 1;
}

constexpr inline bool CheckMinDistance(const Vector3& p, const Vector3* points, const int pointCount,
                                       float minDistance) noexcept
{
    for (int i = 0; i < pointCount; ++i)
    {
        if (dtVdist(p, points[i]) < minDistance)
        {
            return false;
        }
    }

    return true;
}

/// Generate evenly-spaced sample points inside a polygon using Bridson's Poisson Disk Sampling.
static void BridsonsPoissonDiskSampling(const Vector3* vertices, int vertexCount, Vector3* pointBuffer, int* pointCount,
                                        Vector3* tempBuffer, int maxNodeCount, float minDistance,
                                        int numCandidates = 30) noexcept
{
    thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);

    auto maxX = std::numeric_limits<float>::min();
    auto maxY = std::numeric_limits<float>::min();
    auto minX = std::numeric_limits<float>::max();
    auto minY = std::numeric_limits<float>::max();

    for (int i = 0; i < vertexCount; ++i)
    {
        maxX = std::max(maxX, vertices[i].x);
        maxY = std::max(maxY, vertices[i].y);
        minX = std::min(minX, vertices[i].x);
        minY = std::min(minY, vertices[i].y);
    }

    Vector3 initialPoint;

    do
    {
        initialPoint.x = distribution(rng) * (maxX - minX) + minX;
        initialPoint.y = distribution(rng) * (maxY - minY) + minY;
    } while (!IsInside2D(vertices, vertexCount, initialPoint));

    *pointCount = 0;
    InsertVector3(pointBuffer, *pointCount, &initialPoint);

    int activeCount = 0;
    InsertVector3(tempBuffer, activeCount, &initialPoint);

    const auto cellSize = minDistance / std::sqrtf(2.0f);
    const auto gridSizeX = static_cast<int>(std::ceilf((maxX - minX) / cellSize));
    const auto gridSizeY = static_cast<int>(std::ceilf((maxY - minY) / cellSize));

    std::vector<std::vector<bool>> grid(gridSizeX, std::vector<bool>(gridSizeY, false));

    while (activeCount > 0)
    {
        auto randomIndex = std::uniform_int_distribution<int>(0, activeCount - 1)(rng);
        const auto currentPoint = tempBuffer[randomIndex];
        bool foundCandidate = false;

        for (int i = 0; i < numCandidates; ++i)
        {
            const auto angle = distribution(rng) * 2.0f * std::numbers::pi_v<float>;
            const auto distance = distribution(rng) * minDistance + minDistance;
            const auto cosAngle = std::cosf(angle);
            const auto sinAngle = std::sinf(angle);

            Vector3 candidate(currentPoint.x + distance * cosAngle, currentPoint.y + distance * sinAngle, 0.0f);

            if (candidate[0] >= minX && candidate[0] <= maxX && candidate[1] >= minY && candidate[1] <= maxY)
            {
                const auto gridX = static_cast<int>((candidate.x - minX) / cellSize);
                const auto gridY = static_cast<int>((candidate.y - minY) / cellSize);

                if (!grid[gridX][gridY] && IsInside2D(vertices, vertexCount, candidate) &&
                    CheckMinDistance(candidate, pointBuffer, *pointCount, minDistance))
                {
                    InsertVector3(pointBuffer, *pointCount, &candidate);
                    InsertVector3(tempBuffer, activeCount, &candidate);
                    grid[gridX][gridY] = true;

                    foundCandidate = true;
                    break;
                }
            }
        }

        if (!foundCandidate)
        {
            EraseVector3(tempBuffer, activeCount, randomIndex);
        }
    }
}
}; // namespace PolygonMath
