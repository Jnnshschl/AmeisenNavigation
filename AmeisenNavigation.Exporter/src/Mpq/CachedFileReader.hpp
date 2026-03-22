#pragma once

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include <Utils/Logger.hpp>

#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "../Utils/xxhash/xxhash.h"

#include "MpqManager.hpp"

/// Matches the binary layout of file wrapper classes (Dbc, Wdt, Adt, Wmo, M2, etc.)
/// which all start with: unsigned char* Data; unsigned int Size;
/// This struct is what GetFileContent<T> actually returns a pointer to.
struct CachedFileEntry
{
    unsigned char* Data;
    unsigned int Size;
};

class CachedFileReader
{
    MpqManager* Mpq;
    std::shared_mutex CacheMutex;
    std::unordered_map<XXH64_hash_t, CachedFileEntry> Cache;

public:
    explicit CachedFileReader(MpqManager* mpqManager) noexcept
        : Mpq(mpqManager),
        CacheMutex(),
        Cache()
    {}

    /// Returns a pointer to a cached file entry, reinterpreted as T*.
    /// T must have layout-compatible first two members: unsigned char* Data; unsigned int Size;
    /// (e.g., Dbc, Wdt, Adt, Wmo, M2).
    template<typename T>
    inline T* GetFileContent(const char* filename) noexcept
    {
        const auto hash = XXH3_64bits(filename, strlen(filename));

        // Fast path: shared lock for cache hits (concurrent reads)
        {
            std::shared_lock readLock(CacheMutex);
            auto it = Cache.find(hash);
            if (it != Cache.end())
            {
                return it->second.Data && it->second.Size > 0 ? reinterpret_cast<T*>(&it->second) : nullptr;
            }
        }

        // Slow path: exclusive lock for cache misses
        std::unique_lock writeLock(CacheMutex);

        // Double-check after acquiring exclusive lock
        auto it = Cache.find(hash);
        if (it != Cache.end())
        {
            return it->second.Data && it->second.Size > 0 ? reinterpret_cast<T*>(&it->second) : nullptr;
        }

        unsigned int size = 0;
        if (unsigned char* ptr = Mpq->GetFileContent(filename, size))
        {
            Cache[hash] = CachedFileEntry{ptr, size};
        }
        else
        {
            LogW("Failed to load MPQ file: ", filename);
            Cache[hash] = CachedFileEntry{nullptr, 0};
        }

        auto& entry = Cache[hash];
        return entry.Data && entry.Size > 0 ? reinterpret_cast<T*>(&entry) : nullptr;
    }

    inline void Clear() noexcept
    {
        // Don't delete the buffers here - MpqManager owns the memory
        // (it tracks all allocations and frees them in its destructor).
        Cache.clear();
    }
};
