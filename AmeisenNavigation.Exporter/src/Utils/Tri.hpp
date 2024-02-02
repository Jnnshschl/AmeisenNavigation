#pragma once

enum TriType : unsigned char
{
    TERRAIN_GROUND,
    LIQUID_WATER,
    LIQUID_OCEAN,
};

struct Tri
{
    unsigned char type;
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
        : type{ 0 },
        points{ 0, 0, 0 }
    {}

    Tri(unsigned char t, int a, int b, int c) noexcept
        : type{ t },
        points{ a, b, c }
    {}
};
