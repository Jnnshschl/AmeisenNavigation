using System;
using AmeisenNavigation.Server.Objects.Enums;
using Newtonsoft.Json;

public class Settings
{
    [JsonProperty("mmapsFolder")]
    public string MmapsFolder { get; set; } = "C:\\mmaps\\";

    [JsonProperty("vmapsFolder")]
    public string VmapsFolder { get; set; } = "C:\\vmaps\\";

    [JsonProperty("ipAddress")]
    public string IpAddress { get; set; } = "0.0.0.0";

    [JsonProperty("port")]
    public int Port { get; set; } = 47110;

    [JsonProperty("preloadMaps")]
    public int[] PreloadMaps { get; set; } = { };

    [JsonProperty("logToFile")]
    public bool LogToFile { get; set; } = false;

    [JsonProperty("removeOldLog")]
    public bool RemoveOldLog { get; set; } = true;

    [JsonProperty("logFilePath")]
    public string LogFilePath { get; set; } = AppDomain.CurrentDomain.BaseDirectory + "log.txt";

    [JsonProperty("logLevel")]
    public LogLevel LogLevel { get; set; } = LogLevel.DEBUG;
}