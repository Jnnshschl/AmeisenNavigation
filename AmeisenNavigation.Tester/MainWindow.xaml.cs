using AnTCP.Client;
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Media.Imaging;

namespace AmeisenNavigation.Tester
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            Client = new("127.0.0.1", 47110);
        }

        private AnTcpClient Client { get; set; }

        public IEnumerable<Vector3> GetPath(int mapId, Vector3 start, Vector3 end, int flags)
        {
            try
            {
                return Client.IsConnected ? Client.Send(0x0, (mapId, flags, start, end)).AsArray<Vector3>() : Array.Empty<Vector3>();
            }
            catch
            {
                return Array.Empty<Vector3>();
            }
        }

        private static void RenderPath(Graphics graphics, float factor, List<Vector3> path)
        {
            int size = (int)(2.0f * factor);
            int halfsize = (int)(1.0f * factor);

            Pen linePen = new(Color.Black, halfsize);
            SolidBrush nodeBrush = new(Color.Red);

            for (int i = 1; i < path.Count; ++i)
            {
                int x = (int)(path[i].X * factor);
                int y = (int)(path[i].Y * factor);

                int prevX = (int)(path[i - 1].X * factor);
                int prevY = (int)(path[i - 1].Y * factor);

                graphics.DrawLine(linePen, x, y, prevX, prevY);

                graphics.FillRectangle(nodeBrush, new(x - halfsize, y - halfsize, size, size));
                graphics.FillRectangle(nodeBrush, new(prevX - halfsize, prevY - halfsize, size, size));
            }
        }

        private void ButtonRun_Click(object sender, RoutedEventArgs e)
        {
            Run(0);
        }

        private void ButtonRunCatmullRom_Click(object sender, RoutedEventArgs e)
        {
            Run(2);
        }

        private void ButtonRunChaikin_Click(object sender, RoutedEventArgs e)
        {
            Run(1);
        }

        private void Run(int flags)
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

            Vector3 start = new(-8826.562500f, -371.839752f, 71.638428f);
            Vector3 end = new(-8918.406250f, -130.297256f, 80.906364f);

            IEnumerable<Vector3> path = GetPath(0, start, end, flags);

            if (path == null || !path.Any())
            {
                return;
            }

            float minX = path.Min(e => MathF.Abs(e.X));
            float maxX = path.Max(e => MathF.Abs(e.X));
            float minY = path.Min(e => MathF.Abs(e.Y));
            float maxY = path.Max(e => MathF.Abs(e.Y));

            const float padding = 4.0f;

            int boundsX = (int)(maxX - minX + (padding * 2.0f));
            int boundsY = (int)(maxY - minY + (padding * 2.0f));

            List<Vector3> normalizedPath = new();

            foreach (Vector3 v3 in path)
            {
                normalizedPath.Add(new(MathF.Abs(v3.X) - minX, MathF.Abs(v3.Y) - minY, v3.Z));
            }

            PointList.ItemsSource = normalizedPath;

            if (normalizedPath.Count > 0)
            {
                Bitmap bitmap = new((int)ImgRect.ActualHeight, (int)ImgRect.ActualWidth);
                using Graphics graphics = Graphics.FromImage(bitmap);

                float factor = MathF.Max((float)ImgRect.ActualHeight, (float)ImgRect.ActualWidth) / MathF.Max(boundsX, boundsY);
                RenderPath(graphics, factor, normalizedPath);

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
}