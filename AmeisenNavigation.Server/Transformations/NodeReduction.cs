using AmeisenNavigation.Server.Objects;
using System;
using System.Collections.Generic;

namespace AmeisenNavigation.Server.Transformations
{
    public static class NodeReduction
    {
        public static List<Vector3> Perform(List<Vector3> path, double minAngle = 0.2)
        {
            List<Vector3> newPath = new List<Vector3>();

            if (path.Count < 3)
            {
                return newPath;
            }
            
            double lastAngleA = 0.0;
            double lastAngleB = 0.0;

            for (int i = 1; i < path.Count - 1; ++i)
            {
                Vector3 a = path[i - 1];
                Vector3 b = path[i];
                Vector3 c = path[i + 1];

                double angleA = Math.Atan2(b.Y - a.Y, b.X - a.X);
                double angleB = Math.Atan2(b.Y - c.Y, b.X - c.X);

                if (angleA < 0)
                {
                    angleA *= -1;
                }

                if (angleB < 0)
                {
                    angleB *= -1;
                }

                double angeDiffA = angleA - lastAngleA;
                double angeDiffB = angleB - lastAngleB;

                if (angeDiffA < 0)
                {
                    angeDiffA *= -1;
                }

                if (angeDiffB < 0)
                {
                    angeDiffB *= -1;
                }

                if (angeDiffA > minAngle || angeDiffB > minAngle)
                {
                    newPath.Add(b);
                }

                lastAngleA = angleA;
                lastAngleB = angleB;
            }

            return newPath;
        }
    }
}
