#include "Main.hpp"

int main() noexcept
{
    rcContext rcCtx(false);
    rcConfig cfg{ 0 };
    cfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    cfg.cs = MESH_WORLDUNIT;
    cfg.ch = MESH_WORLDUNIT;
    cfg.detailSampleDist = cfg.cs * 16;
    cfg.detailSampleMaxError = cfg.ch * 1;

    cfg.walkableSlopeAngle = 53.0f;
    cfg.walkableClimb = 1;
    cfg.walkableHeight = 3;
    cfg.walkableRadius = 2;

    cfg.minRegionArea = 8;
    cfg.mergeRegionArea = 20;
    cfg.maxEdgeLen = 12;
    cfg.maxSimplificationError = 1.3f;
    cfg.borderSize = 2;

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

    for (int mapIndex = 0; mapIndex < maps.size(); ++mapIndex) // maps.size()
    {
        START_TIMER(startTimeTile);

        const auto& [id, name] = maps[mapIndex];
        const auto mapsPath = std::format("World\\Maps\\{}\\{}", name, name);
        const auto wdtPath = std::format("{}.wdt", mapsPath);

        if (Wdt* wdt = mpqReader.GetFileContent<Wdt>(wdtPath.c_str()))
        {
            dtNavMesh* navmesh = dtAllocNavMesh();

            dtNavMeshParams navmeshParams{ 0 };
            navmeshParams.tileWidth = TILESIZE;
            navmeshParams.tileHeight = TILESIZE;
            navmeshParams.maxPolys = 1 << DT_POLY_BITS;
            navmeshParams.maxTiles = WDT_MAP_SIZE * WDT_MAP_SIZE;

            navmesh->init(&navmeshParams);

            std::vector<Vector3> debugVerts;
            std::vector<Tri> debugTris;

#pragma omp parallel for schedule(dynamic)
            for (int i = 0; i < WDT_MAP_SIZE * WDT_MAP_SIZE; ++i) // WDT_MAP_SIZE * WDT_MAP_SIZE
            {
                auto x = i % WDT_MAP_SIZE;
                auto y = i / WDT_MAP_SIZE;

                const auto amtPath = std::format("{}{}_{}_{}.amt", OUTPUT_DIR, mapIndex, y, x);

                if (wdt && wdt->Main() && wdt->Main()->adt[x][y].exists && !std::filesystem::exists(amtPath))
                {
                    const auto adtPath = std::format("{}_{}_{}.adt", mapsPath, y, x);
                    unsigned char* adtData = nullptr;
                    unsigned int adtSize = 0;

                    std::cout << "[Maps] " << name << ": ADT(" << x << ", " << y << ") " << adtPath << std::endl;

                    if (Adt* adt = mpqReader.GetFileContent<Adt>(adtPath.c_str()))
                    {
                        Structure terrain;

                        // terrain and liquids
                        START_TIMER(startTimeTerrainAndLiquids);

                        for (int a = 0; a < ADT_CELLS_PER_GRID * ADT_CELLS_PER_GRID; ++a)
                        {
                            const int cx = a / ADT_CELLS_PER_GRID;
                            const int cy = a % ADT_CELLS_PER_GRID;

                            adt->GetTerrainVertsAndTris(cx, cy, terrain);
                            adt->GetLiquidVertsAndTris(cx, cy, terrain);
                        }

                        STOP_TIMER(startTimeTerrainAndLiquids, "[Maps] >> Extracting Terrain and Liquids took ");

                        // wmo 
                        START_TIMER(startTimeWmo);
                        adt->GetWmoVertsAndTris(mpqReader, terrain);
                        STOP_TIMER(startTimeWmo, "[Maps] >> Extracting WMO's took ");

                        // doodad
                        START_TIMER(startTimeDoodads);
                        adt->GetDoodadVertsAndTris(mpqReader, terrain);
                        STOP_TIMER(startTimeDoodads, "[Maps] >> Extracting Doodads took ");

                        START_TIMER(startTimeClean);
                        const auto vertCountBefore = terrain.verts.size();
                        const auto triCountBefore = terrain.tris.size();
                        terrain.Clean();
                        STOP_TIMER(startTimeClean, "[Maps] >> Removing duplicate " << vertCountBefore - terrain.verts.size() << "verts, " << triCountBefore - terrain.tris.size() << "tris took ");

                        // debug export
                        // debugVerts.append_range(terrain.verts);
                        // debugTris.append_range(terrain.tris);

                        // navmeh export
                        rcPolyMesh* pmesh = nullptr;
                        rcPolyMeshDetail* dmesh = nullptr;

                        if (terrain.tris.size() > 0 && GenerateNavmeshTile(rcCtx, cfg, x, y, terrain, pmesh, dmesh))
                        {
                            START_TIMER(startTimeNavmesh);

                            for (int i = 0; i < pmesh->npolys; ++i)
                            {
                                if (TriAreaId area = static_cast<TriAreaId>(pmesh->areas[i] & 63))
                                {
                                    switch (area)
                                    {
                                    case LIQUID_LAVA:
                                    case LIQUID_SLIME:
                                        pmesh->flags[i] = NAV_LAVA_SLIME;
                                        break;

                                    case ALLIANCE_LIQUID_SLIME:
                                    case ALLIANCE_LIQUID_LAVA:
                                        pmesh->flags[i] = NAV_LAVA_SLIME & NAV_ALLIANCE;
                                        break;

                                    case HORDE_LIQUID_LAVA:
                                    case HORDE_LIQUID_SLIME:
                                        pmesh->flags[i] = NAV_LAVA_SLIME & NAV_HORDE;
                                        break;

                                    case LIQUID_WATER:
                                    case LIQUID_OCEAN:
                                        pmesh->flags[i] = NAV_WATER;
                                        break;

                                    case ALLIANCE_LIQUID_WATER:
                                    case ALLIANCE_LIQUID_OCEAN:
                                        pmesh->flags[i] = NAV_WATER & NAV_ALLIANCE;
                                        break;

                                    case HORDE_LIQUID_WATER:
                                    case HORDE_LIQUID_OCEAN:
                                        pmesh->flags[i] = NAV_WATER & NAV_HORDE;
                                        break;

                                    case TERRAIN_GROUND:
                                    case WMO:
                                    case DOODAD:
                                        pmesh->flags[i] = NAV_GROUND;
                                        break;

                                    case ALLIANCE_TERRAIN_GROUND:
                                    case ALLIANCE_WMO:
                                    case ALLIANCE_DOODAD:
                                        pmesh->flags[i] = NAV_GROUND & NAV_ALLIANCE;
                                        break;

                                    case HORDE_TERRAIN_GROUND:
                                    case HORDE_WMO:
                                    case HORDE_DOODAD:
                                        pmesh->flags[i] = NAV_GROUND & NAV_HORDE;
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

                            params.walkableHeight = MESH_WORLDUNIT * cfg.walkableHeight;
                            params.walkableRadius = MESH_WORLDUNIT * cfg.walkableRadius;
                            params.walkableClimb = MESH_WORLDUNIT * cfg.walkableClimb;

                            rcVcopy(params.bmin, cfg.bmin);
                            rcVcopy(params.bmax, cfg.bmax);
                            params.tileX = x;
                            params.tileY = y;
                            params.cs = cfg.cs;
                            params.ch = cfg.ch;
                            params.tileLayer = 0;
                            params.buildBvTree = true;

                            unsigned char* navData = nullptr;
                            int navDataSize = 0;

                            if (dtCreateNavMeshData(&params, &navData, &navDataSize))
                            {
                                dtTileRef tileRef = 0;
                                dtStatus dtResult = navmesh->addTile(navData, navDataSize, DT_TILE_FREE_DATA, 0, &tileRef);

                                if (tileRef && dtStatusSucceed(dtResult))
                                {
                                    STOP_TIMER(startTimeNavmesh, "[Maps] >> Building navmesh took ");

                                    std::ofstream file(amtPath, std::ios::binary);

                                    if (file.is_open()) 
                                    {
                                        file.write(reinterpret_cast<const char*>(navData), navDataSize);
                                        file.close();
                                    }
                                }
                            }

                            rcFreePolyMesh(pmesh);
                            rcFreePolyMeshDetail(dmesh);
                        }
                        else
                        {
                            std::cout << "[Maps] >> Building navmesh failed" << std::endl;
                        }
                    }
                }
            }

            STOP_TIMER(startTimeTile, "[Maps] >> Building map WDT(" << wdtPath << ") took ");

            dtNavMeshQuery query;
            query.init(navmesh, 2048);
            float start[3] = { -371.839752f, 71.638428f, -8826.562500f };
            float end[3] = { -130.297256f, 80.906364f, -8918.406250f };

            dtPolyRef startRefX;
            dtPolyRef endRefX;
            dtQueryFilter filter;
            query.findRandomPoint(&filter, frand, &startRefX, start);
            query.findRandomPoint(&filter, frand, &endRefX, end);

            const float extents[3] = { 12.0f, 12.0f, 12.0f };

            float nstart[3] = { 0.0f, 0.0f, 0.0f };
            float nend[3] = { 0.0f, 0.0f, 0.0f };

            dtPolyRef startRef;
            dtPolyRef endRef;
            dtStatus dtResultA = query.findNearestPoly(start, extents, &filter, &startRef, nstart);
            dtStatus dtResultB = query.findNearestPoly(end, extents, &filter, &endRef, nend);

            dtPolyRef path[60]{ 0 };
            int pathCount = 0;

            query.findPath(startRefX, endRefX, start, end, &filter, path, &pathCount, sizeof(path) / sizeof(path[0]));

            // *.obj file export for debug stuff
            if (debugTris.size() > 0)
            {
                START_TIMER(startTimeExport);
                ExportDebugObjFile(debugVerts, debugTris);
                STOP_TIMER(startTimeTile, "[OBJ] Export took ");
            }

            dtFreeNavMesh(navmesh);
        }
    }

    return 0;
}

static bool GenerateNavmeshTile(rcContext& rcCtx, rcConfig& cfg, int x, int y, Structure& terrain, rcPolyMesh*& pmesh, rcPolyMeshDetail*& dmesh) noexcept
{
    rcCalcBounds(reinterpret_cast<float*>(terrain.verts.begin()._Ptr), terrain.verts.size(), cfg.bmin, cfg.bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    cfg.bmin[0] -= cfg.borderSize * cfg.cs;
    cfg.bmin[2] -= cfg.borderSize * cfg.cs;
    cfg.bmax[0] += cfg.borderSize * cfg.cs;
    cfg.bmax[2] += cfg.borderSize * cfg.cs;

    if (rcHeightfield* heightField = rcAllocHeightfield())
    {
        if (rcCreateHeightfield(&rcCtx, *heightField, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
        {
            rcClearUnwalkableTriangles(&rcCtx, cfg.walkableSlopeAngle, terrain.Verts(), terrain.verts.size(), terrain.Tris(), terrain.tris.size(), terrain.AreaIds());
            rcRasterizeTriangles(&rcCtx, terrain.Verts(), terrain.verts.size(), terrain.Tris(), terrain.AreaIds(), terrain.tris.size(), *heightField, cfg.walkableClimb);

            rcFilterLowHangingWalkableObstacles(&rcCtx, cfg.walkableClimb, *heightField);
            rcFilterLedgeSpans(&rcCtx, cfg.walkableHeight, cfg.walkableClimb, *heightField);
            rcFilterWalkableLowHeightSpans(&rcCtx, cfg.walkableHeight, *heightField);

            if (rcCompactHeightfield* compactHeightfield = rcAllocCompactHeightfield())
            {
                if (rcBuildCompactHeightfield(&rcCtx, cfg.walkableHeight, cfg.walkableClimb, *heightField, *compactHeightfield))
                {
                    rcFreeHeightField(heightField);

                    if (rcErodeWalkableArea(&rcCtx, cfg.walkableRadius, *compactHeightfield))
                    {
                        if (rcMedianFilterWalkableArea(&rcCtx, *compactHeightfield))
                        {
                            if (rcBuildDistanceField(&rcCtx, *compactHeightfield))
                            {
                                if (rcBuildRegions(&rcCtx, *compactHeightfield, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea))
                                {
                                    if (rcContourSet* contourSet = rcAllocContourSet())
                                    {
                                        if (rcBuildContours(&rcCtx, *compactHeightfield, cfg.maxSimplificationError, cfg.maxEdgeLen, *contourSet))
                                        {
                                            if (pmesh = rcAllocPolyMesh())
                                            {
                                                if (rcBuildPolyMesh(&rcCtx, *contourSet, cfg.maxVertsPerPoly, *pmesh))
                                                {
                                                    rcFreeContourSet(contourSet);

                                                    if (dmesh = rcAllocPolyMeshDetail())
                                                    {
                                                        if (rcBuildPolyMeshDetail(&rcCtx, *pmesh, *compactHeightfield, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh))
                                                        {
                                                            return true;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        else
                                        {
                                            rcFreeContourSet(contourSet);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    rcFreeHeightField(heightField);
                }

                rcFreeCompactHeightfield(compactHeightfield);
            }
        }
    }

    return false;
}

void ExportDebugObjFile(const std::vector<Vector3>& vertexes, const std::vector<Tri>& tris) noexcept
{
    std::fstream objFstream;
    objFstream << std::fixed << std::showpoint;
    objFstream << std::setprecision(8);
    objFstream.open("C:\\Users\\Jannis\\source\\repos\\recastnavigation\\RecastDemo\\Bin\\Meshes\\debug.obj", std::fstream::out);

    for (const auto& v3 : vertexes)
    {
        objFstream << "v " << v3.x << " " << v3.y << " " << v3.z << "\n";
    }

    for (const auto& tri : tris)
    {
        objFstream << "f " << tri.a + 1 << " " << tri.b + 1 << " " << tri.c + 1 << "\n";
    }

    objFstream.close();
}
