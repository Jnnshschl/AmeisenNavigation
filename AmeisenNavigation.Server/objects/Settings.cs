public class Settings
{
    public string MmapsFolder { get; } = "C:\\mmaps\\";

    public string IpAddress { get; } = "0.0.0.0";

    public int Port { get; } = 47110;

    public int[] PreloadMaps { get; } = { };
}