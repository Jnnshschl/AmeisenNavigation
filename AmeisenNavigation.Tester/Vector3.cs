using System;
using System.Runtime.InteropServices;

namespace AmeisenNavigation.Tester
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Vector3(float x, float y, float z)
    {
        public override readonly string ToString()
            => $"{X}, {Y}, {Z}";

        public float X { get; set; } = x;

        public float Y { get; set; } = y;

        public float Z { get; set; } = z;

        public static Vector3 FromArray(float[] array)
            => new() { X = array[0], Y = array[1], Z = array[2] };

        public static bool operator !=(Vector3 left, Vector3 right)
            => !(left == right);

        public static bool operator ==(Vector3 left, Vector3 right)
            => left.Equals(right);

        public override readonly bool Equals(object obj)
            => obj.GetType() == typeof(Vector3) && ((Vector3)obj).X == X && ((Vector3)obj).Y == Y && ((Vector3)obj).Z == Z;

        public readonly double GetDistance(Vector3 b)
            => MathF.Sqrt(((X - b.X) * (X - b.X)) + ((Y - b.Y) * (Y - b.Y)) + ((Z - b.Z) * (Z - b.Z)));

        public readonly double GetDistance2D(Vector3 b)
            => MathF.Sqrt(MathF.Pow(X - b.X, 2) + MathF.Pow(Y - b.Y, 2));

        public override readonly int GetHashCode()
        {
            unchecked
            {
                return (int)(17 + (X * 23) + (Y * 23) + (Z * 23));
            }
        }

        public readonly float[] ToArray()
            => [X, Y, Z];
    }
}