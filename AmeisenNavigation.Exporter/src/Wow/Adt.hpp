#pragma once

#include <cstring>

#include "AdtStructs.hpp"
#include "Mver.hpp"

// ─────────────────────────────────────────────
// Adt - thin accessor class for ADT file data.
// Provides typed, bounds checked access to ADT sub-chunks via MHDR offsets.
// All data extraction logic lives in AdtChunkExtractor.hpp and RoadDetector.hpp.
// ─────────────────────────────────────────────

class Adt
{
    unsigned char* Data;
    unsigned int Size;

    /// True if [offset, offset + length) lies inside the file.
    bool InBounds(size_t offset, size_t length) const noexcept { return offset <= Size && length <= Size - offset; }

public:
    /// Basic sanity check: MVER + MHDR present and the MCIN table inside the file.
    bool IsValid() const noexcept
    {
        if (!Data || Size < sizeof(MVER) + sizeof(MHDR) || std::memcmp(Data, "REVM", 4) != 0
            || std::memcmp(Data + sizeof(MVER), "RDHM", 4) != 0)
        {
            return false;
        }

        return Mcin() != nullptr;
    }

    // ── Top-level chunk accessors ──

    const MVER* Mver() const noexcept { return reinterpret_cast<const MVER*>(Data); }
    const MHDR* Mhdr() const noexcept { return reinterpret_cast<const MHDR*>(Data + sizeof(MVER)); }

    /// Sub-chunk at an MHDR offset (relative to the MHDR data), nullptr if missing or out of bounds.
    template <typename T>
    T* GetSub(unsigned int offset, size_t minSize = 8) const noexcept
    {
        const size_t absolute = sizeof(MVER) + 8 + static_cast<size_t>(offset);
        return offset && InBounds(absolute, minSize) ? reinterpret_cast<T*>(Data + absolute) : nullptr;
    }

    const MCIN* Mcin() const noexcept { return GetSub<MCIN>(Mhdr()->offsetMcin, sizeof(MCIN)); }
    const MH2O* Mh2o() const noexcept { return GetSub<MH2O>(Mhdr()->offsetMh2o, sizeof(MH2O)); }
    const MTEX* Mtex() const noexcept { return GetSub<MTEX>(Mhdr()->offsetMtex); }

    const MMDX* Mmdx() const noexcept { return GetSub<MMDX>(Mhdr()->offsetMmdx); }
    const MMID* Mmid() const noexcept { return GetSub<MMID>(Mhdr()->offsetMmid); }
    const MDDF* Mddf() const noexcept { return GetSub<MDDF>(Mhdr()->offsetMddf); }

    const MWMO* Mwmo() const noexcept { return GetSub<MWMO>(Mhdr()->offsetMwmo); }
    const MWID* Mwid() const noexcept { return GetSub<MWID>(Mhdr()->offsetMwid); }
    const MODF* Modf() const noexcept { return GetSub<MODF>(Mhdr()->offsetModf); }

    /// True if a chunk's declared payload lies inside the file.
    template <typename T>
    bool ChunkInBounds(const T* chunk) const noexcept
    {
        if (!chunk)
        {
            return false;
        }

        const size_t offset = reinterpret_cast<const unsigned char*>(chunk) - Data;
        return InBounds(offset, 8) && InBounds(offset + 8, chunk->size);
    }

    /// Filename (e.g. of an MMDX/MWMO entry) by its MMID/MWID index, nullptr if invalid.
    template <typename Names, typename Offsets>
    const char* GetFilename(const Names* names, const Offsets* offsets, unsigned int index) const noexcept
    {
        if (!ChunkInBounds(names) || !ChunkInBounds(offsets) || index >= offsets->size / sizeof(uint32_t))
        {
            return nullptr;
        }

        const uint32_t offset = offsets->offsets[index];

        if (offset >= names->size || !std::memchr(names->filenames + offset, 0, names->size - offset))
        {
            return nullptr;
        }

        return names->filenames + offset;
    }

    // ── MCNK-level chunk accessors ──

    const MCNK* Mcnk(unsigned int x, unsigned int y) const noexcept
    {
        const MCIN* mcin = Mcin();

        if (!mcin || x >= ADT_CELLS_PER_GRID || y >= ADT_CELLS_PER_GRID)
        {
            return nullptr;
        }

        const unsigned int offset = mcin->cells[y][x].offsetMcnk;
        return offset && InBounds(offset, sizeof(MCNK)) ? reinterpret_cast<const MCNK*>(Data + offset) : nullptr;
    }

    const MCVT* Mcvt(const MCNK* mcnk) const noexcept
    {
        const size_t offset = reinterpret_cast<const unsigned char*>(mcnk) - Data + mcnk->offsMcvt;
        return mcnk->offsMcvt && InBounds(offset, sizeof(MCVT)) ? reinterpret_cast<const MCVT*>(Data + offset)
                                                                : nullptr;
    }

    const MCLY_Entry* Mcly(const MCNK* mcnk) const noexcept
    {
        // offsMcly points to the sub-chunk header (magic+size), skip 8 bytes to get to the entries.
        const size_t offset = reinterpret_cast<const unsigned char*>(mcnk) - Data + mcnk->offsMcly + 8;
        return mcnk->offsMcly && InBounds(offset, sizeof(MCLY_Entry) * mcnk->nLayers)
                   ? reinterpret_cast<const MCLY_Entry*>(Data + offset)
                   : nullptr;
    }

    const MCLQ* Mclq(const MCNK* mcnk) const noexcept
    {
        const MCLQ* mclq = mcnk->Mclq();

        if (!mclq)
        {
            return nullptr;
        }

        const size_t offset = reinterpret_cast<const unsigned char*>(mclq) - Data;
        return InBounds(offset, sizeof(MCLQ)) ? mclq : nullptr;
    }
};
