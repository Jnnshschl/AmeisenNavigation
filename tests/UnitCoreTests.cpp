#include "TestFramework.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "AmeisenNavigation.hpp"
#include "Helpers/Polygon.hpp"

namespace {
std::filesystem::path TempDir(const char* name)
{
    auto dir = std::filesystem::temp_directory_path() / "anav_tests" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}
} // namespace

TEST_CASE(Vector3_CoordinateConversionsRoundTrip)
{
    const Vector3 wow(1.0f, 2.0f, 3.0f);

    Vector3 rd = wow;
    rd.ToRDCoords();
    CHECK(rd == Vector3(2.0f, 3.0f, 1.0f)); // (wowY, wowZ, wowX)

    Vector3 copy;
    wow.CopyToRDCoords(copy);
    CHECK(copy == rd);

    rd.ToWowCoords();
    CHECK(rd == wow);

    // In place copy (output aliases input) must work too.
    Vector3 alias = wow;
    alias.CopyToRDCoords(alias);
    CHECK(alias == Vector3(2.0f, 3.0f, 1.0f));
    alias.CopyToWowCoords(alias);
    CHECK(alias == wow);
}

TEST_CASE(Path_CapacityAndUniqueAppend)
{
    Path path(3);
    CHECK(path.TryAppend({1, 1, 1}));
    CHECK(path.TryAppendUnique({1, 1, 1}));
    CHECK_EQ(path.pointCount, 1);
    CHECK(path.TryAppend({2, 2, 2}));
    CHECK(path.TryAppend({3, 3, 3}));
    CHECK(path.IsFull());
    CHECK(!path.TryAppend({4, 4, 4}));
    CHECK_EQ(path.pointCount, 3);
    path.Clear();
    CHECK(path.Empty());
}

TEST_CASE(AnpFormat_FactionHelpers)
{
    CHECK_EQ(GetNeutralArea(HORDE_TERRAIN_ROAD), TERRAIN_ROAD);
    CHECK(GetAreaFaction(ALLIANCE_WMO) == AreaFaction::Alliance);
    CHECK(GetAreaFaction(LIQUID_OCEAN) == AreaFaction::Neutral);
    CHECK_EQ(WithFaction(LIQUID_WATER, AreaFaction::Horde), HORDE_LIQUID_WATER);
    CHECK_EQ(AreaToPolyFlags(TERRAIN_CITY), NAV_GROUND);
    CHECK_EQ(AreaToPolyFlags(HORDE_LIQUID_OCEAN), NAV_WATER | NAV_HORDE);
    CHECK_EQ(AreaToPolyFlags(0), NAV_EMPTY);
    CHECK_EQ(AreaToPolyFlags(63), NAV_EMPTY);

    for (unsigned int area = TERRAIN_GROUND; area < ANP_AREA_COUNT; ++area)
    {
        CHECK(IsValidAnpArea(area));
        CHECK_EQ(WithFaction(GetNeutralArea(area), GetAreaFaction(area)), area);
    }
}

TEST_CASE(Anp_TileEntryNames)
{
    int x = -1, y = -1;
    CHECK(Anp::ParseTileEntryName("07_42", x, y));
    CHECK_EQ(x, 7);
    CHECK_EQ(y, 42);
    CHECK(Anp::ParseTileEntryName(Anp::TileEntryName(3, 250).c_str(), x, y));
    CHECK_EQ(y, 250);
    CHECK(!Anp::ParseTileEntryName("mapId", x, y));
    CHECK(!Anp::ParseTileEntryName("params", x, y));
    CHECK(!Anp::ParseTileEntryName("12_", x, y));
    CHECK(!Anp::ParseTileEntryName("_12", x, y));
    CHECK(!Anp::ParseTileEntryName("1_2_3", x, y));
    CHECK(!Anp::ParseTileEntryName("", x, y));
}

