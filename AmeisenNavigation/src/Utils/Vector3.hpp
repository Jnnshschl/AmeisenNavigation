#pragma once

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <utility>

/// 3D float vector that is layout compatible with Detour's float[3].
///
/// Two coordinate systems are in play:
///   WoW:              (x = north, y = west, z = up)
///   Recast/Detour:    (x = wowY, y = wowZ (up), z = wowX)
/// Use ToRDCoords()/ToWowCoords() (in place) or CopyTo*Coords() (to a float buffer) to convert.
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

    constexpr Vector3() noexcept : pos{0.0f, 0.0f, 0.0f} {}
    constexpr Vector3(float x, float y, float z) noexcept : pos{x, y, z} {}
    constexpr explicit Vector3(const float* position) noexcept : pos{position[0], position[1], position[2]} {}

    constexpr operator float*() noexcept { return pos; }
    constexpr operator const float*() const noexcept { return pos; }

    constexpr bool operator==(const Vector3& other) const noexcept
    {
        return x == other.x && y == other.y && z == other.z;
    }

    constexpr Vector3 operator+(const Vector3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vector3 operator-(const Vector3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vector3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }

    constexpr float LengthSquared() const noexcept { return x * x + y * y + z * z; }
    float Length() const noexcept { return std::sqrt(LengthSquared()); }
    float DistanceTo(const Vector3& o) const noexcept { return (*this - o).Length(); }
    constexpr bool IsZero() const noexcept { return x == 0.0f && y == 0.0f && z == 0.0f; }

    bool IsFinite() const noexcept { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }

    /// Convert Recast/Detour coordinates to WoW coordinates (in place).
    constexpr Vector3& ToWowCoords() noexcept
    {
        std::swap(pos[2], pos[1]);
        std::swap(pos[1], pos[0]);
        return *this;
    }

    /// Convert Recast/Detour coordinates to WoW coordinates (copy to output buffer).
    constexpr void CopyToWowCoords(float* out) const noexcept
    {
        const float rx = x, ry = y, rz = z; // out may alias this
        out[0] = rz;
        out[1] = rx;
        out[2] = ry;
    }

    /// Convert WoW coordinates to Recast/Detour coordinates (in place).
    constexpr Vector3& ToRDCoords() noexcept
    {
        std::swap(pos[0], pos[1]);
        std::swap(pos[1], pos[2]);
        return *this;
    }

    /// Convert WoW coordinates to Recast/Detour coordinates (copy to output buffer).
    constexpr void CopyToRDCoords(float* out) const noexcept
    {
        const float wx = x, wy = y, wz = z; // out may alias this
        out[0] = wy;
        out[1] = wz;
        out[2] = wx;
    }

    /// Bitwise hash (distinguishes +0/-0, which is fine for deduplicating exact vertices).
    struct Hash
    {
        size_t operator()(const Vector3& v) const noexcept
        {
            uint64_t h = 0x9E3779B97F4A7C15ull;

            for (const float f : v.pos)
            {
                // + 0.0f turns -0.0f into +0.0f: equal vectors (operator==) must hash equal.
                h ^= static_cast<uint64_t>(std::bit_cast<uint32_t>(f + 0.0f)) + 0x9E3779B97F4A7C15ull + (h << 6)
                     + (h >> 2);
            }

            return static_cast<size_t>(h);
        }
    };
};

static_assert(sizeof(Vector3) == sizeof(float) * 3, "Vector3 must be layout compatible with float[3]");

template <>
struct std::formatter<Vector3> : std::formatter<std::string_view>
{
    auto format(const Vector3& v, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "[{}, {}, {}]", v.x, v.y, v.z);
    }
};
