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
    struct Descriptor
    {
        uint32_t kind = 0;
        uint32_t flags = 1;
        float lightmapScale[3] = {1, 1, 1};
        uint32_t headerUnknown = 0;
        float normalScale = 1.0f;
        uint32_t width = 0, height = 0;
        uint32_t mipLevels = 1;
        uint32_t depth = 0;
        uint32_t d3dFormat = 0;
        uint32_t stream0 = 0, stream1 = 0, stream2 = 0;
        uint32_t f10 = 0xFFFFFFFFu, f11 = 0;
        uint32_t mipChainOffset = 0xFFFFFFFFu;
        uint32_t f13 = 1;
        uint32_t minLevelSize = 0;

        static constexpr size_t kSize = 60;
        static constexpr uint32_t kNormalMap = 5;

        static Descriptor Parse(const CMChunk& textureData)
        {
            if (textureData.GetMaskedID() != Texture_Data || textureData.data.size() < 8)
                throw std::runtime_error("RZTexture: not a Texture_Data chunk");
            size_t off = 0;
            Descriptor d;
            d.kind = textureData.Read<uint32_t>(off);
            d.flags = textureData.Read<uint32_t>(off);
            if (d.flags & 2)
                for (float& scale : d.lightmapScale)
                    scale = textureData.Read<float>(off);
            if (d.flags & 4)
                d.normalScale = textureData.Read<float>(off);
            if (d.kind > 2 || textureData.data.size() < d.Size())
                throw std::runtime_error("RZTexture: invalid texture kind or truncated descriptor");
            d.width = textureData.Read<uint32_t>(off);
            d.height = d.kind == 1 ? d.width : textureData.Read<uint32_t>(off);
            if (d.kind == 2)
                d.depth = textureData.Read<uint32_t>(off);
            d.mipLevels = textureData.Read<uint32_t>(off);
            d.headerUnknown = textureData.Read<uint32_t>(off);
            d.d3dFormat = textureData.Read<uint32_t>(off);
            d.stream0 = textureData.Read<uint32_t>(off);
            d.stream1 = textureData.Read<uint32_t>(off);
            d.stream2 = textureData.Read<uint32_t>(off);
            d.f10 = textureData.Read<uint32_t>(off);
            if (d.kind != 2)
            {
                d.f11 = textureData.Read<uint32_t>(off);
                d.mipChainOffset = textureData.Read<uint32_t>(off);
                if (d.kind == 0)
                    d.f13 = textureData.Read<uint32_t>(off);
                d.minLevelSize = textureData.Read<uint32_t>(off);
            }
            uint32_t maxMips = 1;
            for (auto size = std::max({d.width, d.height, d.depth}); size > 1; size >>= 1)
                ++maxMips;
            if (!d.width || !d.height || d.width > 16384 || d.height > 16384 || d.depth > 2048 ||
                (d.kind == 2 && !d.depth) || d.mipLevels > maxMips)
                throw std::runtime_error("RZTexture: invalid texture dimensions or mip count");
            return d;
        }

        void Write(CMChunk& textureData) const
        {
            textureData.Append<uint32_t>(kind);
            textureData.Append<uint32_t>(flags);
            if (flags & 2)
                for (float scale : lightmapScale)
                    textureData.Append(scale);
            if (flags & 4)
                textureData.Append(normalScale);
            textureData.Append(width);
            if (kind != 1)
                textureData.Append(height);
            if (kind == 2)
                textureData.Append(depth);
            textureData.Append(mipLevels);
            textureData.Append(headerUnknown);
            textureData.Append(d3dFormat);
            textureData.Append(stream0);
            textureData.Append(stream1);
            textureData.Append(stream2);
            textureData.Append(f10);
            if (kind != 2)
            {
                textureData.Append(f11);
                textureData.Append(mipChainOffset);
                if (kind == 0)
                    textureData.Append(f13);
                textureData.Append(minLevelSize);
            }
        }

        XenosTexture::D3DFormat Format() const { return XenosTexture::D3DFormat::Decode(d3dFormat); }

        bool Is2D() const { return kind == 0; }
        bool IsVolume() const { return kind == 2; }
        uint32_t PreviewWidth() const { return width * (IsVolume() ? depth : 1); }

        size_t Size() const
        {
            return 8 + (flags & 2 ? 12 : 0) + (flags & 4 ? 4 : 0) +
                (kind == 0 ? 52 : kind == 1 ? 44 : 40);
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

        const size_t padded = (t.gpuData.size() + 0x3FFF) & ~size_t(0x3FFF);
        t.gpuData.resize(padded, 0);
        return t;
    }

    enum class DdsColor
    {
        Gamma,
        Linear,
        NormalMap
    };

    static RZTexture FromDds(const std::string& name, const std::vector<uint8_t>& file, bool baseLevelOnly = false,
                             DdsColor color = DdsColor::Gamma, float scale = 1.0f)
    {
        BUDds::Image img = BUDds::Decode(file);
        if (baseLevelOnly && img.levels.size() > 1)
            img.levels.resize(1);
        uint32_t format = XenosTexture::kFormat_DXT4_5;
        if (img.fourCC == BUDds::kDXT1)
            format = XenosTexture::kFormat_DXT1;
        else if (img.fourCC == BUDds::kDXT3)
            format = XenosTexture::kFormat_DXT2_3;
        if (color == DdsColor::NormalMap && format != XenosTexture::kFormat_DXT4_5)
            throw std::runtime_error("RZTexture::FromDds: a normal map is DXT5 (" + name + ")");

        RZTexture t;
        t.name = name;
        t.nameCRC = BUCRC().Generate(name);
        t.desc.flags = color == DdsColor::NormalMap ? Descriptor::kNormalMap : 1;
        t.desc.normalScale = scale;
        t.desc.width = img.width;
        t.desc.height = img.height;
        t.desc.mipLevels = static_cast<uint32_t>(img.levels.size());
        const uint32_t base = color == DdsColor::NormalMap ? XenosTexture::D3DFormat::kDXT5TiledNormalMap
                              : color == DdsColor::Linear  ? XenosTexture::D3DFormat::kDXT1TiledLinearColor
                                                           : XenosTexture::D3DFormat::kDXT5Tiled;
        t.desc.d3dFormat = (base & ~0x3Fu) | format;
        t.SetLevels(img.levels);
        return t;
    }

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
        const uint32_t tailLevels = mips > 2 ? mips - 2 : 1;
        const uint32_t tailFirst = mips - tailLevels;
        const uint32_t packedLevel = XenosTexture::PackedMipLevel(desc.width, desc.height);
        const uint32_t bd = f.BlockDim();
        const uint32_t bpb = f.BytesPerBlock();
        desc.mipLevels = mips;
        desc.mipChainOffset = 0xFFFFFFFFu;
        gpuData.clear();
        for (size_t i = 0; i < levels.size(); ++i)
        {
            const BUDds::Level& level = levels[i];
            if (i < packedLevel)
            {
                if (i == tailFirst && i > 0)
                    desc.mipChainOffset = static_cast<uint32_t>(gpuData.size());
                const std::vector<uint8_t> tiled = XenosTexture::Tile(level.data.data(), level.width, level.height, f);
                gpuData.insert(gpuData.end(), tiled.begin(), tiled.end());
                continue;
            }
            uint32_t tw = 0, th = 0;
            PackedTailBlocks(desc.width, desc.height, static_cast<uint32_t>(i), mips, f, tw, th);
            if (tailFirst >= i && tailFirst > 0)
            {
                // A wholly packed texture counts mipChainOffset from the end of its tail.
                uint32_t ox = 0, oy = 0;
                XenosTexture::PackedMipOffset(desc.width, desc.height, tailFirst, f, ox, oy);
                const size_t base = i > 0 ? gpuData.size() : size_t(tw) * th * bpb;
                desc.mipChainOffset = static_cast<uint32_t>(base + (size_t(oy) * tw + ox) * bpb);
            }
            std::vector<uint8_t> tail(size_t(tw) * th * bpb, 0);
            for (size_t k = i; k < levels.size(); ++k)
            {
                const BUDds::Level& sub = levels[k];
                uint32_t ox = 0, oy = 0;
                XenosTexture::PackedMipOffset(desc.width, desc.height, static_cast<uint32_t>(k), f, ox, oy);
                const uint32_t bw = (sub.width + bd - 1) / bd, bh = (sub.height + bd - 1) / bd;
                if (ox + bw > tw || oy + bh > th)
                    throw std::runtime_error("RZTexture: level " + std::to_string(k) + " of " + name +
                                             " does not fit the mip tail");
                for (uint32_t y = 0; y < bh; ++y)
                    std::memcpy(tail.data() + (size_t(oy + y) * tw + ox) * bpb, sub.data.data() + size_t(y) * bw * bpb,
                                size_t(bw) * bpb);
            }
            const std::vector<uint8_t> tiled = XenosTexture::Tile(tail.data(), tw * bd, th * bd, f);
            gpuData.insert(gpuData.end(), tiled.begin(), tiled.end());
            break;
        }

        const BUDds::Level& tailTop = levels[tailFirst];
        desc.f13 = tailLevels;
        desc.minLevelSize = (tailTop.width << 16) | tailTop.height;
    }

    // The inverse of SetLevels: the first count mip levels as linear blocks.
    std::vector<BUDds::Level> Levels(uint32_t count = UINT32_MAX) const
    {
        if (desc.IsVolume())
            return count ? VolumeLevels() : std::vector<BUDds::Level>{};
        if (!desc.Is2D())
            throw std::runtime_error("RZTexture: cubemap layout is not supported");
        const XenosTexture::D3DFormat f = desc.Format();
        const uint32_t mips = std::max(1u, desc.mipLevels);
        const uint32_t wanted = std::min(mips, count);
        const uint32_t packedLevel = XenosTexture::PackedMipLevel(desc.width, desc.height);
        const uint32_t bd = f.BlockDim();
        const uint32_t bpb = f.BytesPerBlock();
        std::vector<BUDds::Level> levels;
        size_t offset = 0;
        for (uint32_t i = 0; i < wanted; ++i)
        {
            if (offset > gpuData.size())
                throw std::runtime_error("RZTexture: " + name + " is shorter than its mip levels");
            const uint32_t w = std::max(1u, desc.width >> i), h = std::max(1u, desc.height >> i);
            if (i < packedLevel)
            {
                levels.push_back({ w, h, XenosTexture::Untile(gpuData.data() + offset, gpuData.size() - offset, w, h, f) });
                offset += XenosTexture::TiledSurfaceSize(w, h, f);
                continue;
            }
            uint32_t tw = 0, th = 0;
            PackedTailBlocks(desc.width, desc.height, i, mips, f, tw, th);
            const std::vector<uint8_t> tail =
                XenosTexture::Untile(gpuData.data() + offset, gpuData.size() - offset, tw * bd, th * bd, f);
            for (uint32_t k = i; k < wanted; ++k)
            {
                const uint32_t kw = std::max(1u, desc.width >> k), kh = std::max(1u, desc.height >> k);
                uint32_t ox = 0, oy = 0;
                XenosTexture::PackedMipOffset(desc.width, desc.height, k, f, ox, oy);
                const uint32_t bw = (kw + bd - 1) / bd, bh = (kh + bd - 1) / bd;
                BUDds::Level level{ kw, kh, std::vector<uint8_t>(size_t(bw) * bh * bpb) };
                for (uint32_t y = 0; y < bh; ++y)
                    std::memcpy(level.data.data() + size_t(y) * bw * bpb, tail.data() + (size_t(oy + y) * tw + ox) * bpb,
                                size_t(bw) * bpb);
                levels.push_back(std::move(level));
            }
            break;
        }
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
        const size_t descSize = Descriptor::Parse(*data).Size();
        if (data->data.size() != descSize + gpuData.size())
            throw std::runtime_error("RZTexture: the new data for " + name + " changes the Texture_Data size");
        std::copy(gpuData.begin(), gpuData.end(), data->data.begin() + static_cast<std::ptrdiff_t>(descSize));

        const uint32_t crc = PostLoadCRC(data->data, data->isLittleEndian);
        CMChunkResourceHeader::SetPostLoadDataCRC(*libraryHeader, crc);
        if (CMChunk* postHeader = post->FindChild(GenSub_ResourceHeader))
            CMChunkResourceHeader::SetPostLoadDataCRC(*postHeader, crc);
    }

    CMChunk BuildTextureData(bool littleEndian = false) const
    {
        CMChunk data = CMChunk::Leaf(Texture_Data, 7, {});
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

    // The packed tail's surface in blocks: 32x32, or wider or taller for a thin texture's levels.
    static void PackedTailBlocks(uint32_t width, uint32_t height, uint32_t first, uint32_t mips,
                                 const XenosTexture::D3DFormat& f, uint32_t& wBlocks, uint32_t& hBlocks)
    {
        const uint32_t blockDim = f.BlockDim();
        wBlocks = hBlocks = 32;
        for (uint32_t k = first; k < mips; ++k)
        {
            uint32_t ox = 0, oy = 0;
            XenosTexture::PackedMipOffset(width, height, k, f, ox, oy);
            const uint32_t w = std::max(1u, width >> k), h = std::max(1u, height >> k);
            wBlocks = std::max(wBlocks, XenosTexture::AlignUp(ox + (w + blockDim - 1) / blockDim, 32));
            hBlocks = std::max(hBlocks, XenosTexture::AlignUp(oy + (h + blockDim - 1) / blockDim, 32));
        }
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
