#include "RZTexture.h"

namespace
{
    uint32_t NativeFourCC(const XenosTexture::D3DFormat& format)
    {
        switch (format.format)
        {
        case XenosTexture::kFormat_DXT1:
        case XenosTexture::kFormat_DXT1_AS_16_16_16_16:
            return BUDds::kDXT1;
        case XenosTexture::kFormat_DXT2_3:
        case XenosTexture::kFormat_DXT2_3_AS_16_16_16_16:
            return BUDds::kDXT3;
        case XenosTexture::kFormat_DXT4_5:
        case XenosTexture::kFormat_DXT4_5_AS_16_16_16_16:
            return BUDds::kDXT5;
        default:
            return 0;
        }
    }

    enum class VolumeLayout
    {
        Slices,
        HorizontalStrip,
    };

    std::vector<uint8_t> ReorderVolume(const std::vector<uint8_t>& source, uint32_t width, uint32_t height,
                                       uint32_t depth, VolumeLayout output)
    {
        std::vector<uint8_t> result(source.size());
        const size_t rowBytes = size_t(width) * 4;
        for (uint32_t z = 0; z < depth; ++z)
        {
            for (uint32_t y = 0; y < height; ++y)
            {
                const size_t sliceOffset = (size_t(z) * height + y) * rowBytes;
                const size_t stripOffset = (size_t(y) * depth + z) * rowBytes;
                if (output == VolumeLayout::HorizontalStrip)
                    std::copy_n(source.data() + sliceOffset, rowBytes, result.data() + stripOffset);
                else
                    std::copy_n(source.data() + stripOffset, rowBytes, result.data() + sliceOffset);
            }
        }
        return result;
    }

    BUDds::Level EncodeDdsLevel(const BUDds::Image& image, size_t mip, const XenosTexture::D3DFormat& format)
    {
        const auto& level = image.levels[mip];
        const auto rgba = BUDds::DecodeRGBA8(image, mip);
        auto pixels = XenosTexture::EncodeRGBA8(rgba.data(), level.width, level.height * level.depth, format);
        return {level.width, level.height, std::move(pixels), level.depth};
    }
} // namespace

RZTexture::Descriptor RZTexture::Descriptor::Parse(const CMChunk& textureData)
{
    if (textureData.GetMaskedID() != Texture_Data || textureData.data.size() < 8)
        throw std::runtime_error("RZTexture: not a Texture_Data chunk");
    size_t offset = 0;
    Descriptor descriptor;
    descriptor.kind = textureData.Read<uint32_t>(offset);
    descriptor.flags = textureData.Read<uint32_t>(offset);
    if (descriptor.flags & kFlagLightmapScale)
        for (float& scale : descriptor.lightmapScale)
            scale = textureData.Read<float>(offset);
    if (descriptor.flags & kFlagNormalScale)
        descriptor.normalScale = textureData.Read<float>(offset);
    if (descriptor.kind > kVolume || textureData.data.size() < descriptor.Size())
        throw std::runtime_error("RZTexture: invalid texture kind or truncated descriptor");
    descriptor.width = textureData.Read<uint32_t>(offset);
    descriptor.height = descriptor.IsCube() ? descriptor.width : textureData.Read<uint32_t>(offset);
    if (descriptor.IsVolume())
        descriptor.depth = textureData.Read<uint32_t>(offset);
    descriptor.mipLevels = textureData.Read<uint32_t>(offset);
    descriptor.usage = textureData.Read<uint32_t>(offset);
    descriptor.d3dFormat = textureData.Read<uint32_t>(offset);
    descriptor.expBias = textureData.Read<uint32_t>(offset);
    descriptor.xgFlags = textureData.Read<uint32_t>(offset);
    descriptor.baseOffset = textureData.Read<uint32_t>(offset);
    descriptor.mipOffset = textureData.Read<uint32_t>(offset);
    if (descriptor.Is2D())
        descriptor.pitch = textureData.Read<uint32_t>(offset);
    if (!descriptor.IsVolume())
    {
        descriptor.lowDataOffset = textureData.Read<uint32_t>(offset);
        descriptor.lowLevels = textureData.Read<uint32_t>(offset);
        descriptor.lowWidth = textureData.Read<uint16_t>(offset);
        descriptor.lowHeight = textureData.Read<uint16_t>(offset);
    }
    uint32_t maxMips = 1;
    for (auto size = std::max({descriptor.width, descriptor.height, descriptor.depth}); size > 1; size >>= 1)
        ++maxMips;
    if (!descriptor.width || !descriptor.height || descriptor.width > 16384 || descriptor.height > 16384 ||
        descriptor.depth > 2048 || (descriptor.IsVolume() && !descriptor.depth) ||
        descriptor.mipLevels > maxMips)
        throw std::runtime_error("RZTexture: invalid texture dimensions or mip count");
    return descriptor;
}

