namespace AmeisenNavigation.Server.Objects
{
    public struct RandomPointRequest
    {
        public RandomPointRequest(int mapId, Vector3 a, float maxRadius)
        {
            MapId = mapId;
            A = a;
            MaxRadius = maxRadius;
        }

        public Vector3 A { get; set; }

        public int MapId { get; set; }

        public float MaxRadius { get; set; }
    }
}