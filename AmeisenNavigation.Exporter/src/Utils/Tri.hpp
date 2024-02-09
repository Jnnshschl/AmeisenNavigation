#pragma once

enum TriAreaId : unsigned char
{
    NO_TYPE,

    LIQUID_LAVA,
    ALLIANCE_LIQUID_LAVA,
    HORDE_LIQUID_LAVA,

    LIQUID_SLIME,
    ALLIANCE_LIQUID_SLIME,
    HORDE_LIQUID_SLIME,

    LIQUID_OCEAN,
    ALLIANCE_LIQUID_OCEAN,
    HORDE_LIQUID_OCEAN,

    LIQUID_WATER,
    ALLIANCE_LIQUID_WATER,
    HORDE_LIQUID_WATER,

    TERRAIN_GROUND,
    ALLIANCE_TERRAIN_GROUND,
    HORDE_TERRAIN_GROUND,

    TERRAIN_ROAD,
    ALLIANCE_TERRAIN_ROAD,
    HORDE_TERRAIN_ROAD,

    TERRAIN_CITY,
    ALLIANCE_TERRAIN_CITY,
    HORDE_TERRAIN_CITY,

    WMO,
    ALLIANCE_WMO,
    HORDE_WMO,

    DOODAD,
    ALLIANCE_DOODAD,
    HORDE_DOODAD,
};

enum TriFlag : unsigned char
{
    NAV_EMPTY = 0,
    NAV_LAVA_SLIME = 1 << 0,
    NAV_WATER = 1 << 1,
    NAV_GROUND = 1 << 2,
    NAV_ROAD = 1 << 3,
    NAV_ALLIANCE = 1 << 4,
    NAV_HORDE = 1 << 5,
};

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

    Tri() noexcept
        : points{ 0, 0, 0 }
    {}

    Tri(int a, int b, int c) noexcept
        : points{ a, b, c }
    {}

    Tri(size_t a, size_t b, size_t c) noexcept
        : points{ static_cast<int>(a), static_cast<int>(b), static_cast<int>(c) }
    {}

    constexpr inline bool operator==(const Tri & other) const noexcept
    {
        return a == other.a && b == other.b && c == other.c;
    }
    
    constexpr inline bool operator<(const Tri& other) const {
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
