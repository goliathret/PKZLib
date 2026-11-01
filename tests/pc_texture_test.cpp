#include "../Resource/RZTexture.h"
#include <cstdio>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    template <class Function> bool Rejects(Function function)
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
            bytes.at(offset + i) = uint8_t(value >> (8 * i));
    }
    BUDds::Image Image(uint32_t format, bool cube = false, bool volume = false, bool luminance = false)
    {
        BUDds::Image image;
        image.width = image.height = 8;
        image.fourCC = format;
        image.cubemap = cube;
        image.volume = volume;
        image.depth = volume ? 4 : 1;
        image.luminance = luminance;
        if (luminance)
        {
            image.bitCount = 8;
            image.redMask = 255;
            image.greenMask = image.blueMask = image.alphaMask = 0;
        }
        for (size_t face = 0; face < image.Faces(); ++face)
            for (uint32_t mip = 0; mip < (volume ? 1u : 4u); ++mip)
            {
                const uint32_t side = std::max(1u, 8u >> mip);
                std::vector<uint8_t> pixels(size_t(side) * side * image.depth * 4, 255);
                for (size_t i = 0; i < pixels.size(); i += 4)
                {
                    pixels[i] = uint8_t(30 + face * 30);
                    pixels[i + 1] = 90;
                    pixels[i + 2] = 160;
                }
                image.levels.push_back({side, side,
                                        BUDds::EncodeRGBA8(image, pixels.data(), side, side, image.depth),
                                        image.depth});
            }
        return image;
    }
    PKPackage Fixture(const BUDds::Image& image, uint16_t version, bool wide = false)
    {
        auto root = CMChunk::Container(Root, 1, wide);
        auto library = CMChunk::Container(Gen_TextureLibrary, 1, wide);
        auto resource = CMChunk::Container(GenSub_Resource, 1, wide);
        const uint16_t headerVersion = version == 5 ? 3 : 5;
        auto header =
            CMChunkResourceHeader::Build(123, 4, 31, 7, 0, 0, "pc texture", true, wide, headerVersion);
        resource.AddChild(header);
        resource.AddChild(CMChunk::Container(Texture, 6, wide));
        library.AddChild(resource);
        const auto dds = BUDds::Encode(image);
        auto data = CMChunk::Leaf(Texture_Data, version, std::vector<uint8_t>(28, 0x43), wide);
        data.isLittleEndian = true;
        Put32(data.data, 0, uint32_t(dds.size() + 4));
        Put32(data.data, 20, 1);
        Put32(data.data, 24, image.cubemap ? 1 : image.volume ? 2 : 0);
        data.data.insert(data.data.end(), dds.begin(), dds.end());
        const uint64_t offset = root.HeaderSize() + library.SerializedSize();
        header = CMChunkResourceHeader::Build(123, 4, 31, 7, offset, RZTexture::PostLoadCRC(data.data, true),
                                              "pc texture", true, wide, headerVersion);
        library.children[0].children[0] = header;
        root.AddChild(std::move(library));
        auto post = CMChunk::Container(Gen_PostLoadData, 1, wide);
        post.AddChild(header);
        post.AddChild(std::move(data));
        root.AddChild(std::move(post));
        PKPackage package;
        package.isLittleEndian = true;
        package.rootChunks.push_back(std::move(root));
        const auto bytes = package.Serialize();
        package.ReadFromMemoryAutoEndian(bytes);
        return package;
    }
    void TextureTests()
    {
        for (uint16_t version : {5, 10})
            for (auto format : {0u, BUDds::kDXT1, BUDds::kDXT5})
                for (bool cube : {false, true})
                {
                    auto image = Image(format, cube);
                    auto package = Fixture(image, version, version == 10);
                    const auto header = RZTexture::Headers(package).at(0);
                    Check(header.GetQualityLevel() == 7 && header.GetLanguageMask() == 31,
                          "PC resource header field order changed");
                    auto texture = RZTexture::Load(package, header);
                    const auto original = texture.BuildTextureData().data;
                    const auto dds = texture.ToDds();
                    texture.ReplaceDds(dds);
                    texture.Store(package);
                    Check(texture.ToDds() == dds && texture.BuildTextureData().data == original,
                          "Native DDS no-op changed headers, wrappers or blocks");
                    auto pixels = texture.DecodeLevel0();
                    Check(pixels.size() == size_t(texture.PreviewWidth()) * 8 * 4,
                          "PC cube preview lost faces");
                    for (size_t i = 0; i < pixels.size(); i += 4)
                    {
                        pixels[i] = 0;
                        pixels[i + 1] = 255;
                        pixels[i + 2] = 0;
                        pixels[i + 3] = 255;
                    }
                    texture.Reencode(pixels.data(), texture.PreviewWidth(), 8);
                    texture.Store(package);
                    const auto changed = texture.BuildTextureData().data;
                    Check(changed.size() == original.size() &&
                              std::equal(changed.begin(), changed.begin() + 156, original.begin()),
                          "PNG replacement changed the PC wrapper or DDS header");
                    PKPackage reloaded;
                    reloaded.ReadFromMemoryAutoEndian(package.Serialize());
                    auto saved = RZTexture::Load(reloaded, RZTexture::Headers(reloaded)[0]);
                    Check(saved.DecodeLevel0()[1] > 240 && saved.ToDds() == texture.ToDds(),
                          "Saved PC texture did not retain pixels, mip levels or faces");
                    auto wrong = Image(format, !cube);
                    const auto before = saved.ToDds();
                    Check(Rejects([&] { saved.ReplaceDds(BUDds::Encode(wrong)); }) && saved.ToDds() == before,
                          "Incompatible DDS changed the texture");
                }
        for (bool luminance : {false, true})
        {
            auto package = Fixture(Image(0, false, !luminance, luminance), 10);
            auto texture = RZTexture::Load(package, RZTexture::Headers(package)[0]);
            const auto pixels = texture.DecodeLevel0();
            if (luminance)
                Check(pixels[0] == pixels[1] && pixels[1] == pixels[2], "L8 texture was not grayscale");
            texture.Reencode(pixels.data(), texture.PreviewWidth(), texture.desc.height);
            Check(texture.DecodeLevel0() == pixels, "LUT slices or luminance channels changed on roundtrip");
        }
    }
    void PaddingAndAlphaTests()
    {
        BUDds::Image image;
        image.width = 3;
        image.height = 2;
        image.bitCount = 24;
        image.alphaMask = 0;
        image.levels.push_back({3, 2, std::vector<uint8_t>(18, 90)});
        auto package = Fixture(image, 5);
        const auto header = RZTexture::Headers(package)[0];
        auto* data = package.FindRootAtOffset(header.GetDataOffset())->FindChild(Texture_Data);
        auto dds = BUDds::Encode(image);
        std::vector<uint8_t> padded(dds.begin(), dds.begin() + 128);
        Put32(padded, 20, 12);
        for (size_t row = 0; row < 2; ++row)
        {
            padded.insert(padded.end(), dds.begin() + 128 + row * 9, dds.begin() + 128 + (row + 1) * 9);
            padded.insert(padded.end(), 3, 0xA5);
        }
        data->data.resize(28);
        Put32(data->data, 0, uint32_t(padded.size() + 4));
        data->data.insert(data->data.end(), padded.begin(), padded.end());
        auto texture = RZTexture::Load(package, header);
        auto pixels = texture.DecodeLevel0();
        for (size_t i = 0; i < pixels.size(); i += 4)
            pixels[i] = 255;
        texture.Reencode(pixels.data(), 3, 2);
        const auto replaced = texture.ToDds();
        Check(replaced[130] == 255 && replaced[142] == 255 && replaced[137] == 0xA5 &&
                  replaced[139] == 0xA5 && replaced[149] == 0xA5 && replaced[151] == 0xA5,
              "RGB24 replacement changed row padding or channel order");
        image.width = image.height = 2;
        image.bitCount = 8;
        image.redMask = image.greenMask = image.blueMask = 0;
        image.alphaMask = 255;
        image.levels = {{2, 2, {0, 50, 128, 255}}};
        package = Fixture(image, 10);
        texture = RZTexture::Load(package, RZTexture::Headers(package)[0]);
        pixels = texture.DecodeLevel0();
        Check(pixels[0] == 255 && pixels[3] == 0 && pixels[7] == 50 && pixels[15] == 255,
              "Alpha-only DDS preview lost the alpha channel");
        texture.Reencode(pixels.data(), 2, 2);
        Check(texture.DecodeLevel0() == pixels, "Alpha-only PNG roundtrip changed opacity");
    }

    void ContainerTests()
    {
        const auto source = Fixture(Image(BUDds::kDXT1), 10).Serialize();
        for (int format = 0; format < 4; ++format)
        {
            BUCompress::Options options;
            options.bigEndian32 = format == 0;
            options.wideOffsets = format >= 2;
            options.lookupFirst = format == 3;
            PKPackage package;
            package.ReadFromMemoryAutoEndian(BUCompress::Compress(source, options));
            const auto savedOptions = package.ContainerOptions();
            Check(savedOptions.bigEndian32 == options.bigEndian32 &&
                      savedOptions.lookupFirst == options.lookupFirst &&
                      savedOptions.wideOffsets == (!options.bigEndian32 && options.wideOffsets),
                  "Container layout was lost");
            Check(package.Serialize() == source, "Container changed package bytes");
            PKPackage reloaded;
            reloaded.ReadFromMemoryAutoEndian(BUCompress::Compress(package.Serialize(), savedOptions));
            Check(reloaded.Serialize() == source, "Saved PC container could not be reopened");
        }
        auto package = Fixture(Image(0), 5);
        auto root = package.rootChunks[0];
        auto post = root.children.back();
        root.children.pop_back();
        const uint32_t offset = uint32_t(root.SerializedSize());
        auto header = CMChunkResourceHeader::Build(123, 4, 31, 7, offset, 0, "pc texture", true, false, 3);
        root.children[0].children[0].children[0] = header;
        post.children[0] = header;
        package.rootChunks = {root, post};
        auto bytes = package.Serialize();
        Put32(bytes, offset + 8, uint32_t(bytes.size() * 2));
        package.ReadFromMemoryAutoEndian(bytes);
        Check(package.warnings.size() == 1 &&
                  RZTexture::Load(package, RZTexture::Headers(package)[0]).desc.IsPC(),
              "Complete texture payload behind a malformed post-load length was not recovered");
        PKPackage repaired;
        repaired.ReadFromMemoryAutoEndian(package.Serialize());
        Check(repaired.warnings.empty(), "Saving did not repair the recovered container length");
        bytes.resize(offset - 10);
        Check(Rejects([&] { repaired.ReadFromMemoryAutoEndian(bytes); }),
              "Truncated main library was accepted");
    }
} // namespace
int main()
{
    try
    {
        TextureTests();
        PaddingAndAlphaTests();
        ContainerTests();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::puts("PASS: PC DDS textures, cubes, LUTs, luminance, container variants and bounded recovery");
}
