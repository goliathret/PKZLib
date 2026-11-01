#include "../RZTexture.h"

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

    BUDds::Level EncodeDdsLevel(const BUDds::Image& image, size_t mip, const XenosTexture::D3DFormat& format)
    {
        const auto& level = image.levels[mip];
        const auto rgba = BUDds::DecodeRGBA8(image, mip);
        auto pixels = XenosTexture::EncodeRGBA8(rgba.data(), level.width, level.height * level.depth, format);
        return {level.width, level.height, std::move(pixels), level.depth};
    }
} // namespace

RZTexture::Descriptor RZTexture::Descriptor::ParseXbox(const CMChunk& textureData)
{
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

void RZTexture::Descriptor::WriteXbox(CMChunk& textureData) const
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
    if (image.volume || image.cubemap)
        throw std::runtime_error(
            "RZTexture::FromDds: use an existing volume or cubemap texture for replacement");
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
             XenosTexture::UntileLevel(layout, 0, 0, gpuData.data(), gpuData.size(), desc.Format()),
             desc.depth}};
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

std::vector<uint8_t> RZTexture::DecodeXbox() const
{
    if (!desc.Is2D() && !desc.IsVolume())
        throw std::runtime_error("RZTexture: cubemap preview is not supported (" + name + ")");
    const auto levels = Levels(1);
    auto rgba = XenosTexture::DecodeToRGBA8(levels[0].data, desc.width,
                                            desc.height * (desc.IsVolume() ? desc.depth : 1), desc.Format());
    if (desc.IsVolume())
        return ReorderSlices(rgba, desc.width, desc.height, desc.depth, SliceLayout::HorizontalStrip);
    return rgba;
}

std::vector<uint8_t> RZTexture::XboxDds() const
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

