#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

#include "../../../AmeisenNavigation.Pack/src/Anp.hpp"
#include "../../../AmeisenNavigation/src/Utils/Logger.hpp"

#include "../Utils/CityMap.hpp"
#include "../Utils/FactionMap.hpp"
#include "../Utils/RoadMap.hpp"
#include "../Utils/Structure.hpp"
#include "../Utils/WaterMap.hpp"
#include "../Wow/AdtStructs.hpp"

#include "AreaMarker.hpp"
#include "BmpRenderer.hpp"

// ─────────────────────────────────────────────
// AdtTileProcessor - builds one Detour tile per ADT from the merged map geometry.
//
// Every tile is built from 80x80 cell sub-tiles (small heightfields, low memory) which are merged
// into one poly mesh per tile. Pipeline per sub-tile (two-pass rasterization):
//   1a. Rasterize TERRAIN triangles -> heightfield
//   2.  Filter walkable spans (ledge, low-height, etc.)
//   1b. Rasterize WATER triangles -> same heightfield (AFTER the filters: water has no ledges)
//   3.  Build compact heightfield
//   4.  Erode + median filter
//   5.  Mark water / road / city / faction areas
//   6.  Render to BMP (debug only)
//   7.  Build regions -> contours -> polymesh + detail mesh
//
// Performance: triangles are bucketed per tile and per sub-tile up front, so each sub-tile only
// rasterizes the handful of triangles that overlap it (instead of the whole map), and the area
// markers use spatial indices. Total cost is O(triangles + cells) instead of O(sub-tiles x triangles).
//
// Parallelism: tiles are built in parallel; when there are fewer tiles than threads (single ADT
// exports) the sub-tiles of each tile are built in parallel instead.
// ─────────────────────────────────────────────

/// Detour tile coordinates of an ADT inside the navmesh grid.
struct TileCoord
{
    int x;    // Detour tile x
    int y;    // Detour tile y
    int adtX; // WoW ADT x (for logging/debug output)
    int adtY; // WoW ADT y
};

/// Recast build settings. Defaults reproduce the historic AmeisenNavigation meshes.
struct TileBuildConfig
{
    int meshResolution = 2560; // heightfield cells per ADT edge (cs = TILESIZE / meshResolution)
    int subTileSize = 80;      // cells per sub-tile edge
    float walkableSlopeAngle = 55.0f;
    float agentHeight = 2.0f;
    float agentRadius = 0.6f;
    float agentClimb = 1.2f;
    int minRegionSize = 10;          // in cells (squared for the area)
    int mergeRegionSize = 25;        // in cells (squared for the area)
    float maxEdgeLength = 12.0f;     // world units
    float maxSimplificationError = 1.0f;
    float detailSampleDistance = 8.0f; // in cells
    float detailSampleMaxError = 0.5f; // in cell heights
    int maxRetries = 3;                // coarser rebuilds when a tile exceeds Detour's vertex limit
    int maxTileVertices = 0xfffe;      // Detour's per tile vertex limit (only lowered by tests)
    bool debugBmp = false;             // write area debug images per tile
};

/// Recast context forwarding messages to the Logger. One instance per thread (log toggling is per context).
/// Recast warnings are mostly benign geometry notes (e.g. "delaunayHull: Removing dangling face") that would
/// flood a full map export, they are only shown with debug logging. Errors are always shown.
class RecastLogContext : public rcContext
{
public:
    RecastLogContext() noexcept : rcContext(false) { enableLog(true); }

protected:
    void doLog(const rcLogCategory category, const char* msg, const int /*len*/) override
    {
        if (category == RC_LOG_ERROR)
        {
            LogE("[Recast] ", msg);
        }
        else
        {
            LogD("[Recast] ", msg);
        }
    }
};

class AdtTileProcessor
{
public:
    struct Stats
    {
        std::atomic<int> built{0};
        std::atomic<int> empty{0};
        std::atomic<int> failed{0};
        std::atomic<int> retried{0};
    };

private:
    /// Compressed sparse row list: items of bucket i are items[offsets[i] .. offsets[i + 1]).
    struct Buckets
    {
        std::vector<uint32_t> offsets;
        std::vector<uint32_t> items;

        size_t Count(size_t bucket) const noexcept { return offsets[bucket + 1] - offsets[bucket]; }
        const uint32_t* Begin(size_t bucket) const noexcept { return items.data() + offsets[bucket]; }
        const uint32_t* End(size_t bucket) const noexcept { return items.data() + offsets[bucket + 1]; }
    };

