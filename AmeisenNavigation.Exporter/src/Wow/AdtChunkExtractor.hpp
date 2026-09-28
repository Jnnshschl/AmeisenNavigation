#pragma once

#include <cmath>
#include <cstring>
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

/// Placements (MODF/MDDF uniqueId) that were already extracted. WoW lists a WMO/doodad in every ADT it
/// overlaps; extracting each placement exactly once avoids duplicate geometry without clipping objects at
/// ADT borders (which used to punch holes into bridges and buildings crossing a border).
class PlacementSet
{
    std::mutex Mutex;
    std::unordered_set<uint32_t> Ids;

public:
    /// Returns true the first time an id is seen.
    bool Insert(uint32_t uniqueId)
    {
        std::lock_guard lock(Mutex);
        return Ids.insert(uniqueId).second;
    }

    void Clear()
    {
        std::lock_guard lock(Mutex);
        Ids.clear();
    }
};

inline TriAreaId LiquidTypeToArea(LiquidType type) noexcept
{
    switch (type)
    {
        case LiquidType::OCEAN: return LIQUID_OCEAN;
        case LiquidType::MAGMA: return LIQUID_LAVA;
        case LiquidType::SLIME: return LIQUID_SLIME;
        default: return LIQUID_WATER;
    }
}

/// Add a liquid quad (4 corners in WoW coords, NW/NE/SW/SE) to the water map and optionally as surface triangles.
inline void AddLiquidQuad(float wowX, float wowY, float hNW, float hNE, float hSW, float hSE, TriAreaId areaId,
                          WaterMap* waterMap, Structure* structure)
{
    const Vector3 nw{wowX, wowY, 0.0f};
    const Vector3 se{wowX - UNITSIZE, wowY - UNITSIZE, 0.0f};
    waterMap->AddRect(nw, se, hNW, hNE, hSW, hSE, areaId);

    // Water surface triangles give the navmesh geometry at the actual water level (not just the ground below).
    if (structure)
    {
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
        structure->triTypes.push_back(areaId);
        structure->triTypes.push_back(areaId);
    }
}

/// Extract terrain vertices and triangles from one MCNK chunk cell (x, y).
inline void ExtractTerrain(Adt* adt, unsigned int x, unsigned int y, Structure* structure)
{
    const MCNK* mcnk = adt->Mcnk(x, y);

    if (!mcnk)
    {
        return;
    }

    const MCVT* mcvt = adt->Mcvt(mcnk);
    std::lock_guard<std::mutex> lock(structure->mutex);

    int mcvtIndex = 0;

    for (int j = 0; j < 17; ++j)
    {
        const int unitCount = j % 2 ? 8 : 9;

        for (int i = 0; i < unitCount; ++i)
        {
            Vector3 v3{mcnk->x - (j * HALFUNITSIZE), mcnk->y - (i * UNITSIZE) - (unitCount == 8 ? HALFUNITSIZE : 0.0f),
                       mcnk->z};

            if (mcvt)
            {
                v3.z += mcvt->heightMap[mcvtIndex];
            }

            mcvtIndex++;

            const size_t vertexCount = structure->verts.size();
            structure->verts.emplace_back(v3.ToRDCoords());

            // Inner vertex: 4 triangles to the surrounding outer vertices.
            if (unitCount == 8 && !mcnk->IsHole(i, j))
            {
                structure->tris.emplace_back(Tri{vertexCount - 9, vertexCount, vertexCount - 8});
                structure->tris.emplace_back(Tri{vertexCount + 9, vertexCount, vertexCount + 8});
                structure->tris.emplace_back(Tri{vertexCount - 8, vertexCount, vertexCount + 9});
                structure->tris.emplace_back(Tri{vertexCount + 8, vertexCount, vertexCount - 9});
                structure->triTypes.insert(structure->triTypes.end(), 4, TERRAIN_GROUND);
            }
        }
    }
}

