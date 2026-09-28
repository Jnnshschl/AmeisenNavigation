#include "TestFramework.hpp"

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

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

TEST_CASE(Navigation_PartialPathIsReported)
{
    EnsureClient();
    Path path(256);
    bool partial = true;

    REQUIRE(Navigation().GetPath(CLIENT, TestWorld::MAP_ID, Wow(-700.0f, 0.0f, -300.0f), Wow(-200.0f, 0.0f, -300.0f),
                                 path, &partial));
    CHECK(!partial);

    // The platform deck (5 yards up, no ramp) can't be reached from the ground: the path leads next to it.
    const Vector3 ground = Wow(-600.0f, 0.0f, -425.0f);
    const Vector3 deck = Wow(-425.0f, TestWorld::PLATFORM_Y, -425.0f);

    REQUIRE(Navigation().GetPath(CLIENT, TestWorld::MAP_ID, ground, deck, path, &partial));
    CHECK(partial);
    REQUIRE(path.pointCount >= 2);
    CHECK_NEAR(path[path.pointCount - 1].z, 0.0f, 0.6);

    partial = false;
    REQUIRE(Navigation().GetRandomPath(CLIENT, TestWorld::MAP_ID, ground, deck, path, 1.0f, &partial));
    CHECK(partial);
}

TEST_CASE(Navigation_ExplorePolygonCoversArea)
{
    EnsureClient();

    // Both sides of the wall: RD x [-390, -210], z [-450, -150]. The wall can only be passed at its end (z > -100).
    const Vector3 outline[] = {Wow(-390.0f, 0.0f, -450.0f), Wow(-210.0f, 0.0f, -450.0f), Wow(-210.0f, 0.0f, -150.0f),
                               Wow(-390.0f, 0.0f, -150.0f)};
    const Vector3 start = Wow(-380.0f, 0.0f, -440.0f);
    constexpr float spacing = 40.0f;

    Path path(256);
    ExploreResult result;
    REQUIRE(Navigation().ExplorePolygon(CLIENT, TestWorld::MAP_ID, start, outline, spacing, path, &result));

    CHECK_NEAR(result.spacing, spacing, 1e-3);
    CHECK(result.waypoints > 20);
    CHECK_EQ(result.reached, result.waypoints);
    CHECK(!result.truncated);
    CHECK(path[0].DistanceTo(start) < 2.0f);

    bool aroundWall = false;

    for (const auto& p : path)
    {
        CHECK_NEAR(p.z, 0.0f, 0.6);
        aroundWall |= p.x > TestWorld::WALL_Z_MAX - 5.0f; // WoW x = RD z
    }

    CHECK(aroundWall);

    // Every point of the area is within the spacing of a route point.
    for (float rdX = -385.0f; rdX < -210.0f; rdX += 10.0f)
    {
        for (float rdZ = -445.0f; rdZ < -150.0f; rdZ += 10.0f)
        {
            const Vector3 probe = Wow(rdX, 0.0f, rdZ);
            float nearest = 1e9f;

            for (const auto& p : path)
            {
                nearest = std::min(nearest, std::hypot(p.x - probe.x, p.y - probe.y));
            }

            CHECK(nearest <= spacing);
        }
    }
}

TEST_CASE(Navigation_ExplorePolygonSkipsUnreachableWaypoints)
{
    EnsureClient();

    // The deck is an island: its waypoints (outline at deck height) can't be reached from the ground.
    const float y = TestWorld::PLATFORM_Y;
    const Vector3 deck[] = {Wow(-447.0f, y, -447.0f), Wow(-403.0f, y, -447.0f), Wow(-403.0f, y, -403.0f),
                            Wow(-447.0f, y, -403.0f)};

    Path path(256);
    ExploreResult result;
    CHECK(!Navigation().ExplorePolygon(CLIENT, TestWorld::MAP_ID, Wow(-600.0f, 0.0f, -425.0f), deck, 10.0f, path,
                                       &result));
    CHECK(result.waypoints > 5);
    CHECK_EQ(result.reached, 0);
    CHECK_EQ(path.pointCount, 0);

    REQUIRE(Navigation().ExplorePolygon(CLIENT, TestWorld::MAP_ID, Wow(-425.0f, y, -425.0f), deck, 10.0f, path,
                                        &result));
    CHECK_EQ(result.reached, result.waypoints);

    for (const auto& p : path)
    {
        CHECK_NEAR(p.z, y, 0.6);
    }
}

