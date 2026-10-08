#pragma once
//
// Lightweight field-schema loader for Goliath chunk dumps.
// Reads the goliathChunks XML format (see schemas/SD-PC.xml).
// Used by pkztool:  pkztool --le -g SD-PC fields package.pak
//

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "CMChunk.h"

namespace pkfields
{

enum class FieldType
{
    U8, U16, U32, S16, S32, F32,
    CharN,   // fixed char[N]
    CString,
    Bytes,
    Unknown
};

struct Field
{
    std::string name;
    std::string offset;     // "0", "0x10", or "next"
    FieldType type = FieldType::Unknown;
    int typeCount = 1;      // e.g. f32[4] -> 4, char[64] -> 64
    std::string countExpr;  // empty | "rest" | "@field" | integer
    std::string enumName;
    std::string flagsName;
    std::string ifCond;
};

struct ChunkSchema
{
    uint32_t id = 0;
    std::string name;
    std::vector<int> versions;  // empty = any
    std::vector<Field> fields;
};

struct SchemaSet
{
    std::string name;
    std::string endian = "little";
    std::map<uint32_t, ChunkSchema> chunks;          // id -> schema
    std::map<std::string, std::map<uint32_t, std::string>> enums;  // name -> (value -> label)
    std::map<std::string, std::map<uint32_t, std::string>> flagSets;

    const ChunkSchema* Find(uint32_t id) const
    {
        auto it = chunks.find(id);
        return it == chunks.end() ? nullptr : &it->second;
    }

    std::string ChunkName(uint32_t id) const
    {
        if (const ChunkSchema* s = Find(id))
            return s->name;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "0x%04X", id);
        return buf;
    }
};

// ---------------------------------------------------------------------------
// Tiny attribute / tag helpers (no external XML library)
// ---------------------------------------------------------------------------

inline std::string Attr(const std::string& tag, const char* key)
{
    std::string needle = std::string(key) + "=\"";
    auto pos = tag.find(needle);
    if (pos == std::string::npos)
        return {};
    pos += needle.size();
    auto end = tag.find('"', pos);
    if (end == std::string::npos)
        return {};
    return tag.substr(pos, end - pos);
}

inline uint32_t ParseU32(const std::string& s, uint32_t def = 0)
{
    if (s.empty())
        return def;
    return static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 0));
}

inline FieldType ParseType(const std::string& t, int& countOut)
{
    countOut = 1;
    if (t == "u8") return FieldType::U8;
    if (t == "u16") return FieldType::U16;
    if (t == "u32") return FieldType::U32;
    if (t == "s16") return FieldType::S16;
    if (t == "s32") return FieldType::S32;
    if (t == "f32") return FieldType::F32;
    if (t == "cstring") return FieldType::CString;
    if (t == "bytes") return FieldType::Bytes;
    if (t == "char") return FieldType::CharN;
    // f32[4], u32[7], char[64], u8[7] ...
    auto lb = t.find('[');
    auto rb = t.find(']');
    if (lb != std::string::npos && rb != std::string::npos && rb > lb)
    {
        countOut = static_cast<int>(std::strtol(t.c_str() + lb + 1, nullptr, 10));
        std::string base = t.substr(0, lb);
        if (base == "f32") return FieldType::F32;
        if (base == "u32") return FieldType::U32;
        if (base == "u16") return FieldType::U16;
        if (base == "u8") return FieldType::U8;
        if (base == "char") return FieldType::CharN;
        if (base == "s16") return FieldType::S16;
    }
    return FieldType::Unknown;
}

inline int ElemSize(FieldType t, int typeCount)
{
    switch (t)
    {
    case FieldType::U8:
    case FieldType::CharN: return typeCount;
    case FieldType::U16:
    case FieldType::S16: return 2 * typeCount;
    case FieldType::U32:
    case FieldType::S32:
    case FieldType::F32: return 4 * typeCount;
    default: return 1;
    }
}

// ---------------------------------------------------------------------------
// Load goliathChunks XML
// ---------------------------------------------------------------------------