/// Extract liquid data from one MCNK cell (x, y) into a WaterMap.
/// If structure is non-null, also generates water surface triangles for navmesh rasterization.
inline void ExtractLiquid(Adt* adt, unsigned int x, unsigned int y, WaterMap* waterMap, Structure* structure,
                          const std::unordered_map<unsigned int, LiquidType>& liquidTypes)
{
    const MCNK* mcnk = adt->Mcnk(x, y);

    if (!mcnk)
    {
        return;
    }

    bool liquidHandled = false;

    // ── MH2O liquid format (WotLK+) ──
    // Only use MH2O if this cell has instances, otherwise fall through to MCLQ (classic maps in WotLK
    // clients may have an MH2O chunk but no instances for some cells).
    if (const MH2O* mh2o = adt->Mh2o(); mh2o && adt->ChunkInBounds(mh2o) && mh2o->liquid[y][x].used > 0)
    {
        const auto* mh2oData = reinterpret_cast<const unsigned char*>(mh2o) + 8;
        const auto* mh2oEnd = mh2oData + mh2o->size;
        const auto inMh2o = [&](const void* p, size_t length) {
            const auto* bytes = static_cast<const unsigned char*>(p);
            return p && bytes >= mh2oData && bytes <= mh2oEnd && length <= static_cast<size_t>(mh2oEnd - bytes);
        };

        liquidHandled = true;

        for (unsigned int k = 0; k < mh2o->liquid[y][x].used; k++)
        {
            const AdtLiquid* liquid = mh2o->GetInstance(x, y, k);

            if (!inMh2o(liquid, sizeof(AdtLiquid)) || liquid->offsetX + liquid->width > 8
                || liquid->offsetY + liquid->height > 8)
            {
                continue;
            }

            TriAreaId liquidAreaId = LIQUID_WATER;

            if (const auto it = liquidTypes.find(liquid->type); it != liquidTypes.end())
            {
                liquidAreaId = LiquidTypeToArea(it->second);
            }

            const unsigned char* renderMask = mh2o->GetRenderMask(liquid);
            const auto* vertexData = static_cast<const unsigned char*>(mh2o->GetLiquidHeight(liquid));

            // Vertex data stride per format (wowdev.wiki / TrinityCore):
            //   HeightDepth:     { float height; float depth; }      = 8 bytes
            //   HeightTexCoord:  { float height; int16 x; int16 y; } = 8 bytes
            //   Depth:           { float depth; }                     = 4 bytes (no heights)
            const int stride = liquid->vertexFormat == AdtLiquidVertexFormat::Depth ? 4 : 8;
            const size_t vertexCount = static_cast<size_t>(liquid->width + 1) * (liquid->height + 1);
            const bool hasHeights = liquid->vertexFormat != AdtLiquidVertexFormat::Depth
                                    && inMh2o(vertexData, vertexCount * static_cast<size_t>(stride));
            const size_t maskBytes = (static_cast<size_t>(liquid->width) * liquid->height + 7) / 8;

            if (renderMask && !inMh2o(renderMask, maskBytes))
            {
                renderMask = nullptr;
            }

            for (int i = 0; i < liquid->height; i++)
            {
                for (int j = 0; j < liquid->width; j++)
                {
                    // Non-rendered cells may have invalid vertex data (garbage/NaN heights).
                    if (renderMask)
                    {
                        const int bitIdx = i * liquid->width + j;

                        if (!((renderMask[bitIdx / 8] >> (bitIdx % 8)) & 1))
                        {
                            continue;
                        }
                    }

                    float hNW = liquid->maxHeightLevel;
                    float hNE = liquid->maxHeightLevel;
                    float hSW = liquid->maxHeightLevel;
                    float hSE = liquid->maxHeightLevel;

                    if (hasHeights)
                    {
                        const auto getH = [&](int dx, int dy) {
                            float h = 0.0f;
                            std::memcpy(&h, vertexData + (dy * (liquid->width + 1) + dx) * stride, sizeof(float));
                            return std::isfinite(h) ? h : liquid->maxHeightLevel;
                        };

                        hNW = getH(j, i);
                        hNE = getH(j + 1, i);
                        hSW = getH(j, i + 1);
                        hSE = getH(j + 1, i + 1);
                    }

                    const float wowX = mcnk->x - ((liquid->offsetY + i) * UNITSIZE);
                    const float wowY = mcnk->y - ((liquid->offsetX + j) * UNITSIZE);
                    AddLiquidQuad(wowX, wowY, hNW, hNE, hSW, hSE, liquidAreaId, waterMap, structure);
                }
            }
        }
    }

    if (liquidHandled)
    {
        return;
    }

    // ── Old MCLQ liquid format (pre-WotLK fallback) ──
    // MCNK liquid flags: 0x04 = river, 0x08 = ocean, 0x10 = magma, 0x20 = slime. Without them the MCLQ
    // offsets may be stale values from old format conversions (floating water quads).
    constexpr unsigned int MCNK_LIQUID_FLAGS = 0x04 | 0x08 | 0x10 | 0x20;

    if (!(mcnk->flags & MCNK_LIQUID_FLAGS))
    {
        return;
    }

    const MCLQ* mclq = adt->Mclq(mcnk);

    if (!mclq)
    {
        return;
    }

    TriAreaId liquidAreaId = LIQUID_WATER;

    if (mcnk->flags & 0x08)
        liquidAreaId = LIQUID_OCEAN;
    else if (mcnk->flags & 0x10)
        liquidAreaId = LIQUID_LAVA;
    else if (mcnk->flags & 0x20)
        liquidAreaId = LIQUID_SLIME;

    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            // Tile flag 0x0F means "don't render this liquid cell"
            if ((mclq->tiles[i * 8 + j] & 0x0F) == 0x0F)
            {
                continue;
            }

            // 9x9 vertex grid - quad (i, j) uses corners [i][j], [i][j+1], [i+1][j], [i+1][j+1]
            const float hNW = mclq->verts[(i) * 9 + j].height;
            const float hNE = mclq->verts[(i) * 9 + j + 1].height;
            const float hSW = mclq->verts[(i + 1) * 9 + j].height;
            const float hSE = mclq->verts[(i + 1) * 9 + j + 1].height;

            if (!std::isfinite(hNW) || !std::isfinite(hNE) || !std::isfinite(hSW) || !std::isfinite(hSE))
            {
                continue;
            }

            AddLiquidQuad(mcnk->x - (i * UNITSIZE), mcnk->y - (j * UNITSIZE), hNW, hNE, hSW, hSE, liquidAreaId,
                          waterMap, structure);
        }
    }
}

