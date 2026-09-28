#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "Processors/AdtTileProcessor.hpp"

constexpr auto AMEISENNAV_VERSION = "1.9.0.0";

/// What to export and how.
struct ExportOptions
{
    std::filesystem::path wowDir;    // WoW client folder or its Data folder
    std::filesystem::path outputDir; // folder for the .anp files
    std::vector<int> mapIds;         // empty = all maps in Map.dbc
    int tileX = -1;                  // only export this ADT (debugging), -1 = all
    int tileY = -1;
    bool debug = false;              // area debug images
    bool skipExisting = false;       // don't rebuild maps whose .anp already exists
    TileBuildConfig buildConfig{};
};

/// Outcome of one map.
struct MapExportStats
{
    unsigned int mapId = 0;
    std::string name;
    bool exported = false; // .anp written
    bool skipped = false;  // nothing to export (no WDT/terrain) or skipExisting
    bool failed = false;
    bool wmoOnly = false;  // dungeon style map made of a single global WMO
    int adts = 0;
    size_t vertices = 0;
    size_t triangles = 0;
    size_t waterRects = 0;
    size_t roadRects = 0;
    size_t cityRects = 0;
    size_t factionRects = 0;
    size_t wmoPlacements = 0;
    size_t doodadPlacements = 0;
    int tilesBuilt = 0;
    int tilesFailed = 0;
};

struct ExportReport
{
    bool setupOk = false; // MPQs and Map.dbc could be loaded
    int failedMaps = 0;
    std::vector<MapExportStats> maps;
};

/// Accept both the client folder and its Data folder.
std::filesystem::path ResolveDataDir(const std::filesystem::path& wowDir);

/// Maps listed in Map.dbc (id, directory name). Empty if the client data can't be read.
std::vector<std::pair<unsigned int, std::string>> ListMaps(const std::filesystem::path& wowDir);

/// Export the selected maps to .anp navmeshes.
ExportReport RunExport(const ExportOptions& options);
