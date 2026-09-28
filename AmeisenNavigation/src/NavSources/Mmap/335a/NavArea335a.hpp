#pragma once

/// TrinityCore 3.3.5a MMAP area ids (MapDefines.h: NavArea).
enum class NavArea335a : unsigned char
{
    EMPTY = 0,
    MAGMA_SLIME = 8,
    WATER = 9,
    GROUND_STEEP = 10,
    GROUND = 11,
};

/// TrinityCore 3.3.5a MMAP polygon flags (MapDefines.h: NavTerrainFlag), derived from the area ids.
enum class NavFlag335a : unsigned short
{
    EMPTY = 0x00,
    GROUND = 1 << (static_cast<int>(NavArea335a::GROUND) - static_cast<int>(NavArea335a::GROUND)),
    GROUND_STEEP = 1 << (static_cast<int>(NavArea335a::GROUND) - static_cast<int>(NavArea335a::GROUND_STEEP)),
    WATER = 1 << (static_cast<int>(NavArea335a::GROUND) - static_cast<int>(NavArea335a::WATER)),
    MAGMA_SLIME = 1 << (static_cast<int>(NavArea335a::GROUND) - static_cast<int>(NavArea335a::MAGMA_SLIME)),
};

static_assert(static_cast<int>(NavFlag335a::GROUND) == 0x01 && static_cast<int>(NavFlag335a::WATER) == 0x04);
