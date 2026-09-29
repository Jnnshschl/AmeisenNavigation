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
    internal struct CastRayExResponse
    {
        public int Hit;
        public float Fraction;
        public Vector3 Position;
        public Vector3 Normal;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct RandomPointAroundData
    {
        public int MapId;
        public Vector3 Start;
        public float Radius;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ExplorePolyRequestHeader
    {
        public int MapId;
        public int Flags;
        public Vector3 Start;
        public float Spacing;
        public int PointCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GetHeightData
    {
        public int MapId;
        public Vector3 Position;
    }
}
