using AmeisenNavigation.Client;
using AmeisenNavigation.Tester.Converters;
using AmeisenNavigation.Tester.Services;
using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;

namespace AmeisenNavigation.Tester.Controls
{
    public class WorldPointEventArgs(float worldX, float worldY, float worldZ = 0) : EventArgs
    {
        public float WorldX { get; } = worldX;
        public float WorldY { get; } = worldY;
        public float WorldZ { get; } = worldZ;
    }

    public class MapCanvas : FrameworkElement
    {
        private TileCache? _tileCache;
        private AnpTileReader? _anpReader;
        private int _mapId;
        private string? _mapName;
        private IReadOnlyList<Vector3> _pathPoints = [];
        private Vector3? _startPoint;
        private Vector3? _endPoint;

        // Navmesh overlay
        private bool _showNavmeshAreas;
        private byte _navmeshOpacity = 100;

        // Pan/zoom state
        private double _offsetX;
        private double _offsetY;
        private double _zoom = 1.0;
        private Point _lastMouse;
        private Point _mouseDownPos;
        private bool _isPanning;
        private const double ClickThreshold = 5.0;

        // Pens and brushes (created once)
        private static readonly Brush PathBrush = CreateBrush("#ffcc00");
        private static readonly Brush PathOutlineBrush = CreateBrush("#80000000");
        private static readonly Brush StartBrush = CreateBrush("#00ff00");
        private static readonly Brush EndBrush = CreateBrush("#ff3333");
        private static readonly Brush NodeBrush = CreateBrush("#ffffff");
        private static readonly Brush NodeOutlineBrush = CreateBrush("#80000000");
        private static readonly Brush TileBorderBrush = CreateBrush("#20ffffff");
        private static readonly Pen TileBorderPen = new(TileBorderBrush, 0.5) { DashStyle = DashStyles.Dot };
        private static readonly Brush BackgroundBrush = CreateBrush("#1a1a1a");
        private static readonly Typeface LabelTypeface = new("Segoe UI");

        // Zoom-adaptive pens (recreated when zoom changes)
        private double _lastPenZoom = -1;
        private Pen _pathPen = null!;
        private Pen _pathOutlinePen = null!;
        private Pen _nodeOutlinePen = null!;

        // Navmesh overlay outline pen (zoom-adaptive, thickness compensates for transform scale)
        private double _lastPolyPenZoom = -1;
        private Pen? _polyOutlinePen;

        static MapCanvas()
        {
            TileBorderPen.Freeze();
        }

        public event EventHandler<WorldPointEventArgs>? StartPointSet;
        public event EventHandler<WorldPointEventArgs>? EndPointSet;
        public event EventHandler<WorldPointEventArgs>? CursorMoved;

        public void SetTileCache(TileCache cache)
        {
            _tileCache = cache;
            InvalidateVisual();
        }

        public void SetAnpReader(AnpTileReader? reader)
        {
            _anpReader = reader;
            InvalidateVisual();
        }

        public void SetMapId(int mapId)
        {
            _mapId = mapId;
        }

        public void SetMapName(string? name)
        {
            _mapName = name;
            InvalidateVisual();
        }

        public void SetShowNavmeshAreas(bool show)
        {
            _showNavmeshAreas = show;
            InvalidateVisual();
        }

        public void SetNavmeshOpacity(byte opacity)
        {
            _navmeshOpacity = opacity;
            InvalidateVisual();
        }

        public void SetPath(IReadOnlyList<Vector3> points)
        {
            _pathPoints = points;
        }

        public void SetEndpoints(Vector3? start, Vector3? end)
        {
            _startPoint = start;
            _endPoint = end;
        }

        public void FitPathInView()
        {
            if (_pathPoints.Count == 0) return;

            double minPx = double.MaxValue, maxPx = double.MinValue;
            double minPy = double.MaxValue, maxPy = double.MinValue;

            foreach (var pt in _pathPoints)
            {
                var (px, py) = WowCoordinateConverter.WorldToPixel(pt.X, pt.Y);
                minPx = Math.Min(minPx, px);
                maxPx = Math.Max(maxPx, px);
                minPy = Math.Min(minPy, py);
                maxPy = Math.Max(maxPy, py);
            }

            double pathW = maxPx - minPx;
            double pathH = maxPy - minPy;
            double padding = 80;

            if (pathW < 1) pathW = 256;
            if (pathH < 1) pathH = 256;

            double zoomX = (ActualWidth - padding * 2) / pathW;
            double zoomY = (ActualHeight - padding * 2) / pathH;
            _zoom = Math.Min(zoomX, zoomY);
            _zoom = Math.Clamp(_zoom, 0.02, 15.0);

            double centerPx = (minPx + maxPx) / 2;
            double centerPy = (minPy + maxPy) / 2;
            _offsetX = ActualWidth / 2 - centerPx * _zoom;
            _offsetY = ActualHeight / 2 - centerPy * _zoom;

            InvalidateVisual();
        }

