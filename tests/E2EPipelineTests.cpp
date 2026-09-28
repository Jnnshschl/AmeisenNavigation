#include "TestFramework.hpp"

#include <cmath>

#include "AmeisenNavigation.hpp"
#include "TestWorld.hpp"

namespace {
using TestWorld::Wow;

AmeisenNavigation& Navigation()
{
    static AmeisenNavigation nav = [] {
        AmeisenNavigationSettings settings;
        settings.meshFolder = TestWorld::Get().meshDir;
        settings.useAnp = true;
        settings.maxPointPath = 256;
        return AmeisenNavigation(settings);
    }();
    return nav;
}

constexpr size_t CLIENT = 1;

void EnsureClient()
{
    Navigation().NewClient(CLIENT);
}

/// Area of the polygon closest to an RD position.
unsigned char AreaAt(float rdX, float rdY, float rdZ)
{
    const auto loaded = Anp::Load(TestWorld::Get().meshDir / Anp::FileName(TestWorld::MAP_ID));
    REQUIRE(loaded.navMesh);

    NavMeshQueryPtr query(dtAllocNavMeshQuery());
    REQUIRE(dtStatusSucceed(query->init(loaded.navMesh.get(), 2048)));

    dtQueryFilter filter;
    const float pos[3]{rdX, rdY, rdZ};
    const float extents[3]{0.5f, 2.0f, 0.5f};
    dtPolyRef ref = 0;
    float nearest[3]{};
    REQUIRE(dtStatusSucceed(query->findNearestPoly(pos, extents, &filter, &ref, nearest)));
    REQUIRE(ref != 0);

    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    REQUIRE(dtStatusSucceed(loaded.navMesh->getTileAndPolyByRef(ref, &tile, &poly)));
    return poly->getArea();
}

float PathLength(const Path& path)
{
    float length = 0.0f;

    for (int i = 1; i < path.pointCount; ++i)
    {
        length += path[i].DistanceTo(path[i - 1]);
    }

    return length;
}
} // namespace

TEST_CASE(Pipeline_BuildsAllTiles)
{
    const auto& world = TestWorld::Get();
    CHECK_EQ(world.tilesBuilt, 2);
    CHECK_EQ(world.tilesFailed, 0);
    REQUIRE(std::filesystem::exists(world.meshDir / "001.anp"));

    const auto loaded = Anp::Load(world.meshDir / "001.anp");
    REQUIRE(loaded.navMesh != nullptr);
    CHECK_EQ(loaded.mapId, 1);
    CHECK_EQ(loaded.tilesLoaded, 2);
    CHECK_EQ(loaded.tilesRejected, 0);
}

TEST_CASE(Pipeline_TooDetailedTilesAreRebuiltCoarser)
{
    // Force the vertex limit down so the first attempt overflows: the processor must retry with coarser
    // settings (or give up cleanly) instead of writing corrupt tiles.
    Structure geometry;
    TestWorld::AddQuad(geometry, -2 * TILESIZE, -TILESIZE, 0.0f, 0.0f, 0.0f, TERRAIN_GROUND);

    for (int i = 0; i < 40; ++i)
    {
        const float x = -1000.0f + i * 24.0f;
        TestWorld::AddWallX(geometry, x, -500.0f + (i % 5) * 20.0f, -40.0f - (i % 7) * 15.0f, -1.0f, 6.0f);
    }

    dtNavMeshParams params{};
    params.orig[0] = (31 - 33) * TILESIZE;
    params.orig[2] = (31 - 32) * TILESIZE;
    params.tileWidth = params.tileHeight = TILESIZE;
    params.maxTiles = 64 * 64;
    params.maxPolys = 1 << 20;

    bool sawSuccessfulRetry = false;
    bool sawFailure = false;

    for (const int limit : {4000, 2000, 1200, 800, 600, 400, 300, 200, 100})
    {
        auto config = TestWorld::FastBuildConfig();
        config.maxTileVertices = limit;

        const auto dir = std::filesystem::temp_directory_path() / "anav_tests" / "coarse";
        Anp::AnpWriter writer(7, params);
        AdtTileProcessor processor(&writer, dir, "Coarse", config);
        processor.Process(&geometry, {{1, 0, 32, 32}, {0, 0, 33, 32}}, nullptr, nullptr);

        const auto& stats = processor.GetStats();
        CHECK_EQ(stats.built.load() + stats.failed.load() + stats.empty.load(), 2);
        CHECK_EQ(writer.GetTileCount(), stats.built.load());

        sawSuccessfulRetry |= stats.retried.load() > 0;
        sawFailure |= stats.failed.load() > 0;
    }

    CHECK(sawSuccessfulRetry);
    CHECK(sawFailure);
}

