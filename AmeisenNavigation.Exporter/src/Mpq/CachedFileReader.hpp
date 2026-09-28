#pragma once

#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "../../../AmeisenNavigation/src/Utils/Logger.hpp"

// Header-only use of xxhash: every function becomes static inline, safe to include from multiple TUs.
#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif
#include "../Utils/xxhash/xxhash.h"

#include "MpqManager.hpp"

/// Matches the binary layout of the file wrapper classes (Dbc, Wdt, Adt, Wmo, M2, ...) which all start with:
///   unsigned char* Data; unsigned int Size;
/// GetFileContent<T> returns a pointer to such an entry reinterpreted as T*.
struct CachedFileEntry
{
    unsigned char* Data;
    unsigned int Size;
};

/// A file read without caching (e.g. ADTs, each is only needed once). Keeps the buffer alive.
struct UncachedFile
{
    MpqFile file;
    CachedFileEntry entry{nullptr, 0};

    template <typename T>
    T* As() noexcept
    {
        return entry.Data && entry.Size > 0 ? reinterpret_cast<T*>(&entry) : nullptr;
    }
};

/// Thread-safe MPQ file cache. Files shared by many ADTs (WMOs, M2s, DBCs) are read once and kept until Clear().
class CachedFileReader
{
    struct Slot
    {
        std::unique_ptr<unsigned char[]> buffer;
        CachedFileEntry entry{nullptr, 0};
    };

    MpqManager* Mpq;
    std::shared_mutex CacheMutex;
    std::mutex MpqMutex; // StormLib handles are not thread-safe
    std::unordered_map<XXH64_hash_t, std::unique_ptr<Slot>> Cache;

public:
    explicit CachedFileReader(MpqManager* mpqManager) noexcept : Mpq(mpqManager) {}

    /// Returns the cached file reinterpreted as T* (T must start with `unsigned char* Data; unsigned int Size;`),
    /// or nullptr if the file doesn't exist. Pointers stay valid until Clear().
    template <typename T>
    T* GetFileContent(const char* filename) noexcept
    {
        try
        {
            const auto hash = XXH3_64bits(filename, std::strlen(filename));

            {
                std::shared_lock readLock(CacheMutex);
                const auto it = Cache.find(hash);

                if (it != Cache.end())
                {
                    return AsType<T>(*it->second);
                }
            }

            // Read outside the cache lock so cache hits of other threads aren't blocked by MPQ I/O.
            auto slot = std::make_unique<Slot>();

            {
                MpqFile file = ReadFromMpq(filename);

                if (file)
                {
                    slot->entry = {file.data.get(), file.size};
                    slot->buffer = std::move(file.data);
                }
                else
                {
                    LogD("File not found in MPQs: ", filename);
                }
            }

            std::unique_lock writeLock(CacheMutex);
            const auto [it, inserted] = Cache.try_emplace(hash, std::move(slot));
            return AsType<T>(*it->second);
        }
        catch (...)
        {
            return nullptr;
        }
    }

    /// Read a file without caching it.
    UncachedFile ReadUncached(const char* filename) noexcept
    {
        UncachedFile result;
        result.file = ReadFromMpq(filename);
        result.entry = {result.file.data.get(), result.file.size};
        return result;
    }

    /// Free all cached files. Pointers returned by GetFileContent become invalid.
    void Clear() noexcept
    {
        std::unique_lock writeLock(CacheMutex);
        Cache.clear();
    }

private:
    template <typename T>
    static T* AsType(Slot& slot) noexcept
    {
        return slot.entry.Data && slot.entry.Size > 0 ? reinterpret_cast<T*>(&slot.entry) : nullptr;
    }

    MpqFile ReadFromMpq(const char* filename) noexcept
    {
        std::lock_guard lock(MpqMutex);
        return Mpq->ReadFile(filename);
    }
};
