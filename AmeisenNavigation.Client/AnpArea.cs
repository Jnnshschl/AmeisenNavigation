namespace AmeisenNavigation.Client
{
    /// <summary>
    /// ANP navmesh area IDs matching the TriAreaId enum in the C++ exporter.
    /// Organized in triplets: {NEUTRAL, ALLIANCE, HORDE} per surface category.
    /// </summary>
    public static class AnpArea
    {
        public const byte TERRAIN_GROUND = 1;
        public const byte ALLIANCE_TERRAIN_GROUND = 2;
        public const byte HORDE_TERRAIN_GROUND = 3;

        public const byte TERRAIN_ROAD = 4;
        public const byte ALLIANCE_TERRAIN_ROAD = 5;
        public const byte HORDE_TERRAIN_ROAD = 6;

        public const byte TERRAIN_CITY = 7;
        public const byte ALLIANCE_TERRAIN_CITY = 8;
        public const byte HORDE_TERRAIN_CITY = 9;

        public const byte WMO = 10;
        public const byte ALLIANCE_WMO = 11;
        public const byte HORDE_WMO = 12;

        public const byte DOODAD = 13;
        public const byte ALLIANCE_DOODAD = 14;
        public const byte HORDE_DOODAD = 15;

        public const byte LIQUID_WATER = 16;
        public const byte ALLIANCE_LIQUID_WATER = 17;
        public const byte HORDE_LIQUID_WATER = 18;

        public const byte LIQUID_OCEAN = 19;
        public const byte ALLIANCE_LIQUID_OCEAN = 20;
        public const byte HORDE_LIQUID_OCEAN = 21;

        public const byte LIQUID_LAVA = 22;
        public const byte ALLIANCE_LIQUID_LAVA = 23;
        public const byte HORDE_LIQUID_LAVA = 24;

        public const byte LIQUID_SLIME = 25;
        public const byte ALLIANCE_LIQUID_SLIME = 26;
        public const byte HORDE_LIQUID_SLIME = 27;
    }
}
