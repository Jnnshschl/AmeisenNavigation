#pragma once

enum class NavTerrainFlag335a : char
{
    EMPTY = 0,
    GROUND = 11,
    GROUND_STEEP = 10,
    WATER = 9,
    MAGMA_SLIME = 8,
};

enum class NavArea335a : char
{
    EMPTY = 0x00,
    GROUND = 1 << (static_cast<char>(NavTerrainFlag335a::GROUND) - static_cast<char>(NavTerrainFlag335a::GROUND)),
    GROUND_STEEP = 1 << (static_cast<char>(NavTerrainFlag335a::GROUND) - static_cast<char>(NavTerrainFlag335a::GROUND_STEEP)),
    WATER = 1 << (static_cast<char>(NavTerrainFlag335a::GROUND) - static_cast<char>(NavTerrainFlag335a::WATER)),
    MAGMA_SLIME = 1 << (static_cast<char>(NavTerrainFlag335a::GROUND) - static_cast<char>(NavTerrainFlag335a::MAGMA_SLIME))
};
