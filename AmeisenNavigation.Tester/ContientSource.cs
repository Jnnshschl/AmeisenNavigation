using System.Collections.Generic;

namespace AmeisenNavigation.Tester
{
    public static class ContientSource
    {
        public static readonly Dictionary<int, string> Continents = new()
        {
            [0] = "Eastern Kingdoms",
            [1] = "Kalimdor",
            [530] = "Outland",
            [571] = "Northrend",
        };

        public static Dictionary<int, string> GetChoises() => Continents;
    }
}