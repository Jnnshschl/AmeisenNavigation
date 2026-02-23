#include "Main.hpp"

int main(int argc, char** argv) noexcept
{
    Logger::Initialize();

    std::cout << "\033[96m"
              << "      ___                   _                 _   __" << std::endl
              << "     /   |  ____ ___  ___  (_)_______  ____  / | / /___ __   __" << std::endl
              << "    / /| | / __ `__ \\/ _ \\/ / ___/ _ \\/ __ \\/  |/ / __ `/ | / /" << std::endl
              << "   / ___ |/ / / / / /  __/ (__  )  __/ / / / /|  / /_/ /| |/ / " << std::endl
              << "  /_/  |_/_/ /_/ /_/\\___/_/____/\\___/_/ /_/_/ |_/\\__,_/ |___/" << std::endl
              << "                                          Exporter " << AMEISENNAV_VERSION << "\033[0m" << std::endl
              << std::endl;

    std::string wowDir = "";
    std::string outputDir = "";
    int targetMapId = -1;
    int targetTileX = -1;
    int targetTileY = -1;

    // Manual Argument Parsing
    if (argc == 1)
    {
        // Debug Mode / Default Args
        wowDir = "Z:\\WoW\\WotLK-Bot";
        outputDir = "Z:\\WoW\\Navmesh";
        targetMapId = 0;
        targetTileX = 32;
        targetTileY = 48;
        LogI(std::format("Debug mode enabled. Using defaults: -w \"{}\" -o "
                         "\"{}\" -m {} -t {},{}",
                         wowDir, outputDir, targetMapId, targetTileX, targetTileY));
    }
    else
    {
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
                targetMapId = std::stoi(std::string(argv[++i]));
            }
            else if ((arg == "--tile" || arg == "-t") && i + 1 < argc)
            {
                std::string tileStr = argv[++i];
                size_t commaPos = tileStr.find(',');
                if (commaPos != std::string::npos)
                {
                    targetTileX = std::stoi(tileStr.substr(0, commaPos));
                    targetTileY = std::stoi(tileStr.substr(commaPos + 1));
                }
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

    LogI(std::format("Starting export. Game Data: \"{}\", Output: \"{}\"", wowPath.string(), outputDir));

    MpqManager mpqManager(wowPath.string().c_str());
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
            liquidTypes[liquidTypeDbc->Read<unsigned int>(i, 0u)] =
                static_cast<LiquidType>(liquidTypeDbc->Read<unsigned int>(i, 3u));
        }
    }

    const std::set<int> unusedMapIds{
        13,  169, 25,  29,  42,  451, 573, 582, 584, 586, 587, 588, 589, 590, 591, 592, 593, 594, 596,
        597, 605, 606, 610, 612, 613, 614, 620, 621, 622, 623, 641, 642, 647, 672, 673, 712, 713, 718,
    };

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

        if (targetMapId != -1 && mapId != (unsigned int)targetMapId)
            continue;
        if (unusedMapIds.find(mapId) != unusedMapIds.end())
            continue;

        START_TIMER(startTimeTile);

        const auto mapsPath = std::format("World\\Maps\\{}\\{}", mapName, mapName);
        const auto wdtPath = std::format("{}.wdt", mapsPath);

        if (Wdt* wdt = mpqReader.GetFileContent<Wdt>(wdtPath.c_str()))
        {
            Structure mapGeometry;
            WaterMap waterMap;
            RoadMap roadMap;

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

                    Logger::Log(Logger::Level::Map, "[", mapName, "] ",
                                std::format("Processing ADT({}, {}) {}", x, y, adtPath));

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
                        }

                        // Step 3: Extract object geometry
                        ExtractWmoGeometry(adt, mpqReader, &terrain, liquidTypes);
                        ExtractDoodadGeometry(adt, mpqReader, &terrain);

                        terrain.Clean();

                        if (!terrain.verts.empty())
                            rcCalcBounds(reinterpret_cast<const float*>(terrain.verts.data()), terrain.verts.size(),
                                         terrain.bbMin, terrain.bbMax);

                        terrain.bbMax[0] = (32 - x) * TILESIZE;
                        terrain.bbMax[2] = (32 - y) * TILESIZE;
                        terrain.bbMin[0] = terrain.bbMax[0] - TILESIZE;
                        terrain.bbMin[2] = terrain.bbMax[2] - TILESIZE;

                        terrain.CleanOutOfBounds(terrain.bbMin, terrain.bbMax);

#pragma omp critical
                        {
                            rcVmin(params.orig, terrain.bbMin);
                            mapGeometry.Append(terrain);
                        }
                    }
                }
            }

            if (!mapGeometry.verts.empty())
            {
                bool isDebug = (argc == 1);
                Anp anp(mapId, params);
                AdtTileProcessor tileProcessor(&anp, outputDir, isDebug);

                if (isDebug)
                {
                    const auto objPath =
                        std::format("Z:\\recastnavigation\\RecastDemo\\Bin\\Meshes\\structure_{}.obj", mapId);
                    mapGeometry.ExportDebugObjFile(objPath.c_str());
                    LogI(std::format("Exported debug OBJ to {}", objPath));
                }

                START_TIMER(startTimeNavmesh);
                waterMap.BuildSpatialIndex();
                rcCalcBounds(mapGeometry.Verts(), mapGeometry.verts.size(), mapGeometry.bbMin, mapGeometry.bbMax);

                if (isDebug)
                {
                    LogI(std::format("WaterMap rects: {}, Geometry bbMin=({},{},{}), bbMax=({},{},{})",
                                     waterMap.rects.size(), mapGeometry.bbMin[0], mapGeometry.bbMin[1],
                                     mapGeometry.bbMin[2], mapGeometry.bbMax[0], mapGeometry.bbMax[1],
                                     mapGeometry.bbMax[2]));
                    if (!waterMap.rects.empty())
                    {
                        const auto& r = waterMap.rects[0];
                        LogI(std::format("  Sample rect[0]: X=[{},{}] Z=[{},{}] h=[{},{},{},{}] type={}", r.minX,
                                         r.maxX, r.minZ, r.maxZ, r.heights[0], r.heights[1], r.heights[2], r.heights[3],
                                         (int)r.type));
                    }
                    LogI(std::format("RoadMap rects: {}", roadMap.rects.size()));
                }

                tileProcessor.Process(&mapGeometry, &waterMap, &roadMap);
                STOP_TIMER(startTimeNavmesh, "Building Navmesh took");

                anp.Save(outputDir.c_str());
                LogS(std::format("Saved navmesh to {}/bin/{:03}.anp", outputDir, mapId));
            }

            STOP_TIMER(startTimeTile, std::format("Parsing Map context [{}] took", mapName));
        }
    }

    return 0;
}
