#include "TestFramework.hpp"

#include <algorithm>
#include <random>
#include <set>

#include "Mpq/FileSort.hpp"
#include "Utils/Misc.hpp"
#include "Utils/RectGrid.hpp"
#include "Utils/Structure.hpp"
#include "Utils/WaterMap.hpp"

TEST_CASE(MpqOrder_PatchesOverrideBaseArchives)
{
    const std::filesystem::path data = std::filesystem::temp_directory_path() / "anav_tests" / "Data";
    std::filesystem::create_directories(data / "enUS");

    std::vector<std::filesystem::path> archives{
        data / "common.MPQ",          data / "patch-3.MPQ",         data / "expansion.MPQ",
        data / "patch.MPQ",           data / "lichking.MPQ",        data / "patch-2.MPQ",
        data / "common-2.MPQ",        data / "patch-A.MPQ",         data / "patch-4.MPQ",
        data / "enUS/locale-enUS.MPQ", data / "enUS/patch-enUS.MPQ", data / "enUS/patch-enUS-3.MPQ",
        data / "enUS/patch-enUS-2.MPQ",
    };

    std::shuffle(archives.begin(), archives.end(), std::mt19937(42));
    SortMpqsByPriority(archives, data);

    std::vector<std::string> names;

    for (const auto& a : archives)
    {
        names.push_back(a.filename().string());
    }

    const std::vector<std::string> expected{
        "patch-enUS-3.MPQ", "patch-enUS-2.MPQ", "patch-enUS.MPQ", "patch-A.MPQ",    "patch-4.MPQ",
        "patch-3.MPQ",      "patch-2.MPQ",      "patch.MPQ",      "locale-enUS.MPQ", "lichking.MPQ",
        "expansion.MPQ",    "common-2.MPQ",     "common.MPQ",
    };

    CHECK(names == expected);
}

TEST_CASE(MpqOrder_IsAStrictWeakOrdering)
{
    // The old comparator violated strict weak ordering (UB in std::sort). The key based order must be
    // irreflexive and asymmetric for every pair.
    const std::vector<std::string> files{"common.MPQ", "common-2.MPQ", "patch.MPQ", "patch-2.MPQ", "patch-Z.MPQ",
                                         "speech.MPQ", "expansion.MPQ", "foo-17.MPQ", "patch-custom.MPQ"};

    for (const auto& a : files)
    {
        for (const auto& b : files)
        {
            const auto ka = GetMpqLoadOrder(a, false);
            const auto kb = GetMpqLoadOrder(b, false);
            CHECK(!(ka < ka));
            CHECK(!((ka < kb) && (kb < ka)));
        }
    }
}

TEST_CASE(RectGrid_MatchesBruteForceAndReportsOnce)
{
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> pos(-1000.0f, 1000.0f);
    std::uniform_real_distribution<float> size(0.1f, 120.0f);

    struct IdRect : RdRect
    {
        int id;
    };

    RectMap<IdRect> map;

    for (int i = 0; i < 2000; ++i)
    {
        IdRect r;
        r.minX = pos(rng);
        r.minZ = pos(rng);
        r.maxX = r.minX + size(rng);
        r.maxZ = r.minZ + size(rng);
        r.id = i;
        map.Add(r);
    }

    map.BuildSpatialIndex();

    for (int q = 0; q < 300; ++q)
    {
        const float minX = pos(rng), minZ = pos(rng);
        const float maxX = minX + size(rng) * 2.0f, maxZ = minZ + size(rng) * 2.0f;

        std::multiset<int> fromGrid;
        map.Query(minX, minZ, maxX, maxZ, [&](const IdRect& r) { fromGrid.insert(r.id); });

        std::multiset<int> brute;

        for (const auto& r : map.rects)
        {
            if (r.Overlaps(minX, minZ, maxX, maxZ))
            {
                brute.insert(r.id);
            }
        }

        CHECK(fromGrid == brute); // equal multisets = no misses and no duplicates
    }
}

