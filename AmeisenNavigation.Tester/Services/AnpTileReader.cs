using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Threading;

namespace AmeisenNavigation.Tester.Services
{
    /// <summary>
    /// Reads .anp navmesh files (ZIP archives) and parses Detour tile binary data
    /// to extract polygon geometry and area IDs for visualization.
    ///
    /// ANP file structure (ZIP):
    ///   "mapId"   -> int (4 bytes)
    ///   "params"  -> dtNavMeshParams
    ///   "XX_YY"   -> raw Detour tile binary data
    ///
    /// IMPORTANT: The "XX_YY" entry names are NOT WoW grid tile coordinates.
    /// They are relative to the geometry bounding box. To determine which WoW
    /// tile a navmesh entry belongs to, we read the dtMeshHeader.bmin from
    /// each entry and convert the RD coordinates to canvas tile coordinates.
    ///
    /// Performance: All geometry is pre-built as frozen StreamGeometry in pixel
    /// coordinates at parse time. Rendering uses a single MatrixTransform for
    /// zoom/pan — zero per-vertex math, zero per-frame allocation.
    /// </summary>
    public sealed class AnpTileReader
    {
        private string? _meshesPath;

        // Per-map cache: mapId -> { (canvasTileX, canvasTileY) -> NavmeshTileData }
        private readonly ConcurrentDictionary<int, Dictionary<(int tx, int ty), NavmeshTileData>> _mapCache = new();
        private readonly ConcurrentDictionary<int, byte> _mapLoading = new();

        private readonly Dispatcher _dispatcher;
        private readonly Action? _tileLoadedCallback;
        private int _pendingInvalidate;

        // dtMeshHeader bmin offset for tile position calculation
        private const int HeaderSize = 100;
        private const int OFF_BMIN = 18 * 4;       // bmin[3] at offset 72

        private const float TileSize = 533.33333f;

        private static readonly Regex TileEntryPattern = new(@"^\d+_\d+$", RegexOptions.Compiled);

        public AnpTileReader(Dispatcher dispatcher, Action? tileLoadedCallback = null)
        {
            _dispatcher = dispatcher;
            _tileLoadedCallback = tileLoadedCallback;
        }

        public string? MeshesPath => _meshesPath;

        public void SetMeshesPath(string? path)
        {
            _meshesPath = path;
            ClearCache();
        }

        public void ClearCache()
        {
            _mapCache.Clear();
            _mapLoading.Clear();
        }

        public NavmeshTileData? GetTile(int mapId, int canvasTileX, int canvasTileY)
        {
            if (_meshesPath == null) return null;

            if (_mapCache.TryGetValue(mapId, out var tiles))
            {
                tiles.TryGetValue((canvasTileX, canvasTileY), out var data);
                return data;
            }

            if (_mapLoading.TryAdd(mapId, 1))
            {
                System.Threading.Tasks.Task.Run(() => LoadMapBackground(mapId));
            }

            return null;
        }

        private void LoadMapBackground(int mapId)
        {
            try
            {
                var tiles = LoadAllTilesFromAnp(mapId);
                _mapCache[mapId] = tiles;

                if (tiles.Count > 0)
                    NotifyTileLoaded();
            }
            finally
            {
                _mapLoading.TryRemove(mapId, out _);
            }
        }

        private Dictionary<(int tx, int ty), NavmeshTileData> LoadAllTilesFromAnp(int mapId)
        {
            var result = new Dictionary<(int tx, int ty), NavmeshTileData>();

            if (_meshesPath == null) return result;

            string anpPath = Path.Combine(_meshesPath, $"{mapId:000}.anp");
            if (!File.Exists(anpPath)) return result;

            try
            {
                using var zip = ZipFile.OpenRead(anpPath);

                foreach (var entry in zip.Entries)
                {
                    if (!TileEntryPattern.IsMatch(entry.Name))
                        continue;

                    byte[] tileData;
                    using (var stream = entry.Open())
                    using (var ms = new MemoryStream())
                    {
                        stream.CopyTo(ms);
                        tileData = ms.ToArray();
                    }

                    if (tileData.Length < HeaderSize)
                        continue;

                    // Read bmin from dtMeshHeader to determine world position
                    float bminRdX = BitConverter.ToSingle(tileData, OFF_BMIN);
                    float bminRdZ = BitConverter.ToSingle(tileData, OFF_BMIN + 8);

                    float wowX = bminRdZ + TileSize / 2;
                    float wowY = bminRdX + TileSize / 2;

                    int canvasTx = (int)(32.0f - wowY / TileSize);
                    int canvasTy = (int)(32.0f - wowX / TileSize);

                    var parsed = DetourTileParser.Parse(tileData);
                    if (parsed != null)
                    {
                        result[(canvasTx, canvasTy)] = parsed;
                    }
                }
            }
            catch { }

            return result;
        }

        private void NotifyTileLoaded()
        {
            if (_tileLoadedCallback == null) return;

            if (Interlocked.Exchange(ref _pendingInvalidate, 1) == 0)
            {
                _dispatcher.BeginInvoke(() =>
                {
                    Interlocked.Exchange(ref _pendingInvalidate, 0);
                    _tileLoadedCallback();
                }, DispatcherPriority.Render);
            }
        }
    }
}
