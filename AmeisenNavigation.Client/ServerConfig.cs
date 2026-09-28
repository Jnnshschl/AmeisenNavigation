namespace AmeisenNavigation.Client
{
    /// <summary>
    /// Server configuration returned by <see cref="AmeisenNavClient.GetConfig"/>.
    /// </summary>
    /// <param name="MmapFormat">-1 custom, 0 unknown/auto, 1 TrinityCore 3.3.5a, 2 SkyFire 5.4.8.</param>
    /// <param name="UseAnpFileFormat">True if the server uses ANP navmeshes (area costs apply).</param>
    /// <param name="MeshesPath">The server's navmesh folder.</param>
    /// <param name="ProtocolVersion">1 for servers without version info, 2+ supports ExplorePolygon and RequireComplete.</param>
    /// <param name="MaxPointPath">Maximum number of points in a returned path (0 if unknown).</param>
    /// <param name="ServerVersion">Server version string (empty if unknown).</param>
    public sealed record ServerConfig(int MmapFormat, bool UseAnpFileFormat, string MeshesPath,
                                      int ProtocolVersion = 1, int MaxPointPath = 0, string ServerVersion = "");
}
