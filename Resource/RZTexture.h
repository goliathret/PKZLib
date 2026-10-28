#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "../BaseUtils/BUCRC.h"
#include "../BaseUtils/BUDds.h"
#include "../Graphics/XenosTexture.h"
#include "../Package/CMChunk.h"
#include "../Package/PKPackage.h"
#include "ResourceHeader.h"

class RZTexture
{
public:
    static constexpr uint16_t kTextureDataVersion = 7;

    struct Descriptor
    {
        enum Kind : uint32_t
        {
            k2D = 0,
            kCube = 1,
            kVolume = 2,
        };

        static constexpr uint32_t kFlagSRGB = 1;
        static constexpr uint32_t kFlagLightmapScale = 2;
        static constexpr uint32_t kFlagNormalScale = 4;
        static constexpr uint32_t kNormalMap = kFlagSRGB | kFlagNormalScale;

        uint32_t kind = k2D;
        uint32_t flags = kFlagSRGB;
        float lightmapScale[3] = {1, 1, 1};
        float normalScale = 1.0f;

        uint32_t width = 0, height = 0;
        uint32_t depth = 0;
        uint32_t mipLevels = 1;
        uint32_t usage = 0;
        uint32_t d3dFormat = 0;
        uint32_t expBias = 0;
        uint32_t xgFlags = 0;
        uint32_t baseOffset = 0;
        uint32_t mipOffset = XenosTexture::kXGContiguousMipOffset;
        uint32_t pitch = 0;

        uint32_t lowDataOffset = 0xFFFFFFFFu;
        uint32_t lowLevels = 1;
        uint16_t lowWidth = 0, lowHeight = 0;

        static Descriptor Parse(const CMChunk& textureData);
        void Write(CMChunk& textureData) const;

        XenosTexture::D3DFormat Format() const { return XenosTexture::D3DFormat::Decode(d3dFormat); }

        bool Is2D() const { return kind == k2D; }
        bool IsCube() const { return kind == kCube; }
        bool IsVolume() const { return kind == kVolume; }
        uint32_t PreviewWidth() const { return width * (IsVolume() ? depth : 1); }

        size_t Size() const
        {
            return 8 + (flags & kFlagLightmapScale ? 12 : 0) + (flags & kFlagNormalScale ? 4 : 0) +
                (kind == k2D ? 52 : kind == kCube ? 44 : 40);
        }

        XenosTexture::Layout Layout() const
        {
            if (pitch)
                throw std::runtime_error("RZTexture: an explicit pitch is not supported");
            if (mipLevels > 1 && mipOffset != XenosTexture::kXGContiguousMipOffset)
                throw std::runtime_error("RZTexture: a separate mip allocation is not supported");
            return XenosTexture::ComputeLayout(width, IsCube() ? width : height, depth, std::max(1u, mipLevels),
                                               IsCube() ? 6 : 1, IsVolume(), Format(), xgFlags);
        }

        void UpdateLowHeader()
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

        bool SameLowHeader(const Descriptor& other) const
        {
            return lowDataOffset == other.lowDataOffset && lowLevels == other.lowLevels &&
                   lowWidth == other.lowWidth && lowHeight == other.lowHeight;
        }
    };

    std::string name;
    uint32_t nameCRC = 0;
    uint32_t languageMask = 0x1F;
    Descriptor desc;
    std::vector<uint8_t> gpuData;

    static RZTexture Load(const PKPackage& pkg, const CMChunkResourceHeader& header)
    {
        const CMChunk* post = pkg.FindRootAtOffset(header.GetDataOffset());
        if (!post || post->GetMaskedID() != Gen_PostLoadData)
            throw std::runtime_error("RZTexture: no Gen_PostLoadData at the header's data offset for " + header.GetName());
        const CMChunk* data = post->FindChild(Texture_Data);
        if (!data)
            throw std::runtime_error("RZTexture: Gen_PostLoadData without Texture_Data for " + header.GetName());
        RZTexture t;
        t.name = header.GetName();
        t.nameCRC = header.GetCRC();
        t.languageMask = header.GetLanguageMask();
        t.desc = Descriptor::Parse(*data);
        t.gpuData.assign(data->data.begin() + static_cast<std::ptrdiff_t>(t.desc.Size()), data->data.end());
        return t;
    }

    static std::vector<CMChunkResourceHeader> Headers(const PKPackage& pkg)
    {
        std::vector<CMChunkResourceHeader> out;
        for (const CMChunk& root : pkg.rootChunks)
        {
            const std::vector<const CMChunk*> libs = root.GetMaskedID() == Root ? root.FindChildren(Gen_TextureLibrary)
                                                                               : std::vector<const CMChunk*>{};
            for (const CMChunk* lib : libs)
                for (const CMChunk* res : lib->FindChildren(GenSub_Resource))
                    if (const CMChunk* h = res->FindChild(GenSub_ResourceHeader))
                        out.emplace_back(*h);
        }
        return out;
    }

