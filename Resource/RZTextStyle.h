#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "../BaseUtils/BUCRC.h"
#include "../Package/CMChunk.h"
#include "../Package/PKPackage.h"
#include "ResourceHeader.h"

class RZTextStyle
{
public:
    std::string name;
    uint32_t nameCRC = 0;
    uint32_t fontCRC = 0;
    std::vector<uint8_t> info;
    uint16_t infoVersion = 3;

    static RZTextStyle Parse(const CMChunk& resource)
    {
        RZTextStyle s;
        if (const CMChunk* h = resource.FindChild(GenSub_ResourceHeader))
        {
            CMChunkResourceHeader header(*h);
            s.name = header.GetName();
            s.nameCRC = header.GetCRC();
        }
        const CMChunk* th = resource.FindChild(TextStyle_Header);
        const CMChunk* ti = th ? th->FindChild(TextStyle_Info) : nullptr;
        if (!ti || ti->data.size() < 8)
            throw std::runtime_error("RZTextStyle: no TextStyle_Info in " + s.name);
        s.info = ti->data;
        s.infoVersion = ti->version;
        size_t off = 4;
        s.fontCRC = ti->Read<uint32_t>(off);
        return s;
    }

    static std::vector<RZTextStyle> All(const PKPackage& pkg)
    {
        std::vector<RZTextStyle> out;
        for (const CMChunk& root : pkg.rootChunks)
            if (root.GetMaskedID() == Root)
                for (const CMChunk* lib : root.FindChildren(Gen_TextStyleLibrary))
                    for (const CMChunk* res : lib->FindChildren(GenSub_Resource))
                        out.push_back(Parse(*res));
        return out;
    }

    RZTextStyle Renamed(const std::string& newName) const
    {
        RZTextStyle s = *this;
        s.name = newName;
        s.nameCRC = BUCRC().Generate(newName);
        return s;
    }

    CMChunk Build(bool littleEndian = false) const
    {
        CMChunk res = CMChunk::Container(GenSub_Resource, 1);
        res.isLittleEndian = littleEndian;
        res.AddChild(CMChunkResourceHeader::Build(nameCRC, 25, 0x1F, 0, 0, 0xFFFFFFFFu, name, littleEndian));
        CMChunk refs = CMChunk::Leaf(GenSub_RefResourceArray, 1, {});
        refs.isLittleEndian = littleEndian;
        refs.Append<uint32_t>(7);
        refs.Append<uint32_t>(fontCRC);
        res.AddChild(std::move(refs));
        CMChunk th = CMChunk::Container(TextStyle_Header, 1);
        th.isLittleEndian = littleEndian;
        CMChunk ti = CMChunk::Leaf(TextStyle_Info, infoVersion, info);
        ti.isLittleEndian = littleEndian;
        th.AddChild(std::move(ti));
        res.AddChild(std::move(th));
        return res;
    }

    static CMChunk BuildLibrary(const std::vector<RZTextStyle>& styles, bool littleEndian = false)
    {
        CMChunk lib = CMChunk::Container(Gen_TextStyleLibrary, 2);
        lib.isLittleEndian = littleEndian;
        CMChunk info = CMChunk::Leaf(GenSub_Info, 1, {});
        info.isLittleEndian = littleEndian;
        info.Append<uint32_t>(static_cast<uint32_t>(styles.size()));
        lib.AddChild(std::move(info));
        for (const RZTextStyle& s : styles)
            lib.AddChild(s.Build(littleEndian));
        return lib;
    }
};
