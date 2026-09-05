#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "../BaseUtils/BUCRC.h"
#include "../Package/CMChunk.h"
#include "ResourceHeader.h"

class RZStringTable
{
public:
    struct SubString
    {
        uint32_t offsetU16 = 0;
        float startTime = 0.0f;
        float endTime = -1.0f;
        std::u16string text;
    };
    struct Entry
    {
        uint32_t nameCRC = 0;
        std::vector<SubString> subs;
        const std::u16string& Text() const
        {
            static const std::u16string empty;
            return subs.empty() ? empty : subs.front().text;
        }
    };

    static constexpr uint32_t kResourceTypeStringTable = 0x82;

    std::string name;
    uint32_t nameCRC = 0;
    uint32_t languageMask = 0;
    std::vector<Entry> entries;

    static RZStringTable Parse(const CMChunk& table, const CMChunkResourceHeader* header = nullptr)
    {
        if (table.GetMaskedID() != StringTable || !table.GetHasChildren())
            throw std::runtime_error("RZStringTable: not a StringTable container");
        const CMChunk* hdr = table.FindChild(StringTable_Header);
        const CMChunk* dat = table.FindChild(StringTable_Data);
        if (!hdr || !dat)
            throw std::runtime_error("RZStringTable: header or data chunk missing");

        RZStringTable t;
        if (header)
        {
            t.name = header->GetName();
            t.nameCRC = header->GetCRC();
            t.languageMask = header->GetLanguageMask();
        }

        size_t off = 0;
        const uint32_t count = hdr->Read<uint32_t>(off);
        std::vector<uint32_t> crcs(count);
        for (uint32_t i = 0; i < count; ++i)
            crcs[i] = hdr->Read<uint32_t>(off);
        const uint32_t totalSubs = hdr->Read<uint32_t>(off);

        uint32_t subsSeen = 0;
        t.entries.resize(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            Entry& e = t.entries[i];
            e.nameCRC = crcs[i];
            const uint32_t subCount = hdr->Read<uint32_t>(off);
            subsSeen += subCount;
            e.subs.resize(subCount);
            for (uint32_t s = 0; s < subCount; ++s)
            {
                e.subs[s].offsetU16 = hdr->Read<uint32_t>(off);
                e.subs[s].startTime = hdr->Read<float>(off);
                e.subs[s].endTime = hdr->Read<float>(off);
                e.subs[s].text = ReadText(*dat, e.subs[s].offsetU16);
            }
        }
        if (subsSeen != totalSubs)
            throw std::runtime_error("RZStringTable: sub-string total " + std::to_string(totalSubs) +
                                     " does not match the records (" + std::to_string(subsSeen) + ")");
        if (off != hdr->data.size())
            throw std::runtime_error("RZStringTable: header has trailing bytes");
        return t;
    }

    static std::vector<RZStringTable> ParseLibrary(const CMChunk& library)
    {
        std::vector<RZStringTable> out;
        for (const CMChunk* res : library.FindChildren(GenSub_Resource))
        {
            const CMChunk* h = res->FindChild(GenSub_ResourceHeader);
            const CMChunk* tbl = res->FindChild(StringTable);
            if (!tbl)
                continue;
            if (h)
            {
                CMChunkResourceHeader header(*h);
                out.push_back(Parse(*tbl, &header));
            }
            else
            {
                out.push_back(Parse(*tbl));
            }
        }
        return out;
    }

    Entry& Add(const std::string& exportName, const std::u16string& text, float start = 0.0f, float end = -1.0f)
    {
        Entry e;
        e.nameCRC = BUCRC().Generate(exportName);
        SubString s;
        s.text = text;
        s.startTime = start;
        s.endTime = end;
        e.subs.push_back(std::move(s));
        entries.push_back(std::move(e));
        return entries.back();
    }

