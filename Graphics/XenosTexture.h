#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace XenosTexture
{

    enum Format : uint32_t
    {
        kFormat_8 = 2,
        kFormat_8_8_8_8 = 6,
        kFormat_DXT1 = 18,
        kFormat_DXT2_3 = 19,
        kFormat_DXT4_5 = 20,
        kFormat_DXN = 26,
        kFormat_DXT1_AS_16_16_16_16 = 51,
        kFormat_DXT2_3_AS_16_16_16_16 = 52,
        kFormat_DXT4_5_AS_16_16_16_16 = 53,
    };

    struct D3DFormat
    {
        uint32_t raw = 0;
        uint32_t format = 0;
        uint32_t endian = 0;
        bool tiled = false;
        uint32_t signX = 0, signY = 0, signZ = 0, signW = 0;
        uint32_t numFormat = 0;
        uint32_t swizzle[4] = {};

        static D3DFormat Decode(uint32_t raw)
        {
            D3DFormat f;
            f.raw = raw;
            f.format = raw & 0x3F;
            f.endian = (raw >> 6) & 3;
            f.tiled = ((raw >> 8) & 1) != 0;
            f.signX = (raw >> 9) & 3;
            f.signY = (raw >> 11) & 3;
            f.signZ = (raw >> 13) & 3;
            f.signW = (raw >> 15) & 3;
            f.numFormat = (raw >> 17) & 1;
            for (int i = 0; i < 4; ++i)
                f.swizzle[i] = (raw >> (18 + 3 * i)) & 7;
            return f;
        }

        static constexpr uint32_t kDXT5Tiled = 0x1A207F54;
        static constexpr uint32_t kDXT1Tiled = 0x1A207F52;
        static constexpr uint32_t kDXT1TiledLinearColor = 0x1A200152;
        static constexpr uint32_t kA8R8G8B8Tiled = 0x18287F86;

        bool IsBlockCompressed() const
        {
            return format == kFormat_DXT1 || format == kFormat_DXT2_3 || format == kFormat_DXT4_5 || format == kFormat_DXN ||
                   format == kFormat_DXT1_AS_16_16_16_16 || format == kFormat_DXT2_3_AS_16_16_16_16 ||
                   format == kFormat_DXT4_5_AS_16_16_16_16;
        }
        uint32_t BlockDim() const { return IsBlockCompressed() ? 4 : 1; }
        uint32_t BytesPerBlock() const
        {
            switch (format)
            {
            case kFormat_8: return 1;
            case kFormat_8_8_8_8: return 4;
            case kFormat_DXT1: case kFormat_DXT1_AS_16_16_16_16: return 8;
            case kFormat_DXT2_3: case kFormat_DXT4_5: case kFormat_DXN:
            case kFormat_DXT2_3_AS_16_16_16_16: case kFormat_DXT4_5_AS_16_16_16_16: return 16;
            default: return 0;
            }
        }
        const char* Name() const
        {
            switch (format)
            {
            case kFormat_8: return "8";
            case kFormat_8_8_8_8: return "8_8_8_8";
            case kFormat_DXT1: return "DXT1";
            case kFormat_DXT2_3: return "DXT2_3";
            case kFormat_DXT4_5: return "DXT4_5";
            case kFormat_DXN: return "DXN";
            case kFormat_DXT1_AS_16_16_16_16: return "DXT1_AS_16_16_16_16";
            case kFormat_DXT2_3_AS_16_16_16_16: return "DXT2_3_AS_16_16_16_16";
            case kFormat_DXT4_5_AS_16_16_16_16: return "DXT4_5_AS_16_16_16_16";
            default: return "unknown";
            }
        }
    };

    inline uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }
    inline uint32_t Log2(uint32_t v) { uint32_t r = 0; while ((1u << r) < v) ++r; return r; }

    inline uint32_t TiledOffset2D(uint32_t x, uint32_t y, uint32_t pitchBlocks, uint32_t bpbLog2)
    {
        const uint32_t macro = ((x >> 5) + (y >> 5) * (pitchBlocks >> 5)) << (bpbLog2 + 7);
        const uint32_t micro = ((x & 7) + ((y & 6) << 2)) << bpbLog2;
        const uint32_t offset = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 8) << (3 + bpbLog2)) + ((y & 1) << 4);
        return ((offset & ~511u) << 3) + ((offset & 448) << 2) + (offset & 63) + ((y & 16) << 7) +
               (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
    }

    inline uint32_t TiledSurfaceSize(uint32_t width, uint32_t height, const D3DFormat& f)
    {
        const uint32_t bd = f.BlockDim();
        const uint32_t bw = AlignUp((width + bd - 1) / bd, 32);
        const uint32_t bh = AlignUp((height + bd - 1) / bd, 32);
        return bw * bh * f.BytesPerBlock();
    }

    inline void SwapEndian(uint8_t* p, size_t n, uint32_t endian)
    {
        if (endian == 1)
        {
            for (size_t i = 0; i + 1 < n; i += 2)
                std::swap(p[i], p[i + 1]);
        }
        else if (endian == 2)
        {
            for (size_t i = 0; i + 3 < n; i += 4)
            {
                std::swap(p[i], p[i + 3]);
                std::swap(p[i + 1], p[i + 2]);
            }
        }
        else if (endian == 3)
        {
            for (size_t i = 0; i + 3 < n; i += 4)
            {
                std::swap(p[i], p[i + 2]);
                std::swap(p[i + 1], p[i + 3]);
            }
        }
    }

    inline std::vector<uint8_t> Untile(const uint8_t* data, size_t size, uint32_t width, uint32_t height, const D3DFormat& f)
    {
        const uint32_t bd = f.BlockDim();
        const uint32_t bpb = f.BytesPerBlock();
        if (!bpb)
            throw std::runtime_error(std::string("XenosTexture: unsupported format ") + f.Name());
        const uint32_t blocksW = (width + bd - 1) / bd;
        const uint32_t blocksH = (height + bd - 1) / bd;
        const uint32_t pitch = AlignUp(blocksW, 32);
        const uint32_t bpbLog2 = Log2(bpb);
        std::vector<uint8_t> out(size_t(blocksW) * blocksH * bpb);
        for (uint32_t y = 0; y < blocksH; ++y)
        {
            for (uint32_t x = 0; x < blocksW; ++x)
            {
                const size_t src = f.tiled ? TiledOffset2D(x, y, pitch, bpbLog2) : (size_t(y) * pitch + x) * bpb;
                if (src + bpb > size)
                    throw std::runtime_error("XenosTexture: tiled data shorter than the surface");
                uint8_t* dst = out.data() + (size_t(y) * blocksW + x) * bpb;
                std::memcpy(dst, data + src, bpb);
                SwapEndian(dst, bpb, f.endian);
            }
        }
        return out;
    }

    inline std::vector<uint8_t> Tile(const uint8_t* linear, uint32_t width, uint32_t height, const D3DFormat& f)
    {
        const uint32_t bd = f.BlockDim();
        const uint32_t bpb = f.BytesPerBlock();
        if (!bpb)
            throw std::runtime_error(std::string("XenosTexture: unsupported format ") + f.Name());
        const uint32_t blocksW = (width + bd - 1) / bd;
        const uint32_t blocksH = (height + bd - 1) / bd;
        const uint32_t pitch = AlignUp(blocksW, 32);
        const uint32_t bpbLog2 = Log2(bpb);
        std::vector<uint8_t> out(TiledSurfaceSize(width, height, f), 0);
        for (uint32_t y = 0; y < blocksH; ++y)
        {
            for (uint32_t x = 0; x < blocksW; ++x)
            {
                const size_t dst = f.tiled ? TiledOffset2D(x, y, pitch, bpbLog2) : (size_t(y) * pitch + x) * bpb;
                std::memcpy(out.data() + dst, linear + (size_t(y) * blocksW + x) * bpb, bpb);
                SwapEndian(out.data() + dst, bpb, f.endian);
            }
        }
        return out;
    }

    inline void Unpack565(uint16_t c, uint8_t* rgb)
    {
        const uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        rgb[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
        rgb[1] = static_cast<uint8_t>((g << 2) | (g >> 4));
        rgb[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
    }

    inline void DecodeDXT1Block(const uint8_t* b, uint8_t* rgba, bool allowAlpha = true)
    {
        const uint16_t c0 = uint16_t(b[0] | (b[1] << 8)), c1 = uint16_t(b[2] | (b[3] << 8));
        uint8_t pal[4][4];
        Unpack565(c0, pal[0]); pal[0][3] = 255;
        Unpack565(c1, pal[1]); pal[1][3] = 255;
        if (c0 > c1 || !allowAlpha)
        {
            for (int i = 0; i < 3; ++i)
            {
                pal[2][i] = static_cast<uint8_t>((2 * pal[0][i] + pal[1][i] + 1) / 3);
                pal[3][i] = static_cast<uint8_t>((pal[0][i] + 2 * pal[1][i] + 1) / 3);
            }
            pal[2][3] = pal[3][3] = 255;
        }
        else
        {
            for (int i = 0; i < 3; ++i)
            {
                pal[2][i] = static_cast<uint8_t>((pal[0][i] + pal[1][i]) / 2);
                pal[3][i] = 0;
            }
            pal[2][3] = 255;
            pal[3][3] = 0;
        }
        const uint32_t idx = uint32_t(b[4]) | (uint32_t(b[5]) << 8) | (uint32_t(b[6]) << 16) | (uint32_t(b[7]) << 24);
        for (int p = 0; p < 16; ++p)
            std::memcpy(rgba + p * 4, pal[(idx >> (2 * p)) & 3], 4);
    }

    inline void DecodeDXT5Block(const uint8_t* b, uint8_t* rgba)
    {
        DecodeDXT1Block(b + 8, rgba, false);
        const uint8_t a0 = b[0], a1 = b[1];
        uint8_t apal[8] = { a0, a1 };
        if (a0 > a1)
            for (int i = 1; i < 7; ++i)
                apal[i + 1] = static_cast<uint8_t>(((7 - i) * a0 + i * a1 + 3) / 7);
        else
        {
            for (int i = 1; i < 5; ++i)
                apal[i + 1] = static_cast<uint8_t>(((5 - i) * a0 + i * a1 + 2) / 5);
            apal[6] = 0;
            apal[7] = 255;
        }
        uint64_t bits = 0;
        for (int i = 0; i < 6; ++i)
            bits |= uint64_t(b[2 + i]) << (8 * i);
        for (int p = 0; p < 16; ++p)
            rgba[p * 4 + 3] = apal[(bits >> (3 * p)) & 7];
    }

    inline void DecodeDXT3Block(const uint8_t* b, uint8_t* rgba)
    {
        DecodeDXT1Block(b + 8, rgba, false);
        for (int p = 0; p < 16; ++p)
        {
            const uint8_t a4 = (b[p / 2] >> ((p & 1) * 4)) & 15;
            rgba[p * 4 + 3] = static_cast<uint8_t>(a4 * 17);
        }
    }

    inline std::vector<uint8_t> DecodeToRGBA8(const std::vector<uint8_t>& linear, uint32_t width, uint32_t height,
                                              const D3DFormat& f)
    {
        std::vector<uint8_t> out(size_t(width) * height * 4, 0);
        if (f.IsBlockCompressed())
        {
            const uint32_t blocksW = (width + 3) / 4, blocksH = (height + 3) / 4;
            const uint32_t bpb = f.BytesPerBlock();
            uint8_t block[64];
            for (uint32_t by = 0; by < blocksH; ++by)
            {
                for (uint32_t bx = 0; bx < blocksW; ++bx)
                {
                    const uint8_t* src = linear.data() + (size_t(by) * blocksW + bx) * bpb;
                    switch (f.format)
                    {
                    case kFormat_DXT1: case kFormat_DXT1_AS_16_16_16_16: DecodeDXT1Block(src, block); break;
                    case kFormat_DXT2_3: case kFormat_DXT2_3_AS_16_16_16_16: DecodeDXT3Block(src, block); break;
                    case kFormat_DXT4_5: case kFormat_DXT4_5_AS_16_16_16_16: DecodeDXT5Block(src, block); break;
                    default: throw std::runtime_error(std::string("XenosTexture: no decoder for ") + f.Name());
                    }
                    for (uint32_t py = 0; py < 4; ++py)
                    {
                        const uint32_t y = by * 4 + py;
                        if (y >= height) break;
                        for (uint32_t px = 0; px < 4; ++px)
                        {
                            const uint32_t x = bx * 4 + px;
                            if (x >= width) break;
                            std::memcpy(out.data() + (size_t(y) * width + x) * 4, block + (py * 4 + px) * 4, 4);
                        }
                    }
                }
            }
            return out;
        }
        if (f.format == kFormat_8_8_8_8)
        {

            for (size_t i = 0; i < size_t(width) * height; ++i)
            {
                const uint8_t* p = linear.data() + i * 4;
                out[i * 4 + 0] = p[2];
                out[i * 4 + 1] = p[1];
                out[i * 4 + 2] = p[0];
                out[i * 4 + 3] = p[3];
            }
            return out;
        }
        if (f.format == kFormat_8)
        {
            for (size_t i = 0; i < size_t(width) * height; ++i)
            {
                out[i * 4 + 0] = out[i * 4 + 1] = out[i * 4 + 2] = 255;
                out[i * 4 + 3] = linear[i];
            }
            return out;
        }
        throw std::runtime_error(std::string("XenosTexture: no decoder for ") + f.Name());
    }

    inline std::vector<uint8_t> EncodeRGBA8To8888(const uint8_t* rgba, uint32_t width, uint32_t height)
    {
        std::vector<uint8_t> out(size_t(width) * height * 4);
        for (size_t i = 0; i < size_t(width) * height; ++i)
        {
            out[i * 4 + 0] = rgba[i * 4 + 2];
            out[i * 4 + 1] = rgba[i * 4 + 1];
            out[i * 4 + 2] = rgba[i * 4 + 0];
            out[i * 4 + 3] = rgba[i * 4 + 3];
        }
        return out;
    }
}