/// Helper: one WMO liquid vertex in RD coordinates.
inline Vector3 WmoLiquidVert(const MLIQ* mliq, const MLIQVert* dataPtr, unsigned int y, unsigned int x,
                             const Matrix4x4& transform) noexcept
{
    const auto& liq = dataPtr[(y * mliq->countXVertices) + x];

    Vector3 base{mliq->position.x + (x * UNITSIZE), mliq->position.y + (y * UNITSIZE),
                 std::fabs(liq.waterVert.height) > 0.5f ? liq.waterVert.height
                                                        : mliq->position.z + liq.waterVert.height};

    return transform.Transform(base).ToRDCoords();
}

/// Append model geometry (vertices already transformed) with index validation.
template <typename IndexFn>
inline void AppendMesh(Structure* structure, const std::vector<Vector3>& verts, unsigned int triCount, IndexFn&& index,
                       TriAreaId area)
{
    std::lock_guard<std::mutex> lock(structure->mutex);
    const size_t base = structure->verts.size();
    structure->verts.insert(structure->verts.end(), verts.begin(), verts.end());

    for (unsigned int t = 0; t < triCount; ++t)
    {
        unsigned int a = 0, b = 0, c = 0;

        if (!index(t, a, b, c) || a >= verts.size() || b >= verts.size() || c >= verts.size())
        {
            continue;
        }

        structure->tris.emplace_back(Tri{base + a, base + b, base + c});
        structure->triTypes.push_back(area);
    }
}

