#pragma once

// Builds synthetic WotLK client data (DBCs, WDT, ADT, WMO, M2) and packs it into MPQ archives, so the exporter
// can be tested end-to-end without a game client. Layouts follow wowdev.wiki / TrinityCore's extractors.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Mpq/MpqManager.hpp"
#include "Wow/AdtStructs.hpp"
#include "Wow/M2.hpp"
#include "Wow/Wdt.hpp"
#include "Wow/Wmo.hpp"
#include "Wow/WmoGroup.hpp"

namespace WowData {
using Bytes = std::vector<uint8_t>;

template <typename T>
void Put(Bytes& out, const T& value)
{
    const auto* p = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

inline void PutBytes(Bytes& out, const Bytes& bytes) { out.insert(out.end(), bytes.begin(), bytes.end()); }

template <typename T>
void PatchAt(Bytes& out, size_t offset, const T& value)
{
    std::memcpy(out.data() + offset, &value, sizeof(T));
}

/// magic (reversed on disk) + uint32 size + payload
inline Bytes Chunk(const char* magic, const Bytes& payload)
{
    Bytes out{static_cast<uint8_t>(magic[3]), static_cast<uint8_t>(magic[2]), static_cast<uint8_t>(magic[1]),
              static_cast<uint8_t>(magic[0])};
    Put(out, static_cast<uint32_t>(payload.size()));
    PutBytes(out, payload);
    return out;
}

inline void SetMagic(unsigned char* dst, const char* magic)
{
    dst[0] = static_cast<unsigned char>(magic[3]);
    dst[1] = static_cast<unsigned char>(magic[2]);
    dst[2] = static_cast<unsigned char>(magic[1]);
    dst[3] = static_cast<unsigned char>(magic[0]);
}

/// Zero terminated strings for DBC string tables / filename chunks.
struct StringTable
{
    std::string data = std::string(1, '\0');

    uint32_t Add(const std::string& s)
    {
        const auto offset = static_cast<uint32_t>(data.size());
        data += s;
        data += '\0';
        return offset;
    }
};

/// WDBC file with fieldCount uint32 fields per record.
inline Bytes MakeDbc(uint32_t fieldCount, const std::vector<std::vector<uint32_t>>& records, const StringTable& strings)
{
    Bytes out{'W', 'D', 'B', 'C'};
    Put(out, static_cast<uint32_t>(records.size()));
    Put(out, fieldCount);
    Put(out, fieldCount * 4u);
    Put(out, static_cast<uint32_t>(strings.data.size()));

    for (const auto& record : records)
    {
        for (uint32_t f = 0; f < fieldCount; ++f)
        {
            Put(out, f < record.size() ? record[f] : 0u);
        }
    }

    out.insert(out.end(), strings.data.begin(), strings.data.end());
    return out;
}

inline Bytes Mver(uint32_t version)
{
    Bytes payload;
    Put(payload, version);
    return Chunk("MVER", payload);
}

/// WDT with the given ADTs, optionally a WMO-only map (MPHD flag 1 + MWMO + MODF).
inline Bytes MakeWdt(const std::vector<std::pair<int, int>>& adts, const std::string& globalWmo = {},
                     const MODF::Entry* globalPlacement = nullptr)
{
    Bytes out = Mver(18);

    Bytes mphd(32, 0);
    const uint32_t flags = globalWmo.empty() ? 0u : 1u;
    PatchAt(mphd, 0, flags);
    PutBytes(out, Chunk("MPHD", mphd));

    Bytes main(64 * 64 * 8, 0);

    for (const auto& [x, y] : adts)
    {
        PatchAt(main, static_cast<size_t>(y * 64 + x) * 8, 1u);
    }

    PutBytes(out, Chunk("MAIN", main));

    if (!globalWmo.empty() && globalPlacement)
    {
        Bytes name(globalWmo.begin(), globalWmo.end());
        name.push_back(0);
        PutBytes(out, Chunk("MWMO", name));

        Bytes modf;
        Put(modf, *globalPlacement);
        PutBytes(out, Chunk("MODF", modf));
    }

    return out;
}

struct AdtSpec
{
    int x = 32;
    int y = 32;
    std::function<float(float wowX, float wowY)> height = [](float, float) { return 0.0f; };

    std::vector<std::string> textures{"tileset\\generic\\grass.blp"};
    std::map<std::pair<int, int>, uint32_t> chunkTexture; // (cx, cy) -> MTEX index, default 0
    std::map<std::pair<int, int>, uint32_t> chunkArea;    // (cx, cy) -> AreaTable id
    std::set<std::pair<int, int>> holes;                  // fully holed chunks

    struct Water
    {
        int cx, cy;
        uint16_t liquidType;                        // LiquidType.dbc id
        std::function<float(int vx, int vy)> height; // per vertex (0..8)
    };
    std::vector<Water> water;

    std::vector<std::string> wmoNames;
    std::vector<MODF::Entry> wmoPlacements;
    std::vector<std::string> doodadNames;
    std::vector<MDDF::Entry> doodadPlacements;
};

/// WoW position of the NW corner of an ADT chunk.
inline std::pair<float, float> ChunkOrigin(int adtX, int adtY, int cx, int cy)
{
    return {(32.0f - adtY) * TILESIZE - cy * CHUNKSIZE, (32.0f - adtX) * TILESIZE - cx * CHUNKSIZE};
}

/// Filename chunk + offset chunk (MMDX/MMID, MWMO/MWID).
inline std::pair<Bytes, Bytes> NameChunks(const char* namesMagic, const char* offsetsMagic,
                                          const std::vector<std::string>& names)
{
    Bytes data;
    Bytes offsets;

    for (const auto& name : names)
    {
        Put(offsets, static_cast<uint32_t>(data.size()));
        data.insert(data.end(), name.begin(), name.end());
        data.push_back(0);
    }

    return {Chunk(namesMagic, data), Chunk(offsetsMagic, offsets)};
}

/// WotLK ADT: MVER, MHDR, MCIN, MTEX, MMDX, MMID, MWMO, MWID, MDDF, MODF, [MH2O], 256 x MCNK (MCVT + MCLY).
inline Bytes MakeAdt(const AdtSpec& spec)
{
    Bytes out = Mver(18);

    const size_t mhdrPos = out.size();
    PutBytes(out, Chunk("MHDR", Bytes(64, 0)));
    const size_t mhdrData = mhdrPos + 8;

    const auto setMhdr = [&](int field, size_t chunkPos) {
        PatchAt(out, mhdrData + static_cast<size_t>(field) * 4, static_cast<uint32_t>(chunkPos - mhdrData));
    };

    const size_t mcinPos = out.size();
    PutBytes(out, Chunk("MCIN", Bytes(256 * 16, 0)));
    setMhdr(1, mcinPos);

    {
        Bytes names;

        for (const auto& t : spec.textures)
        {
            names.insert(names.end(), t.begin(), t.end());
            names.push_back(0);
        }

        setMhdr(2, out.size());
        PutBytes(out, Chunk("MTEX", names));
    }

    {
        auto [mmdx, mmid] = NameChunks("MMDX", "MMID", spec.doodadNames);
        setMhdr(3, out.size());
        PutBytes(out, mmdx);
        setMhdr(4, out.size());
        PutBytes(out, mmid);

        auto [mwmo, mwid] = NameChunks("MWMO", "MWID", spec.wmoNames);
        setMhdr(5, out.size());
        PutBytes(out, mwmo);
        setMhdr(6, out.size());
        PutBytes(out, mwid);

        Bytes mddf;
        for (const auto& e : spec.doodadPlacements)
            Put(mddf, e);
        setMhdr(7, out.size());
        PutBytes(out, Chunk("MDDF", mddf));

        Bytes modf;
        for (const auto& e : spec.wmoPlacements)
            Put(modf, e);
        setMhdr(8, out.size());
        PutBytes(out, Chunk("MODF", modf));
    }

    if (!spec.water.empty())
    {
        // MH2O: 256 headers {offsetInstances, used, offsetAttributes}, then instances and vertex data.
        // All offsets are relative to the start of the MH2O data.
        Bytes mh2o(256 * 12, 0);

        for (const auto& w : spec.water)
        {
            const size_t header = static_cast<size_t>(w.cy * 16 + w.cx) * 12;
            const size_t instanceOffset = mh2o.size();
            PatchAt(mh2o, header, static_cast<uint32_t>(instanceOffset));
            PatchAt(mh2o, header + 4, 1u);

            float minH = 1e9f, maxH = -1e9f;

            for (int vy = 0; vy <= 8; ++vy)
                for (int vx = 0; vx <= 8; ++vx)
                {
                    minH = std::min(minH, w.height(vx, vy));
                    maxH = std::max(maxH, w.height(vx, vy));
                }

            AdtLiquid liquid{};
            liquid.type = w.liquidType;
            liquid.vertexFormat = AdtLiquidVertexFormat::HeightDepth;
            liquid.minHeightLevel = minH;
            liquid.maxHeightLevel = maxH;
            liquid.offsetX = 0;
            liquid.offsetY = 0;
            liquid.width = 8;
            liquid.height = 8;
            liquid.offsetRenderMask = 0; // everything rendered
            liquid.offsetVertexData = static_cast<uint32_t>(instanceOffset + sizeof(AdtLiquid));
            Put(mh2o, liquid);

            // float height[9*9] followed by uint8 depth[9*9]
            for (int vy = 0; vy <= 8; ++vy)
                for (int vx = 0; vx <= 8; ++vx)
                    Put(mh2o, w.height(vx, vy));

            for (int i = 0; i < 81; ++i)
                mh2o.push_back(0x7F);

            while (mh2o.size() % 4)
                mh2o.push_back(0);
        }

        setMhdr(10, out.size());
        PutBytes(out, Chunk("MH2O", mh2o));
    }

    for (int cy = 0; cy < 16; ++cy)
    {
        for (int cx = 0; cx < 16; ++cx)
        {
            const auto [originX, originY] = ChunkOrigin(spec.x, spec.y, cx, cy);
            const auto key = std::make_pair(cx, cy);

            Bytes mcvt;
            for (int j = 0; j < 17; ++j)
            {
                const int unitCount = j % 2 ? 8 : 9;

                for (int i = 0; i < unitCount; ++i)
                {
                    const float wx = originX - j * HALFUNITSIZE;
                    const float wy = originY - i * UNITSIZE - (unitCount == 8 ? HALFUNITSIZE : 0.0f);
                    Put(mcvt, spec.height(wx, wy));
                }
            }

            MCLY_Entry layer{};
            layer.textureId = spec.chunkTexture.contains(key) ? spec.chunkTexture.at(key) : 0u;
            layer.effectId = 0xFFFFFFFF;
            Bytes mcly;
            Put(mcly, layer);

            const Bytes mcvtChunk = Chunk("MCVT", mcvt);
            const Bytes mclyChunk = Chunk("MCLY", mcly);

            MCNK header{};
            SetMagic(header.magic, "MCNK");
            header.size = static_cast<uint32_t>(sizeof(MCNK) - 8 + mcvtChunk.size() + mclyChunk.size());
            header.ix = static_cast<uint32_t>(cx);
            header.iy = static_cast<uint32_t>(cy);
            header.nLayers = 1;
            header.offsMcvt = sizeof(MCNK);
            header.offsMcly = static_cast<uint32_t>(sizeof(MCNK) + mcvtChunk.size());
            header.areaid = spec.chunkArea.contains(key) ? spec.chunkArea.at(key) : 0u;
            header.holes = static_cast<unsigned short>(spec.holes.contains(key) ? 0xFFFF : 0);
            header.x = originX;
            header.y = originY;
            header.z = 0.0f;

            const size_t mcnkPos = out.size();
            Put(out, header);
            PutBytes(out, mcvtChunk);
            PutBytes(out, mclyChunk);

            const size_t cell = mcinPos + 8 + static_cast<size_t>(cy * 16 + cx) * 16;
            PatchAt(out, cell, static_cast<uint32_t>(mcnkPos));
            PatchAt(out, cell + 4, static_cast<uint32_t>(out.size() - mcnkPos));
        }
    }

    return out;
}

/// Horizontal quad (two triangles) as vertices + indices, local WMO/M2 space (x, y horizontal, z up).
inline void AddQuad(std::vector<Vector3>& verts, std::vector<uint16_t>& indices, float x0, float y0, float x1, float y1,
                    float z)
{
    const auto base = static_cast<uint16_t>(verts.size());
    verts.push_back({x0, y0, z});
    verts.push_back({x1, y0, z});
    verts.push_back({x0, y1, z});
    verts.push_back({x1, y1, z});
    indices.insert(indices.end(), {base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2),
                                   static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 1),
                                   static_cast<uint16_t>(base + 3)});
}

