#pragma once

#include <cstring>

/// <summary>
/// Helper function to insert a vector3 into a float buffer.
/// </summary>
inline void InsertVector3(float* target, int& index, const float* vec, int offset = 0) noexcept
{
    memcpy(target + index, vec + offset, 3 * sizeof(float));
    index += 3;
}

/// <summary>
/// Helper function to remove a vector3 from a float buffer.
/// </summary>
inline void EraseVector3(float* target, int count, int& index) noexcept
{
    memset(target + index, 0, 3 * sizeof(float));    
    memcpy(target + index, target + index + 3, (count - index - 3) * (3 * sizeof(float)));
    index -= 3;
}

/// <summary>
/// Helper function to scale two vectors and add them. 
/// Used by the smoothing algorithms.
/// </summary>
inline void ScaleAndAddVector3(const float* vec0, float fac0, const float* vec1, float fac1, float* output) noexcept
{
    output[0] = vec0[0] * fac0 + vec1[0] * fac1;
    output[1] = vec0[1] * fac0 + vec1[1] * fac1;
    output[2] = vec0[2] * fac0 + vec1[2] * fac1;
}
