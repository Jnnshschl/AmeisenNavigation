namespace AmeisenNavigation.Client
{
    /// <summary>
    /// Server configuration returned by <see cref="AmeisenNavClient.GetConfig"/>.
    /// </summary>
    public sealed record ServerConfig(int MmapFormat, bool UseAnpFileFormat, string MeshesPath);
}
