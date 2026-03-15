#pragma once

#include <format>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "../Mpq/CachedFileReader.hpp"
#include "../Utils/CityMap.hpp"
#include "../Utils/FactionMap.hpp"
#include "../Utils/Matrix4x4.hpp"
#include "../Utils/Structure.hpp"
#include "../Utils/Tri.hpp"
#include "../Utils/Vector3.hpp"
#include "../Utils/WaterMap.hpp"

#include "Adt.hpp"
#include "LiquidType.hpp"
#include "M2.hpp"
#include "Wmo.hpp"
#include "WmoGroup.hpp"

// ─────────────────────────────────────────────
// Free functions for extracting data from ADT chunks.
// Each function has a single clear responsibility.
// ─────────────────────────────────────────────

/// Extract terrain vertices and triangles from one MCNK chunk cell (x, y).
inline void ExtractTerrain(Adt* adt, unsigned int x, unsigned int y, Structure* structure) noexcept
{
    if (const MCNK* mcnk = adt->Mcnk(x, y))
    {
        std::lock_guard<std::mutex> lock(structure->mutex);

        int mcvtIndex = 0;

        for (int j = 0; j < 17; ++j)
        {
            const int unitCount = j % 2 ? 8 : 9;

            for (int i = 0; i < unitCount; ++i)
            {
                Vector3 v3{mcnk->x - (j * HALFUNITSIZE),
                           mcnk->y - (i * UNITSIZE) - (unitCount == 8 ? HALFUNITSIZE : 0.0f), mcnk->z};

                if (const MCVT* mcvt = adt->Mcvt(mcnk))
                {
                    v3.z += mcvt->heightMap[mcvtIndex];
                }

                mcvtIndex++;

                const size_t vertexCount = structure->verts.size();
                structure->verts.emplace_back(v3.ToRDCoords());

                if (unitCount == 8)
                {
                    if (!mcnk->IsHole(i, j))
                    {
                        structure->tris.emplace_back(Tri{vertexCount - 9, vertexCount, vertexCount - 8});
                        structure->tris.emplace_back(Tri{vertexCount + 9, vertexCount, vertexCount + 8});
                        structure->tris.emplace_back(Tri{vertexCount - 8, vertexCount, vertexCount + 9});
                        structure->tris.emplace_back(Tri{vertexCount + 8, vertexCount, vertexCount - 9});
                        structure->triTypes.emplace_back(TERRAIN_GROUND);
                        structure->triTypes.emplace_back(TERRAIN_GROUND);
                        structure->triTypes.emplace_back(TERRAIN_GROUND);
                        structure->triTypes.emplace_back(TERRAIN_GROUND);
                    }
                }
            }
        }
    }
}

