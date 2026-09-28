#include "Exporter.hpp"

#include "Main.hpp"

namespace {
struct AreaData
{
    std::unordered_map<unsigned int, unsigned char> factions; // areaId -> 1 Alliance / 2 Horde
    std::unordered_set<unsigned int> cities;
};

struct ClientData
{
    std::vector<std::pair<unsigned int, std::string>> maps;
    std::unordered_map<unsigned int, LiquidType> liquidTypes;
    AreaData areas;
};

/// AreaTable.dbc (WotLK 3.3.5a): field 0 = ID, 2 = ParentAreaID, 4 = Flags, 28 = FactionGroupMask.
/// FactionGroupMask: 0 = contested, 2 = Alliance, 4 = Horde, 6 = sanctuary. Flags: 0x08 capital, 0x20 town.
/// Sub areas (e.g. "Trade District") inherit faction/city status from their parents.
AreaData LoadAreaData(const Dbc* areaTableDbc)
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

    for (const auto& area : allAreas)
    {
        const unsigned int areaId = area.first;

        if (const AreaInfo* f = walkParents(areaId, [](const AreaInfo& a) { return a.faction != 0; }))
        {
            result.factions[areaId] = f->faction;
        }

        if (walkParents(areaId, [](const AreaInfo& a) {
                return (a.flags & (AREA_FLAG_CAPITAL | AREA_FLAG_SLAVE_CAPITAL)) != 0;
            }))
        {
            result.cities.insert(areaId);
        }
    }

    LogI(std::format("AreaTable.dbc: {} areas, {} faction-marked, {} city-marked", allAreas.size(),
                     result.factions.size(), result.cities.size()));
    return result;
}

/// Load Map.dbc (required), LiquidType.dbc and AreaTable.dbc.
bool LoadClientData(CachedFileReader& reader, ClientData& data, bool mapsOnly)
{
    // Map.dbc: field 0 = id, field 1 = internal name (directory)
    const Dbc* mapDbc = reader.GetFileContent<Dbc>("DBFilesClient\\Map.dbc");

    if (!mapDbc || !mapDbc->IsValid())
    {
        LogE("Map.dbc is missing or invalid - cannot proceed");
        return false;
    }

    for (unsigned int i = 0u; i < mapDbc->GetRecordCount(); ++i)
    {
        data.maps.emplace_back(mapDbc->Read<unsigned int>(i, 0u), mapDbc->ReadString(i, 1u));
    }

    if (mapsOnly)
    {
        return true;
    }

    // LiquidType.dbc: field 0 = id, field 3 = type (0 water, 1 ocean, 2 magma, 3 slime)
    if (const Dbc* liquidTypeDbc = reader.GetFileContent<Dbc>("DBFilesClient\\LiquidType.dbc");
        liquidTypeDbc && liquidTypeDbc->IsValid())
    {
        for (unsigned int i = 0u; i < liquidTypeDbc->GetRecordCount(); ++i)
        {
            data.liquidTypes[liquidTypeDbc->Read<unsigned int>(i, 0u)] =
                static_cast<LiquidType>(liquidTypeDbc->Read<unsigned int>(i, 3u));
        }
    }
    else
    {
        LogW("LiquidType.dbc missing or invalid - all liquids will be treated as water");
    }

    data.areas = LoadAreaData(reader.GetFileContent<Dbc>("DBFilesClient\\AreaTable.dbc"));
    return true;
}

/// Everything extracted from one map, shared by all extraction threads.
struct MapData
{
    Structure geometry;
    WaterMap water;
    RoadMap roads;
    CityMap cities;
    FactionMap factions;
    PlacementSet wmoPlacements;
    PlacementSet doodadPlacements;
};

/// Extract everything the navmesh needs from one ADT into the map wide containers.
void ExtractAdt(Adt* adt, CachedFileReader& reader, MapData& map, const ClientData& client)
{
    Structure geometry;

    const MTEX* mtex = adt->Mtex();
    const auto roadTextureIds = FindRoadTextureIds(adt->ChunkInBounds(mtex) ? mtex : nullptr);

    for (int a = 0; a < ADT_CELLS_PER_GRID * ADT_CELLS_PER_GRID; ++a)
    {
        const int cx = a % ADT_CELLS_PER_GRID;
        const int cy = a / ADT_CELLS_PER_GRID;

        ExtractTerrain(adt, cx, cy, &geometry);
        ExtractLiquid(adt, cx, cy, &map.water, &geometry, client.liquidTypes);
        ExtractRoadCoverage(adt, cx, cy, &map.roads, roadTextureIds);
        ExtractCityCoverage(adt, cx, cy, &map.cities, client.areas.cities);
        ExtractFactionCoverage(adt, cx, cy, &map.factions, client.areas.factions);
    }

    // Objects are extracted once per placement (uniqueId) and never clipped at ADT borders.
    ExtractWmoGeometry(adt, reader, &geometry, client.liquidTypes, &map.wmoPlacements);
    ExtractDoodadGeometry(adt, reader, &geometry, &map.doodadPlacements);

    geometry.Clean();

    const std::lock_guard lock(map.geometry.mutex);
    map.geometry.Append(geometry);
}

