#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <unordered_map>

#include "../INavSource.hpp"
#include "MmapFormat.hpp"
#include "MmapTileHeader.hpp"

class MmapNavSource : public INavSource
{
    std::filesystem::path MmapFolder;
    MmapFormat Format;
    std::unordered_map<size_t, std::pair<std::mutex, dtNavMesh*>> NavMeshMap;

    std::map<MmapFormat, std::pair<std::string_view, std::string_view>> MmapFormatPatterns
    {
        { MmapFormat::TC335A, std::make_pair("{:03}.mmap", "{:03}{:02}{:02}.mmtile") },
        { MmapFormat::SF548, std::make_pair("{:04}.mmap", "{:04}_{:02}_{:02}.mmtile") },
    };

public:
    MmapNavSource(const char* mmapFolder, MmapFormat format = MmapFormat::UNKNOWN)
        : MmapFolder(mmapFolder),
        Format(format),
        NavMeshMap{}
    {
        if (format == MmapFormat::UNKNOWN)
        {
            Format = TryDetectMmapFormat();
        }
    }

    ~MmapNavSource()
    {
        for (auto& [id, mtxNavMesh] : NavMeshMap)
        {
            const std::lock_guard lock(mtxNavMesh.first);

            if (mtxNavMesh.second)
            {
                dtFreeNavMesh(mtxNavMesh.second);
            }
        }
    }

    constexpr inline MmapFormat GetFormat() const noexcept { return Format; }

    virtual dtNavMesh* Get(size_t mapId) noexcept override
    {
        LoadMmaps(mapId);
        return NavMeshMap.at(mapId).second;
    }

private:
    bool LoadMmaps(int mapId) noexcept
    {
        const std::lock_guard<std::mutex> lock(NavMeshMap[mapId].first);

        if (NavMeshMap[mapId].second) { return true; }

        const auto& filenameFormat = MmapFormatPatterns.at(Format);

        std::filesystem::path mmapFile(MmapFolder);
        std::string filename = std::vformat(filenameFormat.first, std::make_format_args(mapId));
        mmapFile.append(filename);

        if (!std::filesystem::exists(mmapFile))
        {
            return false;
        }

        std::ifstream mmapStream;
        mmapStream.open(mmapFile, std::ifstream::binary);

        dtNavMeshParams params{};
        mmapStream.read(reinterpret_cast<char*>(&params), sizeof(dtNavMeshParams));
        mmapStream.close();

        NavMeshMap[mapId].second = dtAllocNavMesh();

        if (!NavMeshMap[mapId].second)
        {
            return false;
        }

        dtStatus initStatus = NavMeshMap[mapId].second->init(&params);

        if (dtStatusFailed(initStatus))
        {
            dtFreeNavMesh(NavMeshMap[mapId].second);
            return false;
        }

#pragma omp parallel for schedule(dynamic)
        for (int i = 0; i < 64 * 64; ++i)
        {
            const auto x = i / 64;
            const auto y = i % 64;

            std::filesystem::path mmapTileFile(MmapFolder);
            mmapTileFile.append(std::vformat(filenameFormat.second, std::make_format_args(mapId, x, y)));

            if (!std::filesystem::exists(mmapTileFile))
            {
                continue;
            }


            std::ifstream mmapTileStream;
            mmapTileStream.open(mmapTileFile, std::ifstream::binary);

            MmapTileHeader mmapTileHeader{};
            mmapTileStream.read(reinterpret_cast<char*>(&mmapTileHeader), sizeof(MmapTileHeader));

            if (mmapTileHeader.mmapMagic != MMAP_MAGIC)
            {
                continue;
            }

            if (mmapTileHeader.mmapVersion < MMAP_VERSION)
            {
            }

            void* mmapTileData = malloc(mmapTileHeader.size);
            mmapTileStream.read(static_cast<char*>(mmapTileData), mmapTileHeader.size);
            mmapTileStream.close();

            dtStatus addTileStatus = NavMeshMap[mapId].second->addTile
            (
                static_cast<unsigned char*>(mmapTileData),
                mmapTileHeader.size,
                DT_TILE_FREE_DATA,
                0,
                nullptr
            );

            if (dtStatusFailed(addTileStatus))
            {
                free(mmapTileData);
            }
        }

        return true;
    }

    /// <summary>
    /// Used to detect the mmap file format.
    /// </summary>
    /// <returns>Mmap format detected or MMAP_FORMAT::UNKNOWN</returns>
    inline MmapFormat TryDetectMmapFormat() noexcept
    {
        for (const auto& [format, pattern] : MmapFormatPatterns)
        {
            if (std::filesystem::exists(MmapFolder / std::vformat(pattern.second, std::make_format_args(0, 27, 27))))
            {
                return format;
            }
        }

        return MmapFormat::UNKNOWN;
    }
};
