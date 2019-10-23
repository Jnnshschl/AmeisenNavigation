using System;

namespace AmeisenNavigation.Server.Objects
{
    public class Obstacle
    {
        public Vector3 Position { get; set; }

        public double Angle { get; set; }

        public double AvoidanceForce { get; set; }

        public Obstacle(Vector3 position, double angle)
        {
            Position = position;
            Angle = angle;
            AvoidanceForce = angle * (180.0 / Math.PI) / 360;
        }
    }
}
