#pragma once

#include <algorithm>
#include <memory>

#include "Vector3.hpp"

/// Fixed-capacity point buffer used for path results. Allocated once per client and reused for every request.
struct Path
{
    const int maxSize;

private:
    // ownedPoints MUST be declared before points so it is constructed first.
    std::unique_ptr<Vector3[]> ownedPoints;

public:
    Vector3* points;
    int pointCount;

    explicit Path(int capacity)
        : maxSize(std::max(capacity, 1)),
          ownedPoints(std::make_unique<Vector3[]>(static_cast<size_t>(std::max(capacity, 1)))),
          points(ownedPoints.get()),
          pointCount(0)
    {
    }

    Path(const Path&) = delete;
    Path& operator=(const Path&) = delete;

    Vector3& operator[](int i) noexcept { return points[i]; }
    const Vector3& operator[](int i) const noexcept { return points[i]; }

    Vector3* begin() noexcept { return points; }
    Vector3* end() noexcept { return points + pointCount; }
    const Vector3* begin() const noexcept { return points; }
    const Vector3* end() const noexcept { return points + pointCount; }

    bool Empty() const noexcept { return pointCount == 0; }
    int GetSpace() const noexcept { return maxSize - pointCount; }
    bool IsFull() const noexcept { return pointCount >= maxSize; }
    void Clear() noexcept { pointCount = 0; }

    /// Append a point, returns false (and drops the point) when the buffer is full.
    bool TryAppend(const Vector3& v) noexcept
    {
        if (IsFull())
        {
            return false;
        }

        points[pointCount++] = v;
        return true;
    }

    /// Append a point unless it is identical to the last one. Returns false only when the buffer is full.
    bool TryAppendUnique(const Vector3& v) noexcept
    {
        if (pointCount > 0 && points[pointCount - 1] == v)
        {
            return true;
        }

        return TryAppend(v);
    }

    void ToWowCoords() noexcept
    {
        for (auto& point : *this)
        {
            point.ToWowCoords();
        }
    }

    void ToRdCoords() noexcept
    {
        for (auto& point : *this)
        {
            point.ToRDCoords();
        }
    }
};
