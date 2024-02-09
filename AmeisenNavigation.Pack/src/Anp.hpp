#pragma once

#include <filesystem>
#include <fstream>
#include <format>
#include <mutex>

#include "../../../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "miniz/miniz.h"

class Anp
{
    int MapId;
    dtNavMesh* Navmesh;
    dtNavMeshParams NavmeshParams;
    mz_zip_archive Zip;
    std::mutex Mutex;

public:
    /// <summary>
    /// CReate anew pack.
    /// </summary>
    /// <param name="mapId">MapId of the pack.</param>
    /// <param name="params">dtNavMeshParams used for this pack.</param>
    Anp(int mapId, const dtNavMeshParams& params) noexcept
        : MapId(mapId),
        Navmesh(dtAllocNavMesh()),
        NavmeshParams(params),
        Zip{ 0 },
        Mutex()
    {
        Navmesh->init(&params);
        mz_zip_writer_init_heap(&Zip, 0, 0);
        mz_zip_writer_add_mem(&Zip, "mapId", &mapId, sizeof(int), MZ_BEST_COMPRESSION);
        mz_zip_writer_add_mem(&Zip, "params", &params, sizeof(dtNavMeshParams), MZ_BEST_COMPRESSION);
    }

    /// <summary>
    /// Read a *.anp file from disk.
    /// </summary>
    /// <param name="anpFilePath">Path to the *.anp file.</param>
    explicit Anp(const char* anpFilePath) noexcept
        : MapId(-1), 
        Navmesh{ dtAllocNavMesh() },
        NavmeshParams{ 0 },
        Zip{ 0 },
        Mutex()
    {
        if (mz_zip_reader_init_file(&Zip, anpFilePath, 0) &&
            mz_zip_reader_extract_to_mem(&Zip, mz_zip_reader_locate_file(&Zip, "mapId", 0, 0), &MapId, sizeof(int), 0) &&
            mz_zip_reader_extract_to_mem(&Zip, mz_zip_reader_locate_file(&Zip, "params", 0, 0), &NavmeshParams, sizeof(dtNavMeshParams), 0))
        {
            Navmesh->init(&NavmeshParams);

            for (int i = 0; i < 64 * 64; i++)
            {
                auto x = i % 64;
                auto y = i / 64;

                auto fileIndex = mz_zip_reader_locate_file(&Zip, std::format("{:02}_{:02}", x, y).c_str(), 0, 0);

                if (fileIndex > -1)
                {
                    size_t allocSize = 0;
                    void* navMeshData = mz_zip_reader_extract_to_heap(&Zip, fileIndex, &allocSize, 0);
                    Navmesh->addTile(reinterpret_cast<unsigned char*>(navMeshData), allocSize, DT_TILE_FREE_DATA, 0, 0);
                }
            }

            mz_zip_reader_end(&Zip);
        }
    }

    ~Anp() noexcept
    {
        if (Navmesh) dtFreeNavMesh(Navmesh);
        mz_zip_writer_end(&Zip);
    }

    constexpr inline dtNavMesh* GetNavmesh() const noexcept { return Navmesh; }
    constexpr inline const dtNavMeshParams& GetNavmeshParams() const noexcept { return NavmeshParams; }

    inline dtStatus AddTile(int x, int y, unsigned char* navData, int navDataSize, dtTileRef* tile) noexcept
    {
        const std::lock_guard lock(Mutex);
        mz_zip_writer_add_mem(&Zip, std::format("{:02}_{:02}", x, y).c_str(), navData, navDataSize, MZ_BEST_COMPRESSION);
        return Navmesh->addTile(navData, navDataSize, DT_TILE_FREE_DATA, 0, tile);
    }

    inline dtStatus FreeTile(unsigned char** navData, int* navDataSize, dtTileRef* tile) noexcept
    {
        return Navmesh->removeTile(*tile, navData, navDataSize);
    }

    inline void Save(const char* outputDir) noexcept
    {
        const std::lock_guard lock(Mutex);
        void* zipData = nullptr;
        size_t zip_size;
        mz_zip_writer_finalize_heap_archive(&Zip, &zipData, &zip_size);

        std::filesystem::path outputDirPath(outputDir);
        outputDirPath.append(std::format("{:03}.anp", MapId));

        std::ofstream zip_file(outputDirPath, std::ios::binary);
        zip_file.write(static_cast<const char*>(zipData), zip_size);

        mz_free(zipData);
    }

private:

};
