#pragma once

#include "Mver.hpp"
#include "Wmo.hpp"
#include "WmoGroup.hpp"
#include "M2.hpp"
#include "../Utils/Vector3.hpp"
#include "../Utils/Matrix4x4.hpp"
#include "../Mpq/CachedFileReader.hpp"

constexpr auto TILESIZE = 533.33333f;
constexpr auto WORLDSIZE = TILESIZE * 32.0f;
constexpr auto CHUNKSIZE = TILESIZE / 16.0f;
constexpr auto UNITSIZE = CHUNKSIZE / 8.0f;
constexpr auto HALFUNITSIZE = UNITSIZE / 2.0f;

constexpr auto ADT_CELLS_PER_GRID = 16;
constexpr auto ADT_CELL_SIZE = 8;
constexpr auto ADT_GRID_SIZE = ADT_CELLS_PER_GRID * ADT_CELL_SIZE;

#pragma pack(push, 1)
struct MMDX
{
    unsigned char magic[4];
    unsigned int size;
    char filenames[1];
};

struct MMID
{
    unsigned char magic[4];
    unsigned int size;
    uint32_t offsets[1];
};

struct MDDF
{
    unsigned char magic[4];
    unsigned int size;

    struct Entry
    {
        unsigned int id;
        unsigned int uniqueId;
        float x;
        float y;
        float z;
        float rx;
        float ry;
        float rz;
        unsigned short scale;
        unsigned short flags;
    } entries[1];
};

struct MWMO
{
    unsigned char magic[4];
    unsigned int size;
    char filenames[1];
};

struct MWID
{
    unsigned char magic[4];
    unsigned int size;
    uint32_t offsets[1];
};

struct MODF
{
    unsigned char magic[4];
    unsigned int size;

    struct Entry
    {
        unsigned int id;
        unsigned int uniqueId;
        float x;
        float y;
        float z;
        float rx;
        float ry;
        float rz;
        float bbMinX;
        float bbMinY;
        float bbMinZ;
        float bbMaxX;
        float bbMaxY;
        float bbMaxZ;
        unsigned short flags;
        unsigned short doodadSet;
        unsigned short nameSet;
        unsigned short scale;

    } entries[1];
};

enum class AdtLiquidVertexFormat : unsigned short
{
    HeightDepth = 0,
    HeightTextureCoord = 1,
    Depth = 2,
};

struct AdtLiquid
{
    unsigned short type;
    AdtLiquidVertexFormat vertexFormat;
    float minHeightLevel;
    float maxHeightLevel;
    unsigned char offsetX;
    unsigned char offsetY;
    unsigned char width;
    unsigned char height;
    unsigned int offsetRenderMask;
    unsigned int offsetVertexData;
};

struct AdtLiquidAttributes
{
    unsigned long long fishable;
    unsigned long long deep;
};

class MH2O
{
public:
    unsigned char magic[4];
    unsigned int size;

    struct
    {
        unsigned int offsetInstances;
        unsigned int used;
        unsigned int offsetAttributes;
    } liquid[ADT_CELLS_PER_GRID][ADT_CELLS_PER_GRID];

    inline const AdtLiquid* GetInstance(unsigned int x, unsigned int y) const noexcept
    {
        if (liquid[x][y].used && liquid[x][y].offsetInstances)
        {
            return reinterpret_cast<const AdtLiquid*>(reinterpret_cast<const unsigned char*>(this) + 8 + liquid[x][y].offsetInstances);
        }

        return nullptr;
    }

    inline const AdtLiquidAttributes* GetAttributes(unsigned int x, unsigned int y) const noexcept
    {
        if (liquid[x][y].used)
        {
            if (liquid[x][y].offsetAttributes)
            {
                return reinterpret_cast<const AdtLiquidAttributes*>(reinterpret_cast<const unsigned char*>(this) + 8 + liquid[x][y].offsetAttributes);
            }

            static AdtLiquidAttributes all{ 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF };
            return &all;
        }

        return nullptr;
    }

    inline const unsigned long long* GetRenderMask(const AdtLiquid* h) const noexcept
    {
        return h->offsetRenderMask ? reinterpret_cast<const unsigned long long*>(reinterpret_cast<const unsigned char*>(this) + 8 + h->offsetRenderMask)
            : nullptr;
    }

