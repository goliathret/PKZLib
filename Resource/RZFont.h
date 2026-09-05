#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

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
        }
        const CMChunk* fh = resource.FindChild(Font_Header);
        if (!fh)
            throw std::runtime_error("RZFont: no Font_Header in " + f.name);
        const CMChunk* info = fh->FindChild(Font_Info);
        const CMChunk* data = fh->FindChild(Font_Data);
        if (!info || !data)
            throw std::runtime_error("RZFont: Font_Info or Font_Data missing in " + f.name);
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

    const Glyph* Find(uint32_t codePoint) const
    {
        for (const Glyph& g : glyphs)
            if (g.codePoint == codePoint)
                return &g;
        return nullptr;
    }
};
