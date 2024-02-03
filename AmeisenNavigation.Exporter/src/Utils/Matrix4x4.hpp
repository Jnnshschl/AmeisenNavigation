#pragma once

#include <cmath>
#include <numbers>

#include "Vector3.hpp"

struct Matrix4x4
{
    float data[4][4];

    Matrix4x4() noexcept : data{ {1.0f, 0.0f, 0.0f, 0.0f},
                                {0.0f, 1.0f, 0.0f, 0.0f},
                                {0.0f, 0.0f, 1.0f, 0.0f},
                                {0.0f, 0.0f, 0.0f, 1.0f} }
    {}

    inline void Multiply(const Matrix4x4& other) noexcept
    {
        Matrix4x4 result;

        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                result.data[i][j] = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    result.data[i][j] += data[i][k] * other.data[k][j];
                }
            }
        }

        *this = result;
    }

    inline float DegreesToRadians(float degrees) noexcept
    {
        return std::fmodf(degrees, 360.0f) * std::numbers::pi_v<float> / 180.0f;
    }

    inline void SetRotation(const Vector3& axis) noexcept
    {
        float x = DegreesToRadians(axis.x);
        float y = DegreesToRadians(axis.y);
        float z = DegreesToRadians(axis.z);

        float length = std::sqrtf(x * x + y * y + z * z);

        if (length != 0.0f)
        {
            x /= length;
            y /= length;
            z /= length;
        }

        float c = std::cosf(length);
        float s = std::sinf(length);
        float t = 1.0f - c;

        float tx = t * x;
        float ty = t * y;
        float tz = t * z;
        float sx = s * x;
        float sy = s * y;
        float sz = s * z;

        data[0][0] = tx * x + c;
        data[0][1] = tx * y + sz;
        data[0][2] = tx * z - sy;

        data[1][0] = tx * y - sz;
        data[1][1] = ty * y + c;
        data[1][2] = ty * z + sx;

        data[2][0] = tx * z + sy;
        data[2][1] = ty * z - sx;
        data[2][2] = tz * z + c;
    }

    inline void SetRotation(float x, float y, float z, float w) noexcept
    {
        float length = std::sqrtf(x * x + y * y + z * z + w * w);

        if (length != 0.0f)
        {
            x /= length;
            y /= length;
            z /= length;
            w /= length;
        }

        float xx = x * x;
        float yy = y * y;
        float zz = z * z;
        float xy = x * y;
        float xz = x * z;
        float yz = y * z;
        float wx = w * x;
        float wy = w * y;
        float wz = w * z;

        data[0][0] = 1.0f - 2.0f * (yy + zz);
        data[0][1] = 2.0f * (xy + wz);
        data[0][2] = 2.0f * (xz - wy);

        data[1][0] = 2.0f * (xy - wz);
        data[1][1] = 1.0f - 2.0f * (xx + zz);
        data[1][2] = 2.0f * (yz + wx);

        data[2][0] = 2.0f * (xz + wy);
        data[2][1] = 2.0f * (yz - wx);
        data[2][2] = 1.0f - 2.0f * (xx + yy);
    }

    constexpr inline void SetScale(const Vector3& scale) noexcept
    {
        data[0][0] = scale.x;
        data[1][1] = scale.y;
        data[2][2] = scale.z;
    }

    constexpr inline void SetTranslation(const Vector3& translation) noexcept
    {
        data[3][0] = translation.x;
        data[3][1] = translation.y;
        data[3][2] = translation.z;
    }

    Vector3 Transform(const Vector3& vector) const noexcept
    {
        return {
            vector.x * data[0][0] + vector.y * data[1][0] + vector.z * data[2][0] + data[3][0],
            vector.x * data[0][1] + vector.y * data[1][1] + vector.z * data[2][1] + data[3][1],
            vector.x * data[0][2] + vector.y * data[1][2] + vector.z * data[2][2] + data[3][2]
        };
    }
};