inline bool LoadSchemaFile(const std::string& path, SchemaSet& out, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        error = "cannot open schema: " + path;
        return false;
    }
    std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // root attrs
    auto rootPos = xml.find("<goliathChunks");
    if (rootPos == std::string::npos)
    {
        // also accept <pkz-fields> name-only (no field body required)
        rootPos = xml.find("<pkz-fields");
        if (rootPos == std::string::npos)
        {
            error = "not a goliathChunks / pkz-fields schema: " + path;
            return false;
        }
    }
    auto rootEnd = xml.find('>', rootPos);
    std::string rootTag = xml.substr(rootPos, rootEnd - rootPos + 1);
    out.name = Attr(rootTag, "game");
    if (out.name.empty())
        out.name = Attr(rootTag, "name");
    std::string endian = Attr(rootTag, "endian");
    if (!endian.empty())
        out.endian = endian;

    // ---- enums ----
    size_t pos = 0;
    while (true)
    {
        auto e0 = xml.find("<enum ", pos);
        if (e0 == std::string::npos)
            break;
        auto e1 = xml.find('>', e0);
        std::string etag = xml.substr(e0, e1 - e0 + 1);
        std::string ename = Attr(etag, "name");
        auto close = xml.find("</enum>", e1);
        if (close == std::string::npos)
            break;
        std::string body = xml.substr(e1 + 1, close - e1 - 1);
        size_t vp = 0;
        while (true)
        {
            auto v0 = body.find("<value ", vp);
            if (v0 == std::string::npos)
                break;
            auto v1 = body.find("/>", v0);
            if (v1 == std::string::npos)
                v1 = body.find('>', v0);
            std::string vtag = body.substr(v0, v1 - v0 + 2);
            uint32_t key = ParseU32(Attr(vtag, "value"));
            std::string label = Attr(vtag, "name");
            out.enums[ename][key] = label;
            vp = v1 + 1;
        }
        pos = close + 7;
    }

    // ---- flagSets (store bit names by mask) ----
    pos = 0;
    while (true)
    {
        auto e0 = xml.find("<flagSet ", pos);
        if (e0 == std::string::npos)
            break;
        auto e1 = xml.find('>', e0);
        std::string etag = xml.substr(e0, e1 - e0 + 1);
        std::string fname = Attr(etag, "name");
        auto close = xml.find("</flagSet>", e1);
        if (close == std::string::npos)
            break;
        std::string body = xml.substr(e1 + 1, close - e1 - 1);
        size_t bp = 0;
        while (true)
        {
            auto b0 = body.find("<bit ", bp);
            if (b0 == std::string::npos)
                break;
            auto b1 = body.find("/>", b0);
            if (b1 == std::string::npos)
                b1 = body.find('>', b0);
            std::string btag = body.substr(b0, b1 - b0 + 2);
            uint32_t mask = ParseU32(Attr(btag, "mask"));
            out.flagSets[fname][mask] = Attr(btag, "name");
            bp = b1 + 1;
        }
        // also <value value= name=>
        bp = 0;
        while (true)
        {
            auto v0 = body.find("<value ", bp);
            if (v0 == std::string::npos)
                break;
            auto v1 = body.find("/>", v0);
            if (v1 == std::string::npos)
                v1 = body.find('>', v0);
            std::string vtag = body.substr(v0, v1 - v0 + 2);
            uint32_t key = ParseU32(Attr(vtag, "value"));
            out.flagSets[fname][key] = Attr(vtag, "name");
            bp = v1 + 1;
        }
        pos = close + 10;
    }

    // ---- chunks ----
    pos = 0;
    while (true)
    {
        auto c0 = xml.find("<chunk ", pos);
        if (c0 == std::string::npos)
            break;
        auto c1 = xml.find('>', c0);
        std::string ctag = xml.substr(c0, c1 - c0 + 1);
        ChunkSchema cs;
        cs.id = ParseU32(Attr(ctag, "id"));
        cs.name = Attr(ctag, "name");
        std::string ver = Attr(ctag, "version");
        if (!ver.empty())
        {
            // "3" or "3,5"
            std::stringstream ss(ver);
            std::string part;
            while (std::getline(ss, part, ','))
                cs.versions.push_back(static_cast<int>(std::strtol(part.c_str(), nullptr, 10)));
        }

        bool selfClose = ctag.size() >= 2 && ctag[ctag.size() - 2] == '/';
        if (!selfClose)
        {
            auto close = xml.find("</chunk>", c1);
            if (close == std::string::npos)
                break;
            std::string body = xml.substr(c1 + 1, close - c1 - 1);
            size_t fp = 0;
            while (true)
            {
                auto f0 = body.find("<field ", fp);
                if (f0 == std::string::npos)
                    break;
                auto f1 = body.find("/>", f0);
                if (f1 == std::string::npos)
                    f1 = body.find('>', f0);
                std::string ftag = body.substr(f0, f1 - f0 + 2);
                Field f;
                f.name = Attr(ftag, "name");
                f.offset = Attr(ftag, "offset");
                if (f.offset.empty())
                    f.offset = "next";
                int tc = 1;
                f.type = ParseType(Attr(ftag, "type"), tc);
                f.typeCount = tc;
                f.countExpr = Attr(ftag, "count");
                f.enumName = Attr(ftag, "enum");
                f.flagsName = Attr(ftag, "flags");
                f.ifCond = Attr(ftag, "if");
                // skip struct= / pointsTo= fields for now (too dynamic)
                if (Attr(ftag, "struct").empty() && Attr(ftag, "pointsTo").empty())
                    cs.fields.push_back(std::move(f));
                fp = f1 + 1;
            }
            pos = close + 8;
        }
        else
        {
            pos = c1 + 1;
        }

        if (cs.id != 0)
            out.chunks[cs.id] = std::move(cs);
    }

    if (out.chunks.empty())
    {
        error = "no <chunk> entries found in " + path;
        return false;
    }
    return true;
}

