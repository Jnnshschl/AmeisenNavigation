#pragma once

#include "../Utils/Path.hpp"
#include "../Utils/VectorUtils.hpp"

namespace BezierCurve {
/// Cubic Bezier interpolation at parameter t in [0,1].
inline Vector3 Interpolate(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3,
                           float t) noexcept
{
    const float u = 1.0f - t;
    const float tt = t * t;
    const float uu = u * u;

    return p0 * (uu * u) + p1 * (3.0f * uu * t) + p2 * (3.0f * u * tt) + p3 * (tt * t);
}

/// Piecewise cubic Bezier: every group of 4 points (sharing their end points) forms one curve which is
/// sampled with `points` samples. Remaining points that don't form a full group are appended as they are,
/// so the path always ends at the destination. Output never exceeds its capacity.
inline void SmoothPath(const Vector3* input, int inputSize, Path& output, int points) noexcept
{
    output.Clear();

    if (inputSize <= 0)
    {
        return;
    }

    output.TryAppend(input[0]);

    const int samples = points < 2 ? 2 : points;
    int i = 0;

    for (; i + 3 < inputSize; i += 3)
    {
        for (int j = 1; j < samples; ++j)
        {
            // Keep one slot for the destination.
            if (output.GetSpace() <= 1)
            {
                break;
            }

            const float t = static_cast<float>(j) / static_cast<float>(samples - 1);
            output.TryAppendUnique(Interpolate(input[i], input[i + 1], input[i + 2], input[i + 3], t));
        }
    }

    for (int k = i + 1; k < inputSize; ++k)
    {
        const bool isLast = k == inputSize - 1;

        if (!isLast && output.GetSpace() <= 1)
        {
            continue;
        }

        if (isLast && output.IsFull())
        {
            output.pointCount--;
        }

        output.TryAppendUnique(input[k]);
    }
}
} // namespace BezierCurve
