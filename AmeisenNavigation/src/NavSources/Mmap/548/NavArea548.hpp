#pragma once

/// SkyFire 5.4.8 MMAP terrain types, used as area id *and* polygon flag.
enum class NavArea548 : unsigned char
{
    EMPTY = 0,
    GROUND = 1,
    MAGMA = 2,
    SLIME = 4,
    WATER = 8,
};