// Resolve -g name to a schema file path relative to the executable / cwd / schemas/
inline std::string ResolveGameSchema(const std::string& game)
{
    std::string key = game;
    for (char& c : key)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::replace(key.begin(), key.end(), '_', '-');

    std::string file;
    if (key == "sd-pc" || key == "sd" || key == "shattered-dimensions" ||
        key == "shattered-dimensions-pc" || key == "spider-man-shattered-dimensions" ||
        key == "spider-man-shattered-dimensions-pc")
        file = "SD-PC.xml";
    else if (key == "eot" || key == "edge-of-time")
        file = "EOT-360.xml";
    else
        file = game + ".xml";  // allow arbitrary -g Foo -> schemas/Foo.xml

    // search order
    const char* prefixes[] = {
        "schemas/",
        "../schemas/",
        "../../schemas/",
        "./",
    };
    for (const char* p : prefixes)
    {
        std::string path = std::string(p) + file;
        std::ifstream test(path);
        if (test)
            return path;
    }
    return std::string("schemas/") + file;  // default (will fail load with clear error)
}

// ---------------------------------------------------------------------------
// Binary reader helpers
// ---------------------------------------------------------------------------

struct Cursor
{
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    bool le = true;

    bool Ok(size_t n) const { return pos + n <= size; }

    uint8_t U8()
    {
        if (!Ok(1))
            return 0;
        return data[pos++];
    }
    uint16_t U16()
    {
        if (!Ok(2))
            return 0;
        uint16_t v;
        std::memcpy(&v, data + pos, 2);
        pos += 2;
        if (!le)
            v = static_cast<uint16_t>((v >> 8) | (v << 8));
        return v;
    }
    int16_t S16() { return static_cast<int16_t>(U16()); }
    uint32_t U32()
    {
        if (!Ok(4))
            return 0;
        uint32_t v;
        std::memcpy(&v, data + pos, 4);
        pos += 4;
        if (!le)
            v = ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v & 0xFF0000) >> 8) | ((v >> 24) & 0xFF);
        return v;
    }
    int32_t S32() { return static_cast<int32_t>(U32()); }
    float F32()
    {
        uint32_t u = U32();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    std::string CharN(int n)
    {
        if (!Ok(static_cast<size_t>(n)))
            n = static_cast<int>(size - pos);
        std::string s(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(n));
        pos += static_cast<size_t>(n);
        auto z = s.find('\0');
        if (z != std::string::npos)
            s.resize(z);
        return s;
    }
    std::string CString()
    {
        size_t start = pos;
        while (pos < size && data[pos] != 0)
            ++pos;
        std::string s(reinterpret_cast<const char*>(data + start), pos - start);
        if (pos < size)
            ++pos;
        return s;
    }
};

