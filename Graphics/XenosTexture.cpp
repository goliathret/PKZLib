#include "XenosTexture.h"

#include <cstring>
#include <stdexcept>
#include <string>

#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/xenos.h>
#include <squish/squish.h>

namespace XenosTexture
{
    namespace
    {
        namespace xenos = rex::graphics::xenos;
        namespace texture_util = rex::graphics::texture_util;
        using rex::graphics::FormatInfo;

        const FormatInfo& Info(uint32_t format) { return *FormatInfo::Get(format & 0x3F); }

        void SwapEndian(uint8_t* p, size_t n, uint32_t endian)
        {
            const xenos::Endian e = static_cast<xenos::Endian>(endian);
            if (e == xenos::Endian::kNone)
                return;
            size_t i = 0;
            for (; i + 4 <= n; i += 4)
            {
                uint32_t v;
                std::memcpy(&v, p + i, 4);
                v = xenos::GpuSwap(v, e);
                std::memcpy(p + i, &v, 4);
            }
            for (; i + 2 <= n; i += 2)
            {
                uint16_t v;
                std::memcpy(&v, p + i, 2);
                v = xenos::GpuSwap(v, e);
                std::memcpy(p + i, &v, 2);
            }
        }

        size_t LinearSize(uint32_t width, uint32_t height, const D3DFormat& f)
        {
            const uint32_t bd = f.BlockDim();
            return size_t((width + bd - 1) / bd) * ((height + bd - 1) / bd) * f.BytesPerBlock();
        }

        // Calls visit(linear offset, surface offset) for each block.
        template <typename Visit>
        void ForEachBlock(uint32_t width, uint32_t height, const D3DFormat& f, Visit visit)
        {
            const uint32_t bd = f.BlockDim();
            const uint32_t bpb = f.BytesPerBlock();
            if (!bpb)
                throw std::runtime_error(std::string("XenosTexture: unsupported format ") + f.Name());
            const uint32_t blocksW = (width + bd - 1) / bd;
            const uint32_t blocksH = (height + bd - 1) / bd;
            const uint32_t pitch = rex::align(blocksW, xenos::kTextureTileWidthHeight);
            const uint32_t bpbLog2 = Log2(bpb);
            for (uint32_t y = 0; y < blocksH; ++y)
                for (uint32_t x = 0; x < blocksW; ++x)
                    visit((size_t(y) * blocksW + x) * bpb,
                          f.tiled ? TiledOffset2D(x, y, pitch, bpbLog2) : (size_t(y) * pitch + x) * bpb);
        }

        int SquishFlags(const D3DFormat& f)
        {
            switch (f.format)
            {
            case kFormat_DXT1: case kFormat_DXT1_AS_16_16_16_16: return squish::kDxt1;
            case kFormat_DXT2_3: case kFormat_DXT2_3_AS_16_16_16_16: return squish::kDxt3;
            case kFormat_DXT4_5: case kFormat_DXT4_5_AS_16_16_16_16: return squish::kDxt5;
            default: return 0;
            }
        }
    }

    bool D3DFormat::IsBlockCompressed() const
    {
        const FormatInfo& info = Info(format);
        return info.block_width > 1 || info.block_height > 1;
    }

    uint32_t D3DFormat::BlockDim() const { return Info(format).block_width; }

    uint32_t D3DFormat::BytesPerBlock() const { return Info(format).bytes_per_block(); }

    const char* D3DFormat::Name() const
    {
        const char* name = Info(format).name;
        return std::strncmp(name, "k_", 2) == 0 ? name + 2 : name;
    }

    uint32_t TiledOffset2D(uint32_t x, uint32_t y, uint32_t pitchBlocks, uint32_t bpbLog2)
    {
        return static_cast<uint32_t>(texture_util::GetTiledOffset2D(int32_t(x), int32_t(y), pitchBlocks, bpbLog2));
    }

    uint32_t TiledSurfaceSize(uint32_t width, uint32_t height, const D3DFormat& f)
    {
        const uint32_t bd = f.BlockDim();
        return rex::align((width + bd - 1) / bd, xenos::kTextureTileWidthHeight) *
               rex::align((height + bd - 1) / bd, xenos::kTextureTileWidthHeight) * f.BytesPerBlock();
    }

    uint32_t PackedMipLevel(uint32_t width, uint32_t height) { return texture_util::GetPackedMipLevel(width, height); }

    void PackedMipOffset(uint32_t width, uint32_t height, uint32_t mip, const D3DFormat& f, uint32_t& xBlocks,
                         uint32_t& yBlocks)
    {
        uint32_t zBlocks = 0;
        texture_util::GetPackedMipOffset(width, height, 1, static_cast<xenos::TextureFormat>(f.format), mip, xBlocks,
                                         yBlocks, zBlocks);
    }

    std::vector<uint8_t> Untile(const uint8_t* data, size_t size, uint32_t width, uint32_t height, const D3DFormat& f)
    {
        const uint32_t bpb = f.BytesPerBlock();
        std::vector<uint8_t> out(LinearSize(width, height, f));
        ForEachBlock(width, height, f, [&](size_t block, size_t surface) {
            if (surface + bpb > size)
                throw std::runtime_error("XenosTexture: tiled data shorter than the surface");
            std::memcpy(out.data() + block, data + surface, bpb);
            SwapEndian(out.data() + block, bpb, f.endian);
        });
        return out;
    }

