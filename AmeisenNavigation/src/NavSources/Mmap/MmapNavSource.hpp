#pragma once

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "../../Utils/Logger.hpp"
#include "../INavSource.hpp"
#include "MmapFormat.hpp"
#include "MmapTileHeader.hpp"

/// Loads TrinityCore/SkyFire style MMAPs (one .mmap with dtNavMeshParams + one .mmtile per tile).
class MmapNavSource : public INavSource
{
public:
    struct Patterns
    {
        std::string mmap;   // std::format pattern, arg 0 = mapId
        std::string mmtile; // std::format pattern, args: mapId, x, y
    };

private:
    static constexpr int TILE_GRID_SIZE = 64;

    /// Tile data allocated with dtAlloc, ownership moves into the navmesh (DT_TILE_FREE_DATA).
    struct TileBlob
    {
        unsigned char* data = nullptr;
        int size = 0;
    };

    std::filesystem::path MmapFolder;
    MmapFormat Format;
    Patterns FilePatterns;
    NavMeshCache Cache;

public:
    explicit MmapNavSource(const std::filesystem::path& mmapFolder, MmapFormat format = MmapFormat::UNKNOWN,
                           const Patterns& customPatterns = {})
        : MmapFolder(mmapFolder),
          Format(format == MmapFormat::UNKNOWN ? DetectFormat(mmapFolder) : format)
    {
        if (Format == MmapFormat::CUSTOM)
        {
            FilePatterns = customPatterns;
        }
        else if (const auto* patterns = GetPatterns(Format))
        {
            FilePatterns = *patterns;
        }

        if (Format == MmapFormat::UNKNOWN)
        {
            LogW("Could not detect the MMAP format in \"", mmapFolder.string(), "\", no maps will be available");
        }
    }

    MmapFormat GetFormat() const noexcept { return Format; }

    static const Patterns* GetPatterns(MmapFormat format) noexcept
    {
        static const Patterns tc335a{"{:03}.mmap", "{:03}{:02}{:02}.mmtile"};
        static const Patterns sf548{"{:04}.mmap", "{:04}_{:02}_{:02}.mmtile"};

        switch (format)
        {
            case MmapFormat::TC335A: return &tc335a;
            case MmapFormat::SF548: return &sf548;
            default: return nullptr;
        }
    }

    /// Detect the naming scheme by looking at the .mmtile files in the folder.
    static MmapFormat DetectFormat(const std::filesystem::path& folder) noexcept
    {
        try
        {
            static const std::regex tc335a(R"(^\d{7}\.mmtile$)", std::regex::icase);
            static const std::regex sf548(R"(^\d{4}_\d{2}_\d{2}\.mmtile$)", std::regex::icase);

            std::error_code ec;

            for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
            {
                const auto name = entry.path().filename().string();

                if (std::regex_match(name, tc335a))
                {
                    return MmapFormat::TC335A;
                }

                if (std::regex_match(name, sf548))
                {
                    return MmapFormat::SF548;
                }
            }
        }
        catch (...)
        {
        }

        return MmapFormat::UNKNOWN;
    }

    dtNavMesh* Get(int mapId) noexcept override
    {
        return Cache.GetOrLoad(mapId, [this](int id) { return Load(id); });
    }

private:
    NavMeshPtr Load(int mapId) const
    {
        if (FilePatterns.mmap.empty() || FilePatterns.mmtile.empty())
        {
            return nullptr;
        }

        const auto start = std::chrono::steady_clock::now();
        const auto mmapFile = MmapFolder / std::vformat(FilePatterns.mmap, std::make_format_args(mapId));

        dtNavMeshParams params{};

        {
            std::ifstream stream(mmapFile, std::ios::binary);

            if (!stream.is_open())
            {
                LogW("No navmesh for map ", mapId, " (missing ", mmapFile.string(), ")");
                return nullptr;
            }

            if (!stream.read(reinterpret_cast<char*>(&params), sizeof(params)))
            {
                LogE("Truncated mmap file: ", mmapFile.string());
                return nullptr;
            }
        }

        NavMeshPtr navMesh(dtAllocNavMesh());

        if (!navMesh || dtStatusFailed(navMesh->init(&params)))
        {
            LogE("Failed to init navmesh for map ", mapId, " from ", mmapFile.string());
            return nullptr;
        }

        std::vector<TileBlob> tiles(TILE_GRID_SIZE * TILE_GRID_SIZE);
        const std::string tilePattern = FilePatterns.mmtile;

        // File I/O is the bottleneck, read tiles in parallel and add them sequentially afterwards.
#pragma omp parallel for schedule(dynamic, 16)
        for (int i = 0; i < TILE_GRID_SIZE * TILE_GRID_SIZE; ++i)
        {
            const int x = i / TILE_GRID_SIZE;
            const int y = i % TILE_GRID_SIZE;

            try
            {
                const auto tileFile =
                    MmapFolder / std::vformat(tilePattern, std::make_format_args(mapId, x, y));
                tiles[static_cast<size_t>(i)] = ReadTile(tileFile);
            }
            catch (...)
            {
            }
        }

        int loaded = 0;
        int rejected = 0;

        for (auto& [data, size] : tiles)
        {
            if (!data)
            {
                continue;
            }

            if (dtStatusSucceed(navMesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr)))
            {
                loaded++;
            }
            else
            {
                rejected++;
                dtFree(data);
            }
        }

        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        LogI("Loaded map ", mapId, ": ", loaded, " tiles", rejected ? std::format(" ({} rejected)", rejected) : "",
             " in ", ms, "ms");

        return navMesh;
    }

    static TileBlob ReadTile(const std::filesystem::path& tileFile) noexcept
    {
        std::ifstream stream(tileFile, std::ios::binary | std::ios::ate);

        if (!stream.is_open())
        {
            return {};
        }

        const auto fileSize = static_cast<std::streamoff>(stream.tellg());
        stream.seekg(0);

        MmapTileHeader header{};

        if (fileSize < static_cast<std::streamoff>(sizeof(header))
            || !stream.read(reinterpret_cast<char*>(&header), sizeof(header)))
        {
            return {};
        }

        if (header.mmapMagic != MMAP_MAGIC || header.dtVersion != static_cast<uint32_t>(DT_NAVMESH_VERSION)
            || header.mmapVersion < MMAP_VERSION || header.size == 0
            || static_cast<std::streamoff>(header.size) > fileSize - static_cast<std::streamoff>(sizeof(header)))
        {
            LogW("Skipping invalid mmtile: ", tileFile.string());
            return {};
        }

        auto* data = static_cast<unsigned char*>(dtAlloc(header.size, DT_ALLOC_PERM));

        if (!data)
        {
            return {};
        }

        if (!stream.read(reinterpret_cast<char*>(data), header.size) || !ValidateTileData(data, header.size))
        {
            LogW("Skipping corrupt mmtile: ", tileFile.string());
            dtFree(data);
            return {};
        }

        return {data, static_cast<int>(header.size)};
    }
};
