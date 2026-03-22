#pragma once

#include <atomic>
#include <chrono>
#include <vector>

#include <omp.h>

#include <Utils/Logger.hpp>

#include "../../../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../../../recastnavigation/Detour/Include/DetourNavMeshBuilder.h"
#include "../../../recastnavigation/Recast/Include/Recast.h"

#include "../../../AmeisenNavigation.Pack/src/Anp.hpp"

#include "../Utils/CityMap.hpp"
#include "../Utils/FactionMap.hpp"
#include "../Utils/RoadMap.hpp"
#include "../Utils/Structure.hpp"
#include "../Utils/WaterMap.hpp"
#include "../Wow/AdtStructs.hpp"

#include "AreaMarker.hpp"
#include "BmpRenderer.hpp"

// ─────────────────────────────────────────────
// AdtTileProcessor - orchestrates the navmesh
// generation pipeline for a single ADT tile.
//
// Pipeline per sub-tile (two-pass rasterization):
//   1a. Rasterize TERRAIN triangles → heightfield
//   2.  Filter walkable spans (ledge, low-height, etc.)
//   1b. Rasterize WATER triangles → same heightfield (AFTER filters)
//   3.  Build compact heightfield
//   4.  Erode + median filter
//   5.  Mark water areas (AreaMarker - rect-to-cell)
//   6.  Mark road areas (AreaMarker)
//   6b. Mark city areas (AreaMarker - upgrades TERRAIN_GROUND → TERRAIN_CITY)
//   6c. Mark faction areas (AreaMarker - upgrades neutral → Alliance/Horde)
//   7.  Render to BMP (BmpRenderer, debug only)
//   8.  Build regions → contours → polymesh
//
// Water triangles are rasterized AFTER terrain filters
// because Recast's ledge/height filters are designed for
// terrain and incorrectly clear water surface spans (water
// doesn't have ledges - agents can swim at any depth).
//
// Parallelism strategy:
//   - Many tiles (full map): OpenMP parallel over tiles, sub-tiles sequential per thread
//   - Few tiles (single ADT debug): OpenMP parallel over sub-tiles within each tile
// ─────────────────────────────────────────────

/// Holds terrain-only and water-only triangle data, split from
/// the main Structure so they can be rasterized in separate passes.
struct SplitGeometry
{
    // Terrain triangles (indices into the shared vertex array)
    std::vector<int> terrainTris;
    std::vector<unsigned char> terrainAreas;

    // Water surface triangles (indices into the shared vertex array)
    std::vector<int> waterTris;
    std::vector<unsigned char> waterAreas;

    int TerrainTriCount() const noexcept { return static_cast<int>(terrainAreas.size()); }
    int WaterTriCount() const noexcept { return static_cast<int>(waterAreas.size()); }
};

/// Lightweight per-thread rcContext for parallel Recast operations.
/// Logging is disabled to avoid thread-safety issues with rcContext's internal buffer.
struct ThreadRcContext : rcContext
{
    ThreadRcContext() noexcept { enableLog(false); }
};

class AdtTileProcessor : public rcContext
{
    rcConfig RcCfg;
    Anp* Navmesh;
    std::string OutputDir;
    std::string MapName;
    int MapId;
    bool IsDebug;

public:
    AdtTileProcessor(Anp* anp, const std::string& outputDir, const std::string& mapName, bool isDebug) noexcept
        : Navmesh(anp), OutputDir(outputDir), MapName(mapName), MapId(anp->GetMapId()), IsDebug(isDebug), RcCfg{0}
    {
        enableLog(true);
        const int meshResolution = 2560;

        RcCfg.cs = TILESIZE / meshResolution;
        RcCfg.ch = TILESIZE / meshResolution;
        RcCfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;

        RcCfg.walkableSlopeAngle = 55.0f;
        RcCfg.walkableClimb = static_cast<int>(ceilf(1.2f / RcCfg.ch));
        RcCfg.walkableHeight = static_cast<int>(floorf(2.0f / RcCfg.ch));
        RcCfg.walkableRadius = static_cast<int>(ceilf(0.6f / RcCfg.cs));

        RcCfg.minRegionArea = static_cast<int>(rcSqr(10));
        RcCfg.mergeRegionArea = static_cast<int>(rcSqr(25));
        RcCfg.maxEdgeLen = static_cast<int>(12.0f / RcCfg.cs);
        RcCfg.maxSimplificationError = 1.0f;
        RcCfg.detailSampleDist = RcCfg.cs * 8.0f;
        RcCfg.detailSampleMaxError = RcCfg.ch * 0.5f;

        RcCfg.tileSize = 80;
        RcCfg.borderSize = RcCfg.walkableRadius + 3;
        RcCfg.width = RcCfg.tileSize + RcCfg.borderSize * 2;
        RcCfg.height = RcCfg.tileSize + RcCfg.borderSize * 2;
    }