    static std::optional<CMChunkResourceHeader> FindHeader(const PKPackage& pkg, uint32_t crc, uint32_t languageMask,
                                                           uint32_t qualityLevel)
    {
        std::optional<CMChunkResourceHeader> found;
        if (!crc)
            return found;
        pkg.ForEachChunk([&](const CMChunk& chunk) {
            const CMChunk* h = chunk.GetMaskedID() == GenSub_Resource ? chunk.FindChild(GenSub_ResourceHeader) : nullptr;
            if (!h)
                return;
            CMChunkResourceHeader header(*h);
            if (header.GetResourceType() != kCMResourceType_Texture || header.GetCRC() != crc)
                return;
            if (!found || (header.GetLanguageMask() == languageMask && header.GetQualityLevel() == qualityLevel))
                found = std::move(header);
        });
        return found;
    }

    static const CMChunk* FindRootAtOffset(const PKPackage& pkg, uint64_t fileOffset)
    {
        return pkg.FindRootAtOffset(fileOffset);
    }

    static CMChunk* FindRootAtOffset(PKPackage& pkg, uint64_t fileOffset)
    {
        return pkg.FindRootAtOffset(fileOffset);
    }

    uint32_t PreviewWidth() const { return desc.PreviewWidth(); }
    std::vector<uint8_t> DecodeLevel0() const;
    std::vector<uint8_t> ToDds() const;
    void ReplaceDds(const std::vector<uint8_t>& file);

    static RZTexture FromRGBA8(const std::string& name, const uint8_t* rgba, uint32_t width, uint32_t height,
                               uint32_t d3dFormat = XenosTexture::D3DFormat::kA8R8G8B8Tiled)
    {
        RZTexture t;
        t.name = name;
        t.nameCRC = BUCRC().Generate(name);
        t.desc.width = width;
        t.desc.height = height;
        t.desc.d3dFormat = d3dFormat;
        if (t.desc.Format().format != XenosTexture::kFormat_8_8_8_8)
            throw std::runtime_error("RZTexture::FromRGBA8 writes 8_8_8_8 only");
        t.SetLevels({ BUDds::Level{ width, height, XenosTexture::EncodeRGBA8(rgba, width, height, t.desc.Format()) } });
        return t;
    }

    enum class DdsColor
    {
        Gamma,
        Linear,
        NormalMap
    };

    static RZTexture FromDds(const std::string& name, const std::vector<uint8_t>& file,
                             bool baseLevelOnly = false, DdsColor color = DdsColor::Gamma, float scale = 1.0f);

    // Lays linear mip levels out as retail does and sets the descriptor's mip fields to match.
    void SetLevels(const std::vector<BUDds::Level>& levels)
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