struct AdtCoord
{
    int x, y;
};

/// ADT grid cells covered by the geometry's bounds (for maps without ADTs).
std::vector<AdtCoord> AdtsCoveringGeometry(Structure& geometry)
{
    geometry.ComputeBounds();

    // RD x = wowY, ADT x covers RD x in [(31 - x) * T, (32 - x) * T] (same for y / RD z).
    const auto toAdt = [](float rd) { return std::clamp(31 - static_cast<int>(std::floor(rd / TILESIZE)), 0, 63); };

    const int x0 = toAdt(geometry.bbMax[0]), x1 = toAdt(geometry.bbMin[0]);
    const int y0 = toAdt(geometry.bbMax[2]), y1 = toAdt(geometry.bbMin[2]);

    std::vector<AdtCoord> adts;

    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            adts.push_back({x, y});
        }
    }

    return adts;
}

/// Export one map. Missing data is not an error, only failed tiles or I/O are.
MapExportStats ExportMap(unsigned int mapId, const std::string& mapName, const ExportOptions& options,
                         CachedFileReader& reader, const ClientData& client)
{
    MapExportStats stats;
    stats.mapId = mapId;
    stats.name = mapName;

    const auto mapStart = std::chrono::steady_clock::now();

    if (options.skipExisting && std::filesystem::exists(options.outputDir / Anp::FileName(static_cast<int>(mapId))))
    {
        LogI("[", mapName, "] ", Anp::FileName(static_cast<int>(mapId)), " exists, skipping");
        stats.skipped = true;
        return stats;
    }

    const auto mapsPath = std::format("World\\Maps\\{}\\{}", mapName, mapName);
    const Wdt* wdt = reader.GetFileContent<Wdt>(std::format("{}.wdt", mapsPath).c_str());

    if (!wdt || !wdt->IsValid())
    {
        LogD("[", mapName, "] no WDT, skipping");
        stats.skipped = true;
        return stats;
    }

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

    MapData map;

    if (adts.empty())
    {
        // Dungeons and raids: one global WMO instead of terrain.
        if (options.tileX >= 0 || !ExtractGlobalWmo(wdt, reader, &map.geometry, client.liquidTypes))
        {
            LogD("[", mapName, "] no terrain and no global WMO, skipping");
            stats.skipped = true;
            return stats;
        }

        stats.wmoOnly = true;
        stats.wmoPlacements = 1;
        map.geometry.Clean();

        if (!map.geometry.verts.empty())
        {
            adts = AdtsCoveringGeometry(map.geometry);
        }

        LogI(std::format("[{}] Map {}: global WMO \"{}\" covering {} tiles", mapName, mapId, wdt->GlobalWmoName(),
                         adts.size()));
    }
    else
    {
        const int totalAdts = static_cast<int>(adts.size());
        const int progressInterval = std::max(1, totalAdts / 20);
        std::atomic<int> extracted{0};

        LogI(std::format("[{}] Map {}: extracting {} ADTs", mapName, mapId, totalAdts));

#pragma omp parallel for schedule(dynamic, 1)
        for (int i = 0; i < totalAdts; ++i)
        {
            const AdtCoord coord = adts[static_cast<size_t>(i)];
            const auto adtPath = std::format("{}_{}_{}.adt", mapsPath, coord.x, coord.y);

            // ADTs are only needed once, don't keep them in the cache.
            UncachedFile file = reader.ReadUncached(adtPath.c_str());
            Adt* adt = file.As<Adt>();

            if (!adt || !adt->IsValid())
            {
                LogW("[", mapName, "] Missing or invalid ADT: ", adtPath);
                continue;
            }

            ExtractAdt(adt, reader, map, client);

            const int done = extracted.fetch_add(1, std::memory_order_relaxed) + 1;

            if (done % progressInterval == 0 || done == totalAdts)
            {
                LogP(std::format("[{}] Extracting ADTs: {} / {} ({:.1f}%)", mapName, done, totalAdts,
                                 100.0 * done / totalAdts));
            }
        }

        Logger::EndProgress();
        stats.wmoPlacements = map.wmoPlacements.Size();
        stats.doodadPlacements = map.doodadPlacements.Size();
    }

    stats.adts = static_cast<int>(adts.size());
    stats.vertices = map.geometry.verts.size();
    stats.triangles = map.geometry.tris.size();
    stats.waterRects = map.water.rects.size();
    stats.roadRects = map.roads.rects.size();
    stats.cityRects = map.cities.rects.size();
    stats.factionRects = map.factions.rects.size();

    if (map.geometry.verts.empty() || map.geometry.tris.empty())
    {
        LogW("[", mapName, "] no geometry extracted, skipping");
        stats.skipped = true;
        return stats;
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

    map.water.BuildSpatialIndex();
    map.roads.BuildSpatialIndex();
    map.cities.BuildSpatialIndex();
    map.factions.BuildSpatialIndex();

    LogI(std::format("[{}] Geometry: {} verts, {} tris | Water: {} | Roads: {} | Cities: {} | Factions: {}", mapName,
                     stats.vertices, stats.triangles, stats.waterRects, stats.roadRects, stats.cityRects,
                     stats.factionRects));

    // WMOs/M2s of this map aren't needed anymore, free them before the memory hungry build.
    reader.Clear();

    TileBuildConfig buildConfig = options.buildConfig;
    buildConfig.debugBmp = buildConfig.debugBmp || options.debug;

    Anp::AnpWriter writer(static_cast<int>(mapId), params);
    AdtTileProcessor processor(&writer, options.outputDir, mapName, buildConfig);
    processor.Process(&map.geometry, tiles, &map.water, &map.roads, &map.factions, &map.cities);

    stats.tilesBuilt = processor.GetStats().built.load();
    stats.tilesFailed = processor.GetStats().failed.load();
    stats.failed = stats.tilesFailed > 0;

    if (writer.GetTileCount() == 0)
    {
        LogW("[", mapName, "] no navmesh tiles were built, nothing saved");
        stats.skipped = !stats.failed;
        return stats;
    }

    if (!writer.Save(options.outputDir))
    {
        LogE("[", mapName, "] failed to save ", Anp::FileName(static_cast<int>(mapId)));
        stats.failed = true;
        return stats;
    }

    stats.exported = true;

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - mapStart).count();
    LogS(std::format("[{}] Saved {} ({} tiles, {:.1f} MB) in {}", mapName, Anp::FileName(static_cast<int>(mapId)),
                     writer.GetTileCount(), writer.GetStoredBytes() / (1024.0 * 1024.0),
                     Logger::FormatDuration(seconds)));
    return stats;
}
} // namespace

