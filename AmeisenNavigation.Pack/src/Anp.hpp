#pragma once

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include "../../AmeisenNavigation/src/Utils/Logger.hpp"

#include "AnpFormat.hpp"
#include "DetourUtils.hpp"
#include "miniz/miniz.h"

/// ANP ("AmeisenNavigation Pack") navmesh container.
///
/// An .anp file is a ZIP archive named "{mapId:03}.anp" with the entries:
///   "mapId"   -> int32 map id
///   "params"  -> dtNavMeshParams
///   "XX_YY"   -> one serialized Detour tile (DT_NAVMESH_VERSION), XX/YY = tile grid coordinates
///
/// A tile's entry name has to match the cell in its dtMeshHeader, the loader rejects tiles stored under another
/// cell's name.
namespace Anp {
constexpr const char* MAP_ID_ENTRY = "mapId";
constexpr const char* PARAMS_ENTRY = "params";

inline std::string FileName(int mapId) { return std::format("{:03}.anp", mapId); }

inline std::string TileEntryName(int x, int y) { return std::format("{:02}_{:02}", x, y); }

/// Parse "XX_YY" tile entry names. Rejects everything else (mapId, params, directories, ...).
inline bool ParseTileEntryName(const char* name, int& x, int& y) noexcept
{
    if (!name || !*name)
    {
        return false;
    }

    int values[2]{};
    int part = 0;
    int digits = 0;

    for (const char* c = name; *c; ++c)
    {
        if (*c >= '0' && *c <= '9')
        {
            if (++digits > 6)
            {
                return false;
            }

            values[part] = values[part] * 10 + (*c - '0');
        }
        else if (*c == '_' && part == 0 && digits > 0)
        {
            part = 1;
            digits = 0;
        }
        else
        {
            return false;
        }
    }

    if (part != 1 || digits == 0)
    {
        return false;
    }

    x = values[0];
    y = values[1];
    return true;
}

/// Thread-safe .anp writer used by the exporter. Tiles are deflated outside the archive lock so many
/// tile builder threads can add tiles concurrently, only the (cheap) archive append is serialized.
class AnpWriter
{
    int MapId;
    dtNavMeshParams Params;
    mz_zip_archive Zip;
    std::mutex Mutex;
    bool Valid;
    bool Finalized;
    std::atomic<int> TileCount;
    std::atomic<size_t> StoredBytes;

public:
    AnpWriter(int mapId, const dtNavMeshParams& params) noexcept
        : MapId(mapId),
          Params(params),
          Zip{},
          Mutex(),
          Valid(false),
          Finalized(false),
          TileCount(0),
          StoredBytes(0)
    {
        mz_zip_zero_struct(&Zip);

        Valid = mz_zip_writer_init_heap(&Zip, 0, 64 * 1024)
                && mz_zip_writer_add_mem(&Zip, MAP_ID_ENTRY, &MapId, sizeof(MapId), MZ_NO_COMPRESSION)
                && mz_zip_writer_add_mem(&Zip, PARAMS_ENTRY, &Params, sizeof(Params), MZ_NO_COMPRESSION);

        if (!Valid)
        {
            LogE("Failed to initialize .anp archive for mapId=", MapId);
        }
    }

    ~AnpWriter() noexcept { mz_zip_writer_end(&Zip); }

    AnpWriter(const AnpWriter&) = delete;
    AnpWriter& operator=(const AnpWriter&) = delete;

    int GetMapId() const noexcept { return MapId; }
    const dtNavMeshParams& GetParams() const noexcept { return Params; }
    int GetTileCount() const noexcept { return TileCount.load(); }
    size_t GetStoredBytes() const noexcept { return StoredBytes.load(); }

