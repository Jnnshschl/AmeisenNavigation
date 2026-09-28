// Writes seed corpora for the fuzzers: FuzzCorpus <output dir>
// Creates <dir>/{tile,anp,exporter,protocol} from the synthetic test world and synthetic client files.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../TestWorld.hpp"
#include "../WowDataBuilder.hpp"
#include "Anp.hpp"
#include "Protocol.hpp"

namespace {
namespace fs = std::filesystem;
using WowData::Bytes;

void Write(const fs::path& file, const void* data, size_t size)
{
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
}

void Write(const fs::path& file, const Bytes& data) { Write(file, data.data(), data.size()); }

Bytes Prefixed(uint8_t mode, const Bytes& data)
{
    Bytes out{mode};
    WowData::PutBytes(out, data);
    return out;
}

void AddSized(Bytes& out, const Bytes& chunk)
{
    out.push_back(static_cast<uint8_t>(chunk.size() & 0xFF));
    out.push_back(static_cast<uint8_t>((chunk.size() >> 8) & 0xFF));
    WowData::PutBytes(out, chunk);
}

template <typename T>
void AddPacket(Bytes& out, MessageType type, const T& payload)
{
    out.push_back(static_cast<uint8_t>(type));
    out.push_back(static_cast<uint8_t>(sizeof(T) & 0xFF));
    out.push_back(static_cast<uint8_t>(sizeof(T) >> 8));
    WowData::Put(out, payload);
}

void TileSeeds(const fs::path& dir)
{
    const fs::path anp = TestWorld::Get().meshDir / Anp::FileName(TestWorld::MAP_ID);
    std::ifstream in(anp, std::ios::binary);
    const std::vector<unsigned char> archive((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Write(dir / "anp" / "world.anp", archive.data(), archive.size());

    mz_zip_archive zip{};

    if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0))
    {
        return;
    }

    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i)
    {
        char name[64]{};
        int x = 0;
        int y = 0;

        if (mz_zip_reader_get_filename(&zip, i, name, sizeof(name)) > 0 && Anp::ParseTileEntryName(name, x, y))
        {
            size_t size = 0;

            if (void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0))
            {
                Write(dir / "tile" / name, data, size);
                mz_free(data);
            }
        }
    }

    mz_zip_reader_end(&zip);
}

void ExporterSeeds(const fs::path& dir)
{
    using namespace WowData;

    AdtSpec adt;
    adt.height = [](float x, float y) { return 0.01f * x + 0.02f * y; };
    adt.textures = {"tileset\\grass.blp", "tileset\\road.blp"};
    adt.chunkTexture[{1, 1}] = 1;
    adt.chunkArea[{2, 2}] = 1;
    adt.chunkArea[{3, 3}] = 3;
    adt.holes.insert({4, 4});
    adt.water.push_back({5, 5, 2, [](int vx, int vy) { return 3.0f + 0.1f * static_cast<float>(vx + vy); }});
    adt.wmoNames = {"f.wmo"};
    MODF::Entry wmo{};
    wmo.x = WORLDSIZE;
    wmo.z = WORLDSIZE;
    wmo.uniqueId = 1;
    adt.wmoPlacements = {wmo};
    adt.doodadNames = {"d.mdx"};
    MDDF::Entry doodad{};
    doodad.x = WORLDSIZE;
    doodad.z = WORLDSIZE;
    doodad.scale = 1024;
    adt.doodadPlacements = {doodad};
    Write(dir / "exporter" / "adt", Prefixed(0, MakeAdt(adt)));

    // WMO root with a doodad set referencing "d.mdx", one group with a floor, an M2 box.
    std::vector<Vector3> verts;
    std::vector<uint16_t> indices;
    AddQuad(verts, indices, -10.0f, -10.0f, 10.0f, 10.0f, 0.0f);
    const Bytes group = MakeWmoGroup(verts, indices);

    Bytes root = MakeWmoRoot(1);
    Bytes mods(32, 0);
    PatchAt(mods, 24, 1u); // set 0: start 0, count 1
    PutBytes(root, Chunk("MODS", mods));
    Bytes modn{'d', '.', 'm', 'd', 'x', 0, 0, 0};
    PutBytes(root, Chunk("MODN", modn));
    Bytes modd;
    Put(modd, 0u);
    Put(modd, Vector3(1.0f, 2.0f, 3.0f));
    Put(modd, 0.0f);
    Put(modd, 0.0f);
    Put(modd, 0.0f);
    Put(modd, 1.0f);
    Put(modd, 1.0f);
    Put(modd, 0u);
    PutBytes(root, Chunk("MODD", modd));

    std::vector<Vector3> boxVerts;
    std::vector<uint16_t> boxIndices;
    AddBox(boxVerts, boxIndices, {-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 2.0f});
    const Bytes m2 = MakeM2(boxVerts, boxIndices);

    Bytes wmoSeed;
    AddSized(wmoSeed, root);
    AddSized(wmoSeed, group);
    PutBytes(wmoSeed, m2);
    Write(dir / "exporter" / "wmo", Prefixed(1, wmoSeed));
    Write(dir / "exporter" / "m2", Prefixed(2, m2));

    Write(dir / "exporter" / "wdt", Prefixed(3, MakeWdt({{32, 32}, {33, 32}})));
    MODF::Entry global{};
    Write(dir / "exporter" / "wdt_global", Prefixed(3, MakeWdt({}, "f.wmo", &global)));

    StringTable strings;
    Write(dir / "exporter" / "dbc",
          Prefixed(4, MakeDbc(3, {{1, strings.Add("Kalimdor"), 7}, {2, strings.Add("Azeroth"), 8}}, strings)));
}

