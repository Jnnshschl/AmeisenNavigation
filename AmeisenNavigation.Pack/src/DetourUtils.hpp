#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

/// RAII helpers for Detour objects.
struct NavMeshDeleter
{
    void operator()(dtNavMesh* mesh) const noexcept { dtFreeNavMesh(mesh); }
};

struct NavMeshQueryDeleter
{
    void operator()(dtNavMeshQuery* query) const noexcept { dtFreeNavMeshQuery(query); }
};

using NavMeshPtr = std::unique_ptr<dtNavMesh, NavMeshDeleter>;
using NavMeshQueryPtr = std::unique_ptr<dtNavMeshQuery, NavMeshQueryDeleter>;

/// Tile data buffer allocated with dtAlloc (compatible with DT_TILE_FREE_DATA).
struct DetourDataDeleter
{
    void operator()(unsigned char* data) const noexcept { dtFree(data); }
};

using DetourDataPtr = std::unique_ptr<unsigned char, DetourDataDeleter>;

/// Largest tile grid accepted from a file (a WoW map is 64x64 ADTs). dtNavMesh::init allocates and clears an
/// array of maxTiles tiles, an absurd value from a corrupt file would exhaust the memory.
constexpr int MAX_NAVMESH_TILES = 1 << 16;

/// Largest serialized tile accepted from a file (real tiles are a few MB at most).
constexpr size_t MAX_TILE_DATA_SIZE = 256u * 1024u * 1024u;

/// Sanity check navmesh parameters read from a file before dtNavMesh::init.
inline bool ValidateNavMeshParams(const dtNavMeshParams& params) noexcept
{
    return std::isfinite(params.orig[0]) && std::isfinite(params.orig[1]) && std::isfinite(params.orig[2])
           && std::isfinite(params.tileWidth) && std::isfinite(params.tileHeight) && params.tileWidth > 0.0f
           && params.tileHeight > 0.0f && params.maxTiles > 0 && params.maxTiles <= MAX_NAVMESH_TILES
           && params.maxPolys > 0;
}

