#pragma once

#pragma once

struct Vector3
{
    union
    {
        struct
        {
            float x;
            float y;
            float z;
        };
        float pos[3];
    };

    Vector3() noexcept
        : pos{ 0.0f, 0.0f, 0.0f }
    {}

    Vector3(float* position) noexcept
        : pos{ position[0], position[1], position[2] }
    {}

    Vector3(float x, float y, float z) noexcept
        : pos{ x, y, z }
    {}

    constexpr inline operator float* () noexcept { return pos; }
    constexpr inline operator const float* () const noexcept { return pos; }
};
