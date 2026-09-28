#include "Main.hpp"

#include <charconv>
#include <cstdio>
#include <string_view>

namespace {
struct Options
{
    std::string wowDir;
    std::string outputDir;
    std::vector<int> mapIds; // empty = all maps
    int tileX = -1;
    int tileY = -1;
    int threads = 0;
    bool debug = false;
    bool listMaps = false;
};

void PrintUsage()
{
    std::printf(
        "Usage: AmeisenNavigation.Exporter --wow <path> --output <path> [options]\n\n"
        "  -w, --wow <path>        WoW client folder (or its Data folder)\n"
        "  -o, --output <path>     Output folder for the .anp files\n"
        "  -m, --map <ids>         Only export these map ids (comma separated, e.g. 0,1,530,571)\n"
        "  -t, --tile <x,y>        Only export a single ADT (debugging)\n"
        "  -j, --threads <n>       Number of worker threads (default: all cores)\n"
        "  -d, --debug             Debug logging and area debug images (<output>/debug)\n"
        "  -l, --list-maps         List the maps in Map.dbc and exit\n"
        "  -h, --help              Show this help\n\n"
        "Example: AmeisenNavigation.Exporter -w \"C:\\WoW\" -o \"C:\\meshes\" -m 0,1 -j 8\n");
}

bool ParseInt(std::string_view s, int& out) noexcept
{
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && ptr == s.data() + s.size();
}

/// Returns false (after printing the reason) if the arguments are invalid.
bool ParseArguments(int argc, char** argv, Options& options, bool& exitEarly)
{
    exitEarly = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;

        if (arg == "--help" || arg == "-h")
        {
            PrintUsage();
            exitEarly = true;
            return true;
        }

        if (arg == "--debug" || arg == "-d")
        {
            options.debug = true;
        }
        else if (arg == "--list-maps" || arg == "-l")
        {
            options.listMaps = true;
        }
        else if ((arg == "--wow" || arg == "-w") && hasValue)
        {
            options.wowDir = argv[++i];
        }
        else if ((arg == "--output" || arg == "-o") && hasValue)
        {
            options.outputDir = argv[++i];
        }
        else if ((arg == "--map" || arg == "-m") && hasValue)
        {
            std::string_view list = argv[++i];

            while (!list.empty())
            {
                const auto comma = list.find(',');
                const auto token = list.substr(0, comma);
                int id = 0;

                if (!ParseInt(token, id) || id < 0)
                {
                    LogE("Invalid --map value: ", token);
                    return false;
                }

                options.mapIds.push_back(id);
                list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
            }
        }
        else if ((arg == "--tile" || arg == "-t") && hasValue)
        {
            const std::string_view tile = argv[++i];
            const auto comma = tile.find(',');

            if (comma == std::string_view::npos || !ParseInt(tile.substr(0, comma), options.tileX)
                || !ParseInt(tile.substr(comma + 1), options.tileY) || options.tileX < 0 || options.tileX >= 64
                || options.tileY < 0 || options.tileY >= 64)
            {
                LogE("Invalid --tile value: ", tile, " (expected x,y with 0 <= x,y < 64)");
                return false;
            }
        }
        else if ((arg == "--threads" || arg == "-j") && hasValue)
        {
            if (!ParseInt(argv[++i], options.threads) || options.threads < 1)
            {
                LogE("Invalid --threads value: ", argv[i]);
                return false;
            }
        }
        else
        {
            LogE("Unknown or incomplete argument: ", arg);
            PrintUsage();
            return false;
        }
    }

    if (options.wowDir.empty() || (options.outputDir.empty() && !options.listMaps))
    {
        LogE("Missing required arguments.");
        PrintUsage();
        return false;
    }

    return true;
}

struct AreaData
{
    std::unordered_map<unsigned int, unsigned char> factions; // areaId -> 1 Alliance / 2 Horde
    std::unordered_set<unsigned int> cities;
};

