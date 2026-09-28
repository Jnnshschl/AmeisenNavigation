#pragma once

#include <cstdint>

constexpr uint32_t MMAP_MAGIC = 0x4D4D4150; // 'MMAP'
constexpr uint32_t MMAP_VERSION = 15;

/// Header in front of every TrinityCore/SkyFire .mmtile file.
struct MmapTileHeader
{
    uint32_t mmapMagic;
    uint32_t dtVersion;
    uint32_t mmapVersion;
    uint32_t size;
    char usesLiquids;
    char padding[3];
};

static_assert(sizeof(MmapTileHeader) == 20, "MmapTileHeader must match the on-disk layout");
