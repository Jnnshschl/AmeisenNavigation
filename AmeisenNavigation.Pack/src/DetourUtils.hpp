#pragma once

#include <cstddef>
#include <cstdint>
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

/// Validate a serialized Detour tile before handing it to dtNavMesh::addTile.
/// addTile trusts the header counts and sets up internal pointers from them, so a truncated or
/// corrupt tile would make Detour read past the buffer. This checks magic, version and that the
/// buffer is large enough for everything the header claims.
inline bool ValidateTileData(const unsigned char* data, size_t size) noexcept
{
    if (!data || size < sizeof(dtMeshHeader))
    {
        return false;
    }

    const auto* hdr = reinterpret_cast<const dtMeshHeader*>(data);

    if (hdr->magic != DT_NAVMESH_MAGIC || hdr->version != DT_NAVMESH_VERSION)
    {
        return false;
    }

    if (hdr->vertCount < 0 || hdr->polyCount < 0 || hdr->maxLinkCount < 0 || hdr->detailMeshCount < 0
        || hdr->detailVertCount < 0 || hdr->detailTriCount < 0 || hdr->bvNodeCount < 0 || hdr->offMeshConCount < 0)
    {
        return false;
    }

    const auto align4 = [](int64_t x) { return (x + 3) & ~static_cast<int64_t>(3); };

    const int64_t required = align4(sizeof(dtMeshHeader))
                             + align4(static_cast<int64_t>(sizeof(float)) * 3 * hdr->vertCount)
                             + align4(static_cast<int64_t>(sizeof(dtPoly)) * hdr->polyCount)
                             + align4(static_cast<int64_t>(sizeof(dtLink)) * hdr->maxLinkCount)
                             + align4(static_cast<int64_t>(sizeof(dtPolyDetail)) * hdr->detailMeshCount)
                             + align4(static_cast<int64_t>(sizeof(float)) * 3 * hdr->detailVertCount)
                             + align4(static_cast<int64_t>(sizeof(unsigned char)) * 4 * hdr->detailTriCount)
                             + align4(static_cast<int64_t>(sizeof(dtBVNode)) * hdr->bvNodeCount)
                             + align4(static_cast<int64_t>(sizeof(dtOffMeshConnection)) * hdr->offMeshConCount);

    return static_cast<int64_t>(size) >= required;
}
