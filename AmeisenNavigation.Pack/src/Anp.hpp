#pragma once

#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>

#include "../../AmeisenNavigation/src/Utils/Logger.hpp"

#include "../../../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../../../recastnavigation/Detour/Include/DetourCommon.h"
#include "miniz/miniz.h"

/// WoW's tile grid dimension (64x64 tiles per map).
constexpr int WOW_TILE_GRID_SIZE = 64;

class Anp
{
    int MapId;
    dtNavMesh* Navmesh;
    dtNavMeshParams NavmeshParams;
    mz_zip_archive Zip;
    std::mutex Mutex;

public:
    /// Create a new pack for the given map with the specified navmesh params.
    Anp(int mapId, const dtNavMeshParams& params) noexcept
        : MapId(mapId), Navmesh(dtAllocNavMesh()), NavmeshParams(params), Zip{0}, Mutex()
    {
        Navmesh->init(&params);
        mz_zip_writer_init_heap(&Zip, 0, 0);
        mz_zip_writer_add_mem(&Zip, "mapId", &mapId, sizeof(int), MZ_DEFAULT_COMPRESSION);
        mz_zip_writer_add_mem(&Zip, "params", &params, sizeof(dtNavMeshParams), MZ_DEFAULT_COMPRESSION);
    }

    /// Load an existing .anp file from disk.
    explicit Anp(const char* anpFilePath) noexcept
        : MapId(-1), Navmesh{dtAllocNavMesh()}, NavmeshParams{0}, Zip{0}, Mutex()
    {
        if (!mz_zip_reader_init_file(&Zip, anpFilePath, 0))
        {
            LogE("Failed to open .anp file: ", anpFilePath);
            return;
        }

        bool ok =
            mz_zip_reader_extract_to_mem(&Zip, mz_zip_reader_locate_file(&Zip, "mapId", 0, 0), &MapId, sizeof(int), 0) &&
            mz_zip_reader_extract_to_mem(&Zip, mz_zip_reader_locate_file(&Zip, "params", 0, 0), &NavmeshParams, sizeof(dtNavMeshParams), 0);

        if (!ok)
        {
            LogE("Failed to read mapId/params from .anp file: ", anpFilePath);
            mz_zip_reader_end(&Zip);
            return;
        }

        if (dtStatusFailed(Navmesh->init(&NavmeshParams)))
        {
            LogE("Failed to init navmesh from .anp file: ", anpFilePath);
            mz_zip_reader_end(&Zip);
            MapId = -1;
            return;
        }

        int tilesLoaded = 0;
        for (int i = 0; i < WOW_TILE_GRID_SIZE * WOW_TILE_GRID_SIZE; i++)
        {
            auto x = i % WOW_TILE_GRID_SIZE;
            auto y = i / WOW_TILE_GRID_SIZE;

            char tileName[8];
            snprintf(tileName, sizeof(tileName), "%02d_%02d", x, y);
            auto fileIndex = mz_zip_reader_locate_file(&Zip, tileName, 0, 0);

            if (fileIndex > -1)
            {
                size_t allocSize = 0;
                void* navMeshData = mz_zip_reader_extract_to_heap(&Zip, fileIndex, &allocSize, 0);
                if (navMeshData)
                {
                    // Validate tile data size against header before passing to Detour.
                    // addTile doesn't verify dataSize >= header-claimed layout, which can
                    // cause Detour to set up internal pointers past the buffer.
                    bool sizeOk = false;
                    if (allocSize >= sizeof(dtMeshHeader))
                    {
                        const auto* hdr = reinterpret_cast<const dtMeshHeader*>(navMeshData);
                        if (hdr->magic == DT_NAVMESH_MAGIC && hdr->version == DT_NAVMESH_VERSION)
                        {
                            const int headerSize = dtAlign4(static_cast<int>(sizeof(dtMeshHeader)));
                            const int vertsSize = dtAlign4(static_cast<int>(sizeof(float)) * 3 * hdr->vertCount);
                            const int polysSize = dtAlign4(static_cast<int>(sizeof(dtPoly)) * hdr->polyCount);
                            const int linksSize = dtAlign4(static_cast<int>(sizeof(dtLink)) * hdr->maxLinkCount);
                            const int detailMeshesSize = dtAlign4(static_cast<int>(sizeof(dtPolyDetail)) * hdr->detailMeshCount);
                            const int detailVertsSize = dtAlign4(static_cast<int>(sizeof(float)) * 3 * hdr->detailVertCount);
                            const int detailTrisSize = dtAlign4(static_cast<int>(sizeof(unsigned char)) * 4 * hdr->detailTriCount);
                            const int bvTreeSize = dtAlign4(static_cast<int>(sizeof(dtBVNode)) * hdr->bvNodeCount);
                            const int offMeshSize = dtAlign4(static_cast<int>(sizeof(dtOffMeshConnection)) * hdr->offMeshConCount);

                            const int64_t totalRequired = static_cast<int64_t>(headerSize) + vertsSize + polysSize
                                + linksSize + detailMeshesSize + detailVertsSize + detailTrisSize + bvTreeSize + offMeshSize;

                            sizeOk = (totalRequired > 0 && static_cast<int64_t>(allocSize) >= totalRequired);
                            if (!sizeOk)
                            {
                                LogE("ANP tile ", x, "_", y, " data too small: ",
                                     allocSize, " < ", totalRequired, " (header claims)");
                            }
                        }
                    }

                    if (sizeOk && dtStatusSucceed(Navmesh->addTile(reinterpret_cast<unsigned char*>(navMeshData),
                                                                    static_cast<int>(allocSize), DT_TILE_FREE_DATA, 0, 0)))
                    {
                        tilesLoaded++;
                    }
                    else
                    {
                        mz_free(navMeshData);
                    }
                }
            }
        }

        LogI("Loaded .anp: mapId=", MapId, ", tiles=", tilesLoaded);
        mz_zip_reader_end(&Zip);
    }

