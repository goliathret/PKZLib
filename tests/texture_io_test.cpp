#include "../Resource/RZTexture.h"
#include <cstdio>
#include <stdexcept>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <typename Function> bool Rejects(Function function)
{
    try
    {
        function();
    }
    catch (const std::exception&)
    {
        return true;
    }
    return false;
}
void Put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        bytes.at(offset + i) = uint8_t(value >> (i * 8));
}
void RgbAndDx10()
{
    BUDds::Image image;
    image.width = 3;
    image.height = 2;
    image.bitCount = 24;
    image.alphaMask = 0;
    image.levels.push_back({3, 2, std::vector<uint8_t>(18)});
    for (size_t i = 0; i < 18; i += 3)
    {
        image.levels[0].data[i] = 30;
        image.levels[0].data[i + 1] = 20;
        image.levels[0].data[i + 2] = 10;
    }
    auto file = BUDds::Encode(image);
    auto rgba = BUDds::DecodeRGBA8(BUDds::Decode(file));
    Check(rgba.size() == 24 && rgba[0] == 10 && rgba[1] == 20 && rgba[2] == 30 && rgba[3] == 255,
          "24-bit RGB decode or opaque alpha failed");
    auto padded = std::vector<uint8_t>(file.begin(), file.begin() + 128);
    Put32(padded, 20, 12);
    for (int row = 0; row < 2; ++row)
    {
        padded.insert(padded.end(), file.begin() + 128 + row * 9, file.begin() + 128 + (row + 1) * 9);
        padded.insert(padded.end(), 3, 0);
    }
    Check(BUDds::DecodeRGBA8(BUDds::Decode(padded)) == rgba, "Padded RGB rows were not unpacked");
    padded.pop_back();
    Check(Rejects([&] { BUDds::Decode(padded); }), "Truncated DDS row accepted");
    auto texture = RZTexture::FromDds("RGB", file);
    Check(texture.DecodeLevel0() == rgba, "24-bit DDS could not be converted to a game texture");

    BUDds::Image dx;
    dx.width = dx.height = 1;
    dx.redMask = 0xFF;
    dx.blueMask = 0xFF0000;
    dx.levels.push_back({1, 1, {11, 22, 33, 255}});
    auto legacy = BUDds::Encode(dx);
    std::vector<uint8_t> modern(legacy.begin(), legacy.begin() + 128);
    modern.resize(148);
    Put32(modern, 80, 4);
    Put32(modern, 84, BUDds::kDX10);
    Put32(modern, 128, 28);
    Put32(modern, 132, 3);
    Put32(modern, 140, 1);
    modern.insert(modern.end(), legacy.begin() + 128, legacy.end());
    Check(BUDds::DecodeRGBA8(BUDds::Decode(modern)) == dx.levels[0].data, "DX10 RGBA channel order failed");
    Put32(modern, 140, 2);
    Check(Rejects([&] { BUDds::Decode(modern); }), "DDS array was silently read as a single texture");
}
void CompressedMipRoundTrip()
{
    for (auto format : {XenosTexture::D3DFormat::kDXT1Tiled, XenosTexture::D3DFormat::kDXT5Tiled,
                        (XenosTexture::D3DFormat::kDXT5Tiled & ~63u) | XenosTexture::kFormat_DXT2_3})
    {
        RZTexture texture;
        texture.desc.width = texture.desc.height = 64;
        texture.desc.d3dFormat = format;
        const auto bpb = texture.desc.Format().BytesPerBlock();
        std::vector<BUDds::Level> levels;
        for (uint32_t side : {64, 32, 16, 8})
            levels.push_back(
                {side, side,
                 std::vector<uint8_t>(size_t((side + 3) / 4) * ((side + 3) / 4) * bpb, uint8_t(side))});
        texture.SetLevels(levels);
        const auto size = texture.gpuData.size();
        const auto offset = texture.desc.mipChainOffset;
        auto dds = BUDds::Decode(texture.ToDds());
        Check(dds.levels.size() == levels.size(), "DDS export lost mips");
        for (size_t i = 0; i < levels.size(); ++i)
            Check(dds.levels[i].data == levels[i].data, "DDS export recompressed native blocks");
        dds.levels[2].data[0] ^= 0xFF;
        texture.ReplaceDds(BUDds::Encode(dds));
        Check(texture.gpuData.size() == size && texture.desc.mipChainOffset == offset &&
                  texture.Levels()[2].data == dds.levels[2].data,
              "DDS import changed the layout or mip bytes");
        const auto before = texture.gpuData;
        dds.width *= 2;
        Check(Rejects([&] { texture.ReplaceDds(BUDds::Encode(dds)); }) && texture.gpuData == before,
              "Mismatched DDS changed the texture");
    }
}
void VolumeRoundTrip()
{
    RZTexture texture;
    texture.desc.kind = 2;
    texture.desc.width = texture.desc.height = texture.desc.depth = 32;
    texture.desc.d3dFormat = XenosTexture::D3DFormat::kA8R8G8B8Tiled;
    std::vector<uint8_t> voxels(32 * 32 * 32 * 4);
    for (uint32_t z = 0; z < 32; ++z)
        for (uint32_t y = 0; y < 32; ++y)
            for (uint32_t x = 0; x < 32; ++x)
            {
                const size_t i = ((z * 32 + y) * 32 + x) * 4;
                voxels[i] = uint8_t(z * 8);
                voxels[i + 1] = uint8_t(y * 8);
                voxels[i + 2] = uint8_t(x * 8);
                voxels[i + 3] = 255;
            }
    texture.SetLevels({{32, 32, voxels, 32}});
    Check(texture.BuildTextureData().data.size() == 48 + 131072, "Volume used the 2D header size");
    auto header = RZTexture::Descriptor::Parse(texture.BuildTextureData());
    Check(header.kind == 2 && header.width == 32 && header.height == 32 && header.depth == 32 &&
              header.mipLevels == 1 && header.d3dFormat == texture.desc.d3dFormat && header.Size() == 48,
          "Volume header fields were shifted");
    auto strip = texture.DecodeLevel0();
    Check(strip.size() == 1024 * 32 * 4, "LUT strip has the wrong dimensions");
    const size_t sample = (7 * 1024 + 9 * 32 + 5) * 4;
    Check(strip[sample] == 5 * 8 && strip[sample + 1] == 7 * 8 && strip[sample + 2] == 9 * 8,
          "Volume slices or RGB channels were transposed");
    const auto bytes = texture.gpuData;
    const auto dds = BUDds::Decode(texture.ToDds());
    Check(dds.volume && dds.depth == 32 && dds.width == 32 && dds.levels[0].data == voxels,
          "DDS export flattened or changed the LUT volume");
    texture.ReplaceDds(texture.ToDds());
    Check(texture.gpuData == bytes, "No-op LUT DDS import changed pixels");
    texture.Reencode(strip.data(), 1024, 32);
    Check(texture.gpuData == bytes, "PNG strip could not be imported without transposing slices");
    auto data = texture.BuildTextureData();
    data.data.resize(47);
    Check(Rejects([&] { RZTexture::Descriptor::Parse(data); }), "Truncated volume descriptor accepted");
    Check(Rejects([&] {
              XenosTexture::UntileVolume(bytes.data(), bytes.size(), 16384, 16384, 2048,
                                         texture.desc.Format());
          }),
          "Oversized volume allocation was attempted before checking the data size");
}
} // namespace
int main()
{
    try
    {
        RgbAndDx10();
        CompressedMipRoundTrip();
        VolumeRoundTrip();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::puts("PASS: RGB, DX10, BC1/2/3 mip preservation and LUT volume/strip round trips");
}
