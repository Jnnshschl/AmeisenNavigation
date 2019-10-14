using Newtonsoft.Json;

public class Settings
{
    [JsonProperty("mmapsFolder")]
    public string MmapsFolder { get; set; } = "C:\\mmaps\\";

    [JsonProperty("ipAddress")]
    public string IpAddress { get; set; } = "0.0.0.0";

    [JsonProperty("port")]
    public int Port { get; set; } = 47110;

    [JsonProperty("preloadMaps")]
    public int[] PreloadMaps { get; set; } = { };
}