inline std::string FormatEnum(const SchemaSet& schema, const std::string& enumName, uint32_t v)
{
    auto it = schema.enums.find(enumName);
    if (it == schema.enums.end())
        return {};
    auto jt = it->second.find(v);
    if (jt == it->second.end())
        return {};
    return jt->second;
}

inline std::string FormatFlags(const SchemaSet& schema, const std::string& flagsName, uint32_t v)
{
    auto it = schema.flagSets.find(flagsName);
    if (it == schema.flagSets.end())
        return {};
    std::string out;
    uint32_t left = v;
    // exact value first
    auto exact = it->second.find(v);
    if (exact != it->second.end() && exact->first == v && exact->first != 0)
        return exact->second;
    for (const auto& kv : it->second)
    {
        if (kv.first == 0)
            continue;
        // treat as bit mask if power-of-two-ish or exact match on bits
        if ((v & kv.first) == kv.first)
        {
            if (!out.empty())
                out += " | ";
            out += kv.second;
            left &= ~kv.first;
        }
    }
    if (left && !out.empty())
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), " | 0x%X", left);
        out += buf;
    }
    return out;
}

inline int EvalCount(const Field& f, const std::map<std::string, uint32_t>& ctx, size_t remaining, int elemSize)
{
    if (f.countExpr.empty())
        return 1;
    if (f.countExpr == "rest")
    {
        if (elemSize <= 0)
            return static_cast<int>(remaining);
        return static_cast<int>(remaining / static_cast<size_t>(elemSize));
    }
    if (f.countExpr[0] == '@')
    {
        std::string key = f.countExpr.substr(1);
        auto it = ctx.find(key);
        if (it != ctx.end())
            return static_cast<int>(it->second);
        return 0;
    }
    return static_cast<int>(std::strtol(f.countExpr.c_str(), nullptr, 0));
}

inline bool EvalIf(const std::string& cond, const std::map<std::string, uint32_t>& ctx)
{
    if (cond.empty())
        return true;
    // flags&0x1
    auto amp = cond.find('&');
    if (amp != std::string::npos)
    {
        std::string left = cond.substr(0, amp);
        uint32_t mask = ParseU32(cond.substr(amp + 1));
        auto it = ctx.find(left);
        uint32_t val = it == ctx.end() ? 0u : it->second;
        return (val & mask) != 0;
    }
    auto eq = cond.find("==");
    if (eq != std::string::npos)
    {
        std::string left = cond.substr(0, eq);
        uint32_t rhs = ParseU32(cond.substr(eq + 2));
        auto it = ctx.find(left);
        uint32_t val = it == ctx.end() ? 0u : it->second;
        return val == rhs;
    }
    return true;
}

