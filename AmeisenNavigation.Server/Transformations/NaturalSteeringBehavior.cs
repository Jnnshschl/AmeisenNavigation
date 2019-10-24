using AmeisenNavigation.Server.Objects;
using System;
using System.Collections.Generic;
using System.Linq;

namespace AmeisenNavigation.Server.Transformations
{
    public class NaturalSteeringBehavior
    {
        public static List<Vector3> Perform(List<Vector3> path, bool avoidObstacles = true, double distanceThreshold = 12.0, float maxVelocity = 2f)
        {
            Random rnd = new Random();
            double rndDistanceDivider = (rnd.NextDouble() + 1) * 2.0;
            double rndThresholdExtender = (rnd.NextDouble() * 6.0) - 3.0;

            List<Vector3> newPath = new List<Vector3>() { path[0] };
            List<Obstacle> obstacles = new List<Obstacle>();

            if (avoidObstacles)
            {
                obstacles = FindPossibleObstacles(path);
            }

            Vector3 lastPosition = path[0];
            for (int i = 1; i < path.Count - 1; ++i)
            {
                double distance = 0;
                do
                {
                    distance = lastPosition.GetDistance(path[i]);

                    Vector3 waypointForce = path[i] - lastPosition;
                    Vector3 obstacleForce = Vector3.Zero;

                    double distanceMultiplier = 1;
                    Obstacle nearObstacles = obstacles.FirstOrDefault(e => e.Position.GetDistance(path[i]) < distanceThreshold / 2);

                    if (avoidObstacles && nearObstacles != null)
                    {
                        Obstacle obstacle = nearObstacles;
                        obstacleForce = obstacle.Position - lastPosition;
                        distanceMultiplier = obstacle.AvoidanceForce * 8;
                    }
                    else
                    {
                        distanceMultiplier = distance / rndDistanceDivider;
                    }

                    Vector3 force = waypointForce + obstacleForce;
                    Vector3 velocity = Utils.Truncate(force, maxVelocity * Convert.ToSingle(distanceMultiplier));
                    Vector3 newPosition = lastPosition + velocity;

                    newPath.Add(newPosition);
                    lastPosition = newPosition;
                } while (distance > distanceThreshold + rndThresholdExtender);
            }

            return newPath;
        }

        private static List<Obstacle> FindPossibleObstacles(List<Vector3> path, double maxAngle = Math.PI / 4.0)
        {
            List<Obstacle> obstacles = new List<Obstacle>();

            for (int i = 1; i < path.Count - 1; ++i)
            {
                Vector3 A = path[i - 1];
                Vector3 B = path[i];
                Vector3 C = path[i + 1];

                double a = B.GetDistance2D(C);
                double b = C.GetDistance2D(A);
                double c = A.GetDistance2D(B);

                //// double alpha = Math.Acos(((b * b) + (c * c) - (a * a)) / 2 * b * c);
                double beta = Math.Acos(((a * a) + (c * c) - (b * b)) / 2 * a * c);
                //// double gamma = 180.0 - alpha - beta;

                if (beta > maxAngle)
                {
                    obstacles.Add(new Obstacle(B, beta));
                }
            }

            return obstacles;
        }
    }
}