    /// Add a serialized Detour tile, stored under the cell in its header. Does not take ownership of navData.
    /// Thread-safe.
    bool AddTile(const unsigned char* navData, int navDataSize) noexcept
    {
        if (!Valid || !navData || navDataSize <= 0)
        {
            return false;
        }

        if (!ValidateTileData(navData, static_cast<size_t>(navDataSize)))
        {
            LogE("Refusing to store an invalid tile for mapId=", MapId);
            return false;
        }

        dtMeshHeader header;
        std::memcpy(&header, navData, sizeof(header));
        const auto name = TileEntryName(header.x, header.y);
        const auto crc = static_cast<mz_uint32>(mz_crc32(MZ_CRC32_INIT, navData, static_cast<size_t>(navDataSize)));

        // Raw deflate (negative window bits = no zlib header), as expected inside ZIP archives.
        const mz_uint compFlags =
            tdefl_create_comp_flags_from_zip_params(MZ_DEFAULT_LEVEL, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY);

        size_t compressedSize = 0;
        void* compressed =
            tdefl_compress_mem_to_heap(navData, static_cast<size_t>(navDataSize), &compressedSize, compFlags);

        bool ok = false;
        size_t stored = 0;

        {
            const std::lock_guard lock(Mutex);

            if (Finalized)
            {
                mz_free(compressed);
                return false;
            }

            if (compressed && compressedSize > 0 && compressedSize < static_cast<size_t>(navDataSize))
            {
                ok = mz_zip_writer_add_mem_ex(&Zip, name.c_str(), compressed, compressedSize, nullptr, 0,
                                              static_cast<mz_uint>(MZ_DEFAULT_LEVEL) | static_cast<mz_uint>(MZ_ZIP_FLAG_COMPRESSED_DATA),
                                              static_cast<mz_uint64>(navDataSize), crc);
                stored = compressedSize;
            }
            else
            {
                ok = mz_zip_writer_add_mem(&Zip, name.c_str(), navData, static_cast<size_t>(navDataSize),
                                           MZ_NO_COMPRESSION);
                stored = static_cast<size_t>(navDataSize);
            }
        }

        mz_free(compressed);

        if (!ok)
        {
            LogE("Failed to add tile ", name, " to .anp archive for mapId=", MapId);
            return false;
        }

        TileCount.fetch_add(1, std::memory_order_relaxed);
        StoredBytes.fetch_add(stored, std::memory_order_relaxed);
        return true;
    }

    /// Finalize the archive and write it to "{outputDir}/{mapId:03}.anp" (atomically via a temp file).
    /// The writer can't be used afterwards.
    bool Save(const std::filesystem::path& outputDir) noexcept
    {
        const std::lock_guard lock(Mutex);

        if (!Valid || Finalized)
        {
            return false;
        }

        Finalized = true;

        void* zipData = nullptr;
        size_t zipSize = 0;

        if (!mz_zip_writer_finalize_heap_archive(&Zip, &zipData, &zipSize))
        {
            LogE("Failed to finalize .anp archive for mapId=", MapId);
            return false;
        }

        // Finalizing hands the heap buffer over to us.
        struct BufferGuard
        {
            void* data;
            ~BufferGuard() { mz_free(data); }
        } bufferGuard{zipData};

        try
        {
            std::filesystem::create_directories(outputDir);

            const auto finalPath = outputDir / FileName(MapId);
            auto tempPath = finalPath;
            tempPath += ".tmp";

            {
                std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);

                if (!file.is_open())
                {
                    LogE("Failed to open output file: ", tempPath.string());
                    return false;
                }

                file.write(static_cast<const char*>(zipData), static_cast<std::streamsize>(zipSize));

                if (!file.good())
                {
                    LogE("Failed to write output file: ", tempPath.string());
                    return false;
                }
            }

            std::error_code ec;
            std::filesystem::rename(tempPath, finalPath, ec);

            if (ec)
            {
                // Windows can't rename over an existing file in some setups, retry after removing it.
                std::filesystem::remove(finalPath, ec);
                std::filesystem::rename(tempPath, finalPath, ec);
            }

            if (ec)
            {
                LogE("Failed to move ", tempPath.string(), " to ", finalPath.string(), ": ", ec.message());
                return false;
            }

            return true;
        }
        catch (const std::exception& e)
        {
            LogE("Failed to save .anp for mapId=", MapId, ": ", e.what());
            return false;
        }
    }
};

