using AmeisenNavigation.Tester.Converters;
using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Media;

namespace AmeisenNavigation.Tester.Services
{
    /// <summary>
    /// Pre-built frozen geometry for one area within a navmesh tile.
    /// Stored in pixel coordinates (WowCoordinateConverter.WorldToPixel space)
    /// so rendering only needs a zoom+pan transform - no per-vertex math.
    /// </summary>
    public readonly struct AreaGeometryEntry
    {
        public readonly byte AreaId;
        public readonly StreamGeometry Geometry;

        public AreaGeometryEntry(byte areaId, StreamGeometry geometry)
        {
            AreaId = areaId;
            Geometry = geometry;
        }
    }

    /// <summary>
    /// Parsed navmesh tile data with pre-built frozen StreamGeometry per area.
    /// Ready for immediate rendering with a single DrawGeometry call per area.
    /// </summary>
    public sealed class NavmeshTileData
    {
        public readonly AreaGeometryEntry[] AreaGeometries;

        public NavmeshTileData(AreaGeometryEntry[] areaGeometries)
        {
            AreaGeometries = areaGeometries;
        }
    }

    /// <summary>
    /// Parses raw Detour tile binary data to extract polygon geometry and area IDs
    /// for visualization. Converts vertices to pixel coordinates and builds one
    /// frozen StreamGeometry per area.
    /// </summary>
    public static class DetourTileParser
    {
        // dtMeshHeader constants
        private const int HeaderSize = 100;
        private const int PolySize = 32;
        private const int VertSize = 12;

        // dtMeshHeader field offsets
        private const int OFF_POLY_COUNT = 6 * 4;
        private const int OFF_VERT_COUNT = 7 * 4;

        // dtPoly field offsets
        private const int POLY_OFF_VERTS = 4;
        private const int POLY_OFF_VERTCOUNT = 30;
        private const int POLY_OFF_AREATYPE = 31;

        /// <summary>
        /// Parses a raw Detour tile, converts vertices to pixel coordinates,
        /// groups polygons by area ID, and builds one frozen StreamGeometry per area.
        /// </summary>
        public static NavmeshTileData? Parse(byte[] data)
        {
            if (data.Length < HeaderSize)
                return null;

            int polyCount = BitConverter.ToInt32(data, OFF_POLY_COUNT);
            int vertCount = BitConverter.ToInt32(data, OFF_VERT_COUNT);

            if (polyCount <= 0 || vertCount <= 0)
                return null;

            int vertsOffset = HeaderSize;
            int polysOffset = vertsOffset + vertCount * VertSize;

            if (data.Length < polysOffset + polyCount * PolySize)
                return null;

            // Read all vertices and convert to pixel coordinates ONCE
            // RD -> WoW -> Pixel: wowX = rdZ, wowY = rdX
            float[] verts = new float[vertCount * 3];
            Buffer.BlockCopy(data, vertsOffset, verts, 0, vertCount * VertSize);

            var pixelVerts = new Point[vertCount];
            for (int i = 0; i < vertCount; i++)
            {
                float rdX = verts[i * 3];
                float rdZ = verts[i * 3 + 2];
                float wowX = rdZ;
                float wowY = rdX;
                var (px, py) = WowCoordinateConverter.WorldToPixel(wowX, wowY);
                pixelVerts[i] = new Point(px, py);
            }

            // Group polygon vertex data by area ID
            var areaPolys = new Dictionary<byte, List<Point[]>>();

            for (int p = 0; p < polyCount; p++)
            {
                int polyOff = polysOffset + p * PolySize;

                byte polyVertCount = data[polyOff + POLY_OFF_VERTCOUNT];
                byte areaAndType = data[polyOff + POLY_OFF_AREATYPE];

                byte areaId = (byte)(areaAndType & 0x3F);
                byte polyType = (byte)(areaAndType >> 6);

                if (polyType != 0 || polyVertCount < 3)
                    continue;

                // RC_WALKABLE_AREA (63) -> TERRAIN_GROUND (1)
                if (areaId == 63)
                    areaId = 1;
                else if (areaId == 0)
                    continue;

                // Read vertex indices and resolve to pixel points
                var points = new Point[polyVertCount];
                bool valid = true;
                for (int v = 0; v < polyVertCount; v++)
                {
                    ushort vertIdx = BitConverter.ToUInt16(data, polyOff + POLY_OFF_VERTS + v * 2);
                    if (vertIdx >= vertCount) { valid = false; break; }
                    points[v] = pixelVerts[vertIdx];
                }
                if (!valid) continue;

                if (!areaPolys.TryGetValue(areaId, out var list))
                {
                    list = new List<Point[]>();
                    areaPolys[areaId] = list;
                }
                list.Add(points);
            }

            if (areaPolys.Count == 0) return null;

            // Build one frozen StreamGeometry per area
            var entries = new AreaGeometryEntry[areaPolys.Count];
            int idx = 0;

            foreach (var (areaId, polyList) in areaPolys)
            {
                var geo = new StreamGeometry();
                using (var ctx = geo.Open())
                {
                    foreach (var pts in polyList)
                    {
                        ctx.BeginFigure(pts[0], true, true);
                        for (int v = 1; v < pts.Length; v++)
                            ctx.LineTo(pts[v], false, false);
                    }
                }
                geo.Freeze();

                entries[idx++] = new AreaGeometryEntry(areaId, geo);
            }

            return new NavmeshTileData(entries);
        }
    }
}
