#include "Main.hpp"

int main(int argc, char** argv)
{
    Logger::Initialize();

    fputs(std::format("\033[96m"
          "      ___                   _                 _   __\n"
          "     /   |  ____ ___  ___  (_)_______  ____  / | / /___ __   __\n"
          "    / /| | / __ `__ \\/ _ \\/ / ___/ _ \\/ __ \\/  |/ / __ `/ | / /\n"
          "   / ___ |/ / / / / /  __/ (__  )  __/ / / / /|  / /_/ /| |/ / \n"
          "  /_/  |_/_/ /_/ /_/\\___/_/____/\\___/_/ /_/_/ |_/\\__,_/ |___/\n"
          "                                          Exporter {}\033[0m\n\n", AMEISENNAV_VERSION).c_str(),
          stdout);

    std::string wowDir;
    std::string outputDir;
    int targetMapId = -1;
    int targetTileX = -1;
    int targetTileY = -1;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "--wow" || arg == "-w") && i + 1 < argc)
        {
            wowDir = argv[++i];
        }
        else if ((arg == "--output" || arg == "-o") && i + 1 < argc)
        {
            outputDir = argv[++i];
        }
        else if ((arg == "--map" || arg == "-m") && i + 1 < argc)
        {
            try { targetMapId = std::stoi(std::string(argv[++i])); }
            catch (...) { LogE("Invalid --map value: ", argv[i]); return 1; }
        }
        else if ((arg == "--tile" || arg == "-t") && i + 1 < argc)
        {
            std::string tileStr = argv[++i];
            size_t commaPos = tileStr.find(',');
            if (commaPos != std::string::npos)
            {
                try
                {
                    targetTileX = std::stoi(tileStr.substr(0, commaPos));
                    targetTileY = std::stoi(tileStr.substr(commaPos + 1));
                }
                catch (...) { LogE("Invalid --tile value: ", tileStr); return 1; }
            }
        }
    }

    if (wowDir.empty() || outputDir.empty())
    {
        LogE("Missing required arguments.");
        LogI("Usage: AmeisenNavigation.Exporter.exe --wow <path> --output "
             "<path> [--map <id>] [--tile x,y]");
        LogI("Example: AmeisenNavigation.Exporter.exe -w \"C:\\WoW\" -o "
             "\"C:\\Out\" -m 0 -t 32,48");
        return 1;
    }

    // Ensure wowDir ends with Data\ if it doesn't already
    std::filesystem::path wowPath(wowDir);
    if (wowPath.filename().string() != "Data")
    {
        wowPath /= "Data";
    }

    try
    {
        if (!std::filesystem::exists(wowPath))
        {
            LogE("Game data directory does not exist: \"", wowPath.string(), "\"");
            return 1;
        }

        if (!std::filesystem::exists(outputDir))
        {
            LogI("Creating output directory: \"", outputDir, "\"");
            std::filesystem::create_directories(outputDir);
        }
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        LogE("Filesystem error: ", e.what());
        return 1;
    }

    LogI(std::format("Starting export. Game Data: \"{}\", Output: \"{}\"", wowPath.string(), outputDir));

    MpqManager mpqManager(wowPath.string().c_str());
    CachedFileReader mpqReader(&mpqManager);

    std::vector<std::pair<unsigned int, std::string>> maps;

    if (Dbc* mapDbc = mpqReader.GetFileContent<Dbc>("DBFilesClient\\Map.dbc"))
    {
        if (!mapDbc->IsValid())
        {
            LogE("Map.dbc has invalid header (expected WDBC magic)");
            return 1;
        }

        for (unsigned int i = 0u; i < mapDbc->GetRecordCount(); ++i)
        {
            maps.push_back(std::make_pair(mapDbc->Read<unsigned int>(i, 0u), mapDbc->ReadString(i, 1u)));
        }
    }

    if (maps.empty())
    {
        LogE("Failed to load Map.dbc or it contains no entries - cannot proceed");
        return 1;
    }

    std::unordered_map<unsigned int, LiquidType> liquidTypes;

    if (Dbc* liquidTypeDbc = mpqReader.GetFileContent<Dbc>("DBFilesClient\\LiquidType.dbc"))
    {
        if (!liquidTypeDbc->IsValid())
        {
            LogW("LiquidType.dbc has invalid header - liquid types will be unavailable");
        }
        else
        {
            for (unsigned int i = 0u; i < liquidTypeDbc->GetRecordCount(); ++i)
            {
                liquidTypes[liquidTypeDbc->Read<unsigned int>(i, 0u)] =
                    static_cast<LiquidType>(liquidTypeDbc->Read<unsigned int>(i, 3u));
            }
        }
    }

    // Load AreaTable.dbc to build faction + city lookups
    // WotLK 3.3.5a layout: field 0=ID, field 2=ParentAreaID, field 4=Flags, field 28=FactionGroupMask
    // FactionGroupMask: 0=Contested, 2=Alliance, 4=Horde, 6=Sanctuary
    // Flags: 0x08=Capital, 0x20=SlaveCapital (secondary town)
    //
    // Sub-areas (e.g., "Trade District") often have flags/faction=0 but inherit
    // from their parent zone (e.g., "Stormwind City"). We walk up the parent chain
    // so that all sub-areas get properly marked.
    std::unordered_map<unsigned int, unsigned char> areaFactions;
    std::unordered_set<unsigned int> areaCities;

    if (Dbc* areaTableDbc = mpqReader.GetFileContent<Dbc>("DBFilesClient\\AreaTable.dbc"))
    {
        if (!areaTableDbc->IsValid())
        {
            LogW("AreaTable.dbc has invalid header - faction/city data will be unavailable");
        }
        else
        {
        constexpr unsigned int AREA_FLAG_CAPITAL = 0x08;
        constexpr unsigned int AREA_FLAG_SLAVE_CAPITAL = 0x20;

        // Pass 1: read all areas - store parent, direct faction, and flags
        struct AreaInfo
        {
            unsigned int parentId;
            unsigned int flags;
            unsigned char faction; // 0=unknown, 1=Alliance, 2=Horde
        };
        std::unordered_map<unsigned int, AreaInfo> allAreas;

        for (unsigned int i = 0u; i < areaTableDbc->GetRecordCount(); ++i)
        {
            unsigned int areaId = areaTableDbc->Read<unsigned int>(i, 0u);
            unsigned int parentId = areaTableDbc->Read<unsigned int>(i, 2u);
            unsigned int flags = areaTableDbc->Read<unsigned int>(i, 4u);
            unsigned int factionGroupMask = areaTableDbc->Read<unsigned int>(i, 28u);

            unsigned char faction = 0;
            if (factionGroupMask == 2)
                faction = 1; // Alliance
            else if (factionGroupMask == 4)
                faction = 2; // Horde

            allAreas[areaId] = {parentId, flags, faction};
        }

        // Pass 2: resolve faction by walking up parent chain for areas with faction=0
        auto resolveFaction = [&](unsigned int areaId) -> unsigned char {
            unsigned int current = areaId;
            for (int depth = 0; depth < 10; ++depth)
            {
                auto it = allAreas.find(current);
                if (it == allAreas.end())
                    return 0;
                if (it->second.faction != 0)
                    return it->second.faction;
                if (it->second.parentId == 0 || it->second.parentId == current)
                    return 0;
                current = it->second.parentId;
            }
            return 0;
        };

        // Pass 2b: resolve city status by walking up parent chain
        auto resolveCity = [&](unsigned int areaId) -> bool {
            unsigned int current = areaId;
            for (int depth = 0; depth < 10; ++depth)
            {
                auto it = allAreas.find(current);
                if (it == allAreas.end())
                    return false;
                if (it->second.flags & (AREA_FLAG_CAPITAL | AREA_FLAG_SLAVE_CAPITAL))
                    return true;
                if (it->second.parentId == 0 || it->second.parentId == current)
                    return false;
                current = it->second.parentId;
            }
            return false;
        };

        for (const auto& [areaId, info] : allAreas)
        {
            unsigned char faction = resolveFaction(areaId);
            if (faction != 0)
                areaFactions[areaId] = faction;

            if (resolveCity(areaId))
                areaCities.insert(areaId);
        }

        LogI(std::format("AreaTable.dbc: {} areas loaded, {} faction-marked, {} city-marked",
                         allAreas.size(), areaFactions.size(), areaCities.size()));
        } // else (valid DBC)
    }

    // Maps with no ADT tiles are automatically skipped via WDT existence checks.

    dtNavMeshParams params{0};
    params.tileWidth = TILESIZE;
    params.tileHeight = TILESIZE;
    params.maxPolys = 1 << DT_POLY_BITS;
    params.maxTiles = WDT_MAP_SIZE * WDT_MAP_SIZE;
    params.orig[0] = WORLDSIZE;
    params.orig[1] = std::numeric_limits<float>::lowest();
    params.orig[2] = WORLDSIZE;

    for (const auto& map : maps)
    {
        unsigned int mapId = map.first;
        const std::string& mapName = map.second;

        if (targetMapId != -1 && mapId != static_cast<unsigned int>(targetMapId))
            continue;

        START_TIMER(startTimeTile);

        const auto mapsPath = std::format("World\\Maps\\{}\\{}", mapName, mapName);
        const auto wdtPath = std::format("{}.wdt", mapsPath);

        if (Wdt* wdt = mpqReader.GetFileContent<Wdt>(wdtPath.c_str()))
        {
            Structure mapGeometry;
            WaterMap waterMap;
            RoadMap roadMap;
            FactionMap factionMap;
            CityMap cityMap;

            // Count how many ADTs exist so we can show accurate progress
            int totalAdts = 0;
            for (int i = 0; i < WDT_MAP_SIZE * WDT_MAP_SIZE; ++i)
            {
                int ax = i % WDT_MAP_SIZE;
                int ay = i / WDT_MAP_SIZE;
                if (targetTileX != -1 && targetTileY != -1 && (ax != targetTileX || ay != targetTileY))
                    continue;
                if (wdt->Main()->adt[ay][ax].exists)
                    totalAdts++;
            }

            LogI(std::format("[{}] Extracting {} ADT tiles using {} threads...", mapName, totalAdts, omp_get_max_threads()));

            std::atomic<int> adtsExtracted{0};
            auto extractStart = std::chrono::high_resolution_clock::now();
            const int adtProgressInterval = std::max(1, totalAdts / 20);

#pragma omp parallel for schedule(dynamic)
            for (int i = 0; i < WDT_MAP_SIZE * WDT_MAP_SIZE; ++i)
            {
                int x = i % WDT_MAP_SIZE;
                int y = i / WDT_MAP_SIZE;

                // Specific tile filter
                if (targetTileX != -1 && targetTileY != -1)
                {
                    if (x != targetTileX || y != targetTileY)
                        continue;
                }

                if (wdt->Main()->adt[y][x].exists)
                {
                    const auto adtPath = std::format("{}_{}_{}.adt", mapsPath, x, y);

                    if (Adt* adt = mpqReader.GetFileContent<Adt>(adtPath.c_str()))
                    {
                        Structure terrain;

                        // Step 1: Identify road textures from MTEX
                        auto roadTextureIds = FindRoadTextureIds(adt->Mtex());

                        // Step 2: Extract per-chunk data
                        for (int a = 0; a < ADT_CELLS_PER_GRID * ADT_CELLS_PER_GRID; ++a)
                        {
                            const int cx = a % ADT_CELLS_PER_GRID;
                            const int cy = a / ADT_CELLS_PER_GRID;

                            ExtractTerrain(adt, cx, cy, &terrain);
                            ExtractLiquid(adt, cx, cy, &waterMap, &terrain, liquidTypes);
                            ExtractRoadCoverage(adt, cx, cy, &roadMap, roadTextureIds);
                            ExtractCityCoverage(adt, cx, cy, &cityMap, areaCities);
                            ExtractFactionCoverage(adt, cx, cy, &factionMap, areaFactions);
                        }

                        // Step 3: Extract object geometry
                        ExtractWmoGeometry(adt, mpqReader, &terrain, liquidTypes);
                        ExtractDoodadGeometry(adt, mpqReader, &terrain);

                        terrain.Clean();

                        if (!terrain.verts.empty())
                            rcCalcBounds(reinterpret_cast<const float*>(terrain.verts.data()),
                                         static_cast<int>(terrain.verts.size()), terrain.bbMin, terrain.bbMax);

                        terrain.bbMax[0] = (32 - x) * TILESIZE;
                        terrain.bbMax[2] = (32 - y) * TILESIZE;
                        terrain.bbMin[0] = terrain.bbMax[0] - TILESIZE;
                        terrain.bbMin[2] = terrain.bbMax[2] - TILESIZE;

                        // Expand clip bounds slightly: MCNK positions are stored as floats
                        // whose values may differ from the grid formula (32-x)*TILESIZE by
                        // a small amount. Without tolerance, boundary vertices can fall just
                        // outside the computed bounds and get clipped, creating gaps between
                        // adjacent ADT tiles in the merged geometry.
                        constexpr float clipEps = 1.0f;
                        terrain.bbMin[0] -= clipEps;
                        terrain.bbMin[2] -= clipEps;
                        terrain.bbMax[0] += clipEps;
                        terrain.bbMax[2] += clipEps;

                        terrain.CleanOutOfBounds(terrain.bbMin, terrain.bbMax);

#pragma omp critical(appendGeometry)
                        {
                            mapGeometry.Append(terrain);
                        }

                        const int done = adtsExtracted.fetch_add(1, std::memory_order_relaxed) + 1;
                        if (done % adtProgressInterval == 0 || done == totalAdts)
                        {
                            auto now = std::chrono::high_resolution_clock::now();
                            double elapsed = std::chrono::duration<double>(now - extractStart).count();
                            double eta = (elapsed / done) * (totalAdts - done);
                            Logger::LogProgress(std::format("[{}] Extracting ADTs: {} / {} ({:.1f}%) - ETA: {}",
                                                            mapName, done, totalAdts, 100.0 * done / totalAdts,
                                                            Logger::FormatDuration(eta)));
                        }
                    }
                }
            }

            Logger::EndProgress();
            {
                auto now = std::chrono::high_resolution_clock::now();
                double elapsed = std::chrono::duration<double>(now - extractStart).count();
                LogS(std::format("[{}] Extracted {} ADTs in {}", mapName, adtsExtracted.load(), Logger::FormatDuration(elapsed)));
            }

            if (!mapGeometry.verts.empty())
            {
                // Compute vertex bounds BEFORE creating Anp so params.orig aligns
                // with the tile grid that Process() will generate. Process() uses
                // mapGeometry.bbMin as the grid origin - params.orig must match.
                rcCalcBounds(mapGeometry.Verts(), static_cast<int>(mapGeometry.verts.size()), mapGeometry.bbMin,
                             mapGeometry.bbMax);
                params.orig[0] = mapGeometry.bbMin[0];
                params.orig[2] = mapGeometry.bbMin[2];

                Anp anp(mapId, params);
                AdtTileProcessor tileProcessor(&anp, outputDir, mapName, false);

                START_TIMER(startTimeNavmesh);
                waterMap.BuildSpatialIndex();
                roadMap.BuildSpatialIndex();

                LogI(std::format("[{}] Geometry: {} verts, {} tris | Water: {} rects | Roads: {} | Cities: {} | Factions: {}",
                                 mapName, mapGeometry.verts.size(), mapGeometry.tris.size(),
                                 waterMap.rects.size(), roadMap.rects.size(), cityMap.rects.size(), factionMap.rects.size()));

                tileProcessor.Process(&mapGeometry, &waterMap, &roadMap, &factionMap, &cityMap);
                STOP_TIMER(startTimeNavmesh, std::format("[{}] Building navmesh took", mapName));

                LogI(std::format("[{}] Saving {}/{:03}.anp...", mapName, outputDir, mapId));
                anp.Save(outputDir.c_str());
                LogS(std::format("[{}] Saved navmesh to {}/{:03}.anp", mapName, outputDir, mapId));
            }

            STOP_TIMER(startTimeTile, std::format("Parsing Map context [{}] took", mapName));
        }
    }

    return 0;
}
