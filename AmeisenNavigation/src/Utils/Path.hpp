#pragma once

#include "Vector3.hpp"
#include "VectorUtils.hpp"

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
    constexpr inline bool IsFull() const noexcept { return pointCount > maxSize - 1; }

    /// <summary>
    /// Appends a Vector3 to the path.
    /// </summary>
    inline void Append(Vector3* v3) noexcept
    {
        InsertVector3(points, pointCount, v3);
    }

    /// <summary>
    /// Adds Vector3 to the path if there is space for it.
    /// </summary>
    /// <param name="v3"></param>
    /// <returns></returns>
    inline bool TryAppend(Vector3* v3) noexcept
    {
        if (!IsFull())
        {
            InsertVector3(points, pointCount, v3);
            return true;
        }

        return false;
    }

    /// <summary>
    /// Convert the whole path's points to wow coordinates;
    /// </summary>
    constexpr inline void ToWowCoords() noexcept
    {
        for (auto& point : *this)
        {
            point.ToWowCoords();
        }
    }

    /// <summary>
    /// Convert the whole path's points to rd coordinates;
    /// </summary>
    constexpr inline void ToRdCoords() noexcept
    {
        for (auto& point : *this)
        {
            point.ToRDCoords();
        }
    }
};
