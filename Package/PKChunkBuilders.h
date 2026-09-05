#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "CMChunk.h"

namespace PKChunkBuilders
{

    inline CMChunk PackageHeader(uint32_t packageId, const std::string& nameA, const std::string& nameB = "",
                                 bool littleEndian = false, bool wideLength = false)
    {
        CMChunk c = CMChunk::Leaf(Gen_LevelHeader, 3, {}, wideLength);
        c.isLittleEndian = littleEndian;
        c.Append<uint32_t>(packageId);
        c.Append<uint16_t>(2);
        c.Append<uint16_t>(0);
        char a[128] = {};
        char b[128] = {};
        std::strncpy(a, nameA.c_str(), sizeof(a) - 1);
        std::strncpy(b, nameB.c_str(), sizeof(b) - 1);
        c.AppendBytes(a, sizeof(a));
        c.AppendBytes(b, sizeof(b));
        return c;
    }

    inline CMChunk Languages(const std::vector<uint32_t>& languages, bool littleEndian = false,
                             bool wideLength = false)
    {
        CMChunk c = CMChunk::Leaf(Gen_Languages, 1, {}, wideLength);
        c.isLittleEndian = littleEndian;
        for (uint32_t l : languages)
            c.Append<uint32_t>(l);
        return c;
    }

    inline CMChunk ExportContext(const std::string& quality, const std::string& platform, const std::string& region,
                                 bool littleEndian = false, bool wideLength = false)
    {
        CMChunk c = CMChunk::Leaf(Gen_ExportContext, 2, {}, wideLength);
        c.isLittleEndian = littleEndian;
        for (const std::string* s : { &quality, &platform, &region })
            c.AppendBytes(s->c_str(), s->size() + 1);
        return c;
    }

    inline CMChunk LodDistances(float d0, float d1, float d2, float d3, bool littleEndian = false,
                                bool wideLength = false)
    {
        CMChunk c = CMChunk::Leaf(Gen_LODDistances, 1, {}, wideLength);
        c.isLittleEndian = littleEndian;
        c.Append<float>(d0);
        c.Append<float>(d1);
        c.Append<float>(d2);
        c.Append<float>(d3);
        return c;
    }

    inline CMChunk EmptyLibrary(uint32_t libraryType, uint16_t version, bool littleEndian = false,
                                bool wideLength = false)
    {
        CMChunk lib = CMChunk::Container(libraryType, version, wideLength);
        lib.isLittleEndian = littleEndian;
        CMChunk info = CMChunk::Leaf(GenSub_Info, 1, {}, wideLength);
        info.isLittleEndian = littleEndian;
        info.Append<uint32_t>(0);
        lib.AddChild(std::move(info));
        return lib;
    }

    inline CMChunk Root(bool littleEndian = false, bool wideLength = false)
    {
        CMChunk root = CMChunk::Container(::Root, 1, wideLength);
        root.isLittleEndian = littleEndian;
        return root;
    }
}
