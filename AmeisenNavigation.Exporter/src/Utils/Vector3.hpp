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

    constexpr inline bool operator==(const Vector3& other) const noexcept
    {
        return x == other.x && y == other.y && z == other.z;
    }

    /// <summary>
    /// Convert the recast and detour coordinates to wow coordinates.
    /// </summary>
    constexpr inline Vector3& ToWowCoords() noexcept
    {
        std::swap(pos[2], pos[1]);
        std::swap(pos[1], pos[0]);
        return *this;
    }

    /// <summary>
    /// Convert the wow coordinates to recast and detour coordinates.
    /// </summary>
    constexpr inline Vector3& ToRDCoords() noexcept
    {
        std::swap(pos[0], pos[1]);
        std::swap(pos[1], pos[2]);
        return *this;
    }

    struct Hash 
    {
        size_t operator()(const Vector3& v) const noexcept
        {
            size_t hash = 17;
            hash = hash * 31 + std::hash<float>()(v.x);
            hash = hash * 31 + std::hash<float>()(v.y);
            hash = hash * 31 + std::hash<float>()(v.z);
            return hash;
        }
    };
};
