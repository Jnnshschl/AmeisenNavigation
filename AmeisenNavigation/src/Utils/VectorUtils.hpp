#pragma once

#include "Vector3.hpp"

/// Compute output = vec0 * fac0 + vec1 * fac1 (component-wise).
inline void ScaleAndAddVector3(const Vector3& vec0, float fac0, const Vector3& vec1, float fac1,
                               Vector3& output) noexcept
{
    output.x = vec0.x * fac0 + vec1.x * fac1;
    output.y = vec0.y * fac0 + vec1.y * fac1;
    output.z = vec0.z * fac0 + vec1.z * fac1;
}
