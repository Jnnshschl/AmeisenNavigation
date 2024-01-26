#pragma once

#include "Vector3.hpp"

struct Path
{
    const int maxSize;
    Vector3* points;
    int pointCount;

    Path(int maxSize, Vector3* points) noexcept
        : maxSize(maxSize),
        points(points),
        pointCount(0)
    {}

    Path(int maxSize) noexcept
        : maxSize(maxSize),
        points(new Vector3[maxSize]),
        pointCount(0)
    {}

    constexpr inline auto& operator[](int i) noexcept { return points[i]; }
    constexpr inline Vector3* begin() const noexcept { return points; }
    constexpr inline Vector3* end() const noexcept { return points + pointCount; }
    constexpr inline int GetSpace() const noexcept { return maxSize - pointCount; }
};