    virtual void doLog(const rcLogCategory category, const char* msg, const int /*len*/) noexcept
    {
        switch (category)
        {
            case RC_LOG_WARNING: LogW("[Recast] ", msg); break;
            case RC_LOG_ERROR:   LogE("[Recast] ", msg); break;
            default:             LogD("[Recast] ", msg); break;
        }
    }

    // ── Main processing pipeline ──

    inline void Process(Structure* structure, const WaterMap* waterMap, const RoadMap* roadMap,
                        const FactionMap* factionMap = nullptr, const CityMap* cityMap = nullptr) noexcept
    {
        if (!structure || structure->verts.empty() || structure->tris.empty())
            return;

        // Clear steep triangles (sets area to RC_NULL_AREA for slopes > walkableSlopeAngle).
        // Water surface triangles are flat (slope ≈ 0°) and always survive this step.
        rcClearUnwalkableTriangles(this, RcCfg.walkableSlopeAngle, structure->Verts(),
                                   static_cast<int>(structure->verts.size()), structure->Tris(),
                                   static_cast<int>(structure->tris.size()), structure->AreaIds());

        // Split triangles into terrain-only and water-only arrays.
        // Water triangles are rasterized in a separate pass AFTER terrain filters
        // to prevent the ledge/height filters from clearing water surface spans.
        SplitGeometry split;
        {
            const int* allTris = structure->Tris();
            const unsigned char* allAreas = structure->AreaIds();
            const int triCount = static_cast<int>(structure->tris.size());

            // Pre-reserve to avoid reallocations during the split loop.
            // Most triangles are terrain; water is typically a small fraction.
            split.terrainTris.reserve(static_cast<size_t>(triCount) * 3);
            split.terrainAreas.reserve(triCount);
            split.waterTris.reserve(static_cast<size_t>(triCount / 4) * 3);
            split.waterAreas.reserve(triCount / 4);

            for (int i = 0; i < triCount; ++i)
            {
                unsigned char a = allAreas[i];
                if (a >= LIQUID_WATER && a <= HORDE_LIQUID_SLIME)
                {
                    split.waterTris.push_back(allTris[i * 3]);
                    split.waterTris.push_back(allTris[i * 3 + 1]);
                    split.waterTris.push_back(allTris[i * 3 + 2]);
                    split.waterAreas.push_back(a);
                }
                else
                {
                    split.terrainTris.push_back(allTris[i * 3]);
                    split.terrainTris.push_back(allTris[i * 3 + 1]);
                    split.terrainTris.push_back(allTris[i * 3 + 2]);
                    split.terrainAreas.push_back(a);
                }
            }

            if (IsDebug)
            {
                log(RC_LOG_PROGRESS, "Split geometry: %d terrain tris, %d water tris", split.TerrainTriCount(),
                    split.WaterTriCount());
            }
        }

        int width = 0;
        int height = 0;
        rcCalcGridSize(structure->bbMin, structure->bbMax, TILESIZE, &width, &height);

        const float borderPadding = RcCfg.borderSize * RcCfg.cs;
        const float subTileSize = RcCfg.tileSize * RcCfg.cs;

        const int tileCount = static_cast<int>(ceilf((TILESIZE / RcCfg.cs) / static_cast<float>(RcCfg.tileSize)));
        const int subTilesPerTile = tileCount * tileCount;
        const int totalTiles = width * height;

        // Debug BMP only for single-tile mode (multi-tile would overwrite the same pixel buffer)
        std::vector<uint8_t> adtPixels;
        if (IsDebug && totalTiles == 1)
        {
            adtPixels.assign(2560 * 2560 * 3, 0);
        }

        // Choose parallelism strategy based on tile count vs available threads.
        // Many tiles (full map): parallelize at tile level, sub-tiles sequential per thread.
        // Few tiles (single ADT): parallelize at sub-tile level within each tile.
        const int numThreads = omp_get_max_threads();
        const bool parallelTiles = (totalTiles >= numThreads);

        std::atomic<int> tilesCompleted{0};
        auto buildStart = std::chrono::high_resolution_clock::now();

        // Adaptive progress interval: ~20 updates total, at least every tile
        const int tileProgressInterval = std::max(1, totalTiles / 20);

        if (parallelTiles)
        {
            // ── Tile-level parallelism (full map export) ──
            // Each thread processes one tile at a time, running all its sub-tiles sequentially.
            // This avoids allocating millions of sub-meshes simultaneously.
            LogI(std::format("[{}] Building {} tiles using {} threads ({} sub-tiles each)",
                             MapName, totalTiles, numThreads, subTilesPerTile));

#pragma omp parallel
            {
                ThreadRcContext ctx;

#pragma omp for schedule(dynamic)
                for (int tIdx = 0; tIdx < totalTiles; ++tIdx)
                {
                    const int tX = tIdx % width;
                    const int tY = tIdx / width;

                    float tbbMin[3]{structure->bbMin[0] + tX * TILESIZE, structure->bbMin[1],
                                    structure->bbMin[2] + tY * TILESIZE};
                    float tbbMax[3]{tbbMin[0] + TILESIZE, structure->bbMax[1], tbbMin[2] + TILESIZE};

                    std::vector<rcPolyMesh*> spmeshes(subTilesPerTile, nullptr);
                    std::vector<rcPolyMeshDetail*> sdmeshes(subTilesPerTile, nullptr);
                    int meshIndex = 0;

                    for (int s = 0; s < subTilesPerTile; ++s)
                    {
                        const int stX = s % tileCount;
                        const int stY = s / tileCount;

                        float stbbMin[3]{(tbbMin[0] + stX * subTileSize) - borderPadding, structure->bbMin[1],
                                         (tbbMin[2] + stY * subTileSize) - borderPadding};
                        float stbbMax[3]{(tbbMin[0] + (stX + 1) * subTileSize) + borderPadding, structure->bbMax[1],
                                         (tbbMin[2] + (stY + 1) * subTileSize) + borderPadding};

                        if (BuildSubTile(&ctx, stbbMin, stbbMax, structure, &spmeshes[meshIndex], &sdmeshes[meshIndex],
                                         stX, stY, nullptr, waterMap, roadMap, factionMap, cityMap, split))
                        {
                            meshIndex++;
                        }
                    }

                    if (meshIndex > 0)
                    {
                        MergeAndAddTile(&ctx, spmeshes.data(), sdmeshes.data(), meshIndex, subTilesPerTile, tX, tY,
                                        tbbMin, tbbMax);
                    }

                    const int done = tilesCompleted.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (done % tileProgressInterval == 0 || done == totalTiles)
                    {
                        auto now = std::chrono::high_resolution_clock::now();
                        double elapsed = std::chrono::duration<double>(now - buildStart).count();
                        double eta = (elapsed / done) * (totalTiles - done);
                        LogP(std::format("[{}] Building navmesh: {} / {} tiles ({:.1f}%) - ETA: {}",
                                         MapName, done, totalTiles, 100.0 * done / totalTiles,
                                         Logger::FormatDuration(eta)));
                    }
                }
            }

            Logger::EndProgress();
        }
        else
        {
            // ── Sub-tile-level parallelism (single tile / debug export) ──
            // Few tiles: parallelize the sub-tile loop within each tile.
            LogI(std::format("[{}] Building {} tiles ({} sub-tiles each, sub-tile parallel)",
                             MapName, totalTiles, subTilesPerTile));

            for (int tIdx = 0; tIdx < totalTiles; ++tIdx)
            {
                const int tX = tIdx % width;
                const int tY = tIdx / width;

                float tbbMin[3]{structure->bbMin[0] + tX * TILESIZE, structure->bbMin[1],
                                structure->bbMin[2] + tY * TILESIZE};
                float tbbMax[3]{tbbMin[0] + TILESIZE, structure->bbMax[1], tbbMin[2] + TILESIZE};

                std::vector<rcPolyMesh*> spmeshes(subTilesPerTile, nullptr);
                std::vector<rcPolyMeshDetail*> sdmeshes(subTilesPerTile, nullptr);

                std::atomic<int> subTilesCompleted{0};
                auto tileStart = std::chrono::high_resolution_clock::now();
                const int subProgressInterval = std::max(1, subTilesPerTile / 20);

                // Each sub-tile writes to its own slot [s] - no contention.
                // BMP rendering also writes to non-overlapping pixel regions per (stX, stY).
#pragma omp parallel
                {
                    ThreadRcContext ctx;

#pragma omp for schedule(dynamic)
                    for (int s = 0; s < subTilesPerTile; ++s)
                    {
                        const int stX = s % tileCount;
                        const int stY = s / tileCount;

                        float stbbMin[3]{(tbbMin[0] + stX * subTileSize) - borderPadding, structure->bbMin[1],
                                         (tbbMin[2] + stY * subTileSize) - borderPadding};
                        float stbbMax[3]{(tbbMin[0] + (stX + 1) * subTileSize) + borderPadding, structure->bbMax[1],
                                         (tbbMin[2] + (stY + 1) * subTileSize) + borderPadding};

                        BuildSubTile(&ctx, stbbMin, stbbMax, structure, &spmeshes[s], &sdmeshes[s], stX, stY,
                                     adtPixels.empty() ? nullptr : adtPixels.data(), waterMap, roadMap, factionMap,
                                     cityMap, split);

                        const int done = subTilesCompleted.fetch_add(1, std::memory_order_relaxed) + 1;
                        if (done % subProgressInterval == 0 || done == subTilesPerTile)
                        {
                            auto now = std::chrono::high_resolution_clock::now();
                            double elapsed = std::chrono::duration<double>(now - tileStart).count();
                            double eta = (elapsed / done) * (subTilesPerTile - done);
                            LogP(std::format("[{}] Tile {}/{}: sub-tiles {} / {} ({:.1f}%) - ETA: {}",
                                             MapName, tIdx + 1, totalTiles, done, subTilesPerTile,
                                             100.0 * done / subTilesPerTile, Logger::FormatDuration(eta)));
                        }
                    }
                }

                Logger::EndProgress();

                // Compact: pack non-null meshes to the front for merge
                int meshIndex = 0;
                for (int i = 0; i < subTilesPerTile; ++i)
                {
                    if (spmeshes[i])
                    {
                        if (meshIndex != i)
                        {
                            spmeshes[meshIndex] = spmeshes[i];
                            sdmeshes[meshIndex] = sdmeshes[i];
                            spmeshes[i] = nullptr;
                            sdmeshes[i] = nullptr;
                        }
                        meshIndex++;
                    }
                }

                if (meshIndex > 0)
                {
                    MergeAndAddTile(this, spmeshes.data(), sdmeshes.data(), meshIndex, subTilesPerTile, tX, tY,
                                    tbbMin, tbbMax);
                }
            }
        }

        {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - buildStart).count();
            LogS(std::format("[{}] Built {} tiles ({} sub-tiles each) in {}",
                             MapName, totalTiles, subTilesPerTile, Logger::FormatDuration(elapsed)));
        }

