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
        VolumeRoundTrip();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::puts("PASS: LUT volume descriptors, RGB channels and PNG strip round trips");
}
