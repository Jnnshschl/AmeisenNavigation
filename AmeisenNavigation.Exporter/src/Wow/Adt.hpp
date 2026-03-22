#pragma once

#include "AdtStructs.hpp"
#include "Mver.hpp"

// ─────────────────────────────────────────────
// Adt - thin accessor class for ADT file data.
// Provides typed access to ADT sub-chunks via MHDR offsets.
// All data extraction logic lives in AdtChunkExtractor.hpp
// and RoadDetector.hpp as free functions.
// ─────────────────────────────────────────────

class Adt
{
    unsigned char* Data;
    unsigned int Size;

public:
    // ── Top-level chunk accessors ──

    inline const MVER* Mver() const noexcept { return reinterpret_cast<MVER*>(Data); };
    inline const MHDR* Mhdr() const noexcept { return reinterpret_cast<MHDR*>(Data + sizeof(MVER)); };

    template <typename T>
    inline T* GetSub(unsigned int offset) const noexcept
    {
        return offset ? reinterpret_cast<T*>(Data + sizeof(MVER) + 8 + offset) : nullptr;
    };

    inline const auto Mcin() const noexcept { return GetSub<MCIN>(Mhdr()->offsetMcin); };
    inline const auto Mh2o() const noexcept { return GetSub<MH2O>(Mhdr()->offsetMh2o); };
    inline const auto Mtex() const noexcept { return GetSub<MTEX>(Mhdr()->offsetMtex); };

    inline const auto Mmdx() const noexcept { return GetSub<MMDX>(Mhdr()->offsetMmdx); };
    inline const auto Mmid() const noexcept { return GetSub<MMID>(Mhdr()->offsetMmid); };
    inline auto Mddf() const noexcept { return GetSub<MDDF>(Mhdr()->offsetMddf); };

    inline const auto Mwmo() const noexcept { return GetSub<MWMO>(Mhdr()->offsetMwmo); };
    inline const auto Mwid() const noexcept { return GetSub<MWID>(Mhdr()->offsetMwid); };
    inline const auto Modf() const noexcept { return GetSub<MODF>(Mhdr()->offsetModf); };

    // ── MCNK-level chunk accessors ──

    inline const MCNK* Mcnk(unsigned int x, unsigned int y) noexcept
    {
        unsigned int offset = Mcin()->cells[y][x].offsetMcnk;
        return offset ? reinterpret_cast<MCNK*>(Data + offset) : nullptr;
    };

    inline const MCVT* Mcvt(const MCNK* mcnk) noexcept
    {
        unsigned int offset = mcnk->offsMcvt;
        return offset ? reinterpret_cast<const MCVT*>(reinterpret_cast<const unsigned char*>(mcnk) + offset) : nullptr;
    };

    inline const MCLY_Entry* Mcly(const MCNK* mcnk) noexcept
    {
        unsigned int offset = mcnk->offsMcly;
        // offsMcly points to the sub-chunk header (magic+size), skip 8 bytes to get data
        return offset ? reinterpret_cast<const MCLY_Entry*>(reinterpret_cast<const unsigned char*>(mcnk) + offset + 8)
                      : nullptr;
    };
};