// Decode and print one leaf chunk's fields
inline void DumpFields(const CMChunk& chunk, const SchemaSet& schema, int indent)
{
    const ChunkSchema* cs = schema.Find(chunk.GetMaskedID());
    if (!cs || cs->fields.empty())
        return;

    Cursor cur;
    cur.data = chunk.data.data();
    cur.size = chunk.data.size();
    cur.pos = 0;
    cur.le = chunk.isLittleEndian || schema.endian != "big";

    std::map<std::string, uint32_t> ctx;
    ctx["version"] = chunk.GetVersion();

    const std::string pad(static_cast<size_t>(indent) * 2, ' ');

    for (const Field& f : cs->fields)
    {
        if (!EvalIf(f.ifCond, ctx))
            continue;

        // absolute offset support
        if (f.offset != "next" && !f.offset.empty())
        {
            size_t abs = static_cast<size_t>(ParseU32(f.offset));
            if (abs < cur.size)
                cur.pos = abs;
        }

        int elem = ElemSize(f.type, f.typeCount);
        int count = EvalCount(f, ctx, cur.size - cur.pos, elem);
        if (count < 0)
            count = 0;
        if (count > 256)
            count = 256;  // display limit

        std::printf("%s%s = ", pad.c_str(), f.name.c_str());

        if (f.type == FieldType::Unknown)
        {
            std::printf("<unsupported type>\n");
            continue;
        }

        if (count != 1 && f.type != FieldType::CharN)
            std::printf("[");

        for (int i = 0; i < count; ++i)
        {
            if (i && f.type != FieldType::CharN)
                std::printf(", ");

            switch (f.type)
            {
            case FieldType::U8:
            {
                uint32_t v = cur.U8();
                if (i == 0)
                    ctx[f.name] = v;
                std::printf("%u", v);
                break;
            }
            case FieldType::U16:
            {
                uint32_t v = cur.U16();
                if (i == 0)
                    ctx[f.name] = v;
                std::printf("%u", v);
                break;
            }
            case FieldType::S16:
            {
                int32_t v = cur.S16();
                if (i == 0)
                    ctx[f.name] = static_cast<uint32_t>(v);
                std::printf("%d", v);
                break;
            }
            case FieldType::U32:
            {
                uint32_t v = cur.U32();
                if (i == 0)
                    ctx[f.name] = v;
                if (!f.enumName.empty())
                {
                    std::string lab = FormatEnum(schema, f.enumName, v);
                    if (!lab.empty())
                        std::printf("%u (%s)", v, lab.c_str());
                    else
                        std::printf("%u (0x%X)", v, v);
                }
                else if (!f.flagsName.empty())
                {
                    std::string lab = FormatFlags(schema, f.flagsName, v);
                    if (!lab.empty())
                        std::printf("0x%X (%s)", v, lab.c_str());
                    else
                        std::printf("0x%X", v);
                }
                else if (v > 9)
                    std::printf("%u (0x%X)", v, v);
                else
                    std::printf("%u", v);
                break;
            }
            case FieldType::S32:
            {
                int32_t v = cur.S32();
                if (i == 0)
                    ctx[f.name] = static_cast<uint32_t>(v);
                std::printf("%d", v);
                break;
            }
            case FieldType::F32:
            {
                // typeCount may be >1 for f32[3] as a single logical field
                if (f.typeCount > 1 && count == 1)
                {
                    std::printf("[");
                    for (int k = 0; k < f.typeCount; ++k)
                    {
                        if (k)
                            std::printf(", ");
                        std::printf("%g", cur.F32());
                    }
                    std::printf("]");
                }
                else
                    std::printf("%g", cur.F32());
                break;
            }
            case FieldType::CharN:
            {
                std::string s = cur.CharN(f.typeCount > 0 ? f.typeCount : 1);
                std::printf("'%s'", s.c_str());
                break;
            }
            case FieldType::CString:
            {
                std::string s = cur.CString();
                std::printf("'%s'", s.c_str());
                break;
            }
            case FieldType::Bytes:
            {
                size_t n = static_cast<size_t>(count);
                if (n > 16)
                    n = 16;
                for (size_t k = 0; k < n; ++k)
                    std::printf("%02X", cur.U8());
                if (static_cast<size_t>(count) > 16)
                {
                    cur.pos += static_cast<size_t>(count) - 16;
                    std::printf("... (%d bytes)", count);
                }
                break;
            }
            default:
                break;
            }
        }

        if (count != 1 && f.type != FieldType::CharN)
            std::printf("]");
        std::printf("\n");
    }
}

// Walk package and print named tree + fields for leaves
inline void DumpTree(const CMChunk& chunk, const SchemaSet& schema, int depth, int maxDepth,
                     const std::string& filter)
{
    if (depth > maxDepth)
        return;

    const std::string pad(static_cast<size_t>(depth) * 2, ' ');
    std::string name = schema.ChunkName(chunk.GetMaskedID());
    // fallback to built-in enum name
    if (name.size() > 2 && name[0] == '0' && name[1] == 'x')
        name = ToString(chunk.GetIDToEnum());

    bool matchFilter = filter.empty() ||
                       name.find(filter) != std::string::npos ||
                       filter == name;

    if (matchFilter || chunk.GetHasChildren())
    {
        std::printf("%s%s  id=0x%X  ver=%u  size=%llu%s  @0x%llX\n",
                    pad.c_str(),
                    name.c_str(),
                    chunk.GetMaskedID(),
                    chunk.GetVersion(),
                    static_cast<unsigned long long>(chunk.GetLength()),
                    chunk.GetHasChildren() ? "  [CHILDREN]" : "",
                    static_cast<unsigned long long>(chunk.offset));

        if (!chunk.GetHasChildren() && matchFilter)
            DumpFields(chunk, schema, depth + 1);
    }

    if (chunk.GetHasChildren())
    {
        for (const CMChunk& child : chunk.children)
            DumpTree(child, schema, depth + 1, maxDepth, filter);
    }
}

}  // namespace pkfields
