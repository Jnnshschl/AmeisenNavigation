using AmeisenNavigation.Client;
using System;
using System.Collections.Concurrent;
using System.Windows.Media;

namespace AmeisenNavigation.Tester.Services
{
    /// <summary>
    /// Maps TriAreaId (0-27) to colors for navmesh overlay visualization.
    /// Color scheme matches C++ BmpRenderer::GetAreaColor() for consistency
    /// between debug BMPs and tester overlay.
    ///
    /// Area IDs follow a repeating triplet pattern:
    ///   {NEUTRAL, ALLIANCE, HORDE} per terrain category.
    ///   faction = (areaId - 1) % 3: 0=neutral, 1=alliance, 2=horde
    ///   baseArea = ((areaId - 1) / 3) * 3 + 1
    ///
    /// Brushes are cached by (areaId, opacity) so each unique combination
    /// is only allocated once, eliminating per-frame GC pressure.
    /// </summary>
    public static class AreaColors
    {
        private static readonly Color[] BaseColors = new Color[28];
        private static readonly SolidColorBrush TransparentBrush;

        // Cache: key = (areaId << 8) | opacity -> frozen brush
        private static readonly ConcurrentDictionary<int, SolidColorBrush> BrushCache = new();

        static AreaColors()
        {
            TransparentBrush = new SolidColorBrush(Colors.Transparent);
            TransparentBrush.Freeze();

            for (int i = 0; i < 28; i++)
                BaseColors[i] = ComputeAreaColor((byte)i);
        }

        /// <summary>
        /// Get a frozen brush for the given area ID at a specific opacity (0-255).
        /// Returns a cached semi-transparent brush - no allocation after first call
        /// for each unique (areaId, opacity) pair.
        /// </summary>
        public static SolidColorBrush GetBrush(byte areaId, byte opacity = 100)
        {
            if (areaId == 0 || areaId > 27)
                return TransparentBrush;

            int key = (areaId << 8) | opacity;

            return BrushCache.GetOrAdd(key, _ =>
            {
                var c = BaseColors[areaId];
                var brush = new SolidColorBrush(Color.FromArgb(opacity, c.R, c.G, c.B));
                brush.Freeze();
                return brush;
            });
        }

        /// <summary>
        /// Get a frozen brush with full opacity for legend/UI purposes.
        /// </summary>
        public static SolidColorBrush GetSolidBrush(byte areaId)
        {
            return GetBrush(areaId, 255);
        }

        /// <summary>
        /// Get the human-readable name for an area ID.
        /// </summary>
        public static string GetAreaName(byte areaId) => areaId switch
        {
            AnpArea.TERRAIN_GROUND => "Ground",
            AnpArea.ALLIANCE_TERRAIN_GROUND => "Ground (Alliance)",
            AnpArea.HORDE_TERRAIN_GROUND => "Ground (Horde)",
            AnpArea.TERRAIN_ROAD => "Road",
            AnpArea.ALLIANCE_TERRAIN_ROAD => "Road (Alliance)",
            AnpArea.HORDE_TERRAIN_ROAD => "Road (Horde)",
            AnpArea.TERRAIN_CITY => "City",
            AnpArea.ALLIANCE_TERRAIN_CITY => "City (Alliance)",
            AnpArea.HORDE_TERRAIN_CITY => "City (Horde)",
            AnpArea.WMO => "WMO",
            AnpArea.ALLIANCE_WMO => "WMO (Alliance)",
            AnpArea.HORDE_WMO => "WMO (Horde)",
            AnpArea.DOODAD => "Doodad",
            AnpArea.ALLIANCE_DOODAD => "Doodad (Alliance)",
            AnpArea.HORDE_DOODAD => "Doodad (Horde)",
            AnpArea.LIQUID_WATER => "Water",
            AnpArea.ALLIANCE_LIQUID_WATER => "Water (Alliance)",
            AnpArea.HORDE_LIQUID_WATER => "Water (Horde)",
            AnpArea.LIQUID_OCEAN => "Ocean",
            AnpArea.ALLIANCE_LIQUID_OCEAN => "Ocean (Alliance)",
            AnpArea.HORDE_LIQUID_OCEAN => "Ocean (Horde)",
            AnpArea.LIQUID_LAVA => "Lava",
            AnpArea.ALLIANCE_LIQUID_LAVA => "Lava (Alliance)",
            AnpArea.HORDE_LIQUID_LAVA => "Lava (Horde)",
            AnpArea.LIQUID_SLIME => "Slime",
            AnpArea.ALLIANCE_LIQUID_SLIME => "Slime (Alliance)",
            AnpArea.HORDE_LIQUID_SLIME => "Slime (Horde)",
            _ => $"Unknown ({areaId})"
        };

        private static Color ComputeAreaColor(byte areaId)
        {
            if (areaId == 0)
                return Colors.Transparent;

            int faction = (areaId - 1) % 3;
            byte baseArea = (byte)(((areaId - 1) / 3) * 3 + 1);

            byte r, g, b;
            switch (baseArea)
            {
                case AnpArea.TERRAIN_GROUND: r = 0; g = 150; b = 0; break;
                case AnpArea.TERRAIN_ROAD: r = 100; g = 100; b = 100; break;
                case AnpArea.TERRAIN_CITY: r = 150; g = 150; b = 0; break;
                case AnpArea.WMO: r = 200; g = 200; b = 200; break;
                case AnpArea.DOODAD: r = 139; g = 69; b = 19; break;
                case AnpArea.LIQUID_WATER:
                case AnpArea.LIQUID_OCEAN: r = 0; g = 0; b = 255; break;
                case AnpArea.LIQUID_LAVA: r = 255; g = 0; b = 0; break;
                case AnpArea.LIQUID_SLIME: r = 150; g = 0; b = 150; break;
                default: r = 255; g = 255; b = 255; break;
            }

            if (faction == 1) // Alliance
            {
                r = (byte)(r * 0.4f);
                g = (byte)(g * 0.6f);
                b = (byte)Math.Min(255, b * 0.6f + 130);
            }
            else if (faction == 2) // Horde
            {
                r = (byte)Math.Min(255, r * 0.6f + 130);
                g = (byte)(g * 0.5f);
                b = (byte)(b * 0.3f);
            }

            return Color.FromRgb(r, g, b);
        }
    }
}
