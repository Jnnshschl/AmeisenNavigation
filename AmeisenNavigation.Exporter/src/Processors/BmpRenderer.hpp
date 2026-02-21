#pragma once

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>

#include "../../../recastnavigation/Recast/Include/Recast.h"

#include "../Utils/Bmp.hpp"
#include "../Utils/Tri.hpp"
#include "../Wow/AdtStructs.hpp"

// ─────────────────────────────────────────────
// BMP debug rendering for area visualization.
// ─────────────────────────────────────────────

/// Map an area ID to an RGB color for BMP debug output.
inline void GetAreaColor(unsigned char area, uint8_t& r, uint8_t& g, uint8_t& b) noexcept
{
    r = 0;
    g = 0;
    b = 0;
    switch (area)
    {
        case TERRAIN_GROUND:
        case ALLIANCE_TERRAIN_GROUND:
        case HORDE_TERRAIN_GROUND:
            r = 0;
            g = 150;
            b = 0;
            break;
        case TERRAIN_ROAD:
        case ALLIANCE_TERRAIN_ROAD:
        case HORDE_TERRAIN_ROAD:
            r = 100;
            g = 100;
            b = 100;
            break;
        case TERRAIN_CITY:
        case ALLIANCE_TERRAIN_CITY:
        case HORDE_TERRAIN_CITY:
            r = 150;
            g = 150;
            b = 0;
            break;
        case LIQUID_WATER:
        case ALLIANCE_LIQUID_WATER:
        case HORDE_LIQUID_WATER:
        case LIQUID_OCEAN:
        case ALLIANCE_LIQUID_OCEAN:
        case HORDE_LIQUID_OCEAN:
            r = 0;
            g = 0;
            b = 255;
            break;
        case LIQUID_LAVA:
        case ALLIANCE_LIQUID_LAVA:
        case HORDE_LIQUID_LAVA:
            r = 255;
            g = 0;
            b = 0;
            break;
        case LIQUID_SLIME:
        case ALLIANCE_LIQUID_SLIME:
        case HORDE_LIQUID_SLIME:
            r = 150;
            g = 0;
            b = 150;
            break;
        case WMO:
        case ALLIANCE_WMO:
        case HORDE_WMO:
            r = 200;
            g = 200;
            b = 200;
            break;
        case DOODAD:
        case ALLIANCE_DOODAD:
        case HORDE_DOODAD:
            r = 139;
            g = 69;
            b = 19;
            break;
        default:
            if (area != 0)
            {
                r = 255;
                g = 255;
                b = 255;
            }
            break;
    }
}

/// Render one sub-tile of the compact heightfield into the BMP pixel buffer.
inline void RenderSubTileToBmp(const rcCompactHeightfield* chf, uint8_t* adtPixels, int stX, int stY, int tileSize,
                               int borderSize, int width) noexcept
{
    if (!adtPixels)
        return;

    for (int y = 0; y < tileSize; ++y)
    {
        for (int x = 0; x < tileSize; ++x)
        {
            const rcCompactCell& c = chf->cells[(x + borderSize) + (y + borderSize) * width];
            if (c.count > 0)
            {
                unsigned char area = chf->areas[c.index + c.count - 1];
                uint8_t r, g, b;
                GetAreaColor(area, r, g, b);

                int adtX = stX * tileSize + x;
                int adtY = stY * tileSize + y;

                if (adtX >= 0 && adtX < 2560 && adtY >= 0 && adtY < 2560)
                {
                    int bufferY = 2559 - adtY;
                    int bufferX = 2559 - adtX;

                    int pIdx = (bufferX + bufferY * 2560) * 3;
                    adtPixels[pIdx + 0] = b;
                    adtPixels[pIdx + 1] = g;
                    adtPixels[pIdx + 2] = r;
                }
            }
        }
    }
}

/// Save the complete BMP debug image.
inline void SaveDebugBmp(const std::string& outputDir, int mapId, const float* bbMax, uint8_t* adtPixels) noexcept
{
    if (!adtPixels)
        return;

    int mapX = static_cast<int>(32.0f - (bbMax[0] / TILESIZE));
    int mapY = static_cast<int>(32.0f - (bbMax[2] / TILESIZE));

    std::filesystem::create_directories(std::format("{}/debug", outputDir));
    std::string filename = std::format("{}/debug/area_{}_{}_{}.bmp", outputDir, mapId, mapX, mapY);
    BmpWriter::Write(filename, 2560, 2560, adtPixels);
}
