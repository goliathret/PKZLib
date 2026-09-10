#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "../BaseUtils/BUCRC.h"
#include "../Package/CMChunk.h"
#include "../Package/PKPackage.h"
#include "ResourceHeader.h"

class RZFont
{
public:
    struct Glyph
    {
        uint32_t flags = 0;
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        uint32_t codePoint = 0;
        float advance = 0, height = 0, width = 0;
    };

    std::string name;
    uint32_t nameCRC = 0;
    uint32_t languageMask = 0x1F;
    uint32_t version = 3;
    uint32_t infoVersion = 6;
    uint32_t dataVersion = 5;
    uint32_t glyphCount = 0;
    uint32_t info1 = 0;
    float info2 = 0, info3 = 0, info4 = 0;
    uint32_t info5 = 0, info6 = 0, info7 = 0;
    uint32_t textureCRC = 0;
    std::vector<Glyph> glyphs;

    static RZFont Parse(const CMChunk& resource)
    {
        RZFont f;
        if (const CMChunk* h = resource.FindChild(GenSub_ResourceHeader))
        {
            CMChunkResourceHeader header(*h);
            f.name = header.GetName();
            f.nameCRC = header.GetCRC();
            f.languageMask = header.GetLanguageMask();
        }
        const CMChunk* fh = resource.FindChild(Font_Header);
        if (!fh)
            throw std::runtime_error("RZFont: no Font_Header in " + f.name);
        f.version = fh->version;
        const CMChunk* info = fh->FindChild(Font_Info);
        const CMChunk* data = fh->FindChild(Font_Data);
        if (!info || !data)
            throw std::runtime_error("RZFont: Font_Info or Font_Data missing in " + f.name);
        f.infoVersion = info->version;
        f.dataVersion = data->version;
        size_t off = 0;
        f.glyphCount = info->Read<uint32_t>(off);
        f.info1 = info->Read<uint32_t>(off);
        f.info2 = info->Read<float>(off);
        f.info3 = info->Read<float>(off);
        f.info4 = info->Read<float>(off);
        f.info5 = info->Read<uint32_t>(off);
        f.info6 = info->Read<uint32_t>(off);
        f.info7 = info->Read<uint32_t>(off);
        f.textureCRC = info->Read<uint32_t>(off);
        if (data->data.size() < size_t(f.glyphCount) * 36)
            throw std::runtime_error("RZFont: Font_Data shorter than the glyph count in " + f.name);
        off = 0;
        f.glyphs.resize(f.glyphCount);
        for (Glyph& g : f.glyphs)
        {
            g.flags = data->Read<uint32_t>(off);
            g.u0 = data->Read<float>(off);
            g.v0 = data->Read<float>(off);
            g.u1 = data->Read<float>(off);
            g.v1 = data->Read<float>(off);
            g.codePoint = data->Read<uint32_t>(off);
            g.advance = data->Read<float>(off);
            g.height = data->Read<float>(off);
            g.width = data->Read<float>(off);
        }
        return f;
    }

    static std::vector<RZFont> ParseLibrary(const CMChunk& library)
    {
        std::vector<RZFont> out;
        for (const CMChunk* res : library.FindChildren(GenSub_Resource))
            out.push_back(Parse(*res));
        return out;
    }

    static std::vector<RZFont> All(const PKPackage& pkg)
    {
        std::vector<RZFont> out;
        for (const CMChunk& root : pkg.rootChunks)
            if (root.GetMaskedID() == Root)
                for (const CMChunk* lib : root.FindChildren(Gen_FontLibrary))
                    for (RZFont& f : ParseLibrary(*lib))
                        out.push_back(std::move(f));
        return out;
    }

    RZFont Renamed(const std::string& newName) const
    {
        RZFont f = *this;
        f.name = newName;
        f.nameCRC = BUCRC().Generate(newName);
        return f;
    }

    RZFont WithAtlas(const std::string& textureName) const
    {
        RZFont f = *this;
        f.textureCRC = BUCRC().Generate(textureName);
        return f;
    }

    CMChunk BuildLibraryResource(bool littleEndian = false) const
    {
        CMChunk res = CMChunk::Container(GenSub_Resource, 1);
        res.isLittleEndian = littleEndian;
        res.AddChild(CMChunkResourceHeader::Build(nameCRC, 7, languageMask, 0, 0, 0xFFFFFFFFu, name, littleEndian));

        CMChunk header = CMChunk::Container(Font_Header, static_cast<uint16_t>(version));
        header.isLittleEndian = littleEndian;

        CMChunk info = CMChunk::Leaf(Font_Info, static_cast<uint16_t>(infoVersion), {});
        info.isLittleEndian = littleEndian;
        info.Append<uint32_t>(static_cast<uint32_t>(glyphs.size()));
        info.Append<uint32_t>(info1);
        info.Append<float>(info2);
        info.Append<float>(info3);
        info.Append<float>(info4);
        info.Append<uint32_t>(info5);
        info.Append<uint32_t>(info6);
        info.Append<uint32_t>(info7);
        info.Append<uint32_t>(textureCRC);
        header.AddChild(std::move(info));

        CMChunk data = CMChunk::Leaf(Font_Data, static_cast<uint16_t>(dataVersion), {});
        data.isLittleEndian = littleEndian;
        for (const Glyph& g : glyphs)
        {
            data.Append<uint32_t>(g.flags);
            data.Append<float>(g.u0);
            data.Append<float>(g.v0);
            data.Append<float>(g.u1);
            data.Append<float>(g.v1);
            data.Append<uint32_t>(g.codePoint);
            data.Append<float>(g.advance);
            data.Append<float>(g.height);
            data.Append<float>(g.width);
        }
        header.AddChild(std::move(data));

        res.AddChild(std::move(header));
        return res;
    }

    static CMChunk BuildLibrary(const std::vector<RZFont>& fonts, bool littleEndian = false)
    {
        CMChunk lib = CMChunk::Container(Gen_FontLibrary, 4);
        lib.isLittleEndian = littleEndian;
        CMChunk info = CMChunk::Leaf(GenSub_Info, 1, {});
        info.isLittleEndian = littleEndian;
        info.Append<uint32_t>(static_cast<uint32_t>(fonts.size()));
        lib.AddChild(std::move(info));
        for (const RZFont& f : fonts)
            lib.AddChild(f.BuildLibraryResource(littleEndian));
        return lib;
    }

    const Glyph* Find(uint32_t codePoint) const
    {
        for (const Glyph& g : glyphs)
            if (g.codePoint == codePoint)
                return &g;
        return nullptr;
    }
};