TEST_CASE(Navigation_ExplorePolygonMixedReachability)
{
    EnsureClient();

    // Ground around the deck island, outline at deck height: deck samples snap onto the (unreachable) deck, the
    // others to the ground. After the first failed search the remaining deck waypoints are pruned via the closed
    // list, the ground must still be covered completely.
    const float y = TestWorld::PLATFORM_Y;
    const Vector3 area[] = {Wow(-520.0f, y, -520.0f), Wow(-340.0f, y, -520.0f), Wow(-340.0f, y, -340.0f),
                            Wow(-520.0f, y, -340.0f)};
    constexpr float spacing = 15.0f;

    Path path(256);
    ExploreResult result;
    REQUIRE(Navigation().ExplorePolygon(CLIENT, TestWorld::MAP_ID, Wow(-510.0f, 0.0f, -510.0f), area, spacing, path,
                                        &result));
    CHECK(!result.truncated);
    CHECK(result.reached > 20);
    CHECK(result.reached < result.waypoints); // the deck waypoints

    for (const auto& p : path)
    {
        CHECK_NEAR(p.z, 0.0f, 0.6);
    }

    // Ground away from the deck (RD [-450, -400]) is covered.
    for (float rdX = -515.0f; rdX < -340.0f; rdX += 10.0f)
    {
        for (float rdZ = -515.0f; rdZ < -340.0f; rdZ += 10.0f)
        {
            if (rdX > -460.0f && rdX < -390.0f && rdZ > -460.0f && rdZ < -390.0f)
            {
                continue;
            }

            const Vector3 probe = Wow(rdX, 0.0f, rdZ);
            float nearest = 1e9f;

            for (const auto& p : path)
            {
                nearest = std::min(nearest, std::hypot(p.x - probe.x, p.y - probe.y));
            }

            CHECK(nearest <= spacing);
        }
    }
}

TEST_CASE(Navigation_ExplorePolygonLimits)
{
    EnsureClient();
    Path path(256);
    ExploreResult result;
    const Vector3 start = Wow(-700.0f, 0.0f, -300.0f);

    // Both ADTs with a 2 yard spacing: the spacing grows to stay below MAX_EXPLORE_WAYPOINTS, the route is cut.
    const Vector3 all[] = {Wow(-1060.0f, 0.0f, -530.0f), Wow(-10.0f, 0.0f, -530.0f), Wow(-10.0f, 0.0f, -10.0f),
                           Wow(-1060.0f, 0.0f, -10.0f)};
    REQUIRE(Navigation().ExplorePolygon(CLIENT, TestWorld::MAP_ID, start, all, 2.0f, path, &result));
    CHECK(result.spacing > 20.0f);
    CHECK(result.waypoints <= MAX_EXPLORE_WAYPOINTS);
    CHECK(result.truncated);
    CHECK_EQ(path.pointCount, path.maxSize);

    // Invalid polygons.
    CHECK(!Navigation().ExplorePolygon(CLIENT, TestWorld::MAP_ID, start, std::span<const Vector3>(all, 2), 10.0f,
                                       path));
    const Vector3 nan[] = {all[0], all[1], Wow(std::nanf(""), 0.0f, 0.0f)};
    CHECK(!Navigation().ExplorePolygon(CLIENT, TestWorld::MAP_ID, start, nan, 10.0f, path));
    CHECK(!Navigation().ExplorePolygon(CLIENT, 999, start, all, 10.0f, path));
}

