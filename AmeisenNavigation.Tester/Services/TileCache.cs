using System;
using System.Collections.Concurrent;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace AmeisenNavigation.Tester.Services
{
    public sealed class TileCache
    {
        private readonly MpqArchiveSet _mpq;
        private readonly string _cacheDir;
        private readonly Dispatcher _dispatcher;
        private readonly Action? _tileLoadedCallback;

        // Thread-safe caches
        private readonly ConcurrentDictionary<(string map, int x, int y), BitmapSource?> _memory = new();
        private readonly ConcurrentDictionary<(string map, int x, int y), byte> _loading = new();

        // md5translate.trs: maps "MapName\mapX_Y.blp" -> hash filename
        private Md5TranslateTable? _md5Translate;
        private volatile bool _md5TranslateLoaded;
        private readonly object _trsLock = new();

        // Coalesced invalidation - avoids flooding dispatcher with per-tile callbacks
        private int _pendingInvalidate;

        public int TilesLoaded;
        public int TilesFailed;

        public TileCache(MpqArchiveSet mpq, Dispatcher dispatcher, Action? tileLoadedCallback = null, string? cacheDir = null)
        {
            _mpq = mpq;
            _dispatcher = dispatcher;
            _tileLoadedCallback = tileLoadedCallback;
            _cacheDir = cacheDir ?? Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "AmeisenNavigation", "TileCache");
        }

        /// <summary>
        /// Returns a cached tile immediately, or null if not yet loaded.
        /// If the tile isn't cached, a background load is queued automatically.
        /// </summary>
        public BitmapSource? GetTile(string mapInternalName, int tileX, int tileY)
        {
            var key = (mapInternalName, tileX, tileY);

            if (_memory.TryGetValue(key, out var cached))
                return cached;

            // Not in memory - queue background load (if not already loading)
            if (_loading.TryAdd(key, 1))
            {
                _ = Task.Run(() => LoadTileBackground(mapInternalName, tileX, tileY));
            }

            return null;
        }

        private void LoadTileBackground(string mapInternalName, int tileX, int tileY)
        {
            var key = (mapInternalName, tileX, tileY);

            try
            {
                // Try disk cache first
                string pngPath = GetCachePath(mapInternalName, tileX, tileY);
                if (File.Exists(pngPath))
                {
                    var bmp = LoadPng(pngPath);
                    if (bmp != null)
                    {
                        _memory[key] = bmp;
                        Interlocked.Increment(ref TilesLoaded);
                        NotifyTileLoaded();
                        return;
                    }
                }

                // Decode from MPQ
                byte[]? blpData = TryReadMinimapBlp(mapInternalName, tileX, tileY);
                if (blpData == null)
                {
                    _memory[key] = null;
                    Interlocked.Increment(ref TilesFailed);
                    return;
                }

                var decoded = BlpReader.Decode(blpData);
                if (decoded == null)
                {
                    _memory[key] = null;
                    Interlocked.Increment(ref TilesFailed);
                    return;
                }

                // Save to disk cache
                try
                {
                    string dir = Path.GetDirectoryName(pngPath)!;
                    Directory.CreateDirectory(dir);

                    using var fs = new FileStream(pngPath, FileMode.Create);
                    var encoder = new PngBitmapEncoder();
                    encoder.Frames.Add(BitmapFrame.Create(decoded));
                    encoder.Save(fs);
                }
                catch { }

                _memory[key] = decoded;
                Interlocked.Increment(ref TilesLoaded);
                NotifyTileLoaded();
            }
            finally
            {
                _loading.TryRemove(key, out _);
            }
        }

        private void NotifyTileLoaded()
        {
            if (_tileLoadedCallback == null) return;

            // Only schedule one dispatcher callback at a time; subsequent tiles just set the flag
            if (Interlocked.Exchange(ref _pendingInvalidate, 1) == 0)
            {
                _dispatcher.BeginInvoke(() =>
                {
                    Interlocked.Exchange(ref _pendingInvalidate, 0);
                    _tileLoadedCallback();
                }, DispatcherPriority.Render);
            }
        }

        private byte[]? TryReadMinimapBlp(string mapName, int tileX, int tileY)
        {
            // Strategy 1: md5translate.trs lookup (WoW 3.3.5a standard)
            EnsureMd5TranslateLoaded();
            if (_md5Translate != null)
            {
                // WoW minimap naming: map{X}_{Y}.blp where:
                //   tileX = canvas horizontal = WDT column (X)
                //   tileY = canvas vertical   = WDT row (Y)
                // Keys in the trs include the .blp extension
                string[] trsKeys =
                [
                    $"{mapName}\\map{tileX}_{tileY}.blp",
                    $"{mapName}\\map{tileX:D2}_{tileY:D2}.blp",
                    $"{mapName}\\map{tileX}_{tileY}",
                    $"{mapName}\\map{tileX:D2}_{tileY:D2}",
                ];

                foreach (string trsKey in trsKeys)
                {
                    if (_md5Translate.TryGetValue(trsKey, out string? hashFile))
                    {
                        string baseName = hashFile.EndsWith(".blp", StringComparison.OrdinalIgnoreCase)
                            ? hashFile[..^4] : hashFile;

                        string[] hashPaths =
                        [
                            $@"Textures\Minimap\{baseName}.blp",
                            $@"Textures\Minimap\{mapName}\{baseName}.blp",
                        ];

                        foreach (string blpPath in hashPaths)
                        {
                            byte[]? data = _mpq.ReadFile(blpPath);
                            if (data != null) return data;
                        }
                    }
                }
            }

            // Strategy 2: Direct path (some versions/repacks)
            string[] pathsToTry =
            [
                $@"World\Minimaps\{mapName}\map{tileX}_{tileY}.blp",
                $@"Textures\Minimap\{mapName}\map{tileX}_{tileY}.blp",
                $@"World\Minimaps\{mapName}\map{tileX:D2}_{tileY:D2}.blp",
                $@"Textures\Minimap\{mapName}\map{tileX:D2}_{tileY:D2}.blp",
            ];

            foreach (string path in pathsToTry)
            {
                byte[]? data = _mpq.ReadFile(path);
                if (data != null) return data;
            }

            return null;
        }

        private void EnsureMd5TranslateLoaded()
        {
            if (_md5TranslateLoaded) return;

            lock (_trsLock)
            {
                if (_md5TranslateLoaded) return;

                byte[]? trsData = _mpq.ReadFile(@"Textures\Minimap\md5translate.trs");
                _md5Translate = trsData != null ? Md5TranslateTable.Parse(trsData) : null;
                _md5TranslateLoaded = true;
            }
        }

        public void ClearMemoryCache()
        {
            _memory.Clear();
            _loading.Clear();
            TilesLoaded = 0;
            TilesFailed = 0;
        }

        private string GetCachePath(string map, int x, int y)
            => Path.Combine(_cacheDir, map, $"map{x}_{y}.png");

        private static BitmapSource? LoadPng(string path)
        {
            try
            {
                var bmp = new BitmapImage();
                bmp.BeginInit();
                bmp.UriSource = new Uri(path, UriKind.Absolute);
                bmp.CacheOption = BitmapCacheOption.OnLoad;
                bmp.EndInit();
                bmp.Freeze();
                return bmp;
            }
            catch
            {
                return null;
            }
        }
    }
}