/// Closed box (12 triangles).
inline void AddBox(std::vector<Vector3>& verts, std::vector<uint16_t>& indices, Vector3 min, Vector3 max)
{
    const auto base = static_cast<uint16_t>(verts.size());

    for (int k = 0; k < 8; ++k)
    {
        verts.push_back({(k & 1) ? max.x : min.x, (k & 2) ? max.y : min.y, (k & 4) ? max.z : min.z});
    }

    const int faces[12][3] = {{0, 2, 1}, {1, 2, 3}, {4, 5, 6}, {5, 7, 6}, {0, 1, 4}, {1, 5, 4},
                              {2, 6, 3}, {3, 6, 7}, {0, 4, 2}, {2, 4, 6}, {1, 3, 5}, {3, 7, 5}};

    for (const auto& f : faces)
    {
        indices.insert(indices.end(), {static_cast<uint16_t>(base + f[0]), static_cast<uint16_t>(base + f[1]),
                                       static_cast<uint16_t>(base + f[2])});
    }
}

inline Bytes MakeWmoRoot(uint32_t groupCount)
{
    Bytes out = Mver(17);

    MOHD mohd{};
    SetMagic(mohd.magic, "MOHD");
    mohd.size = sizeof(MOHD) - 8;
    mohd.groupCount = groupCount;
    Put(out, mohd);
    return out;
}