    CMChunk BuildTable(bool littleEndian = false, bool wideLength = false) const
    {
        CMChunk header = CMChunk::Leaf(StringTable_Header, 4, {}, wideLength);
        CMChunk data = CMChunk::Leaf(StringTable_Data, 2, {}, wideLength);
        header.isLittleEndian = data.isLittleEndian = littleEndian;

        std::vector<std::vector<uint32_t>> offsets(entries.size());
        for (size_t i = 0; i < entries.size(); ++i)
        {
            for (const SubString& s : entries[i].subs)
            {
                offsets[i].push_back(static_cast<uint32_t>(data.data.size() / 2));
                for (char16_t ch : s.text)
                    data.Append<uint16_t>(static_cast<uint16_t>(ch));
                data.Append<uint16_t>(0);
            }
        }

        uint32_t totalSubs = 0;
        for (const Entry& e : entries)
            totalSubs += static_cast<uint32_t>(e.subs.size());

        header.Append<uint32_t>(static_cast<uint32_t>(entries.size()));
        for (const Entry& e : entries)
            header.Append<uint32_t>(e.nameCRC);
        header.Append<uint32_t>(totalSubs);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            header.Append<uint32_t>(static_cast<uint32_t>(entries[i].subs.size()));
            for (size_t s = 0; s < entries[i].subs.size(); ++s)
            {
                header.Append<uint32_t>(offsets[i][s]);
                header.Append<float>(entries[i].subs[s].startTime);
                header.Append<float>(entries[i].subs[s].endTime);
            }
        }

        CMChunk table = CMChunk::Container(StringTable, 4, wideLength);
        table.isLittleEndian = littleEndian;
        table.AddChild(std::move(header));
        table.AddChild(std::move(data));
        return table;
    }

    CMChunk BuildResource(bool littleEndian = false, bool wideLength = false) const
    {
        CMChunk res = CMChunk::Container(GenSub_Resource, 1, wideLength);
        res.isLittleEndian = littleEndian;
        const uint32_t crc = nameCRC ? nameCRC : BUCRC().Generate(name);
        res.AddChild(CMChunkResourceHeader::Build(crc, kResourceTypeStringTable, languageMask, 0, 0, 0xFFFFFFFFu,
                                                  name, littleEndian, wideLength));
        res.AddChild(BuildTable(littleEndian, wideLength));
        return res;
    }

    static CMChunk BuildLibrary(const std::vector<RZStringTable>& tables, bool littleEndian = false,
                                bool wideLength = false)
    {
        CMChunk lib = CMChunk::Container(Gen_StringTableLibrary, 4, wideLength);
        lib.isLittleEndian = littleEndian;
        CMChunk info = CMChunk::Leaf(GenSub_Info, 1, {}, wideLength);
        info.isLittleEndian = littleEndian;
        info.Append<uint32_t>(static_cast<uint32_t>(tables.size()));
        lib.AddChild(std::move(info));
        for (const RZStringTable& t : tables)
            lib.AddChild(t.BuildResource(littleEndian, wideLength));
        return lib;
    }

    static std::u16string ReadText(const CMChunk& data, uint32_t offsetU16)
    {
        std::u16string s;
        size_t off = size_t(offsetU16) * 2;
        while (off + 2 <= data.data.size())
        {
            const uint16_t ch = data.Read<uint16_t>(off);
            if (!ch)
                break;
            s.push_back(static_cast<char16_t>(ch));
        }
        return s;
    }

    static std::u16string FromUtf8(const std::string& utf8)
    {
        std::u16string out;
        for (size_t i = 0; i < utf8.size();)
        {
            const unsigned char c = static_cast<unsigned char>(utf8[i]);
            uint32_t cp;
            size_t n;
            if (c < 0x80) { cp = c; n = 1; }
            else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
            else { cp = 0xFFFD; n = 1; }
            for (size_t k = 1; k < n && i + k < utf8.size(); ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
            i += n;
            if (cp >= 0x10000)
            {
                cp -= 0x10000;
                out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
            }
            else
            {
                out.push_back(static_cast<char16_t>(cp));
            }
        }
        return out;
    }

    static std::string ToUtf8(const std::u16string& s)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i)
        {
            uint32_t cp = s[i];
            if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size())
                cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++i] - 0xDC00);
            if (cp < 0x80) out.push_back(static_cast<char>(cp));
            else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
            else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
            else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        }
        return out;
    }
};
