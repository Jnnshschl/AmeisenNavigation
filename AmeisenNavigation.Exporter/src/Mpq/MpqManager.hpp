#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "../../../AmeisenNavigation/src/Utils/Logger.hpp"

// Link StormLib explicitly (build system) instead of through StormLib.h's #pragma comment(lib).
#ifndef __STORMLIB_NO_STATIC_LINK__
#define __STORMLIB_NO_STATIC_LINK__
#endif
#include <StormLib.h>

#include "FileSort.hpp"

/// Owned file buffer read from an MPQ archive.
struct MpqFile
{
    std::unique_ptr<unsigned char[]> data;
    unsigned int size = 0;

    explicit operator bool() const noexcept { return data && size > 0; }
};

/// Opens all MPQ archives of a WoW Data folder and reads files by priority (patches override base archives).
/// Not thread-safe (StormLib archive handles aren't), CachedFileReader serializes access.
class MpqManager
{
    std::vector<HANDLE> Mpqs;

public:
    explicit MpqManager(const std::filesystem::path& dataDir) noexcept
    {
        std::vector<std::filesystem::path> archives;

        try
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     dataDir, std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file())
                {
                    continue;
                }

                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (ext == ".mpq")
                {
                    archives.push_back(entry.path());
                }
            }
        }
        catch (const std::exception& e)
        {
            LogE("Failed to scan \"", dataDir.string(), "\" for MPQ archives: ", e.what());
        }

        SortMpqsByPriority(archives, dataDir);

        for (const auto& archive : archives)
        {
            HANDLE mpq = nullptr;

            // TCHAR is wchar_t for Unicode builds of StormLib (Windows), char otherwise.
#if defined(_WIN32) && defined(_UNICODE)
            const std::wstring archiveName = archive.wstring();
#else
            const std::string archiveName = archive.string();
#endif

            if (SFileOpenArchive(archiveName.c_str(), 0, MPQ_OPEN_READ_ONLY, &mpq))
            {
                Mpqs.push_back(mpq);
                LogD("Opened MPQ: ", archive.string());
            }
            else
            {
                LogW("Failed to open MPQ: ", archive.string(), " (error ", SErrGetLastError(), ")");
            }
        }

        LogS("Loaded ", Mpqs.size(), " MPQ archives");
    }

    ~MpqManager() noexcept
    {
        for (HANDLE mpq : Mpqs)
        {
            SFileCloseArchive(mpq);
        }
    }

    MpqManager(const MpqManager&) = delete;
    MpqManager& operator=(const MpqManager&) = delete;

    size_t GetArchiveCount() const noexcept { return Mpqs.size(); }

    /// Read a file from the highest priority archive that contains it.
    /// Uses the archives' hash tables (O(1) per archive) instead of wildcard searches.
    MpqFile ReadFile(const char* name) const noexcept
    {
        for (HANDLE mpq : Mpqs)
        {
            HANDLE file = nullptr;

            if (!SFileOpenFileEx(mpq, name, SFILE_OPEN_FROM_MPQ, &file))
            {
                continue;
            }

            MpqFile result;
            const DWORD size = SFileGetFileSize(file, nullptr);

            if (size != SFILE_INVALID_SIZE && size > 0)
            {
                result.data.reset(new (std::nothrow) unsigned char[size]);
                DWORD read = 0;

                if (result.data && SFileReadFile(file, result.data.get(), size, &read, nullptr) && read == size)
                {
                    result.size = size;
                }
                else
                {
                    result.data.reset();
                }
            }

            SFileCloseFile(file);

            if (result)
            {
                return result;
            }
        }

        return {};
    }
};