TEST_CASE(Pipeline_TilesAreAdtAligned)
{
    const auto loaded = Anp::Load(TestWorld::Get().meshDir / "001.anp");
    REQUIRE(loaded.navMesh);

    int found = 0;

    for (int i = 0; i < loaded.navMesh->getMaxTiles(); ++i)
    {
        const dtMeshTile* tile = static_cast<const dtNavMesh*>(loaded.navMesh.get())->getTile(i);

        if (!tile || !tile->header)
        {
            continue;
        }

        found++;
        const float expectedMinX = tile->header->x == 1 ? -TILESIZE : -2.0f * TILESIZE;
        CHECK_NEAR(tile->header->bmin[0], expectedMinX, 1e-2);
        CHECK_NEAR(tile->header->bmin[2], -TILESIZE, 1e-2);
        CHECK_NEAR(tile->header->bmax[0] - tile->header->bmin[0], TILESIZE, 1e-2);
    }

    CHECK_EQ(found, 2);
}

TEST_CASE(Pipeline_AreasAreMarked)
{
    CHECK_EQ(AreaAt(-600.0f, 0.0f, -300.0f), TERRAIN_GROUND);
    CHECK(IsLiquidArea(AreaAt(-90.0f, 0.5f, -430.0f)));
    CHECK_EQ(AreaAt(-175.0f, 0.0f, -275.0f), TERRAIN_ROAD);
    CHECK_EQ(AreaAt(-70.0f, 0.0f, -70.0f), ALLIANCE_TERRAIN_GROUND);
    CHECK_EQ(AreaAt(-425.0f, TestWorld::PLATFORM_Y, -425.0f), WMO);
}

TEST_CASE(Navigation_PathAcrossTilesAvoidsWall)
{
    EnsureClient();

    // Start in ADT (33,32), end in ADT (32,32): crosses the tile border and the wall.
    const Vector3 start = Wow(-700.0f, 0.0f, -300.0f);
    const Vector3 end = Wow(-200.0f, 0.0f, -300.0f);
    Path path(256);

    REQUIRE(Navigation().GetPath(CLIENT, TestWorld::MAP_ID, start, end, path));
    REQUIRE(path.pointCount >= 3);

    CHECK_NEAR(path[0].x, start.x, 1.5);
    CHECK_NEAR(path[0].y, start.y, 1.5);
    CHECK_NEAR(path[path.pointCount - 1].x, end.x, 1.5);
    CHECK_NEAR(path[path.pointCount - 1].y, end.y, 1.5);

    // The wall forces a detour around one of its ends.
    CHECK(PathLength(path) > start.DistanceTo(end) + 50.0f);

    bool passesWallEnd = false;

    for (const auto& p : path)
    {
        // WoW x = RD z: a corner near either end of the wall.
        passesWallEnd |= p.x > TestWorld::WALL_Z_MAX - 5.0f || p.x < TestWorld::WALL_Z_MIN + 5.0f;
    }

    CHECK(passesWallEnd);
}

TEST_CASE(Navigation_RaycastHitsWall)
{
    EnsureClient();

    RaycastResult hit;
    const bool clear = Navigation().CastMovementRay(CLIENT, TestWorld::MAP_ID, Wow(-400.0f, 0.0f, -300.0f),
                                                    Wow(-200.0f, 0.0f, -300.0f), &hit);
    CHECK(!clear);
    CHECK(hit.hit);
    CHECK(hit.t > 0.0f && hit.t < 1.0f);
    // Hit position (WoW y = RD x) close to the wall.
    CHECK_NEAR(hit.hitPosition.y, TestWorld::WALL_X, 4.0);

    RaycastResult open;
    CHECK(Navigation().CastMovementRay(CLIENT, TestWorld::MAP_ID, Wow(-400.0f, 0.0f, -50.0f),
                                       Wow(-200.0f, 0.0f, -50.0f), &open));
    CHECK(!open.hit);
    CHECK_NEAR(open.t, 1.0f, 1e-6);
}

TEST_CASE(Navigation_GetHeightPicksClosestFloor)
{
    EnsureClient();

    Vector3 out;
    REQUIRE(Navigation().GetHeight(CLIENT, TestWorld::MAP_ID, Wow(-600.0f, 3.0f, -200.0f), out));
    CHECK_NEAR(out.z, 0.0f, 0.6);

    // Below the platform: ground or deck depending on the probe height.
    REQUIRE(Navigation().GetHeight(CLIENT, TestWorld::MAP_ID, Wow(-425.0f, 6.0f, -425.0f), out));
    CHECK_NEAR(out.z, TestWorld::PLATFORM_Y, 0.6);
    REQUIRE(Navigation().GetHeight(CLIENT, TestWorld::MAP_ID, Wow(-425.0f, 0.5f, -425.0f), out));
    CHECK_NEAR(out.z, 0.0f, 0.6);

    // Way off the mesh.
    CHECK(!Navigation().GetHeight(CLIENT, TestWorld::MAP_ID, Wow(5000.0f, 0.0f, 5000.0f), out));
}

