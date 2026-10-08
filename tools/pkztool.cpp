#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../BaseUtils/BUDecompress.h"
#include "../Package/CMChunk.h"
#include "../Package/CMChunkTypes.h"
#include "../Package/PKChunkBuilders.h"
#include "../Package/PKPackage.h"
#include "../BaseUtils/BUPng.h"
#include "../Resource/RZStringTable.h"
#include "../Resource/RZTexture.h"
#include "../Resource/ResourceHeader.h"
#include "pkztool_dump.h"
#include "pkztool_make.h"
#include "../Package/PKFieldSchema.h"

namespace
{
    bool gLittleEndian = false;
    std::string gGameSchema;  // e.g. "SD-PC" -> schemas/SD-PC.xml

    std::vector<uint8_t> ReadFile(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            throw std::runtime_error("cannot open " + path);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    void WriteFile(const std::string& path, const std::vector<uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        if (!f)
            throw std::runtime_error("cannot write " + path);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void PrintTree(const CMChunk& c, int depth, int maxDepth)
    {
        std::printf("%*s%08llx %-34s id 0x%06X v%u %s len %llu\n", depth * 2, "",
                    static_cast<unsigned long long>(c.offset), ToString(c.GetIDToEnum()).c_str(), c.GetMaskedID(),
                    c.GetVersion(), c.GetHasChildren() ? "container" : "leaf     ",
                    static_cast<unsigned long long>(c.GetLength()));
        if (depth >= maxDepth)
            return;
        for (const auto& child : c.children)
            PrintTree(child, depth + 1, maxDepth);
    }

    int Info(const std::string& path)
    {
        const std::vector<uint8_t> bytes = ReadFile(path);
        std::printf("%s: %zu bytes\n", path.c_str(), bytes.size());
        if (BUDecompress::IsCompressed(bytes.data(), bytes.size()))
        {
            BUDecompress d;
            d.ParseHeader(bytes.data(), bytes.size());
            const auto& h = d.mCompHeader;
            std::printf("BUCompress container (%s): sector 0x%X, header 0x%X, bigChunk 0x%X, %u compressed sectors, "
                        "compressed %llu, uncompressed %llu (%.2f:1)\n",
                        d.mbBigEndian32 ? "32-bit big-endian" : "64-bit little-endian", h.uiSectorSize,
                        h.uiTotalHeaderSize, h.uiBigChunkSize, h.uiNbSectorsCompData,
                        static_cast<unsigned long long>(h.uiCompDataSize),
                        static_cast<unsigned long long>(h.uiUncompFileSize),
                        h.uiCompDataSize ? double(h.uiUncompFileSize) / double(h.uiCompDataSize) : 0.0);
        }
        else
        {
            std::printf("raw chunk tree\n");
        }
        PKPackage pkg;
        pkg.isLittleEndian = gLittleEndian;
        pkg.ReadFromMemory(bytes);
        std::printf("%zu root chunk(s), %zu tail byte(s)\n", pkg.rootChunks.size(), pkg.tailBytes.size());
        for (const auto& root : pkg.rootChunks)
        {
            PrintTree(root, 0, 0);
            for (const auto& child : root.children)
                PrintTree(child, 1, 1);
        }
        return 0;
    }

    int Tree(const std::string& path, int maxDepth)
    {
        PKPackage pkg;
        pkg.isLittleEndian = gLittleEndian;
        pkg.ReadFromFile(path);
        for (const auto& root : pkg.rootChunks)
            PrintTree(root, 0, maxDepth);
        return 0;
    }

    int Unpack(const std::string& in, const std::string& out)
    {
        const std::vector<uint8_t> bytes = ReadFile(in);
        if (!BUDecompress::IsCompressed(bytes.data(), bytes.size()))
        {
            std::fprintf(stderr, "%s is not a BUCompress container; copying as is\n", in.c_str());
            WriteFile(out, bytes);
            return 0;
        }
        const std::vector<uint8_t> raw = BUDecompress::Decompress(bytes);
        WriteFile(out, raw);
        std::printf("%s -> %s: %zu bytes\n", in.c_str(), out.c_str(), raw.size());
        return 0;
    }

    int Pack(const std::string& in, const std::string& out, int level)
    {
        std::vector<uint8_t> bytes = ReadFile(in);
        if (BUDecompress::IsCompressed(bytes.data(), bytes.size()))
            bytes = BUDecompress::Decompress(bytes);
        BUCompress::Options opt;
        opt.level = level;
        const std::vector<uint8_t> packed = BUCompress::Compress(bytes, opt);

        const std::vector<uint8_t> check = BUDecompress::Decompress(packed);
        if (check != bytes)
            throw std::runtime_error("self-check failed: the container does not inflate to the input");
        WriteFile(out, packed);
        std::printf("%s -> %s: %zu -> %zu bytes (%.2f:1)\n", in.c_str(), out.c_str(), bytes.size(), packed.size(),
                    packed.empty() ? 0.0 : double(bytes.size()) / double(packed.size()));
        return 0;
    }

    int RoundTrip(const std::string& path)
    {
        std::vector<uint8_t> bytes = ReadFile(path);
        if (BUDecompress::IsCompressed(bytes.data(), bytes.size()))
            bytes = BUDecompress::Decompress(bytes);
        PKPackage pkg;
        pkg.isLittleEndian = gLittleEndian;
        pkg.ReadFromMemory(bytes);
        const std::vector<uint8_t> again = pkg.Serialize();
        if (again == bytes)
        {
            std::printf("%s: round-trips byte for byte (%zu bytes, %zu root chunks)\n", path.c_str(), bytes.size(),
                        pkg.rootChunks.size());
            return 0;
        }
        size_t first = 0;
        while (first < bytes.size() && first < again.size() && bytes[first] == again[first])
            ++first;
        std::printf("%s: MISMATCH at offset 0x%zx (original %zu bytes, rewritten %zu)\n", path.c_str(), first,
                    bytes.size(), again.size());
        return 1;
    }

    int Strings(const std::string& path, const std::string& only)
    {
        PKPackage pkg;
        pkg.isLittleEndian = gLittleEndian;
        pkg.ReadFromFile(path);
        int tables = 0;
        for (const auto& root : pkg.rootChunks)
        {
            for (const CMChunk* lib : root.FindChildren(Gen_StringTableLibrary))
            {
                for (const RZStringTable& t : RZStringTable::ParseLibrary(*lib))
                {
                    if (!only.empty() && t.name != only)
                        continue;
                    ++tables;
                    std::printf("== %s (crc %08X, languages 0x%X): %zu strings\n", t.name.c_str(), t.nameCRC,
                                t.languageMask, t.entries.size());
                    for (size_t i = 0; i < t.entries.size(); ++i)
                    {
                        const auto& e = t.entries[i];
                        std::printf("  %4zu %08X", i, e.nameCRC);
                        for (const auto& s : e.subs)
                        {
                            if (s.startTime != 0.0f || s.endTime != -1.0f)
                                std::printf(" [%.2f..%.2f]", s.startTime, s.endTime);
                            std::printf(" \"%s\"", RZStringTable::ToUtf8(s.text).c_str());
                        }
                        std::printf("\n");
                    }
                }
            }
        }
        if (!tables)
            std::printf("no string tables%s\n", only.empty() ? "" : " with that name");
        return 0;
    }

    std::string EscapeText(const std::u16string& text)
    {
        std::string out;
        for (char c : RZStringTable::ToUtf8(text))
        {
            if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else out += c;
        }
        return out;
    }

    int ExportStrings(const std::string& path, const std::string& outPath, uint32_t languageMask)
    {
        PKPackage pkg;
        pkg.isLittleEndian = gLittleEndian;
        pkg.ReadFromFile(path);
        uint32_t packageId = 0;
        for (const auto& root : pkg.rootChunks)
        {
            if (const CMChunk* h = root.FindChild(Gen_LevelHeader))
            {
                size_t off = 0;
                packageId = h->Read<uint32_t>(off);
                break;
            }
        }
        std::ofstream out(outPath, std::ios::binary | std::ios::app);
        if (!out)
            throw std::runtime_error("cannot open " + outPath);
        size_t rows = 0;
        uint32_t tableIndex = 0;
        for (const auto& root : pkg.rootChunks)
        {
            for (const CMChunk* lib : root.FindChildren(Gen_StringTableLibrary))
            {
                for (const RZStringTable& t : RZStringTable::ParseLibrary(*lib))
                {
                    if (t.languageMask && !(t.languageMask & languageMask))
                        continue;
                    for (size_t i = 0; i < t.entries.size(); ++i)
                    {
                        const auto& e = t.entries[i];
                        for (size_t s = 0; s < e.subs.size(); ++s)
                        {
                            char head[128];
                            std::snprintf(head, sizeof(head), "%u\t%u\t%s\t%zu\t%zu\t%08X\t%g\t%g\t", packageId, tableIndex,
                                          t.name.c_str(), i, s, e.nameCRC, e.subs[s].startTime, e.subs[s].endTime);
                            out << head << EscapeText(e.subs[s].text) << "\n";
                            ++rows;
                        }
                    }
                    ++tableIndex;
                }
            }
        }
        std::printf("%s: package %u, %u table(s), %zu rows -> %s\n", path.c_str(), packageId, tableIndex, rows, outPath.c_str());
        return 0;
    }

    int MakeStrings(int argc, char** argv)
    {
        if (argc < 4)
            throw std::runtime_error("make-strings needs <out.pkz> <packageId> <packageName> <strings.txt>");
        const std::string out = argv[0];
        const uint32_t packageId = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
        const std::string packageName = argv[2];
        const std::string listPath = argv[3];
        bool compress = false;
        uint32_t languageMask = 0x1F;
        for (int i = 4; i < argc; ++i)
        {
            if (!std::strcmp(argv[i], "--compress"))
                compress = true;
            else if (!std::strcmp(argv[i], "--lang") && i + 1 < argc)
                languageMask = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        }
        if (packageId == 0 || packageId >= 4096)
            throw std::runtime_error("package id must be 1..4095 (the manager's table is 4096 entries)");

        RZStringTable table;
        table.name = packageName;
        table.languageMask = languageMask;
        std::ifstream list(listPath);
        if (!list)
            throw std::runtime_error("cannot open " + listPath);
        std::string line;
        while (std::getline(list, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            const size_t tab = line.find('\t');
            if (tab == std::string::npos)
                throw std::runtime_error("expected NAME<TAB>text: " + line);
            table.Add(line.substr(0, tab), RZStringTable::FromUtf8(line.substr(tab + 1)));
        }

        const bool le = gLittleEndian;
        PKPackage pkg;
        pkg.isLittleEndian = le;
        CMChunk root = PKChunkBuilders::Root(le);
        root.AddChild(PKChunkBuilders::PackageHeader(packageId, packageName, "", le));
        root.AddChild(PKChunkBuilders::Languages({ 1, 2, 5, 3, 4 }, le));
        root.AddChild(PKChunkBuilders::ExportContext("HiRez", "Xbox360", "WorldWide", le));
        root.AddChild(PKChunkBuilders::LodDistances(0.0f, 3.0f, 6.0f, 18.0f, le));
        root.AddChild(RZStringTable::BuildLibrary({ table }, le));
        pkg.rootChunks.push_back(std::move(root));
        pkg.WriteToFile(out, compress);
        std::printf("%s: %llu bytes%s, package %u (0x%X) \"%s\", %zu strings, handles 0x%08X + id\n", out.c_str(),
                    static_cast<unsigned long long>(pkg.GetPackageSize()), compress ? " (compressed container)" : "",
                    packageId, packageId, packageName.c_str(), table.entries.size(), packageId << 20);
        return 0;
    }

    void PrintTexture(const PKPackage& pkg, const CMChunkResourceHeader& h)
    {
        try
        {
            const RZTexture t = RZTexture::Load(pkg, h);
            const XenosTexture::D3DFormat f = t.desc.Format();
            std::printf("%-56s %5ux%-5u mips %u %-10s %s%s d3dfmt %08X data %zu dim %u depth %u mipchain %08X min %08X\n",
                        t.name.c_str(), t.desc.width, t.desc.height, t.desc.mipLevels, f.Name(),
                        f.tiled ? "tiled" : "linear", t.desc.Is2D() ? "" : " (not 2D)", t.desc.d3dFormat,
                        t.gpuData.size(), t.desc.dimension, t.desc.depth, t.desc.mipChainOffset, t.desc.minLevelSize);
        }
        catch (const std::exception& e)
        {
            std::printf("%-56s (%s)\n", h.GetName().c_str(), e.what());
        }
    }

    // "Gen_AudioSampleLibrary" -> "audiosample"
    std::string LibraryKind(const CMChunk& lib)
    {
        std::string s = ToString(lib.GetIDToEnum());
        if (s.rfind("Gen_", 0) == 0)
            s.erase(0, 4);
        if (s.size() > 7 && s.compare(s.size() - 7, 7, "Library") == 0)
            s.resize(s.size() - 7);
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // With no kind, every library and its resource count; otherwise the resources of each library whose kind contains it.
    int List(const std::string& path, std::string kind)
    {
        PKPackage pkg;
        pkg.isLittleEndian = gLittleEndian;
        pkg.ReadFromFile(path);
        for (char& c : kind)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (kind.size() > 1 && kind.back() == 's')
            kind.pop_back();
        int listed = 0;
        for (const CMChunk& root : pkg.rootChunks)
        {
            if (root.GetMaskedID() != Root)
                continue;
            for (const CMChunk& lib : root.children)
            {
                const std::vector<const CMChunk*> resources = lib.FindChildren(GenSub_Resource);
                const std::string libKind = LibraryKind(lib);
                if (resources.empty() || libKind.find(kind) == std::string::npos)
                    continue;
                ++listed;
                if (kind.empty())
                {
                    std::printf("%-16s %5zu\n", libKind.c_str(), resources.size());
                    continue;
                }
                std::printf("== %s: %zu\n", libKind.c_str(), resources.size());
                for (const CMChunk* res : resources)
                {
                    const CMChunk* h = res->FindChild(GenSub_ResourceHeader);
                    if (!h)
                        continue;
                    const CMChunkResourceHeader header(*h);
                    if (lib.GetMaskedID() == Gen_TextureLibrary)
                        PrintTexture(pkg, header);
                    else
                        std::printf("%-56s crc %08X %s\n", header.GetName().c_str(), header.GetCRC(),
                                    header.GetResourceTypeName().c_str());
                }
            }
        }
        if (!listed)
            std::printf("no %s resources\n", kind.c_str());
        return 0;
    }

    int TextureExport(const std::string& path, const std::string& name, const std::string& out)
    {
        PKPackage pkg;
        pkg.isLittleEndian = gLittleEndian;
        pkg.ReadFromFile(path);
        for (const CMChunkResourceHeader& h : RZTexture::Headers(pkg))
        {
            if (h.GetName() != name)
                continue;
            const RZTexture t = RZTexture::Load(pkg, h);
            if (out.size() > 4 && out.compare(out.size() - 4, 4, ".bin") == 0)
            {

                std::vector<uint8_t> raw;
                CMChunk tmp = CMChunk::Leaf(Texture_Data, 7, {});
                tmp.isLittleEndian = gLittleEndian;
                t.desc.Write(tmp);
                raw.insert(raw.end(), tmp.data.begin(), tmp.data.end());
                raw.insert(raw.end(), t.gpuData.begin(), t.gpuData.end());
                WriteFile(out, raw);
                std::printf("%s -> %s: %zu raw bytes\n", name.c_str(), out.c_str(), raw.size());
                return 0;
            }
            const std::vector<uint8_t> rgba = t.DecodeLevel0();
            WriteFile(out, BUPng::EncodeRGBA8(rgba.data(), t.desc.width, t.desc.height));
            std::printf("%s -> %s: %ux%u %s\n", name.c_str(), out.c_str(), t.desc.width, t.desc.height, t.desc.Format().Name());
            return 0;
        }
        std::fprintf(stderr, "no texture named %s\n", name.c_str());
        return 1;
    }

    int Retexture(const std::string& in, const std::string& out, int count, char** pairs)
    {
        PKPackage pkg;
        pkg.isLittleEndian = gLittleEndian;
        pkg.ReadFromFile(in);
        const std::vector<CMChunkResourceHeader> headers = RZTexture::Headers(pkg);
        for (int k = 0; k + 1 < count; k += 2)
        {
            const std::string name = pairs[k];
            const auto h = std::find_if(headers.begin(), headers.end(),
                                        [&](const CMChunkResourceHeader& x) { return x.GetName() == name; });
            if (h == headers.end())
                throw std::runtime_error("no texture named " + name);
            RZTexture t = RZTexture::Load(pkg, *h);
            const std::vector<uint8_t> png = ReadFile(pairs[k + 1]);
            const BUPng::Image img = BUPng::Decode(png.data(), png.size());
            t.Reencode(img.rgba.data(), img.width, img.height);
            t.Store(pkg);
            std::printf("%s <- %s: %ux%u %s, %u mips\n", name.c_str(), pairs[k + 1], t.desc.width, t.desc.height,
                        t.desc.Format().Name(), t.desc.mipLevels);
        }
        const bool compress = out.size() > 4 && out.compare(out.size() - 4, 4, ".pkz") == 0;
        pkg.WriteToFile(out, compress);
        std::printf("%s: %llu bytes raw%s\n", out.c_str(), static_cast<unsigned long long>(pkg.GetPackageSize()),
                    compress ? ", compressed container" : "");
        return 0;
    }


    int Fields(const std::string& path, int maxDepth, const std::string& filter)
    {
        if (gGameSchema.empty())
        {
            std::fprintf(stderr, "fields requires -g <game> (e.g. -g SD-PC)\n");
            return 2;
        }
        const std::string schemaPath = pkfields::ResolveGameSchema(gGameSchema);
        pkfields::SchemaSet schema;
        std::string err;
        if (!pkfields::LoadSchemaFile(schemaPath, schema, err))
        {
            std::fprintf(stderr, "pkztool: %s\n", err.c_str());
            return 1;
        }
        std::printf("schema: %s (%zu chunk types) from %s\n",
                    schema.name.c_str(), schema.chunks.size(), schemaPath.c_str());

        PKPackage pkg;
        pkg.isLittleEndian = gLittleEndian || (schema.endian != "big");
        pkg.ReadFromFile(path);
        for (const CMChunk& root : pkg.rootChunks)
            pkfields::DumpTree(root, schema, 0, maxDepth, filter);
        return 0;
    }

    int Usage()
    {
        std::fprintf(stderr,
                     "pkztool [--le] [-g|--game NAME] <command> ...\n"
                     "  info <file>\n  tree <file> [maxDepth]\n  unpack <in.pkz> <out.pak>\n"
                     "  pack <in.pak> <out.pkz> [level]\n  roundtrip <file>\n  strings <file> [tableName]\n"
                     "  export-strings <file> <out.tsv> [langMask]  append one language's tables as rows\n"
                     "  make-strings <out.pkz> <packageId> <packageName> <strings.txt> [--compress] [--lang MASK]\n"
                     "  list [kind] <file>                    the libraries and their resource counts, or the resources\n"
                     "                                        of a kind (textures, strings, fonts, huds, geometry, audio...)\n"
                     "  texture <file> <name> <out.png|.bin>  export level 0 as PNG, or the raw descriptor + GPU bytes\n"
                     "  retexture <in> <out.pak|.pkz> <name> <in.png> [<name> <in.png>...]\n"
                     "                                        re-encode textures in place from PNGs of the same size\n"
                     "  font <file> [name]                    dump the bitmap fonts' glyph tables\n"
                     "  font-atlas <file> <font> <out.png>    the font's atlas with every glyph box drawn\n"
                     "  hud <file> [name]                     dump the HUD window definitions\n"
                     "  make <manifest> <out.pkz> [--paks <dir>]  author a package (see tools/pkztool_make.h)\n"
                     "  fields <file> [maxDepth] [filter]       dump fields using -g schema (e.g. -g SD-PC)\n"
                     "\n"
                     "Global flags (before command):\n"
                     "  --le                 little-endian headers/payloads (PC)\n"
                     "  -g|--game NAME       load schemas/NAME.xml field definitions\n");
        return 2;
    }
}

int main(int argc, char** argv)
{
    try
    {
        int i = 1;
        while (i < argc)
        {
            if (!std::strcmp(argv[i], "--le"))
            {
                gLittleEndian = true;
                ++i;
            }
            else if ((!std::strcmp(argv[i], "-g") || !std::strcmp(argv[i], "--game")) && i + 1 < argc)
            {
                gGameSchema = argv[i + 1];
                i += 2;
            }
            else
                break;
        }
        if (i >= argc)
            return Usage();
        const std::string cmd = argv[i++];
        const int rest = argc - i;
        if (cmd == "info" && rest >= 1) return Info(argv[i]);
        if (cmd == "tree" && rest >= 1) return Tree(argv[i], rest >= 2 ? std::atoi(argv[i + 1]) : 3);
        if (cmd == "unpack" && rest >= 2) return Unpack(argv[i], argv[i + 1]);
        if (cmd == "pack" && rest >= 2) return Pack(argv[i], argv[i + 1], rest >= 3 ? std::atoi(argv[i + 2]) : Z_BEST_SPEED);
        if (cmd == "roundtrip" && rest >= 1) return RoundTrip(argv[i]);
        if (cmd == "strings" && rest >= 1) return Strings(argv[i], rest >= 2 ? argv[i + 1] : "");
        if (cmd == "export-strings" && rest >= 2)
            return ExportStrings(argv[i], argv[i + 1], rest >= 3 ? static_cast<uint32_t>(std::strtoul(argv[i + 2], nullptr, 0)) : 1u);
        if (cmd == "make-strings") return MakeStrings(rest, argv + i);
        if (cmd == "list" && rest == 1) return List(argv[i], "");
        if (cmd == "list" && rest >= 2) return List(argv[i + 1], argv[i]);
        if (cmd == "texture" && rest >= 3) return TextureExport(argv[i], argv[i + 1], argv[i + 2]);
        if (cmd == "retexture" && rest >= 4 && rest % 2 == 0) return Retexture(argv[i], argv[i + 1], rest - 2, argv + i + 2);
        if (cmd == "font" && rest >= 1) return pkztool::Font(argv[i], rest >= 2 ? argv[i + 1] : "", gLittleEndian);
        if (cmd == "font-atlas" && rest >= 3) return pkztool::FontAtlas(argv[i], argv[i + 1], argv[i + 2], gLittleEndian);
        if (cmd == "hud" && rest >= 1) return pkztool::Hud(argv[i], rest >= 2 ? argv[i + 1] : "", gLittleEndian);
        if (cmd == "styles" && rest >= 1) return pkztool::Styles(argv[i], gLittleEndian);
        if (cmd == "fields" && rest >= 1)
        {
            const int depth = rest >= 2 ? std::atoi(argv[i + 1]) : 6;
            const std::string filter = rest >= 3 ? argv[i + 2] : "";
            return Fields(argv[i], depth, filter);
        }
        if (cmd == "make" && rest >= 2)
        {
            std::string paks;
            for (int k = i + 2; k + 1 < argc; ++k)
                if (!std::strcmp(argv[k], "--paks"))
                    paks = argv[k + 1];
            return pkztool::Make(argv[i], argv[i + 1], paks, gLittleEndian);
        }
        return Usage();
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "pkztool: %s\n", e.what());
        return 1;
    }
}