void RZTexture::Descriptor::Write(CMChunk& textureData) const
{
    textureData.Append<uint32_t>(kind);
    textureData.Append<uint32_t>(flags);
    if (flags & kFlagLightmapScale)
        for (float scale : lightmapScale)
            textureData.Append(scale);
    if (flags & kFlagNormalScale)
        textureData.Append(normalScale);
    textureData.Append(width);
    if (!IsCube())
        textureData.Append(height);
    if (IsVolume())
        textureData.Append(depth);
    textureData.Append(mipLevels);
    textureData.Append(usage);
    textureData.Append(d3dFormat);
    textureData.Append(expBias);
    textureData.Append(xgFlags);
    textureData.Append(baseOffset);
    textureData.Append(mipOffset);
    if (Is2D())
        textureData.Append(pitch);
    if (!IsVolume())
    {
        textureData.Append(lowDataOffset);
        textureData.Append(lowLevels);
        textureData.Append(lowWidth);
        textureData.Append(lowHeight);
    }
}

RZTexture RZTexture::FromDds(const std::string& name, const std::vector<uint8_t>& file, bool baseLevelOnly,
                             DdsColor color, float scale)
{
    auto image = BUDds::Decode(file);
    if (image.volume)
        throw std::runtime_error("RZTexture::FromDds: use an existing volume texture for volume replacement");
    if (color == DdsColor::NormalMap && image.fourCC != BUDds::kDXT5)
        throw std::runtime_error("RZTexture::FromDds: a normal map is DXT5 (" + name + ")");
    if (baseLevelOnly)
        image.levels.resize(1);

    if (!image.fourCC)
    {
        const auto rgba = BUDds::DecodeRGBA8(image);
        auto texture = FromRGBA8(name, rgba.data(), image.width, image.height);
        if (color == DdsColor::Linear)
            texture.desc.d3dFormat &= ~(0xFFu << 9);

        std::vector<BUDds::Level> levels;
        for (size_t mip = 0; mip < image.levels.size(); ++mip)
            levels.push_back(EncodeDdsLevel(image, mip, texture.desc.Format()));
        texture.SetLevels(levels);
        return texture;
    }

    uint32_t format = XenosTexture::kFormat_DXT4_5;
    if (image.fourCC == BUDds::kDXT1)
        format = XenosTexture::kFormat_DXT1;
    else if (image.fourCC == BUDds::kDXT3)
        format = XenosTexture::kFormat_DXT2_3;

    uint32_t baseFormat = XenosTexture::D3DFormat::kDXT5Tiled;
    if (color == DdsColor::NormalMap)
        baseFormat = XenosTexture::D3DFormat::kDXT5TiledNormalMap;
    else if (color == DdsColor::Linear)
        baseFormat = XenosTexture::D3DFormat::kDXT1TiledLinearColor;

    RZTexture texture;
    texture.name = name;
    texture.nameCRC = BUCRC().Generate(name);
    texture.desc.flags = color == DdsColor::NormalMap ? Descriptor::kNormalMap : Descriptor::kFlagSRGB;
    texture.desc.normalScale = scale;
    texture.desc.width = image.width;
    texture.desc.height = image.height;
    texture.desc.d3dFormat = (baseFormat & ~0x3Fu) | format;
    texture.SetLevels(image.levels);
    return texture;
}

std::vector<BUDds::Level> RZTexture::VolumeLevels() const
{
    if (std::max(1u, desc.mipLevels) != 1)
        throw std::runtime_error("RZTexture: volume mip chains are not supported");
    const XenosTexture::Layout layout = desc.Layout();
    return {{desc.width, desc.height,
             XenosTexture::UntileLevel(layout, 0, 0, gpuData.data(), gpuData.size(), desc.Format()), desc.depth}};
}