std::filesystem::path ResolveDataDir(const std::filesystem::path& wowDir)
{
    std::error_code ec;

    if (wowDir.filename().string() != "Data" && std::filesystem::is_directory(wowDir / "Data", ec))
    {
        return wowDir / "Data";
    }

    return wowDir;
}

std::vector<std::pair<unsigned int, std::string>> ListMaps(const std::filesystem::path& wowDir)
{
    const auto dataDir = ResolveDataDir(wowDir);
    std::error_code ec;

    if (!std::filesystem::is_directory(dataDir, ec))
    {
        LogE("Game data directory does not exist: \"", dataDir.string(), "\"");
        return {};
    }

    MpqManager mpqManager(dataDir);
    CachedFileReader reader(&mpqManager);

    ClientData client;

    if (mpqManager.GetArchiveCount() == 0 || !LoadClientData(reader, client, true))
    {
        return {};
    }

    return client.maps;
}

ExportReport RunExport(const ExportOptions& options)
{
    ExportReport report;
    const auto dataDir = ResolveDataDir(options.wowDir);

    try
    {
        if (!std::filesystem::is_directory(dataDir))
        {
            LogE("Game data directory does not exist: \"", dataDir.string(), "\"");
            return report;
        }

        std::filesystem::create_directories(options.outputDir);
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        LogE("Filesystem error: ", e.what());
        return report;
    }

    LogI("Game data: \"", dataDir.string(), "\"");

    MpqManager mpqManager(dataDir);

    if (mpqManager.GetArchiveCount() == 0)
    {
        LogE("No MPQ archives found in \"", dataDir.string(), "\"");
        return report;
    }

    CachedFileReader reader(&mpqManager);
    ClientData client;

    if (!LoadClientData(reader, client, false))
    {
        return report;
    }

    report.setupOk = true;

    for (const int id : options.mapIds)
    {
        if (std::none_of(client.maps.begin(), client.maps.end(),
                         [id](const auto& m) { return m.first == static_cast<unsigned int>(id); }))
        {
            LogW("Map ", id, " does not exist in Map.dbc");
        }
    }

    const auto exportStart = std::chrono::steady_clock::now();

    for (const auto& map : client.maps)
    {
        const unsigned int mapId = map.first;

        if (!options.mapIds.empty()
            && std::find(options.mapIds.begin(), options.mapIds.end(), static_cast<int>(mapId))
                   == options.mapIds.end())
        {
            continue;
        }

        MapExportStats stats;

        try
        {
            stats = ExportMap(mapId, map.second, options, reader, client);
        }
        catch (const std::exception& e)
        {
            LogE("[", map.second, "] export failed: ", e.what());
            stats.mapId = mapId;
            stats.name = map.second;
            stats.failed = true;
        }

        report.failedMaps += stats.failed ? 1 : 0;
        report.maps.push_back(std::move(stats));

        // Map names, liquid types and areas are plain copies, the cached files can go.
        reader.Clear();
    }

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - exportStart).count();
    const auto exported = std::count_if(report.maps.begin(), report.maps.end(), [](const auto& m) { return m.exported; });

    if (report.failedMaps > 0)
    {
        LogE("Export finished with ", report.failedMaps, " failed map(s), ", exported, " exported, in ",
             Logger::FormatDuration(seconds));
    }
    else
    {
        LogS("Export finished: ", exported, " map(s) exported in ", Logger::FormatDuration(seconds));
    }

    return report;
}
