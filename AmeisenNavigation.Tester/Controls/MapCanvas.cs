using AmeisenNavigation.Tester.Converters;
using AmeisenNavigation.Tester.Services;
using System;
using System.Collections.Generic;
using System.Linq;
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
        private string? _mapName;
        private IReadOnlyList<Vector3> _pathPoints = [];
        private Vector3? _startPoint;
        private Vector3? _endPoint;

        // Pan/zoom state
        private double _offsetX;
        private double _offsetY;
        private double _zoom = 1.0;
        private Point _lastMouse;
        private bool _isPanning;

        // Pens and brushes (created once)
        private static readonly Pen PathPen = CreatePen("#ffcc00", 2.5);
        private static readonly Pen PathOutlinePen = CreatePen("#80000000", 4.0);
        private static readonly Brush StartBrush = CreateBrush("#00ff00");
        private static readonly Brush EndBrush = CreateBrush("#ff3333");
        private static readonly Brush NodeBrush = CreateBrush("#ffffff");
        private static readonly Pen NodeOutlinePen = CreatePen("#80000000", 1.0);
        private static readonly Brush TileBorderBrush = CreateBrush("#20ffffff");
        private static readonly Pen TileBorderPen = new(TileBorderBrush, 0.5) { DashStyle = DashStyles.Dot };
        private static readonly Brush BackgroundBrush = CreateBrush("#1a1a1a");
        private static readonly Typeface LabelTypeface = new("Segoe UI");

        static MapCanvas()
        {
            PathPen.Freeze();
            PathOutlinePen.Freeze();
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

        public void SetMapName(string? name)
        {
            _mapName = name;
            InvalidateVisual();
        }

        public void SetPath(IEnumerable<Vector3> points)
        {
            _pathPoints = points.ToList();
            InvalidateVisual();
        }

        public void SetEndpoints(Vector3? start, Vector3? end)
        {
            _startPoint = start;
            _endPoint = end;
            InvalidateVisual();
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

            DrawPath(dc);
        }

        private void DrawPath(DrawingContext dc)
        {
            if (_pathPoints.Count < 2) return;

            // Build screen-space points
            var screenPoints = new Point[_pathPoints.Count];
            for (int i = 0; i < _pathPoints.Count; i++)
            {
                var (px, py) = WowCoordinateConverter.WorldToPixel(_pathPoints[i].X, _pathPoints[i].Y);
                screenPoints[i] = new Point(px * _zoom + _offsetX, py * _zoom + _offsetY);
            }

            // Draw path lines (outline first, then colored)
            for (int i = 1; i < screenPoints.Length; i++)
            {
                dc.DrawLine(PathOutlinePen, screenPoints[i - 1], screenPoints[i]);
            }
            for (int i = 1; i < screenPoints.Length; i++)
            {
                dc.DrawLine(PathPen, screenPoints[i - 1], screenPoints[i]);
            }

            // Draw nodes
            double nodeRadius = Math.Max(3, 5 * Math.Min(_zoom, 2));
            for (int i = 0; i < screenPoints.Length; i++)
            {
                Brush brush;
                double r;
                if (i == 0)
                {
                    brush = StartBrush;
                    r = nodeRadius * 1.5;
                }
                else if (i == screenPoints.Length - 1)
                {
                    brush = EndBrush;
                    r = nodeRadius * 1.5;
                }
                else
                {
                    brush = NodeBrush;
                    r = nodeRadius;
                }

                dc.DrawEllipse(brush, NodeOutlinePen, screenPoints[i], r, r);
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
            _isPanning = true;
            CaptureMouse();
            e.Handled = true;
        }

        protected override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
        {
            _isPanning = false;
            ReleaseMouseCapture();
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

            if (Keyboard.Modifiers.HasFlag(ModifierKeys.Shift))
                StartPointSet?.Invoke(this, new WorldPointEventArgs(wx, wy));
            else
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
