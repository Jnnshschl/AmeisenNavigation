#pragma once

#include "../Utils/VectorUtils.hpp"
#include "../Utils/Vector3.hpp"

namespace CatmullRomSpline
{
    static void SmoothPath(const Vector3* input, int inputSize, Vector3* output, int* outputSize, int outputMaxSize, int points, float alpha) noexcept
    {
        InsertVector3(output, *outputSize, input, 0);

        Vector3 A1;
        Vector3 A2;
        Vector3 A3;

        Vector3 B1;
        Vector3 B2;

        Vector3 C;

        // make sure we dont go over the buffer bounds
        // why 3: we add 3 floats in the loop
        const int maxIndex = outputMaxSize - 1;

        for (int i = 1; i < inputSize - 2; ++i)
        {
            const Vector3& p0 = input[i - 1];
            const Vector3& p1 = input[i];
            const Vector3& p2 = input[i + 1];
            const Vector3& p3 = input[i + 2];

            const float t0 = 0.0f;

            const float t1 = std::powf(std::powf(p1.x - p0.x, 2.0f) + std::powf(p1.y - p0.y, 2.0f) + std::powf(p1.z - p0.z, 2.0f), alpha * 0.5f) + t0;
            const float t2 = std::powf(std::powf(p2.x - p1.x, 2.0f) + std::powf(p2.y - p1.y, 2.0f) + std::powf(p2.z - p1.z, 2.0f), alpha * 0.5f) + t1;
            const float t3 = std::powf(std::powf(p3.x - p2.x, 2.0f) + std::powf(p3.y - p2.y, 2.0f) + std::powf(p3.z - p2.z, 2.0f), alpha * 0.5f) + t2;

            for (float t = t1; t < t2; t += ((t2 - t1) / static_cast<float>(points)))
            {
                ScaleAndAddVector3(p0, (t1 - t) / (t1 - t0), p1, (t - t0) / (t1 - t0), A1);
                ScaleAndAddVector3(p1, (t2 - t) / (t2 - t1), p2, (t - t1) / (t2 - t1), A2);
                ScaleAndAddVector3(p2, (t3 - t) / (t3 - t2), p3, (t - t2) / (t3 - t2), A3);

                ScaleAndAddVector3(A1, (t2 - t) / (t2 - t0), A2, (t - t0) / (t2 - t0), B1);
                ScaleAndAddVector3(A2, (t3 - t) / (t3 - t1), A3, (t - t1) / (t3 - t1), B2);

                ScaleAndAddVector3(B1, (t2 - t) / (t2 - t1), B2, (t - t1) / (t2 - t1), C);

                if (!std::isnan(C.x) && !std::isnan(C.y) && !std::isnan(C.z))
                {
                    if (*outputSize > maxIndex) { return; }

                    InsertVector3(output, *outputSize, &C, 0);
                }
            }
        }
    }
}