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

        static constexpr size_t kPCHeaderSize = 28;
        static constexpr uint32_t kFlagSRGB = 1;
        static constexpr uint32_t kFlagLightmapScale = 2;
        static constexpr uint32_t kFlagNormalScale = 4;
        static constexpr uint32_t kNormalMap = kFlagSRGB | kFlagNormalScale;

        // PS3 needs its own texture reader later.
        // 3DS needs its own texture reader too.
        enum class Storage
        {
            Xbox360,
            PC
        };
        Storage storage = Storage::Xbox360;
        bool IsPC() const { return storage == Storage::PC; }
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

        XenosTexture::D3DFormat Format() const;

        bool Is2D() const { return kind == k2D; }
        bool IsCube() const { return kind == kCube; }
        bool IsVolume() const { return kind == kVolume; }
        uint32_t PreviewWidth() const { return width * (IsVolume() ? depth : IsPC() && IsCube() ? 6 : 1); }

        size_t Size() const
        {
            if (IsPC())
                return kPCHeaderSize;
            return 8 + (flags & kFlagLightmapScale ? 12 : 0) + (flags & kFlagNormalScale ? 4 : 0) +
                   (kind == k2D     ? 52
                    : kind == kCube ? 44
                                    : 40);
        }

        XenosTexture::Layout Layout() const;

        void UpdateLowHeader();

        bool SameLowHeader(const Descriptor& other) const
        {
            return lowDataOffset == other.lowDataOffset && lowLevels == other.lowLevels &&
                   lowWidth == other.lowWidth && lowHeight == other.lowHeight;
        }

    private:
        static Descriptor ParsePC(const CMChunk& textureData);
        static Descriptor ParseXbox(const CMChunk& textureData);
        void WriteXbox(CMChunk& textureData) const;
    };

    std::string name;
    uint32_t nameCRC = 0;
    uint32_t languageMask = 0x1F;
    Descriptor desc;
    // DDS bytes on PC; an XG texture allocation on Xbox 360.
    std::vector<uint8_t> gpuData;

    static RZTexture Load(const PKPackage& pkg, const CMChunkResourceHeader& header)
    {
        const CMChunk* post = pkg.FindRootAtOffset(header.GetDataOffset());
        if (!post || post->GetMaskedID() != Gen_PostLoadData)
            throw std::runtime_error("RZTexture: no Gen_PostLoadData at the header's data offset for " +
                                     header.GetName());
        const CMChunk* data = post->FindChild(Texture_Data);
        if (!data)
            throw std::runtime_error("RZTexture: Gen_PostLoadData without Texture_Data for " +
                                     header.GetName());
        RZTexture t;
        t.name = header.GetName();
        t.nameCRC = header.GetCRC();
        t.languageMask = header.GetLanguageMask();
        t.originalHeader = header;
        t.dataVersion = data->version;
        t.desc = Descriptor::Parse(*data);
        if (t.desc.IsPC())
            t.pcHeader.assign(data->data.begin(), data->data.begin() + Descriptor::kPCHeaderSize);
        t.gpuData.assign(data->data.begin() + static_cast<std::ptrdiff_t>(t.desc.Size()), data->data.end());
        return t;
    }

    static std::vector<CMChunkResourceHeader> Headers(const PKPackage& pkg)
    {
        std::vector<CMChunkResourceHeader> out;
        for (const CMChunk& root : pkg.rootChunks)
        {
            const std::vector<const CMChunk*> libs = root.GetMaskedID() == Root
                                                         ? root.FindChildren(Gen_TextureLibrary)
                                                         : std::vector<const CMChunk*>{};
            for (const CMChunk* lib : libs)
                for (const CMChunk* res : lib->FindChildren(GenSub_Resource))
                    if (const CMChunk* h = res->FindChild(GenSub_ResourceHeader))
                        out.emplace_back(*h);
        }
        return out;
    }

    static std::optional<CMChunkResourceHeader> FindHeader(const PKPackage& pkg, uint32_t crc,
                                                           uint32_t languageMask, uint32_t qualityLevel)
    {
        std::optional<CMChunkResourceHeader> found;
        if (!crc)
            return found;
        pkg.ForEachChunk([&](const CMChunk& chunk) {
            const CMChunk* h =
                chunk.GetMaskedID() == GenSub_Resource ? chunk.FindChild(GenSub_ResourceHeader) : nullptr;
            if (!h)
                return;
            CMChunkResourceHeader header(*h);
            if (header.GetResourceType() != kCMResourceType_Texture || header.GetCRC() != crc)
                return;
            if (!found ||
                (header.GetLanguageMask() == languageMask && header.GetQualityLevel() == qualityLevel))
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

    // These factories create Xbox 360 textures; Load detects the platform of existing assets.
    static RZTexture FromRGBA8(const std::string& name, const uint8_t* rgba, uint32_t width, uint32_t height,
                               uint32_t d3dFormat = XenosTexture::D3DFormat::kA8R8G8B8Tiled);

    enum class DdsColor
    {
        Gamma,
        Linear,
        NormalMap
    };

    static RZTexture FromDds(const std::string& name, const std::vector<uint8_t>& file,
                             bool baseLevelOnly = false, DdsColor color = DdsColor::Gamma,
                             float scale = 1.0f);

    // Lays linear mip levels out as retail does and sets the descriptor's mip fields to match.
    void SetLevels(const std::vector<BUDds::Level>& levels);

    // The inverse of SetLevels: the first count mip levels as linear blocks.
    std::vector<BUDds::Level> Levels(uint32_t count = UINT32_MAX) const;

    // Re-encodes from an RGBA8 image in the texture's own size, format and mip count, so the data keeps its
    // layout.
    void Reencode(const uint8_t* rgba, uint32_t width, uint32_t height);

    // Writes gpuData over this texture's data in pkg (it must keep its size) and updates both post-load CRCs.
    void Store(PKPackage& pkg) const;

    CMChunk BuildTextureData(bool littleEndian = false) const
    {
        CMChunk data = CMChunk::Leaf(Texture_Data, desc.IsPC() ? dataVersion : kTextureDataVersion, {});
        data.isLittleEndian = littleEndian || desc.IsPC();
        if (desc.IsPC())
            data.data = pcHeader;
        else
            desc.Write(data);
        data.AppendBytes(gpuData.data(), gpuData.size());
        return data;
    }

    CMChunk BuildLibraryResource(uint64_t dataOffset, bool littleEndian = false) const
    {
        CMChunk res = CMChunk::Container(GenSub_Resource, 1);
        res.isLittleEndian = littleEndian;
        const uint32_t crc = PostLoadCRC(BuildTextureData(littleEndian).data, littleEndian);
        res.AddChild(
            CMChunkResourceHeader::Build(nameCRC, 4, languageMask, 0, dataOffset, crc, name, littleEndian));
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
        post.AddChild(
            CMChunkResourceHeader::Build(nameCRC, 4, languageMask, 0, dataOffset, crc, name, littleEndian));
        post.AddChild(std::move(data));
        return post;
    }

    // CRC-32 of the Texture_Data payload, stored little-endian even in big-endian packages.
    static uint32_t PostLoadCRC(const std::vector<uint8_t>& textureData, bool littleEndian)
    {
        const uint32_t crc = BUCRC().GenerateRaw(textureData.data(), textureData.size());
        return littleEndian ? crc
                            : (crc >> 24) | ((crc >> 8) & 0xFF00u) | ((crc << 8) & 0xFF0000u) | (crc << 24);
    }

    static uint64_t RootChunkOffset(const PKPackage& pkg, size_t rootIndex)
    {
        uint64_t off = 0;
        for (size_t i = 0; i < rootIndex && i < pkg.rootChunks.size(); ++i)
            off += pkg.rootChunks[i].SerializedSize();
        return off;
    }

private:
    enum class SliceLayout
    {
        Slices,
        HorizontalStrip
    };
    static std::vector<uint8_t> ReorderSlices(const std::vector<uint8_t>& source, uint32_t width,
                                              uint32_t height, uint32_t slices, SliceLayout output);
    void SetXboxLevels(const std::vector<BUDds::Level>& levels);
    std::vector<BUDds::Level> XboxLevels(uint32_t count) const;
    void ReencodeXbox(const uint8_t* rgba, uint32_t width, uint32_t height);
    std::vector<uint8_t> DecodeXbox() const;
    std::vector<uint8_t> XboxDds() const;
    void ReplaceXbox(const BUDds::Image& image);
    std::vector<uint8_t> pcHeader;
    uint16_t dataVersion = kTextureDataVersion;
    std::optional<CMChunkResourceHeader> originalHeader;
    std::vector<uint8_t> DecodePC() const;
    void ReencodePC(const uint8_t* rgba, uint32_t width, uint32_t height);
    void ReplacePC(const BUDds::Image& source);
    std::vector<BUDds::Level> VolumeLevels() const;
    void SetVolumeLevel(const std::vector<BUDds::Level>& levels);
    void ReencodeVolume(const uint8_t* rgba, uint32_t width, uint32_t height);
    void ReplaceLevels(const std::vector<BUDds::Level>& levels);

    static CMChunk* FindLibraryHeader(PKPackage& pkg, uint32_t nameCRC,
                                      const std::optional<CMChunkResourceHeader>& original)
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
                    if (header && CMChunkResourceHeader(*header).GetCRC() == nameCRC &&
                        (!original || CMChunkResourceHeader(*header).IsSameResource(*original)))
                        return header;
                }
            }
        }
        return nullptr;
    }

    static std::vector<uint8_t> HalveRGBA8(const uint8_t* rgba, uint32_t width, uint32_t height);
};