void RZTexture::ReplaceXbox(const BUDds::Image& image)
{
    if (image.cubemap)
        throw std::runtime_error("RZTexture: Xbox cubemap replacement is not supported");
    if (image.width != desc.width || image.height != desc.height || image.volume != desc.IsVolume() ||
        image.depth != (desc.IsVolume() ? desc.depth : 1))
        throw std::runtime_error("RZTexture: DDS dimensions do not match the package texture");

    if (image.levels.size() != std::max(1u, desc.mipLevels))
    {
        auto rgba = BUDds::DecodeRGBA8(image);
        if (desc.IsVolume())
            rgba = ReorderSlices(rgba, desc.width, desc.height, desc.depth, SliceLayout::HorizontalStrip);
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
    auto voxels = ReorderSlices(strip, desc.width, desc.height, desc.depth, SliceLayout::Slices);
    auto linear =
        XenosTexture::EncodeRGBA8(voxels.data(), desc.width, desc.height * desc.depth, desc.Format());
    ReplaceLevels({{desc.width, desc.height, std::move(linear), desc.depth}});
}

RZTexture RZTexture::FromRGBA8(const std::string& name, const uint8_t* rgba, uint32_t width, uint32_t height,
                               uint32_t d3dFormat)
{
    RZTexture t;
    t.name = name;
    t.nameCRC = BUCRC().Generate(name);
    t.desc.width = width;
    t.desc.height = height;
    t.desc.d3dFormat = d3dFormat;
    if (t.desc.Format().format != XenosTexture::kFormat_8_8_8_8)
        throw std::runtime_error("RZTexture::FromRGBA8 writes 8_8_8_8 only");
    t.SetLevels(
        {BUDds::Level{width, height, XenosTexture::EncodeRGBA8(rgba, width, height, t.desc.Format())}});
    return t;
}

void RZTexture::SetXboxLevels(const std::vector<BUDds::Level>& levels)
{

    if (desc.IsVolume())
    {
        SetVolumeLevel(levels);
        return;
    }
    if (!desc.Is2D())
        throw std::runtime_error("RZTexture: cubemap layout is not supported");
    if (levels.empty())
        throw std::runtime_error("RZTexture: no mip levels for " + name);
    const XenosTexture::D3DFormat f = desc.Format();
    const uint32_t mips = static_cast<uint32_t>(levels.size());
    if (!desc.width || !desc.height || mips > 15)
        throw std::runtime_error("RZTexture: invalid mip chain");
    for (size_t i = 0; i < levels.size(); ++i)
    {
        const auto& level = levels[i];
        const auto width = std::max(1u, desc.width >> i), height = std::max(1u, desc.height >> i);
        const auto bd = f.BlockDim();
        if (!bd || level.width != width || level.height != height || level.depth != 1 ||
            level.data.size() != size_t((width + bd - 1) / bd) * ((height + bd - 1) / bd) * f.BytesPerBlock())
            throw std::runtime_error("RZTexture: invalid mip dimensions or byte count");
    }
    desc.mipLevels = mips;
    const XenosTexture::Layout layout = desc.Layout();
    gpuData.assign(layout.Total(), 0);
    for (uint32_t i = 0; i < mips; ++i)
        XenosTexture::TileLevel(layout, i, 0, levels[i].data, gpuData, f);
    desc.UpdateLowHeader();
}

std::vector<BUDds::Level> RZTexture::XboxLevels(uint32_t count) const
{

    if (desc.IsVolume())
        return count ? VolumeLevels() : std::vector<BUDds::Level>{};
    if (!desc.Is2D())
        throw std::runtime_error("RZTexture: cubemap layout is not supported");
    const XenosTexture::D3DFormat f = desc.Format();
    const XenosTexture::Layout layout = desc.Layout();
    const uint32_t wanted = std::min(std::max(1u, desc.mipLevels), count);
    std::vector<BUDds::Level> levels;
    for (uint32_t i = 0; i < wanted; ++i)
        levels.push_back({layout.levels[i].width, layout.levels[i].height,
                          XenosTexture::UntileLevel(layout, i, 0, gpuData.data(), gpuData.size(), f)});
    return levels;
}

void RZTexture::ReencodeXbox(const uint8_t* rgba, uint32_t width, uint32_t height)
{

    if (desc.IsVolume())
    {
        ReencodeVolume(rgba, width, height);
        return;
    }
    if (width != desc.width || height != desc.height)
        throw std::runtime_error("RZTexture: " + name + " is " + std::to_string(desc.width) + "x" +
                                 std::to_string(desc.height) + ", the image is " + std::to_string(width) +
                                 "x" + std::to_string(height));
    if (!desc.Is2D())
        throw std::runtime_error("RZTexture: only 2D textures are re-encoded (" + name + ")");
    const XenosTexture::D3DFormat f = desc.Format();
    std::vector<BUDds::Level> levels(desc.mipLevels ? desc.mipLevels : 1);
    std::vector<uint8_t> image(rgba, rgba + size_t(width) * height * 4);
    for (size_t i = 0; i < levels.size(); ++i)
    {
        levels[i].width = width;
        levels[i].height = height;
        levels[i].data = XenosTexture::EncodeRGBA8(image.data(), width, height, f);
        if (i + 1 < levels.size())
        {
            image = HalveRGBA8(image.data(), width, height);
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
    }
    ReplaceLevels(levels);
}

XenosTexture::D3DFormat RZTexture::Descriptor::Format() const
{
    if (IsPC())
        throw std::runtime_error("RZTexture: PC DDS data does not use an Xenos format");
    return XenosTexture::D3DFormat::Decode(d3dFormat);
}

XenosTexture::Layout RZTexture::Descriptor::Layout() const
{
    if (pitch)
        throw std::runtime_error("RZTexture: an explicit pitch is not supported");
    if (mipLevels > 1 && mipOffset != XenosTexture::kXGContiguousMipOffset)
        throw std::runtime_error("RZTexture: a separate mip allocation is not supported");
    return XenosTexture::ComputeLayout(width, IsCube() ? width : height, depth, std::max(1u, mipLevels),
                                       IsCube() ? 6 : 1, IsVolume(), Format(), xgFlags);
}

void RZTexture::Descriptor::UpdateLowHeader()
{
    if (IsVolume())
        return;
    const XenosTexture::Layout layout = Layout();
    const uint32_t levels = std::max(1u, mipLevels);
    const uint32_t first = std::min(2u, levels - 1);
    const XenosTexture::Layout::Level& level = layout.levels[first];
    const XenosTexture::Layout::Subresource& surface = layout.Of(first);
    lowLevels = levels - first;
    lowWidth = static_cast<uint16_t>(level.width);
    lowHeight = static_cast<uint16_t>(level.height);
    if (!first)
        lowDataOffset = 0xFFFFFFFFu;
    else if (!level.packed)
        lowDataOffset = surface.offset;
    else
        lowDataOffset = (layout.packedLevel == 0 ? layout.baseSize : surface.offset) +
                        (level.tailY * surface.pitch + level.tailX) * layout.bytesPerBlock;
}
