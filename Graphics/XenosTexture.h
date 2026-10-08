#pragma once

#include <cstdint>
#include <vector>

// Xbox 360 texture layout and codecs, on ReXGlue and libsquish (XenosTexture.cpp).
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
        static constexpr uint32_t kDXT5TiledNormalMap = 0x1A215554;
        static constexpr uint32_t kA8R8G8B8Tiled = 0x18287F86;

        bool IsBlockCompressed() const;
        uint32_t BlockDim() const;
        uint32_t BytesPerBlock() const;
        const char* Name() const;
    };

    inline uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }
    inline uint32_t Log2(uint32_t v) { uint32_t r = 0; while ((1u << r) < v) ++r; return r; }

    uint32_t TiledOffset2D(uint32_t x, uint32_t y, uint32_t pitchBlocks, uint32_t bpbLog2);
    uint32_t TiledSurfaceSize(uint32_t width, uint32_t height, const D3DFormat& f);

    uint32_t PackedMipLevel(uint32_t width, uint32_t height);
    void PackedMipOffset(uint32_t width, uint32_t height, uint32_t mip, const D3DFormat& f, uint32_t& xBlocks,
                         uint32_t& yBlocks);

    std::vector<uint8_t> Untile(const uint8_t* data, size_t size, uint32_t width, uint32_t height, const D3DFormat& f);
    std::vector<uint8_t> Tile(const uint8_t* linear, uint32_t width, uint32_t height, const D3DFormat& f);

    std::vector<uint8_t> DecodeToRGBA8(const std::vector<uint8_t>& linear, uint32_t width, uint32_t height,
                                       const D3DFormat& f);
    std::vector<uint8_t> EncodeRGBA8(const uint8_t* rgba, uint32_t width, uint32_t height, const D3DFormat& f);
    std::vector<uint8_t> EncodeRGBA8To8888(const uint8_t* rgba, uint32_t width, uint32_t height);
}
