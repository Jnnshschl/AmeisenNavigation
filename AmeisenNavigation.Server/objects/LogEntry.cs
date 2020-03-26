using AmeisenNavigation.Server.Objects.Enums;
using System;

namespace AmeisenNavigation.Server.Objects
{
    public class LogEntry
    {
        public LogEntry(string coloredPart, ConsoleColor color, string uncoloredPart = "", LogLevel logLevel = LogLevel.INFO)
        {
            ColoredPart = coloredPart;
            Color = color;
            UncoloredPart = uncoloredPart;
            LogLevel = logLevel;
        }

        public ConsoleColor Color { get; set; }

        public string ColoredPart { get; set; }

        public LogLevel LogLevel { get; set; }

        public string UncoloredPart { get; set; }
    }
}