inline Bytes MakeWmoGroup(const std::vector<Vector3>& verts, const std::vector<uint16_t>& indices)
{
    Bytes out = Mver(17);

    Bytes mopy;
    for (size_t t = 0; t < indices.size() / 3; ++t)
    {
        mopy.push_back(0x20); // collidable render face
        mopy.push_back(0);    // material
    }

    Bytes movi;
    for (const uint16_t i : indices)
        Put(movi, i);

    Bytes movt;
    for (const auto& v : verts)
        Put(movt, v);

    Bytes subChunks;
    PutBytes(subChunks, Chunk("MOPY", mopy));
    PutBytes(subChunks, Chunk("MOVI", movi));
    PutBytes(subChunks, Chunk("MOVT", movt));

    // The MOGP header is 0x44 bytes, the struct only maps the fields the exporter reads.
    MOGP mogp{};
    SetMagic(mogp.magic, "MOGP");
    mogp.size = static_cast<uint32_t>(ChunkDetail::MOGP_HEADER_SIZE + subChunks.size());
    Put(out, mogp);
    out.resize(out.size() + (ChunkDetail::MOGP_HEADER_SIZE - (sizeof(MOGP) - 8)), 0);
    PutBytes(out, subChunks);
    return out;
}

/// M2 with a collision mesh (the only part the exporter uses).
inline Bytes MakeM2(const std::vector<Vector3>& verts, const std::vector<uint16_t>& indices)
{
    MD20 header{};
    std::memcpy(header.magic, "MD20", 4);
    header.size = 264; // version

    Bytes body;
    const auto base = static_cast<uint32_t>(sizeof(MD20));

    header.countBoundingVertices = static_cast<uint32_t>(verts.size());
    header.offsetBoundingVertices = base;
    for (const auto& v : verts)
        Put(body, v);

    header.countBoundingTriangles = static_cast<uint32_t>(indices.size());
    header.offsetBoundingTriangles = base + static_cast<uint32_t>(body.size());
    for (const uint16_t i : indices)
        Put(body, i);

    while (body.size() % 4)
        body.push_back(0);

    header.countBoundingNormals = static_cast<uint32_t>(indices.size() / 3);
    header.offsetBoundingNormals = base + static_cast<uint32_t>(body.size());
    for (size_t i = 0; i < indices.size() / 3; ++i)
        Put(body, Vector3(0, 0, 1));

    header.boundingRadius = 10.0f;

    Bytes out;
    Put(out, header);
    PutBytes(out, body);
    return out;
}

/// Write an MPQ archive with the given files.
inline bool WriteMpq(const std::filesystem::path& path, const std::vector<std::pair<std::string, Bytes>>& files)
{
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::remove(path);

#if defined(_WIN32) && defined(_UNICODE)
    const std::wstring name = path.wstring();
#else
    const std::string name = path.string();
#endif

    HANDLE mpq = nullptr;

    if (!SFileCreateArchive(name.c_str(), MPQ_CREATE_ARCHIVE_V2 | MPQ_CREATE_LISTFILE,
                            static_cast<DWORD>(files.size() + 16), &mpq))
    {
        return false;
    }

    bool ok = true;

    for (const auto& [archivedName, data] : files)
    {
        HANDLE file = nullptr;

        ok = ok
             && SFileCreateFile(mpq, archivedName.c_str(), 0, static_cast<DWORD>(data.size()), 0,
                                MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING, &file)
             && SFileWriteFile(file, data.data(), static_cast<DWORD>(data.size()), MPQ_COMPRESSION_ZLIB)
             && SFileFinishFile(file);
    }

    return SFileCloseArchive(mpq) && ok;
}
} // namespace WowData