/// AreaTable.dbc (WotLK 3.3.5a): field 0 = ID, 2 = ParentAreaID, 4 = Flags, 28 = FactionGroupMask.
/// FactionGroupMask: 0 = contested, 2 = Alliance, 4 = Horde, 6 = sanctuary. Flags: 0x08 capital, 0x20 town.
/// Sub areas (e.g. "Trade District") inherit faction/city status from their parents.
AreaData LoadAreaData(Dbc* areaTableDbc)
{
    AreaData result;

    if (!areaTableDbc || !areaTableDbc->IsValid())
    {
        LogW("AreaTable.dbc missing or invalid - faction/city data will be unavailable");
        return result;
    }

    constexpr unsigned int AREA_FLAG_CAPITAL = 0x08;
    constexpr unsigned int AREA_FLAG_SLAVE_CAPITAL = 0x20;

    struct AreaInfo
    {
        unsigned int parentId;
        unsigned int flags;
        unsigned char faction; // 0 = unknown, 1 = Alliance, 2 = Horde
    };

    std::unordered_map<unsigned int, AreaInfo> allAreas;

    for (unsigned int i = 0u; i < areaTableDbc->GetRecordCount(); ++i)
    {
        const auto factionGroupMask = areaTableDbc->Read<unsigned int>(i, 28u);
        const unsigned char faction = factionGroupMask == 2 ? 1 : (factionGroupMask == 4 ? 2 : 0);
        allAreas[areaTableDbc->Read<unsigned int>(i, 0u)] = {areaTableDbc->Read<unsigned int>(i, 2u),
                                                               areaTableDbc->Read<unsigned int>(i, 4u), faction};
    }

    const auto walkParents = [&](unsigned int areaId, auto&& predicate) -> const AreaInfo* {
        unsigned int current = areaId;

        for (int depth = 0; depth < 10; ++depth)
        {
            const auto it = allAreas.find(current);

            if (it == allAreas.end())
                return nullptr;

            if (predicate(it->second))
                return &it->second;

            if (it->second.parentId == 0 || it->second.parentId == current)
                return nullptr;

            current = it->second.parentId;
        }

        return nullptr;
    };

    for (const auto& [areaId, info] : allAreas)
    {
        if (const AreaInfo* f = walkParents(areaId, [](const AreaInfo& a) { return a.faction != 0; }))
        {
            result.factions[areaId] = f->faction;
        }

        if (walkParents(areaId,
                        [&](const AreaInfo& a) { return (a.flags & (AREA_FLAG_CAPITAL | AREA_FLAG_SLAVE_CAPITAL)) != 0; }))
        {
            result.cities.insert(areaId);
        }
    }

    LogI(std::format("AreaTable.dbc: {} areas, {} faction-marked, {} city-marked", allAreas.size(),
                     result.factions.size(), result.cities.size()));
    return result;
}

/// Extract everything the navmesh needs from one ADT into the map wide containers.
void ExtractAdt(Adt* adt, CachedFileReader& reader, Structure& mapGeometry, WaterMap& waterMap, RoadMap& roadMap,
                CityMap& cityMap, FactionMap& factionMap, PlacementSet& wmoPlacements, PlacementSet& doodadPlacements,
                const std::unordered_map<unsigned int, LiquidType>& liquidTypes, const AreaData& areas)
{
    Structure geometry;

    const MTEX* mtex = adt->Mtex();
    const auto roadTextureIds = FindRoadTextureIds(adt->ChunkInBounds(mtex) ? mtex : nullptr);

    for (int a = 0; a < ADT_CELLS_PER_GRID * ADT_CELLS_PER_GRID; ++a)
    {
        const int cx = a % ADT_CELLS_PER_GRID;
        const int cy = a / ADT_CELLS_PER_GRID;

        ExtractTerrain(adt, cx, cy, &geometry);
        ExtractLiquid(adt, cx, cy, &waterMap, &geometry, liquidTypes);
        ExtractRoadCoverage(adt, cx, cy, &roadMap, roadTextureIds);
        ExtractCityCoverage(adt, cx, cy, &cityMap, areas.cities);
        ExtractFactionCoverage(adt, cx, cy, &factionMap, areas.factions);
    }

    // Objects are extracted once per placement (uniqueId) and never clipped at ADT borders.
    ExtractWmoGeometry(adt, reader, &geometry, liquidTypes, &wmoPlacements);
    ExtractDoodadGeometry(adt, reader, &geometry, &doodadPlacements);

    geometry.Clean();

    const std::lock_guard lock(mapGeometry.mutex);
    mapGeometry.Append(geometry);
}

