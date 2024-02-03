#pragma once

#include "Mver.hpp"
#include "../Utils/Misc.hpp"

#pragma pack(push, 1)
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
    inline MVER* Mver() noexcept { return reinterpret_cast<MVER*>(Data); };
    inline MOGP* Mogp() noexcept { return reinterpret_cast<MOGP*>(Data + sizeof(MVER)); };
    inline MOVT* Movt() noexcept { return GetSubChunk(Data, Size, MOVT); }
    inline MOVI* Movi() noexcept { return GetSubChunk(Data, Size, MOVI); }
    inline MONR* Monr() noexcept { return GetSubChunk(Data, Size, MONR); }
    inline MOPY* Mopy() noexcept { return GetSubChunk(Data, Size, MOPY); }
};
