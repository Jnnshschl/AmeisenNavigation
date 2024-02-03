#pragma once

#pragma pack(push, 1)
struct MVER
{
    unsigned char magic[4];
    unsigned int size;
    unsigned int version;
};
#pragma pack(pop)
