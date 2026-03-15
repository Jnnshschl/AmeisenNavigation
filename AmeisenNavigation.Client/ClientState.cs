namespace AmeisenNavigation.Client
{
    /// <summary>
    /// Client state sent to the server to select the base query filter.
    /// Faction states make the pathfinder avoid enemy faction territory.
    /// </summary>
    public enum ClientState : byte
    {
        Normal = 0,
        NormalAlliance = 1,
        NormalHorde = 2,
        Dead = 3,
    }
}
