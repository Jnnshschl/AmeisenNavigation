#pragma once

#include <format>
#include <mutex>

#include "../INavSource.hpp"

#include "../../../AmeisenNavigation.Pack/src/Anp.hpp"

class AnpNavSource : public INavSource
{
    const std::filesystem::path MmapFolder;
    std::unordered_map<size_t, std::pair<std::mutex, Anp*>> NavMeshMap;

public:
    AnpNavSource(const char* mmapFolder)
        : MmapFolder(mmapFolder),
        NavMeshMap{}
    {}

    ~AnpNavSource() noexcept
    {
        for (const auto& [mapId, p] : NavMeshMap)
        {
            if (p.second) delete p.second;
        }
    }

    virtual dtNavMesh* Get(size_t mapId) noexcept override
    {
        if (!NavMeshMap.contains(mapId))
        {
            std::filesystem::path anpPath = MmapFolder;
            anpPath.append(std::format("{:03}.anp", mapId));

            if (std::filesystem::exists(anpPath))
            {
                NavMeshMap[mapId].second = new Anp(anpPath.string().c_str());
            }
        }

        return NavMeshMap[mapId].second ? NavMeshMap[mapId].second->GetNavmesh() : nullptr;
    }
};
