#pragma once

#include <cstdint>
#include <cstring>

#define GetSubChunk(m, s, t) FindSubChunk<t>(m, s, #t)

namespace ChunkDetail {
/// Size of the MOGP header data in WMO group files (sub-chunks follow it inside MOGP).
constexpr uint32_t MOGP_HEADER_SIZE = 0x44;

/// Chunk magics are stored reversed on disk ("MVER" -> "REVM").
inline bool MagicEquals(const unsigned char* memory, const char chunkName[5]) noexcept
{
    return memory[0] == static_cast<unsigned char>(chunkName[3]) && memory[1] == static_cast<unsigned char>(chunkName[2])
           && memory[2] == static_cast<unsigned char>(chunkName[1])
           && memory[3] == static_cast<unsigned char>(chunkName[0]);
}

/// Walk the chunk list [begin, end) and return the chunk with the given name. Descends into MOGP.
/// `wellFormed` is cleared when a chunk header points past the end of the buffer.
inline unsigned char* WalkChunks(unsigned char* begin, unsigned char* end, const char chunkName[5], bool& wellFormed,
                                 int depth = 0) noexcept
{
    unsigned char* cursor = begin;

    while (end - cursor >= 8)
    {
        uint32_t size = 0;
        std::memcpy(&size, cursor + 4, sizeof(size));

        if (size > static_cast<uint32_t>(end - cursor - 8))
        {
            wellFormed = false;
            return nullptr;
        }

        if (MagicEquals(cursor, chunkName))
        {
            return cursor;
        }

        if (depth == 0 && MagicEquals(cursor, "MOGP") && size >= MOGP_HEADER_SIZE)
        {
            if (unsigned char* found =
                    WalkChunks(cursor + 8 + MOGP_HEADER_SIZE, cursor + 8 + size, chunkName, wellFormed, 1))
            {
                return found;
            }
        }

        cursor += 8 + size;
    }

    return nullptr;
}
} // namespace ChunkDetail

/// Find a chunk by name in a chunked WoW file (WMO root/group, ...). Walks the chunk headers so chunk
/// names appearing inside other chunks' data can't produce false matches, falls back to a byte scan
/// for files with non-standard layouts.
template <typename T>
T* FindSubChunk(unsigned char* memory, unsigned int size, const char chunkName[5]) noexcept
{
    if (!memory || size < 8)
    {
        return nullptr;
    }

    bool wellFormed = true;

    if (unsigned char* chunk = ChunkDetail::WalkChunks(memory, memory + size, chunkName, wellFormed))
    {
        return reinterpret_cast<T*>(chunk);
    }

    if (wellFormed)
    {
        return nullptr; // clean walk, the chunk really doesn't exist
    }

    // Byte scan fallback: only accept a match whose header and declared size fit into the buffer, callers derive
    // element counts from that size.
    for (unsigned char* cursor = memory, *end = memory + (size - 8); cursor <= end; ++cursor)
    {
        if (ChunkDetail::MagicEquals(cursor, chunkName))
        {
            uint32_t chunkSize = 0;
            std::memcpy(&chunkSize, cursor + 4, sizeof(chunkSize));

            if (chunkSize <= static_cast<uint32_t>(memory + size - cursor - 8))
            {
                return reinterpret_cast<T*>(cursor);
            }
        }
    }

    return nullptr;
}