TEST_CASE(Navigation_RejectsInvalidCoordinatesAndMaps)
{
    EnsureClient();

    // Non-finite and out-of-world values must never reach Detour (float -> int tile math is UB for them).
    const Vector3 valid = Wow(-700.0f, 0.0f, -300.0f);
    const Vector3 invalid[] = {Vector3(std::nanf(""), 0.0f, 0.0f), Vector3(0.0f, INFINITY, 0.0f),
                               Vector3(1e30f, -1e30f, 0.0f), Vector3(0.0f, 0.0f, -2e5f)};
    Path path(64);
    Vector3 out;

    for (const Vector3& bad : invalid)
    {
        CHECK(!Navigation().GetPath(CLIENT, TestWorld::MAP_ID, valid, bad, path));
        CHECK(!Navigation().GetPath(CLIENT, TestWorld::MAP_ID, bad, valid, path));
        CHECK(!Navigation().GetRandomPath(CLIENT, TestWorld::MAP_ID, valid, bad, path, 1.0f));
        CHECK(!Navigation().MoveAlongSurface(CLIENT, TestWorld::MAP_ID, valid, bad, out));
        CHECK(!Navigation().MoveAlongSurface(CLIENT, TestWorld::MAP_ID, bad, valid, out));
        CHECK(!Navigation().CastMovementRay(CLIENT, TestWorld::MAP_ID, valid, bad));
        CHECK(!Navigation().CastMovementRay(CLIENT, TestWorld::MAP_ID, bad, valid));
        CHECK(!Navigation().GetHeight(CLIENT, TestWorld::MAP_ID, bad, out));
        CHECK(!Navigation().GetRandomPointAround(CLIENT, TestWorld::MAP_ID, bad, 5.0f, out));

        const Vector3 polygon[] = {valid, Wow(-650.0f, 0.0f, -300.0f), bad};
        CHECK(!Navigation().ExplorePolygon(CLIENT, TestWorld::MAP_ID, valid, polygon, 10.0f, path));
        CHECK(!Navigation().ExplorePolygon(CLIENT, TestWorld::MAP_ID, bad, polygon, 10.0f, path));

        Path input(4);
        input.TryAppend(valid);
        input.TryAppend(bad);
        Path validated(16);
        CHECK(Navigation().PostProcessMoveAlongSurface(CLIENT, TestWorld::MAP_ID, input, validated));
        CHECK_EQ(validated.pointCount, 1); // stops at the invalid point
    }

    CHECK(!Navigation().GetRandomPointAround(CLIENT, TestWorld::MAP_ID, valid, INFINITY, out));
    CHECK(!Navigation().GetRandomPointAround(CLIENT, TestWorld::MAP_ID, valid, MAX_QUERY_RADIUS * 2.0f, out));

    // Polygons larger than MAX_EXPLORE_EXTENT.
    const Vector3 huge[] = {Wow(-20000.0f, 0.0f, 0.0f), Wow(20000.0f, 0.0f, 0.0f), Wow(0.0f, 0.0f, 20000.0f)};
    CHECK(!Navigation().ExplorePolygon(CLIENT, TestWorld::MAP_ID, valid, huge, 1000.0f, path));

    // Map ids outside the accepted range never touch the nav source.
    CHECK(!Navigation().GetPath(CLIENT, -1, valid, valid, path));
    CHECK(!Navigation().GetPath(CLIENT, MAX_MAP_ID + 1, valid, valid, path));
    CHECK(!Navigation().PreloadMap(-5));

    // Still fine afterwards.
    CHECK(Navigation().GetPath(CLIENT, TestWorld::MAP_ID, valid, Wow(-200.0f, 0.0f, -300.0f), path));
}

TEST_CASE(Navigation_QueriesArePooledAcrossClients)
{
    // Own engine so the counts only include this test.
    AmeisenNavigationSettings settings;
    settings.meshFolder = TestWorld::Get().meshDir;
    settings.useAnp = true;
    settings.maxPointPath = 64;
    AmeisenNavigation nav(settings);

    const Vector3 start = Wow(-700.0f, 0.0f, -300.0f);
    const Vector3 end = Wow(-200.0f, 0.0f, -300.0f);

    // 50 clients one after another share a single query (used to be one ~2.7 MB node pool per client and map).
    for (size_t c = 100; c < 150; ++c)
    {
        REQUIRE(nav.NewClient(c));
        Path path(64);
        REQUIRE(nav.GetPath(c, TestWorld::MAP_ID, start, end, path));
    }

    CHECK_EQ(nav.GetQueryCount(TestWorld::MAP_ID), size_t{1});
    CHECK_EQ(nav.GetQueryCount(4242), size_t{0});

    // Concurrent requests get their own queries, at most one per concurrently running request.
    constexpr int threads = 8;
    std::atomic<int> failures{0};
    std::vector<std::thread> workers;

    for (int t = 0; t < threads; ++t)
    {
        workers.emplace_back([&, t] {
            const size_t client = 1000 + static_cast<size_t>(t);
            nav.NewClient(client);
            Path path(64);

            for (int i = 0; i < 25; ++i)
            {
                failures += nav.GetPath(client, TestWorld::MAP_ID, start, end, path) ? 0 : 1;
            }
        });
    }

    for (auto& worker : workers)
    {
        worker.join();
    }

    CHECK_EQ(failures.load(), 0);
    CHECK(nav.GetQueryCount(TestWorld::MAP_ID) <= static_cast<size_t>(threads) + 1);
}
