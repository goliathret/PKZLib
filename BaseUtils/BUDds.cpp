#include "BUDds.h"
#include "../Graphics/XenosTexture.h"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace BUDds
{
    namespace
    {
        constexpr size_t kHeaderSize = 128;
        constexpr size_t kDx10HeaderSize = 20;
        constexpr uint32_t kMagic = FourCC('D', 'D', 'S', ' ');

        enum HeaderFlags : uint32_t
        {
            kRequiredFields = 0x1007,
            kPitch = 0x8,
            kMipCount = 0x20000,
            kLinearSize = 0x80000,
            kDepth = 0x800000,
        };

        enum PixelFlags : uint32_t
        {
            kAlphaPixels = 0x1,
            kAlphaOnly = 0x2,
            kFourCC = 0x4,
            kRgb = 0x40,
        };

        enum Caps : uint32_t
        {
            kComplex = 0x8,
            kTexture = 0x1000,
            kMipMap = 0x400000,
        };

        constexpr uint32_t kCubemap = 0x200;
        constexpr uint32_t kVolume = 0x200000;
        constexpr uint32_t kDx10Cubemap = 0x4;
        constexpr uint32_t kTexture3D = 4;

        void WriteU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
                bytes.at(offset + i) = uint8_t(value >> (i * 8));
        }

        uint32_t MaxMipCount(const Image& image)
        {
            uint32_t count = 1;
            for (auto side = std::max({image.width, image.height, image.depth}); side > 1; side >>= 1)
                ++count;
            return count;
        }

        size_t RowBytes(const Image& image, uint32_t width)
        {
            if (image.fourCC)
                return size_t((width + 3) / 4) * BlockBytes(image.fourCC);
            return size_t(width) * (image.bitCount / 8);
        }

        size_t RowCount(const Image& image, uint32_t height)
        {
            return image.fourCC ? (height + 3) / 4 : height;
        }

        size_t AlignRow(size_t bytes)
        {
            return (bytes + 3) & ~size_t(3);
        }

        void ValidateMasks(const Image& image)
        {
            const uint32_t masks[] = {image.redMask, image.greenMask, image.blueMask, image.alphaMask};
            uint32_t used = 0;
            for (auto mask : masks)
            {
                if ((used & mask) || (image.bitCount < 32 && (mask >> image.bitCount)))
                    throw std::runtime_error("BUDds: overlapping or out-of-range color masks");
                used |= mask;
                if (!mask)
                    continue;

                while (!(mask & 1))
                    mask >>= 1;
                if (mask & (mask + 1))
                    throw std::runtime_error("BUDds: non-contiguous color mask");
            }
        }

        void ValidateImage(const Image& image)
        {
            if (!image.width || !image.height || !image.depth || image.width > 16384 ||
                image.height > 16384 || image.depth > 2048)
                throw std::runtime_error("BUDds: invalid image dimensions");
            if (!image.volume && image.depth != 1)
                throw std::runtime_error("BUDds: a 2D texture cannot have multiple depth slices");

            if (image.fourCC)
            {
                if (!BlockBytes(image.fourCC))
                    throw std::runtime_error("BUDds: unsupported compression format");
                return;
            }
            if (image.bitCount != 8 && image.bitCount != 16 && image.bitCount != 24 && image.bitCount != 32)
                throw std::runtime_error("BUDds: unsupported pixel size");
            ValidateMasks(image);
        }

        void SetDxgiFormat(Image& image, uint32_t format)
        {
            image.fourCC = FourCCFromDxgi(format);
            image.bitCount = 32;
            image.redMask = 0x00FF0000;
            image.greenMask = 0x0000FF00;
            image.blueMask = 0x000000FF;
            image.alphaMask = 0xFF000000;
            if (image.fourCC)
                return;

            switch (format)
            {
            case 27: // R8G8B8A8: typeless, UNORM, sRGB
            case 28:
            case 29:
                std::swap(image.redMask, image.blueMask);
                break;
            case 87: // B8G8R8A8
            case 90:
            case 91:
                break;
            case 88: // B8G8R8X8
            case 92:
            case 93:
                image.alphaMask = 0;
                break;
            case 65: // A8
                image.bitCount = 8;
                image.redMask = image.greenMask = image.blueMask = 0;
                image.alphaMask = 255;
                break;
            default:
                throw std::runtime_error("BUDds: unsupported DXGI format " + std::to_string(format));
            }
        }

        size_t ReadHeader(const uint8_t* bytes, size_t size, Image& image)
        {
            if (size < kHeaderSize || ReadU32LE(bytes) != kMagic || ReadU32LE(bytes + 4) != 124 ||
                ReadU32LE(bytes + 76) != 32)
                throw std::runtime_error("BUDds: invalid or truncated DDS header");

            const uint32_t caps = ReadU32LE(bytes + 112);
            if (caps & kCubemap)
                throw std::runtime_error("BUDds: cubemaps are not supported");
            image.width = ReadU32LE(bytes + 16);
            image.height = ReadU32LE(bytes + 12);
            image.volume = (caps & kVolume) != 0;
            image.depth = image.volume ? ReadU32LE(bytes + 24) : 1;

            const uint32_t pixelFlags = ReadU32LE(bytes + 80);
            image.fourCC = (pixelFlags & kFourCC) ? ReadU32LE(bytes + 84) : 0;
            image.bitCount = ReadU32LE(bytes + 88);
            image.redMask = ReadU32LE(bytes + 92);
            image.greenMask = ReadU32LE(bytes + 96);
            image.blueMask = ReadU32LE(bytes + 100);
            image.alphaMask = ReadU32LE(bytes + 104);

            if (image.fourCC == kDX10)
            {
                if (size < kHeaderSize + kDx10HeaderSize)
                    throw std::runtime_error("BUDds: truncated DX10 header");
                const uint8_t* dx10 = bytes + kHeaderSize;
                const uint32_t dimension = ReadU32LE(dx10 + 4);
                const uint32_t miscFlags = ReadU32LE(dx10 + 8);
                const uint32_t arraySize = ReadU32LE(dx10 + 12);
                if (arraySize != 1 || (miscFlags & kDx10Cubemap) || dimension < 2 || dimension > kTexture3D)
                    throw std::runtime_error("BUDds: arrays and cubemaps are not supported");

                image.volume = dimension == kTexture3D;
                image.depth = image.volume ? ReadU32LE(bytes + 24) : 1;
                SetDxgiFormat(image, ReadU32LE(dx10));
                return kHeaderSize + kDx10HeaderSize;
            }

            if (!image.fourCC)
            {
                if (!(pixelFlags & (kRgb | kAlphaOnly)))
                    throw std::runtime_error("BUDds: DDS pixels must be RGB or alpha");
                if (!(pixelFlags & (kAlphaPixels | kAlphaOnly)))
                    image.alphaMask = 0;
            }
            return kHeaderSize;
        }

        void ReadMipChain(Image& image, const uint8_t* bytes, size_t size, size_t offset)
        {
            const uint32_t count = std::max(1u, ReadU32LE(bytes + 28));
            if (count > MaxMipCount(image))
                throw std::runtime_error("BUDds: invalid mip count");

            const size_t topRow = RowBytes(image, image.width);
            const bool hasPitch = !image.fourCC && (ReadU32LE(bytes + 8) & kPitch);
            const size_t topPitch = hasPitch ? ReadU32LE(bytes + 20) : topRow;
            if (topPitch < topRow)
                throw std::runtime_error("BUDds: row pitch is shorter than the image");
            const bool paddedRows = topPitch > topRow && topPitch == AlignRow(topRow);

            uint32_t width = image.width, height = image.height, depth = image.depth;
            for (uint32_t mip = 0; mip < count; ++mip)
            {
                const size_t rowBytes = RowBytes(image, width);
                const size_t rows = RowCount(image, height) * depth;
                size_t pitch = paddedRows ? AlignRow(rowBytes) : rowBytes;
                if (mip == 0)
                    pitch = topPitch;
                const size_t needed = pitch * rows;
                if (offset > size || needed > size - offset)
                    throw std::runtime_error("BUDds: file ends inside mip " + std::to_string(mip));

                Level level{width, height, std::vector<uint8_t>(rowBytes * rows), depth};
                for (size_t row = 0; row < rows; ++row)
                    std::copy_n(bytes + offset + row * pitch, rowBytes, level.data.data() + row * rowBytes);
                image.levels.push_back(std::move(level));
                offset += needed;
                width = std::max(1u, width / 2);
                height = std::max(1u, height / 2);
                depth = std::max(1u, depth / 2);
            }
        }

        std::vector<uint8_t> WriteHeader(const Image& image)
        {
            uint32_t flags = kRequiredFields;
            uint32_t caps = kTexture;
            uint32_t pixelFlags = kFourCC;
            size_t pitch = RowBytes(image, image.width);
            if (image.fourCC)
            {
                flags |= kLinearSize;
                pitch *= RowCount(image, image.height) * image.depth;
            }
            else
            {
                flags |= kPitch;
                const bool hasRgb = image.redMask || image.greenMask || image.blueMask;
                pixelFlags = hasRgb ? kRgb : kAlphaOnly;
                if (hasRgb && image.alphaMask)
                    pixelFlags |= kAlphaPixels;
            }
            if (image.volume)
            {
                flags |= kDepth;
                caps |= kComplex;
            }
            if (image.levels.size() > 1)
            {
                flags |= kMipCount;
                caps |= kComplex | kMipMap;
            }

            std::vector<uint8_t> file(kHeaderSize, 0);
            WriteU32(file, 0, kMagic);
            WriteU32(file, 4, 124);
            WriteU32(file, 8, flags);
            WriteU32(file, 12, image.height);
            WriteU32(file, 16, image.width);
            WriteU32(file, 20, uint32_t(pitch));
            WriteU32(file, 24, image.volume ? image.depth : 0);
            WriteU32(file, 28, uint32_t(image.levels.size()));
            WriteU32(file, 76, 32);
            WriteU32(file, 80, pixelFlags);
            WriteU32(file, 84, image.fourCC);
            WriteU32(file, 88, image.fourCC ? 0 : image.bitCount);
            if (!image.fourCC)
            {
                WriteU32(file, 92, image.redMask);
                WriteU32(file, 96, image.greenMask);
                WriteU32(file, 100, image.blueMask);
                WriteU32(file, 104, image.alphaMask);
            }
            WriteU32(file, 108, caps);
            WriteU32(file, 112, image.volume ? kVolume : 0);
            return file;
        }

        std::vector<uint8_t> DecodeBlocks(const Image& image, const Level& level)
        {
            uint32_t format = XenosTexture::kFormat_DXT4_5;
            if (image.fourCC == kDXT1)
                format = XenosTexture::kFormat_DXT1;
            else if (image.fourCC == kDXT3)
                format = XenosTexture::kFormat_DXT2_3;

            const size_t sliceSize = LevelSize(level.width, level.height, BlockBytes(image.fourCC));
            if (level.data.size() != sliceSize * level.depth)
                throw std::runtime_error("BUDds: wrong compressed pixel count");

            std::vector<uint8_t> rgba;
            rgba.reserve(size_t(level.width) * level.height * level.depth * 4);
            for (uint32_t z = 0; z < level.depth; ++z)
            {
                const auto start = level.data.begin() + z * sliceSize;
                const std::vector<uint8_t> slice(start, start + sliceSize);
                const auto decoded = XenosTexture::DecodeToRGBA8(slice, level.width, level.height,
                                                                 XenosTexture::D3DFormat::Decode(format));
                rgba.insert(rgba.end(), decoded.begin(), decoded.end());
            }
            return rgba;
        }

        uint8_t ReadComponent(uint32_t pixel, uint32_t mask, uint8_t fallback)
        {
            if (!mask)
                return fallback;
            while (!(mask & 1))
            {
                mask >>= 1;
                pixel >>= 1;
            }
            return uint8_t((uint64_t(pixel & mask) * 255 + mask / 2) / mask);
        }

        std::vector<uint8_t> DecodePixels(const Image& image, const Level& level)
        {
            const size_t pixels = size_t(level.width) * level.height * level.depth;
            const size_t bytesPerPixel = image.bitCount / 8;
            if (level.data.size() != pixels * bytesPerPixel)
                throw std::runtime_error("BUDds: wrong pixel count");

            std::vector<uint8_t> rgba(pixels * 4);
            const bool alphaOnly = !image.redMask && !image.greenMask && !image.blueMask;
            for (size_t i = 0; i < pixels; ++i)
            {
                uint32_t pixel = 0;
                for (size_t byte = 0; byte < bytesPerPixel; ++byte)
                    pixel |= uint32_t(level.data[i * bytesPerPixel + byte]) << (byte * 8);
                rgba[i * 4] = ReadComponent(pixel, image.redMask, alphaOnly ? 255 : 0);
                rgba[i * 4 + 1] = ReadComponent(pixel, image.greenMask, alphaOnly ? 255 : 0);
                rgba[i * 4 + 2] = ReadComponent(pixel, image.blueMask, alphaOnly ? 255 : 0);
                rgba[i * 4 + 3] = ReadComponent(pixel, image.alphaMask, 255);
            }
            return rgba;
        }
    } // namespace

    uint32_t FourCCFromDxgi(uint32_t dxgi)
    {
        switch (dxgi)
        {
        case 70: // BC1: typeless, UNORM, sRGB
        case 71:
        case 72:
            return kDXT1;
        case 73: // BC2
        case 74:
        case 75:
            return kDXT3;
        case 76: // BC3
        case 77:
        case 78:
            return kDXT5;
        default:
            return 0;
        }
    }

    uint32_t BlockBytes(uint32_t fourCC)
    {
        switch (fourCC)
        {
        case kDXT1:
            return 8;
        case kDXT3:
        case kDXT5:
            return 16;
        default:
            return 0;
        }
    }

    Image Decode(const uint8_t* bytes, size_t size)
    {
        Image image;
        const size_t dataOffset = ReadHeader(bytes, size, image);
        ValidateImage(image);
        image.blockBytes = BlockBytes(image.fourCC);
        ReadMipChain(image, bytes, size, dataOffset);
        return image;
    }

    std::vector<uint8_t> Encode(const Image& image)
    {
        ValidateImage(image);
        if (image.levels.empty() || image.levels.size() > MaxMipCount(image))
            throw std::runtime_error("BUDds: invalid mip chain");

        auto file = WriteHeader(image);
        uint32_t width = image.width, height = image.height, depth = image.depth;
        for (const auto& level : image.levels)
        {
            const size_t needed = RowBytes(image, width) * RowCount(image, height) * depth;
            if (level.width != width || level.height != height || level.depth != depth ||
                level.data.size() != needed)
                throw std::runtime_error("BUDds: mip dimensions or pixel count differ");
            file.insert(file.end(), level.data.begin(), level.data.end());
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
            depth = std::max(1u, depth / 2);
        }
        return file;
    }

    std::vector<uint8_t> DecodeRGBA8(const Image& image, size_t mip)
    {
        ValidateImage(image);
        const auto& level = image.levels.at(mip);
        if (image.fourCC)
            return DecodeBlocks(image, level);
        return DecodePixels(image, level);
    }
} // namespace BUDds
