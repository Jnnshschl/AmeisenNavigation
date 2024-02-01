#pragma once

#include <cstring>

#include "Vector3.hpp"

/// <summary>
/// Helper function to insert a vector3 into a float buffer.
/// </summary>
inline void InsertVector3At(Vector3* target, int index, const Vector3* vec, int offset = 0) noexcept
{
    memcpy(target + index, vec + offset, sizeof(Vector3));
}

/// <summary>
/// Helper function to insert a vector3 into a float buffer.
/// </summary>
inline void InsertVector3(Vector3* target, int& index, const Vector3* vec, int offset = 0) noexcept
{
    InsertVector3At(target, index, vec, offset);
    index++;
}

/// <summary>
/// Helper function to remove a vector3 from a float buffer.
/// </summary>
inline void EraseVector3(Vector3* target, int count, int& index) noexcept
{
    memset(target + index, 0, sizeof(Vector3));
    memcpy(target + index, target + index + 1, (count - index - 1) * (sizeof(Vector3)));
    index--;
}

/// <summary>
/// Helper function to scale two vectors and add them. 
/// Used by the smoothing algorithms.
/// </summary>
inline void ScaleAndAddVector3(const Vector3& vec0, float fac0, const Vector3& vec1, float fac1, Vector3& output) noexcept
{
    output[0] = vec0.x * fac0 + vec1.x * fac1;
    output[1] = vec0.y * fac0 + vec1.y * fac1;
    output[2] = vec0.z * fac0 + vec1.z * fac1;
}
