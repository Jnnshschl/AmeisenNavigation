using System;
using AmeisenNavigation.Server.Objects.Enums;

namespace AmeisenNavigation.Server.Objects
{
    public class LogEntry
    {
        public string ColoredPart { get; set; }

        public string UncoloredPart { get; set; }

        public ConsoleColor Color { get; set; }

        public LogLevel LogLevel { get; set; }

        public LogEntry(string coloredPart, ConsoleColor color, string uncoloredPart = "", LogLevel logLevel = LogLevel.INFO)
        {
            ColoredPart = coloredPart;
            Color = color;
            UncoloredPart = uncoloredPart;
            LogLevel = logLevel;
        }
    }
}
