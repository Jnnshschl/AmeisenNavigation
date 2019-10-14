namespace AmeisenNavigation.Server.Objects
{
    public struct Vector3
    {
        public Vector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public float X { get; set; }

        public float Y { get; set; }

        public float Z { get; set; }

        public static bool operator ==(Vector3 left, Vector3 right)
        {
            return left.Equals(right);
        }

        public static bool operator !=(Vector3 left, Vector3 right)
        {
            return !(left == right);
        }

        public static Vector3 FromArray(float[] array)
            => new Vector3()
            {
                X = array[0],
                Y = array[1],
                Z = array[2]
            };

        public float[] ToArray()
            => new float[3]
            {
                X,
                Y,
                Z
            };

        public override bool Equals(object obj)
            => obj.GetType() == typeof(Vector3)
            && ((Vector3)obj).X == X
            && ((Vector3)obj).Y == Y
            && ((Vector3)obj).Z == Z;

        public override int GetHashCode()
        {
            unchecked
            {
                return (int)(17 + (X * 23) + (Y * 23) + (Z * 23));
            }
        }
    }
}