    inline const float* GetLiquidHeight(const AdtLiquid* liquid) const noexcept
    {
        if (!liquid->offsetVertexData)
        {
            return nullptr;
        }

        switch (liquid->vertexFormat)
        {
        case AdtLiquidVertexFormat::HeightDepth:
        case AdtLiquidVertexFormat::HeightTextureCoord:
            return reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(this) + 8 + liquid->offsetVertexData);

        case AdtLiquidVertexFormat::Depth:
        default:
            return nullptr;
        }
    }
};

struct MCVT
{
    unsigned char magic[4];
    unsigned int size;
    float heightMap[(9 * 9) + (8 * 8)];
};

struct MCNK
{
    unsigned char magic[4];
    unsigned int size;
    unsigned int flags;
    unsigned int ix;
    unsigned int iy;
    unsigned int nLayers;
    unsigned int nDoodadRefs;
    unsigned int offsMcvt;
    unsigned int offsMcnr;
    unsigned int offsMcly;
    unsigned int offsMcrf;
    unsigned int offsMcal;
    unsigned int sizeMcal;
    unsigned int offsMcsh;
    unsigned int sizeMcsh;
    unsigned int areaid;
    unsigned int nMapObjRefs;
    unsigned int holes;
    unsigned short s[2];
    unsigned int data[3];
    unsigned int predTex;
    unsigned int nEffectDoodad;
    unsigned int offsMcse;
    unsigned int nSndEmitters;
    unsigned int offsMclq;
    unsigned int sizeMclq;
    float  x;
    float  y;
    float  z;
    unsigned int offsMccv;
    unsigned int props;
    unsigned int effectId;

    inline bool IsHole(unsigned int x, unsigned int y) const noexcept { return !holes || !((holes & 0x0000FFFFu) & ((1 << (x / 2)) << ((y / 4) << 2))); }
};

struct MCIN
{
    unsigned char magic[4];
    unsigned int size;

    struct
    {
        unsigned int offsetMcnk;
        unsigned int size;
        unsigned int flags;
        unsigned int asyncId;
    } cells[ADT_CELLS_PER_GRID][ADT_CELLS_PER_GRID];
};

struct MHDR
{
    unsigned char magic[4];
    unsigned int size;
    unsigned int flags;
    unsigned int offsetMcin;
    unsigned int offsetMtex;
    unsigned int offsetMmdx;
    unsigned int offsetMmid;
    unsigned int offsetMwmo;
    unsigned int offsetMwid;
    unsigned int offsetMddf;
    unsigned int offsetModf;
    unsigned int offsetMfbo;
    unsigned int offsetMh2o;
    unsigned int data[5];
};
#pragma pack(pop)

class Adt
{
    unsigned char* Data;
    unsigned int Size;

public:
    inline const MVER* Mver() const noexcept { return reinterpret_cast<MVER*>(Data); };
    inline const MHDR* Mhdr() const noexcept { return reinterpret_cast<MHDR*>(Data + sizeof(MVER)); };

    template<typename T>
    inline T* GetSub(unsigned int offset) const noexcept
    {
        return offset ? reinterpret_cast<T*>(Data + sizeof(MVER) + 8 + offset) : nullptr;
    };

    inline const auto Mcin() const noexcept { return GetSub<MCIN>(Mhdr()->offsetMcin); };
    inline const auto Mh2o() const noexcept { return GetSub<MH2O>(Mhdr()->offsetMh2o); };

    inline const auto Mmdx() const noexcept { return GetSub<MMDX>(Mhdr()->offsetMmdx); };
    inline const auto Mmid() const noexcept { return GetSub<MMID>(Mhdr()->offsetMmid); };
    inline const auto Mddf() const noexcept { return GetSub<MDDF>(Mhdr()->offsetMddf); };

    inline const auto Mwmo() const noexcept { return GetSub<MWMO>(Mhdr()->offsetMwmo); };
    inline const auto Mwid() const noexcept { return GetSub<MWID>(Mhdr()->offsetMwid); };
    inline const auto Modf() const noexcept { return GetSub<MODF>(Mhdr()->offsetModf); };

    inline const MCNK* Mcnk(unsigned int x, unsigned int y) noexcept
    {
        unsigned int offset = Mcin()->cells[x][y].offsetMcnk;
        return offset ? reinterpret_cast<MCNK*>(Data + offset) : nullptr;
    };

    inline const MCVT* Mcvt(const MCNK* mcnk) noexcept
    {
        unsigned int offset = mcnk->offsMcvt;
        return offset ? reinterpret_cast<const MCVT*>(reinterpret_cast<const unsigned char*>(mcnk) + offset) : nullptr;
    };

