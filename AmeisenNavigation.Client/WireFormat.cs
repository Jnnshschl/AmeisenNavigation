using System.Runtime.InteropServices;

namespace AmeisenNavigation.Client
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct PathRequestData
    {
        public int MapId;
        public int Flags;
        public Vector3 Start;
        public Vector3 End;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct MoveRequestData
    {
        public int MapId;
        public Vector3 Start;
        public Vector3 End;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct CastRayData
    {
        public int MapId;
        public Vector3 Start;
        public Vector3 End;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct RandomPointAroundData
    {
        public int MapId;
        public Vector3 Start;
        public float Radius;
    }
}
