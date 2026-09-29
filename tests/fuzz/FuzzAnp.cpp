// Fuzz .anp archive loading (zip container, params, tile validation, Detour addTile).

#include <cstdint>

#include "Anp.hpp"
#include "Utils/Logger.hpp"

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    Logger::SetQuiet(true);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const auto result = Anp::LoadFromMemory(data, size, "fuzz.anp");

    if (result.navMesh)
    {
        // Loaded tiles must be walkable by Detour: touch every tile's header and polygons.
        const dtNavMesh* mesh = result.navMesh.get();

        for (int i = 0; i < mesh->getMaxTiles(); ++i)
        {
            const dtMeshTile* tile = mesh->getTile(i);

            if (tile && tile->header)
            {
                volatile int polys = tile->header->polyCount;
                (void)polys;
            }
        }
    }

    return 0;
}