    inline void GetTerrainVertsAndTris(unsigned int x, unsigned int y, Structure& structure) noexcept
    {
        if (const MCNK* mcnk = Mcnk(x, y))
        {
            std::lock_guard lock(structure.mutex);

            // heightMap index, 0 - 144
            int mcvtIndex = 0;

            for (int j = 0; j < 17; ++j) // 17 total row count (9 + 8)
            {
                // how many units are in this row (this alternates 9, 8, 9, 8, ..., 9)
                const int unitCount = j % 2 ? 8 : 9;

                for (int i = 0; i < unitCount; ++i)
                {
                    // chunk offset - unit offset
                    Vector3 v3{ mcnk->x - (j * HALFUNITSIZE), mcnk->y - (i * UNITSIZE), mcnk->z };

                    // add the heightMap offset if there is one
                    if (const MCVT* mcvt = Mcvt(mcnk))
                    {
                        v3.z += mcvt->heightMap[mcvtIndex];
                    }

                    mcvtIndex++;

                    const size_t vertexCount = structure.verts.size();
                    structure.verts.emplace_back(v3.ToRDCoords());

                    if (unitCount == 8)
                    {
                        // shift the inner row by HALFUNITSIZE
                        v3.y -= HALFUNITSIZE;

                        // check for holes in the inner grid, if there is no hole, generate tris
                        if (mcnk->IsHole(i, j))
                        {
                            structure.tris.emplace_back(Tri{ vertexCount - 9, vertexCount, vertexCount - 8 }); // Top
                            structure.tris.emplace_back(Tri{ vertexCount + 9, vertexCount, vertexCount + 8 }); // Bottom
                            structure.tris.emplace_back(Tri{ vertexCount - 8, vertexCount, vertexCount + 9 }); // Right
                            structure.tris.emplace_back(Tri{ vertexCount + 8, vertexCount, vertexCount - 9 }); // Left
                            structure.triTypes.emplace_back(TERRAIN_GROUND);
                            structure.triTypes.emplace_back(TERRAIN_GROUND);
                            structure.triTypes.emplace_back(TERRAIN_GROUND);
                            structure.triTypes.emplace_back(TERRAIN_GROUND);
                        }
                    }
                }
            }
        }
    }

    inline void GetLiquidVertsAndTris(unsigned int x, unsigned int y, Structure& structure) noexcept
    {
        if (const MCNK* mcnk = Mcnk(x, y))
        {
            if (const MH2O* mh2o = Mh2o())
            {
                if (const AdtLiquid* liquid = mh2o->GetInstance(x, y))
                {
                    const AdtLiquidAttributes* attributes = mh2o->GetAttributes(x, y);

                    const bool isOcean = liquid->type == 2;
                    const unsigned char* renderMask = nullptr; // (unsigned char*)mh2o->GetRenderMask(liquid);
                    const float* liquidHeights = mh2o->GetLiquidHeight(liquid);

                    // unsigned int liquidHeightIndex = 0;

                    for (unsigned char i = liquid->offsetY; i < liquid->offsetY + liquid->height; i++)
                    {
                        const float cx = mcnk->x - (i * UNITSIZE);

                        for (unsigned char j = liquid->offsetX; j < liquid->offsetX + liquid->width; j++)
                        {
                            if (isOcean || !renderMask)
                            {
                                const float cy = mcnk->y - (j * UNITSIZE);
                                const float cz = liquid->maxHeightLevel; // isOcean ?  : liquidHeights[liquidHeightIndex]
                                // liquidHeightIndex++;

                                std::lock_guard lock(structure.mutex);
                                const size_t vertsIndex = structure.verts.size();

                                structure.verts.emplace_back(Vector3{ cx, cy, cz }.ToRDCoords());
                                structure.verts.emplace_back(Vector3{ cx - UNITSIZE, cy, cz }.ToRDCoords());
                                structure.verts.emplace_back(Vector3{ cx, cy - UNITSIZE, cz }.ToRDCoords());
                                structure.verts.emplace_back(Vector3{ cx - UNITSIZE, cy - UNITSIZE, cz }.ToRDCoords());

                                structure.tris.emplace_back(Tri{ vertsIndex + 2, vertsIndex, vertsIndex + 1 });
                                structure.tris.emplace_back(Tri{ vertsIndex + 1, vertsIndex + 3, vertsIndex + 2 });
                                structure.triTypes.emplace_back(isOcean ? LIQUID_OCEAN : LIQUID_WATER);
                                structure.triTypes.emplace_back(isOcean ? LIQUID_OCEAN : LIQUID_WATER);
                            }
                        }
                    }
                }
            }
        }
    }