void RZTexture::SetVolumeLevel(const std::vector<BUDds::Level>& levels)
{
    if (levels.size() != 1 || levels[0].width != desc.width || levels[0].height != desc.height ||
        levels[0].depth != desc.depth)
        throw std::runtime_error("RZTexture: volume dimensions or mip count differ");
    desc.mipLevels = 1;
    const XenosTexture::Layout layout = desc.Layout();
    gpuData.assign(layout.Total(), 0);
    XenosTexture::TileLevel(layout, 0, 0, levels[0].data, gpuData, desc.Format());
}

std::vector<uint8_t> RZTexture::DecodeLevel0() const
{
    if (!desc.Is2D() && !desc.IsVolume())
        throw std::runtime_error("RZTexture: cubemap preview is not supported (" + name + ")");
    const auto levels = Levels(1);
    auto rgba = XenosTexture::DecodeToRGBA8(levels[0].data, desc.width,
                                            desc.height * (desc.IsVolume() ? desc.depth : 1), desc.Format());
    if (desc.IsVolume())
        return ReorderVolume(rgba, desc.width, desc.height, desc.depth, VolumeLayout::HorizontalStrip);
    return rgba;
}

std::vector<uint8_t> RZTexture::ToDds() const
{
    BUDds::Image image;
    image.width = desc.width;
    image.height = desc.height;
    image.volume = desc.IsVolume();
    image.depth = image.volume ? desc.depth : 1;
    image.fourCC = NativeFourCC(desc.Format());
    image.blockBytes = BUDds::BlockBytes(image.fourCC);
    image.levels = Levels();
    if (!image.fourCC)
    {
        for (auto& level : image.levels)
        {
            const auto rgba = XenosTexture::DecodeToRGBA8(level.data, level.width, level.height * level.depth,
                                                          desc.Format());
            level.data =
                XenosTexture::EncodeRGBA8To8888(rgba.data(), level.width, level.height * level.depth);
        }
    }
    return BUDds::Encode(image);
}

void RZTexture::ReplaceLevels(const std::vector<BUDds::Level>& levels)
{
    RZTexture candidate = *this;
    candidate.SetLevels(levels);
    if (candidate.gpuData.size() > gpuData.size() || !candidate.desc.SameLowHeader(desc))
        throw std::runtime_error("RZTexture: replacement changes the original layout");
    candidate.gpuData.resize(gpuData.size(), 0);
    gpuData = std::move(candidate.gpuData);
}

void RZTexture::ReplaceDds(const std::vector<uint8_t>& file)
{
    const auto image = BUDds::Decode(file);
    if (image.width != desc.width || image.height != desc.height || image.volume != desc.IsVolume() ||
        image.depth != (desc.IsVolume() ? desc.depth : 1))
        throw std::runtime_error("RZTexture: DDS dimensions do not match the package texture");

    if (image.levels.size() != std::max(1u, desc.mipLevels))
    {
        auto rgba = BUDds::DecodeRGBA8(image);
        if (desc.IsVolume())
            rgba = ReorderVolume(rgba, desc.width, desc.height, desc.depth, VolumeLayout::HorizontalStrip);
        Reencode(rgba.data(), PreviewWidth(), desc.height);
        return;
    }

    // Matching compressed formats can keep the DDS blocks without recompressing them.
    if (image.fourCC && image.fourCC == NativeFourCC(desc.Format()))
    {
        ReplaceLevels(image.levels);
        return;
    }

    std::vector<BUDds::Level> levels;
    for (size_t mip = 0; mip < image.levels.size(); ++mip)
        levels.push_back(EncodeDdsLevel(image, mip, desc.Format()));
    ReplaceLevels(levels);
}

void RZTexture::ReencodeVolume(const uint8_t* rgba, uint32_t width, uint32_t height)
{
    if (width != PreviewWidth() || height != desc.height)
        throw std::runtime_error("RZTexture: volume PNG must be a horizontal strip of all depth slices");
    std::vector<uint8_t> strip(rgba, rgba + size_t(width) * height * 4);
    auto voxels = ReorderVolume(strip, desc.width, desc.height, desc.depth, VolumeLayout::Slices);
    auto linear =
        XenosTexture::EncodeRGBA8(voxels.data(), desc.width, desc.height * desc.depth, desc.Format());
    ReplaceLevels({{desc.width, desc.height, std::move(linear), desc.depth}});
}
