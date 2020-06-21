using AmeisenNavigation.Server.Objects;
using System.Text;

namespace AmeisenNavigation.Server
{
    public static class Utils
    {
        public static string CleanString(string input)
        {
            StringBuilder sb = new StringBuilder(input.Length);

            foreach (char c in input)
            {
                if (c != '\n' && c != '\r' && c != '\t')
                {
                    sb.Append(c);
                }
            }

            return sb.ToString();
        }

        public static Vector3 Normalize(Vector3 vector, float max)
                    => new Vector3(vector.X / max, vector.Y / max, vector.Z / max);

        public static Vector3 Truncate(Vector3 vector, float max)
            => new Vector3(
                vector.X < 0 ?
                    vector.X <= max * -1 ? max * -1 : vector.X
                    : vector.X >= max ? max : vector.X,
                vector.Y < 0 ?
                    vector.Y <= max * -1 ? max * -1 : vector.Y
                    : vector.Y >= max ? max : vector.Y,
                vector.Z < 0 ?
                    vector.Z <= max * -1 ? max * -1 : vector.Z
                    : vector.Z >= max ? max : vector.Z);
    }
}