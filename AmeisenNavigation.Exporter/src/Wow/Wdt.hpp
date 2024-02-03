#pragma once

#include "Mver.hpp"

constexpr auto WDT_MAP_SIZE = 64;

#pragma pack(push, 1)
struct MPHD
{
    unsigned char magic[4];
    unsigned int size;
    unsigned int data[8];
};

struct MAIN
{
    unsigned char magic[4];
    unsigned int size;

    struct AdtData
    {
        unsigned int exists;
        unsigned int data;
    } adt[64][64];
};
#pragma pack(pop)

class Wdt
{
    unsigned char* Data;
    unsigned int Size;

public:
    inline MVER* Mver() noexcept { return reinterpret_cast<MVER*>(Data); };
    inline MPHD* Mphd() noexcept { return reinterpret_cast<MPHD*>(Data + sizeof(MVER)); };
    inline MAIN* Main() noexcept { return reinterpret_cast<MAIN*>(Data + sizeof(MVER) + sizeof(MPHD)); };
};