/// Upper bound for the decompressed tile data of one map (the biggest continents are well below 1 GB).
constexpr uint64_t MAX_ANP_TILE_BYTES = 8ull * 1024 * 1024 * 1024;

struct LoadResult
{
    NavMeshPtr navMesh;
    int mapId = -1;
    int tilesLoaded = 0;
    int tilesRejected = 0;
};

/// True if the (validated) tile's header says it belongs to cell (x, y).
inline bool IsInCell(const unsigned char* tileData, int x, int y) noexcept
{
    dtMeshHeader header;
    std::memcpy(&header, tileData, sizeof(header));
    return header.x == x && header.y == y;
}

/// Load an .anp archive from memory into a ready-to-query dtNavMesh. `name` is only used for log messages.
/// Every tile is validated (ValidateTileData) before Detour sees it, broken tiles are counted as rejected.
/// Tiles are decompressed and validated in parallel (OpenMP), then added sequentially (addTile isn't thread-safe).
inline LoadResult LoadFromMemory(const unsigned char* archive, size_t archiveSize, const std::string& name) noexcept
{
    LoadResult result;

    try
    {
        if (!archive || archiveSize == 0)
        {
            LogE("Empty .anp file: ", name);
            return result;
        }

        mz_zip_archive zip;
        mz_zip_zero_struct(&zip);

        if (!mz_zip_reader_init_mem(&zip, archive, archiveSize, 0))
        {
            LogE("Invalid .anp archive: ", name);
            return result;
        }

        struct ZipGuard
        {
            mz_zip_archive* zip;
            ~ZipGuard() { mz_zip_reader_end(zip); }
        } zipGuard{&zip};

        int mapId = -1;
        dtNavMeshParams params{};

        const int mapIdIndex = mz_zip_reader_locate_file(&zip, MAP_ID_ENTRY, nullptr, 0);
        const int paramsIndex = mz_zip_reader_locate_file(&zip, PARAMS_ENTRY, nullptr, 0);

        if (mapIdIndex < 0 || paramsIndex < 0
            || !mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(mapIdIndex), &mapId, sizeof(mapId), 0)
            || !mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(paramsIndex), &params, sizeof(params), 0))
        {
            LogE("Failed to read mapId/params from .anp file: ", name);
            return result;
        }

        NavMeshPtr navMesh(ValidateNavMeshParams(params) ? dtAllocNavMesh() : nullptr);

        if (!navMesh || dtStatusFailed(navMesh->init(&params)))
        {
            LogE("Invalid navmesh parameters in .anp file: ", name);
            return result;
        }

        // Archive entry, the grid cell its name ("XX_YY") says the tile belongs to and its uncompressed size.
        struct TileEntry
        {
            mz_uint index;
            int x;
            int y;
            size_t size;
        };

        std::vector<TileEntry> tileEntries;
        const mz_uint fileCount = mz_zip_reader_get_num_files(&zip);
        tileEntries.reserve(fileCount);
        uint64_t declaredBytes = 0;

        for (mz_uint i = 0; i < fileCount; ++i)
        {
            char entryName[64]{};
            int x = 0;
            int y = 0;

            mz_zip_archive_file_stat stat{};

            // The declared size decides the allocation, don't trust it beyond what a tile can be.
            if (mz_zip_reader_get_filename(&zip, i, entryName, sizeof(entryName)) > 0
                && ParseTileEntryName(entryName, x, y) && mz_zip_reader_file_stat(&zip, i, &stat)
                && stat.m_uncomp_size <= MAX_TILE_DATA_SIZE
                && tileEntries.size() < static_cast<size_t>(params.maxTiles))
            {
                tileEntries.push_back({i, x, y, static_cast<size_t>(stat.m_uncomp_size)});
                declaredBytes += stat.m_uncomp_size;
            }
            else if (ParseTileEntryName(entryName, x, y))
            {
                result.tilesRejected++;
            }
        }

        // All tiles are decompressed before they are added, a bogus archive mustn't claim more than any map has.
        if (declaredBytes > MAX_ANP_TILE_BYTES)
        {
            LogE("Rejecting .anp file ", name, ": tiles claim ", declaredBytes / (1024 * 1024), " MB");
            return result;
        }

        static_assert(MAX_TILE_DATA_SIZE <= static_cast<size_t>(INT32_MAX), "addTile takes the tile size as int");

        // Decompress straight into Detour's allocation and validate in parallel, only addTile (not thread-safe)
        // runs sequentially. Reading from an in-memory archive is thread-safe in miniz (no shared read state).
        std::vector<unsigned char*> tiles(tileEntries.size(), nullptr);

