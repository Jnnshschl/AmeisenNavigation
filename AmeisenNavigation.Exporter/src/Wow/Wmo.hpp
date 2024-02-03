#pragma once

#include "Mver.hpp"
#include "../Utils/Misc.hpp"

#pragma pack(push, 1)
struct MODD
{
    unsigned char magic[4];
    unsigned int size;

    struct Definition
    {
        unsigned int nameIndex;
        Vector3 position;
        float qx;
        float qy;
        float qz;
        float qw;
        float scale;
        unsigned char b;
        unsigned char g;
        unsigned char r;
        unsigned char a;
    }defs[1];
};

struct MODN
{
    unsigned char magic[4];
    unsigned int size;
    char names[1];
};

struct MODS
{
    unsigned char magic[4];
    unsigned int size;

    struct
    {
        char name[20];
        unsigned int startIndex;
        unsigned int count;
        char pad[4];
    }set[1];
};

struct MOHD
{
    unsigned char magic[4];
    unsigned int size;
    unsigned int textureCount;
    unsigned int groupCount;
    unsigned int portalCount;
    unsigned int lightCount;
    unsigned int doodadNameCount;
    unsigned int doodadDefCount;
    unsigned int doodadSetCount;
    unsigned int color;
    unsigned int rootWmoId;
    float bbMin[3];
    float bbMax[3];
    unsigned int flags;
};
#pragma pack (pop)

struct Wmo
{
    unsigned char* Data;
    unsigned int Size;

public:
    inline MVER* Mver() noexcept { return reinterpret_cast<MVER*>(Data); };
    inline MOHD* Mohd() noexcept { return reinterpret_cast<MOHD*>(Data + sizeof(MVER)); };
    inline MODS* Mods() noexcept { return GetSubChunk(Data, Size, MODS); }
    inline MODN* Modn() noexcept { return GetSubChunk(Data, Size, MODN); }
    inline MODD* Modd() noexcept { return GetSubChunk(Data, Size, MODD); }
};