    inline void GetWmoVertsAndTris(CachedFileReader& reader, Structure& structure) const noexcept
    {
        if (const MODF* modf = Modf())
        {
            std::unordered_map<unsigned int, unsigned char*> wmoFiles;

            for (int i = 0; i < modf->size / sizeof(MODF::Entry); i++)
            {
                const auto& entry = modf->entries[i];
                const auto wmoRootFilename = Mwmo()->filenames + Mwid()->offsets[entry.id];

                if (const Wmo* wmo = reader.GetFileContent<Wmo>(wmoRootFilename))
                {
                    if (const MOHD* mohd = wmo->Mohd())
                    {
                        Matrix4x4 tranform;

                        if (entry.x != 0.0f || entry.y != 0.0f || entry.z != 0.0f)
                        {
                            tranform.SetTranslation({ -(entry.z - WORLDSIZE), -(entry.x - WORLDSIZE), entry.y });
                        }

                        tranform.SetRotation({ entry.rz, entry.rx, entry.ry + 180.0f });

                        for (unsigned int w = 0; w < mohd->groupCount; w++)
                        {
                            std::string_view wmoRF(wmoRootFilename);
                            const auto wmoGroupName = std::format("{}_{:03}.wmo", wmoRF.substr(0, wmoRF.find_last_of('.')), w);

                            if (const WmoGroup* wmoGroup = reader.GetFileContent<WmoGroup>(wmoGroupName.c_str()))
                            {
                                if (const MOVT* movt = wmoGroup->Movt())
                                {
                                    if (const MOVI* movi = wmoGroup->Movi())
                                    {
                                        if (const MOPY* mopy = wmoGroup->Mopy())
                                        {
                                            std::lock_guard lock(structure.mutex);
                                            const size_t vertsBase = structure.verts.size();

                                            for (unsigned int d = 0; d < movt->Count(); ++d)
                                            {
                                                structure.verts.emplace_back(tranform.Transform(movt->verts[d]).ToRDCoords());
                                            }

                                            for (unsigned int d = 0; d < movi->Count(); d += 3)
                                            {
                                                if ((mopy->data[d / 3].flags & 0x04) != 0 && mopy->data[d / 3].materials != 0xFF)
                                                {
                                                    continue;
                                                }

                                                structure.tris.emplace_back(Tri{ vertsBase + movi->tris[d] , vertsBase + movi->tris[d + 1], vertsBase + movi->tris[d + 2] });
                                                structure.triTypes.emplace_back(WMO);
                                            }
                                        }
                                    }
                                }


                                if (const MLIQ* mliq = wmoGroup->Mliq())
                                {
                                    const auto vertCount = mliq->countYVertices * mliq->countXVertices;
                                    const auto dataPtr = reinterpret_cast<const MLIQVert*>(mliq + 1);
                                    const auto flags = reinterpret_cast<const unsigned char*>(dataPtr + vertCount);

                                    for (unsigned int y = 0; y < mliq->height; ++y)
                                    {
                                        for (unsigned int x = 0; x < mliq->width; ++x)
                                        {
                                            std::lock_guard lock(structure.mutex);
                                            const size_t vertsIndex = structure.verts.size();

                                            AddLiquidVert(mliq, dataPtr, y, x, structure.verts, tranform);
                                            AddLiquidVert(mliq, dataPtr, y, x + 1, structure.verts, tranform);
                                            AddLiquidVert(mliq, dataPtr, y + 1, x, structure.verts, tranform);
                                            AddLiquidVert(mliq, dataPtr, y + 1, x + 1, structure.verts, tranform);

                                            unsigned char f = 0; // flags[(y * mliq->height) + x];

                                            // TODO: find right way to interpret flags
                                            if (true || f != 0x0F)
                                            {
                                                structure.tris.emplace_back(Tri{ vertsIndex + 2, vertsIndex, vertsIndex + 1 });
                                                structure.tris.emplace_back(Tri{ vertsIndex + 1, vertsIndex + 3, vertsIndex + 2 });

                                                TriAreaId t = LIQUID_WATER;

                                                if (f & 1)
                                                {
                                                    t = LIQUID_WATER;
                                                }
                                                else if (f & 2)
                                                {
                                                    t = LIQUID_OCEAN;
                                                }
                                                else if (f & 4)
                                                {
                                                    t = LIQUID_LAVA;
                                                }
                                                else if (f & 8)
                                                {
                                                    t = LIQUID_SLIME;
                                                }

                                                structure.triTypes.emplace_back(t);
                                                structure.triTypes.emplace_back(t);
                                            }
                                        }
                                    }
                                }
                            }
                        }

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
                                        const auto m2Name = std::format("{}.m2", doodadPath.substr(0, doodadPath.find_last_of('.')));

                                        if (const M2* m2 = reader.GetFileContent<M2>(m2Name.c_str()))
                                        {
                                            if (const MD20* md20 = m2->Md20())
                                            {
                                                if (m2->IsCollideable())
                                                {
                                                    Matrix4x4 doodadTranform;
                                                    doodadTranform.SetTranslation(definition.position);
                                                    doodadTranform.SetRotation({ 0.0f, 180.0f, 0.0f });
                                                    doodadTranform.SetRotation(-definition.qy, definition.qz, -definition.qx, definition.qw);
                                                    doodadTranform.Multiply(tranform);

                                                    std::lock_guard lock(structure.mutex);
                                                    const size_t vertsBase = structure.verts.size();

                                                    for (unsigned int d = 0; d < md20->countBoundingVertices; ++d)
                                                    {
                                                        const Vector3 v3 = *m2->Vertex(d);
                                                        structure.verts.emplace_back(doodadTranform.Transform(v3).ToRDCoords());
                                                    }

                                                    for (unsigned int d = 0; d < md20->countBoundingTriangles; d += 3)
                                                    {
                                                        const auto t = m2->Tri(d);
                                                        structure.tris.emplace_back(Tri{ vertsBase + *t , vertsBase + *(t + 1), vertsBase + *(t + 2) });
                                                        structure.triTypes.emplace_back(WMO);
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

    inline void AddLiquidVert(const MLIQ* mliq, const MLIQVert* dataPtr, int y, int x, std::vector<Vector3>& verts, Matrix4x4& tranform) const noexcept
    {
        const auto liq = dataPtr[(y * mliq->width) + x];

        Vector3 base
        {
            mliq->position.x + (x * UNITSIZE),
            mliq->position.y + (y * UNITSIZE),
            std::fabsf(liq.waterVert.height) > 0.5f ? liq.waterVert.height : mliq->position.z + liq.waterVert.height
        };

        verts.emplace_back(tranform.Transform(base).ToRDCoords());
    }

    inline void GetDoodadVertsAndTris(CachedFileReader& reader, Structure& structure) const noexcept
    {
        if (MDDF* mddf = Mddf())
        {
            for (int i = 0; i < mddf->size / sizeof(MDDF::Entry); i++)
            {
                const auto entry = mddf->entries[i];

                Matrix4x4 tranform;

                if (entry.x != 0.0f || entry.y != 0.0f || entry.z != 0.0f)
                {
                    tranform.SetTranslation({ -(entry.z - WORLDSIZE), -(entry.x - WORLDSIZE), entry.y });
                }

                tranform.SetRotation({ entry.rz, entry.rx, entry.ry + 180.0f });

                std::string_view doodadPath(Mmdx()->filenames + Mmid()->offsets[entry.id]);
                const auto m2Name = std::format("{}.m2", doodadPath.substr(0, doodadPath.find_last_of('.')));

                if (const M2* m2 = reader.GetFileContent<M2>(m2Name.c_str()))
                {
                    if (const MD20* md20 = m2->Md20())
                    {
                        if (m2->IsCollideable())
                        {
                            std::lock_guard lock(structure.mutex);
                            const size_t vertsBase = structure.verts.size();

                            for (unsigned int d = 0; d < md20->countBoundingVertices; ++d)
                            {
                                const Vector3 v3 = *m2->Vertex(d);
                                structure.verts.emplace_back(tranform.Transform(v3).ToRDCoords());
                            }

                            for (unsigned int d = 0; d < md20->countBoundingTriangles; d += 3)
                            {
                                const auto t = m2->Tri(d);
                                structure.tris.emplace_back(Tri{ vertsBase + *t , vertsBase + *(t + 1), vertsBase + *(t + 2) });
                                structure.triTypes.emplace_back(DOODAD);
                            }
                        }
                    }
                }
            }
        }
    }
};
