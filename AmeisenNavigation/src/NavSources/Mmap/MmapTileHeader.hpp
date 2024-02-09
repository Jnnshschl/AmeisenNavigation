#pragma once

constexpr auto MMAP_MAGIC = 0x4D4D4150;
constexpr auto MMAP_VERSION = 15;

struct MmapTileHeader
{
    unsigned int mmapMagic;
    unsigned int dtVersion;
    unsigned int mmapVersion;
    unsigned int size;
    char usesLiquids;
    char padding[3];
};
