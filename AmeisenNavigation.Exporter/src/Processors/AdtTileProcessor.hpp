#pragma once

#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

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
// AdtTileProcessor — orchestrates the navmesh
// generation pipeline for a single ADT tile.
//
// Pipeline per sub-tile (two-pass rasterization):
//   1a. Rasterize TERRAIN triangles → heightfield
//   2.  Filter walkable spans (ledge, low-height, etc.)
//   1b. Rasterize WATER triangles → same heightfield (AFTER filters)
//   3.  Build compact heightfield
//   4.  Erode + median filter
//   5.  Mark water areas (AreaMarker — rect-to-cell)
//   6.  Mark road areas (AreaMarker)
//   6b. Mark city areas (AreaMarker — upgrades TERRAIN_GROUND → TERRAIN_CITY)
//   6c. Mark faction areas (AreaMarker — upgrades neutral → Alliance/Horde)
//   7.  Render to BMP (BmpRenderer, debug only)
//   8.  Build regions → contours → polymesh
//
// Water triangles are rasterized AFTER terrain filters
// because Recast's ledge/height filters are designed for
// terrain and incorrectly clear water surface spans (water
// doesn't have ledges — agents can swim at any depth).
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

class AdtTileProcessor : public rcContext
{
    std::thread Thread;
    std::mutex Mutex;
    std::condition_variable CondVar;
    std::deque<Structure*> Queue;
    rcConfig RcCfg;
    Anp* Navmesh;
    std::string OutputDir;
    int MapId;
    bool IsActive;
    bool IsDebug;

public:
    AdtTileProcessor(Anp* anp, const std::string& outputDir, bool isDebug) noexcept
        : Navmesh(anp), OutputDir(outputDir), MapId(anp->GetMapId()), IsActive(false), IsDebug(isDebug), RcCfg{0}
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

    virtual void doLog(const rcLogCategory category, const char* msg, const int len) noexcept
    {
        std::cout << "[" << category << "]: " << msg << std::endl;
    }

    inline void Start() noexcept
    {
        IsActive = true;
        Thread = std::thread(&AdtTileProcessor::Run, this);
    }

