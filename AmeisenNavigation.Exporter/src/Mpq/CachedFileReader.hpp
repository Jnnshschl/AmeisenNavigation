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

/// The entry reinterpreted as T*, nullptr if it is empty or T has an IsValid() check that fails. Callers only
/// ever see files that passed their format's basic checks.
template <typename T>
T* AsValid(CachedFileEntry& entry) noexcept
{
    if (!entry.Data || entry.Size == 0)
    {
        return nullptr;
    }

    T* file = reinterpret_cast<T*>(&entry);

    if constexpr (requires(const T& t) { t.IsValid(); })
    {
        if (!file->IsValid())
        {
            return nullptr;
        }
    }

    return file;
}

/// A file read without caching (e.g. ADTs, each is only needed once). Keeps the buffer alive.
struct UncachedFile
{
    MpqFile file;
    CachedFileEntry entry{nullptr, 0};

    template <typename T>
    T* As() noexcept
    {
        return AsValid<T>(entry);
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
    /// or nullptr if the file doesn't exist or fails T::IsValid(). Pointers stay valid until Clear().
    template <typename T>
    T* GetFileContent(const char* filename) noexcept
    {
        try
        {
            const auto hash = XXH3_64bits(filename, std::strlen(filename));

            if (Slot* cached = Find(hash))
            {
                return AsType<T>(*cached);
            }

            // Read outside the cache lock so cache hits of other threads aren't blocked by MPQ I/O. Threads missing
            // the same file (neighbouring ADTs share most models) queue on the MPQ lock: check again once it's ours,
            // so the file is read and decompressed only once.
            const std::lock_guard mpqLock(MpqMutex);

            if (Slot* cached = Find(hash))
            {
                return AsType<T>(*cached);
            }

            auto slot = std::make_unique<Slot>();
            MpqFile file = Mpq->ReadFile(filename);

            if (file)
            {
                slot->entry = {file.data.get(), file.size};
                slot->buffer = std::move(file.data);
            }
            else
            {
                LogD("File not found in MPQs: ", filename);
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

    /// Put a file into the cache as if it had been read from the MPQs (tests, fuzzing). A file that is already
    /// cached is kept (pointers to it stay valid until Clear()), returns false then.
    bool Insert(const char* filename, const unsigned char* data, size_t size) noexcept
    {
        try
        {
            auto slot = std::make_unique<Slot>();

            if (size > 0)
            {
                slot->buffer = std::make_unique<unsigned char[]>(size);
                std::memcpy(slot->buffer.get(), data, size);
                slot->entry = {slot->buffer.get(), static_cast<unsigned int>(size)};
            }

            const auto hash = XXH3_64bits(filename, std::strlen(filename));
            std::unique_lock writeLock(CacheMutex);
            return Cache.try_emplace(hash, std::move(slot)).second;
        }
        catch (...)
        {
            return false;
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
    Slot* Find(XXH64_hash_t hash)
    {
        std::shared_lock readLock(CacheMutex);
        const auto it = Cache.find(hash);
        return it != Cache.end() ? it->second.get() : nullptr;
    }

    template <typename T>
    static T* AsType(Slot& slot) noexcept
    {
        return AsValid<T>(slot.entry);
    }

    MpqFile ReadFromMpq(const char* filename) noexcept
    {
        std::lock_guard lock(MpqMutex);
        return Mpq->ReadFile(filename);
    }
};