    ~Anp() noexcept
    {
        if (Navmesh)
            dtFreeNavMesh(Navmesh);
        mz_zip_writer_end(&Zip);
    }

    constexpr inline dtNavMesh* GetNavmesh() const noexcept { return Navmesh; }
    constexpr inline dtNavMeshParams& GetNavmeshParams() noexcept { return NavmeshParams; }
    constexpr inline int GetMapId() const noexcept { return MapId; }

    inline dtStatus AddTile(int x, int y, unsigned char* navData, int navDataSize, dtTileRef* tile) noexcept
    {
        const std::lock_guard lock(Mutex);
        char tileName[8];
        snprintf(tileName, sizeof(tileName), "%02d_%02d", x, y);
        mz_zip_writer_add_mem(&Zip, tileName, navData, navDataSize, MZ_DEFAULT_COMPRESSION);
        return Navmesh->addTile(navData, navDataSize, DT_TILE_FREE_DATA, 0, tile);
    }

    inline dtStatus FreeTile(unsigned char** navData, int* navDataSize, dtTileRef* tile) noexcept
    {
        return Navmesh->removeTile(*tile, navData, navDataSize);
    }

    inline bool Save(const char* outputDir)
    {
        const std::lock_guard lock(Mutex);
        void* zipData = nullptr;
        size_t zip_size = 0;

        if (!mz_zip_writer_finalize_heap_archive(&Zip, &zipData, &zip_size))
        {
            LogE("Failed to finalize .anp archive for mapId=", MapId);
            return false;
        }

        try
        {
            std::filesystem::path outputDirPath(outputDir);
            outputDirPath.append(std::format("{:03}.anp", MapId));

            std::ofstream zip_file(outputDirPath, std::ios::binary);
            if (!zip_file.is_open())
            {
                LogE("Failed to open output file: ", outputDirPath.string());
                mz_free(zipData);
                return false;
            }

            zip_file.write(static_cast<const char*>(zipData), zip_size);
            mz_free(zipData);
            return zip_file.good();
        }
        catch (const std::exception& e)
        {
            LogE("Failed to save .anp for mapId=", MapId, ": ", e.what());
            mz_free(zipData);
            return false;
        }
    }

private:
};
