#pragma once

#include <cmath>

#include "../Utils/VectorUtils.hpp"
#include "../Utils/Vector3.hpp"

namespace CatmullRomSpline
{
    /// Squared distance between two points.
    inline float DistSq(const Vector3& a, const Vector3& b) noexcept
    {
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    static void SmoothPath(const Vector3* input, int inputSize, Vector3* output, int* outputSize, int outputMaxSize, int points, float alpha) noexcept
    {
        InsertVector3(output, *outputSize, input, 0);

        const float halfAlpha = alpha * 0.5f;

        Vector3 A1, A2, A3;
        Vector3 B1, B2;
        Vector3 C;

        for (int i = 1; i < inputSize - 2; ++i)
        {
            const Vector3& p0 = input[i - 1];
            const Vector3& p1 = input[i];
            const Vector3& p2 = input[i + 1];
            const Vector3& p3 = input[i + 2];

            const float t0 = 0.0f;
            // powf(distSq, alpha*0.5) replaces powf(dx,2)+powf(dy,2)+... then powf(sum, alpha*0.5)
            const float t1 = std::powf(DistSq(p1, p0), halfAlpha) + t0;
            const float t2 = std::powf(DistSq(p2, p1), halfAlpha) + t1;
            const float t3 = std::powf(DistSq(p3, p2), halfAlpha) + t2;

            // Skip degenerate segments where consecutive points are identical
            if (t1 == t0 || t2 == t1 || t3 == t2 || t2 == t0 || t3 == t1)
                continue;

            // Precompute reciprocals to replace divisions in the inner loop
            const float inv_t1_t0 = 1.0f / (t1 - t0);
            const float inv_t2_t1 = 1.0f / (t2 - t1);
            const float inv_t3_t2 = 1.0f / (t3 - t2);
            const float inv_t2_t0 = 1.0f / (t2 - t0);
            const float inv_t3_t1 = 1.0f / (t3 - t1);
            const float step = (t2 - t1) / static_cast<float>(points);

            for (float t = t1; t < t2; t += step)
            {
                const float dt1 = t1 - t, dt2 = t2 - t, dt3 = t3 - t;
                const float ft0 = t - t0, ft1 = t - t1, ft2 = t - t2;

                ScaleAndAddVector3(p0, dt1 * inv_t1_t0, p1, ft0 * inv_t1_t0, A1);
                ScaleAndAddVector3(p1, dt2 * inv_t2_t1, p2, ft1 * inv_t2_t1, A2);
                ScaleAndAddVector3(p2, dt3 * inv_t3_t2, p3, ft2 * inv_t3_t2, A3);

                ScaleAndAddVector3(A1, dt2 * inv_t2_t0, A2, ft0 * inv_t2_t0, B1);
                ScaleAndAddVector3(A2, dt3 * inv_t3_t1, A3, ft1 * inv_t3_t1, B2);

                ScaleAndAddVector3(B1, dt2 * inv_t2_t1, B2, ft1 * inv_t2_t1, C);

                InsertVector3(output, *outputSize, &C, 0);

                if (*outputSize > outputMaxSize - 1) { return; }
            }
        }
    }
}