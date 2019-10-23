using AmeisenNavigation.Server.Objects;

namespace AmeisenNavigation.Server
{
    public static class Utils
    {
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
