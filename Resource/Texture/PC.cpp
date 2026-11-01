#include "../RZTexture.h"

namespace
{

    bool SameFormat(const BUDds::Image& a, const BUDds::Image& b)
    {
        return a.fourCC == b.fourCC && a.luminance == b.luminance &&
               (a.fourCC ||
                (a.bitCount == b.bitCount && a.redMask == b.redMask && a.greenMask == b.greenMask &&
                 a.blueMask == b.blueMask && a.alphaMask == b.alphaMask));
    }
} // namespace

RZTexture::Descriptor RZTexture::Descriptor::ParsePC(const CMChunk& textureData)
{
    if (!textureData.isLittleEndian || textureData.data.size() < kPCHeaderSize)
        throw std::runtime_error("RZTexture: truncated PC DDS wrapper");
    if (textureData.version != 5 && textureData.version != 10)
        throw std::runtime_error("RZTexture: unsupported PC texture wrapper version");
    const auto image =
        BUDds::Decode(textureData.data.data() + kPCHeaderSize, textureData.data.size() - kPCHeaderSize);
    Descriptor descriptor;
    descriptor.storage = Storage::PC;
    descriptor.kind = image.cubemap ? kCube : image.volume ? kVolume : k2D;
    descriptor.width = image.width;
    descriptor.height = image.height;
    descriptor.depth = image.volume ? image.depth : 0;
    descriptor.mipLevels = uint32_t(image.Mips());
    descriptor.d3dFormat = image.fourCC;
    size_t offset = 4;
    for (auto& scale : descriptor.lightmapScale)
        scale = textureData.Read<float>(offset);
    descriptor.normalScale = textureData.Read<float>(offset);
    descriptor.flags = textureData.Read<uint32_t>(offset) ? kFlagSRGB : 0;
    const uint32_t kind = textureData.Read<uint32_t>(offset);
    if (kind != descriptor.kind)
        throw std::runtime_error("RZTexture: PC wrapper kind does not match its DDS");
    return descriptor;
}

std::vector<uint8_t> RZTexture::DecodePC() const
{
    const auto image = BUDds::Decode(gpuData);
    std::vector<uint8_t> rgba;
    for (size_t face = 0; face < image.Faces(); ++face)
    {
        auto pixels = BUDds::DecodeRGBA8(image, face * image.Mips());
        rgba.insert(rgba.end(), pixels.begin(), pixels.end());
    }
    if (!image.cubemap && !image.volume)
        return rgba;
    return ReorderSlices(rgba, desc.width, desc.height, image.cubemap ? 6 : image.depth,
                         SliceLayout::HorizontalStrip);
}

void RZTexture::ReplacePC(const BUDds::Image& source)
{
    const auto target = BUDds::Decode(gpuData);
    if (source.width != target.width || source.height != target.height || source.depth != target.depth ||
        source.volume != target.volume || source.cubemap != target.cubemap || source.levels.empty() ||
        source.levels.size() % source.Faces())
        throw std::runtime_error("RZTexture: replacement must keep the PC texture dimensions and faces");
    if (source.volume && source.Mips() != target.Mips())
        throw std::runtime_error("RZTexture: volume replacement must keep its mip count");
    auto bytes = gpuData;
    for (size_t face = 0; face < target.Faces(); ++face)
    {
        std::vector<uint8_t> rgba;
        if (source.Mips() != target.Mips())
            rgba = BUDds::DecodeRGBA8(source, face * source.Mips());
        for (size_t mip = 0; mip < target.Mips(); ++mip)
        {
            const auto& destination = target.levels[face * target.Mips() + mip];
            std::vector<uint8_t> encoded;
            if (source.Mips() == target.Mips())
            {
                const size_t index = face * source.Mips() + mip;
                const auto& input = source.levels[index];
                if (input.width != destination.width || input.height != destination.height ||
                    input.depth != destination.depth)
                    throw std::runtime_error("RZTexture: DDS mip dimensions differ");
                if (SameFormat(source, target))
                    encoded = input.data;
                else
                {
                    const auto pixels = BUDds::DecodeRGBA8(source, index);
                    encoded = BUDds::EncodeRGBA8(target, pixels.data(), destination.width, destination.height,
                                                 destination.depth);
                }
            }
            else
            {
                encoded = BUDds::EncodeRGBA8(target, rgba.data(), destination.width, destination.height);
                if (mip + 1 < target.Mips())
                    rgba = HalveRGBA8(rgba.data(), destination.width, destination.height);
            }
            if (encoded.size() != destination.data.size())
                throw std::runtime_error("RZTexture: replacement DDS mip byte count differs");
            const size_t rows =
                (target.fourCC ? (destination.height + 3) / 4 : destination.height) * destination.depth;
            const size_t rowBytes = destination.data.size() / rows;
            for (size_t row = 0; row < rows; ++row)
                std::copy_n(encoded.data() + row * rowBytes, rowBytes,
                            bytes.data() + destination.offset + row * destination.rowPitch);
        }
    }
    gpuData = std::move(bytes);
}

void RZTexture::ReencodePC(const uint8_t* rgba, uint32_t width, uint32_t height)
{
    if (width != PreviewWidth() || height != desc.height)
        throw std::runtime_error("RZTexture: PC PNG replacement must keep the preview dimensions");
    BUDds::Image source;
    source.width = desc.width;
    source.height = desc.height;
    source.volume = desc.IsVolume();
    source.depth = source.volume ? desc.depth : 1;
    source.cubemap = desc.IsCube();
    source.redMask = 0xFF;
    source.blueMask = 0xFF0000;
    std::vector<uint8_t> strip(rgba, rgba + size_t(width) * height * 4);
    const uint32_t slices = source.cubemap ? 6 : source.depth;
    const auto pixels = ReorderSlices(strip, desc.width, desc.height, slices, SliceLayout::Slices);
    const size_t faceBytes = size_t(desc.width) * desc.height * source.depth * 4;
    for (size_t face = 0; face < source.Faces(); ++face)
        source.levels.push_back(
            {desc.width, desc.height,
             std::vector<uint8_t>(pixels.begin() + face * faceBytes, pixels.begin() + (face + 1) * faceBytes),
             source.depth});
    ReplacePC(source);
}
