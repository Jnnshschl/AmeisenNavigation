#pragma once

#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

#include "../../../recastnavigation/Recast/Include/Recast.h"
#include "../../../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../../../recastnavigation/Detour/Include/DetourNavMeshBuilder.h"
#include "../../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "../../../AmeisenNavigation.Pack/src/Anp.hpp"

#include "../Utils/Structure.hpp"
#include "../Wow/Adt.hpp"

class AdtTileProcessor : public rcContext
{
    std::thread Thread;
    std::mutex Mutex;
    std::condition_variable CondVar;
    std::deque<Structure*> Queue;
    rcConfig RcCfg;
    Anp* Navmesh;
    bool IsActive;

public:
    AdtTileProcessor(Anp* anp) noexcept
        : Navmesh(anp),
        RcCfg{ 0 },
        IsActive(false)
    {
        enableLog(true);
        const int meshResolution = 2000;

        RcCfg.cs = TILESIZE / meshResolution;
        RcCfg.ch = TILESIZE / meshResolution;
        RcCfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;

        RcCfg.walkableSlopeAngle = 55.0f;
        RcCfg.walkableClimb = static_cast<int>(ceilf(1.0f / RcCfg.ch));
        RcCfg.walkableHeight = static_cast<int>(floorf(2.0f / RcCfg.ch));
        RcCfg.walkableRadius = static_cast<int>(ceilf(1.0f / RcCfg.cs));

        RcCfg.minRegionArea = static_cast<int>(rcSqr(8));
        RcCfg.mergeRegionArea = static_cast<int>(rcSqr(20));
        RcCfg.maxEdgeLen = static_cast<int>(12.0f / RcCfg.cs);
        RcCfg.maxSimplificationError = 1.3f;
        RcCfg.detailSampleDist = RcCfg.cs * 6.0f;
        RcCfg.detailSampleMaxError = RcCfg.ch * 1.0f;

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
            {
                break;
            }

            Structure* structure = Queue.front();
            Queue.pop_front();
            lock.unlock();

            Process(structure);
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

    inline void Process(Structure* structure) noexcept
    {
        if (structure)
        {
            rcClearUnwalkableTriangles(this, RcCfg.walkableSlopeAngle, structure->Verts(), structure->verts.size(), structure->Tris(), structure->tris.size(), structure->AreaIds());

            int width = 0;
            int height = 0;
            rcCalcGridSize(structure->bbMin, structure->bbMax, TILESIZE, &width, &height);

            const float borderPadding = RcCfg.borderSize * RcCfg.cs;
            const float subTileSize = RcCfg.tileSize * RcCfg.cs;

            const int tileCount = static_cast<int>(ceilf((TILESIZE / RcCfg.cs) / static_cast<float>(RcCfg.tileSize)));
            const int totalTileCount = tileCount * tileCount;

            rcPolyMesh** spmeshes = new rcPolyMesh * [totalTileCount];
            rcPolyMeshDetail** sdmeshes = new rcPolyMeshDetail * [totalTileCount];

            for (int tX = 0; tX < width; ++tX)
            {
                for (int tY = 0; tY < height; ++tY)
                {
                    float tbbMin[3]
                    {
                        structure->bbMin[0] + tX * TILESIZE,
                        structure->bbMin[1],
                        structure->bbMin[2] + tY * TILESIZE
                    };

                    float tbbMax[3]
                    {
                        tbbMin[0] + TILESIZE,
                        structure->bbMax[1],
                        tbbMin[2] + TILESIZE
                    };

                    int meshIndex = 0;

                    for (int stX = 0; stX < tileCount; ++stX)
                    {
                        for (int stY = 0; stY < tileCount; ++stY)
                        {
                            float stbbMin[3]
                            {
                                (tbbMin[0] + stX * subTileSize) - borderPadding,
                                structure->bbMin[1],
                                (tbbMin[2] + stY * subTileSize) - borderPadding
                            };

                            float stbbMax[3]
                            {
                                (tbbMin[0] + (stX + 1) * subTileSize) + borderPadding,
                                structure->bbMax[1],
                                (tbbMin[2] + (stY + 1) * subTileSize) + borderPadding
                            };

                            if (GenerateNavmeshTile(stbbMin, stbbMax, structure, &spmeshes[meshIndex], &sdmeshes[meshIndex]))
                            {
                                meshIndex++;
                            }
                        }
                    }

                    rcPolyMesh* pmesh = rcAllocPolyMesh();
                    rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();

                    if (rcMergePolyMeshes(this, spmeshes, meshIndex, *pmesh)
                        && rcMergePolyMeshDetails(this, sdmeshes, meshIndex, *dmesh))
                    {
                        for (int i = 0; i < totalTileCount; i++)
                        {
                            rcFreePolyMesh(spmeshes[i]);
                            spmeshes[i] = nullptr;
                            rcFreePolyMeshDetail(sdmeshes[i]);
                            sdmeshes[i] = nullptr;
                        }

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
                                    pmesh->flags[p] = NAV_LAVA_SLIME & NAV_ALLIANCE;
                                    break;

                                case HORDE_LIQUID_LAVA:
                                case HORDE_LIQUID_SLIME:
                                    pmesh->flags[p] = NAV_LAVA_SLIME & NAV_HORDE;
                                    break;

                                case LIQUID_WATER:
                                case LIQUID_OCEAN:
                                    pmesh->flags[p] = NAV_WATER;
                                    break;

                                case ALLIANCE_LIQUID_WATER:
                                case ALLIANCE_LIQUID_OCEAN:
                                    pmesh->flags[p] = NAV_WATER & NAV_ALLIANCE;
                                    break;

                                case HORDE_LIQUID_WATER:
                                case HORDE_LIQUID_OCEAN:
                                    pmesh->flags[p] = NAV_WATER & NAV_HORDE;
                                    break;

                                case TERRAIN_GROUND:
                                case WMO:
                                case DOODAD:
                                    pmesh->flags[p] = NAV_GROUND;
                                    break;

                                case ALLIANCE_TERRAIN_GROUND:
                                case ALLIANCE_WMO:
                                case ALLIANCE_DOODAD:
                                    pmesh->flags[p] = NAV_GROUND & NAV_ALLIANCE;
                                    break;

                                case HORDE_TERRAIN_GROUND:
                                case HORDE_WMO:
                                case HORDE_DOODAD:
                                    pmesh->flags[p] = NAV_GROUND & NAV_HORDE;
                                    break;

                                default:
                                    break;
                                }
                            }
                        }

                        dtNavMeshCreateParams params{ 0 };
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

                        params.walkableHeight = TILESIZE / 250;
                        params.walkableRadius = TILESIZE / 2000;
                        params.walkableClimb = TILESIZE / 750;

                        unsigned char* navData = nullptr;
                        int navDataSize = 0;
                        dtTileRef tile;

                        if (dtCreateNavMeshData(&params, &navData, &navDataSize)
                            && dtStatusSucceed(Navmesh->AddTile(params.tileX, params.tileY, navData, navDataSize, &tile)))
                        {
                            // structure->ExportDebugObjFile("C:\\Users\\Jannis\\source\\repos\\recastnavigation\\RecastDemo\\Bin\\Meshes\\structure.obj");
                            // Navmesh->SaveRecastDemoMesh("C:\\Users\\Jannis\\source\\repos\\recastnavigation\\RecastDemo\\Bin\\all_tiles_navmesh.bin");
                            Navmesh->FreeTile(&navData, &navDataSize, &tile);
                        }
                    }

                    rcFreePolyMesh(pmesh);
                    rcFreePolyMeshDetail(dmesh);
                }
            }

