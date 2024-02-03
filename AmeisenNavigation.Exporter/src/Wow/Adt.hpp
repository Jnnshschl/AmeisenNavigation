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

    struct MDDFData
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

    struct MODFData
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

    inline AdtLiquid* GetInstance(unsigned int x, unsigned int y) noexcept
    {
        if (liquid[x][y].used && liquid[x][y].offsetInstances)
        {
            return reinterpret_cast<AdtLiquid*>(reinterpret_cast<unsigned char*>(this) + 8 + liquid[x][y].offsetInstances);
        }

        return nullptr;
    }

    inline AdtLiquidAttributes* GetAttributes(unsigned int x, unsigned int y) noexcept
    {
        if (liquid[x][y].used)
        {
            if (liquid[x][y].offsetAttributes)
            {
                return reinterpret_cast<AdtLiquidAttributes*>(reinterpret_cast<unsigned char*>(this) + 8 + liquid[x][y].offsetAttributes);
            }

            static AdtLiquidAttributes all{ 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF };
            return &all;
        }

        return nullptr;
    }

    inline unsigned long long* GetRenderMask(AdtLiquid* h) noexcept
    {
        return h->offsetRenderMask ? reinterpret_cast<unsigned long long*>(reinterpret_cast<unsigned char*>(this) + 8 + h->offsetRenderMask)
            : nullptr;
    }

    inline float* GetLiquidHeight(AdtLiquid* liquid) noexcept
    {
        if (!liquid->offsetVertexData)
        {
            return nullptr;
        }

        switch (liquid->vertexFormat)
        {
        case AdtLiquidVertexFormat::HeightDepth:
        case AdtLiquidVertexFormat::HeightTextureCoord:
            return reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + 8 + liquid->offsetVertexData);

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
    inline MVER* Mver() noexcept { return reinterpret_cast<MVER*>(Data); };
    inline MHDR* Mhdr() noexcept { return reinterpret_cast<MHDR*>(Data + sizeof(MVER)); };

    template<typename T>
    inline T* GetSub(unsigned int offset) noexcept
    {
        return offset ? reinterpret_cast<T*>(Data + sizeof(MVER) + 8 + offset) : nullptr;
    };

    inline auto Mcin() noexcept { return GetSub<MCIN>(Mhdr()->offsetMcin); };
    inline auto Mh2o() noexcept { return GetSub<MH2O>(Mhdr()->offsetMh2o); };

    inline auto Mmdx() noexcept { return GetSub<MMDX>(Mhdr()->offsetMmdx); };
    inline auto Mmid() noexcept { return GetSub<MMID>(Mhdr()->offsetMmid); };
    inline auto Mddf() noexcept { return GetSub<MDDF>(Mhdr()->offsetMddf); };

    inline auto Mwmo() noexcept { return GetSub<MWMO>(Mhdr()->offsetMwmo); };
    inline auto Mwid() noexcept { return GetSub<MWID>(Mhdr()->offsetMwid); };
    inline auto Modf() noexcept { return GetSub<MODF>(Mhdr()->offsetModf); };

    inline MCNK* Mcnk(unsigned int x, unsigned int y) noexcept
    {
        unsigned int offset = Mcin()->cells[x][y].offsetMcnk;
        return offset ? reinterpret_cast<MCNK*>(Data + offset) : nullptr;
    };

    inline MCVT* Mcvt(MCNK* mcnk) noexcept
    {
        unsigned int offset = mcnk->offsMcvt;
        return offset ? reinterpret_cast<MCVT*>(reinterpret_cast<unsigned char*>(mcnk) + offset) : nullptr;
    };

    inline void GetTerrainVertsAndTris(unsigned int x, unsigned int y, std::vector<Vector3>& verts, std::vector<Tri>& tris) noexcept
    {
        if (MCNK* mcnk = Mcnk(x, y))
        {
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
                    if (MCVT* mcvt = Mcvt(mcnk))
                    {
                        v3.z += mcvt->heightMap[mcvtIndex];
                    }

                    mcvtIndex++;

                    const size_t vertexCount = verts.size();
                    verts.emplace_back(v3);

                    if (unitCount == 8)
                    {
                        // shift the inner row by HALFUNITSIZE
                        v3.y -= HALFUNITSIZE;

                        // check for holes in the inner grid, if there is no hole, generate tris
                        if (mcnk->IsHole(i, j))
                        {
                            tris.emplace_back(Tri{ TERRAIN_GROUND, vertexCount - 9, vertexCount, vertexCount - 8 }); // Top
                            tris.emplace_back(Tri{ TERRAIN_GROUND, vertexCount + 9, vertexCount, vertexCount + 8 }); // Bottom
                            tris.emplace_back(Tri{ TERRAIN_GROUND, vertexCount - 8, vertexCount, vertexCount + 9 }); // Right
                            tris.emplace_back(Tri{ TERRAIN_GROUND, vertexCount + 8, vertexCount, vertexCount - 9 }); // Left
                        }
                    }
                }
            }
        }
    }

    inline void GetLiquidVertsAndTris(unsigned int x, unsigned int y, std::vector<Vector3>& verts, std::vector<Tri>& tris) noexcept
    {
        if (MCNK* mcnk = Mcnk(x, y))
        {
            if (MH2O* mh2o = Mh2o())
            {
                if (AdtLiquid* liquid = mh2o->GetInstance(x, y))
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

                                const size_t vertsIndex = verts.size();
                                verts.push_back({ cx, cy, cz });
                                verts.push_back({ cx - UNITSIZE, cy, cz });
                                verts.push_back({ cx, cy - UNITSIZE, cz });
                                verts.push_back({ cx - UNITSIZE, cy - UNITSIZE, cz });

                                tris.push_back({ isOcean ? LIQUID_OCEAN : LIQUID_WATER, vertsIndex + 2, vertsIndex, vertsIndex + 1 });
                                tris.push_back({ isOcean ? LIQUID_OCEAN : LIQUID_WATER, vertsIndex + 1, vertsIndex + 3, vertsIndex + 2 });
                            }
                        }
                    }
                }
            }
        }
    }

    inline void GetWmoVertsAndTris(CachedFileReader& wmoReader, std::vector<Vector3>& verts, std::vector<Tri>& tris) noexcept
    {
        if (MODF* modf = Modf())
        {
            std::unordered_map<unsigned int, unsigned char*> wmoFiles;

            for (int i = 0; i < modf->size / sizeof(MODF::MODFData); i++)
            {
                const auto& entry = modf->entries[i];
                const auto wmoRootFilename = Mwmo()->filenames + Mwid()->offsets[entry.id];

                if (Wmo* wmo = wmoReader.GetFileContent<Wmo>(wmoRootFilename))
                {
                    if (MOHD* mohd = wmo->Mohd())
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

                            if (WmoGroup* wmoGroup = wmoReader.GetFileContent<WmoGroup>(wmoGroupName.c_str()))
                            {
                                if (MOVT* movt = wmoGroup->Movt())
                                {
                                    if (MOVI* movi = wmoGroup->Movi())
                                    {
                                        if (MONR* monr = wmoGroup->Monr())
                                        {
                                            if (MOPY* mopy = wmoGroup->Mopy())
                                            {
                                                const size_t vertsBase = verts.size();

                                                for (unsigned int i = 0; i < movt->Count(); ++i)
                                                {
                                                    verts.push_back(tranform.Transform(movt->verts[i]));
                                                }

                                                for (unsigned int i = 0; i < movi->Count(); i += 3)
                                                {
                                                    if ((mopy->data[i / 3].flags & 0x04) != 0 && mopy->data[i / 3].materials != 0xFF)
                                                    {
                                                        continue;
                                                    }

                                                    tris.push_back({ WMO, vertsBase + movi->tris[i] , vertsBase + movi->tris[i + 1], vertsBase + movi->tris[i + 2] });
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (MODD* modd = wmo->Modd())
                        {
                            if (modd->size > 0)
                            {
                                if (MODS* mods = wmo->Mods())
                                {
                                    if (MODN* modn = wmo->Modn())
                                    {
                                        for (unsigned int m = 0; m < modd->size / sizeof(MODD::Definition); m++)
                                        {
                                            const auto& definition = modd->defs[m];
                                            std::string_view doodadPath(modn->names + definition.nameIndex);
                                            const auto m2Name = std::format("{}.m2", doodadPath.substr(0, doodadPath.find_last_of('.')), i);

                                            if (M2* m2 = wmoReader.GetFileContent<M2>(m2Name.c_str()))
                                            {
                                                if (MD20* md20 = m2->Md20())
                                                {
                                                    if (m2->IsCollideable())
                                                    {
                                                        Matrix4x4 doodadTranform;
                                                        doodadTranform.SetTranslation(definition.position);
                                                        doodadTranform.SetRotation({ 0.0f, 180.0f, 0.0f });
                                                        doodadTranform.SetRotation(-definition.qy, definition.qz, -definition.qx, definition.qw);
                                                        doodadTranform.Multiply(tranform);

                                                        const size_t vertsBase = verts.size();

                                                        for (unsigned int i = 0; i < md20->countBoundingVertices; ++i)
                                                        {
                                                            const Vector3 v3 = *m2->Vertex(i);
                                                            verts.push_back(doodadTranform.Transform(v3));
                                                        }

                                                        for (unsigned int i = 0; i < md20->countBoundingTriangles; i += 3)
                                                        {
                                                            const auto t = m2->Tri(i);
                                                            tris.push_back({ WMO, vertsBase + *t , vertsBase + *(t + 1), vertsBase + *(t + 2) });
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
    }

    inline void GetDoodadVertsAndTris(unsigned int x, unsigned int y, CachedFileReader& m2Reader, std::vector<Vector3>& verts, std::vector<Tri>& tris) noexcept
    {
        if (MCNK* mcnk = Mcnk(x, y))
        {
            if (MDDF* mddf = Mddf())
            {
                std::unordered_map<unsigned int, unsigned char*> mdxFiles;

                for (int i = 0; i < mddf->size / sizeof(MDDF::MDDFData); i++)
                {
                    const auto entry = mddf->entries[i];

                    if (M2* m2 = m2Reader.GetFileContent<M2>(Mmdx()->filenames + Mmid()->offsets[entry.id]))
                    {
                    }
                }
            }
        }
    }
};