TEST_CASE(DetourUtils_RejectsCorruptTiles)
{
    std::vector<unsigned char> garbage(4096, 0xAB);
    CHECK(!ValidateTileData(garbage.data(), garbage.size()));
    CHECK(!ValidateTileData(nullptr, 0));

    dtMeshHeader header{};
    header.magic = DT_NAVMESH_MAGIC;
    header.version = DT_NAVMESH_VERSION;
    header.vertCount = 1000; // claims far more data than provided
    std::vector<unsigned char> truncated(sizeof(dtMeshHeader) + 64);
    std::memcpy(truncated.data(), &header, sizeof(header));
    CHECK(!ValidateTileData(truncated.data(), truncated.size()));

    header.vertCount = -5;
    std::memcpy(truncated.data(), &header, sizeof(header));
    CHECK(!ValidateTileData(truncated.data(), truncated.size()));
}

TEST_CASE(FilterProvider_Tc335aCostsUseAreaIds)
{
    MmapQueryFilterProvider provider(MmapFormat::TC335A, 1.3f, 4.0f);
    const dtQueryFilter* normal = provider.Get(ClientState::NORMAL);
    REQUIRE(normal != nullptr);

    // TrinityCore area ids: water = 9, magma/slime = 8 (the flags are 4 and 8).
    CHECK_NEAR(normal->getAreaCost(static_cast<int>(NavArea335a::WATER)), 1.3f, 1e-6);
    CHECK_NEAR(normal->getAreaCost(static_cast<int>(NavArea335a::MAGMA_SLIME)), 4.0f, 1e-6);
    CHECK_NEAR(normal->getAreaCost(static_cast<int>(NavArea335a::GROUND)), 1.0f, 1e-6);
    CHECK_NEAR(normal->getAreaCost(4), 1.0f, 1e-6); // flag value, not an area id
    CHECK_EQ(normal->getExcludeFlags(), static_cast<unsigned short>(NavFlag335a::GROUND_STEEP));

    const dtQueryFilter* dead = provider.Get(ClientState::DEAD);
    REQUIRE(dead != nullptr);
    CHECK_NEAR(dead->getAreaCost(static_cast<int>(NavArea335a::MAGMA_SLIME)), 1.0f, 1e-6);

    CHECK(provider.Get(static_cast<ClientState>(42)) == nullptr);
}

TEST_CASE(FilterProvider_AnpFactionCosts)
{
    AnpQueryFilterProvider provider(1.6f, 4.0f, 0.75f, 3.0f);

    const dtQueryFilter* alliance = provider.Get(ClientState::NORMAL_ALLIANCE);
    const dtQueryFilter* horde = provider.Get(ClientState::NORMAL_HORDE);
    REQUIRE(alliance && horde);

    CHECK_NEAR(alliance->getAreaCost(HORDE_TERRAIN_ROAD), 0.75f * 3.0f, 1e-5);
    CHECK_NEAR(alliance->getAreaCost(ALLIANCE_TERRAIN_ROAD), 0.75f, 1e-5);
    CHECK_NEAR(horde->getAreaCost(ALLIANCE_LIQUID_WATER), 1.6f * 3.0f, 1e-5);
    // Bad liquids are expensive regardless of faction.
    CHECK_NEAR(horde->getAreaCost(ALLIANCE_LIQUID_LAVA), 4.0f, 1e-5);
    CHECK_NEAR(provider.Get(ClientState::DEAD)->getAreaCost(LIQUID_SLIME), 1.0f, 1e-5);
}

TEST_CASE(NavClient_FilterValidation)
{
    AnpQueryFilterProvider provider;
    AmeisenNavClient client(1, &provider, 64, 32);

    CHECK(client.QueryFilter() == provider.Get(ClientState::NORMAL));

    const AreaCost valid[] = {{TERRAIN_ROAD, 0.5f}, {LIQUID_WATER, 10.0f}};
    CHECK(client.ConfigureQueryFilter(ClientState::NORMAL_HORDE, valid));
    CHECK(client.HasCustomFilter());
    CHECK(client.GetClientState() == ClientState::NORMAL_HORDE);
    CHECK_NEAR(client.QueryFilter()->getAreaCost(TERRAIN_ROAD), 0.5f, 1e-6);
    // Unconfigured areas keep the state's defaults.
    CHECK_NEAR(client.QueryFilter()->getAreaCost(ALLIANCE_TERRAIN_GROUND), 3.0f, 1e-6);

    // Out of range area ids would write past dtQueryFilter::m_areaCost.
    const AreaCost badArea[] = {{200, 1.0f}};
    CHECK(!client.ConfigureQueryFilter(ClientState::NORMAL, badArea));
    CHECK(client.GetClientState() == ClientState::NORMAL_HORDE); // unchanged

    const AreaCost badCost[] = {{TERRAIN_GROUND, -1.0f}};
    CHECK(!client.ConfigureQueryFilter(ClientState::NORMAL, badCost));

    const AreaCost nanCost[] = {{TERRAIN_GROUND, std::nanf("")}};
    CHECK(!client.ConfigureQueryFilter(ClientState::NORMAL, nanCost));

    CHECK(!client.ConfigureQueryFilter(static_cast<ClientState>(17), {}));

    // Empty override list resets to the provider filter.
    CHECK(client.ConfigureQueryFilter(ClientState::DEAD, {}));
    CHECK(!client.HasCustomFilter());
    CHECK(client.QueryFilter() == provider.Get(ClientState::DEAD));
}

