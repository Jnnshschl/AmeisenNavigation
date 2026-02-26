using System;
using System.Buffers.Binary;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace AmeisenNavigation.Tester.Services
{
    /// <summary>
    /// Minimal BLP2 texture decoder. Supports DXT1, DXT3, DXT5, and uncompressed palette formats
    /// used by WoW 3.3.5a minimap tiles. Returns frozen WPF BitmapSource (BGRA32).
    /// </summary>
    public static class BlpReader
    {
        private const uint BLP2_MAGIC = 0x32504C42; // "BLP2"

        // BLP2 encoding types
        private const byte ENCODING_UNCOMPRESSED = 1;
        private const byte ENCODING_DXT = 2;

        // Alpha encoding for DXT
        private const byte ALPHA_DXT1 = 0;
        private const byte ALPHA_DXT3 = 1;
        private const byte ALPHA_DXT5 = 7;

        public static BitmapSource? Decode(byte[] data)
        {
            if (data.Length < 148)
                return null;

            var span = data.AsSpan();

            uint magic = BinaryPrimitives.ReadUInt32LittleEndian(span);
            if (magic != BLP2_MAGIC)
                return null;

            // BLP2 header
            // 0x00: magic (4)
            // 0x04: type (4) — 1 = BLP2
            // 0x08: encoding (1) — 1=uncompressed, 2=DXT
            // 0x09: alphaDepth (1) — 0,1,4,8
            // 0x0A: alphaEncoding (1) — 0=DXT1, 1=DXT3, 7=DXT5
            // 0x0B: hasMips (1)
            // 0x0C: width (4)
            // 0x10: height (4)
            // 0x14: mipOffsets[16] (64)
            // 0x54: mipSizes[16] (64)
            // 0x94: palette[256] (1024) — only for encoding=1

            byte encoding = span[0x08];
            byte alphaDepth = span[0x09];
            byte alphaEncoding = span[0x0A];
            int width = (int)BinaryPrimitives.ReadUInt32LittleEndian(span[0x0C..]);
            int height = (int)BinaryPrimitives.ReadUInt32LittleEndian(span[0x10..]);
            uint mip0Offset = BinaryPrimitives.ReadUInt32LittleEndian(span[0x14..]);
            uint mip0Size = BinaryPrimitives.ReadUInt32LittleEndian(span[0x54..]);

            if (width <= 0 || height <= 0 || mip0Offset == 0 || mip0Size == 0)
                return null;
            if (mip0Offset + mip0Size > data.Length)
                return null;

            var mipData = span[(int)mip0Offset..(int)(mip0Offset + mip0Size)];
            byte[] pixels = new byte[width * height * 4]; // BGRA

            if (encoding == ENCODING_DXT)
            {
                if (alphaDepth == 0 || alphaEncoding == ALPHA_DXT1)
                    DecodeDxt1(mipData, pixels, width, height, alphaDepth > 0);
                else if (alphaEncoding == ALPHA_DXT3)
                    DecodeDxt3(mipData, pixels, width, height);
                else if (alphaEncoding == ALPHA_DXT5)
                    DecodeDxt5(mipData, pixels, width, height);
                else
                    return null;
            }
            else if (encoding == ENCODING_UNCOMPRESSED)
            {
                DecodePalette(span, mipData, pixels, width, height, alphaDepth);
            }
            else
            {
                return null;
            }

            var bmp = BitmapSource.Create(width, height, 96, 96, PixelFormats.Bgra32, null, pixels, width * 4);
            bmp.Freeze();
            return bmp;
        }

        private static void DecodeDxt1(ReadOnlySpan<byte> src, byte[] dst, int w, int h, bool hasAlpha)
        {
            int blocksX = (w + 3) / 4;
            int blocksY = (h + 3) / 4;
            int offset = 0;
            Span<byte> colors = stackalloc byte[16];

            for (int by = 0; by < blocksY; by++)
            {
                for (int bx = 0; bx < blocksX; bx++)
                {
                    if (offset + 8 > src.Length) return;

                    ushort c0 = BinaryPrimitives.ReadUInt16LittleEndian(src[offset..]);
                    ushort c1 = BinaryPrimitives.ReadUInt16LittleEndian(src[(offset + 2)..]);
                    uint lookup = BinaryPrimitives.ReadUInt32LittleEndian(src[(offset + 4)..]);
                    offset += 8;

                    Rgb565ToRgb(c0, out byte r0, out byte g0, out byte b0);
                    Rgb565ToRgb(c1, out byte r1, out byte g1, out byte b1);
                    colors[0] = b0; colors[1] = g0; colors[2] = r0; colors[3] = 255;
                    colors[4] = b1; colors[5] = g1; colors[6] = r1; colors[7] = 255;

                    if (c0 > c1)
                    {
                        colors[8] = (byte)((2 * b0 + b1) / 3); colors[9] = (byte)((2 * g0 + g1) / 3);
                        colors[10] = (byte)((2 * r0 + r1) / 3); colors[11] = 255;
                        colors[12] = (byte)((b0 + 2 * b1) / 3); colors[13] = (byte)((g0 + 2 * g1) / 3);
                        colors[14] = (byte)((r0 + 2 * r1) / 3); colors[15] = 255;
                    }
                    else
                    {
                        colors[8] = (byte)((b0 + b1) / 2); colors[9] = (byte)((g0 + g1) / 2);
                        colors[10] = (byte)((r0 + r1) / 2); colors[11] = 255;
                        colors[12] = 0; colors[13] = 0; colors[14] = 0;
                        colors[15] = hasAlpha ? (byte)0 : (byte)255;
                    }

                    for (int y = 0; y < 4; y++)
                    {
                        int py = by * 4 + y;
                        if (py >= h) break;
                        for (int x = 0; x < 4; x++)
                        {
                            int px = bx * 4 + x;
                            if (px >= w) continue;
                            int idx = (int)((lookup >> (2 * (y * 4 + x))) & 3);
                            int dstOff = (py * w + px) * 4;
                            dst[dstOff + 0] = colors[idx * 4 + 0];
                            dst[dstOff + 1] = colors[idx * 4 + 1];
                            dst[dstOff + 2] = colors[idx * 4 + 2];
                            dst[dstOff + 3] = colors[idx * 4 + 3];
                        }
                    }
                }
            }
        }

        private static void DecodeDxt3(ReadOnlySpan<byte> src, byte[] dst, int w, int h)
        {
            int blocksX = (w + 3) / 4;
            int blocksY = (h + 3) / 4;
            int offset = 0;
            Span<byte> colors = stackalloc byte[12];

            for (int by = 0; by < blocksY; by++)
            {
                for (int bx = 0; bx < blocksX; bx++)
                {
                    if (offset + 16 > src.Length) return;

                    ulong alphaBlock = BinaryPrimitives.ReadUInt64LittleEndian(src[offset..]);
                    offset += 8;

                    ushort c0 = BinaryPrimitives.ReadUInt16LittleEndian(src[offset..]);
                    ushort c1 = BinaryPrimitives.ReadUInt16LittleEndian(src[(offset + 2)..]);
                    uint lookup = BinaryPrimitives.ReadUInt32LittleEndian(src[(offset + 4)..]);
                    offset += 8;

                    Rgb565ToRgb(c0, out byte r0, out byte g0, out byte b0);
                    Rgb565ToRgb(c1, out byte r1, out byte g1, out byte b1);
                    colors[0] = b0; colors[1] = g0; colors[2] = r0;
                    colors[3] = b1; colors[4] = g1; colors[5] = r1;
                    colors[6] = (byte)((2 * b0 + b1) / 3); colors[7] = (byte)((2 * g0 + g1) / 3); colors[8] = (byte)((2 * r0 + r1) / 3);
                    colors[9] = (byte)((b0 + 2 * b1) / 3); colors[10] = (byte)((g0 + 2 * g1) / 3); colors[11] = (byte)((r0 + 2 * r1) / 3);

                    for (int y = 0; y < 4; y++)
                    {
                        int py = by * 4 + y;
                        if (py >= h) break;
                        for (int x = 0; x < 4; x++)
                        {
                            int px = bx * 4 + x;
                            if (px >= w) continue;
                            int idx = (int)((lookup >> (2 * (y * 4 + x))) & 3);
                            int alphaIdx = y * 4 + x;
                            byte alpha = (byte)(((alphaBlock >> (alphaIdx * 4)) & 0xF) * 17); // 0-15 → 0-255

                            int dstOff = (py * w + px) * 4;
                            dst[dstOff + 0] = colors[idx * 3 + 0];
                            dst[dstOff + 1] = colors[idx * 3 + 1];
                            dst[dstOff + 2] = colors[idx * 3 + 2];
                            dst[dstOff + 3] = alpha;
                        }
                    }
                }
            }
        }

        private static void DecodeDxt5(ReadOnlySpan<byte> src, byte[] dst, int w, int h)
        {
            int blocksX = (w + 3) / 4;
            int blocksY = (h + 3) / 4;
            int offset = 0;
            Span<byte> alphas = stackalloc byte[8];
            Span<byte> colors = stackalloc byte[12];

            for (int by = 0; by < blocksY; by++)
            {
                for (int bx = 0; bx < blocksX; bx++)
                {
                    if (offset + 16 > src.Length) return;

                    byte alpha0 = src[offset];
                    byte alpha1 = src[offset + 1];

                    ulong alphaBits = 0;
                    for (int i = 0; i < 6; i++)
                        alphaBits |= (ulong)src[offset + 2 + i] << (8 * i);
                    offset += 8;
                    alphas[0] = alpha0;
                    alphas[1] = alpha1;
                    if (alpha0 > alpha1)
                    {
                        alphas[2] = (byte)((6 * alpha0 + 1 * alpha1) / 7);
                        alphas[3] = (byte)((5 * alpha0 + 2 * alpha1) / 7);
                        alphas[4] = (byte)((4 * alpha0 + 3 * alpha1) / 7);
                        alphas[5] = (byte)((3 * alpha0 + 4 * alpha1) / 7);
                        alphas[6] = (byte)((2 * alpha0 + 5 * alpha1) / 7);
                        alphas[7] = (byte)((1 * alpha0 + 6 * alpha1) / 7);
                    }
                    else
                    {
                        alphas[2] = (byte)((4 * alpha0 + 1 * alpha1) / 5);
                        alphas[3] = (byte)((3 * alpha0 + 2 * alpha1) / 5);
                        alphas[4] = (byte)((2 * alpha0 + 3 * alpha1) / 5);
                        alphas[5] = (byte)((1 * alpha0 + 4 * alpha1) / 5);
                        alphas[6] = 0;
                        alphas[7] = 255;
                    }

                    // Color block (same as DXT1 with 4-color mode)
                    ushort c0 = BinaryPrimitives.ReadUInt16LittleEndian(src[offset..]);
                    ushort c1 = BinaryPrimitives.ReadUInt16LittleEndian(src[(offset + 2)..]);
                    uint lookup = BinaryPrimitives.ReadUInt32LittleEndian(src[(offset + 4)..]);
                    offset += 8;

                    Rgb565ToRgb(c0, out byte r0, out byte g0, out byte b0);
                    Rgb565ToRgb(c1, out byte r1, out byte g1, out byte b1);

                    colors[0] = b0; colors[1] = g0; colors[2] = r0;
                    colors[3] = b1; colors[4] = g1; colors[5] = r1;
                    colors[6] = (byte)((2 * b0 + b1) / 3); colors[7] = (byte)((2 * g0 + g1) / 3); colors[8] = (byte)((2 * r0 + r1) / 3);
                    colors[9] = (byte)((b0 + 2 * b1) / 3); colors[10] = (byte)((g0 + 2 * g1) / 3); colors[11] = (byte)((r0 + 2 * r1) / 3);

                    for (int y = 0; y < 4; y++)
                    {
                        int py = by * 4 + y;
                        if (py >= h) break;
                        for (int x = 0; x < 4; x++)
                        {
                            int px = bx * 4 + x;
                            if (px >= w) continue;

                            int colorIdx = (int)((lookup >> (2 * (y * 4 + x))) & 3);
                            int alphaIdx = y * 4 + x;
                            byte a = alphas[(int)((alphaBits >> (3 * alphaIdx)) & 7)];

                            int dstOff = (py * w + px) * 4;
                            dst[dstOff + 0] = colors[colorIdx * 3 + 0];
                            dst[dstOff + 1] = colors[colorIdx * 3 + 1];
                            dst[dstOff + 2] = colors[colorIdx * 3 + 2];
                            dst[dstOff + 3] = a;
                        }
                    }
                }
            }
        }

        private static void DecodePalette(ReadOnlySpan<byte> header, ReadOnlySpan<byte> mipData, byte[] dst, int w, int h, byte alphaDepth)
        {
            // Palette is at offset 0x94, 256 entries of BGRA (1024 bytes)
            if (header.Length < 0x94 + 1024) return;
            var palette = header[0x94..(0x94 + 1024)];

            int pixelCount = w * h;
            if (mipData.Length < pixelCount) return;

            // Alpha data follows indices (if alphaDepth > 0)
            var indices = mipData[..pixelCount];
            var alphaData = mipData.Length > pixelCount ? mipData[pixelCount..] : ReadOnlySpan<byte>.Empty;

            for (int i = 0; i < pixelCount; i++)
            {
                int palOff = indices[i] * 4;
                dst[i * 4 + 0] = palette[palOff + 0]; // B
                dst[i * 4 + 1] = palette[palOff + 1]; // G
                dst[i * 4 + 2] = palette[palOff + 2]; // R

                if (alphaDepth == 8 && alphaData.Length > i)
                    dst[i * 4 + 3] = alphaData[i];
                else if (alphaDepth == 4 && alphaData.Length > i / 2)
                    dst[i * 4 + 3] = (byte)((i % 2 == 0 ? alphaData[i / 2] & 0xF : alphaData[i / 2] >> 4) * 17);
                else if (alphaDepth == 1 && alphaData.Length > i / 8)
                    dst[i * 4 + 3] = (byte)(((alphaData[i / 8] >> (i % 8)) & 1) * 255);
                else
                    dst[i * 4 + 3] = 255;
            }
        }

        private static void Rgb565ToRgb(ushort c, out byte r, out byte g, out byte b)
        {
            r = (byte)(((c >> 11) & 0x1F) * 255 / 31);
            g = (byte)(((c >> 5) & 0x3F) * 255 / 63);
            b = (byte)((c & 0x1F) * 255 / 31);
        }
    }
}
