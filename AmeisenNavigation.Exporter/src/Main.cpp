#include "Main.hpp"

int main() noexcept
{
    MpqManager mpqManager(GAME_DIR);
    CachedFileReader mpqReader(&mpqManager);

    std::vector<std::pair<unsigned int, std::string>> maps;

    if (Dbc* mapDbc = mpqReader.GetFileContent<Dbc>("DBFilesClient\\Map.dbc"))
    {
        for (unsigned int i = 0u; i < mapDbc->GetRecordCount(); ++i)
        {
            maps.push_back(std::make_pair(mapDbc->Read<unsigned int>(i, 0u), mapDbc->ReadString(i, 1u)));
        }
    }

    std::unordered_map<unsigned int, LiquidType> liquidTypes;

    if (Dbc* liquidTypeDbc = mpqReader.GetFileContent<Dbc>("DBFilesClient\\LiquidType.dbc"))
    {
        for (unsigned int i = 0u; i < liquidTypeDbc->GetRecordCount(); ++i)
        {
            liquidTypes[liquidTypeDbc->Read<unsigned int>(i, 0u)] = static_cast<LiquidType>(liquidTypeDbc->Read<unsigned int>(i, 3u));
        }
    }

    const std::set<int> unusedMapIds
    {
        13,  169, 25,  29,
        42,  451, 573, 582,
        584, 586, 587, 588,
        589, 590, 591, 592,
        593, 594, 596, 597,
        605, 606, 610, 612,
        613, 614, 620, 621,
        622, 623, 641, 642,
        647, 672, 673, 712,
        713, 718,
    };

    dtNavMeshParams params{ 0 };
    params.tileWidth = TILESIZE;
    params.tileHeight = TILESIZE;
    params.maxPolys = 1 << DT_POLY_BITS;
    params.maxTiles = WDT_MAP_SIZE * WDT_MAP_SIZE;
    params.orig[0] = WORLDSIZE;
    params.orig[1] = std::numeric_limits<float>::min();
    params.orig[2] = WORLDSIZE;

    // #pragma omp parallel for schedule(dynamic)
    for (int mapIndex = 0; mapIndex < 1; ++mapIndex) // maps.size()
    {
        // skip building trash maps
        if (unusedMapIds.contains(mapIndex)) continue;

        START_TIMER(startTimeTile);

        const auto& [mapId, mapName] = maps[mapIndex];
        const auto mapsPath = std::format("World\\Maps\\{}\\{}", mapName, mapName);
        const auto wdtPath = std::format("{}.wdt", mapsPath);

        if (Wdt* wdt = mpqReader.GetFileContent<Wdt>(wdtPath.c_str()))
        {
            Structure mapGeometry;

#pragma omp parallel for schedule(dynamic)
            for (int i = 0; i < 16; ++i)
            {
                auto x = 32 + i % 4;// % WDT_MAP_SIZE;
                auto y = 48 + i / 4;// / WDT_MAP_SIZE;

                if (wdt->Main()->adt[y][x].exists)
                {
                    const auto adtPath = std::format("{}_{}_{}.adt", mapsPath, x, y);
                    unsigned char* adtData = nullptr;
                    unsigned int adtSize = 0;

                    std::cout << "[" << mapName << "]: ADT(" << x << ", " << y << ") " << adtPath << std::endl;

                    if (Adt* adt = mpqReader.GetFileContent<Adt>(adtPath.c_str()))
                    {
                        // terrain and liquids
                        START_TIMER(startTimeTerrainAndLiquids);

                        Structure terrain;

                        for (int a = 0; a < ADT_CELLS_PER_GRID * ADT_CELLS_PER_GRID; ++a)
                        {
                            const int cx = a % ADT_CELLS_PER_GRID;
                            const int cy = a / ADT_CELLS_PER_GRID;

                            adt->GetTerrainVertsAndTris(cx, cy, &terrain);
                            adt->GetLiquidVertsAndTris(cx, cy, &terrain);
                        }

                        STOP_TIMER(startTimeTerrainAndLiquids, "[" << mapName << "] >> Extracting Terrain and Liquids took ");

                        // wmo 
                        START_TIMER(startTimeWmo);
                        adt->GetWmoVertsAndTris(mpqReader, &terrain);
                        STOP_TIMER(startTimeWmo, "[" << mapName << "] >> Extracting WMO's took ");

                        // doodad
                        START_TIMER(startTimeDoodads);
                        adt->GetDoodadVertsAndTris(mpqReader, &terrain);
                        STOP_TIMER(startTimeDoodads, "[" << mapName << "] >> Extracting Doodads took ");

                        // clean out unused/duplicate verts and tris
                        START_TIMER(startTimeClean);
                        const auto vertCountBefore = terrain.verts.size();
                        const auto triCountBefore = terrain.tris.size();
                        terrain.Clean();

                        // clean out out of bounds verts and tris
                        rcCalcBounds(reinterpret_cast<float*>(terrain.verts.begin()._Ptr), terrain.verts.size(), terrain.bbMin, terrain.bbMax);

                        terrain.bbMax[0] = (32 - x) * TILESIZE;
                        terrain.bbMax[2] = (32 - y) * TILESIZE;
                        terrain.bbMin[0] = terrain.bbMax[0] - TILESIZE;
                        terrain.bbMin[2] = terrain.bbMax[2] - TILESIZE;

                        terrain.CleanOutOfBounds(terrain.bbMin, terrain.bbMax);
                        STOP_TIMER(startTimeClean, "[" << mapName << "] >> Removing duplicate " << vertCountBefore - terrain.verts.size() << " verts, " << triCountBefore - terrain.tris.size() << " tris took ");

                        // *.obj file export for debug stuff
                        if (false)
                        {
                            const auto objName = std::format("C:\\Users\\Jannis\\source\\repos\\recastnavigation\\RecastDemo\\Bin\\Meshes\\{:02}_{:02}.obj", x, y);
                            terrain.ExportDebugObjFile(objName.c_str());
                        }

#pragma omp critical
                        {
                            rcVmin(params.orig, terrain.bbMin);
                            mapGeometry.Append(terrain);
                        }
                    }
                }
            }

            Anp anp(mapIndex, params);
            AdtTileProcessor* tileProcessor = new AdtTileProcessor(&anp);

            START_TIMER(startTimeNavmesh);
            rcCalcBounds(mapGeometry.Verts(), mapGeometry.verts.size(), mapGeometry.bbMin, mapGeometry.bbMax);
            tileProcessor->Process(&mapGeometry);
            STOP_TIMER(startTimeNavmesh, "[" << mapName << "] >> Building Navmesh took ");

            anp.Save(OUTPUT_DIR);

            STOP_TIMER(startTimeTile, "[" << mapName << "] >> Parsing map WDT(" << wdtPath << ") took ");
        }
    }

    return 0;
}