            delete[] spmeshes;
            delete[] sdmeshes;
        }
    }

    inline bool GenerateNavmeshTile(float* bbMin, float* bbMax, Structure* terrain, rcPolyMesh** pmesh, rcPolyMeshDetail** dmesh) noexcept
    {
        if (rcHeightfield* heightField = rcAllocHeightfield())
        {
            if (rcCreateHeightfield(this, *heightField, RcCfg.width, RcCfg.height, bbMin, bbMax, RcCfg.cs, RcCfg.ch))
            {
                rcRasterizeTriangles(this, terrain->Verts(), terrain->verts.size(), terrain->Tris(), terrain->AreaIds(), terrain->tris.size(), *heightField, RcCfg.walkableClimb);

                rcFilterLowHangingWalkableObstacles(this, RcCfg.walkableClimb, *heightField);
                rcFilterLedgeSpans(this, RcCfg.walkableHeight, RcCfg.walkableClimb, *heightField);
                rcFilterWalkableLowHeightSpans(this, RcCfg.walkableHeight, *heightField);

                if (rcCompactHeightfield* compactHeightfield = rcAllocCompactHeightfield())
                {
                    if (rcBuildCompactHeightfield(this, RcCfg.walkableHeight, RcCfg.walkableClimb, *heightField, *compactHeightfield))
                    {
                        rcFreeHeightField(heightField);

                        if (rcErodeWalkableArea(this, RcCfg.walkableRadius, *compactHeightfield))
                        {
                            if (rcMedianFilterWalkableArea(this, *compactHeightfield))
                            {
                                if (rcBuildDistanceField(this, *compactHeightfield))
                                {
                                    if (rcBuildRegions(this, *compactHeightfield, RcCfg.borderSize, RcCfg.minRegionArea, RcCfg.mergeRegionArea))
                                    {
                                        if (rcContourSet* contourSet = rcAllocContourSet())
                                        {
                                            if (rcBuildContours(this, *compactHeightfield, RcCfg.maxSimplificationError, RcCfg.maxEdgeLen, *contourSet))
                                            {
                                                if (*pmesh = rcAllocPolyMesh())
                                                {
                                                    if (rcBuildPolyMesh(this, *contourSet, RcCfg.maxVertsPerPoly, **pmesh))
                                                    {
                                                        rcFreeContourSet(contourSet);

                                                        if (*dmesh = rcAllocPolyMeshDetail())
                                                        {
                                                            if (rcBuildPolyMeshDetail(this, **pmesh, *compactHeightfield, RcCfg.detailSampleDist, RcCfg.detailSampleMaxError, **dmesh))
                                                            {
                                                                rcFreeCompactHeightfield(compactHeightfield);
                                                                return true;
                                                            }
                                                        }
                                                    }
                                                }
                                            }

                                            rcFreeContourSet(contourSet);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    rcFreeCompactHeightfield(compactHeightfield);
                }
            }

            rcFreeHeightField(heightField);
        }

        return false;
    }
};
