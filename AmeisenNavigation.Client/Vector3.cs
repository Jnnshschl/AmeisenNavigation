using System;
using System.Runtime.InteropServices;

namespace AmeisenNavigation.Client
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Vector3(float x, float y, float z)
    {
        public float X = x;
        public float Y = y;
        public float Z = z;

        public readonly bool IsZero => X == 0 && Y == 0 && Z == 0;

        public readonly float DistanceTo(Vector3 b)
            => MathF.Sqrt((X - b.X) * (X - b.X) + (Y - b.Y) * (Y - b.Y) + (Z - b.Z) * (Z - b.Z));

        public readonly float DistanceTo2D(Vector3 b)
            => MathF.Sqrt((X - b.X) * (X - b.X) + (Y - b.Y) * (Y - b.Y));

        public static bool operator ==(Vector3 left, Vector3 right)
            => left.X == right.X && left.Y == right.Y && left.Z == right.Z;

        public static bool operator !=(Vector3 left, Vector3 right)
            => !(left == right);

        public override readonly bool Equals(object? obj)
            => obj is Vector3 v && this == v;

        public override readonly int GetHashCode()
            => HashCode.Combine(X, Y, Z);

        public override readonly string ToString() => $"({X:F2}, {Y:F2}, {Z:F2})";
    }
}
