#pragma once

#include "../Utils/VectorUtils.hpp"

namespace ChaikinCurve
{
    void SmoothPath(const float* input, int inputSize, float* output, int* outputSize, int outputMaxSize) noexcept
    {
        InsertVector3(output, *outputSize, input, 0);

        float result[3];

        for (int i = 0; i < inputSize - 3; i += 3)
        {
            ScaleAndAddVector3(input + i, 0.75f, input + i + 3, 0.25f, result);
            InsertVector3(output, *outputSize, result, 0);

            ScaleAndAddVector3(input + i, 0.25f, input + i + 3, 0.75f, result);
            InsertVector3(output, *outputSize, result, 0);

            if (*outputSize > outputMaxSize - 9) { break; }
        }

        InsertVector3(output, *outputSize, input, inputSize - 3);
    }
}
