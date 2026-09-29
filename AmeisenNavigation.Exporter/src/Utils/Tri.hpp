#pragma once

#include <cstddef>
#include <functional>
#include <tuple>

// Area ids and poly flags are part of the ANP format, shared with the server.
#include "../../../AmeisenNavigation.Pack/src/AnpFormat.hpp"

/// Triangle as three vertex indices.
struct Tri
{
    union
    {
        struct
        {
            int a;
            int b;
            int c;
        };
        int points[3];
    };

    constexpr Tri() noexcept : points{0, 0, 0} {}

    constexpr Tri(int a, int b, int c) noexcept : points{a, b, c} {}

    constexpr Tri(size_t a, size_t b, size_t c) noexcept
        : points{static_cast<int>(a), static_cast<int>(b), static_cast<int>(c)}
    {
    }

    constexpr bool operator==(const Tri& other) const noexcept
    {
        return a == other.a && b == other.b && c == other.c;
    }

    constexpr bool operator<(const Tri& other) const noexcept
    {
        return std::tie(a, b, c) < std::tie(other.a, other.b, other.c);
    }

    struct Hash
    {
        size_t operator()(const Tri& tri) const noexcept
        {
            size_t hash = 17;
            hash = hash * 31 + std::hash<int>()(tri.a);
            hash = hash * 31 + std::hash<int>()(tri.b);
            hash = hash * 31 + std::hash<int>()(tri.c);
            return hash;
        }
    };
};

static_assert(sizeof(Tri) == sizeof(int) * 3, "Tri must be layout compatible with int[3] (Recast index format)");
