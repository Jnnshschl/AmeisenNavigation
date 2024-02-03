#pragma once

enum TriType : unsigned char
{
    TERRAIN_GROUND,
    LIQUID_WATER,
    LIQUID_OCEAN,
    WMO
};

struct Tri
{
    unsigned char type;
    union
    {
        struct
        {
            size_t a;
            size_t b;
            size_t c;
        };
        size_t points[3];
    };

    Tri() noexcept
        : type{ 0 },
        points{ 0, 0, 0 }
    {}

    Tri(unsigned char t, size_t a, size_t b, size_t c) noexcept
        : type{ t },
        points{ a, b, c }
    {}
};