    std::vector<uint8_t> Tile(const uint8_t* linear, uint32_t width, uint32_t height, const D3DFormat& f)
    {
        const uint32_t bpb = f.BytesPerBlock();
        std::vector<uint8_t> out(TiledSurfaceSize(width, height, f), 0);
        ForEachBlock(width, height, f, [&](size_t block, size_t surface) {
            std::memcpy(out.data() + surface, linear + block, bpb);
            SwapEndian(out.data() + surface, bpb, f.endian);
        });
        return out;
    }

    namespace
    {
        template<typename Visit>
        void VolumeBlocks(uint32_t width, uint32_t height, uint32_t depth, const D3DFormat& format, Visit visit)
        {
            if (!width || !height || !depth || format.BlockDim() != 1 || format.BytesPerBlock() != 4)
                throw std::runtime_error("XenosTexture: volume decoding currently requires 32-bit pixels");
            const auto pitch = AlignUp(width, 32), rows = AlignUp(height, 32);
            for (uint32_t z = 0; z < depth; ++z)
                for (uint32_t y = 0; y < height; ++y)
                    for (uint32_t x = 0; x < width; ++x)
                    {
                        const size_t surface = format.tiled ?
                            size_t(texture_util::GetTiledOffset3D(x, y, z, pitch, rows, 2)) :
                            ((size_t(z) * rows + y) * pitch + x) * 4;
                        visit(((size_t(z) * height + y) * width + x) * 4, surface);
                    }
        }
    }

    std::vector<uint8_t> UntileVolume(const uint8_t* data, size_t size, uint32_t width, uint32_t height,
                                       uint32_t depth, const D3DFormat& format)
    {
        if (!width || !height || !depth || width > 16384 || height > 16384 || depth > 2048 ||
            size_t(width) * height * depth * 4 > size)
            throw std::runtime_error("XenosTexture: invalid or truncated volume");
        std::vector<uint8_t> linear(size_t(width) * height * depth * 4);
        VolumeBlocks(width, height, depth, format, [&](size_t destination, size_t source) {
            if (source > size || size - source < 4)
                throw std::runtime_error("XenosTexture: volume data is truncated");
            std::memcpy(linear.data() + destination, data + source, 4);
            SwapEndian(linear.data() + destination, 4, format.endian);
        });
        return linear;
    }

    std::vector<uint8_t> TileVolume(const std::vector<uint8_t>& linear, uint32_t width, uint32_t height,
                                     uint32_t depth, const D3DFormat& format)
    {
        if (linear.size() != size_t(width) * height * depth * 4)
            throw std::runtime_error("XenosTexture: wrong volume pixel count");
        std::vector<uint8_t> tiled(size_t(AlignUp(width, 32)) * AlignUp(height, 32) * AlignUp(depth, 4) * 4);
        VolumeBlocks(width, height, depth, format, [&](size_t source, size_t destination) {
            if (destination > tiled.size() || tiled.size() - destination < 4)
                throw std::runtime_error("XenosTexture: volume layout exceeds its allocation");
            std::memcpy(tiled.data() + destination, linear.data() + source, 4);
            SwapEndian(tiled.data() + destination, 4, format.endian);
        });
        return tiled;
    }

    std::vector<uint8_t> DecodeToRGBA8(const std::vector<uint8_t>& linear, uint32_t width, uint32_t height,
                                       const D3DFormat& f)
    {
        if (linear.size() < LinearSize(width, height, f))
            throw std::runtime_error("XenosTexture: pixel data is truncated");
        std::vector<uint8_t> out(size_t(width) * height * 4, 0);
        if (const int flags = SquishFlags(f))
        {
            if (linear.size() < size_t(squish::GetStorageRequirements(int(width), int(height), flags)))
                throw std::runtime_error("XenosTexture: block data shorter than the image");
            squish::DecompressImage(out.data(), int(width), int(height), linear.data(), flags);
            return out;
        }
        if (f.format == kFormat_8_8_8_8)
        {
            for (size_t i = 0; i < size_t(width) * height; ++i)
            {
                const uint8_t* p = linear.data() + i * 4;
                const uint8_t components[] = {p[0], p[1], p[2], p[3], 0, 255};
                for (int channel = 0; channel < 4; ++channel)
                {
                    if (f.swizzle[channel] > 5)
                        throw std::runtime_error("XenosTexture: invalid color swizzle");
                    out[i * 4 + channel] = components[f.swizzle[channel]];
                }
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

    std::vector<uint8_t> EncodeRGBA8(const uint8_t* rgba, uint32_t width, uint32_t height, const D3DFormat& f)
    {
        if (const int flags = SquishFlags(f))
        {
            std::vector<uint8_t> out(size_t(squish::GetStorageRequirements(int(width), int(height), flags)));
            squish::CompressImage(rgba, int(width), int(height), out.data(), flags | squish::kColourClusterFit);
            return out;
        }
        if (f.format == kFormat_8_8_8_8)
        {
            std::vector<uint8_t> out(size_t(width) * height * 4, 255);
            for (size_t i = 0; i < size_t(width) * height; ++i)
                for (int channel = 0; channel < 4; ++channel)
                    if (f.swizzle[channel] < 4)
                        out[i * 4 + f.swizzle[channel]] = rgba[i * 4 + channel];
            return out;
        }
        if (f.format == kFormat_8)
        {
            std::vector<uint8_t> out(size_t(width) * height);
            for (size_t i = 0; i < out.size(); ++i)
                out[i] = rgba[i * 4 + 3];
            return out;
        }
        throw std::runtime_error(std::string("XenosTexture: no encoder for ") + f.Name());
    }

    std::vector<uint8_t> EncodeRGBA8To8888(const uint8_t* rgba, uint32_t width, uint32_t height)
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
