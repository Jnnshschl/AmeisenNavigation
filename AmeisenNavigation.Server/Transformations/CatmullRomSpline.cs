using AmeisenNavigation.Server.Objects;
using System;
using System.Collections.Generic;

namespace AmeisenNavigation.Server.Transformations
{
    public static class CatmullRomSpline
    {
        public static List<Vector3> Perform(List<Vector3> input, int numberOfPoints)
        {
            List<Vector3> splinePoints = new List<Vector3> { input[0] };

            for (int i = 1; i < input.Count - 2; ++i)
            {
                Vector3 p0 = input[i - 1];
                Vector3 p1 = input[i];
                Vector3 p2 = input[i + 1];
                Vector3 p3 = input[i + 2];

                float t0 = 0.0f;
                float t1 = GetT(t0, p0, p1);
                float t2 = GetT(t1, p1, p2);
                float t3 = GetT(t2, p2, p3);

                for (float t = t1; t < t2; t += ((t2 - t1) / (float)numberOfPoints))
                {
                    Vector3 A1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1;
                    Vector3 A2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2;
                    Vector3 A3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3;

                    Vector3 B1 = (t2 - t) / (t2 - t0) * A1 + (t - t0) / (t2 - t0) * A2;
                    Vector3 B2 = (t3 - t) / (t3 - t1) * A2 + (t - t1) / (t3 - t1) * A3;

                    Vector3 C = (t2 - t) / (t2 - t1) * B1 + (t - t1) / (t2 - t1) * B2;

                    splinePoints.Add(C);
                }
            }

            splinePoints.Add(input[input.Count - 1]);
            return splinePoints;
        }

        private static float GetT(float t, Vector3 p0, Vector3 p1)
        {
            float a = (float)(Math.Pow(p1.X - p0.X, 2.0f) + Math.Pow(p1.Y - p0.Y, 2.0f) + Math.Pow(p1.Z - p0.Z, 2.0f));
            float b = (float)Math.Pow(a, 0.5f);

            return b + t;
        }
    }
}