/// Validate a serialized Detour tile before handing it to dtNavMesh::addTile.
///
/// Detour trusts tile data completely: addTile sets up pointers from the header counts and links neighbours by
/// index, queries follow vertex, neighbour, detail mesh, BV tree and off-mesh indices without bounds checks.
/// A corrupt or truncated tile (bit rot, broken download, hostile file) would read and write out of bounds.
/// This checks magic/version, that the buffer holds everything the header claims, every index against its
/// array, and that all coordinates are finite. Cost: one pass over the tile (cheap next to decompression).
inline bool ValidateTileData(const unsigned char* data, size_t size) noexcept
{
    if (!data || size < sizeof(dtMeshHeader) || size > MAX_TILE_DATA_SIZE)
    {
        return false;
    }

    dtMeshHeader hdr;
    std::memcpy(&hdr, data, sizeof(hdr));

    if (hdr.magic != DT_NAVMESH_MAGIC || hdr.version != DT_NAVMESH_VERSION)
    {
        return false;
    }

    // addTile writes links[maxLinkCount - 1], a tile needs at least one polygon and one link slot. Polygon and
    // vertex indices are 16 bit in dtPoly/dtOffMeshConnection.
    if (hdr.polyCount <= 0 || hdr.polyCount > 0xffff || hdr.vertCount <= 0 || hdr.vertCount > 0xffff
        || hdr.maxLinkCount <= 0 || hdr.detailMeshCount < 0 || hdr.detailVertCount < 0 || hdr.detailTriCount < 0
        || hdr.bvNodeCount < 0 || hdr.offMeshConCount < 0 || hdr.offMeshBase < 0
        || hdr.offMeshBase > hdr.polyCount || hdr.offMeshConCount > hdr.polyCount - hdr.offMeshBase)
    {
        return false;
    }

    const auto finite = [](const float* v, int count) {
        for (int i = 0; i < count; ++i)
        {
            if (!std::isfinite(v[i]))
            {
                return false;
            }
        }

        return true;
    };

    if (!finite(hdr.bmin, 3) || !finite(hdr.bmax, 3) || !std::isfinite(hdr.walkableHeight)
        || !std::isfinite(hdr.walkableRadius) || !std::isfinite(hdr.walkableClimb) || !std::isfinite(hdr.bvQuantFactor)
        || hdr.bvQuantFactor < 0.0f || hdr.bmin[0] > hdr.bmax[0] || hdr.bmin[1] > hdr.bmax[1]
        || hdr.bmin[2] > hdr.bmax[2])
    {
        return false;
    }

    // Queries quantize positions inside the tile to 16 bit BV coordinates ((unsigned short)(bvQuantFactor * d)),
    // the tile's extent must stay within that range (real tiles: a few thousand cells). Coordinates beyond +-1e7
    // are garbage (the WoW world spans +-17067).
    const float extent = std::max({hdr.bmax[0] - hdr.bmin[0], hdr.bmax[1] - hdr.bmin[1], hdr.bmax[2] - hdr.bmin[2]});

    if (std::fabs(hdr.bmin[0]) > 1e7f || std::fabs(hdr.bmin[1]) > 1e7f || std::fabs(hdr.bmin[2]) > 1e7f
        || std::fabs(hdr.bmax[0]) > 1e7f || std::fabs(hdr.bmax[1]) > 1e7f || std::fabs(hdr.bmax[2]) > 1e7f
        || (hdr.bvNodeCount > 0 && !(hdr.bvQuantFactor * extent + 1.0f < 65535.0f)))
    {
        return false;
    }

    // Same layout as dtNavMesh::addTile.
    const auto align4 = [](int64_t x) { return (x + 3) & ~static_cast<int64_t>(3); };

    const int64_t headerSize = align4(sizeof(dtMeshHeader));
    const int64_t vertsSize = align4(static_cast<int64_t>(sizeof(float)) * 3 * hdr.vertCount);
    const int64_t polysSize = align4(static_cast<int64_t>(sizeof(dtPoly)) * hdr.polyCount);
    const int64_t linksSize = align4(static_cast<int64_t>(sizeof(dtLink)) * hdr.maxLinkCount);
    const int64_t detailMeshesSize = align4(static_cast<int64_t>(sizeof(dtPolyDetail)) * hdr.detailMeshCount);
    const int64_t detailVertsSize = align4(static_cast<int64_t>(sizeof(float)) * 3 * hdr.detailVertCount);
    const int64_t detailTrisSize = align4(static_cast<int64_t>(sizeof(unsigned char)) * 4 * hdr.detailTriCount);
    const int64_t bvTreeSize = align4(static_cast<int64_t>(sizeof(dtBVNode)) * hdr.bvNodeCount);
    const int64_t offMeshSize = align4(static_cast<int64_t>(sizeof(dtOffMeshConnection)) * hdr.offMeshConCount);

    if (static_cast<int64_t>(size) < headerSize + vertsSize + polysSize + linksSize + detailMeshesSize
                                         + detailVertsSize + detailTrisSize + bvTreeSize + offMeshSize)
    {
        return false;
    }

    // Element access through memcpy: the buffer isn't necessarily aligned for the structs.
    const unsigned char* verts = data + headerSize;
    const unsigned char* polys = verts + vertsSize;
    const unsigned char* detailMeshes = polys + polysSize + linksSize;
    const unsigned char* detailVerts = detailMeshes + detailMeshesSize;
    const unsigned char* detailTris = detailVerts + detailVertsSize;
    const unsigned char* bvTree = detailTris + detailTrisSize;
    const unsigned char* offMeshCons = bvTree + bvTreeSize;

    const auto load = [](const unsigned char* base, int64_t index, auto& out) {
        std::memcpy(&out, base + index * static_cast<int64_t>(sizeof(out)), sizeof(out));
    };

    for (int i = 0; i < hdr.vertCount * 3; ++i)
    {
        float v = 0.0f;
        load(verts, i, v);

        if (!std::isfinite(v))
        {
            return false;
        }
    }

    for (int i = 0; i < hdr.detailVertCount * 3; ++i)
    {
        float v = 0.0f;
        load(detailVerts, i, v);

        if (!std::isfinite(v))
        {
            return false;
        }
    }

    for (int ip = 0; ip < hdr.polyCount; ++ip)
    {
        dtPoly poly{};
        load(polys, ip, poly);

        const unsigned char type = poly.getType();
        const bool offMesh = type == DT_POLYTYPE_OFFMESH_CONNECTION;

        if (type > DT_POLYTYPE_OFFMESH_CONNECTION)
        {
            return false;
        }

        if (offMesh)
        {
            // Off-mesh polys are two point segments, their connection is offMeshCons[ip - offMeshBase].
            if (poly.vertCount != 2 || ip < hdr.offMeshBase || ip - hdr.offMeshBase >= hdr.offMeshConCount)
            {
                return false;
            }
        }
        else if (poly.vertCount < 3 || poly.vertCount > DT_VERTS_PER_POLYGON || ip >= hdr.detailMeshCount)
        {
            return false;
        }

        for (int j = 0; j < poly.vertCount; ++j)
        {
            if (poly.verts[j] >= hdr.vertCount)
            {
                return false;
            }

            // Internal neighbours are stored as index + 1, external ones carry DT_EXT_LINK and a direction.
            const unsigned short nei = poly.neis[j];

            if (nei != 0 && !(nei & DT_EXT_LINK) && (nei - 1 >= hdr.polyCount || nei - 1 == ip))
            {
                return false;
            }
        }

        if (offMesh)
        {
            continue;
        }

        dtPolyDetail detail{};
        load(detailMeshes, ip, detail);

        // Every ground poly needs its detail triangles (height queries and closest point lookups use them).
        if (detail.triCount == 0 || static_cast<int64_t>(detail.vertBase) + detail.vertCount > hdr.detailVertCount
            || static_cast<int64_t>(detail.triBase) + detail.triCount > hdr.detailTriCount)
        {
            return false;
        }

        // Detail triangle indices address the poly's vertices first, then the detail vertices.
        const int maxIndex = poly.vertCount + detail.vertCount;

        for (int t = 0; t < detail.triCount; ++t)
        {
            const unsigned char* tri = detailTris + (static_cast<int64_t>(detail.triBase) + t) * 4;

            if (tri[0] >= maxIndex || tri[1] >= maxIndex || tri[2] >= maxIndex)
            {
                return false;
            }
        }
    }

    // BV tree: leaves reference polygons, internal nodes store a negative escape offset that must stay in the tree.
    for (int n = 0; n < hdr.bvNodeCount; ++n)
    {
        dtBVNode node{};
        load(bvTree, n, node);

        if (node.i >= 0 ? node.i >= hdr.polyCount : -static_cast<int64_t>(node.i) > hdr.bvNodeCount - n)
        {
            return false;
        }
    }

    for (int c = 0; c < hdr.offMeshConCount; ++c)
    {
        dtOffMeshConnection con{};
        load(offMeshCons, c, con);

        if (con.poly != hdr.offMeshBase + c || !finite(con.pos, 6) || !std::isfinite(con.rad))
        {
            return false;
        }
    }

    return true;
}