/// Add the collision mesh of an M2 model with the given transform.
inline void AddM2Collision(const M2* m2, const Matrix4x4& transform, Structure* structure, TriAreaId area)
{
    if (!m2->IsValid() || !m2->IsCollideable())
    {
        return;
    }

    const MD20* md20 = m2->Md20();

    std::vector<Vector3> verts;
    verts.reserve(md20->countBoundingVertices);

    for (unsigned int d = 0; d < md20->countBoundingVertices; ++d)
    {
        verts.push_back(transform.Transform(*m2->Vertex(d)).ToRDCoords());
    }

    AppendMesh(
        structure, verts, md20->countBoundingTriangles / 3,
        [&](unsigned int t, unsigned int& a, unsigned int& b, unsigned int& c) {
            const unsigned short* tri = m2->Tri(static_cast<size_t>(t) * 3);
            a = tri[0];
            b = tri[1];
            c = tri[2];
            return true;
        },
        area);
}

/// Extract WMO geometry (solid + liquid + doodads of the placement's doodad set) from all MODF placements.
inline void ExtractWmoGeometry(Adt* adt, CachedFileReader& reader, Structure* structure,
                               const std::unordered_map<unsigned int, LiquidType>& liquidTypes,
                               PlacementSet* placements = nullptr)
{
    const MODF* modf = adt->Modf();

    if (!adt->ChunkInBounds(modf))
    {
        return;
    }

    for (size_t i = 0; i < modf->size / sizeof(MODF::Entry); i++)
    {
        const auto& entry = modf->entries[i];

        if (placements && !placements->Insert(entry.uniqueId))
        {
            continue; // already extracted from a neighbour ADT
        }

        const char* wmoRootFilename = adt->GetFilename(adt->Mwmo(), adt->Mwid(), entry.id);

        if (!wmoRootFilename)
        {
            continue;
        }

        const Wmo* wmo = reader.GetFileContent<Wmo>(wmoRootFilename);

        if (!wmo || !wmo->IsValid())
        {
            continue;
        }

        const MOHD* mohd = wmo->Mohd();

        Matrix4x4 transform;
        transform.SetRotation({entry.rz, entry.rx, entry.ry + 180.0f});

        if (entry.x != 0.0f || entry.y != 0.0f || entry.z != 0.0f)
        {
            transform.SetTranslation({-(entry.z - WORLDSIZE), -(entry.x - WORLDSIZE), entry.y});
        }

        const std::string_view rootName(wmoRootFilename);
        const auto rootStem = rootName.substr(0, rootName.find_last_of('.'));

        for (unsigned int w = 0; w < mohd->groupCount && w < 512; w++)
        {
            const auto wmoGroupName = std::format("{}_{:03}.wmo", rootStem, w);
            const WmoGroup* wmoGroup = reader.GetFileContent<WmoGroup>(wmoGroupName.c_str());

            if (!wmoGroup)
            {
                continue;
            }

            // Solid geometry
            const MOVT* movt = wmoGroup->Movt();
            const MOVI* movi = wmoGroup->Movi();
            const MOPY* mopy = wmoGroup->Mopy();

            if (movt && movi && mopy)
            {
                std::vector<Vector3> verts;
                verts.reserve(movt->Count());

                for (unsigned int d = 0; d < movt->Count(); ++d)
                {
                    verts.push_back(transform.Transform(movt->verts[d]).ToRDCoords());
                }

                const unsigned int triCount = std::min(movi->Count() / 3, mopy->Count());

                AppendMesh(
                    structure, verts, triCount,
                    [&](unsigned int t, unsigned int& a, unsigned int& b, unsigned int& c) {
                        // Skip non-collidable render-only polygons (F_DETAIL/F_NOCOLLIDE style materials).
                        if ((mopy->data[t].flags & 0x04) != 0 && mopy->data[t].materials != 0xFF)
                        {
                            return false;
                        }

                        a = movi->tris[t * 3];
                        b = movi->tris[t * 3 + 1];
                        c = movi->tris[t * 3 + 2];
                        return true;
                    },
                    WMO);
            }

            // WMO liquid
            if (const MLIQ* mliq = wmoGroup->Mliq(); mliq && wmoGroup->LiquidInBounds(mliq))
            {
                TriAreaId wmoLiquidType = LIQUID_WATER;
                const MOGP* mogp = wmoGroup->Mogp();

                if (mogp && mogp->groupLiquid > 0)
                {
                    if ((mohd->flags & 0x04) != 0) // "use liquid type dbc id"
                    {
                        if (const auto it = liquidTypes.find(mogp->groupLiquid); it != liquidTypes.end())
                        {
                            wmoLiquidType = LiquidTypeToArea(it->second);
                        }
                    }
                    else
                    {
                        switch (mogp->groupLiquid)
                        {
                            case 2: wmoLiquidType = LIQUID_OCEAN; break;
                            case 3: wmoLiquidType = LIQUID_LAVA; break;
                            case 4: wmoLiquidType = LIQUID_SLIME; break;
                            default: wmoLiquidType = LIQUID_WATER; break;
                        }
                    }
                }

                const auto vertCount = mliq->countYVertices * mliq->countXVertices;
                const auto* dataPtr = reinterpret_cast<const MLIQVert*>(mliq + 1);
                const auto* flags = reinterpret_cast<const unsigned char*>(dataPtr + vertCount);

                std::lock_guard<std::mutex> lock(structure->mutex);

                for (unsigned int y = 0; y < mliq->height; ++y)
                {
                    for (unsigned int x = 0; x < mliq->width; ++x)
                    {
                        if (flags[y * mliq->width + x] == 0x0F)
                        {
                            continue;
                        }

                        const size_t vertsIndex = structure->verts.size();
                        structure->verts.push_back(WmoLiquidVert(mliq, dataPtr, y, x, transform));
                        structure->verts.push_back(WmoLiquidVert(mliq, dataPtr, y, x + 1, transform));
                        structure->verts.push_back(WmoLiquidVert(mliq, dataPtr, y + 1, x, transform));
                        structure->verts.push_back(WmoLiquidVert(mliq, dataPtr, y + 1, x + 1, transform));

                        structure->tris.emplace_back(Tri{vertsIndex + 2, vertsIndex, vertsIndex + 1});
                        structure->tris.emplace_back(Tri{vertsIndex + 1, vertsIndex + 3, vertsIndex + 2});
                        structure->triTypes.push_back(wmoLiquidType);
                        structure->triTypes.push_back(wmoLiquidType);
                    }
                }
            }
        }

        // WMO doodads: set 0 is always present, plus the placement's doodad set.
        const MODD* modd = wmo->Modd();
        const MODN* modn = wmo->Modn();

        if (!modd || !modn || modd->size == 0)
        {
            continue;
        }

        const unsigned int definitionCount = modd->size / sizeof(MODD::Definition);
        std::vector<std::pair<unsigned int, unsigned int>> ranges; // [start, end)

        if (const MODS* mods = wmo->Mods())
        {
            const unsigned int setCount = mods->size / sizeof(mods->set[0]);

            const auto addSet = [&](unsigned int set) {
                if (set < setCount)
                {
                    const auto start = std::min(mods->set[set].startIndex, definitionCount);
                    const auto end = std::min(start + mods->set[set].count, definitionCount);
                    ranges.emplace_back(start, end);
                }
            };

            addSet(0);

            if (entry.doodadSet != 0)
            {
                addSet(entry.doodadSet);
            }
        }
        else
        {
            ranges.emplace_back(0u, definitionCount);
        }

        for (const auto& [start, end] : ranges)
        {
            for (unsigned int m = start; m < end; m++)
            {
                const auto& definition = modd->defs[m];

                // nameIndex is 24 bits, the upper 8 bits are flags.
                const unsigned int nameIndex = definition.nameIndex & 0x00FFFFFF;

                if (nameIndex >= modn->size)
                {
                    continue;
                }

                const std::string_view doodadPath(modn->names + nameIndex,
                                                  strnlen(modn->names + nameIndex, modn->size - nameIndex));
                const auto m2Name = std::format("{}.m2", doodadPath.substr(0, doodadPath.find_last_of('.')));

                if (const M2* m2 = reader.GetFileContent<M2>(m2Name.c_str()))
                {
                    Matrix4x4 doodadTransform;
                    doodadTransform.SetScale({definition.scale, definition.scale, definition.scale});
                    doodadTransform.SetRotation({0.0f, 180.0f, 0.0f});
                    doodadTransform.SetRotation(-definition.qy, definition.qz, -definition.qx, definition.qw);
                    doodadTransform.SetTranslation(definition.position);
                    doodadTransform.Multiply(transform);

                    AddM2Collision(m2, doodadTransform, structure, WMO);
                }
            }
        }
    }
}