void ProtocolSeeds(const fs::path& dir)
{
    const auto wow = [](float x, float y, float z) { return TestWorld::Wow(x, y, z); };
    const int map = TestWorld::MAP_ID;
    const Vector3 a = wow(-700.0f, 0.0f, -300.0f);
    const Vector3 b = wow(-200.0f, 0.0f, -300.0f);

    Bytes all;
    AddPacket(all, MessageType::PATH, PathRequestData{map, 2 | 16, a, b});
    AddPacket(all, MessageType::RANDOM_PATH, PathRequestData{map, 32, a, b});
    AddPacket(all, MessageType::MOVE_ALONG_SURFACE, MoveRequestData{map, a, b});
    AddPacket(all, MessageType::RANDOM_POINT, map);
    AddPacket(all, MessageType::RANDOM_POINT_AROUND, RandomPointAroundData{map, a, 20.0f});
    AddPacket(all, MessageType::CAST_RAY, CastRayData{map, a, b});
    AddPacket(all, MessageType::CAST_RAY_EX, CastRayData{map, a, b});
    AddPacket(all, MessageType::GET_HEIGHT, GetHeightData{map, a});
    Write(dir / "protocol" / "requests", all);

    Bytes explore;
    ExplorePolyRequestHeader header{map, 0, a, 40.0f, 4};
    Bytes payload;
    WowData::Put(payload, header);

    for (const Vector3& p : {wow(-390.0f, 0.0f, -450.0f), wow(-210.0f, 0.0f, -450.0f), wow(-210.0f, 0.0f, -150.0f),
                             wow(-390.0f, 0.0f, -150.0f)})
    {
        WowData::Put(payload, p);
    }

    explore.push_back(static_cast<uint8_t>(MessageType::EXPLORE_POLY));
    explore.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
    explore.push_back(static_cast<uint8_t>(payload.size() >> 8));
    WowData::PutBytes(explore, payload);
    Write(dir / "protocol" / "explore", explore);

    Bytes filter;
    Bytes filterPayload;
    WowData::Put(filterPayload, ConfigureFilterHeader{ClientState::NORMAL_HORDE, 2});
    WowData::Put(filterPayload, FilterConfig{TERRAIN_ROAD, 0.5f});
    WowData::Put(filterPayload, FilterConfig{LIQUID_WATER, 5.0f});
    filter.push_back(static_cast<uint8_t>(MessageType::CONFIGURE_FILTER));
    filter.push_back(static_cast<uint8_t>(filterPayload.size()));
    filter.push_back(0);
    WowData::PutBytes(filter, filterPayload);
    AddPacket(filter, MessageType::PATH, PathRequestData{map, 0, a, b});
    filter.push_back(static_cast<uint8_t>(MessageType::GET_CONFIG));
    filter.push_back(0);
    filter.push_back(0);
    Write(dir / "protocol" / "filter_config", filter);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("Usage: %s <output dir>\n", argv[0]);
        return 1;
    }

    Logger::SetQuiet(true);
    const fs::path dir = argv[1];
    TileSeeds(dir);
    ExporterSeeds(dir);
    ProtocolSeeds(dir);
    std::printf("Seed corpora written to %s\n", dir.string().c_str());
    return 0;
}
