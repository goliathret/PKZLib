#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace BUDds
{
    struct Level
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> data;
    };

    struct Image
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t fourCC = 0;
        uint32_t blockBytes = 0;
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

    inline uint32_t FourCCFromDxgi(uint32_t dxgi)
    {
        switch (dxgi)
        {
        case 70: case 71: case 72: return kDXT1;
        case 73: case 74: case 75: return kDXT3;
        case 76: case 77: case 78: return kDXT5;
        default: return 0;
        }
    }

    inline uint32_t BlockBytes(uint32_t fourCC)
    {
        return fourCC == kDXT1 ? 8u : (fourCC == kDXT3 || fourCC == kDXT5) ? 16u : 0u;
    }

    inline size_t LevelSize(uint32_t width, uint32_t height, uint32_t blockBytes)
    {
        const size_t bw = (width + 3) / 4 ? (width + 3) / 4 : 1;
        const size_t bh = (height + 3) / 4 ? (height + 3) / 4 : 1;
        return bw * bh * blockBytes;
    }

    inline Image Decode(const uint8_t* bytes, size_t size)
    {
        if (size < 128 || ReadU32LE(bytes) != FourCC('D', 'D', 'S', ' '))
            throw std::runtime_error("BUDds: not a DDS file");
        if (ReadU32LE(bytes + 4) != 124)
            throw std::runtime_error("BUDds: unexpected header size");

        Image img;
        img.height = ReadU32LE(bytes + 12);
        img.width = ReadU32LE(bytes + 16);
        const uint32_t mipCount = ReadU32LE(bytes + 28);
        const uint32_t pfFlags = ReadU32LE(bytes + 80);
        uint32_t fourCC = ReadU32LE(bytes + 84);
        size_t offset = 128;

        if (fourCC == kDX10)
        {
            if (size < 148)
                throw std::runtime_error("BUDds: truncated DX10 header");
            const uint32_t dxgi = ReadU32LE(bytes + 128);
            fourCC = FourCCFromDxgi(dxgi);
            if (!fourCC)
                throw std::runtime_error("BUDds: DXGI format " + std::to_string(dxgi) + " is not BC1/BC2/BC3");
            offset = 148;
        }
        else if ((pfFlags & 0x4) == 0)
        {
            throw std::runtime_error("BUDds: only block-compressed DDS files are read (use `png` for uncompressed art)");
        }

        img.fourCC = fourCC;
        img.blockBytes = BlockBytes(fourCC);
        if (!img.blockBytes)
            throw std::runtime_error("BUDds: unsupported FourCC");
        if (!img.width || !img.height)
            throw std::runtime_error("BUDds: zero-sized image");

        const uint32_t levels = mipCount ? mipCount : 1u;
        uint32_t w = img.width, h = img.height;
        for (uint32_t i = 0; i < levels; ++i)
        {
            const size_t need = LevelSize(w, h, img.blockBytes);
            if (offset + need > size)
                throw std::runtime_error("BUDds: file ends inside level " + std::to_string(i));
            Level level;
            level.width = w;
            level.height = h;
            level.data.assign(bytes + offset, bytes + offset + need);
            img.levels.push_back(std::move(level));
            offset += need;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        return img;
    }

    inline Image Decode(const std::vector<uint8_t>& file) { return Decode(file.data(), file.size()); }
}