TEST_CASE(NavMeshCache_LoadsOncePerMapUnderContention)
{
    NavMeshCache cache;
    std::atomic<int> loads{0};

    const auto loader = [&](int) -> NavMeshPtr {
        loads++;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        NavMeshPtr mesh(dtAllocNavMesh());
        dtNavMeshParams params{};
        params.tileWidth = params.tileHeight = 100.0f;
        params.maxTiles = 4;
        params.maxPolys = 16;
        mesh->init(&params);
        return mesh;
    };

    std::vector<std::thread> threads;
    std::atomic<dtNavMesh*> seen{nullptr};
    std::atomic<bool> mismatch{false};

    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&]() {
            dtNavMesh* mesh = cache.GetOrLoad(1, loader);
            dtNavMesh* expected = nullptr;

            if (!seen.compare_exchange_strong(expected, mesh) && expected != mesh)
            {
                mismatch = true;
            }
        });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    CHECK_EQ(loads.load(), 1);
    CHECK(!mismatch.load());
    CHECK(seen.load() != nullptr);

    // Failed loads are remembered.
    int failedLoads = 0;
    CHECK(cache.GetOrLoad(2, [&](int) -> NavMeshPtr { failedLoads++; return nullptr; }) == nullptr);
    CHECK(cache.GetOrLoad(2, [&](int) -> NavMeshPtr { failedLoads++; return nullptr; }) == nullptr);
    CHECK_EQ(failedLoads, 1);
}

TEST_CASE(MmapNavSource_DetectsFormatFromFileNames)
{
    const auto tc = TempDir("mmap_tc");
    std::ofstream(tc / "0003228.mmtile") << "x";
    CHECK(MmapNavSource::DetectFormat(tc) == MmapFormat::TC335A);

    const auto sf = TempDir("mmap_sf");
    std::ofstream(sf / "0000_32_28.mmtile") << "x";
    CHECK(MmapNavSource::DetectFormat(sf) == MmapFormat::SF548);

    const auto empty = TempDir("mmap_empty");
    CHECK(MmapNavSource::DetectFormat(empty) == MmapFormat::UNKNOWN);

    // Missing map files are handled gracefully.
    MmapNavSource source(tc, MmapFormat::TC335A);
    CHECK(source.Get(0) == nullptr);
}

TEST_CASE(Polygon_PoissonSamplingStaysInsideAndSpaced)
{
    const Vector3 square[] = {{0, 0, 0}, {100, 0, 0}, {100, 100, 0}, {0, 100, 0}};
    constexpr int capacity = 512;
    std::vector<Vector3> points(capacity);
    std::vector<Vector3> temp(capacity);
    int count = 0;

    PolygonMath::BridsonsPoissonDiskSampling(square, 4, points.data(), &count, temp.data(), capacity, 10.0f);

    CHECK(count > 20);
    CHECK(count <= capacity);

    for (int i = 0; i < count; ++i)
    {
        CHECK(PolygonMath::IsInside2D(square, 4, points[i]));

        for (int j = i + 1; j < count; ++j)
        {
            const float dx = points[i].x - points[j].x;
            const float dy = points[i].y - points[j].y;
            CHECK(dx * dx + dy * dy >= 100.0f - 1e-3f);
        }
    }

    // Respects small capacities.
    PolygonMath::BridsonsPoissonDiskSampling(square, 4, points.data(), &count, temp.data(), 5, 1.0f);
    CHECK(count <= 5);
}
