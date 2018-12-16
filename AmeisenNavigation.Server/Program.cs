using AmeisenNavigationWrapper;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;

namespace AmeisenBot.NavigationRESTApi
{
    internal class Program
    {
        private struct Vector3
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

        private static void Main(string[] args)
        {
            Console.Title = "AmeisenNavigation Server";

            float[] start = { -8826.562500f, -371.839752f, 71.638428f };
            float[] end = { -8847.150391f, -387.518677f, 72.575912f };
            float[] tileLoc = { -8918.406250f, -130.297256f, 80.906364f };

            int pathSize;
            float[] path_raw = new float[1024 * 3];

            List<Vector3> Path = new List<Vector3>();

            unsafe
            {
                using (AmeisenNav ameisenNav = new AmeisenNav("H:\\mmaps\\"))
                {
                    path_raw = ameisenNav.GetPath(0, start[0], start[1], start[2], tileLoc[0], tileLoc[1], tileLoc[2], &pathSize);
                }
            }

            for (int i = 0; i < pathSize * 3; i += 3)
            {
                Path.Add(new Vector3(path_raw[i], path_raw[i + 1], path_raw[i + 2]));
            }

            string json_path = JsonConvert.SerializeObject(Path);

            Console.WriteLine($"Path contains {Path.Count} Nodes");
            Console.WriteLine($"Path json {json_path}");
            Console.ReadKey();
        }
    }
}
