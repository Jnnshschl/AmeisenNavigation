using AmeisenNavigation.Client;
using AmeisenNavigation.Tester.Controls;
using AmeisenNavigation.Tester.Converters;
using AmeisenNavigation.Tester.Services;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace AmeisenNavigation.Tester
{
    public partial class MainWindow : Window
    {
        private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

        private readonly NavmeshClient _navClient = new();
        private MpqArchiveSet? _mpq;
        private TileCache? _tileCache;
        private AnpTileReader? _anpReader;
        private bool _isBusy;

        // Debounce for cost/filter slider changes
        private CancellationTokenSource? _filterDebounce;

        private static readonly string SettingsPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "AmeisenNavigation", "tester_settings.txt");

        private int MapId
        {
            get
            {
                if (ComboBoxMap.SelectedItem is KeyValuePair<int, string> kvp)
                    return kvp.Key;
                return 0;
            }
        }

        public MainWindow()
        {
            InitializeComponent();
            ComboBoxMap.ItemsSource = MapDefinitions.GetChoices();

            MapCanvas.StartPointSet += MapCanvas_StartPointSet;
            MapCanvas.EndPointSet += MapCanvas_EndPointSet;
            MapCanvas.CursorMoved += MapCanvas_CursorMoved;
        }

        private async void Window_Loaded(object sender, RoutedEventArgs e)
        {
            ComboBoxMap.SelectedIndex = 0;

            TextBoxStartX.Text = "-8826.5625";
            TextBoxStartY.Text = "-371.8398";
            TextBoxStartZ.Text = "71.6384";
            TextBoxEndX.Text = "-8918.4063";
            TextBoxEndY.Text = "-130.2973";
            TextBoxEndZ.Text = "80.9064";

            await TryRestoreDataDirAsync();
            await UpdateConnectionStatusAsync();
        }

        private void Window_Closing(object sender, CancelEventArgs e)
        {
            _filterDebounce?.Cancel();
            _navClient.Dispose();
            _mpq?.Dispose();
        }

        // --- Busy guard ---

        private void SetBusy(bool busy)
        {
            _isBusy = busy;
            ButtonRun.IsEnabled = !busy;
            ButtonRandomStart.IsEnabled = !busy;
            ButtonRandomEnd.IsEnabled = !busy;
        }

        // --- Data Source ---

        private async void BtnBrowseData_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new Microsoft.Win32.OpenFileDialog
            {
                Title = "Select any file in WoW Data directory",
                Filter = "MPQ files (*.mpq)|*.mpq|All files (*.*)|*.*",
                CheckFileExists = true,
            };

            if (dialog.ShowDialog() == true)
            {
                string? dir = Path.GetDirectoryName(dialog.FileName);
                if (dir != null)
                    await LoadDataDirAsync(dir);
            }
        }

        private async Task LoadDataDirAsync(string dir)
        {
            _mpq?.Dispose();
            _tileCache?.ClearMemoryCache();

            TxtDataDir.Text = "Loading MPQ archives...";
            TxtDataDir.Foreground = (Brush)FindResource("ForegroundDimBrush");

            var mpq = new MpqArchiveSet();
            bool loaded = await Task.Run(() => mpq.Load(dir));

            if (!loaded)
            {
                MessageBox.Show($"No MPQ files found in:\n{dir}", "Error",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
                mpq.Dispose();
                return;
            }

            _mpq = mpq;
            TxtDataDir.Text = $"{dir} ({_mpq.ArchiveCount} MPQs)";
            TxtDataDir.Tag = dir;
            TxtDataDir.Foreground = (Brush)FindResource("ForegroundBrush");

            _tileCache = new TileCache(_mpq, Dispatcher, () => MapCanvas.InvalidateVisual());
            MapCanvas.SetTileCache(_tileCache);
            UpdateMapCanvas();

            SaveSettings(dir, _anpReader?.MeshesPath);
        }

        private async Task TryRestoreDataDirAsync()
        {
            try
            {
                if (!File.Exists(SettingsPath)) return;

                var lines = File.ReadAllLines(SettingsPath);

                if (lines.Length > 0)
                {
                    string dir = lines[0].Trim();
                    if (Directory.Exists(dir))
                        await LoadDataDirAsync(dir);
                }

                if (lines.Length > 1)
                {
                    string meshDir = lines[1].Trim();
                    if (Directory.Exists(meshDir))
                        LoadMeshesDir(meshDir);
                }
            }
            catch { }
        }

        // --- Navmesh Overlay ---

        private void BtnBrowseMeshes_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new Microsoft.Win32.OpenFolderDialog
            {
                Title = "Select meshes output directory (containing .anp files)",
            };

            if (dialog.ShowDialog() == true)
            {
                LoadMeshesDir(dialog.FolderName);
                SaveSettings(TxtDataDir.Tag as string, dialog.FolderName);
            }
        }

        private void LoadMeshesDir(string dir)
        {
            _anpReader = new AnpTileReader(Dispatcher, () => MapCanvas.InvalidateVisual());
            _anpReader.SetMeshesPath(dir);
            MapCanvas.SetAnpReader(_anpReader);

            TxtMeshesDir.Text = dir;
            TxtMeshesDir.Foreground = (Brush)FindResource("ForegroundBrush");
        }

        private void ChkShowAreas_Click(object sender, RoutedEventArgs e)
        {
            MapCanvas.SetShowNavmeshAreas(ChkShowAreas.IsChecked == true);
        }

        private void SldOverlayOpacity_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (lblOverlayOpacity != null)
                lblOverlayOpacity.Text = $"Opacity: {(int)sldOverlayOpacity.Value}%";
            MapCanvas?.SetNavmeshOpacity((byte)(sldOverlayOpacity.Value * 2.55));
        }

        // --- Settings persistence ---

        private static void SaveSettings(string? dataDir, string? meshesDir)
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(SettingsPath)!);
                var lines = new List<string>
                {
                    dataDir ?? "",
                    meshesDir ?? "",
                };
                File.WriteAllLines(SettingsPath, lines);
            }
            catch { }
        }

        // --- Connection ---

        private static readonly Brush ConnectedBrush = new SolidColorBrush(Color.FromRgb(0, 200, 0));
        private static readonly Brush DisconnectedBrush = new SolidColorBrush(Color.FromRgb(255, 51, 51));

        static MainWindow()
        {
            ConnectedBrush.Freeze();
            DisconnectedBrush.Freeze();
        }

        private async Task UpdateConnectionStatusAsync()
        {
            bool wasConnected = _navClient.IsConnected;
            bool connected = await _navClient.TryConnectAsync();
            var brush = connected ? ConnectedBrush : DisconnectedBrush;
            StatusIndicator.Fill = brush;
            TxtConnectionStatus.Text = connected ? "Connected" : "Disconnected";
            TxtStatusConnection.Text = TxtConnectionStatus.Text;
            TxtStatusConnection.Foreground = brush;

            // Send filter config on first connect so server state matches UI
            if (connected && !wasConnected)
                await ApplyFilterConfigAsync();
        }

        // --- Map ---

        private void ComboBoxMap_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            _tileCache?.ClearMemoryCache();
            _anpReader?.ClearCache();
            UpdateMapCanvas();
        }

        private void UpdateMapCanvas()
        {
            int mapId = MapId;
            MapCanvas.SetMapId(mapId);
            if (MapDefinitions.InternalNames.TryGetValue(mapId, out string? name))
                MapCanvas.SetMapName(name);
        }

        // --- Coordinates (map click) → auto-pathfind ---

        private async void MapCanvas_StartPointSet(object? sender, WorldPointEventArgs e)
        {
            TextBoxStartX.Text = e.WorldX.ToString("F2", Inv);
            TextBoxStartY.Text = e.WorldY.ToString("F2", Inv);
            await TrySnapZAsync(TextBoxStartZ, e.WorldX, e.WorldY);
            UpdateEndpointsOnCanvas();
            await RunPathfindingAsync(fitToPath: false);
        }

        private async void MapCanvas_EndPointSet(object? sender, WorldPointEventArgs e)
        {
            TextBoxEndX.Text = e.WorldX.ToString("F2", Inv);
            TextBoxEndY.Text = e.WorldY.ToString("F2", Inv);
            await TrySnapZAsync(TextBoxEndZ, e.WorldX, e.WorldY);
            UpdateEndpointsOnCanvas();
            await RunPathfindingAsync(fitToPath: false);
        }

        private async Task TrySnapZAsync(TextBox zBox, float worldX, float worldY)
        {
            if (!_navClient.IsConnected)
            {
                if (!float.TryParse(zBox.Text, Inv, out _))
                    zBox.Text = "0";
                return;
            }

            float currentZ = float.TryParse(zBox.Text, Inv, out float z) ? z : 200f;
            var probe = new Vector3(worldX, worldY, currentZ);
            var snapped = await _navClient.MoveAlongSurfaceAsync(MapId, probe, probe);

            if (!snapped.IsZero)
                zBox.Text = snapped.Z.ToString("F2", Inv);
        }

        private void UpdateEndpointsOnCanvas()
        {
            MapCanvas.SetEndpoints(TryParseStartPoint(), TryParseEndPoint());
            MapCanvas.InvalidateVisual();
        }

        private void MapCanvas_CursorMoved(object? sender, WorldPointEventArgs e)
        {
            TxtStatusCoords.Text = $"X: {e.WorldX:F1}  Y: {e.WorldY:F1}";
            var (tx, ty) = WowCoordinateConverter.WorldToTile(e.WorldX, e.WorldY);
            TxtStatusTile.Text = $"Tile: {tx}, {ty}";
            if (_tileCache != null)
                TxtStatusZoom.Text = $"Tiles: {_tileCache.TilesLoaded} loaded, {_tileCache.TilesFailed} missing";
        }

        // --- Path ---

        private async void ButtonRun_Click(object sender, RoutedEventArgs e)
        {
            if (_isBusy) return;
            SetBusy(true);
            try
            {
                await UpdateConnectionStatusAsync();
                await RunPathfindingAsync(fitToPath: true);
            }
            finally { SetBusy(false); }
        }

        private async void ButtonRandomStart_Click(object sender, RoutedEventArgs e)
        {
            if (_isBusy) return;
            SetBusy(true);
            try
            {
                await UpdateConnectionStatusAsync();
                if (!_navClient.IsConnected) return;

                Vector3 pos = await _navClient.GetRandomPointAsync(MapId);
                TextBoxStartX.Text = pos.X.ToString("F4", Inv);
                TextBoxStartY.Text = pos.Y.ToString("F4", Inv);
                TextBoxStartZ.Text = pos.Z.ToString("F4", Inv);
                await RunPathfindingAsync(fitToPath: true);
            }
            finally { SetBusy(false); }
        }

        private async void ButtonRandomEnd_Click(object sender, RoutedEventArgs e)
        {
            if (_isBusy) return;
            SetBusy(true);
            try
            {
                await UpdateConnectionStatusAsync();
                if (!_navClient.IsConnected) return;

                Vector3 pos = await _navClient.GetRandomPointAsync(MapId);
                TextBoxEndX.Text = pos.X.ToString("F4", Inv);
                TextBoxEndY.Text = pos.Y.ToString("F4", Inv);
                TextBoxEndZ.Text = pos.Z.ToString("F4", Inv);
                await RunPathfindingAsync(fitToPath: true);
            }
            finally { SetBusy(false); }
        }

        /// <summary>
        /// Core pathfinding. Does NOT manage busy state — callers handle that.
        /// When fitToPath is false, the camera stays where it is (map clicks, cost re-runs).
        /// When the path is empty, nothing changes (no camera move, no path clear).
        /// </summary>
        private async Task RunPathfindingAsync(bool fitToPath)
        {
            if (!_navClient.IsConnected) return;

            var start = TryParseStartPoint();
            var end = TryParseEndPoint();
            if (start == null || end == null) return;

            var flags = PathFlags.None;
            if (rbPathFlagsChaikin.IsChecked == true) flags |= PathFlags.SmoothChaikin;
            if (rbPathFlagsCatmullRom.IsChecked == true) flags |= PathFlags.SmoothCatmullRom;
            if (rbPathFlagsBezier.IsChecked == true) flags |= PathFlags.SmoothBezier;
            if (rbPathFlagsValidateCpop.IsChecked == true) flags |= PathFlags.ValidateClosestPointOnPoly;
            if (rbPathFlagsValidateMas.IsChecked == true) flags |= PathFlags.ValidateMoveAlongSurface;

            PathType pathType = rbPathTypeRandom.IsChecked == true
                ? PathType.RANDOM
                : PathType.STRAIGHT;

            long startTime = Stopwatch.GetTimestamp();
            var path = await _navClient.GetPathAsync(pathType, MapId, start.Value, end.Value, flags);
            TimeSpan elapsed = Stopwatch.GetElapsedTime(startTime);

            if (path.Count == 0)
            {
                TxtStatusConnection.Text = "No path found";
                return;
            }

            MapCanvas.SetPath(path);
            MapCanvas.SetEndpoints(start, end);

            if (fitToPath)
                MapCanvas.FitPathInView();
            else
                MapCanvas.InvalidateVisual();

            lblPointCount.Text = $"Points: {path.Count}";
            lblTime.Text = $"{elapsed.TotalMilliseconds:F2} ms";

            float distance = 0;
            for (int i = 1; i < path.Count; i++)
                distance += path[i - 1].DistanceTo(path[i]);
            lblDistance.Text = $"{MathF.Round(distance, 2)} m";

            PointList.ItemsSource = path.Select(p => p.ToString());
        }

        private Vector3? TryParseStartPoint()
        {
            if (float.TryParse(TextBoxStartX.Text, Inv, out float x)
                && float.TryParse(TextBoxStartY.Text, Inv, out float y)
                && float.TryParse(TextBoxStartZ.Text, Inv, out float z))
                return new Vector3(x, y, z);
            return null;
        }

        private Vector3? TryParseEndPoint()
        {
            if (float.TryParse(TextBoxEndX.Text, Inv, out float x)
                && float.TryParse(TextBoxEndY.Text, Inv, out float y)
                && float.TryParse(TextBoxEndZ.Text, Inv, out float z))
                return new Vector3(x, y, z);
            return null;
        }

        // --- Smoothing mutual exclusion ---

        private void RbPathFlagsChaikin_Checked(object sender, RoutedEventArgs e)
        {
            if (rbPathFlagsCatmullRom != null) rbPathFlagsCatmullRom.IsChecked = false;
            if (rbPathFlagsBezier != null) rbPathFlagsBezier.IsChecked = false;
        }

        private void RbPathFlagsCatmullRom_Checked(object sender, RoutedEventArgs e)
        {
            if (rbPathFlagsChaikin != null) rbPathFlagsChaikin.IsChecked = false;
            if (rbPathFlagsBezier != null) rbPathFlagsBezier.IsChecked = false;
        }

        private void RbPathFlagsBezier_Checked(object sender, RoutedEventArgs e)
        {
            if (rbPathFlagsChaikin != null) rbPathFlagsChaikin.IsChecked = false;
            if (rbPathFlagsCatmullRom != null) rbPathFlagsCatmullRom.IsChecked = false;
        }

        private void RbPathFlagsValidateCpop_Checked(object sender, RoutedEventArgs e)
        {
            if (rbPathFlagsValidateMas != null) rbPathFlagsValidateMas.IsChecked = false;
        }

        private void RbPathFlagsValidateMas_Checked(object sender, RoutedEventArgs e)
        {
            if (rbPathFlagsValidateCpop != null) rbPathFlagsValidateCpop.IsChecked = false;
        }

        private void PathOption_Changed(object sender, RoutedEventArgs e)
        {
            // Smoothing/validation changed → re-run immediately (no debounce needed)
            _ = RunPathfindingAsync(fitToPath: false);
        }

        // --- Filter Config (debounced) ---

        private void RbClientState_Click(object sender, RoutedEventArgs e) => ScheduleFilterUpdate();

        private void SldCostGround_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (lblCostGround != null)
                lblCostGround.Text = $"Ground: {MathF.Round((float)sldCostGround.Value, 1)}";
            ScheduleFilterUpdate();
        }

        private void SldCostRoad_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (lblCostRoad != null)
                lblCostRoad.Text = $"Road: {MathF.Round((float)sldCostRoad.Value, 2)}";
            ScheduleFilterUpdate();
        }

        private void SldCostWater_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (lblCostWater != null)
                lblCostWater.Text = $"Water: {MathF.Round((float)sldCostWater.Value, 1)}";
            ScheduleFilterUpdate();
        }

        private void SldCostBadLiquid_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (lblCostBadLiquid != null)
                lblCostBadLiquid.Text = $"Magma/Slime: {MathF.Round((float)sldCostBadLiquid.Value, 1)}";
            ScheduleFilterUpdate();
        }

        private void SldFactionDanger_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (lblFactionDanger != null)
                lblFactionDanger.Text = $"Faction: {MathF.Round((float)sldFactionDanger.Value, 1)}";
            ScheduleFilterUpdate();
        }

        /// <summary>
        /// Debounces filter/cost changes: waits 400ms after the last change,
        /// then sends the filter config and re-runs pathfinding.
        /// </summary>
        private void ScheduleFilterUpdate()
        {
            _filterDebounce?.Cancel();
            _filterDebounce = new CancellationTokenSource();
            var token = _filterDebounce.Token;
            _ = DebouncedFilterUpdateAsync(token);
        }

        private async Task DebouncedFilterUpdateAsync(CancellationToken token)
        {
            try
            {
                await Task.Delay(400, token);
            }
            catch (TaskCanceledException)
            {
                return;
            }

            if (token.IsCancellationRequested) return;

            await ApplyFilterConfigAsync();
            await RunPathfindingAsync(fitToPath: false);
        }

        private async Task ApplyFilterConfigAsync()
        {
            try
            {
                if (!_navClient.IsConnected) return;

                byte state = 0;
                if (rbClientStateNormalAlly?.IsChecked == true) state = 1;
                else if (rbClientStateNormalHorde?.IsChecked == true) state = 2;
                else if (rbClientStateDead?.IsChecked == true) state = 3;

                float ground = (float)(sldCostGround?.Value ?? 1.0);
                float road = (float)(sldCostRoad?.Value ?? 0.75);
                float water = (float)(sldCostWater?.Value ?? 1.6);
                float badLiquid = (float)(sldCostBadLiquid?.Value ?? 4.0);
                float danger = (float)(sldFactionDanger?.Value ?? 3.0);

                float allyMult = state == 2 ? danger : 1.0f;  // Horde bot → alliance areas costly
                float hordeMult = state == 1 ? danger : 1.0f;  // Alliance bot → horde areas costly

                await _navClient.ApplyFilterAsync(state, ground, road, water, badLiquid, allyMult, hordeMult);
            }
            catch { }
        }
    }
}
