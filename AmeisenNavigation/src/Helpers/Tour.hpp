#pragma once

#include <algorithm>
#include <numeric>
#include <span>
#include <vector>

#include "../Utils/Vector3.hpp"

namespace Tour {
/// Order points into a short open tour beginning at `start`: nearest neighbour construction improved by
/// 2-opt moves (segment reversals) until no move helps or maxPasses is reached. Returns point indices.
inline std::vector<int> Order(const Vector3& start, std::span<const Vector3> points, int maxPasses = 8)
{
    const int n = static_cast<int>(points.size());
    std::vector<int> order;
    order.reserve(points.size());

    // Nearest neighbour.
    std::vector<bool> used(points.size(), false);
    Vector3 current = start;

    for (int step = 0; step < n; ++step)
    {
        int best = -1;
        float bestDistance = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            if (used[static_cast<size_t>(i)])
            {
                continue;
            }

            const float d = current.DistanceTo(points[static_cast<size_t>(i)]);

            if (best < 0 || d < bestDistance)
            {
                best = i;
                bestDistance = d;
            }
        }

        used[static_cast<size_t>(best)] = true;
        order.push_back(best);
        current = points[static_cast<size_t>(best)];
    }

    // 2-opt on the open path start -> order[0] -> ... -> order[n-1] (the start stays fixed, the end is free).
    const auto at = [&](int position) -> const Vector3& {
        return position < 0 ? start : points[static_cast<size_t>(order[static_cast<size_t>(position)])];
    };

    for (int pass = 0; pass < maxPasses; ++pass)
    {
        bool improved = false;

        for (int i = 0; i < n - 1; ++i)
        {
            for (int j = i + 1; j < n; ++j)
            {
                // Reverse order[i..j]: edges (i-1, i) and (j, j+1) become (i-1, j) and (i, j+1).
                float delta = at(i - 1).DistanceTo(at(j)) - at(i - 1).DistanceTo(at(i));

                if (j + 1 < n)
                {
                    delta += at(i).DistanceTo(at(j + 1)) - at(j).DistanceTo(at(j + 1));
                }

                if (delta < -1e-3f)
                {
                    std::reverse(order.begin() + i, order.begin() + j + 1);
                    improved = true;
                }
            }
        }

        if (!improved)
        {
            break;
        }
    }

    return order;
}

/// Length of the open tour start -> points[order[0]] -> ...
inline float Length(const Vector3& start, std::span<const Vector3> points, const std::vector<int>& order)
{
    float length = 0.0f;
    Vector3 current = start;

    for (const int i : order)
    {
        length += current.DistanceTo(points[static_cast<size_t>(i)]);
        current = points[static_cast<size_t>(i)];
    }

    return length;
}
} // namespace Tour
