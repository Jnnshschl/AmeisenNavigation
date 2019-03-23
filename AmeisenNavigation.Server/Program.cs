using AmeisenNavigationWrapper;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace AmeisenNavigation.Server
{
    internal class Program
    {
        public struct PathRequest
        {
            public Vector3 A { get; set; }
            public Vector3 B { get; set; }
            public int MapId { get; set; }

            public PathRequest(Vector3 a, Vector3 b, int mapId)
            {
                A = a;
                B = b;
                MapId = mapId;
            }
        }

        public struct Vector3
        {
            public float X { get; set; }
            public float Y { get; set; }
            public float Z { get; set; }

            public Vector3(float x, float y, float z)
            {
                X = x;
                Y = y;
                Z = z;
            }
        }

        private static bool StopServer { get; set; }
        private static TcpListener TcpListener { get; set; }
        private static AmeisenNav AmeisenNav { get; set; }

        private static readonly string errorPath = AppDomain.CurrentDomain.BaseDirectory + "errors.txt";
        private static readonly string settingsPath = AppDomain.CurrentDomain.BaseDirectory + "config.json";

        private static void Main(string[] args)
        {
            Console.Title = "AmeisenNavigation Server";
            Console.WriteLine($"-> AmeisenNavigation Server");
            Console.WriteLine($">> Loading config from: {settingsPath}");

            Settings settings = new Settings();

            if (File.Exists(settingsPath))
            {
                settings = JsonConvert.DeserializeObject<Settings>(File.ReadAllText(settingsPath));
            }
            else
            {
                File.WriteAllText(settingsPath, JsonConvert.SerializeObject(settings));
            }

            if (!Directory.Exists(settings.mmapsFolder))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine(">> MMAP folder missing, edit folder in config.json");
                Console.ResetColor();
                Console.ReadKey();
            }
            else
            {
                Console.WriteLine($">> MMAPS located at: {settings.mmapsFolder}");
                AmeisenNav = new AmeisenNav(settings.mmapsFolder);
                StopServer = false;

                if (settings.preloadMaps.Length > 0)
                {
                    Console.WriteLine($">> Preloading Maps");
                    foreach (int i in settings.preloadMaps)
                    {
                        AmeisenNav.LoadMap(i);
                    }
                    Console.WriteLine($">> Preloaded {settings.preloadMaps.Length} Maps");
                }

                TcpListener = new TcpListener(IPAddress.Parse(settings.ipAddress), settings.port);
                TcpListener.Start();

                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine($">> Server running ({settings.ipAddress}:{settings.port}) press Ctrl + C to exit");
                Console.ResetColor();
                EnterServerLoop();

                AmeisenNav.Dispose();
            }

            // Debug stuff
            /*float[] start = { -8826.562500f, -371.839752f, 71.638428f };
            float[] end = { -8847.150391f, -387.518677f, 72.575912f };
            float[] tileLoc = { -8918.406250f, -130.297256f, 80.906364f };

            List<Vector3> Path = GetPath(start, tileLoc);
            string json_path = JsonConvert.SerializeObject(Path);

            Console.WriteLine($"Path contains {Path.Count} Nodes");
            Console.WriteLine($"Path json {json_path}");
            Console.ReadKey();*/
        }

        public static List<Vector3> GetPath(Vector3 start, Vector3 end, int map_id, TcpClient client)
        {
            int pathSize;
            float[] path_raw = new float[1024 * 3];
            List<Vector3> Path = new List<Vector3>();

            Stopwatch sw = new Stopwatch();
            sw.Start();
            unsafe
            {
                path_raw = AmeisenNav.GetPath(map_id, start.X, start.Y, start.Z, end.X, end.Y, end.Z, &pathSize);
            }
            sw.Stop();

            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.Write($">> {client.Client.RemoteEndPoint}");

            Console.ResetColor();
            Console.Write(": Pathfinding took ");

            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.Write($"{sw.ElapsedMilliseconds} ");

            Console.ResetColor();
            Console.WriteLine("ms");

            for (int i = 0; i < pathSize * 3; i += 3)
            {
                Path.Add(new Vector3(path_raw[i], path_raw[i + 1], path_raw[i + 2]));
            }

            return Path;
        }

        public static void EnterServerLoop()
        {
            while (!StopServer)
            {
                TcpClient newClient = TcpListener.AcceptTcpClient();
                Thread userThread = new Thread(new ThreadStart(() => HandleClient(newClient)));
                userThread.Start();
            }
        }

        public static void HandleClient(object obj)
        {
            TcpClient client = (TcpClient)obj;
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($">> New Client: {client.Client.RemoteEndPoint}");
            Console.ResetColor();

            StreamWriter sWriter = new StreamWriter(client.GetStream(), Encoding.ASCII);
            StreamReader sReader = new StreamReader(client.GetStream(), Encoding.ASCII);

            bool isClientConnected = true;
            string rawData;
            string rawPath;
            PathRequest pathRequest;

            while (isClientConnected)
            {
                try
                {
                    rawData = sReader.ReadLine().Replace("&gt;", "");
                    //Console.WriteLine($">> {client.Client.RemoteEndPoint} sent data: {rawData}");

                    pathRequest = JsonConvert.DeserializeObject<PathRequest>(rawData);

                    List<Vector3> path = GetPath(pathRequest.A, pathRequest.B, pathRequest.MapId, client);
                    rawPath = JsonConvert.SerializeObject(path);

                    sWriter.WriteLine(JsonConvert.SerializeObject(path) + " &gt;");
                    sWriter.Flush();
                }
                catch (Exception e)
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.Write(">> Error at client: ");
                    Console.ResetColor();
                    Console.WriteLine($"{client.Client.RemoteEndPoint}");
                    try
                    {
                        File.AppendAllText(errorPath, e.ToString() + "\n");
                    }
                    catch { }
                    isClientConnected = false;
                }
            }
        }
    }
}