/// Extract liquid (water) data from one MCNK cell (x, y) into a WaterMap.
/// If structure is non-null, also generates water surface triangles for navmesh rasterization.
inline void ExtractLiquid(Adt* adt, unsigned int x, unsigned int y, WaterMap* waterMap, Structure* structure,
                          const std::unordered_map<unsigned int, LiquidType>& liquidTypes) noexcept
{
    if (const MCNK* mcnk = adt->Mcnk(x, y))
    {
        if (const MH2O* mh2o = adt->Mh2o())
        {
            for (unsigned int k = 0; k < mh2o->liquid[y][x].used; k++)
            {
                if (const AdtLiquid* liquid = mh2o->GetInstance(x, y, k))
                {
                    TriAreaId liquidAreaId = LIQUID_WATER;
                    auto it = liquidTypes.find(liquid->type);
                    if (it != liquidTypes.end())
                    {
                        switch (it->second)
                        {
                            case LiquidType::OCEAN:
                                liquidAreaId = LIQUID_OCEAN;
                                break;
                            case LiquidType::MAGMA:
                                liquidAreaId = LIQUID_LAVA;
                                break;
                            case LiquidType::SLIME:
                                liquidAreaId = LIQUID_SLIME;
                                break;
                            default:
                                liquidAreaId = LIQUID_WATER;
                                break;
                        }
                    }

                    const unsigned char* renderMask = mh2o->GetRenderMask(liquid);
                    const unsigned char* vertexData =
                        reinterpret_cast<const unsigned char*>(mh2o->GetLiquidHeight(liquid));

                    // Vertex data stride per format (from wowdev.wiki / TrinityCore):
                    //   HeightDepth:     { float height; float depth; }     = 8 bytes
                    //   HeightTexCoord:  { float height; int16 x; int16 y; } = 8 bytes
                    //   Depth:           { float depth; }                    = 4 bytes (unused for height reads)
                    int stride = 8;
                    if (liquid->vertexFormat == AdtLiquidVertexFormat::Depth)
                        stride = 4;

                    for (unsigned char i = 0; i < liquid->height; i++)
                    {
                        for (unsigned char j = 0; j < liquid->width; j++)
                        {
                            bool isRendered = true;
                            if (renderMask)
                            {
                                int bitIdx = i * liquid->width + j;
                                isRendered = (renderMask[bitIdx / 8] >> (bitIdx % 8)) & 1;
                            }

                            if (isRendered)
                            {
                                float hNW = liquid->maxHeightLevel;
                                float hNE = liquid->maxHeightLevel;
                                float hSW = liquid->maxHeightLevel;
                                float hSE = liquid->maxHeightLevel;

                                if (vertexData && liquid->vertexFormat != AdtLiquidVertexFormat::Depth)
                                {
                                    auto getH = [&](int dx, int dy) {
                                        return *reinterpret_cast<const float*>(
                                            vertexData + (dy * (liquid->width + 1) + dx) * stride);
                                    };
                                    hNW = getH(j, i);
                                    hNE = getH(j + 1, i);
                                    hSW = getH(j, i + 1);
                                    hSE = getH(j + 1, i + 1);
                                }

                                float wowX = mcnk->x - ((liquid->offsetY + i) * UNITSIZE);
                                float wowY = mcnk->y - ((liquid->offsetX + j) * UNITSIZE);
                                Vector3 nw{wowX, wowY, 0.0f};
                                Vector3 se{wowX - UNITSIZE, wowY - UNITSIZE, 0.0f};
                                waterMap->AddRect(nw, se, hNW, hNE, hSW, hSE, liquidAreaId);

                                // Generate water surface triangles so the navmesh has
                                // geometry at the actual water level (not just terrain below).
                                if (structure)
                                {
                                    // 4 corners in RD coords (ToRDCoords: wowX,wowY,wowZ → wowY,wowZ,wowX)
                                    Vector3 vNW = Vector3{wowX, wowY, hNW}.ToRDCoords();
                                    Vector3 vNE = Vector3{wowX, wowY - UNITSIZE, hNE}.ToRDCoords();
                                    Vector3 vSW = Vector3{wowX - UNITSIZE, wowY, hSW}.ToRDCoords();
                                    Vector3 vSE = Vector3{wowX - UNITSIZE, wowY - UNITSIZE, hSE}.ToRDCoords();

                                    std::lock_guard<std::mutex> lock(structure->mutex);
                                    const size_t base = structure->verts.size();
                                    structure->verts.push_back(vNW);
                                    structure->verts.push_back(vNE);
                                    structure->verts.push_back(vSW);
                                    structure->verts.push_back(vSE);

                                    // CCW winding for up-facing normals in RD coordinate space
                                    structure->tris.emplace_back(Tri{base + 2, base + 1, base});
                                    structure->tris.emplace_back(Tri{base + 2, base + 3, base + 1});
                                    structure->triTypes.push_back(liquidAreaId);
                                    structure->triTypes.push_back(liquidAreaId);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/// Helper: add one WMO liquid vertex
inline void AddWmoLiquidVert(const MLIQ* mliq, const MLIQVert* dataPtr, int y, int x, std::vector<Vector3>& verts,
                             Matrix4x4& tranform) noexcept
{
    const auto liq = dataPtr[(y * mliq->countXVertices) + x];

    Vector3 base{mliq->position.x + (x * UNITSIZE), mliq->position.y + (y * UNITSIZE),
                 std::fabsf(liq.waterVert.height) > 0.5f ? liq.waterVert.height
                                                         : mliq->position.z + liq.waterVert.height};

    verts.emplace_back(tranform.Transform(base).ToRDCoords());
}

/// Extract WMO geometry (solid + liquid) from all MODF placements.
inline void ExtractWmoGeometry(Adt* adt, CachedFileReader& reader, Structure* structure,
                               const std::unordered_map<unsigned int, LiquidType>& liquidTypes) noexcept
{
    if (const MODF* modf = adt->Modf())
    {
        for (int i = 0; i < modf->size / sizeof(MODF::Entry); i++)
        {
            const auto& entry = modf->entries[i];
            const auto wmoRootFilename = adt->Mwmo()->filenames + adt->Mwid()->offsets[entry.id];

            if (const Wmo* wmo = reader.GetFileContent<Wmo>(wmoRootFilename))
            {
                if (const MOHD* mohd = wmo->Mohd())
                {
                    Matrix4x4 tranform;
                    tranform.SetRotation({entry.rz, entry.rx, entry.ry + 180.0f});

                    if (entry.x != 0.0f || entry.y != 0.0f || entry.z != 0.0f)
                    {
                        tranform.SetTranslation({-(entry.z - WORLDSIZE), -(entry.x - WORLDSIZE), entry.y});
                    }

                    for (unsigned int w = 0; w < mohd->groupCount; w++)
                    {
                        std::string_view wmoRF(wmoRootFilename);
                        const auto wmoGroupName =
                            std::format("{}_{:03}.wmo", wmoRF.substr(0, wmoRF.find_last_of('.')), w);

                        if (const WmoGroup* wmoGroup = reader.GetFileContent<WmoGroup>(wmoGroupName.c_str()))
                        {
                            // Solid geometry
                            if (const MOVT* movt = wmoGroup->Movt())
                            {
                                if (const MOVI* movi = wmoGroup->Movi())
                                {
                                    if (const MOPY* mopy = wmoGroup->Mopy())
                                    {
                                        std::lock_guard<std::mutex> lock(structure->mutex);
                                        const size_t vertsBase = structure->verts.size();

                                        for (unsigned int d = 0; d < movt->Count(); ++d)
                                        {
                                            structure->verts.emplace_back(
                                                tranform.Transform(movt->verts[d]).ToRDCoords());
                                        }

                                        for (unsigned int d = 0; d < movi->Count(); d += 3)
                                        {
                                            if ((mopy->data[d / 3].flags & 0x04) != 0 &&
                                                mopy->data[d / 3].materials != 0xFF)
                                            {
                                                continue;
                                            }

                                            structure->tris.emplace_back(Tri{vertsBase + movi->tris[d],
                                                                             vertsBase + movi->tris[d + 1],
                                                                             vertsBase + movi->tris[d + 2]});
                                            structure->triTypes.emplace_back(WMO);
                                        }
                                    }
                                }
                            }

                            // WMO liquid
                            if (const MLIQ* mliq = wmoGroup->Mliq())
                            {
                                TriAreaId wmoLiquidType = LIQUID_WATER;

                                const MOGP* mogp = wmoGroup->Mogp();
                                bool useNewLiquidType = (mohd->flags & 0x04) != 0;

                                if (mogp && mogp->groupLiquid > 0)
                                {
                                    if (useNewLiquidType)
                                    {
                                        auto liqIt = liquidTypes.find(mogp->groupLiquid);
                                        if (liqIt != liquidTypes.end())
                                        {
                                            switch (liqIt->second)
                                            {
                                                case LiquidType::OCEAN:
                                                    wmoLiquidType = LIQUID_OCEAN;
                                                    break;
                                                case LiquidType::MAGMA:
                                                    wmoLiquidType = LIQUID_LAVA;
                                                    break;
                                                case LiquidType::SLIME:
                                                    wmoLiquidType = LIQUID_SLIME;
                                                    break;
                                                default:
                                                    break;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        switch (mogp->groupLiquid)
                                        {
                                            case 2:
                                                wmoLiquidType = LIQUID_OCEAN;
                                                break;
                                            case 3:
                                                wmoLiquidType = LIQUID_LAVA;
                                                break;
                                            case 4:
                                                wmoLiquidType = LIQUID_SLIME;
                                                break;
                                            default:
                                                wmoLiquidType = LIQUID_WATER;
                                                break;
                                        }
                                    }
                                }

                                const auto vertCount = mliq->countYVertices * mliq->countXVertices;
                                const auto dataPtr = reinterpret_cast<const MLIQVert*>(mliq + 1);
                                const auto flags = reinterpret_cast<const unsigned char*>(dataPtr + vertCount);

                                for (unsigned int y = 0; y < mliq->height; ++y)
                                {
                                    for (unsigned int x = 0; x < mliq->width; ++x)
                                    {
                                        std::lock_guard<std::mutex> lock(structure->mutex);
                                        const size_t vertsIndex = structure->verts.size();

                                        AddWmoLiquidVert(mliq, dataPtr, y, x, structure->verts, tranform);
                                        AddWmoLiquidVert(mliq, dataPtr, y, x + 1, structure->verts, tranform);
                                        AddWmoLiquidVert(mliq, dataPtr, y + 1, x, structure->verts, tranform);
                                        AddWmoLiquidVert(mliq, dataPtr, y + 1, x + 1, structure->verts, tranform);

                                        unsigned char f = flags[y * mliq->width + x];

                                        if (f != 0x0F)
                                        {
                                            structure->tris.emplace_back(
                                                Tri{vertsIndex + 2, vertsIndex, vertsIndex + 1});
                                            structure->tris.emplace_back(
                                                Tri{vertsIndex + 1, vertsIndex + 3, vertsIndex + 2});

                                            structure->triTypes.emplace_back(wmoLiquidType);
                                            structure->triTypes.emplace_back(wmoLiquidType);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // WMO doodads
                    if (const MODD* modd = wmo->Modd())
                    {
                        if (modd->size > 0)
                        {
                            if (const MODN* modn = wmo->Modn())
                            {
                                for (unsigned int m = 0; m < modd->size / sizeof(MODD::Definition); m++)
                                {
                                    const auto& definition = modd->defs[m];
                                    std::string_view doodadPath(modn->names + definition.nameIndex);
                                    const auto m2Name =
                                        std::format("{}.m2", doodadPath.substr(0, doodadPath.find_last_of('.')));

                                    if (const M2* m2 = reader.GetFileContent<M2>(m2Name.c_str()))
                                    {
                                        if (const MD20* md20 = m2->Md20())
                                        {
                                            if (m2->IsCollideable())
                                            {
                                                Matrix4x4 doodadTranform;
                                                doodadTranform.SetRotation({0.0f, 180.0f, 0.0f});
                                                doodadTranform.SetRotation(-definition.qy, definition.qz,
                                                                           -definition.qx, definition.qw);
                                                doodadTranform.SetTranslation(definition.position);
                                                doodadTranform.Multiply(tranform);

                                                std::lock_guard<std::mutex> lock(structure->mutex);
                                                const size_t vertsBase = structure->verts.size();

                                                for (unsigned int d = 0; d < md20->countBoundingVertices; ++d)
                                                {
                                                    const Vector3 v3 = *m2->Vertex(d);
                                                    structure->verts.emplace_back(
                                                        doodadTranform.Transform(v3).ToRDCoords());
                                                }

                                                for (unsigned int d = 0; d < md20->countBoundingTriangles; d += 3)
                                                {
                                                    const auto t = m2->Tri(d);
                                                    structure->tris.emplace_back(Tri{
                                                        vertsBase + *t, vertsBase + *(t + 1), vertsBase + *(t + 2)});
                                                    structure->triTypes.emplace_back(WMO);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/// Extract city coverage from one MCNK chunk (x, y) into a CityMap.
/// Uses MCNK.areaid to check if this chunk is within a city area (capital or town).
inline void ExtractCityCoverage(Adt* adt, unsigned int x, unsigned int y, CityMap* cityMap,
                                const std::unordered_set<unsigned int>& areaCities) noexcept
{
    if (!cityMap || areaCities.empty())
        return;

    const MCNK* mcnk = adt->Mcnk(x, y);
    if (!mcnk || mcnk->areaid == 0)
        return;

    if (areaCities.find(mcnk->areaid) == areaCities.end())
        return; // Not a city area

    // Each MCNK chunk covers CHUNKSIZE × CHUNKSIZE in world space.
    Vector3 nw{mcnk->x, mcnk->y, 0.0f};
    Vector3 se{mcnk->x - CHUNKSIZE, mcnk->y - CHUNKSIZE, 0.0f};
    cityMap->AddRect(nw, se);
}

/// Extract faction coverage from one MCNK chunk (x, y) into a FactionMap.
/// Uses MCNK.areaid to look up the AreaTable.dbc faction for this chunk.
/// Only adds a rect if the area has a non-neutral faction (Alliance=1, Horde=2).
inline void ExtractFactionCoverage(Adt* adt, unsigned int x, unsigned int y, FactionMap* factionMap,
                                   const std::unordered_map<unsigned int, unsigned char>& areaFactions) noexcept
{
    if (!factionMap || areaFactions.empty())
        return;

    const MCNK* mcnk = adt->Mcnk(x, y);
    if (!mcnk || mcnk->areaid == 0)
        return;

    auto it = areaFactions.find(mcnk->areaid);
    if (it == areaFactions.end() || it->second == 0)
        return; // Unknown or contested/sanctuary — no faction marking

    // Each MCNK chunk covers CHUNKSIZE × CHUNKSIZE in world space.
    // mcnk->x/y is the NW corner; extends negatively.
    Vector3 nw{mcnk->x, mcnk->y, 0.0f};
    Vector3 se{mcnk->x - CHUNKSIZE, mcnk->y - CHUNKSIZE, 0.0f};
    factionMap->AddRect(nw, se, it->second);
}

/// Extract standalone doodad geometry from MDDF placements.
inline void ExtractDoodadGeometry(Adt* adt, CachedFileReader& reader, Structure* structure) noexcept
{
    if (MDDF* mddf = adt->Mddf())
    {
        for (int i = 0; i < mddf->size / sizeof(MDDF::Entry); i++)
        {
            const auto entry = mddf->entries[i];

            Matrix4x4 tranform;

            float doodadScale = entry.scale / 1024.0f;
            tranform.SetScale({doodadScale, doodadScale, doodadScale});

            tranform.SetRotation({entry.rz, entry.rx, entry.ry + 180.0f});

            if (entry.x != 0.0f || entry.y != 0.0f || entry.z != 0.0f)
            {
                tranform.SetTranslation({-(entry.z - WORLDSIZE), -(entry.x - WORLDSIZE), entry.y});
            }

            std::string_view doodadPath(adt->Mmdx()->filenames + adt->Mmid()->offsets[entry.id]);
            const auto m2Name = std::format("{}.m2", doodadPath.substr(0, doodadPath.find_last_of('.')));

            if (const M2* m2 = reader.GetFileContent<M2>(m2Name.c_str()))
            {
                if (const MD20* md20 = m2->Md20())
                {
                    if (m2->IsCollideable())
                    {
                        std::lock_guard lock(structure->mutex);
                        const size_t vertsBase = structure->verts.size();

                        for (unsigned int d = 0; d < md20->countBoundingVertices; ++d)
                        {
                            const Vector3 v3 = *m2->Vertex(d);
                            structure->verts.emplace_back(tranform.Transform(v3).ToRDCoords());
                        }

                        for (unsigned int d = 0; d < md20->countBoundingTriangles; d += 3)
                        {
                            const auto t = m2->Tri(d);
                            structure->tris.emplace_back(
                                Tri{vertsBase + *t, vertsBase + *(t + 1), vertsBase + *(t + 2)});
                            structure->triTypes.emplace_back(DOODAD);
                        }
                    }
                }
            }
        }
    }
}