    inline void Stop() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(Mutex);
            IsActive = false;
        }
        CondVar.notify_one();
        Thread.join();
    }

    inline void Run() noexcept
    {
        while (true)
        {
            std::unique_lock<std::mutex> lock(Mutex);
            CondVar.wait(lock, [this] { return !Queue.empty() || !IsActive; });

            if (!IsActive)
                break;

            Structure* structure = Queue.front();
            Queue.pop_front();
            lock.unlock();

            Process(structure, nullptr, nullptr);
        }
    }

    inline void Add(Structure* structure) noexcept
    {
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Queue.push_back(structure);
        }
        CondVar.notify_one();
    }

    // ── Main processing pipeline ──

    inline void Process(Structure* structure, const WaterMap* waterMap, const RoadMap* roadMap,
                        const FactionMap* factionMap = nullptr, const CityMap* cityMap = nullptr) noexcept
    {
        if (!structure)
            return;

        // Clear steep triangles (sets area to RC_NULL_AREA for slopes > walkableSlopeAngle).
        // Water surface triangles are flat (slope ≈ 0°) and always survive this step.
        rcClearUnwalkableTriangles(this, RcCfg.walkableSlopeAngle, structure->Verts(), structure->verts.size(),
                                   structure->Tris(), structure->tris.size(), structure->AreaIds());

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
        const int totalTileCount = tileCount * tileCount;

        std::vector<rcPolyMesh*> spmeshes(totalTileCount, nullptr);
        std::vector<rcPolyMeshDetail*> sdmeshes(totalTileCount, nullptr);

        std::vector<uint8_t> adtPixels;
        if (IsDebug)
        {
            adtPixels.assign(2560 * 2560 * 3, 0);
        }

        for (int tX = 0; tX < width; ++tX)
        {
            for (int tY = 0; tY < height; ++tY)
            {
                float tbbMin[3]{structure->bbMin[0] + tX * TILESIZE, structure->bbMin[1],
                                structure->bbMin[2] + tY * TILESIZE};

                float tbbMax[3]{tbbMin[0] + TILESIZE, structure->bbMax[1], tbbMin[2] + TILESIZE};

                int meshIndex = 0;

                for (int stX = 0; stX < tileCount; ++stX)
                {
                    for (int stY = 0; stY < tileCount; ++stY)
                    {
                        float stbbMin[3]{(tbbMin[0] + stX * subTileSize) - borderPadding, structure->bbMin[1],
                                         (tbbMin[2] + stY * subTileSize) - borderPadding};

                        float stbbMax[3]{(tbbMin[0] + (stX + 1) * subTileSize) + borderPadding, structure->bbMax[1],
                                         (tbbMin[2] + (stY + 1) * subTileSize) + borderPadding};

                        if (BuildSubTile(stbbMin, stbbMax, structure, &spmeshes[meshIndex], &sdmeshes[meshIndex], stX,
                                         stY, adtPixels.empty() ? nullptr : adtPixels.data(), waterMap, roadMap,
                                         factionMap, cityMap, split))
                        {
                            meshIndex++;
                        }
                    }
                }

                // Merge sub-tile meshes and add to navmesh
                MergeAndAddTile(spmeshes.data(), sdmeshes.data(), meshIndex, totalTileCount, tbbMin, tbbMax, tX, tY);
            }
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

    inline bool BuildSubTile(float* bbMin, float* bbMax, Structure* structure, rcPolyMesh** pmesh,
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

        if (!rcCreateHeightfield(this, *heightField, RcCfg.width, RcCfg.height, bbMin, bbMax, RcCfg.cs, RcCfg.ch))
        {
            rcFreeHeightField(heightField);
            return false;
        }

        // Pass 1: Rasterize terrain-only triangles
        if (split.TerrainTriCount() > 0)
        {
            rcRasterizeTriangles(this, structure->Verts(), structure->verts.size(), split.terrainTris.data(),
                                 split.terrainAreas.data(), split.TerrainTriCount(), *heightField,
                                 RcCfg.walkableClimb);
        }

        // Terrain filters — only see terrain spans. Water hasn't been rasterized yet,
        // so ledge/height filters can't incorrectly clear water surfaces.
        rcFilterLowHangingWalkableObstacles(this, RcCfg.walkableClimb, *heightField);
        rcFilterLedgeSpans(this, RcCfg.walkableHeight, RcCfg.walkableClimb, *heightField);
        rcFilterWalkableLowHeightSpans(this, RcCfg.walkableHeight, *heightField);

        // Pass 2: Rasterize water-only triangles (AFTER filters).
        // Water surface spans are added to the already-filtered heightfield.
        if (split.WaterTriCount() > 0)
        {
            rcRasterizeTriangles(this, structure->Verts(), structure->verts.size(), split.waterTris.data(),
                                 split.waterAreas.data(), split.WaterTriCount(), *heightField,
                                 RcCfg.walkableClimb);
        }

        // ── Build compact heightfield ──
        rcCompactHeightfield* chf = rcAllocCompactHeightfield();
        if (!chf)
        {
            rcFreeHeightField(heightField);
            return false;
        }

        if (!rcBuildCompactHeightfield(this, RcCfg.walkableHeight, RcCfg.walkableClimb, *heightField, *chf))
        {
            rcFreeCompactHeightfield(chf);
            rcFreeHeightField(heightField);
            return false;
        }

        // Heightfield no longer needed after compaction
        rcFreeHeightField(heightField);

        // ── Erode + median filter ──
        if (!rcErodeWalkableArea(this, RcCfg.walkableRadius, *chf))
        {
            rcFreeCompactHeightfield(chf);
            return false;
        }

        if (!rcMedianFilterWalkableArea(this, *chf))
        {
            rcFreeCompactHeightfield(chf);
            return false;
        }

        // Mark water areas (rect-to-cell direct mapping).
        // Restores water areas cleared by erosion/median at boundaries.
        MarkWaterAreas(chf, waterMap, stX, stY, IsDebug, this);

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
        if (!rcBuildDistanceField(this, *chf))
        {
            rcFreeCompactHeightfield(chf);
            return false;
        }

        if (!rcBuildRegions(this, *chf, RcCfg.borderSize, RcCfg.minRegionArea, RcCfg.mergeRegionArea))
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

        if (!rcBuildContours(this, *chf, RcCfg.maxSimplificationError, RcCfg.maxEdgeLen, *contourSet))
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

        if (!rcBuildPolyMesh(this, *contourSet, RcCfg.maxVertsPerPoly, **pmesh))
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

        if (!rcBuildPolyMeshDetail(this, **pmesh, *chf, RcCfg.detailSampleDist, RcCfg.detailSampleMaxError, **dmesh))
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

    inline void MergeAndAddTile(rcPolyMesh** spmeshes, rcPolyMeshDetail** sdmeshes, int meshIndex, int totalTileCount,
                                float* tbbMin, float* tbbMax, int tX, int tY) noexcept
    {
        rcPolyMesh* pmesh = rcAllocPolyMesh();
        rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();

        if (rcMergePolyMeshes(this, spmeshes, meshIndex, *pmesh) &&
            rcMergePolyMeshDetails(this, sdmeshes, meshIndex, *dmesh))
        {
            for (int i = 0; i < totalTileCount; i++)
            {
                rcFreePolyMesh(spmeshes[i]);
                spmeshes[i] = nullptr;
                rcFreePolyMeshDetail(sdmeshes[i]);
                sdmeshes[i] = nullptr;
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
            rcVcopy(params.bmin, tbbMin);
            rcVcopy(params.bmax, tbbMax);
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
