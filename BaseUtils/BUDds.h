#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace BUDds
{
    struct Level
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> data;
        uint32_t depth = 1;
    };

    struct Image
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t fourCC = 0;
        uint32_t blockBytes = 0;
        uint32_t depth = 1;
        bool volume = false;
        uint32_t bitCount = 32;
        uint32_t redMask = 0x00FF0000;
        uint32_t greenMask = 0x0000FF00;
        uint32_t blueMask = 0x000000FF;
        uint32_t alphaMask = 0xFF000000;
        std::vector<Level> levels;
    };

    inline uint32_t ReadU32LE(const uint8_t* p)
    {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }

    inline constexpr uint32_t FourCC(char a, char b, char c, char d)
    {
        return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16) |
               (uint32_t(uint8_t(d)) << 24);
    }

    inline constexpr uint32_t kDXT1 = FourCC('D', 'X', 'T', '1');
    inline constexpr uint32_t kDXT3 = FourCC('D', 'X', 'T', '3');
    inline constexpr uint32_t kDXT5 = FourCC('D', 'X', 'T', '5');
    inline constexpr uint32_t kDX10 = FourCC('D', 'X', '1', '0');

    uint32_t FourCCFromDxgi(uint32_t dxgi);
    uint32_t BlockBytes(uint32_t fourCC);

    inline size_t LevelSize(uint32_t width, uint32_t height, uint32_t blockBytes)
    {
        const size_t blocksWide = std::max(1u, (width + 3) / 4);
        const size_t blocksHigh = std::max(1u, (height + 3) / 4);
        return blocksWide * blocksHigh * blockBytes;
    }

    Image Decode(const uint8_t* bytes, size_t size);
    inline Image Decode(const std::vector<uint8_t>& file)
    {
        return Decode(file.data(), file.size());
    }
    std::vector<uint8_t> Encode(const Image& image);
    std::vector<uint8_t> DecodeRGBA8(const Image& image, size_t level = 0);
} // namespace BUDds
