#pragma once

#include <chrono>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <utility>

#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "../Utils/xxhash/xxhash.h"

#include "MpqManager.hpp"

class CachedFileReader
{
    MpqManager* Mpq;
    std::mutex CacheMutex;
    std::unordered_map<XXH64_hash_t, std::pair<void*, unsigned int>> Cache;

public:
    explicit CachedFileReader(MpqManager* mpqManager) noexcept
        : Mpq(mpqManager),
        CacheMutex(),
        Cache()
    {}

    template<typename T>
    inline T* GetFileContent(const char* filename) noexcept
    {
        const auto hash = XXH3_64bits(filename, strlen(filename));

        std::unique_lock<std::mutex> cacheLock(CacheMutex);

        if (!Cache.contains(hash))
        {
            unsigned int size = 0;

            if (unsigned char* ptr = Mpq->GetFileContent(filename, size))
            {
                Cache[hash] = std::make_pair(ptr, size);
            }
            else
            {
                std::cout << "[CachedFileReader] Failed to load: " << filename << std::endl;
                Cache[hash] = std::make_pair(nullptr, 0);
            }
        }

        // The wrapper classes (Dbc, Wdt, Adt, Wmo, M2, etc.) all have layout {unsigned char* Data; unsigned int Size;}
        // which matches std::pair<void*, unsigned int>. Returning a pointer to the pair lets the wrapper class
        // use pair.first as its Data pointer and pair.second as its Size — do NOT "fix" this to return .first.
        return Cache[hash].first && Cache[hash].second > 0 ? reinterpret_cast<T*>(&Cache[hash]) : nullptr;
    }

    inline void Clear() noexcept
    {
        // Don't delete the buffers here — MpqManager owns the memory
        // (it tracks all allocations and frees them in its destructor).
        Cache.clear();
    }
};
