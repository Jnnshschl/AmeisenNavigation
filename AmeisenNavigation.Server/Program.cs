using AmeisenNavigationWrapper;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace AmeisenBot.NavigationRESTApi
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

        private const int PORT = 47110;

        private static void Main(string[] args)
        {
            AmeisenNav = new AmeisenNav("H:\\mmaps\\");
            Console.Title = "AmeisenNavigation Server";
            StopServer = false;

            TcpListener = new TcpListener(IPAddress.Any, PORT);
            TcpListener.Start();

            Console.WriteLine($"Starting server on 0.0.0.0:{PORT}");
            EnterServerLoop();

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

        public static List<Vector3> GetPath(Vector3 start, Vector3 end, int map_id)
        {
            int pathSize;
            float[] path_raw = new float[1024 * 3];
            List<Vector3> Path = new List<Vector3>();

            unsafe
            {
                path_raw = AmeisenNav.GetPath(map_id, start.X, start.Y, start.Z, end.X, end.Y, end.Z, &pathSize);
            }


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
            Console.WriteLine($"New Client: {client.Client.RemoteEndPoint}");

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
                    Console.WriteLine($"{client.Client.RemoteEndPoint} sent data: {rawData}");

                    pathRequest = JsonConvert.DeserializeObject<PathRequest>(rawData);

                    List<Vector3> path = GetPath(pathRequest.A, pathRequest.B, pathRequest.MapId);
                    rawPath = JsonConvert.SerializeObject(path);

                    sWriter.WriteLine(JsonConvert.SerializeObject(path) + " &gt;");
                    sWriter.Flush();
                }
                catch (Exception e)
                {
                    Console.WriteLine(e.ToString());
                    isClientConnected = false;
                }
            }
        }
    }
}
