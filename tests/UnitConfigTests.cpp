#include "TestFramework.hpp"

#include <filesystem>
#include <fstream>

#include "NavServer.hpp"

namespace {
std::filesystem::path TempFile(const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / "anav_tests";
    std::filesystem::create_directories(dir);
    return dir / name;
}
} // namespace

TEST_CASE(Config_SaveLoadRoundTrip)
{
    const auto file = TempFile("roundtrip.cfg");

    AmeisenNavConfig out;
    out.port = 12345;
    out.catmullRomSplineAlpha = 0.25f;
    out.useAnpFileFormat = true;
    out.mmapsPath = "/some/path with spaces/";
    out.preloadMaps = "0,1";
    out.waterCost = 2.5f;
    out.roadCost = 0.5f;
    out.badLiquidCost = 9.0f;
    REQUIRE(out.Save(file));

    AmeisenNavConfig in;
    std::vector<std::string> errors;
    REQUIRE(in.Load(file, &errors));
    CHECK(errors.empty());
    CHECK_EQ(in.port, 12345);
    CHECK_NEAR(in.catmullRomSplineAlpha, 0.25f, 1e-6);
    CHECK(in.useAnpFileFormat);
    CHECK_EQ(in.mmapsPath, std::string("/some/path with spaces/"));
    CHECK_EQ(in.preloadMaps, std::string("0,1"));
    CHECK_NEAR(in.waterCost, 2.5f, 1e-6);
    CHECK_NEAR(in.roadCost, 0.5f, 1e-6);
    CHECK_NEAR(in.badLiquidCost, 9.0f, 1e-6);
}

TEST_CASE(Config_ToleratesCrlfCommentsAndBadValues)
{
    const auto file = TempFile("messy.cfg");

    {
        std::ofstream f(file, std::ios::binary);
        f << "# comment\r\n"
          << "; another comment\r\n"
          << "iPort = 4711\r\n"
          << "sMmapsPath=\"C:\\meshes\\\"\r\n"
          << "fFactionDangerCost=abc\r\n"
          << "iMaxPolyPath=12x\r\n"
          << "bUseAnpFileFormat=true\r\n"
          << "unknownKey=5\r\n"
          << "garbage line without equals\r\n";
    }

    AmeisenNavConfig cfg;
    std::vector<std::string> errors;
    REQUIRE(cfg.Load(file, &errors));

    CHECK_EQ(cfg.port, 4711);
    CHECK_EQ(cfg.mmapsPath, std::string("C:\\meshes\\"));
    CHECK(cfg.useAnpFileFormat);
    CHECK_NEAR(cfg.factionDangerCost, 3.0f, 1e-6); // default kept
    CHECK_EQ(cfg.maxPolyPath, 2048);               // default kept
    CHECK_EQ(errors.size(), size_t{2});
}

TEST_CASE(Config_PreloadMapsParsing)
{
    AmeisenNavConfig cfg;
    cfg.preloadMaps = " 0, 1,,530 ,abc, -4, 571";
    const auto maps = cfg.GetPreloadMaps();
    REQUIRE(maps.size() == 4);
    CHECK_EQ(maps[0], 0);
    CHECK_EQ(maps[1], 1);
    CHECK_EQ(maps[2], 530);
    CHECK_EQ(maps[3], 571);
}

TEST_CASE(Config_ValidationClampsAndRejects)
{
    AmeisenNavConfig cfg;
    cfg.mmapsPath = std::filesystem::temp_directory_path().string();
    cfg.bezierCurvePoints = 0;
    cfg.catmullRomSplineAlpha = 3.0f;
    cfg.factionDangerCost = -1.0f;
    cfg.waterCost = 0.0f;

    std::vector<std::string> errors, warnings;
    NavServer::ValidateConfig(cfg, errors, warnings);
    CHECK(errors.empty());
    CHECK_EQ(warnings.size(), size_t{4});
    CHECK(cfg.waterCost > 0.0f);
    CHECK_EQ(cfg.bezierCurvePoints, 2);
    CHECK_NEAR(cfg.catmullRomSplineAlpha, 1.0f, 1e-6);
    CHECK(cfg.factionDangerCost > 0.0f);

    AmeisenNavConfig bad;
    bad.mmapsPath = "/definitely/not/existing/path";
    bad.port = 70000;
    bad.maxSearchNodes = 0;
    bad.mmapFormat = 7;
    errors.clear();
    warnings.clear();
    NavServer::ValidateConfig(bad, errors, warnings);
    CHECK_EQ(errors.size(), size_t{4});
}

TEST_CASE(Config_MmapFormatMapping)
{
    CHECK(NavServer::ToMmapFormat(-1) == MmapFormat::CUSTOM);
    CHECK(NavServer::ToMmapFormat(0) == MmapFormat::UNKNOWN);
    CHECK(NavServer::ToMmapFormat(1) == MmapFormat::TC335A);
    CHECK(NavServer::ToMmapFormat(2) == MmapFormat::SF548);
}

TEST_CASE(Config_EnvironmentOverrides)
{
    const auto set = [](const char* name, const char* value) {
#ifdef _WIN32
        _putenv_s(name, value);
#else
        setenv(name, value, 1);
#endif
    };

    set("ANAVTEST_iPort", "4711");
    set("ANAVTEST_sMmapsPath", " /meshes ");
    set("ANAVTEST_bUseAnpFileFormat", "true");
    set("ANAVTEST_fWaterCost", "not a number");

    AmeisenNavConfig cfg;
    std::vector<std::string> errors;
    CHECK_EQ(cfg.ApplyEnvironment("ANAVTEST_", &errors), 3);
    CHECK_EQ(cfg.port, 4711);
    CHECK_EQ(cfg.mmapsPath, std::string("/meshes"));
    CHECK(cfg.useAnpFileFormat);
    CHECK_NEAR(cfg.waterCost, 1.6f, 1e-6);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].find("ANAVTEST_fWaterCost") != std::string::npos);

    // Nothing set for another prefix.
    AmeisenNavConfig untouched;
    CHECK_EQ(untouched.ApplyEnvironment("ANAVTEST_NOPE_"), 0);
}
