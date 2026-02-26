namespace AmeisenNavigation.Tester.Converters
{
    public static class WowCoordinateConverter
    {
        public const float TileSize = 533.33333f;
        public const float WorldSize = TileSize * 32.0f;
        public const int TileCount = 64;
        public const int TilePixelSize = 256;
        public const int WorldPixelSize = TileCount * TilePixelSize;

        public static (int tileX, int tileY) WorldToTile(float worldX, float worldY)
        {
            int tileX = (int)(32.0f - (worldX / TileSize));
            int tileY = (int)(32.0f - (worldY / TileSize));
            return (tileX, tileY);
        }

        public static (float worldX, float worldY) TileToWorld(int tileX, int tileY)
        {
            float worldX = (32 - tileX) * TileSize;
            float worldY = (32 - tileY) * TileSize;
            return (worldX, worldY);
        }

        /// <summary>
        /// Convert world coordinates to pixel position on the full virtual canvas.
        /// WoW X = north/south (maps to canvas Y), WoW Y = east/west (maps to canvas X).
        /// </summary>
        public static (double pixelX, double pixelY) WorldToPixel(float worldX, float worldY)
        {
            double fracTileX = 32.0 - worldX / TileSize;
            double fracTileY = 32.0 - worldY / TileSize;
            return (fracTileY * TilePixelSize, fracTileX * TilePixelSize);
        }

        /// <summary>
        /// Convert canvas pixel position back to world coordinates.
        /// </summary>
        public static (float worldX, float worldY) PixelToWorld(double pixelX, double pixelY)
        {
            float worldX = (float)((32.0 - pixelY / TilePixelSize) * TileSize);
            float worldY = (float)((32.0 - pixelX / TilePixelSize) * TileSize);
            return (worldX, worldY);
        }
    }
}
