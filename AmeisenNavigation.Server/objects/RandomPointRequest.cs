using System.Runtime.InteropServices;

namespace AmeisenNavigation.Server.Objects
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RandomPointRequest
    {
        public RandomPointRequest(int mapId, Vector3 a, float maxRadius)
        {
            Type = 2;
            MapId = mapId;
            A = a;
            MaxRadius = maxRadius;
        }

        public int Type { get; set; }

        public Vector3 A { get; set; }

        public int MapId { get; set; }

        public float MaxRadius { get; set; }
    }
}