#pragma once

#include <chrono>
#include <filesystem>

#include "../../../../AmeisenNavigation.Pack/src/Anp.hpp"
#include "../../Utils/Logger.hpp"
#include "../INavSource.hpp"

/// Loads ANP navmeshes ("{mapId:03}.anp") exported by AmeisenNavigation.Exporter.
class AnpNavSource : public INavSource
{
    const std::filesystem::path MeshFolder;
    NavMeshCache Cache;

public:
    explicit AnpNavSource(const std::filesystem::path& meshFolder) : MeshFolder(meshFolder) {}

    dtNavMesh* Get(int mapId) noexcept override
    {
        return Cache.GetOrLoad(mapId, [this](int id) -> NavMeshPtr {
            const auto anpPath = MeshFolder / Anp::FileName(id);

            if (!std::filesystem::exists(anpPath))
            {
                LogW("No navmesh for map ", id, " (missing ", anpPath.string(), ")");
                return nullptr;
            }

            const auto start = std::chrono::steady_clock::now();
            auto result = Anp::Load(anpPath);

            if (!result.navMesh)
            {
                return nullptr;
            }

            if (result.mapId != id)
            {
                LogW(anpPath.string(), " contains mapId ", result.mapId, ", expected ", id);
            }

            const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            LogI("Loaded map ", id, ": ", result.tilesLoaded, " tiles", result.tilesRejected ? " (" : "",
                 result.tilesRejected ? std::to_string(result.tilesRejected) + " rejected)" : std::string(), " in ",
                 ms, "ms");

            return std::move(result.navMesh);
        });
    }
};