    // The inverse of SetLevels: the first count mip levels as linear blocks.
    std::vector<BUDds::Level> Levels(uint32_t count = UINT32_MAX) const
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
            levels.push_back({ layout.levels[i].width, layout.levels[i].height,
                               XenosTexture::UntileLevel(layout, i, 0, gpuData.data(), gpuData.size(), f) });
        return levels;
    }

    // Re-encodes from an RGBA8 image in the texture's own size, format and mip count, so the data keeps its layout.
    void Reencode(const uint8_t* rgba, uint32_t width, uint32_t height)
    {
        if (desc.IsVolume())
        {
            ReencodeVolume(rgba, width, height);
            return;
        }
        if (width != desc.width || height != desc.height)
            throw std::runtime_error("RZTexture: " + name + " is " + std::to_string(desc.width) + "x" +
                                     std::to_string(desc.height) + ", the image is " + std::to_string(width) + "x" +
                                     std::to_string(height));
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

    // Writes gpuData over this texture's data in pkg (it must keep its size) and updates both post-load CRCs.
    void Store(PKPackage& pkg) const
    {
        CMChunk* libraryHeader = FindLibraryHeader(pkg, nameCRC);
        if (!libraryHeader)
            throw std::runtime_error("RZTexture: no texture header for " + name);
        CMChunk* post = pkg.FindRootAtOffset(CMChunkResourceHeader(*libraryHeader).GetDataOffset());
        CMChunk* data = post ? post->FindChild(Texture_Data) : nullptr;
        if (!data)
            throw std::runtime_error("RZTexture: no Texture_Data for " + name);
        if (data->version != kTextureDataVersion)
            throw std::runtime_error("RZTexture: Texture_Data v" + std::to_string(data->version) + " of " + name +
                                     " is not the Edge of Time layout");
        Descriptor stored = desc;
        stored.UpdateLowHeader();
        CMChunk payload = CMChunk::Leaf(Texture_Data, data->version, {});
        payload.isLittleEndian = data->isLittleEndian;
        stored.Write(payload);
        payload.AppendBytes(gpuData.data(), gpuData.size());
        if (payload.data.size() != data->data.size())
            throw std::runtime_error("RZTexture: the new data for " + name + " changes the Texture_Data size");
        data->data = std::move(payload.data);

        const uint32_t crc = PostLoadCRC(data->data, data->isLittleEndian);
        CMChunkResourceHeader::SetPostLoadDataCRC(*libraryHeader, crc);
        if (CMChunk* postHeader = post->FindChild(GenSub_ResourceHeader))
            CMChunkResourceHeader::SetPostLoadDataCRC(*postHeader, crc);
    }

    CMChunk BuildTextureData(bool littleEndian = false) const
    {
        CMChunk data = CMChunk::Leaf(Texture_Data, kTextureDataVersion, {});
        data.isLittleEndian = littleEndian;
        desc.Write(data);
        data.AppendBytes(gpuData.data(), gpuData.size());
        return data;
    }

    CMChunk BuildLibraryResource(uint64_t dataOffset, bool littleEndian = false) const
    {
        CMChunk res = CMChunk::Container(GenSub_Resource, 1);
        res.isLittleEndian = littleEndian;
        const uint32_t crc = PostLoadCRC(BuildTextureData(littleEndian).data, littleEndian);
        res.AddChild(CMChunkResourceHeader::Build(nameCRC, 4, languageMask, 0, dataOffset, crc, name, littleEndian));
        CMChunk tex = CMChunk::Container(Texture, 6);
        tex.isLittleEndian = littleEndian;
        res.AddChild(std::move(tex));
        return res;
    }

    CMChunk BuildPostLoadData(uint64_t dataOffset, bool littleEndian = false) const
    {
        CMChunk post = CMChunk::Container(Gen_PostLoadData, 1);
        post.isLittleEndian = littleEndian;
        CMChunk data = BuildTextureData(littleEndian);
        const uint32_t crc = PostLoadCRC(data.data, littleEndian);
        post.AddChild(CMChunkResourceHeader::Build(nameCRC, 4, languageMask, 0, dataOffset, crc, name, littleEndian));
        post.AddChild(std::move(data));
        return post;
    }

    // CRC-32 of the Texture_Data payload, stored little-endian even in big-endian packages.
    static uint32_t PostLoadCRC(const std::vector<uint8_t>& textureData, bool littleEndian)
    {
        const uint32_t crc = BUCRC().GenerateRaw(textureData.data(), textureData.size());
        return littleEndian ? crc : (crc >> 24) | ((crc >> 8) & 0xFF00u) | ((crc << 8) & 0xFF0000u) | (crc << 24);
    }

    static uint64_t RootChunkOffset(const PKPackage& pkg, size_t rootIndex)
    {
        uint64_t off = 0;
        for (size_t i = 0; i < rootIndex && i < pkg.rootChunks.size(); ++i)
            off += pkg.rootChunks[i].SerializedSize();
        return off;
    }

private:
    std::vector<BUDds::Level> VolumeLevels() const;
    void SetVolumeLevel(const std::vector<BUDds::Level>& levels);
    void ReencodeVolume(const uint8_t* rgba, uint32_t width, uint32_t height);
    void ReplaceLevels(const std::vector<BUDds::Level>& levels);

    static CMChunk* FindLibraryHeader(PKPackage& pkg, uint32_t nameCRC)
    {
        for (CMChunk& root : pkg.rootChunks)
        {
            if (root.GetMaskedID() != Root)
                continue;
            for (CMChunk& lib : root.children)
            {
                if (lib.GetMaskedID() != Gen_TextureLibrary)
                    continue;
                for (CMChunk& res : lib.children)
                {
                    CMChunk* header = res.FindChild(GenSub_ResourceHeader);
                    if (header && CMChunkResourceHeader(*header).GetCRC() == nameCRC)
                        return header;
                }
            }
        }
        return nullptr;
    }

    static std::vector<uint8_t> HalveRGBA8(const uint8_t* rgba, uint32_t width, uint32_t height)
    {
        const uint32_t w = std::max(1u, width / 2), h = std::max(1u, height / 2);
        std::vector<uint8_t> out(size_t(w) * h * 4);
        for (uint32_t y = 0; y < h; ++y)
        {
            const uint32_t y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
            for (uint32_t x = 0; x < w; ++x)
            {
                const uint32_t x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
                for (int c = 0; c < 4; ++c)
                {
                    const uint32_t sum = rgba[(size_t(y0) * width + x0) * 4 + c] + rgba[(size_t(y0) * width + x1) * 4 + c] +
                                         rgba[(size_t(y1) * width + x0) * 4 + c] + rgba[(size_t(y1) * width + x1) * 4 + c];
                    out[(size_t(y) * w + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
                }
            }
        }
        return out;
    }
};
