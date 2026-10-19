#include "RZTexture.h"

namespace
{
std::vector<uint8_t> VolumeStrip(const std::vector<uint8_t>& source, uint32_t width, uint32_t height,
                                 uint32_t depth, bool toStrip)
{
    std::vector<uint8_t> result(source.size());
    for (uint32_t z = 0; z < depth; ++z)
        for (uint32_t y = 0; y < height; ++y)
        {
            const auto voxel = (size_t(z) * height + y) * width * 4;
            const auto strip = (size_t(y) * depth + z) * width * 4;
            std::copy_n(source.data() + (toStrip ? voxel : strip), size_t(width) * 4,
                        result.data() + (toStrip ? strip : voxel));
        }
    return result;
}
} // namespace

std::vector<BUDds::Level> RZTexture::VolumeLevels() const
{
    if (std::max(1u, desc.mipLevels) != 1)
        throw std::runtime_error("RZTexture: volume mip chains are not supported");
    return {{desc.width, desc.height,
             XenosTexture::UntileVolume(gpuData.data(), gpuData.size(), desc.width, desc.height, desc.depth,
                                        desc.Format()),
             desc.depth}};
}

void RZTexture::SetVolumeLevel(const std::vector<BUDds::Level>& levels)
{
    if (levels.size() != 1 || levels[0].width != desc.width || levels[0].height != desc.height ||
        levels[0].depth != desc.depth)
        throw std::runtime_error("RZTexture: volume dimensions or mip count differ");
    gpuData = XenosTexture::TileVolume(levels[0].data, desc.width, desc.height, desc.depth, desc.Format());
    desc.mipLevels = 1;
}

std::vector<uint8_t> RZTexture::DecodeLevel0() const
{
    if (!desc.Is2D() && !desc.IsVolume())
        throw std::runtime_error("RZTexture: cubemap preview is not supported (" + name + ")");
    const auto levels = Levels(1);
    auto rgba = XenosTexture::DecodeToRGBA8(levels[0].data, desc.width,
                                            desc.height * (desc.IsVolume() ? desc.depth : 1), desc.Format());
    return desc.IsVolume() ? VolumeStrip(rgba, desc.width, desc.height, desc.depth, true) : rgba;
}

void RZTexture::ReplaceLevels(const std::vector<BUDds::Level>& levels)
{
    RZTexture candidate = *this;
    candidate.SetLevels(levels);
    if (candidate.gpuData.size() > gpuData.size() || candidate.desc.mipChainOffset != desc.mipChainOffset ||
        candidate.desc.f13 != desc.f13 || candidate.desc.minLevelSize != desc.minLevelSize)
        throw std::runtime_error("RZTexture: replacement changes the original layout");
    candidate.gpuData.resize(gpuData.size(), 0);
    gpuData = std::move(candidate.gpuData);
}

void RZTexture::ReencodeVolume(const uint8_t* rgba, uint32_t width, uint32_t height)
{
    if (width != PreviewWidth() || height != desc.height)
        throw std::runtime_error("RZTexture: volume PNG must be a horizontal strip of all depth slices");
    std::vector<uint8_t> strip(rgba, rgba + size_t(width) * height * 4);
    auto voxels = VolumeStrip(strip, desc.width, desc.height, desc.depth, false);
    auto linear =
        XenosTexture::EncodeRGBA8(voxels.data(), desc.width, desc.height * desc.depth, desc.Format());
    ReplaceLevels({{desc.width, desc.height, std::move(linear), desc.depth}});
}
