#pragma once

#include <random>
#include <cmath>
#include <numbers>

#include "../Utils/VectorUtils.hpp"

namespace PolygonMath
{
    constexpr inline bool IsInside2D(const float* vertices, int vertexCount, const float* p) noexcept
    {
        int count = 0;

        for (int i = 0; i < vertexCount * 3; i += 3)
        {
            int next = (i + 1) % vertexCount;

            if (((vertices[i + 1] <= p[1] && p[1] < vertices[next + 1]) || (vertices[next + 1] <= p[1] && p[1] < vertices[i + 1]))
                && (p[0] < (vertices[next] - vertices[i]) * (p[1] - vertices[i + 1]) / (vertices[next + 1] - vertices[i + 1]) + vertices[i]))
            {
                count++;
            }
        }

        return count % 2 == 1;
    }


    constexpr inline bool CheckMinDistance(const float* p, const float* points, const int pointCount, float minDistance) noexcept
    {
        for (int i = 0; i < pointCount * 3; i += 3)
        {
            if (dtVdist(p, points + i) < minDistance)
            {
                return false;
            }
        }

        return true;
    }

    /// <summary>
    /// Generate randomized nodes using the Bridson's Poisson Disk Sampling algorithm that cover the polygons area.
    /// </summary>
    /// <param name="vertices">Float vertices of the polygon.</param>
    /// <param name="vertexCount">Count of floats in the vertices buffer.</param>
    /// <param name="pointBuffer">Buffer where the points will be stored.</param>
    /// <param name="pointCount">Size of the point buffer.</param>
    /// <param name="tempBuffer">Temporary Buffer.</param>
    /// <param name="maxNodeCount">Max Node Count.</param>
    /// <param name="minDistance">Minimum distance between nodes</param>
    /// <param name="numCandidates"></param>
    static void BridsonsPoissonDiskSampling(const float* vertices, int vertexCount, float* pointBuffer, int* pointCount, float* tempBuffer, int maxNodeCount, float minDistance, int numCandidates = 30) noexcept
    {
        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_real_distribution<float> distribution(0.0, 1.0);

        auto maxX = std::numeric_limits<float>::min();
        auto maxY = std::numeric_limits<float>::min();
        auto minX = std::numeric_limits<float>::max();
        auto minY = std::numeric_limits<float>::max();

        for (int i = 0; i < vertexCount * 3; i += 3)
        {
            maxX = std::max(maxX, vertices[i]);
            maxY = std::max(maxY, vertices[i + 1]);
            minX = std::min(minX, vertices[i]);
            minY = std::min(minY, vertices[i + 1]);
        }

        float initialPoint[3]{ 0.0f, 0.0f, 0.0f };

        do
        {
            initialPoint[0] = distribution(rng) * (maxX - minX) + minX;
            initialPoint[1] = distribution(rng) * (maxY - minY) + minY;
        } while (!IsInside2D(vertices, vertexCount, initialPoint));

        *pointCount = 0;
        InsertVector3(pointBuffer, *pointCount, initialPoint);

        int activeCount = 0;
        InsertVector3(tempBuffer, activeCount, initialPoint);

        const auto cellSize = minDistance / std::sqrtf(2);
        const auto gridSizeX = static_cast<int>(std::ceilf((maxX - minX) / cellSize));
        const auto gridSizeY = static_cast<int>(std::ceilf((maxY - minY) / cellSize));

        std::vector<std::vector<bool>> grid(gridSizeX, std::vector<bool>(gridSizeY, false));

        while (activeCount > 0)
        {
            auto randomIndex = std::uniform_int_distribution<int>(0, static_cast<int>(vertexCount / 3) - 1)(rng) * 3;
            const auto currentPoint = tempBuffer + randomIndex;
            bool foundCandidate = false;

            for (int i = 0; i < numCandidates; ++i)
            {
                const auto angle = distribution(rng) * 2.0f * std::numbers::pi_v<float>;
                const auto distance = distribution(rng) * minDistance + minDistance;
                const auto cosAngle = std::cosf(angle);
                const auto sinAngle = std::sinf(angle);

                float candidate[3]
                {
                    currentPoint[0] + distance * cosAngle,
                    currentPoint[1] + distance * sinAngle,
                    0.0f
                };

                if (candidate[0] >= minX && candidate[0] <= maxX && candidate[1] >= minY && candidate[1] <= maxY)
                {
                    const auto gridX = static_cast<int>((candidate[0] - minX) / cellSize);
                    const auto gridY = static_cast<int>((candidate[1] - minY) / cellSize);

                    if (!grid[gridX][gridY] 
                        && IsInside2D(vertices, vertexCount, candidate) 
                        && CheckMinDistance(candidate, pointBuffer, *pointCount, minDistance))
                    {
                        InsertVector3(pointBuffer, *pointCount, candidate);
                        InsertVector3(tempBuffer, activeCount, candidate);
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
};