TEST_CASE(WaterMap_CornerHeightMapping)
{
    WaterMap water;
    // WoW NW corner (100, 200), SE corner (90, 190)
    water.AddRect({100.0f, 200.0f, 0.0f}, {90.0f, 190.0f, 0.0f}, 1.0f, 2.0f, 3.0f, 4.0f, LIQUID_OCEAN);
    REQUIRE(water.rects.size() == 1);

    const WaterRect& r = water.rects[0];
    CHECK_NEAR(r.minX, 190.0f - 0.05f, 1e-4); // RD x = WoW y
    CHECK_NEAR(r.maxZ, 100.0f + 0.05f, 1e-4); // RD z = WoW x
    CHECK_NEAR(r.heights[3], 1.0f, 1e-6);     // NW -> maxX, maxZ
    CHECK_NEAR(r.heights[0], 4.0f, 1e-6);     // SE -> minX, minZ
    CHECK_NEAR(r.MaxHeight(), 4.0f, 1e-6);
    CHECK_EQ(static_cast<int>(r.type), static_cast<int>(LIQUID_OCEAN));
}

TEST_CASE(Structure_CleanRemovesDuplicatesAndUnusedVerts)
{
    Structure s;
    s.verts = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 0, 0}, {5, 5, 5}}; // [3] duplicates [0], [4] unused
    s.tris = {Tri{0, 1, 2}, Tri{3, 1, 2}};                           // second is a duplicate after welding
    s.triTypes = {TERRAIN_GROUND, TERRAIN_GROUND};

    s.Clean();

    CHECK_EQ(s.verts.size(), size_t{3});
    CHECK_EQ(s.tris.size(), size_t{1});
    CHECK_EQ(s.triTypes.size(), size_t{1});
}

TEST_CASE(ChunkWalker_FindsNestedChunksAndIgnoresPayloadMatches)
{
    // MVER(4) | MOGP(header 0x44 + MOPY(2) + MOVT(12)) with a fake "MOVI" string inside the MOPY payload.
    std::vector<unsigned char> file;
    const auto addChunk = [&](const char* magic, const std::vector<unsigned char>& payload) {
        file.insert(file.end(), {static_cast<unsigned char>(magic[3]), static_cast<unsigned char>(magic[2]),
                                 static_cast<unsigned char>(magic[1]), static_cast<unsigned char>(magic[0])});
        const uint32_t size = static_cast<uint32_t>(payload.size());
        file.insert(file.end(), reinterpret_cast<const unsigned char*>(&size),
                    reinterpret_cast<const unsigned char*>(&size) + 4);
        file.insert(file.end(), payload.begin(), payload.end());
    };

    addChunk("MVER", {17, 0, 0, 0});

    std::vector<unsigned char> mogp(0x44, 0);
    const auto appendSub = [&](const char* magic, const std::vector<unsigned char>& payload) {
        mogp.insert(mogp.end(), {static_cast<unsigned char>(magic[3]), static_cast<unsigned char>(magic[2]),
                                 static_cast<unsigned char>(magic[1]), static_cast<unsigned char>(magic[0])});
        const uint32_t size = static_cast<uint32_t>(payload.size());
        mogp.insert(mogp.end(), reinterpret_cast<const unsigned char*>(&size),
                    reinterpret_cast<const unsigned char*>(&size) + 4);
        mogp.insert(mogp.end(), payload.begin(), payload.end());
    };

    appendSub("MOPY", {'I', 'V', 'O', 'M'}); // reversed "MOVI" as payload bytes
    appendSub("MOVT", std::vector<unsigned char>(12, 0));
    addChunk("MOGP", mogp);

    unsigned char* data = file.data();
    const auto size = static_cast<unsigned int>(file.size());

    struct Any
    {
        unsigned char magic[4];
        uint32_t size;
    };

    const Any* movt = FindSubChunk<Any>(data, size, "MOVT");
    REQUIRE(movt != nullptr);
    CHECK_EQ(movt->size, 12u);
    CHECK(FindSubChunk<Any>(data, size, "MOPY") != nullptr);
    CHECK(FindSubChunk<Any>(data, size, "MOVI") == nullptr); // payload bytes must not match
}
