#pragma once

#include "../Utils/VectorUtils.hpp"

namespace CatmullRomSpline
{
    inline float Interpolate(float p0, float p1, float p2, float p3, float t, float alpha) noexcept
    {
        float t2 = std::powf(t, alpha);
        float t3 = t2 * t;
        return 0.5f * ((2.0f * p1) +
            (-p0 + p2) * t +
            (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
            (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    }

    static void SmoothPath(const float* input, int inputSize, float* output, int* outputSize, int outputMaxSize, int points, float alpha) noexcept
    {
        InsertVector3(output, *outputSize, input, 0);

        float c[3]{};
        const float tDelta = 1.0f / points;

        for (int i = 3; i < inputSize - 6; i += 3)
        {
            const auto p0 = input + i - 3;
            const auto p1 = p0 + 3;
            const auto p2 = p1 + 3;
            const auto p3 = p2 + 3;

            for (int j = 0; j < points; ++j)
            {
                const float t = j * tDelta;

                for (int d = 0; d < 3; ++d)
                {
                    c[d] = Interpolate(p0[d], p1[d], p2[d], p3[d], t, alpha);
                }

                InsertVector3(output, *outputSize, c, 0);

                if (*outputSize > outputMaxSize - 3)
                {
                    break;
                }
            }
        }
    }
}