#pragma omp parallel for schedule(dynamic, 4)
        for (int i = 0; i < static_cast<int>(tileEntries.size()); ++i)
        {
            const TileEntry& entry = tileEntries[static_cast<size_t>(i)];

            if (entry.size == 0)
            {
                continue;
            }

            auto* data = static_cast<unsigned char*>(dtAlloc(entry.size, DT_ALLOC_PERM));

            // A tile has to sit in the cell its entry name says, otherwise it would occupy another tile's cell
            // (that one then fails with DT_ALREADY_OCCUPIED) or be linked to the wrong neighbours.
            if (data && mz_zip_reader_extract_to_mem(&zip, entry.index, data, entry.size, 0)
                && ValidateTileData(data, entry.size) && IsInCell(data, entry.x, entry.y))
            {
                tiles[static_cast<size_t>(i)] = data;
            }
            else if (data)
            {
                dtFree(data);
            }
        }

        for (size_t i = 0; i < tiles.size(); ++i)
        {
            if (tiles[i]
                && dtStatusSucceed(navMesh->addTile(tiles[i], static_cast<int>(tileEntries[i].size),
                                                    DT_TILE_FREE_DATA, 0, nullptr)))
            {
                result.tilesLoaded++;
                continue;
            }

            result.tilesRejected++;

            if (tiles[i])
            {
                dtFree(tiles[i]);
            }
        }

        result.mapId = mapId;
        result.navMesh = std::move(navMesh);
        return result;
    }
    catch (const std::exception& e)
    {
        LogE("Failed to load .anp file ", name, ": ", e.what());
        result.navMesh.reset();
        return result;
    }
    catch (...)
    {
        result.navMesh.reset();
        return result;
    }
}

/// Load a complete .anp file into a ready-to-query dtNavMesh.
inline LoadResult Load(const std::filesystem::path& anpFilePath) noexcept
{
    try
    {
        std::ifstream file(anpFilePath, std::ios::binary | std::ios::ate);

        if (!file.is_open())
        {
            LogE("Failed to open .anp file: ", anpFilePath.string());
            return {};
        }

        const auto size = static_cast<std::streamoff>(file.tellg());

        if (size <= 0)
        {
            LogE("Empty .anp file: ", anpFilePath.string());
            return {};
        }

        // No zero fill, every byte is read.
        const auto fileData = std::make_unique_for_overwrite<unsigned char[]>(static_cast<size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(fileData.get()), size);

        if (!file.good())
        {
            LogE("Failed to read .anp file: ", anpFilePath.string());
            return {};
        }

        return LoadFromMemory(fileData.get(), static_cast<size_t>(size), anpFilePath.string());
    }
    catch (const std::exception& e)
    {
        LogE("Failed to load .anp file ", anpFilePath.string(), ": ", e.what());
        return {};
    }
}
} // namespace Anp