/// Export one map. Returns false on errors (missing data is not an error).
bool ExportMap(unsigned int mapId, const std::string& mapName, const Options& options, CachedFileReader& reader,
               const std::unordered_map<unsigned int, LiquidType>& liquidTypes, const AreaData& areas)
{
    const auto mapStart = std::chrono::steady_clock::now();
    const auto mapsPath = std::format("World\\Maps\\{}\\{}", mapName, mapName);
    const Wdt* wdt = reader.GetFileContent<Wdt>(std::format("{}.wdt", mapsPath).c_str());

    if (!wdt || !wdt->IsValid())
    {
        LogD("[", mapName, "] no WDT, skipping");
        return true;
    }

    struct AdtCoord
    {
        int x, y;
    };

    std::vector<AdtCoord> adts;

    for (int y = 0; y < WDT_MAP_SIZE; ++y)
    {
        for (int x = 0; x < WDT_MAP_SIZE; ++x)
        {
            if (options.tileX >= 0 && (x != options.tileX || y != options.tileY))
            {
                continue;
            }

            if (wdt->Main()->adt[y][x].exists)
            {
                adts.push_back({x, y});
            }
        }
    }

    if (adts.empty())
    {
        LogD("[", mapName, "] no terrain tiles (WMO-only map), skipping");
        return true;
    }

    Structure mapGeometry;
    WaterMap waterMap;
    RoadMap roadMap;
    FactionMap factionMap;
    CityMap cityMap;
    PlacementSet wmoPlacements;
    PlacementSet doodadPlacements;

    const int totalAdts = static_cast<int>(adts.size());
    const int progressInterval = std::max(1, totalAdts / 20);
    std::atomic<int> extracted{0};
    std::atomic<int> missing{0};

    LogI(std::format("[{}] Map {}: extracting {} ADTs", mapName, mapId, totalAdts));

#pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < totalAdts; ++i)
    {
        const auto [x, y] = adts[static_cast<size_t>(i)];
        const auto adtPath = std::format("{}_{}_{}.adt", mapsPath, x, y);

        // ADTs are only needed once, don't keep them in the cache.
        UncachedFile file = reader.ReadUncached(adtPath.c_str());
        Adt* adt = file.As<Adt>();

        if (!adt || !adt->IsValid())
        {
            missing.fetch_add(1, std::memory_order_relaxed);
            LogW("[", mapName, "] Missing or invalid ADT: ", adtPath);
            continue;
        }

        ExtractAdt(adt, reader, mapGeometry, waterMap, roadMap, cityMap, factionMap, wmoPlacements, doodadPlacements,
                   liquidTypes, areas);

        const int done = extracted.fetch_add(1, std::memory_order_relaxed) + 1;

        if (done % progressInterval == 0 || done == totalAdts)
        {
            LogP(std::format("[{}] Extracting ADTs: {} / {} ({:.1f}%)", mapName, done, totalAdts,
                             100.0 * done / totalAdts));
        }
    }

    Logger::EndProgress();

    if (mapGeometry.verts.empty() || mapGeometry.tris.empty())
    {
        LogW("[", mapName, "] no geometry extracted, skipping");
        return true;
    }

    // Detour tile grid aligned to the ADT grid: tile (x, y) = ADT (maxX - x, maxY - y).
    int maxAdtX = 0;
    int maxAdtY = 0;

    for (const auto& adt : adts)
    {
        maxAdtX = std::max(maxAdtX, adt.x);
        maxAdtY = std::max(maxAdtY, adt.y);
    }

    dtNavMeshParams params{};
    params.orig[0] = static_cast<float>(31 - maxAdtX) * TILESIZE;
    params.orig[1] = 0.0f;
    params.orig[2] = static_cast<float>(31 - maxAdtY) * TILESIZE;
    params.tileWidth = TILESIZE;
    params.tileHeight = TILESIZE;
    params.maxTiles = WDT_MAP_SIZE * WDT_MAP_SIZE;
    params.maxPolys = 1 << 20; // ignored with 64 bit poly refs, kept positive for 32 bit readers

    std::vector<TileCoord> tiles;
    tiles.reserve(adts.size());

    for (const auto& adt : adts)
    {
        tiles.push_back({maxAdtX - adt.x, maxAdtY - adt.y, adt.x, adt.y});
    }

    waterMap.BuildSpatialIndex();
    roadMap.BuildSpatialIndex();
    cityMap.BuildSpatialIndex();
    factionMap.BuildSpatialIndex();

    LogI(std::format("[{}] Geometry: {} verts, {} tris | Water: {} | Roads: {} | Cities: {} | Factions: {}", mapName,
                     mapGeometry.verts.size(), mapGeometry.tris.size(), waterMap.rects.size(), roadMap.rects.size(),
                     cityMap.rects.size(), factionMap.rects.size()));

    // WMOs/M2s of this map aren't needed anymore, free them before the memory hungry build.
    reader.Clear();

    TileBuildConfig buildConfig;
    buildConfig.debugBmp = options.debug;

    Anp::AnpWriter writer(static_cast<int>(mapId), params);
    AdtTileProcessor processor(&writer, options.outputDir, mapName, buildConfig);
    processor.Process(&mapGeometry, tiles, &waterMap, &roadMap, &factionMap, &cityMap);

    if (writer.GetTileCount() == 0)
    {
        LogW("[", mapName, "] no navmesh tiles were built, nothing saved");
        return processor.GetStats().failed.load() == 0;
    }

    if (!writer.Save(options.outputDir))
    {
        LogE("[", mapName, "] failed to save ", Anp::FileName(static_cast<int>(mapId)));
        return false;
    }

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - mapStart).count();
    LogS(std::format("[{}] Saved {} ({} tiles, {:.1f} MB) in {}", mapName, Anp::FileName(static_cast<int>(mapId)),
                     writer.GetTileCount(), writer.GetStoredBytes() / (1024.0 * 1024.0),
                     Logger::FormatDuration(seconds)));

    return processor.GetStats().failed.load() == 0;
}
} // namespace

