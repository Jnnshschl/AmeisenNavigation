#pragma once

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
};
