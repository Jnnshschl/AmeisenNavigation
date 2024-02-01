using AnTCP.Client;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Runtime.InteropServices.JavaScript;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;

namespace AmeisenNavigation.Tester
{
    public enum MessageType
    {
        PATH,                   // Generate a simple straight path
        MOVE_ALONG_SURFACE,     // Move an entity by small deltas using pathfinding (usefull to prevent falling off edges...)
        RANDOM_POINT,           // Get a random point on the mesh
        RANDOM_POINT_AROUND,    // Get a random point on the mesh in a circle
        CAST_RAY,               // Cast a movement ray to test for obstacles
        RANDOM_PATH,            // Generate a straight path where the nodes get offsetted by a random value
        EXPLORE_POLY,           // Generate a route to explore the polygon (W.I.P)
        CONFIGURE_FILTER,       // Cpnfigure the clients dtQueryFilter area costs
    };

    public enum PathType
    {
        STRAIGHT,
        RANDOM,
    };

    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            Client = new("127.0.0.1", 47110);
        }

        private AnTcpClient Client { get; set; }

        public IEnumerable<Vector3> GetPath(MessageType msgType, int mapId, Vector3 start, Vector3 end, int flags)
        {
            try
            {
                return Client.IsConnected ? Client.Send((byte)msgType, (mapId, flags, start, end)).AsArray<Vector3>() : [];
            }
            catch
            {
                return [];
            }
        }

        public Vector3 GetPoint(int mapId)
        {
            try
            {
                return Client.IsConnected ? Client.Send((byte)MessageType.RANDOM_POINT, mapId).As<Vector3>() : new();
            }
            catch
            {
                return new();
            }
        }

        private static void DrawPath(IEnumerable<Vector3> path, float minX, float minY, int padding, float factor, Graphics graphics, Pen linePen, SolidBrush nodeStartBrush, SolidBrush nodeEndBrush, SolidBrush nodeBrush)
        {
            int nodeSize = (int)(factor * 2);
            Vector2 lastN = new(path.First().X - minX + padding, path.First().Y - minY + padding);
            int nodeCount = path.Count();

            for (int i = 1; i < nodeCount; ++i)
            {
                Vector2 n = new(path.ElementAt(i).X - minX + padding, path.ElementAt(i).Y - minY + padding);

                int x = (int)(n.X * factor);
                int y = (int)(n.Y * factor);

                int prevX = (int)(lastN.X * factor);
                int prevY = (int)(lastN.Y * factor);

                graphics.DrawLine(linePen, x, y, prevX, prevY);
                graphics.FillRectangle(i == nodeCount - 1 ? nodeEndBrush : nodeBrush, new(x - (nodeSize / 2), y - (nodeSize / 2), nodeSize, nodeSize));
                graphics.FillRectangle(i == 1 ? nodeStartBrush : nodeBrush, new(prevX - (nodeSize / 2), prevY - (nodeSize / 2), nodeSize, nodeSize));

                lastN = n;
            }
        }

        private static bool TryLoadFloat(TextBox textBox, out float f)
        {
            bool result = float.TryParse(textBox.Text, out f);
            // mark textbox red
            return result;
        }

        private void ButtonRandomEnd_Click(object sender, RoutedEventArgs e)
        {
            if (!Client.IsConnected)
            {
                try
                {
                    Client.Connect();
                }
                catch
                {
                    // ignored, will happen when we cant connect
                    return;
                }
            }

            Vector3 pos = GetPoint(0);
            TextBoxEndX.Text = pos.X.ToString();
            TextBoxEndY.Text = pos.Y.ToString();
            TextBoxEndZ.Text = pos.Z.ToString();

            GetPathAndDraw();
        }

        private void ButtonRandomStart_Click(object sender, RoutedEventArgs e)
        {
            if (!Client.IsConnected)
            {
                try
                {
                    Client.Connect();
                }
                catch
                {
                    // ignored, will happen when we cant connect
                    return;
                }
            }

            Vector3 pos = GetPoint(0);
            TextBoxStartX.Text = pos.X.ToString();
            TextBoxStartY.Text = pos.Y.ToString();
            TextBoxStartZ.Text = pos.Z.ToString();

            GetPathAndDraw();
        }

        private void ButtonRun_Click(object sender, RoutedEventArgs e)
        {
            GetPathAndDraw();
        }

        private void GetPathAndDraw()
        {
            int flags = 0;

            if (rbPathFlagsChaikin.IsChecked == true) { flags |= 1; }
            if (rbPathFlagsCatmullRom.IsChecked == true) { flags |= 2; }
            if (rbPathFlagsBezier.IsChecked == true) { flags |= 4; }
            if (rbPathFlagsValidateCpop.IsChecked == true) { flags |= 8; }
            if (rbPathFlagsValidateMas.IsChecked == true) { flags |= 16; }

            PathType type = PathType.STRAIGHT;

            if (rbPathTypeRandom.IsChecked == true) { type = PathType.RANDOM; }

            Run(flags, type);
        }

        private void RbPathFlagsBezier_Checked(object sender, RoutedEventArgs e)
        {
            rbPathFlagsChaikin.IsChecked = false;
            rbPathFlagsCatmullRom.IsChecked = false;
        }

        private void RbPathFlagsCatmullRom_Checked(object sender, RoutedEventArgs e)
        {
            rbPathFlagsChaikin.IsChecked = false;
            rbPathFlagsBezier.IsChecked = false;
        }

        private void RbPathFlagsChaikin_Checked(object sender, RoutedEventArgs e)
        {
            rbPathFlagsBezier.IsChecked = false;
            rbPathFlagsCatmullRom.IsChecked = false;
        }

        private void RbPathFlagsValidateCpop_Checked(object sender, RoutedEventArgs e)
        {
            rbPathFlagsValidateMas.IsChecked = false;
        }

        private void RbPathFlagsValidateMas_Checked(object sender, RoutedEventArgs e)
        {
            rbPathFlagsValidateCpop.IsChecked = false;
        }

        private void Run(int flags, PathType type)
        {
            if (!Client.IsConnected)
            {
                try
                {
                    Client.Connect();
                }
                catch
                {
                    // ignored, will happen when we cant connect
                    return;
                }
            }

            if (TryLoadFloat(TextBoxStartX, out float sX) && TryLoadFloat(TextBoxStartY, out float sY) && TryLoadFloat(TextBoxStartZ, out float sZ)
                && TryLoadFloat(TextBoxEndX, out float eX) && TryLoadFloat(TextBoxEndY, out float eY) && TryLoadFloat(TextBoxEndZ, out float eZ))
            {
                Vector3 start = new(sX, sY, sZ);
                Vector3 end = new(eX, eY, eZ);

                Stopwatch sw = Stopwatch.StartNew();
                IEnumerable<Vector3> path = type switch
                {
                    PathType.STRAIGHT => GetPath(MessageType.PATH, 0, start, end, flags),
                    PathType.RANDOM => GetPath(MessageType.RANDOM_PATH, 0, start, end, flags),
                    _ => throw new NotImplementedException(),
                };
                sw.Stop();

                if (path == null || !path.Any())
                {
                    return;
                }

                UpdateViews(path, sw.Elapsed);

                float minX = float.MaxValue;
                float maxX = float.MinValue;
                float minY = float.MaxValue;
                float maxY = float.MinValue;

                foreach (Vector3 point in path)
                {
                    minX = Math.Min(minX, point.X);
                    maxX = Math.Max(maxX, point.X);
                    minY = Math.Min(minY, point.Y);
                    maxY = Math.Max(maxY, point.Y);
                }

                int padding = 12;
                int sizeX = (int)MathF.Abs(maxX - minX) + (2 * padding);
                int sizeY = (int)MathF.Abs(maxY - minY) + (2 * padding);

                float factor = MathF.Max((float)ImgRect.ActualHeight / sizeY, (float)ImgRect.ActualWidth / sizeX);

                using Pen linePen = new(Color.Black, factor);
                using SolidBrush nodeStartBrush = new(Color.LimeGreen);
                using SolidBrush nodeEndBrush = new(Color.Red);
                using SolidBrush nodeBrush = new(Color.White);

                using Bitmap bitmap = new((int)(sizeX * factor), (int)(sizeY * factor));
                using Graphics graphics = Graphics.FromImage(bitmap);

                DrawPath(path, minX, minY, padding, factor, graphics, linePen, nodeStartBrush, nodeEndBrush, nodeBrush);

                using MemoryStream memory = new();
                bitmap.Save(memory, ImageFormat.Png);

                BitmapImage bitmapImagePath = new();
                bitmapImagePath.BeginInit();
                bitmapImagePath.StreamSource = memory;
                bitmapImagePath.CacheOption = BitmapCacheOption.OnLoad;
                bitmapImagePath.EndInit();

                ImgCanvas.Source = bitmapImagePath;
                ImgCanvas.Visibility = Visibility.Visible;
            }
        }

        private void UpdateViews(IEnumerable<Vector3> path, TimeSpan duration)
        {
            PointList.ItemsSource = path;
            lblPointCount.Content = $"Points: {path.Count()}";
            lblTime.Content = duration;

            Vector3 last = default;
            float distance = 0.0f;

            foreach (Vector3 point in path)
            {
                if (last != default)
                {
                    distance += (float)last.GetDistance(point);
                }

                last = point;
            }

            lblDistance.Content = $"{MathF.Round(distance, 2)} m";
        }

        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            Vector3 start = new(-8826.562500f, -371.839752f, 71.638428f);
            Vector3 end = new(-8918.406250f, -130.297256f, 80.906364f);

            TextBoxStartX.Text = start.X.ToString();
            TextBoxStartY.Text = start.Y.ToString();
            TextBoxStartZ.Text = start.Z.ToString();

            TextBoxEndX.Text = end.X.ToString();
            TextBoxEndY.Text = end.Y.ToString();
            TextBoxEndZ.Text = end.Z.ToString();

            GetPathAndDraw();
        }

        private void TypeFlags_Click(object sender, RoutedEventArgs e)
        {
            GetPathAndDraw();
        }

        private void SldCostWater_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            float v = MathF.Round((float)sldCostWater.Value, 1);
            if (lblCostWater != null) lblCostWater.Content = $"Water: {v,1}";
            UpdateFilterConfig();
        }

        private void SldCostGround_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            float v = MathF.Round((float)sldCostGround.Value, 1);
            if (lblCostGround != null) lblCostGround.Content = $"Ground: {v,1}";
            UpdateFilterConfig();
        }

        private void SldCostBadLiquid_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            float v = MathF.Round((float)sldCostBadLiquid.Value, 2);
            if (lblCostBadLiquid != null) lblCostBadLiquid.Content = $"Magma/Slime: {v,1}";
            UpdateFilterConfig();
        }

        struct FilterConfig
        {
            public char State;
            public int Count = 3;
            public char GroundArea = (char)1;
            public float GroundCost;
            public char WaterArea = (char)4;
            public float WaterAreaCost;
            public char BadLiquidArea = (char)8;
            public float BadLiquidCost;

            public FilterConfig() { }
        }

        private void RbClientState_Click(object sender, RoutedEventArgs e)
        {
            UpdateFilterConfig();
        }

        private void UpdateFilterConfig()
        {
            try
            {
                char state = (char)0;

                if (rbClientStateNormalAlly.IsChecked == true)
                {
                    state = (char)1;
                }
                else if (rbClientStateNormalHorde.IsChecked == true)
                {
                    state = (char)2;
                }
                else if (rbClientStateDead.IsChecked == true)
                {
                    state = (char)3;
                }

                if (Client != null && Client.IsConnected)
                {
                    bool result = Client.Send((byte)MessageType.CONFIGURE_FILTER, new FilterConfig()
                    {
                        State = state,
                        GroundCost = (float)sldCostGround.Value,
                        WaterAreaCost = (float)sldCostWater.Value,
                        BadLiquidCost = (float)sldCostBadLiquid.Value,
                    }).As<bool>();
                }
            }
            catch { }
        }
    }
}