        public void CenterOnWorld(float worldX, float worldY, double? zoom = null)
        {
            var (px, py) = WowCoordinateConverter.WorldToPixel(worldX, worldY);
            if (zoom.HasValue) _zoom = zoom.Value;
            _offsetX = ActualWidth / 2 - px * _zoom;
            _offsetY = ActualHeight / 2 - py * _zoom;
            InvalidateVisual();
        }

        protected override void OnRender(DrawingContext dc)
        {
            dc.DrawRectangle(BackgroundBrush, null, new Rect(0, 0, ActualWidth, ActualHeight));

            if (_tileCache == null || _mapName == null)
            {
                DrawCenteredText(dc, "Select WoW Data directory and map to load minimap tiles");
                DrawPath(dc);
                return;
            }

            // Calculate visible tile range
            double left = -_offsetX / _zoom;
            double top = -_offsetY / _zoom;
            double right = (ActualWidth - _offsetX) / _zoom;
            double bottom = (ActualHeight - _offsetY) / _zoom;

            int tileMinX = Math.Max(0, (int)(left / WowCoordinateConverter.TilePixelSize));
            int tileMinY = Math.Max(0, (int)(top / WowCoordinateConverter.TilePixelSize));
            int tileMaxX = Math.Min(63, (int)(right / WowCoordinateConverter.TilePixelSize));
            int tileMaxY = Math.Min(63, (int)(bottom / WowCoordinateConverter.TilePixelSize));

            double tileScreenSize = WowCoordinateConverter.TilePixelSize * _zoom;

            for (int ty = tileMinY; ty <= tileMaxY; ty++)
            {
                for (int tx = tileMinX; tx <= tileMaxX; tx++)
                {
                    double sx = tx * WowCoordinateConverter.TilePixelSize * _zoom + _offsetX;
                    double sy = ty * WowCoordinateConverter.TilePixelSize * _zoom + _offsetY;
                    var rect = new Rect(sx, sy, tileScreenSize, tileScreenSize);

                    var tile = _tileCache.GetTile(_mapName, tx, ty);
                    if (tile != null)
                    {
                        dc.DrawImage(tile, rect);
                    }

                    // Draw tile grid lines
                    if (_zoom > 0.15)
                    {
                        dc.DrawRectangle(null, TileBorderPen, rect);
                    }
                }
            }

            // Draw navmesh area overlay (between minimap and path)
            if (_showNavmeshAreas && _anpReader != null && _zoom > 0.15)
            {
                DrawNavmeshOverlay(dc, tileMinX, tileMinY, tileMaxX, tileMaxY);
            }

            DrawPath(dc);
        }

        // --- Navmesh overlay (transform-based, zero per-vertex math) ---

        private void DrawNavmeshOverlay(DrawingContext dc, int tileMinX, int tileMinY, int tileMaxX, int tileMaxY)
        {
            if (_anpReader == null) return;

            bool drawOutlines = _zoom > 1.5;
            Pen? outlinePen = null;

            if (drawOutlines)
            {
                EnsurePolyPen();
                outlinePen = _polyOutlinePen;
            }

            // All tile geometries are pre-built in pixel coordinates.
            // One transform converts the entire overlay from pixel space to screen space.
            var transform = new MatrixTransform(new Matrix(_zoom, 0, 0, _zoom, _offsetX, _offsetY));
            transform.Freeze();

            dc.PushClip(new RectangleGeometry(new Rect(0, 0, ActualWidth, ActualHeight)));
            dc.PushTransform(transform);

            for (int ty = tileMinY; ty <= tileMaxY; ty++)
            {
                for (int tx = tileMinX; tx <= tileMaxX; tx++)
                {
                    var tileData = _anpReader.GetTile(_mapId, tx, ty);
                    if (tileData == null) continue;

                    // Draw pre-built frozen geometries - one DrawGeometry call per area
                    foreach (var entry in tileData.AreaGeometries)
                    {
                        var brush = AreaColors.GetBrush(entry.AreaId, _navmeshOpacity);
                        dc.DrawGeometry(brush, outlinePen, entry.Geometry);
                    }
                }
            }

            dc.Pop(); // transform
            dc.Pop(); // clip
        }

        private void EnsurePolyPen()
        {
            if (_lastPolyPenZoom == _zoom && _polyOutlinePen != null) return;
            _lastPolyPenZoom = _zoom;

            // Pen thickness must compensate for the transform scale
            // so outlines appear at consistent screen-space thickness
            double thickness = Math.Max(0.2, 0.5 / _zoom);
            var brush = new SolidColorBrush(Color.FromArgb(60, 0, 0, 0));
            brush.Freeze();
            _polyOutlinePen = new Pen(brush, thickness);
            _polyOutlinePen.Freeze();
        }

        // --- Path drawing ---

        private void EnsurePens()
        {
            if (_lastPenZoom == _zoom) return;
            _lastPenZoom = _zoom;

            double scale = Math.Clamp(_zoom, 0.3, 3.0);
            _pathPen = new Pen(PathBrush, 2.0 * scale) { LineJoin = PenLineJoin.Round };
            _pathPen.Freeze();
            _pathOutlinePen = new Pen(PathOutlineBrush, 3.5 * scale) { LineJoin = PenLineJoin.Round };
            _pathOutlinePen.Freeze();
            _nodeOutlinePen = new Pen(NodeOutlineBrush, 0.7 * scale);
            _nodeOutlinePen.Freeze();
        }