TEST_CASE(Navigation_MoveAlongSurfaceStopsAtWall)
{
    EnsureClient();

    Vector3 result;
    REQUIRE(Navigation().MoveAlongSurface(CLIENT, TestWorld::MAP_ID, Wow(-320.0f, 0.0f, -300.0f),
                                          Wow(-280.0f, 0.0f, -300.0f), result));

    // Blocked before the wall (RD x = WoW y).
    CHECK(result.y < TestWorld::WALL_X);
    CHECK(result.y > -325.0f);
    CHECK_NEAR(result.z, 0.0f, 0.6);
}

TEST_CASE(Navigation_RandomPointsAreOnMesh)
{
    EnsureClient();

    for (int i = 0; i < 50; ++i)
    {
        Vector3 p;
        REQUIRE(Navigation().GetRandomPoint(CLIENT, TestWorld::MAP_ID, p));
        CHECK(p.y >= -2.0f * TILESIZE - 1.0f && p.y <= 1.0f);
        CHECK(p.x >= -TILESIZE - 1.0f && p.x <= 1.0f);

        Vector3 around;
        const Vector3 center = Wow(-700.0f, 0.0f, -300.0f);

        if (Navigation().GetRandomPointAround(CLIENT, TestWorld::MAP_ID, center, 20.0f, around))
        {
            CHECK(around.DistanceTo(center) < 60.0f);
        }
    }
}

TEST_CASE(Navigation_RandomPathKeepsEndpoints)
{
    EnsureClient();

    const Vector3 start = Wow(-700.0f, 0.0f, -300.0f);
    const Vector3 end = Wow(-200.0f, 0.0f, -300.0f);
    Path path(256);

    REQUIRE(Navigation().GetRandomPath(CLIENT, TestWorld::MAP_ID, start, end, path, 3.0f));
    REQUIRE(path.pointCount >= 2);
    CHECK_NEAR(path[path.pointCount - 1].x, end.x, 1.5);
    CHECK_NEAR(path[path.pointCount - 1].y, end.y, 1.5);
}

TEST_CASE(Navigation_RandomPathWithOversizedCallerBuffer)
{
    EnsureClient();

    // The client's corner ref buffer holds maxPointPath (256) entries, a bigger caller buffer must not overflow it.
    Path path(4096);
    REQUIRE(Navigation().GetRandomPath(CLIENT, TestWorld::MAP_ID, Wow(-700.0f, 0.0f, -300.0f),
                                       Wow(-200.0f, 0.0f, -300.0f), path, 2.0f));
    CHECK(path.pointCount <= 256);
}

TEST_CASE(Navigation_ValidationSnapsSmoothedPathToMesh)
{
    EnsureClient();

    const Vector3 start = Wow(-700.0f, 0.0f, -300.0f);
    const Vector3 end = Wow(-200.0f, 0.0f, -300.0f);
    Path path(256);
    Path smooth(256);
    Path validated(256);

    REQUIRE(Navigation().GetPath(CLIENT, TestWorld::MAP_ID, start, end, path));
    Navigation().SmoothPathCatmullRom(path, smooth, 6, 0.5f);
    REQUIRE(smooth.pointCount > path.pointCount);

    REQUIRE(Navigation().PostProcessMoveAlongSurface(CLIENT, TestWorld::MAP_ID, smooth, validated));
    REQUIRE(validated.pointCount > 0);

    for (const auto& p : validated)
    {
        CHECK_NEAR(p.z, 0.0f, 0.6);
    }

    REQUIRE(Navigation().PostProcessClosestPointOnPoly(CLIENT, TestWorld::MAP_ID, smooth, validated));
    CHECK(validated.pointCount > 0);
}

TEST_CASE(Navigation_UnknownMapAndClientFailGracefully)
{
    EnsureClient();

    Path path(16);
    CHECK(!Navigation().GetPath(CLIENT, 999, Wow(0, 0, 0), Wow(1, 0, 1), path));
    CHECK(!Navigation().GetPath(424242, TestWorld::MAP_ID, Wow(0, 0, 0), Wow(1, 0, 1), path));
    CHECK(!Navigation().GetPath(CLIENT, TestWorld::MAP_ID, Wow(std::nanf(""), 0, 0), Wow(1, 0, 1), path));
    CHECK_EQ(path.pointCount, 0);
}