    /// Derived Recast parameters for one build attempt.
    struct Params
    {
        float cs, ch;
        int walkableClimb, walkableHeight, walkableRadius;
        int borderSize, subTileSize, subTilesPerAxis, width;
        int minRegionArea, mergeRegionArea, maxEdgeLen;
        float maxSimplificationError, detailSampleDist, detailSampleMaxError;
    };

    /// Per-thread scratch buffers, reused between sub-tiles.
    struct Scratch
    {
        std::vector<int> terrainTris;
        std::vector<unsigned char> terrainAreas;
        std::vector<int> waterTris;
        std::vector<unsigned char> waterAreas;
    };

    Anp::AnpWriter* Writer;
    std::filesystem::path OutputDir;
    std::string MapName;
    TileBuildConfig Config;
    Stats Statistics;

public:
    AdtTileProcessor(Anp::AnpWriter* writer, const std::filesystem::path& outputDir, const std::string& mapName,
                     const TileBuildConfig& config = {}) noexcept
        : Writer(writer),
          OutputDir(outputDir),
          MapName(mapName),
          Config(config)
    {
        Config.meshResolution = std::max(Config.meshResolution, 16);
        Config.subTileSize = std::clamp(Config.subTileSize, 16, Config.meshResolution);
        Config.maxRetries = std::max(Config.maxRetries, 0);
        Config.maxTileVertices = std::clamp(Config.maxTileVertices, 3, 0xfffe);
    }

    const Stats& GetStats() const noexcept { return Statistics; }

    /// Build all tiles and add them to the writer. The structure's area ids are modified (steep -> null area).
    void Process(Structure* structure, const std::vector<TileCoord>& tiles, const WaterMap* waterMap,
                 const RoadMap* roadMap, const FactionMap* factionMap = nullptr,
                 const CityMap* cityMap = nullptr) noexcept
    {
        if (!Writer || !structure || structure->verts.empty() || structure->tris.empty() || tiles.empty())
        {
            return;
        }

        const auto buildStart = std::chrono::steady_clock::now();
        const Params base = MakeParams(0);

        // Mark steep triangles as unwalkable (they still rasterize as obstacles).
        {
            RecastLogContext ctx;
            rcClearUnwalkableTriangles(&ctx, Config.walkableSlopeAngle, structure->Verts(),
                                       static_cast<int>(structure->verts.size()), structure->Tris(),
                                       static_cast<int>(structure->tris.size()), structure->AreaIds());
        }

        const Buckets tileBuckets = BucketTrianglesByTile(*structure, tiles, base.borderSize * base.cs);

#ifdef _OPENMP
        const int numThreads = omp_get_max_threads();
#else
        const int numThreads = 1;
#endif
        const int totalTiles = static_cast<int>(tiles.size());
        const bool parallelTiles = totalTiles >= numThreads;
        const int progressInterval = std::max(1, totalTiles / 50);
        std::atomic<int> completed{0};

        LogI(std::format("[{}] Building {} tiles ({}x{} sub-tiles each) using {} threads", MapName, totalTiles,
                         base.subTilesPerAxis, base.subTilesPerAxis, numThreads));

#pragma omp parallel for schedule(dynamic, 1) if (parallelTiles)
        for (int t = 0; t < totalTiles; ++t)
        {
            BuildTile(*structure, tiles[static_cast<size_t>(t)], tileBuckets, static_cast<size_t>(t), waterMap,
                      roadMap, factionMap, cityMap, !parallelTiles);

            const int done = completed.fetch_add(1, std::memory_order_relaxed) + 1;

            if (done % progressInterval == 0 || done == totalTiles)
            {
                const double elapsed =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - buildStart).count();
                const double eta = (elapsed / done) * (totalTiles - done);
                LogP(std::format("[{}] Building navmesh: {} / {} tiles ({:.1f}%) - ETA: {}", MapName, done,
                                 totalTiles, 100.0 * done / totalTiles, Logger::FormatDuration(eta)));
            }
        }

        Logger::EndProgress();

        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - buildStart).count();
        Logger::Log(Statistics.failed.load() > 0 ? Logger::Level::Warning : Logger::Level::Success,
                    std::format("[{}] Built {} tiles ({} empty, {} failed, {} needed coarser settings) in {}",
                                MapName, Statistics.built.load(), Statistics.empty.load(), Statistics.failed.load(),
                                Statistics.retried.load(), Logger::FormatDuration(elapsed)));
    }