int main(int argc, char** argv)
{
    Logger::Initialize();

    Options options;
    bool exitEarly = false;

    if (!ParseArguments(argc, argv, options, exitEarly))
    {
        return 1;
    }

    if (exitEarly)
    {
        return 0;
    }

    Logger::SetDebugEnabled(options.debug || Logger::IsDebugEnabled());

    const bool color = Logger::IsColorEnabled();
    std::fputs(std::format("{}"
                           "      ___                   _                 _   __\n"
                           "     /   |  ____ ___  ___  (_)_______  ____  / | / /___ __   __\n"
                           "    / /| | / __ `__ \\/ _ \\/ / ___/ _ \\/ __ \\/  |/ / __ `/ | / /\n"
                           "   / ___ |/ / / / / /  __/ (__  )  __/ / / / /|  / /_/ /| |/ / \n"
                           "  /_/  |_/_/ /_/ /_/\\___/_/____/\\___/_/ /_/_/ |_/\\__,_/ |___/\n"
                           "                                          Exporter {}{}\n\n",
                           color ? "\033[96m" : "", AMEISENNAV_VERSION, color ? "\033[0m" : "")
                   .c_str(),
               stdout);

#ifdef _OPENMP
    if (options.threads > 0)
    {
        omp_set_num_threads(options.threads);
    }

    // Tiles use nested parallelism for single-ADT exports (MSVC only implements OpenMP 2.0).
#if _OPENMP >= 200805
    omp_set_max_active_levels(2);
#else
    omp_set_nested(1);
#endif
#endif

    // Accept both the client folder and its Data folder.
    std::filesystem::path wowPath(options.wowDir);

    if (wowPath.filename().string() != "Data" && std::filesystem::exists(wowPath / "Data"))
    {
        wowPath /= "Data";
    }

    try
    {
        if (!std::filesystem::is_directory(wowPath))
        {
            LogE("Game data directory does not exist: \"", wowPath.string(), "\"");
            return 1;
        }

        if (!options.listMaps && !std::filesystem::exists(options.outputDir))
        {
            LogI("Creating output directory: \"", options.outputDir, "\"");
            std::filesystem::create_directories(options.outputDir);
        }
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        LogE("Filesystem error: ", e.what());
        return 1;
    }

    LogI("Game data: \"", wowPath.string(), "\"");

    MpqManager mpqManager(wowPath);

    if (mpqManager.GetArchiveCount() == 0)
    {
        LogE("No MPQ archives found in \"", wowPath.string(), "\"");
        return 1;
    }

    CachedFileReader reader(&mpqManager);

    // Map.dbc: field 0 = id, field 1 = internal name (directory)
    std::vector<std::pair<unsigned int, std::string>> maps;
    Dbc* mapDbc = reader.GetFileContent<Dbc>("DBFilesClient\\Map.dbc");

    if (!mapDbc || !mapDbc->IsValid())
    {
        LogE("Map.dbc is missing or invalid - cannot proceed");
        return 1;
    }

    for (unsigned int i = 0u; i < mapDbc->GetRecordCount(); ++i)
    {
        maps.emplace_back(mapDbc->Read<unsigned int>(i, 0u), mapDbc->ReadString(i, 1u));
    }

    if (options.listMaps)
    {
        for (const auto& [id, name] : maps)
        {
            std::printf("%5u  %s\n", id, name.c_str());
        }

        return 0;
    }

    // LiquidType.dbc: field 0 = id, field 3 = type (0 water, 1 ocean, 2 magma, 3 slime)
    std::unordered_map<unsigned int, LiquidType> liquidTypes;

    if (Dbc* liquidTypeDbc = reader.GetFileContent<Dbc>("DBFilesClient\\LiquidType.dbc");
        liquidTypeDbc && liquidTypeDbc->IsValid())
    {
        for (unsigned int i = 0u; i < liquidTypeDbc->GetRecordCount(); ++i)
        {
            liquidTypes[liquidTypeDbc->Read<unsigned int>(i, 0u)] =
                static_cast<LiquidType>(liquidTypeDbc->Read<unsigned int>(i, 3u));
        }
    }
    else
    {
        LogW("LiquidType.dbc missing or invalid - all liquids will be treated as water");
    }

    const AreaData areas = LoadAreaData(reader.GetFileContent<Dbc>("DBFilesClient\\AreaTable.dbc"));

    for (const int id : options.mapIds)
    {
        if (std::none_of(maps.begin(), maps.end(), [id](const auto& m) { return m.first == static_cast<unsigned>(id); }))
        {
            LogW("Map ", id, " does not exist in Map.dbc");
        }
    }

    const auto exportStart = std::chrono::steady_clock::now();
    int failedMaps = 0;

    for (const auto& [mapId, mapName] : maps)
    {
        if (!options.mapIds.empty()
            && std::find(options.mapIds.begin(), options.mapIds.end(), static_cast<int>(mapId)) == options.mapIds.end())
        {
            continue;
        }

        if (!ExportMap(mapId, mapName, options, reader, liquidTypes, areas))
        {
            failedMaps++;
        }

        // Map names, liquid types and areas are plain copies, the cached files can go.
        reader.Clear();
    }

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - exportStart).count();

    if (failedMaps > 0)
    {
        LogE("Export finished with ", failedMaps, " failed map(s) in ", Logger::FormatDuration(seconds));
        return 2;
    }

    LogS("Export finished in ", Logger::FormatDuration(seconds));
    return 0;
}
