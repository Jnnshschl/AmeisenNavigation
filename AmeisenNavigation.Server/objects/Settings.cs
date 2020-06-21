using AmeisenNavigation.Server.Objects.Enums;
using Newtonsoft.Json;
using System;

public class Settings
{
    [JsonProperty("ipAddress")]
    public string IpAddress { get; set; } = "0.0.0.0";

    [JsonProperty("logFilePath")]
    public string LogFilePath { get; set; } = AppDomain.CurrentDomain.BaseDirectory + "log.txt";

    [JsonProperty("logLevel")]
    public LogLevel LogLevel { get; set; } = LogLevel.DEBUG;

    [JsonProperty("logToFile")]
    public bool LogToFile { get; set; } = false;

    [JsonProperty("mmapsFolder")]
    public string MmapsFolder { get; set; } = "C:\\mmaps\\";

    [JsonProperty("port")]
    public int Port { get; set; } = 47110;

    [JsonProperty("preloadMaps")]
    public int[] PreloadMaps { get; set; } = { };

    [JsonProperty("removeOldLog")]
    public bool RemoveOldLog { get; set; } = true;

    [JsonProperty("vmapsFolder")]
    public string VmapsFolder { get; set; } = "C:\\vmaps\\";
}