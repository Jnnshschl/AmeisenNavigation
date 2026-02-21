#pragma once

#include <cctype>
#include <cstring>
#include <string>
#include <unordered_set>

#include "../Utils/RoadMap.hpp"
#include "../Utils/Vector3.hpp"
#include "Adt.hpp"

// ─────────────────────────────────────────────
// Road detection via ADT terrain texture analysis.
//
// Approach: MCNK.predTex stores an 8×8 grid of 2-bit layer indices
// telling which texture layer (0-3) is dominant at each sub-cell.
// We check if that dominant layer's texture filename contains road keywords.
// ─────────────────────────────────────────────

/// Parse MTEX chunk and return set of texture indices whose filenames match road keywords.
inline std::unordered_set<unsigned int> FindRoadTextureIds(const MTEX* mtex) noexcept
{
    std::unordered_set<unsigned int> result;
    if (!mtex)
        return result;

    unsigned int texIdx = 0;
    const char* ptr = mtex->filenames;
    const char* end = ptr + mtex->size;

    while (ptr < end && *ptr != '\0')
    {
        std::string name(ptr);
        // Convert to lowercase for case-insensitive matching
        for (auto& c : name)
            c = static_cast<char>(tolower(c));

        if (name.find("road") != std::string::npos || name.find("cobblestone") != std::string::npos ||
            name.find("flagstone") != std::string::npos || name.find("brick") != std::string::npos ||
            name.find("paved") != std::string::npos)
        {
            result.insert(texIdx);
        }

        ptr += strlen(ptr) + 1; // advance past null terminator
        texIdx++;
    }

    return result;
}

/// Extract road coverage from one MCNK chunk (x, y) into a RoadMap.
/// Uses predTex (8×8 dominant texture layer grid) + MCLY layer definitions.
inline void ExtractRoadCoverage(Adt* adt, unsigned int x, unsigned int y, RoadMap* roadMap,
                                const std::unordered_set<unsigned int>& roadTextureIds) noexcept
{
    if (roadTextureIds.empty())
        return;

    const MCNK* mcnk = adt->Mcnk(x, y);
    if (!mcnk || mcnk->nLayers == 0)
        return;

    const MCLY_Entry* layers = adt->Mcly(mcnk);
    if (!layers)
        return;

    // predTex is unsigned char[16]: 128 bits = 64 cells × 2 bits each
    // Layout: row-major, 2 bits per cell, LSB-first within each byte
    const unsigned char* predTexData = mcnk->predTex;

    for (int row = 0; row < 8; ++row)
    {
        for (int col = 0; col < 8; ++col)
        {
            int bitOffset = (row * 8 + col) * 2;
            unsigned int layerIdx = (predTexData[bitOffset / 8] >> (bitOffset % 8)) & 0x03;

            if (layerIdx < mcnk->nLayers)
            {
                unsigned int texId = layers[layerIdx].textureId;
                if (roadTextureIds.count(texId))
                {
                    // This sub-cell's dominant texture is a road
                    float wowX = mcnk->x - (row * UNITSIZE);
                    float wowY = mcnk->y - (col * UNITSIZE);
                    Vector3 nw{wowX, wowY, 0.0f};
                    Vector3 se{wowX - UNITSIZE, wowY - UNITSIZE, 0.0f};
                    roadMap->AddRect(nw, se);
                }
            }
        }
    }
}