        if (IsDebug && !adtPixels.empty())
        {
            SaveDebugBmp(OutputDir, MapId, structure->bbMax, adtPixels.data());
        }
    }

private:
    // ── Sub-tile navmesh building (two-pass rasterization) ──
    //
    // Flattened with goto cleanup to ensure all Recast allocations
    // are freed on every exit path (success or failure).
    // Takes explicit rcContext* for thread-safe parallel execution.

    inline bool BuildSubTile(rcContext* ctx, float* bbMin, float* bbMax, Structure* structure, rcPolyMesh** pmesh,
                             rcPolyMeshDetail** dmesh, int stX, int stY, uint8_t* adtPixels, const WaterMap* waterMap,
                             const RoadMap* roadMap, const FactionMap* factionMap, const CityMap* cityMap,
                             const SplitGeometry& split) noexcept
    {
        *pmesh = nullptr;
        *dmesh = nullptr;

        // ── Allocate heightfield and rasterize ──
        rcHeightfield* heightField = rcAllocHeightfield();
        if (!heightField)
            return false;

        if (!rcCreateHeightfield(ctx, *heightField, RcCfg.width, RcCfg.height, bbMin, bbMax, RcCfg.cs, RcCfg.ch))
        {
            rcFreeHeightField(heightField);
            return false;
        }

        // Pass 1: Rasterize terrain-only triangles
        if (split.TerrainTriCount() > 0)
        {
            rcRasterizeTriangles(ctx, structure->Verts(), static_cast<int>(structure->verts.size()),
                                 split.terrainTris.data(), split.terrainAreas.data(), split.TerrainTriCount(),
                                 *heightField, RcCfg.walkableClimb);
        }

        // Terrain filters - only see terrain spans. Water hasn't been rasterized yet,
        // so ledge/height filters can't incorrectly clear water surfaces.
        rcFilterLowHangingWalkableObstacles(ctx, RcCfg.walkableClimb, *heightField);
        rcFilterLedgeSpans(ctx, RcCfg.walkableHeight, RcCfg.walkableClimb, *heightField);
        rcFilterWalkableLowHeightSpans(ctx, RcCfg.walkableHeight, *heightField);

        // Pass 2: Rasterize water-only triangles (AFTER filters).
        // Water surface spans are added to the already-filtered heightfield.
        if (split.WaterTriCount() > 0)
        {
            rcRasterizeTriangles(ctx, structure->Verts(), static_cast<int>(structure->verts.size()),
                                 split.waterTris.data(), split.waterAreas.data(), split.WaterTriCount(),
                                 *heightField, RcCfg.walkableClimb);
        }

        // ── Build compact heightfield ──
        rcCompactHeightfield* chf = rcAllocCompactHeightfield();
        if (!chf)
        {
            rcFreeHeightField(heightField);
            return false;
        }

        if (!rcBuildCompactHeightfield(ctx, RcCfg.walkableHeight, RcCfg.walkableClimb, *heightField, *chf))
        {
            rcFreeCompactHeightfield(chf);
            rcFreeHeightField(heightField);
            return false;
        }

        // Heightfield no longer needed after compaction
        rcFreeHeightField(heightField);

        // ── Erode + median filter ──
        if (!rcErodeWalkableArea(ctx, RcCfg.walkableRadius, *chf))
        {
            rcFreeCompactHeightfield(chf);
            return false;
        }

        if (!rcMedianFilterWalkableArea(ctx, *chf))
        {
            rcFreeCompactHeightfield(chf);
            return false;
        }

        // Mark water areas (rect-to-cell direct mapping).
        // Restores water areas cleared by erosion/median at boundaries.
        MarkWaterAreas(chf, waterMap, stX, stY, IsDebug, ctx);

        // Mark road areas
        MarkRoadAreas(chf, roadMap);

        // Mark city areas (upgrades TERRAIN_GROUND → TERRAIN_CITY within city bounds).
        // Runs after roads so that roads within cities stay TERRAIN_ROAD.
        MarkCityAreas(chf, cityMap);

        // Mark faction areas (upgrades neutral base IDs to Alliance/Horde variants).
        // Must run after water, road, and city marking so all base area IDs are finalized.
        MarkFactionAreas(chf, factionMap);

        // Render to BMP (debug only)
        if (IsDebug)
        {
            RenderSubTileToBmp(chf, adtPixels, stX, stY, RcCfg.tileSize, RcCfg.borderSize, RcCfg.width);
        }

        // ── Build regions → contours → polymesh ──
        if (!rcBuildDistanceField(ctx, *chf))
        {
            rcFreeCompactHeightfield(chf);
            return false;
        }

        if (!rcBuildRegions(ctx, *chf, RcCfg.borderSize, RcCfg.minRegionArea, RcCfg.mergeRegionArea))
        {
            rcFreeCompactHeightfield(chf);
            return false;
        }

        rcContourSet* contourSet = rcAllocContourSet();
        if (!contourSet)
        {
            rcFreeCompactHeightfield(chf);
            return false;
        }

        if (!rcBuildContours(ctx, *chf, RcCfg.maxSimplificationError, RcCfg.maxEdgeLen, *contourSet))
        {
            rcFreeContourSet(contourSet);
            rcFreeCompactHeightfield(chf);
            return false;
        }

        // ── Build poly mesh ──
        *pmesh = rcAllocPolyMesh();
        if (!*pmesh)
        {
            rcFreeContourSet(contourSet);
            rcFreeCompactHeightfield(chf);
            return false;
        }

        if (!rcBuildPolyMesh(ctx, *contourSet, RcCfg.maxVertsPerPoly, **pmesh))
        {
            rcFreePolyMesh(*pmesh);
            *pmesh = nullptr;
            rcFreeContourSet(contourSet);
            rcFreeCompactHeightfield(chf);
            return false;
        }

        // Contour set no longer needed
        rcFreeContourSet(contourSet);

        // ── Build detail mesh ──
        *dmesh = rcAllocPolyMeshDetail();
        if (!*dmesh)
        {
            rcFreePolyMesh(*pmesh);
            *pmesh = nullptr;
            rcFreeCompactHeightfield(chf);
            return false;
        }

        if (!rcBuildPolyMeshDetail(ctx, **pmesh, *chf, RcCfg.detailSampleDist, RcCfg.detailSampleMaxError, **dmesh))
        {
            rcFreePolyMeshDetail(*dmesh);
            *dmesh = nullptr;
            rcFreePolyMesh(*pmesh);
            *pmesh = nullptr;
            rcFreeCompactHeightfield(chf);
            return false;
        }

        rcFreeCompactHeightfield(chf);
        return true;
    }

    // ── Merge sub-tiles and add to navmesh ──

    inline void MergeAndAddTile(rcContext* ctx, rcPolyMesh** spmeshes, rcPolyMeshDetail** sdmeshes, int meshIndex,
                                int totalTileCount, int tX, int tY, const float* tbbMin, const float* tbbMax) noexcept
    {
        rcPolyMesh* pmesh = rcAllocPolyMesh();
        rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();

        bool merged = rcMergePolyMeshes(ctx, spmeshes, meshIndex, *pmesh) &&
                      rcMergePolyMeshDetails(ctx, sdmeshes, meshIndex, *dmesh);

        // Always free sub-tile meshes - whether merge succeeded or not
        for (int i = 0; i < totalTileCount; i++)
        {
            rcFreePolyMesh(spmeshes[i]);
            spmeshes[i] = nullptr;
            rcFreePolyMeshDetail(sdmeshes[i]);
            sdmeshes[i] = nullptr;
        }

        if (merged)
        {
            // ── Snap boundary vertices & detect portal edges ──
            //
            // Recast already subtracts borderSize from contour vertex coords
            // (RecastContour.cpp line 976), so after rcMergePolyMeshes the
            // merged vertices are in contour-set space: 0..tileSize per
            // sub-tile, offset by the merge's (ox,oz) to 0..tw for the full
            // tile.  No additional shift is needed.
            //
            // However, rcMergePolyMeshes calls buildMeshAdjacency which
            // re-initialises all adjacency to 0xffff, wiping any portal
            // flags that rcBuildPolyMesh set on individual sub-tiles.
            // We must re-detect portal edges on the merged mesh.
            const int tw = static_cast<int>(roundf(TILESIZE / RcCfg.cs));
            const int th = tw;

            // Snap vertices near tile boundaries to exact edge positions.
            // Contour simplification can nudge boundary vertices by up to
            // maxSimplificationError cells; without snapping the exact-
            // equality portal check would miss them.
            const int snapDist = static_cast<int>(ceilf(RcCfg.maxSimplificationError)) + 1;

            for (int i = 0; i < pmesh->nverts; ++i)
            {
                int x = static_cast<int>(pmesh->verts[i * 3 + 0]);
                int z = static_cast<int>(pmesh->verts[i * 3 + 2]);

                if (x < snapDist) x = 0;
                else if (x > tw - snapDist) x = tw;

                if (z < snapDist) z = 0;
                else if (z > th - snapDist) z = th;

                pmesh->verts[i * 3 + 0] = static_cast<unsigned short>(x);
                pmesh->verts[i * 3 + 2] = static_cast<unsigned short>(z);
            }

            // Align bounds to the tile grid (horizontal only; keep Y from mesh).
            // Needed for Detour neighbour lookup which relies on exact bmin/bmax.
            pmesh->bmin[0] = tbbMin[0];
            pmesh->bmin[2] = tbbMin[2];
            pmesh->bmax[0] = tbbMax[0];
            pmesh->bmax[2] = tbbMax[2];

            // Mark portal edges: unconnected edges on the tile boundary get
            // portal flags so Detour can link adjacent tiles at runtime.
            for (int i = 0; i < pmesh->npolys; ++i)
            {
                unsigned short* p = &pmesh->polys[i * 2 * pmesh->nvp];
                for (int j = 0; j < pmesh->nvp; ++j)
                {
                    if (p[j] == RC_MESH_NULL_IDX) break;
                    if (p[pmesh->nvp + j] != RC_MESH_NULL_IDX) continue;

                    int nj = j + 1;
                    if (nj >= pmesh->nvp || p[nj] == RC_MESH_NULL_IDX) nj = 0;

                    const unsigned short* va = &pmesh->verts[p[j] * 3];
                    const unsigned short* vb = &pmesh->verts[p[nj] * 3];

                    if      (va[0] == 0  && vb[0] == 0)  p[pmesh->nvp + j] = 0x8000 | 0; // Portal x-
                    else if (va[2] == th && vb[2] == th)  p[pmesh->nvp + j] = 0x8000 | 1; // Portal z+
                    else if (va[0] == tw && vb[0] == tw)  p[pmesh->nvp + j] = 0x8000 | 2; // Portal x+
                    else if (va[2] == 0  && vb[2] == 0)   p[pmesh->nvp + j] = 0x8000 | 3; // Portal z-
                }
            }

            AssignPolyFlags(pmesh);

            dtNavMeshCreateParams params{0};
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

            params.tileX = tX;
            params.tileY = tY;
            params.cs = RcCfg.cs;
            params.ch = RcCfg.ch;
            params.tileLayer = 0;
            params.buildBvTree = true;

            params.walkableHeight = 2.0f;
            params.walkableRadius = 0.6f;
            params.walkableClimb = 1.2f;

            unsigned char* navData = nullptr;
            int navDataSize = 0;
            dtTileRef tile;

            if (dtCreateNavMeshData(&params, &navData, &navDataSize))
            {
                if (dtStatusFailed(Navmesh->AddTile(params.tileX, params.tileY, navData, navDataSize, &tile)))
                {
                    dtFree(navData);
                }
            }
        }

        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
    }

    // ── Assign navmesh poly flags from area IDs ──

    inline void AssignPolyFlags(rcPolyMesh* pmesh) noexcept
    {
        for (int p = 0; p < pmesh->npolys; ++p)
        {
            if (TriAreaId area = static_cast<TriAreaId>(pmesh->areas[p] & 63))
            {
                switch (area)
                {
                    case LIQUID_LAVA:
                    case LIQUID_SLIME:
                        pmesh->flags[p] = NAV_LAVA_SLIME;
                        break;
                    case ALLIANCE_LIQUID_SLIME:
                    case ALLIANCE_LIQUID_LAVA:
                        pmesh->flags[p] = NAV_LAVA_SLIME | NAV_ALLIANCE;
                        break;
                    case HORDE_LIQUID_LAVA:
                    case HORDE_LIQUID_SLIME:
                        pmesh->flags[p] = NAV_LAVA_SLIME | NAV_HORDE;
                        break;
                    case LIQUID_WATER:
                    case LIQUID_OCEAN:
                        pmesh->flags[p] = NAV_WATER;
                        break;
                    case ALLIANCE_LIQUID_WATER:
                    case ALLIANCE_LIQUID_OCEAN:
                        pmesh->flags[p] = NAV_WATER | NAV_ALLIANCE;
                        break;
                    case HORDE_LIQUID_WATER:
                    case HORDE_LIQUID_OCEAN:
                        pmesh->flags[p] = NAV_WATER | NAV_HORDE;
                        break;
                    case TERRAIN_GROUND:
                    case TERRAIN_CITY:
                    case WMO:
                    case DOODAD:
                        pmesh->flags[p] = NAV_GROUND;
                        break;
                    case TERRAIN_ROAD:
                        pmesh->flags[p] = NAV_GROUND | NAV_ROAD;
                        break;
                    case ALLIANCE_TERRAIN_GROUND:
                    case ALLIANCE_TERRAIN_CITY:
                    case ALLIANCE_WMO:
                    case ALLIANCE_DOODAD:
                        pmesh->flags[p] = NAV_GROUND | NAV_ALLIANCE;
                        break;
                    case ALLIANCE_TERRAIN_ROAD:
                        pmesh->flags[p] = NAV_GROUND | NAV_ROAD | NAV_ALLIANCE;
                        break;
                    case HORDE_TERRAIN_GROUND:
                    case HORDE_TERRAIN_CITY:
                    case HORDE_WMO:
                    case HORDE_DOODAD:
                        pmesh->flags[p] = NAV_GROUND | NAV_HORDE;
                        break;
                    case HORDE_TERRAIN_ROAD:
                        pmesh->flags[p] = NAV_GROUND | NAV_ROAD | NAV_HORDE;
                        break;
                    default:
                        break;
                }
            }
        }
    }
};
