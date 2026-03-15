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
/// Neutral areas get the base color, Alliance areas get a blue tint,
/// Horde areas get a red tint — while keeping the terrain type distinguishable.
inline void GetAreaColor(unsigned char area, uint8_t& r, uint8_t& g, uint8_t& b) noexcept
{
    r = 0;
    g = 0;
    b = 0;

    if (area == 0)
        return;

    // Determine faction: 0=neutral, 1=Alliance, 2=Horde
    // TriAreaId pattern: groups of 3 → (area-1)%3: 0=neutral, 1=Alliance, 2=Horde
    int faction = 0;
    unsigned char baseArea = area;
    if (area >= 1 && area <= HORDE_LIQUID_SLIME)
    {
        faction = (area - 1) % 3;    // 0=neutral, 1=ally, 2=horde
        baseArea = static_cast<unsigned char>(((area - 1) / 3) * 3 + 1); // map to neutral base
    }

    // Base color by terrain type
    switch (baseArea)
    {
        case TERRAIN_GROUND: r = 0;   g = 150; b = 0;   break; // green
        case TERRAIN_ROAD:   r = 100; g = 100; b = 100; break; // gray
        case TERRAIN_CITY:   r = 150; g = 150; b = 0;   break; // yellow
        case WMO:            r = 200; g = 200; b = 200; break; // light gray
        case DOODAD:         r = 139; g = 69;  b = 19;  break; // brown
        case LIQUID_WATER:
        case LIQUID_OCEAN:   r = 0;   g = 0;   b = 255; break; // blue
        case LIQUID_LAVA:    r = 255; g = 0;   b = 0;   break; // red
        case LIQUID_SLIME:   r = 150; g = 0;   b = 150; break; // magenta
        default:             r = 255; g = 255; b = 255; break; // white (unknown)
    }

    // Apply faction tint
    if (faction == 1) // Alliance — shift toward blue, reduce red
    {
        r = static_cast<uint8_t>(r * 0.4f);
        g = static_cast<uint8_t>(g * 0.6f);
        b = static_cast<uint8_t>(std::min(255.0f, b * 0.6f + 130.0f));
    }
    else if (faction == 2) // Horde — shift toward red, reduce blue
    {
        r = static_cast<uint8_t>(std::min(255.0f, r * 0.6f + 130.0f));
        g = static_cast<uint8_t>(g * 0.5f);
        b = static_cast<uint8_t>(b * 0.3f);
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
