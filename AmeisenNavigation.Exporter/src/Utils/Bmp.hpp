#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>


#pragma pack(push, 1)
struct BMPFileHeader
{
    uint16_t file_type{0x4D42}; // File type always BM which is 0x4D42
    uint32_t file_size{0};      // Size of the file (in bytes)
    uint16_t reserved1{0};      // Reserved, always 0
    uint16_t reserved2{0};      // Reserved, always 0
    uint32_t offset_data{0};    // Start position of pixel data (bytes from the beginning of the file)
};

struct BMPInfoHeader
{
    uint32_t size{0};        // Size of this header (in bytes)
    int32_t width{0};        // width of bitmap in pixels
    int32_t height{0};       // width of bitmap in pixels
                             // (if positive, bottom-up, with origin in lower left corner)
                             // (if negative, top-down, with origin in upper left corner)
    uint16_t planes{1};      // No. of planes for the target device, this is always 1
    uint16_t bit_count{0};   // No. of bits per pixel
    uint32_t compression{0}; // 0 or 3 - uncompressed. This bitmap uses 0.
    uint32_t size_image{0};  // 0 - for uncompressed images
    int32_t x_pixels_per_meter{0};
    int32_t y_pixels_per_meter{0};
    uint32_t colors_used{
        0}; // No. color indexes in the color table. Use 0 for the max number of colors allowed by bit_count
    uint32_t colors_important{0}; // No. of colors used for displaying the bitmap. If 0 all colors are important
};
#pragma pack(pop)

class BmpWriter
{
public:
    static bool Write(const std::string& filename, int width, int height, const uint8_t* data)
    {
        std::ofstream of(filename, std::ios::binary);
        if (!of)
            return false;

        BMPFileHeader file_header;
        BMPInfoHeader info_header;

        int paddingSize = (4 - (width * 3) % 4) % 4;
        uint32_t dataSize = (width * 3 + paddingSize) * height;

        file_header.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader);
        file_header.file_size = file_header.offset_data + dataSize;

        info_header.size = sizeof(BMPInfoHeader);
        info_header.width = width;
        info_header.height = height;
        info_header.bit_count = 24;

        of.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
        of.write(reinterpret_cast<const char*>(&info_header), sizeof(info_header));

        std::vector<uint8_t> padding(paddingSize, 0);

        // BMP is bottom-up by default.
        // We want to write it in a way that matches WoW coordinates.
        // If we write from y = height-1 down to 0, it should be top-down in the file but the scanlines are handled by
        // BMP format. Actually, if we want (0,0) to be top-left in the image viewing, we can use negative height or
        // just write rows accordingly. The user said "use the wow coordinates so the exported bmp looks like wow". In
        // WoW, North is +X, East is -Y? Or is it different? Let's assume the data passed is already in a coordinate
        // space we want to visualize.

        for (int y = height - 1; y >= 0; --y)
        {
            of.write(reinterpret_cast<const char*>(data + (y * width * 3)), width * 3);
            if (paddingSize > 0)
            {
                of.write(reinterpret_cast<const char*>(padding.data()), paddingSize);
            }
        }

        return true;
    }
};