        private void DrawPath(DrawingContext dc)
        {
            if (_pathPoints.Count < 2) return;

            EnsurePens();

            // Build screen-space points
            var screenPoints = new Point[_pathPoints.Count];
            for (int i = 0; i < _pathPoints.Count; i++)
            {
                var (px, py) = WowCoordinateConverter.WorldToPixel(_pathPoints[i].X, _pathPoints[i].Y);
                screenPoints[i] = new Point(px * _zoom + _offsetX, py * _zoom + _offsetY);
            }

            // Build StreamGeometry for path lines (one draw call instead of N)
            var geo = new StreamGeometry();
            using (var ctx = geo.Open())
            {
                ctx.BeginFigure(screenPoints[0], false, false);
                for (int i = 1; i < screenPoints.Length; i++)
                    ctx.LineTo(screenPoints[i], true, false);
            }
            geo.Freeze();

            dc.DrawGeometry(null, _pathOutlinePen, geo);
            dc.DrawGeometry(null, _pathPen, geo);

            // Draw nodes
            double nodeRadius = Math.Max(1.5, 2.5 * Math.Min(_zoom, 2));
            for (int i = 0; i < screenPoints.Length; i++)
            {
                Brush brush;
                double r;
                if (i == 0)
                {
                    brush = StartBrush;
                    r = nodeRadius * 1.4;
                }
                else if (i == screenPoints.Length - 1)
                {
                    brush = EndBrush;
                    r = nodeRadius * 1.4;
                }
                else
                {
                    brush = NodeBrush;
                    r = nodeRadius;
                }

                dc.DrawEllipse(brush, _nodeOutlinePen, screenPoints[i], r, r);
            }
        }

        private void DrawCenteredText(DrawingContext dc, string text)
        {
            var ft = new FormattedText(text, System.Globalization.CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, LabelTypeface, 14, Brushes.Gray,
                VisualTreeHelper.GetDpi(this).PixelsPerDip);
            dc.DrawText(ft, new Point((ActualWidth - ft.Width) / 2, (ActualHeight - ft.Height) / 2));
        }

        // --- Mouse interaction ---

        protected override void OnMouseWheel(MouseWheelEventArgs e)
        {
            var pos = e.GetPosition(this);
            double worldXBefore = (pos.X - _offsetX) / _zoom;
            double worldYBefore = (pos.Y - _offsetY) / _zoom;

            double factor = e.Delta > 0 ? 1.15 : 1.0 / 1.15;
            _zoom = Math.Clamp(_zoom * factor, 0.02, 15.0);

            _offsetX = pos.X - worldXBefore * _zoom;
            _offsetY = pos.Y - worldYBefore * _zoom;

            InvalidateVisual();
            e.Handled = true;
        }

        protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
        {
            _lastMouse = e.GetPosition(this);
            _mouseDownPos = _lastMouse;
            _isPanning = true;
            CaptureMouse();
            e.Handled = true;
        }

        protected override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
        {
            var pos = e.GetPosition(this);
            bool wasClick = (pos - _mouseDownPos).Length < ClickThreshold;

            _isPanning = false;
            ReleaseMouseCapture();

            if (wasClick)
            {
                double canvasX = (pos.X - _offsetX) / _zoom;
                double canvasY = (pos.Y - _offsetY) / _zoom;
                var (wx, wy) = WowCoordinateConverter.PixelToWorld(canvasX, canvasY);
                StartPointSet?.Invoke(this, new WorldPointEventArgs(wx, wy));
            }

            e.Handled = true;
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            var pos = e.GetPosition(this);

            if (_isPanning)
            {
                _offsetX += pos.X - _lastMouse.X;
                _offsetY += pos.Y - _lastMouse.Y;
                _lastMouse = pos;
                InvalidateVisual();
            }

            // Always report cursor world position
            double canvasX = (pos.X - _offsetX) / _zoom;
            double canvasY = (pos.Y - _offsetY) / _zoom;
            var (wx, wy) = WowCoordinateConverter.PixelToWorld(canvasX, canvasY);
            CursorMoved?.Invoke(this, new WorldPointEventArgs(wx, wy));
        }

        protected override void OnMouseRightButtonDown(MouseButtonEventArgs e)
        {
            var pos = e.GetPosition(this);
            double canvasX = (pos.X - _offsetX) / _zoom;
            double canvasY = (pos.Y - _offsetY) / _zoom;
            var (wx, wy) = WowCoordinateConverter.PixelToWorld(canvasX, canvasY);
            EndPointSet?.Invoke(this, new WorldPointEventArgs(wx, wy));
            e.Handled = true;
        }

        private static Pen CreatePen(string color, double thickness)
        {
            var pen = new Pen(CreateBrush(color), thickness);
            pen.Freeze();
            return pen;
        }

        private static SolidColorBrush CreateBrush(string color)
        {
            var brush = new SolidColorBrush((Color)ColorConverter.ConvertFromString(color));
            brush.Freeze();
            return brush;
        }
    }
}
