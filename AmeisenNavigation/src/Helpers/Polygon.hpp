#pragma once

#include <vector>
#include <random>
#include <cmath>
#include <numbers>

#include "Point.hpp"

struct Polygon
{
    std::vector<Point> vertices;

    Polygon(const std::vector<Point>& vertices) : vertices(vertices) {}

    bool isInside(const Point& p) const noexcept
    {
        int n = vertices.size();
        int count = 0;

        for (int i = 0; i < n; ++i)
        {
            int next = (i + 1) % n;

            if (((vertices[i].y <= p.y && p.y < vertices[next].y) || (vertices[next].y <= p.y && p.y < vertices[i].y))
                && (p.x < (vertices[next].x - vertices[i].x) * (p.y - vertices[i].y) / (vertices[next].y - vertices[i].y) + vertices[i].x))
            {
                count++;
            }
        }

        return count % 2 == 1;
    }

    /// <summary>
    /// Generate randomized nodes using the Bridson's Poisson Disk Sampling algorithm that cover the polygons area.
    /// </summary>
    /// <param name="minDistance">Minimum distance between nodes</param>
    /// <param name="numCandidates"></param>
    /// <returns></returns>
    std::vector<Point> bridsonsPoissonDiskSampling(float minDistance, int numCandidates = 30) const noexcept
    {
        std::vector<Point> points;
        std::vector<Point> activeList;
        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_real_distribution<float> distribution(0.0, 1.0);

        if (vertices.empty())
        {
            return points;
        }

        float maxX = std::numeric_limits<float>::min();
        float maxY = std::numeric_limits<float>::min();
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();

        for (const Point& vertex : vertices)
        {
            maxX = std::max(maxX, vertex.x);
            maxY = std::max(maxY, vertex.y);
            minX = std::min(minX, vertex.x);
            minY = std::min(minY, vertex.y);
        }

        Point initialPoint{ 0.0f, 0.0f };

        do
        {
            initialPoint = { distribution(rng) * (maxX - minX) + minX, distribution(rng) * (maxY - minY) + minY };
        } while ((!isInside(initialPoint)));

        points.emplace_back(initialPoint);
        activeList.emplace_back(initialPoint);

        const float cellSize = minDistance / std::sqrt(2);
        const int gridSizeX = static_cast<int>(std::ceil((maxX - minX) / cellSize));
        const int gridSizeY = static_cast<int>(std::ceil((maxY - minY) / cellSize));

        std::vector<std::vector<bool>> grid(gridSizeX, std::vector<bool>(gridSizeY, false));

        while (!activeList.empty())
        {
            int randomIndex = std::uniform_int_distribution<int>(0, activeList.size() - 1)(rng);
            Point currentPoint = activeList[randomIndex];
            bool foundCandidate = false;

            for (int i = 0; i < numCandidates; ++i)
            {
                float angle = distribution(rng) * 2.0f * std::numbers::pi_v<float>;
                float distance = distribution(rng) * minDistance + minDistance;
                float cosAngle = std::cos(angle);
                float sinAngle = std::sin(angle);

                Point candidate{ currentPoint.x + distance * cosAngle, currentPoint.y + distance * sinAngle };

                if (candidate.x >= minX && candidate.x <= maxX && candidate.y >= minY && candidate.y <= maxY)
                {
                    int gridX = static_cast<int>((candidate.x - minX) / cellSize);
                    int gridY = static_cast<int>((candidate.y - minY) / cellSize);

                    if (!grid[gridX][gridY] && isInside(candidate) && checkMinDistance(candidate, points, minDistance))
                    {
                        points.emplace_back(candidate);
                        activeList.emplace_back(candidate);
                        grid[gridX][gridY] = true;

                        foundCandidate = true;
                        break;
                    }
                }
            }

            if (!foundCandidate)
            {
                activeList.erase(activeList.begin() + randomIndex);
            }
        }

        return points;
    }

private:
    static bool checkMinDistance(const Point& p, const std::vector<Point>& points, float minDistance)
    {
        for (const Point& existingPoint : points)
        {
            float dist = std::sqrt((p.x - existingPoint.x) * (p.x - existingPoint.x) + (p.y - existingPoint.y) * (p.y - existingPoint.y));

            if (dist < minDistance)
            {
                return false;
            }
        }

        return true;
    }
};
