#pragma once

#include <cstdint>

#include "../Utils/Vector3.hpp"
#include "Mver.hpp"
#include "../Utils/Misc.hpp"

#pragma pack(push, 1)
struct MLIQVert
{
    union
    {
        struct
        {
            unsigned char flow1;
            unsigned char flow2;
            unsigned char flow1Pct;
            unsigned char filler;
            float height;
        }  waterVert;
        struct
        {
            unsigned short s;
            unsigned short t;
            float height;
        } magmaVert;
    };
};

struct MLIQ
{
    unsigned char magic[4];
    unsigned int size;
    unsigned int countXVertices;
    unsigned int countYVertices;
    unsigned int width;
    unsigned int height;
    Vector3 position;
    unsigned short materialId;
};

struct MOPY
{
    unsigned char magic[4];
    unsigned int size;
    struct
    {
        unsigned char flags;
        unsigned char materials;
    }data[1];

    inline unsigned int Count() const noexcept { return size / sizeof(data); }
};

struct MONR
{
    unsigned char magic[4];
    unsigned int size;
    Vector3 normals[1];

    inline unsigned int Count() const noexcept { return size / sizeof(Vector3); }
};

struct MOVI
{
    unsigned char magic[4];
    unsigned int size;
    unsigned short tris[1];

    inline unsigned int Count() const noexcept { return size / sizeof(tris); }
};

struct MOVT
{
    unsigned char magic[4];
    unsigned int size;
    Vector3 verts[1];

    inline unsigned int Count() const noexcept { return size / sizeof(Vector3); }
};

struct MOGP
{
    unsigned char magic[4];
    unsigned int size;
    unsigned int groupName;
    unsigned int descGroupName;
    unsigned int flags;
    float bbMin[3];
    float bbMax[3];
    unsigned short moprIdx;
    unsigned short moprNItems;
    unsigned short nBatchA;
    unsigned short nBatchB;
    unsigned int nBatchC;
    unsigned int fogIdx;
    unsigned int groupLiquid;
    unsigned int groupWMOID;
};
#pragma pack (pop)

struct WmoGroup
{
    unsigned char* Data;
    unsigned int Size;

public:
    inline const MVER* Mver() const noexcept { return reinterpret_cast<MVER*>(Data); };
    /// Group header, nullptr if the file is too small to hold it.
    inline const MOGP* Mogp() const noexcept
    {
        return Data && Size >= sizeof(MVER) + sizeof(MOGP) ? reinterpret_cast<MOGP*>(Data + sizeof(MVER)) : nullptr;
    }
    inline const MOVT* Movt() const noexcept { return GetSubChunk(Data, Size, MOVT); }
    inline const MOVI* Movi() const noexcept { return GetSubChunk(Data, Size, MOVI); }
    inline const MONR* Monr() const noexcept { return GetSubChunk(Data, Size, MONR); }
    inline const MOPY* Mopy() const noexcept { return GetSubChunk(Data, Size, MOPY); }
    inline const MLIQ* Mliq() const noexcept { return GetSubChunk(Data, Size, MLIQ); }

    /// True if the MLIQ vertex grid and flag array fit into the chunk and the grid covers width x height quads.
    inline bool LiquidInBounds(const MLIQ* mliq) const noexcept
    {
        if (!mliq || mliq->width >= mliq->countXVertices || mliq->height >= mliq->countYVertices)
        {
            return false;
        }

        const uint64_t required = sizeof(MLIQ)
                                  + static_cast<uint64_t>(mliq->countXVertices) * mliq->countYVertices * sizeof(MLIQVert)
                                  + static_cast<uint64_t>(mliq->width) * mliq->height;
        const uint64_t available = Size - static_cast<uint64_t>(reinterpret_cast<const unsigned char*>(mliq) - Data);
        return required <= 8ull + mliq->size && required <= available;
    }
};
