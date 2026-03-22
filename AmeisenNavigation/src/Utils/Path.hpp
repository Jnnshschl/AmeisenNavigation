#pragma once

#include <memory>

#include "Vector3.hpp"
#include "VectorUtils.hpp"

struct Path
{
    const int maxSize;

private:
    // ownedPoints MUST be declared before points so it is constructed first
    // (C++ initializes members in declaration order, not initializer-list order).
    std::unique_ptr<Vector3[]> ownedPoints;

public:
    Vector3* points;
    int pointCount;

    Path(int maxSize)
        : maxSize(maxSize),
        ownedPoints(std::make_unique<Vector3[]>(maxSize)),
        points(ownedPoints.get()),
        pointCount(0)
    {}

    ~Path() = default;

    constexpr inline auto& operator[](int i) noexcept { return points[i]; }

    constexpr inline Vector3* begin() const noexcept { return points; }
    constexpr inline Vector3* end() const noexcept { return points + pointCount; }

    constexpr inline int GetSpace() const noexcept { return maxSize - pointCount; }
    constexpr inline bool IsFull() const noexcept { return pointCount > maxSize - 1; }

    inline void Append(Vector3* v3) noexcept
    {
        InsertVector3(points, pointCount, v3);
    }

    inline bool TryAppend(Vector3* v3) noexcept
    {
        if (!IsFull())
        {
            InsertVector3(points, pointCount, v3);
            return true;
        }

        return false;
    }

    constexpr inline void ToWowCoords() noexcept
    {
        for (auto& point : *this)
        {
            point.ToWowCoords();
        }
    }

    constexpr inline void ToRdCoords() noexcept
    {
        for (auto& point : *this)
        {
            point.ToRDCoords();
        }
    }
};
