#pragma once

#include <format>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include "../INavSource.hpp"

#include "../../AmeisenNavigation.Pack/src/Anp.hpp"

class AnpNavSource : public INavSource
{
    const std::filesystem::path MmapFolder;
    std::unordered_map<size_t, std::unique_ptr<Anp>> NavMeshMap;
    mutable std::shared_mutex MapMutex;

public:
    AnpNavSource(const char* mmapFolder) : MmapFolder(mmapFolder), NavMeshMap{} {}

    ~AnpNavSource() = default;

    virtual dtNavMesh* Get(size_t mapId) noexcept override
    {
        try
        {
            // Fast path: shared lock for cache hits (concurrent reads)
            {
                std::shared_lock readLock(MapMutex);
                auto it = NavMeshMap.find(mapId);
                if (it != NavMeshMap.end() && it->second)
                    return it->second->GetNavmesh();
            }

            // Slow path: exclusive lock for loading
            std::unique_lock writeLock(MapMutex);

            // Double-check after acquiring exclusive lock
            auto it = NavMeshMap.find(mapId);
            if (it != NavMeshMap.end() && it->second)
                return it->second->GetNavmesh();

            // Check for nullptr sentinel (already tried and failed)
            if (it != NavMeshMap.end())
                return nullptr;

            std::filesystem::path anpPath = MmapFolder;
            anpPath.append(std::format("{:03}.anp", mapId));

            if (std::filesystem::exists(anpPath))
            {
                NavMeshMap[mapId] = std::make_unique<Anp>(anpPath.string().c_str());
                auto& anp = NavMeshMap[mapId];
                return anp ? anp->GetNavmesh() : nullptr;
            }

            // Insert nullptr sentinel so we don't retry missing files
            NavMeshMap[mapId] = nullptr;
            return nullptr;
        }
        catch (...) { return nullptr; }
    }
};
