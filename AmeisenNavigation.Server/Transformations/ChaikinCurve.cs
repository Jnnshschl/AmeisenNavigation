using AmeisenNavigation.Server.Objects;
using System.Collections.Generic;

namespace AmeisenNavigation.Server.Transformations
{
    public static class ChaikinCurve
    {
        public static List<Vector3> Perform(List<Vector3> path, int iterations = 1)
        {
            // we cant smooth a straight line
            if (path.Count > 1)
            {
                List<Vector3> smoothPath = new List<Vector3>
                {
                    path[0]
                };

                for (int i = 0; i < path.Count - 1; ++i)
                {
                    Vector3 p0 = path[i];
                    Vector3 p1 = path[i + 1];

                    float qx = 0.75f * p0.X + 0.25f * p1.X;
                    float qy = 0.75f * p0.Y + 0.25f * p1.Y;
                    Vector3 Q = new Vector3(qx, qy, p0.Z);

                    float rx = 0.25f * p0.X + 0.75f * p1.X;
                    float ry = 0.25f * p0.Y + 0.75f * p1.Y;
                    Vector3 R = new Vector3(rx, ry, p1.Z);

                    smoothPath.Add(Q);
                    smoothPath.Add(R);
                }

                smoothPath.Add(path[path.Count - 1]);

                if (iterations > 0)
                {
                    return Perform(smoothPath, --iterations);
                }
                else
                {
                    return smoothPath;
                }
            }
            else
            {
                return path;
            }
        }
    }
}