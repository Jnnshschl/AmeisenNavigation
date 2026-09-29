#pragma once

#include "../Utils/Path.hpp"
#include "../Utils/VectorUtils.hpp"

namespace ChaikinCurve {
/// Chaikin corner cutting (one iteration). Keeps the first and last point, replaces every segment
/// (a, b) by the points at 25% and 75%. Never writes past the output capacity; when the output is too
/// small the tail of the input is appended unsmoothed so the path still ends at the destination.
inline void SmoothPath(const Vector3* input, int inputSize, Path& output) noexcept
{
    output.Clear();

    if (inputSize <= 0)
    {
        return;
    }

    if (inputSize < 3)
    {
        for (int i = 0; i < inputSize; ++i)
        {
            output.TryAppend(input[i]);
        }

        return;
    }

    output.TryAppend(input[0]);

    Vector3 q;
    Vector3 r;

    for (int i = 0; i < inputSize - 1; ++i)
    {
        // Reserve one slot for the final point.
        if (output.GetSpace() < 3)
        {
            break;
        }

        ScaleAndAddVector3(input[i], 0.75f, input[i + 1], 0.25f, q);
        ScaleAndAddVector3(input[i], 0.25f, input[i + 1], 0.75f, r);
        output.TryAppend(q);
        output.TryAppend(r);
    }

    if (output.IsFull())
    {
        output.pointCount--;
    }

    output.TryAppend(input[inputSize - 1]);
}
} // namespace ChaikinCurve
