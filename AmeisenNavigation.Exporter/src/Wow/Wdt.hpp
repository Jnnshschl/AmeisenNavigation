#pragma once

#include <cstring>

#include "../Utils/Misc.hpp"
#include "AdtStructs.hpp"
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
    /// MVER + MPHD + MAIN present.
    inline bool IsValid() const noexcept
    {
        return Data && Size >= sizeof(MVER) + sizeof(MPHD) + sizeof(MAIN)
               && std::memcmp(Data + sizeof(MVER) + sizeof(MPHD), "NIAM", 4) == 0;
    }

    inline const MVER* Mver() const noexcept { return reinterpret_cast<MVER*>(Data); };
    inline const MPHD* Mphd() const noexcept { return reinterpret_cast<MPHD*>(Data + sizeof(MVER)); };
    inline const MAIN* Main() const noexcept { return reinterpret_cast<MAIN*>(Data + sizeof(MVER) + sizeof(MPHD)); };

    /// MPHD flag 0x01: the map is a single global WMO (most dungeons/raids), described by MWMO + MODF.
    inline bool HasGlobalWmo() const noexcept { return (Mphd()->data[0] & 0x01) != 0 && GlobalWmoName() && GlobalWmoPlacement(); }

    /// File name of the global WMO, nullptr if missing.
    inline const char* GlobalWmoName() const noexcept
    {
        const MWMO* mwmo = GetSubChunk(Data, Size, MWMO);

        if (!mwmo || mwmo->size == 0 || !std::memchr(mwmo->filenames, 0, mwmo->size))
        {
            return nullptr;
        }

        return mwmo->filenames[0] ? mwmo->filenames : nullptr;
    }

    /// Placement of the global WMO, nullptr if missing.
    inline const MODF::Entry* GlobalWmoPlacement() const noexcept
    {
        const MODF* modf = GetSubChunk(Data, Size, MODF);
        return modf && modf->size >= sizeof(MODF::Entry) ? &modf->entries[0] : nullptr;
    }
};
