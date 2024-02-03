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
    inline const MVER* Mver() const noexcept { return reinterpret_cast<MVER*>(Data); };
    inline const MPHD* Mphd() const noexcept { return reinterpret_cast<MPHD*>(Data + sizeof(MVER)); };
    inline const MAIN* Main() const noexcept { return reinterpret_cast<MAIN*>(Data + sizeof(MVER) + sizeof(MPHD)); };
};
