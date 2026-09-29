// Standalone benchmark (not part of ctest):
//   anav_benchmark [adtsPerAxis=2] [pathQueries=20000]
//
// Builds a synthetic terrain with ADT-like triangle density at production resolution through the real exporter
// pipeline, then measures path query throughput of the navigation engine.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>

#include "AmeisenNavigation.hpp"
#include "Processors/AdtTileProcessor.hpp"
#include "Utils/Logger.hpp"

namespace {
using Clock = std::chrono::steady_clock;

double Seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

/// Rolling hills sampled on a 128x128 quad grid per ADT (32768 triangles, close to a real ADT's 65536 with
/// half resolution) plus box shaped obstacles standing in for WMOs/doodads.
void GenerateWorld(Structure& s, int adts, std::mt19937& rng)
{
    constexpr int GRID = 128;
    const float step = TILESIZE / GRID;
    const int total = GRID * adts;
    const float originX = -static_cast<float>(adts) * TILESIZE;
    const float originZ = -static_cast<float>(adts) * TILESIZE;

    const auto height = [](float x, float z) {
        return 6.0f * std::sin(x * 0.013f) * std::cos(z * 0.017f) + 2.0f * std::sin(x * 0.07f + z * 0.05f);
    };

    for (int z = 0; z <= total; ++z)
    {
        for (int x = 0; x <= total; ++x)
        {
            const float px = originX + x * step;
            const float pz = originZ + z * step;
            s.verts.push_back({px, height(px, pz), pz});
        }
    }

    for (int z = 0; z < total; ++z)
    {
        for (int x = 0; x < total; ++x)
        {
            const int a = z * (total + 1) + x;
            const int b = a + 1;
            const int c = a + (total + 1);
            const int d = c + 1;
            s.AddTri(Tri{a, c, b}, TERRAIN_GROUND);
            s.AddTri(Tri{b, c, d}, TERRAIN_GROUND);
        }
    }

    std::uniform_real_distribution<float> posX(originX, 0.0f);
    std::uniform_real_distribution<float> size(2.0f, 15.0f);

    for (int i = 0; i < 300 * adts * adts; ++i)
    {
        const float x0 = posX(rng), z0 = posX(rng);
        const float x1 = x0 + size(rng), z1 = z0 + size(rng);
        const float y0 = height(x0, z0) - 1.0f, y1 = y0 + size(rng);

        const int base = static_cast<int>(s.verts.size());
        for (int k = 0; k < 8; ++k)
        {
            s.verts.push_back({(k & 1) ? x1 : x0, (k & 2) ? y1 : y0, (k & 4) ? z1 : z0});
        }

        const int faces[12][3] = {{0, 4, 1}, {1, 4, 5}, {2, 3, 6}, {3, 7, 6}, {0, 2, 4}, {2, 6, 4},
                                  {1, 5, 3}, {3, 5, 7}, {0, 1, 2}, {1, 3, 2}, {4, 6, 5}, {5, 6, 7}};

        for (const auto& f : faces)
        {
            s.AddTri(Tri{base + f[0], base + f[1], base + f[2]}, WMO);
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    Logger::Initialize();
    const int adts = argc > 1 ? std::max(1, std::atoi(argv[1])) : 2;
    const int queries = argc > 2 ? std::max(1, std::atoi(argv[2])) : 20000;

    std::mt19937 rng(7);
    Structure world;
    GenerateWorld(world, adts, rng);

    std::printf("World: %dx%d ADTs, %zu verts, %zu tris\n", adts, adts, world.verts.size(), world.tris.size());

    // Lower bound of the old pipeline's overhead: it handed *all* map triangles to every sub-tile's
    // rasterizer, which at least bounds-checks each of them. Measure one such pass.
    {
        const auto start = Clock::now();
        size_t overlapping = 0;
        const float bmin[2]{-100.0f, -100.0f}, bmax[2]{-83.0f, -83.0f};

        for (const auto& t : world.tris)
        {
            const Vector3& a = world.verts[static_cast<size_t>(t.a)];
            const Vector3& b = world.verts[static_cast<size_t>(t.b)];
            const Vector3& c = world.verts[static_cast<size_t>(t.c)];
            const float minX = std::min({a.x, b.x, c.x}), maxX = std::max({a.x, b.x, c.x});
            const float minZ = std::min({a.z, b.z, c.z}), maxZ = std::max({a.z, b.z, c.z});
            overlapping += (maxX >= bmin[0] && minX <= bmax[0] && maxZ >= bmin[1] && minZ <= bmax[1]) ? 1 : 0;
        }

        const double scan = Seconds(start);
        const double subTiles = static_cast<double>(adts) * adts * 32 * 32;
        std::printf("Old pipeline culling lower bound: %.3f ms per sub-tile x %.0f sub-tiles = %.1f s (%zu hits)\n",
                    scan * 1000.0, subTiles, scan * subTiles, overlapping);
    }

    const auto dir = std::filesystem::temp_directory_path() / "anav_bench";
    std::filesystem::create_directories(dir);

    dtNavMeshParams params{};
    params.orig[0] = -static_cast<float>(adts) * TILESIZE;
    params.orig[2] = -static_cast<float>(adts) * TILESIZE;
    params.tileWidth = params.tileHeight = TILESIZE;
    params.maxTiles = 64 * 64;
    params.maxPolys = 1 << 20;

    std::vector<TileCoord> tiles;

    for (int y = 0; y < adts; ++y)
    {
        for (int x = 0; x < adts; ++x)
        {
            tiles.push_back({x, y, 32 - adts + x, 32 - adts + y});
        }
    }

    {
        WaterMap water;
        RoadMap roads;
        const auto start = Clock::now();
        Anp::AnpWriter writer(0, params);
        AdtTileProcessor processor(&writer, dir, "Bench");
        processor.Process(&world, tiles, &water, &roads);
        writer.Save(dir);
        std::printf("Navmesh build (production resolution): %.2f s for %zu tiles, %.2f s per tile\n", Seconds(start),
                    tiles.size(), Seconds(start) / static_cast<double>(tiles.size()));
    }

    AmeisenNavigationSettings settings;
    settings.meshFolder = dir;
    settings.useAnp = true;
    AmeisenNavigation nav(settings);

    {
        const auto start = Clock::now();
        nav.PreloadMap(0);
        std::printf("Load .anp: %.1f ms\n", Seconds(start) * 1000.0);
    }

    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());

    for (const unsigned workers : {1u, threads})
    {
        std::atomic<int> ok{0};
        const auto start = Clock::now();
        std::vector<std::thread> pool;

        for (unsigned w = 0; w < workers; ++w)
        {
            nav.NewClient(100 + w);
            pool.emplace_back([&, w]() {
                std::mt19937 local(w);
                std::uniform_real_distribution<float> pos(-static_cast<float>(adts) * TILESIZE + 5.0f, -5.0f);
                Path path(512);

                for (int i = static_cast<int>(w); i < queries; i += static_cast<int>(workers))
                {
                    const Vector3 a(pos(local), pos(local), 10.0f), b(pos(local), pos(local), 10.0f);
                    Vector3 sa, sb;
                    nav.GetHeight(100 + w, 0, a, sa);
                    nav.GetHeight(100 + w, 0, b, sb);
                    ok += nav.GetPath(100 + w, 0, sa, sb, path) ? 1 : 0;
                }
            });
        }

        for (auto& t : pool)
        {
            t.join();
        }

        const double seconds = Seconds(start);
        std::printf("Path queries (%u thread%s): %d/%d ok, %.0f paths/s, %.1f us avg\n", workers,
                    workers == 1 ? "" : "s", ok.load(), queries, queries / seconds, seconds * 1e6 / queries * workers);
    }

    return 0;
}
