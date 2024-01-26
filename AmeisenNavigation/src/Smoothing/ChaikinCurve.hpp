#pragma once

#include "../Utils/VectorUtils.hpp"
#include "../Utils/Vector3.hpp"

namespace ChaikinCurve
{
    static void SmoothPath(const Vector3* input, int inputSize, Vector3* output, int* outputSize, int outputMaxSize) noexcept
    {
        InsertVector3(output, *outputSize, input, 0);

        Vector3 result;

        for (int i = 0; i < inputSize - 1; i += 1)
        {
            ScaleAndAddVector3(input[i], 0.75f, input[i + 1], 0.25f, result);
            InsertVector3(output, *outputSize, &result, 0);

            ScaleAndAddVector3(input[i], 0.25f, input[i + 1], 0.75f, result);
            InsertVector3(output, *outputSize, &result, 0);

            if (*outputSize > outputMaxSize - 1) { break; }
        }

        InsertVector3(output, *outputSize, input, inputSize - 1);
    }
}
