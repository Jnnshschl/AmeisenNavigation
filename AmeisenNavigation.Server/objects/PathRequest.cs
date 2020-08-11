using AmeisenNavigation.Server.Objects.Enums;

namespace AmeisenNavigation.Server.Objects
{
    public struct PathRequest
    {
        public PathRequest(int mapId, Vector3 a, Vector3 b, PathRequestFlags flags = PathRequestFlags.None, MovementType movementType = MovementType.FindPath)
        {
            MapId = mapId;
            A = a;
            B = b;
            Flags = flags;
            MovementType = movementType;
        }

        public Vector3 A { get; set; }

        public Vector3 B { get; set; }

        public PathRequestFlags Flags { get; set; }

        public int MapId { get; set; }

        public MovementType MovementType { get; set; }
    }
}