#pragma once

#include <cstring>

#include "Vector3.hpp"

/// Copy a Vector3 from vec[offset] to target[index].
inline void InsertVector3At(Vector3* target, int index, const Vector3* vec, int offset = 0) noexcept
{
    target[index] = vec[offset];
}

/// Copy a Vector3 from vec[offset] to target[index], then increment index.
inline void InsertVector3(Vector3* target, int& index, const Vector3* vec, int offset = 0) noexcept
{
    InsertVector3At(target, index, vec, offset);
    index++;
}

/// Remove target[index] by shifting the following elements left. Decrements count.
inline void EraseVector3(Vector3* target, int& count, int index) noexcept
{
    if (index < 0 || index >= count)
    {
        return;
    }

    std::memmove(target + index, target + index + 1, static_cast<size_t>(count - index - 1) * sizeof(Vector3));
    count--;
}

/// Compute output = vec0 * fac0 + vec1 * fac1 (component-wise).
inline void ScaleAndAddVector3(const Vector3& vec0, float fac0, const Vector3& vec1, float fac1,
                               Vector3& output) noexcept
{
    output.x = vec0.x * fac0 + vec1.x * fac1;
    output.y = vec0.y * fac0 + vec1.y * fac1;
    output.z = vec0.z * fac0 + vec1.z * fac1;
}
