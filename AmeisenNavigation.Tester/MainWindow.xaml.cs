using AnTCP.Client;
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;

namespace AmeisenNavigation.Tester
{
    public enum MessageType
    {
        PATH,
        MOVE_ALONG_SURFACE,
        RANDOM_POINT,
        RANDOM_POINT_AROUND,
        CAST_RAY,
        RANDOM_PATH,
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

        private void ButtonRun_Click(object sender, RoutedEventArgs e)
        {
            Run(0, CheckBoxRandomPath.IsChecked == true ? PathType.RANDOM : PathType.STRAIGHT);
        }

        private void ButtonRunCatmullRom_Click(object sender, RoutedEventArgs e)
        {
            Run(2, CheckBoxRandomPath.IsChecked == true ? PathType.RANDOM : PathType.STRAIGHT);
        }

        private void ButtonRunChaikin_Click(object sender, RoutedEventArgs e)
        {
            Run(1, CheckBoxRandomPath.IsChecked == true ? PathType.RANDOM : PathType.STRAIGHT);
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

                IEnumerable<Vector3> path = type switch
                {
                    PathType.STRAIGHT => GetPath(MessageType.PATH, 0, start, end, flags),
                    PathType.RANDOM => GetPath(MessageType.RANDOM_PATH, 0, start, end, flags),
                    _ => throw new NotImplementedException(),
                };

                if (path == null || !path.Any())
                {
                    return;
                }

                PointList.ItemsSource = path;

                float minX = path.Min(e => MathF.Abs(e.X));
                float maxX = path.Max(e => MathF.Abs(e.X));
                float minY = path.Min(e => MathF.Abs(e.Y));
                float maxY = path.Max(e => MathF.Abs(e.Y));

                const float padding = 8.0f;

                int boundsX = (int)(maxX - minX + (padding * 2.0f));
                int boundsY = (int)(maxY - minY + (padding * 2.0f));

                List<Vector3> normalizedPath = [];

                foreach (Vector3 v3 in path)
                {
                    normalizedPath.Add(new(MathF.Abs(v3.X) - minX + padding, MathF.Abs(v3.Y) - minY + padding, 0.0f));
                }

                if (normalizedPath.Count > 0)
                {
                    float factor = MathF.Max((float)ImgRect.ActualHeight, (float)ImgRect.ActualWidth) / MathF.Max(boundsX, boundsY);

                    int nodeSize = (int)factor * 2;
                    int lineWidth = (int)factor * 1;

                    using SolidBrush nodeBrush = new(Color.Red);
                    using Pen linePen = new(Color.Black, lineWidth);
                    using SolidBrush bgBrush = new(Color.DarkGray);

                    using Bitmap bitmap = new((int)(boundsX * factor), (int)(boundsY * factor));
                    using Graphics graphics = Graphics.FromImage(bitmap);

                    graphics.FillRectangle(bgBrush, 0.0f, 0.0f, boundsX * factor, boundsY * factor);

                    for (int i = 1; i < normalizedPath.Count; ++i)
                    {
                        int x = (int)(normalizedPath[i].X * factor);
                        int y = (int)(normalizedPath[i].Y * factor);

                        int prevX = (int)(normalizedPath[i - 1].X * factor);
                        int prevY = (int)(normalizedPath[i - 1].Y * factor);

                        graphics.DrawLine(linePen, x, y, prevX, prevY);

                        graphics.FillRectangle(nodeBrush, new(x - lineWidth, y - lineWidth, nodeSize, nodeSize));
                        graphics.FillRectangle(nodeBrush, new(prevX - lineWidth, prevY - lineWidth, nodeSize, nodeSize));
                    }

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
        }

        private static bool TryLoadFloat(TextBox textBox, out float f)
        {
            bool result = float.TryParse(textBox.Text, out f);
            // mark textbox red
            return result;
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
        }
    }
}