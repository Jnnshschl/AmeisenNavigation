using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace AmeisenNavigation.Server.Objects
{
    public class LogEntry
    {
        public string ColoredPart { get; set; }

        public string UncoloredPart { get; set; }

        public ConsoleColor Color { get; set; }

        public LogEntry(string coloredPart, ConsoleColor color, string uncoloredPart = "")
        {
            ColoredPart = coloredPart;
            Color = color;
            UncoloredPart = uncoloredPart;
        }
    }
}
