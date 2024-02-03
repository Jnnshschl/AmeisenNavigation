#pragma once

#include <iostream>
#include <unordered_map>

#include "MpqManager.hpp"

class CachedFileReader
{
    MpqManager* Mpq;
    std::unordered_map<unsigned long long, std::pair<void*, unsigned int>> Cache;

public:
    CachedFileReader(MpqManager* mpqManager) noexcept
        : Mpq(mpqManager),
        Cache{}
    {}

    ~CachedFileReader() noexcept
    {
        for (const auto& [id, ptr] : Cache)
        {
            if (ptr.first)
            {
                delete[] ptr.first;
            }
        }
    }

    unsigned long long Fnv1a64(const char* str) const noexcept
    {
        const unsigned long long FNV_prime = 1099511628211ULL;
        const unsigned long long offset_basis = 14695981039346656037ULL;
        unsigned long long hash = offset_basis;

        while (*str != '\0')
        {
            hash ^= *str;
            hash *= FNV_prime;
            ++str;
        }

        return hash;
    }

    template<typename T>
    inline T* GetFileContent(const char* filename) noexcept
    {
        const auto hash = Fnv1a64(filename);

        if (!Cache.contains(hash))
        {
            unsigned int size = 0;

            if (unsigned char* ptr = Mpq->GetFileContent(filename, size))
            {
                std::cout << "[CachedFileReader] Loaded: " << filename << std::endl;
                Cache[hash] = std::make_pair(ptr, size);
            }
            else
            {
                std::cout << "[CachedFileReader] Failed to load: " << filename << std::endl;
                Cache[hash] = std::make_pair(nullptr, 0);
            }
        }

        return reinterpret_cast<T*>(&Cache[hash]);
    }
};
