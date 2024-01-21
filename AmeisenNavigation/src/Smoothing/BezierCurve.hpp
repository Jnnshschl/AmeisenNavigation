#pragma once

#include "../../recastnavigation/Detour/Include/DetourCommon.h"

#include "../Utils/VectorUtils.hpp"

namespace BezierCurve
{
    /// <summary>
    /// Interpolate points using a Bezier-Curve.
    /// </summary>
    /// <param name="p0">Point0.</param>
    /// <param name="p1">Point1.</param>
    /// <param name="p2">Point2.</param>
    /// <param name="p3">Point3.</param>
    /// <param name="p">Interpolated point.</param>
    /// <param name="t">Curve progress.</param>
    inline void Interpolate(const float* p0, const float* p1, const float* p2, const float* p3, float* p, float t) noexcept
    {
        const float u = 1.0f - t;
        const float tt = t * t;
        const float uu = u * u;
        const float uuu = uu * u;
        const float ttt = tt * t;

        float pTemp[3]{ 0.0f };

        // (1-t)^3 * P0
        dtVscale(p, p0, uuu);

        // 3(1-t)^2 * t * P1
        dtVscale(pTemp, p1, 3.0f * uu * t);
        dtVadd(p, p, pTemp);

        // 3(1-t) * t^2 * P2
        dtVscale(pTemp, p2, 3.0f * u * tt);
        dtVadd(p, p, pTemp);

        // t^3 * P3
        dtVscale(pTemp, p3, ttt);
        dtVadd(p, p, pTemp);
    }

    static void SmoothPath(const float* input, int inputSize, float* output, int* outputSize, int outputMaxSize, int points) noexcept
    {
        float c[3]{};

        for (int i = 0; i < inputSize - 9; i += 9)
        {
            const auto* p0 = input + i;
            const auto* p1 = input + i + 3;
            const auto* p2 = input + i + 6;
            const auto* p3 = input + i + 9;

            for (int j = 0; j < points; ++j)
            {
                Interpolate(p0, p1, p2, p3, c, static_cast<float>(j) / static_cast<float>(points - 1));
                InsertVector3(output, *outputSize, c, 0);

                if (*outputSize > outputMaxSize - 3) { return; }
            }
        }
    }
}
