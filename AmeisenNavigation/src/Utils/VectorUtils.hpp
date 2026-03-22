#pragma once

#include <cstring>

#include "Vector3.hpp"

/// Copy a Vector3 from vec[offset] to target[index].
__forceinline void InsertVector3At(Vector3* target, int index, const Vector3* vec, int offset = 0) noexcept
{
    memcpy(target + index, vec + offset, sizeof(Vector3));
}

/// Copy a Vector3 from vec[offset] to target[index], then increment index.
__forceinline void InsertVector3(Vector3* target, int& index, const Vector3* vec, int offset = 0) noexcept
{
    InsertVector3At(target, index, vec, offset);
    index++;
}

/// Remove Vector3 at target[index] by shifting subsequent elements left.
__forceinline void EraseVector3(Vector3* target, int count, int& index) noexcept
{
    memset(target + index, 0, sizeof(Vector3));
    memcpy(target + index, target + index + 1, (count - index - 1) * (sizeof(Vector3)));
    index--;
}

/// Compute output = vec0 * fac0 + vec1 * fac1 (component-wise).
__forceinline void ScaleAndAddVector3(const Vector3& vec0, float fac0, const Vector3& vec1, float fac1, Vector3& output) noexcept
{
    output[0] = vec0.x * fac0 + vec1.x * fac1;
    output[1] = vec0.y * fac0 + vec1.y * fac1;
    output[2] = vec0.z * fac0 + vec1.z * fac1;
}
