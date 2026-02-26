using System.Collections.Generic;

namespace AmeisenNavigation.Tester
{
    public static class MapDefinitions
    {
        public static readonly Dictionary<int, string> DisplayNames = new()
        {
            [0] = "Eastern Kingdoms",
            [1] = "Kalimdor",
            [530] = "Outland",
            [571] = "Northrend",
        };

        public static readonly Dictionary<int, string> InternalNames = new()
        {
            [0] = "Azeroth",
            [1] = "Kalimdor",
            [530] = "Expansion01",
            [571] = "Northrend",
        };

        public static Dictionary<int, string> GetChoices() => DisplayNames;
    }
}