private:
    Params MakeParams(int retry) const noexcept
    {
        Params p{};
        p.cs = TILESIZE / static_cast<float>(Config.meshResolution);
        p.ch = p.cs;
        p.walkableClimb = static_cast<int>(std::ceil(Config.agentClimb / p.ch));
        p.walkableHeight = static_cast<int>(std::floor(Config.agentHeight / p.ch));
        p.walkableRadius = static_cast<int>(std::ceil(Config.agentRadius / p.cs));
        p.borderSize = p.walkableRadius + 3;
        p.subTileSize = Config.subTileSize;
        p.subTilesPerAxis = (Config.meshResolution + p.subTileSize - 1) / p.subTileSize;
        p.width = p.subTileSize + p.borderSize * 2;
        p.minRegionArea = Config.minRegionSize * Config.minRegionSize;
        p.mergeRegionArea = Config.mergeRegionSize * Config.mergeRegionSize;

        // Every retry trades detail for fewer vertices.
        const float coarsen = 1.0f + static_cast<float>(retry);
        p.maxEdgeLen = static_cast<int>(Config.maxEdgeLength * coarsen / p.cs);
        p.maxSimplificationError = Config.maxSimplificationError * coarsen;
        p.detailSampleDist = p.cs * Config.detailSampleDistance * coarsen;
        p.detailSampleMaxError = p.ch * Config.detailSampleMaxError * coarsen;
        return p;
    }

    void TileBounds(const TileCoord& tile, float* bmin, float* bmax) const noexcept
    {
        const float* orig = Writer->GetParams().orig;
        bmin[0] = orig[0] + static_cast<float>(tile.x) * TILESIZE;
        bmin[2] = orig[2] + static_cast<float>(tile.y) * TILESIZE;
        bmax[0] = bmin[0] + TILESIZE;
        bmax[2] = bmin[2] + TILESIZE;
    }

    static void TriangleBounds(const Structure& s, size_t tri, float* bmin, float* bmax) noexcept
    {
        const Tri& t = s.tris[tri];
        const Vector3& a = s.verts[static_cast<size_t>(t.a)];
        const Vector3& b = s.verts[static_cast<size_t>(t.b)];
        const Vector3& c = s.verts[static_cast<size_t>(t.c)];

        for (int i = 0; i < 3; ++i)
        {
            bmin[i] = std::min({a.pos[i], b.pos[i], c.pos[i]});
            bmax[i] = std::max({a.pos[i], b.pos[i], c.pos[i]});
        }
    }

    /// Assign each triangle to every tile its (padded) bounding box overlaps.
    Buckets BucketTrianglesByTile(const Structure& s, const std::vector<TileCoord>& tiles, float padding) const
    {
        int minX = tiles[0].x, maxX = tiles[0].x, minY = tiles[0].y, maxY = tiles[0].y;

        for (const auto& t : tiles)
        {
            minX = std::min(minX, t.x);
            maxX = std::max(maxX, t.x);
            minY = std::min(minY, t.y);
            maxY = std::max(maxY, t.y);
        }

        const int gridW = maxX - minX + 1;
        const int gridH = maxY - minY + 1;

        // Grid cell -> index in `tiles` (or -1 if that tile isn't built).
        std::vector<int> lookup(static_cast<size_t>(gridW) * gridH, -1);

        for (size_t i = 0; i < tiles.size(); ++i)
        {
            lookup[static_cast<size_t>((tiles[i].x - minX) + (tiles[i].y - minY) * gridW)] = static_cast<int>(i);
        }

        const float* orig = Writer->GetParams().orig;

        const auto forEachTile = [&](size_t tri, auto&& fn) {
            float bmin[3], bmax[3];
            TriangleBounds(s, tri, bmin, bmax);

            // Clean() dropped such triangles already, the int conversions below must never see them.
            if (!IsPlausibleCoordinate(bmin[0]) || !IsPlausibleCoordinate(bmax[0]) || !IsPlausibleCoordinate(bmin[2])
                || !IsPlausibleCoordinate(bmax[2]))
            {
                return;
            }

            const int x0 = std::max(minX, static_cast<int>(std::floor((bmin[0] - padding - orig[0]) / TILESIZE)));
            const int x1 = std::min(maxX, static_cast<int>(std::floor((bmax[0] + padding - orig[0]) / TILESIZE)));
            const int y0 = std::max(minY, static_cast<int>(std::floor((bmin[2] - padding - orig[2]) / TILESIZE)));
            const int y1 = std::min(maxY, static_cast<int>(std::floor((bmax[2] + padding - orig[2]) / TILESIZE)));

            for (int y = y0; y <= y1; ++y)
            {
                for (int x = x0; x <= x1; ++x)
                {
                    if (const int idx = lookup[static_cast<size_t>((x - minX) + (y - minY) * gridW)]; idx >= 0)
                    {
                        fn(static_cast<size_t>(idx));
                    }
                }
            }
        };

        Buckets buckets;
        buckets.offsets.assign(tiles.size() + 1, 0);

        for (size_t i = 0; i < s.tris.size(); ++i)
        {
            forEachTile(i, [&](size_t idx) { buckets.offsets[idx + 1]++; });
        }

        for (size_t i = 1; i < buckets.offsets.size(); ++i)
        {
            buckets.offsets[i] += buckets.offsets[i - 1];
        }

        buckets.items.resize(buckets.offsets.back());
        std::vector<uint32_t> cursor(buckets.offsets.begin(), buckets.offsets.end() - 1);

        for (size_t i = 0; i < s.tris.size(); ++i)
        {
            forEachTile(i, [&](size_t idx) { buckets.items[cursor[idx]++] = static_cast<uint32_t>(i); });
        }

        return buckets;
    }

    /// Assign a tile's triangles to the sub-tiles their (padded) bounds overlap.
    static Buckets BucketTrianglesBySubTile(const Structure& s, const uint32_t* begin, const uint32_t* end,
                                            const float* tileMin, const Params& p)
    {
        const int n = p.subTilesPerAxis;
        const float subSize = static_cast<float>(p.subTileSize) * p.cs;
        const float padding = static_cast<float>(p.borderSize) * p.cs;

        const auto range = [&](float lo, float hi, float origin, int& i0, int& i1) {
            // Conservative by one sub-tile on the low side, the rasterizer culls exactly anyway.
            i0 = std::max(0, static_cast<int>(std::floor((lo - origin - padding) / subSize)) - 1);
            i1 = std::min(n - 1, static_cast<int>(std::floor((hi - origin + padding) / subSize)));
        };

        Buckets buckets;
        buckets.offsets.assign(static_cast<size_t>(n) * n + 1, 0);

        for (int pass = 0; pass < 2; ++pass)
        {
            std::vector<uint32_t> cursor;

            if (pass == 1)
            {
                for (size_t i = 1; i < buckets.offsets.size(); ++i)
                {
                    buckets.offsets[i] += buckets.offsets[i - 1];
                }

                buckets.items.resize(buckets.offsets.back());
                cursor.assign(buckets.offsets.begin(), buckets.offsets.end() - 1);
            }

            for (const uint32_t* it = begin; it != end; ++it)
            {
                float bmin[3], bmax[3];
                TriangleBounds(s, *it, bmin, bmax);

                int x0, x1, z0, z1;
                range(bmin[0], bmax[0], tileMin[0], x0, x1);
                range(bmin[2], bmax[2], tileMin[2], z0, z1);

                for (int z = z0; z <= z1; ++z)
                {
                    for (int x = x0; x <= x1; ++x)
                    {
                        const size_t bucket = static_cast<size_t>(x + z * n);

                        if (pass == 0)
                        {
                            buckets.offsets[bucket + 1]++;
                        }
                        else
                        {
                            buckets.items[cursor[bucket]++] = *it;
                        }
                    }
                }
            }
        }

        return buckets;
    }

    void BuildTile(Structure& s, const TileCoord& tile, const Buckets& tileBuckets, size_t tileIndex,
                   const WaterMap* waterMap, const RoadMap* roadMap, const FactionMap* factionMap,
                   const CityMap* cityMap, bool parallelSubTiles) noexcept
    {
        try
        {
            if (tileBuckets.Count(tileIndex) == 0)
            {
                Statistics.empty.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            float tbbMin[3]{};
            float tbbMax[3]{};
            TileBounds(tile, tbbMin, tbbMax);

            // Vertical bounds from this tile's triangles only: spans store heights with 13 bits
            // (8191 * ch ~ 1700 yards), a map wide range would clamp high terrain.
            tbbMin[1] = std::numeric_limits<float>::max();
            tbbMax[1] = std::numeric_limits<float>::lowest();

            for (const uint32_t* it = tileBuckets.Begin(tileIndex); it != tileBuckets.End(tileIndex); ++it)
            {
                float bmin[3], bmax[3];
                TriangleBounds(s, *it, bmin, bmax);
                tbbMin[1] = std::min(tbbMin[1], bmin[1]);
                tbbMax[1] = std::max(tbbMax[1], bmax[1]);
            }

            tbbMin[1] -= 1.0f;
            tbbMax[1] += 1.0f;

            const Params base = MakeParams(0);

            if ((tbbMax[1] - tbbMin[1]) / base.ch > RC_SPAN_MAX_HEIGHT)
            {
                LogW(std::format("[{}] Tile {},{}: height range {:.0f} exceeds the heightfield limit, clamping",
                                 MapName, tile.adtX, tile.adtY, tbbMax[1] - tbbMin[1]));
                tbbMax[1] = tbbMin[1] + RC_SPAN_MAX_HEIGHT * base.ch;
            }

            const Buckets subBuckets =
                BucketTrianglesBySubTile(s, tileBuckets.Begin(tileIndex), tileBuckets.End(tileIndex), tbbMin, base);

            for (int retry = 0; retry <= Config.maxRetries; ++retry)
            {
                const Params p = MakeParams(retry);
                const BuildResult result = BuildTileAttempt(s, tile, tbbMin, tbbMax, subBuckets, p, waterMap,
                                                            roadMap, factionMap, cityMap, parallelSubTiles,
                                                            Config.debugBmp && retry == 0);

                if (result == BuildResult::Built)
                {
                    Statistics.built.fetch_add(1, std::memory_order_relaxed);

                    if (retry > 0)
                    {
                        Statistics.retried.fetch_add(1, std::memory_order_relaxed);
                        LogW(std::format("[{}] Tile {},{}: built with coarser settings (level {})", MapName,
                                         tile.adtX, tile.adtY, retry));
                    }

                    return;
                }

                if (result == BuildResult::Empty)
                {
                    Statistics.empty.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                if (result == BuildResult::Failed)
                {
                    break;
                }

                // BuildResult::TooDetailed -> retry coarser.
            }

            Statistics.failed.fetch_add(1, std::memory_order_relaxed);
            LogE(std::format("[{}] Tile {},{}: failed to build a valid Detour tile", MapName, tile.adtX, tile.adtY));
        }
        catch (const std::exception& e)
        {
            Statistics.failed.fetch_add(1, std::memory_order_relaxed);
            LogE(std::format("[{}] Tile {},{}: {}", MapName, tile.adtX, tile.adtY, e.what()));
        }
    }

    enum class BuildResult
    {
        Built,
        Empty,
        TooDetailed, // exceeds Detour's 16 bit vertex/poly limits
        Failed,
    };

    BuildResult BuildTileAttempt(Structure& s, const TileCoord& tile, const float* tbbMin, const float* tbbMax,
                                 const Buckets& subBuckets, const Params& p, const WaterMap* waterMap,
                                 const RoadMap* roadMap, const FactionMap* factionMap, const CityMap* cityMap,
                                 [[maybe_unused]] bool parallelSubTiles, bool debugBmp)
    {
        const int subTileCount = p.subTilesPerAxis * p.subTilesPerAxis;
        std::vector<rcPolyMesh*> pmeshes(static_cast<size_t>(subTileCount), nullptr);
        std::vector<rcPolyMeshDetail*> dmeshes(static_cast<size_t>(subTileCount), nullptr);
        std::vector<uint8_t> pixels;

        if (debugBmp)
        {
            pixels.assign(static_cast<size_t>(Config.meshResolution) * Config.meshResolution * 3, 0);
        }

        const float subSize = static_cast<float>(p.subTileSize) * p.cs;
        const float padding = static_cast<float>(p.borderSize) * p.cs;

#pragma omp parallel if (parallelSubTiles)
        {
            RecastLogContext ctx;
            Scratch scratch;

#pragma omp for schedule(dynamic, 4)
            for (int st = 0; st < subTileCount; ++st)
            {
                if (subBuckets.Count(static_cast<size_t>(st)) == 0)
                {
                    continue;
                }

                const int stX = st % p.subTilesPerAxis;
                const int stY = st / p.subTilesPerAxis;

                float bmin[3]{tbbMin[0] + stX * subSize - padding, tbbMin[1], tbbMin[2] + stY * subSize - padding};
                float bmax[3]{tbbMin[0] + (stX + 1) * subSize + padding, tbbMax[1],
                              tbbMin[2] + (stY + 1) * subSize + padding};

                GatherSubTileTriangles(s, subBuckets, static_cast<size_t>(st), scratch);

                BuildSubTile(&ctx, s, scratch, bmin, bmax, p, &pmeshes[static_cast<size_t>(st)],
                             &dmeshes[static_cast<size_t>(st)], stX, stY, pixels.empty() ? nullptr : pixels.data(),
                             waterMap, roadMap, factionMap, cityMap);
            }
        }

        if (!pixels.empty())
        {
            SaveDebugBmp(OutputDir, Writer->GetMapId(), tile.adtX, tile.adtY, Config.meshResolution, pixels.data());
        }

        // Compact the non-empty meshes to the front for merging.
        int meshCount = 0;
        int vertexUpperBound = 0;

        for (int i = 0; i < subTileCount; ++i)
        {
            if (pmeshes[static_cast<size_t>(i)] && dmeshes[static_cast<size_t>(i)]
                && pmeshes[static_cast<size_t>(i)]->npolys > 0)
            {
                vertexUpperBound += pmeshes[static_cast<size_t>(i)]->nverts;
                std::swap(pmeshes[static_cast<size_t>(meshCount)], pmeshes[static_cast<size_t>(i)]);
                std::swap(dmeshes[static_cast<size_t>(meshCount)], dmeshes[static_cast<size_t>(i)]);
                meshCount++;
            }
        }

        const auto freeSubMeshes = [&]() {
            for (int i = 0; i < subTileCount; ++i)
            {
                rcFreePolyMesh(pmeshes[static_cast<size_t>(i)]);
                rcFreePolyMeshDetail(dmeshes[static_cast<size_t>(i)]);
            }
        };

        if (meshCount == 0)
        {
            freeSubMeshes();
            return BuildResult::Empty;
        }

        RecastLogContext ctx;

        // rcMergePolyMeshes truncates vertex indices beyond 16 bits (and logs "Data can be corrupted").
        // Merging dedupes shared border vertices, so the sum is only an upper bound; stay quiet while
        // probing and check the merged result instead.
        if (vertexUpperBound >= Config.maxTileVertices)
        {
            ctx.enableLog(false);
        }

        rcPolyMesh* pmesh = rcAllocPolyMesh();
        rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();

        const bool merged = pmesh && dmesh && rcMergePolyMeshes(&ctx, pmeshes.data(), meshCount, *pmesh)
                            && rcMergePolyMeshDetails(&ctx, dmeshes.data(), meshCount, *dmesh);

        freeSubMeshes();

        const auto cleanup = [&]() {
            rcFreePolyMesh(pmesh);
            rcFreePolyMeshDetail(dmesh);
        };

        if (!merged)
        {
            cleanup();
            return vertexUpperBound >= Config.maxTileVertices ? BuildResult::TooDetailed : BuildResult::Failed;
        }

        if (pmesh->nverts > Config.maxTileVertices || pmesh->npolys >= 0xffff)
        {
            cleanup();
            return BuildResult::TooDetailed;
        }

        FinalizeMergedMesh(pmesh, tbbMin, tbbMax, p);

        dtNavMeshCreateParams params{};
        params.verts = pmesh->verts;
        params.vertCount = pmesh->nverts;
        params.polys = pmesh->polys;
        params.polyAreas = pmesh->areas;
        params.polyFlags = pmesh->flags;
        params.polyCount = pmesh->npolys;
        params.nvp = pmesh->nvp;
        params.detailMeshes = dmesh->meshes;
        params.detailVerts = dmesh->verts;
        params.detailVertsCount = dmesh->nverts;
        params.detailTris = dmesh->tris;
        params.detailTriCount = dmesh->ntris;
        rcVcopy(params.bmin, pmesh->bmin);
        rcVcopy(params.bmax, pmesh->bmax);
        params.tileX = tile.x;
        params.tileY = tile.y;
        params.tileLayer = 0;
        params.cs = p.cs;
        params.ch = p.ch;
        params.buildBvTree = true;
        params.walkableHeight = Config.agentHeight;
        params.walkableRadius = Config.agentRadius;
        params.walkableClimb = Config.agentClimb;

        unsigned char* navData = nullptr;
        int navDataSize = 0;
        const bool created = dtCreateNavMeshData(&params, &navData, &navDataSize);
        cleanup();

        if (!created)
        {
            return BuildResult::TooDetailed;
        }

        const bool stored = Writer->AddTile(navData, navDataSize);
        dtFree(navData);
        return stored ? BuildResult::Built : BuildResult::Failed;
    }

    /// Copy the sub-tile's terrain and water triangles into contiguous Recast index/area arrays.
    static void GatherSubTileTriangles(Structure& s, const Buckets& subBuckets, size_t subTile, Scratch& scratch)
    {
        scratch.terrainTris.clear();
        scratch.terrainAreas.clear();
        scratch.waterTris.clear();
        scratch.waterAreas.clear();

        for (const uint32_t* it = subBuckets.Begin(subTile); it != subBuckets.End(subTile); ++it)
        {
            const Tri& t = s.tris[*it];
            const unsigned char area = s.triTypes[*it];
            const bool water = IsLiquidArea(area);

            auto& tris = water ? scratch.waterTris : scratch.terrainTris;
            auto& areas = water ? scratch.waterAreas : scratch.terrainAreas;
            tris.insert(tris.end(), {t.a, t.b, t.c});
            areas.push_back(area);
        }
    }

    bool BuildSubTile(rcContext* ctx, Structure& s, const Scratch& scratch, const float* bmin, const float* bmax,
                      const Params& p, rcPolyMesh** pmeshOut, rcPolyMeshDetail** dmeshOut, int stX, int stY,
                      uint8_t* pixels, const WaterMap* waterMap, const RoadMap* roadMap,
                      const FactionMap* factionMap, const CityMap* cityMap) const noexcept
    {
        *pmeshOut = nullptr;
        *dmeshOut = nullptr;

        rcHeightfield* hf = nullptr;
        rcCompactHeightfield* chf = nullptr;
        rcContourSet* cset = nullptr;
        rcPolyMesh* pmesh = nullptr;
        rcPolyMeshDetail* dmesh = nullptr;

        const auto cleanup = [&](bool keepMeshes) {
            rcFreeHeightField(hf);
            rcFreeCompactHeightfield(chf);
            rcFreeContourSet(cset);

            if (!keepMeshes)
            {
                rcFreePolyMesh(pmesh);
                rcFreePolyMeshDetail(dmesh);
            }
        };

        const int nverts = static_cast<int>(s.verts.size());

        hf = rcAllocHeightfield();

        if (!hf || !rcCreateHeightfield(ctx, *hf, p.width, p.width, bmin, bmax, p.cs, p.ch))
        {
            cleanup(false);
            return false;
        }

        // Pass 1: terrain, then the terrain filters. Water isn't rasterized yet so the ledge/height
        // filters can't clear water surfaces.
        if (!scratch.terrainAreas.empty()
            && !rcRasterizeTriangles(ctx, s.Verts(), nverts, scratch.terrainTris.data(), scratch.terrainAreas.data(),
                                     static_cast<int>(scratch.terrainAreas.size()), *hf, p.walkableClimb))
        {
            cleanup(false);
            return false;
        }

        rcFilterLowHangingWalkableObstacles(ctx, p.walkableClimb, *hf);
        rcFilterLedgeSpans(ctx, p.walkableHeight, p.walkableClimb, *hf);
        rcFilterWalkableLowHeightSpans(ctx, p.walkableHeight, *hf);

        // Pass 2: water surfaces.
        if (!scratch.waterAreas.empty()
            && !rcRasterizeTriangles(ctx, s.Verts(), nverts, scratch.waterTris.data(), scratch.waterAreas.data(),
                                     static_cast<int>(scratch.waterAreas.size()), *hf, p.walkableClimb))
        {
            cleanup(false);
            return false;
        }

        chf = rcAllocCompactHeightfield();

        if (!chf || !rcBuildCompactHeightfield(ctx, p.walkableHeight, p.walkableClimb, *hf, *chf))
        {
            cleanup(false);
            return false;
        }

        rcFreeHeightField(hf);
        hf = nullptr;

        if (chf->spanCount == 0)
        {
            cleanup(false);
            return false;
        }

        if (!rcErodeWalkableArea(ctx, p.walkableRadius, *chf) || !rcMedianFilterWalkableArea(ctx, *chf))
        {
            cleanup(false);
            return false;
        }

        // Water first (restores shores cleared by erosion), roads before cities (roads in cities stay roads),
        // factions last (all base ids final).
        MarkWaterAreas(chf, waterMap);
        MarkRoadAreas(chf, roadMap);
        MarkCityAreas(chf, cityMap);
        MarkFactionAreas(chf, factionMap);

        if (pixels)
        {
            RenderSubTileToBmp(chf, pixels, Config.meshResolution, stX, stY, p.subTileSize, p.borderSize, p.width);
        }

        if (!rcBuildDistanceField(ctx, *chf)
            || !rcBuildRegions(ctx, *chf, p.borderSize, p.minRegionArea, p.mergeRegionArea))
        {
            cleanup(false);
            return false;
        }

        cset = rcAllocContourSet();

        if (!cset || !rcBuildContours(ctx, *chf, p.maxSimplificationError, p.maxEdgeLen, *cset) || cset->nconts == 0)
        {
            cleanup(false);
            return false;
        }

        pmesh = rcAllocPolyMesh();

        if (!pmesh || !rcBuildPolyMesh(ctx, *cset, DT_VERTS_PER_POLYGON, *pmesh))
        {
            cleanup(false);
            return false;
        }

        dmesh = rcAllocPolyMeshDetail();

        if (!dmesh || !rcBuildPolyMeshDetail(ctx, *pmesh, *chf, p.detailSampleDist, p.detailSampleMaxError, *dmesh))
        {
            cleanup(false);
            return false;
        }

        *pmeshOut = pmesh;
        *dmeshOut = dmesh;
        cleanup(true);
        return true;
    }

    /// Make the merged poly mesh a proper Detour tile: tile-relative vertex coordinates, exact tile bounds,
    /// portal flags on the tile border and poly flags from the area ids.
    static void FinalizeMergedMesh(rcPolyMesh* pmesh, const float* tbbMin, const float* tbbMax,
                                   const Params& p) noexcept
    {
        const int tw = static_cast<int>(std::lround(TILESIZE / p.cs));
        const int th = tw;

        // rcMergePolyMeshes stores vertices relative to the min corner of the *merged sub-meshes*. When the
        // first sub-tile row/column is empty that corner isn't the tile corner, shift the vertices so they
        // are relative to the tile bounds we set below (otherwise the whole tile would be misplaced).
        const int offsetX = static_cast<int>(std::lround((pmesh->bmin[0] - tbbMin[0]) / p.cs));
        const int offsetZ = static_cast<int>(std::lround((pmesh->bmin[2] - tbbMin[2]) / p.cs));

        // Snap vertices near the tile border onto it. Contour simplification can nudge border vertices
        // by up to maxSimplificationError cells, which would break the exact portal edge test below.
        const int snapDist = static_cast<int>(std::ceil(p.maxSimplificationError)) + 1;

        for (int i = 0; i < pmesh->nverts; ++i)
        {
            unsigned short* v = &pmesh->verts[i * 3];
            int x = static_cast<int>(v[0]) + offsetX;
            int z = static_cast<int>(v[2]) + offsetZ;

            if (x < snapDist)
                x = 0;
            else if (x > tw - snapDist)
                x = tw;

            if (z < snapDist)
                z = 0;
            else if (z > th - snapDist)
                z = th;

            v[0] = static_cast<unsigned short>(std::clamp(x, 0, 0xffff));
            v[2] = static_cast<unsigned short>(std::clamp(z, 0, 0xffff));
        }

        // Exact tile bounds (horizontal), Detour's neighbour linking relies on them.
        pmesh->bmin[0] = tbbMin[0];
        pmesh->bmin[2] = tbbMin[2];
        pmesh->bmax[0] = tbbMax[0];
        pmesh->bmax[2] = tbbMax[2];

        // rcMergePolyMeshes rebuilt the adjacency and dropped the sub-meshes' portal flags.
        // Unconnected edges on the tile border become portals so Detour can link neighbour tiles.
        for (int i = 0; i < pmesh->npolys; ++i)
        {
            unsigned short* poly = &pmesh->polys[i * 2 * pmesh->nvp];

            for (int j = 0; j < pmesh->nvp; ++j)
            {
                if (poly[j] == RC_MESH_NULL_IDX)
                    break;

                if (poly[pmesh->nvp + j] != RC_MESH_NULL_IDX)
                    continue;

                int nj = j + 1;

                if (nj >= pmesh->nvp || poly[nj] == RC_MESH_NULL_IDX)
                    nj = 0;

                const unsigned short* va = &pmesh->verts[poly[j] * 3];
                const unsigned short* vb = &pmesh->verts[poly[nj] * 3];

                if (va[0] == 0 && vb[0] == 0)
                    poly[pmesh->nvp + j] = 0x8000 | 0; // x-
                else if (va[2] == th && vb[2] == th)
                    poly[pmesh->nvp + j] = 0x8000 | 1; // z+
                else if (va[0] == tw && vb[0] == tw)
                    poly[pmesh->nvp + j] = 0x8000 | 2; // x+
                else if (va[2] == 0 && vb[2] == 0)
                    poly[pmesh->nvp + j] = 0x8000 | 3; // z-
            }
        }

        for (int i = 0; i < pmesh->npolys; ++i)
        {
            pmesh->flags[i] = AreaToPolyFlags(pmesh->areas[i] & RC_WALKABLE_AREA);
        }
    }
};
