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

    /// <summary>
    /// Convert the recast and detour coordinates to wow coordinates.
    /// </summary>
    constexpr inline void ToWowCoords() noexcept
    {
        std::swap(pos[2], pos[1]);
        std::swap(pos[1], pos[0]);
    }

    /// <summary>
    /// Convert the recast and detour coordinates to wow coordinates.
    /// </summary>
    constexpr inline void CopyToWowCoords(float* out) const noexcept
    {
        out[0] = z;
        out[1] = x;
        out[2] = y;
    }

    /// <summary>
    /// Convert the wow coordinates to recast and detour coordinates.
    /// </summary>
    constexpr inline void ToRDCoords() noexcept
    {
        std::swap(pos[0], pos[1]);
        std::swap(pos[1], pos[2]);
    }

    /// <summary>
    /// Convert the wow coordinates to recast and detour coordinates.
    /// </summary>
    constexpr inline void CopyToRDCoords(float* out) const noexcept
    {
        out[0] = y;
        out[1] = z;
        out[2] = x;
    }
};
