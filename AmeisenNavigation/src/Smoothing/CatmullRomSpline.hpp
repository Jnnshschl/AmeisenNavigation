#pragma once

#include <algorithm>
#include <cmath>

#include "../Utils/Path.hpp"
#include "../Utils/VectorUtils.hpp"

namespace CatmullRomSpline {
/// Knot interval for the parametrized Catmull-Rom spline (alpha 0 = uniform, 0.5 = centripetal, 1 = chordal).
/// Clamped to a small epsilon so duplicate points can't cause divisions by zero.
inline float KnotInterval(const Vector3& a, const Vector3& b, float halfAlpha) noexcept
{
    return std::max(std::pow((b - a).LengthSquared(), halfAlpha), 1e-4f);
}

/// Evaluate one Catmull-Rom segment between p1 and p2 at parameter t in [t1, t2] (Barry-Goldman pyramid).
inline Vector3 Evaluate(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t0, float t1,
                        float t2, float t3, float t) noexcept
{
    Vector3 a1, a2, a3, b1, b2, c;
    ScaleAndAddVector3(p0, (t1 - t) / (t1 - t0), p1, (t - t0) / (t1 - t0), a1);
    ScaleAndAddVector3(p1, (t2 - t) / (t2 - t1), p2, (t - t1) / (t2 - t1), a2);
    ScaleAndAddVector3(p2, (t3 - t) / (t3 - t2), p3, (t - t2) / (t3 - t2), a3);
    ScaleAndAddVector3(a1, (t2 - t) / (t2 - t0), a2, (t - t0) / (t2 - t0), b1);
    ScaleAndAddVector3(a2, (t3 - t) / (t3 - t1), a3, (t - t1) / (t3 - t1), b2);
    ScaleAndAddVector3(b1, (t2 - t) / (t2 - t1), b2, (t - t1) / (t2 - t1), c);
    return c;
}

/// Interpolating spline through every input point. Each segment is sampled with `points` samples, the
/// first/last segment use mirrored phantom control points so the curve starts and ends exactly at the
/// input endpoints. Output never exceeds its capacity and always ends with the last input point.
inline void SmoothPath(const Vector3* input, int inputSize, Path& output, int points, float alpha) noexcept
{
    output.Clear();

    if (inputSize <= 0)
    {
        return;
    }

    if (inputSize < 3 || points < 1)
    {
        for (int i = 0; i < inputSize; ++i)
        {
            output.TryAppend(input[i]);
        }

        return;
    }

    const float halfAlpha = std::clamp(alpha, 0.0f, 1.0f) * 0.5f;
    const Vector3 first = input[0] * 2.0f - input[1];
    const Vector3 last = input[inputSize - 1] * 2.0f - input[inputSize - 2];

    for (int i = 0; i < inputSize - 1; ++i)
    {
        const Vector3& p0 = i > 0 ? input[i - 1] : first;
        const Vector3& p1 = input[i];
        const Vector3& p2 = input[i + 1];
        const Vector3& p3 = i + 2 < inputSize ? input[i + 2] : last;

        const float t0 = 0.0f;
        const float t1 = t0 + KnotInterval(p0, p1, halfAlpha);
        const float t2 = t1 + KnotInterval(p1, p2, halfAlpha);
        const float t3 = t2 + KnotInterval(p2, p3, halfAlpha);

        // Sample [t1, t2), the segment end is the next segment's start (or the final point).
        for (int k = 0; k < points; ++k)
        {
            if (output.GetSpace() <= 1)
            {
                break;
            }

            const float t = t1 + (t2 - t1) * (static_cast<float>(k) / static_cast<float>(points));
            output.TryAppendUnique(k == 0 ? p1 : Evaluate(p0, p1, p2, p3, t0, t1, t2, t3, t));
        }
    }

    output.TryAppendUnique(input[inputSize - 1]);
}
} // namespace CatmullRomSpline
