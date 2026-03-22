#pragma once

#include <format>
#include <functional>
#include <type_traits>

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

    Vector3() noexcept : pos{0.0f, 0.0f, 0.0f} {}

    Vector3(float* position) noexcept : pos{position[0], position[1], position[2]} {}

    Vector3(float x, float y, float z) noexcept : pos{x, y, z} {}

    constexpr inline operator float*() noexcept { return pos; }
    constexpr inline operator const float*() const noexcept { return pos; }

    constexpr inline bool operator==(const Vector3& other) const noexcept
    {
        return x == other.x && y == other.y && z == other.z;
    }

    /// Convert Recast/Detour coordinates to WoW coordinates (in-place).
    constexpr inline Vector3& ToWowCoords() noexcept
    {
        std::swap(pos[2], pos[1]);
        std::swap(pos[1], pos[0]);
        return *this;
    }

    /// Convert Recast/Detour coordinates to WoW coordinates (copy to output buffer).
    constexpr inline void CopyToWowCoords(float* out) const noexcept
    {
        out[0] = z;
        out[1] = x;
        out[2] = y;
    }

    /// Convert WoW coordinates to Recast/Detour coordinates (in-place).
    constexpr inline Vector3& ToRDCoords() noexcept
    {
        std::swap(pos[0], pos[1]);
        std::swap(pos[1], pos[2]);
        return *this;
    }

    /// Convert WoW coordinates to Recast/Detour coordinates (copy to output buffer).
    constexpr inline void CopyToRDCoords(float* out) const noexcept
    {
        out[0] = y;
        out[1] = z;
        out[2] = x;
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

template <>
struct std::formatter<Vector3> : std::formatter<string>
{
    auto format(const Vector3& v, format_context& ctx) const
    {
        return std::format_to(ctx.out(), "[{}, {}, {}]", v.x, v.y, v.z);
    }
};