/// Extract city coverage from one MCNK chunk (x, y) into a CityMap.
/// Uses MCNK.areaid to check if this chunk is within a city area (capital or town).
inline void ExtractCityCoverage(Adt* adt, unsigned int x, unsigned int y, CityMap* cityMap,
                                const std::unordered_set<unsigned int>& areaCities)
{
    if (!cityMap || areaCities.empty())
    {
        return;
    }

    const MCNK* mcnk = adt->Mcnk(x, y);

    if (!mcnk || mcnk->areaid == 0 || !areaCities.contains(mcnk->areaid))
    {
        return;
    }

    // Each MCNK chunk covers CHUNKSIZE x CHUNKSIZE in world space, mcnk->x/y is the NW corner.
    cityMap->AddRect({mcnk->x, mcnk->y, 0.0f}, {mcnk->x - CHUNKSIZE, mcnk->y - CHUNKSIZE, 0.0f});
}

/// Extract faction coverage from one MCNK chunk (x, y) into a FactionMap.
/// Only adds a rect if the area has a non-neutral faction (Alliance=1, Horde=2).
inline void ExtractFactionCoverage(Adt* adt, unsigned int x, unsigned int y, FactionMap* factionMap,
                                   const std::unordered_map<unsigned int, unsigned char>& areaFactions)
{
    if (!factionMap || areaFactions.empty())
    {
        return;
    }

    const MCNK* mcnk = adt->Mcnk(x, y);

    if (!mcnk || mcnk->areaid == 0)
    {
        return;
    }

    const auto it = areaFactions.find(mcnk->areaid);

    if (it == areaFactions.end() || it->second == 0)
    {
        return; // unknown or contested/sanctuary
    }

    factionMap->AddRect({mcnk->x, mcnk->y, 0.0f}, {mcnk->x - CHUNKSIZE, mcnk->y - CHUNKSIZE, 0.0f}, it->second);
}

