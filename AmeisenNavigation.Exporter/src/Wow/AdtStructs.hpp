#pragma once

#include <cstdint>

// ─────────────────────────────────────────────
// ADT size constants
// ─────────────────────────────────────────────

constexpr auto TILESIZE = 533.33333f;
constexpr auto WORLDSIZE = TILESIZE * 32.0f;
constexpr auto CHUNKSIZE = TILESIZE / 16.0f;
constexpr auto UNITSIZE = CHUNKSIZE / 8.0f;
constexpr auto HALFUNITSIZE = UNITSIZE / 2.0f;

constexpr auto ADT_CELLS_PER_GRID = 16;
constexpr auto ADT_CELL_SIZE = 8;
constexpr auto ADT_GRID_SIZE = ADT_CELLS_PER_GRID * ADT_CELL_SIZE;

// ─────────────────────────────────────────────
// ADT packed chunk structures
// All structs include their 8-byte chunk header (magic[4] + size)
// ─────────────────────────────────────────────

#pragma pack(push, 1)

// MMDX — M2 model filename list (null-terminated strings)
struct MMDX
{
    unsigned char magic[4];
    unsigned int size;
    char filenames[1];
};

// MMID — offsets into MMDX filenames
struct MMID
{
    unsigned char magic[4];
    unsigned int size;
    uint32_t offsets[1];
};

// MDDF — M2 doodad placement entries
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

// MWMO — WMO filename list (null-terminated strings)
struct MWMO
{
    unsigned char magic[4];
    unsigned int size;
    char filenames[1];
};

// MWID — offsets into MWMO filenames
struct MWID
{
    unsigned char magic[4];
    unsigned int size;
    uint32_t offsets[1];
};

// MODF — WMO placement entries
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

// MTEX — texture filename list (null-terminated strings)
struct MTEX
{
    unsigned char magic[4];
    unsigned int size;
    char filenames[1]; // zero-terminated strings, concatenated
};

// MCLY — single terrain texture layer entry (16 bytes)
struct MCLY_Entry
{
    unsigned int textureId;    // index into MTEX filename array
    unsigned int flags;        // bit 8 = use_alpha_map, bit 9 = compressed
    unsigned int offsetInMCAL; // offset into MCAL sub-chunk
    unsigned int effectId;     // FK to GroundEffectTexture.dbc, 0xFFFFFFFF = none
};

// ─────────────────────────────────────────────
// MH2O (liquid) structures
// ─────────────────────────────────────────────

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

    inline const AdtLiquid* GetInstance(unsigned int x, unsigned int y, unsigned int index = 0) const noexcept
    {
        if (liquid[y][x].offsetInstances == 0 || index >= liquid[y][x].used)
            return nullptr;
        return reinterpret_cast<const AdtLiquid*>(reinterpret_cast<const unsigned char*>(this) + 8 +
                                                  liquid[y][x].offsetInstances) +
               index;
    }

    inline const AdtLiquidAttributes* GetAttributes(unsigned int x, unsigned int y,
                                                    unsigned int index = 0) const noexcept
    {
        if (liquid[y][x].offsetAttributes == 0 || index >= liquid[y][x].used)
            return nullptr;
        return reinterpret_cast<const AdtLiquidAttributes*>(reinterpret_cast<const unsigned char*>(this) + 8 +
                                                            liquid[y][x].offsetAttributes) +
               index;
    }

    inline const unsigned char* GetRenderMask(const AdtLiquid* h) const noexcept
    {
        return h->offsetRenderMask ? reinterpret_cast<const unsigned char*>(
                                         reinterpret_cast<const unsigned char*>(this) + 8 + h->offsetRenderMask)
                                   : nullptr;
    }

    inline const void* GetLiquidHeight(const AdtLiquid* liquid) const noexcept
    {
        return liquid->offsetVertexData ? reinterpret_cast<const void*>(reinterpret_cast<const unsigned char*>(this) +
                                                                        8 + liquid->offsetVertexData)
                                        : nullptr;
    }
};

// ─────────────────────────────────────────────
// MCNK sub-chunk structures
// ─────────────────────────────────────────────

// MCVT — height map vertices (9×9 + 8×8 = 145 floats)
struct MCVT
{
    unsigned char magic[4];
    unsigned int size;
    float heightMap[(9 * 9) + (8 * 8)];
};

// MCNK — map chunk header (128 bytes = 0x80, including 8-byte chunk header)
// Layout matches wowdev.wiki ADT/v18 exactly.
// All field offsets documented relative to data start (after magic+size).
struct MCNK
{
    unsigned char magic[4]; // +0x00 (struct offset)
    unsigned int size;      // +0x04

    // --- Fields below at data offset 0x00 (struct offset 0x08) ---
    unsigned int flags;       // data+0x00
    unsigned int ix;          // data+0x04 (IndexX)
    unsigned int iy;          // data+0x08 (IndexY)
    unsigned int nLayers;     // data+0x0C
    unsigned int nDoodadRefs; // data+0x10
    unsigned int offsMcvt;    // data+0x14 (ofsHeight)
    unsigned int offsMcnr;    // data+0x18 (ofsNormal)
    unsigned int offsMcly;    // data+0x1C (ofsLayer)
    unsigned int offsMcrf;    // data+0x20 (ofsRefs)
    unsigned int offsMcal;    // data+0x24 (ofsAlpha)
    unsigned int sizeMcal;    // data+0x28 (sizeAlpha)
    unsigned int offsMcsh;    // data+0x2C (ofsShadow)
    unsigned int sizeMcsh;    // data+0x30 (sizeShadow)
    unsigned int areaid;      // data+0x34
    unsigned int nMapObjRefs; // data+0x38
    unsigned short holes;     // data+0x3C (holes_low_res)
    unsigned short pad;       // data+0x3E (unknown_but_used)

    // data+0x40: uint2_t[8][8] predominantTexture — 128 bits = 16 bytes
    // Packed: 2 bits per sub-cell, row-major, LSB-first
    unsigned char predTex[16]; // data+0x40..0x4F

    // data+0x50: uint1_t[8][8] noEffectDoodad — 64 bits = 8 bytes
    unsigned char noEffectDoodad[8]; // data+0x50..0x57

    unsigned int offsMcse;     // data+0x58 (ofsSndEmitters)
    unsigned int nSndEmitters; // data+0x5C
    unsigned int offsMclq;     // data+0x60 (ofsLiquid - old MCLQ)
    unsigned int sizeMclq;     // data+0x64

    float x; // data+0x68 (position.x — WoW X, north/south)
    float y; // data+0x6C (position.y — WoW Y, east/west)
    float z; // data+0x70 (position.z — WoW Z, height)

    unsigned int offsMccv; // data+0x74
    unsigned int offsMclv; // data+0x78 (Cata+, unused pre-Cata)
    unsigned int unused;   // data+0x7C

    inline bool IsHole(unsigned int x, unsigned int y) const noexcept
    {
        if (!holes)
            return false;
        return ((holes & 0x0000FFFFu) & ((1 << (x / 2)) << ((y / 4) << 2))) != 0;
    }
};

// MCIN — chunk index table
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

// MHDR — ADT file header with sub-chunk offsets
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
