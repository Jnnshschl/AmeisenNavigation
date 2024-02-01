#pragma once

#include "Mver.hpp"

constexpr auto TILESIZE = 533.33333f;
constexpr auto CHUNKSIZE = TILESIZE / 16.0f;
constexpr auto UNITSIZE = CHUNKSIZE / 8.0f;
constexpr auto HALFUNITSIZE = UNITSIZE / 2.0f;

constexpr auto ADT_CELLS_PER_GRID = 16;
constexpr auto ADT_CELL_SIZE = 8;
constexpr auto ADT_GRID_SIZE = ADT_CELLS_PER_GRID * ADT_CELL_SIZE;

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
    unsigned int offsetExistsBitmap;
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

    inline unsigned long long GetExistsBitmap(AdtLiquid* h) noexcept
    {
        return h->offsetExistsBitmap ? *reinterpret_cast<unsigned long long*>(reinterpret_cast<unsigned char*>(this) + 8 + h->offsetExistsBitmap)
            : 0xFFFFFFFFFFFFFFFFuLL;
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
    unsigned int offsMCcse;
    unsigned int nSndEmitters;
    unsigned int offsMclq;
    unsigned int sizeMclq;
    float  x;
    float  y;
    float  z;
    unsigned int offsMccv;
    unsigned int props;
    unsigned int effectId;
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

class Adt
{
    unsigned char* Data;

public:

    Adt(unsigned char* data) noexcept
        : Data(data)
    {}

    ~Adt() noexcept
    {
        if (Data) free(Data);
    }

    inline MVER* Mver() noexcept { return reinterpret_cast<MVER*>(Data); };
    inline MHDR* Mhdr() noexcept { return reinterpret_cast<MHDR*>(Data + sizeof(MVER)); };

    inline MCIN* Mcin() noexcept
    {
        unsigned int offset = Mhdr()->offsetMcin;
        return offset ? reinterpret_cast<MCIN*>(Data + sizeof(MVER) + 8 + offset) : nullptr;
    };

    inline MH2O* Mh2o() noexcept
    {
        unsigned int offset = Mhdr()->offsetMh2o;
        return offset ? reinterpret_cast<MH2O*>(Data + sizeof(MVER) + 8 + offset) : nullptr;
    };

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

    inline void GetTerrainVertsAndTris(unsigned int x, unsigned int y, std::vector<Vector3>& vertexes, std::vector<Tri>& tris) noexcept
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

                    vertexes.emplace_back(v3);

                    if (unitCount == 8)
                    {
                        // shift the inner row by HALFUNITSIZE
                        v3.y -= HALFUNITSIZE;

                        // check for holes in the inner grid, if there is no hole, generate tris
                        if (!mcnk->holes || !((mcnk->holes & 0x0000FFFFu) & ((1 << (i / 2)) << ((j / 4) << 2))))
                        {
                            int vertexCount = vertexes.size();
                            tris.emplace_back(Tri{ vertexCount - 9, vertexCount, vertexCount - 8 }); // Top
                            tris.emplace_back(Tri{ vertexCount + 9, vertexCount, vertexCount + 8 }); // Bottom
                            tris.emplace_back(Tri{ vertexCount - 8, vertexCount, vertexCount + 9 }); // Right
                            tris.emplace_back(Tri{ vertexCount + 8, vertexCount, vertexCount - 9 }); // Left
                        }
                    }
                }
            }
        }
    }

    inline void GetLiquidVertsAndTris(unsigned int x, unsigned int y, std::vector<Vector3>& vertexes, std::vector<Tri>& tris) noexcept
    {
        if (MH2O* mh2o = Mh2o())
        {
            if (AdtLiquid* liquid = mh2o->GetInstance(x, y))
            {
                AdtLiquidAttributes* attributes = mh2o->GetAttributes(x, y);
                unsigned long long existBitmap = mh2o->GetExistsBitmap(liquid);
            }
        }
    }
};