/// Extract standalone doodad geometry from MDDF placements.
inline void ExtractDoodadGeometry(Adt* adt, CachedFileReader& reader, Structure* structure,
                                  PlacementSet* placements = nullptr)
{
    const MDDF* mddf = adt->Mddf();

    if (!adt->ChunkInBounds(mddf))
    {
        return;
    }

    for (size_t i = 0; i < mddf->size / sizeof(MDDF::Entry); i++)
    {
        const auto& entry = mddf->entries[i];

        if (placements && !placements->Insert(entry.uniqueId))
        {
            continue;
        }

        const char* doodadFilename = adt->GetFilename(adt->Mmdx(), adt->Mmid(), entry.id);

        if (!doodadFilename)
        {
            continue;
        }

        const std::string_view doodadPath(doodadFilename);
        const auto m2Name = std::format("{}.m2", doodadPath.substr(0, doodadPath.find_last_of('.')));
        const M2* m2 = reader.GetFileContent<M2>(m2Name.c_str());

        if (!m2)
        {
            continue;
        }

        Matrix4x4 transform;
        const float doodadScale = entry.scale / 1024.0f;
        transform.SetScale({doodadScale, doodadScale, doodadScale});
        transform.SetRotation({entry.rz, entry.rx, entry.ry + 180.0f});

        if (entry.x != 0.0f || entry.y != 0.0f || entry.z != 0.0f)
        {
            transform.SetTranslation({-(entry.z - WORLDSIZE), -(entry.x - WORLDSIZE), entry.y});
        }

        AddM2Collision(m2, transform, structure, DOODAD);
    }
}
