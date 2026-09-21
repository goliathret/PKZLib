#pragma once

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../BaseUtils/BUPng.h"
#include "../Package/PKPackageBuilder.h"
#include "../Resource/RZFont.h"
#include "../Resource/RZStringTable.h"
#include "../Resource/RZTextStyle.h"
#include "../Resource/RZTexture.h"

namespace pkztool
{
    inline std::string DirOf(const std::string& path)
    {
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    }

    inline bool IsAbsolute(const std::string& p)
    {
        return !p.empty() && (p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':'));
    }

    inline bool FileExists(const std::string& p)
    {
        std::ifstream f(p, std::ios::binary);
        return f.good();
    }

    inline std::string SiblingSpelling(const std::string& p)
    {
        const size_t dot = p.rfind('.');
        if (dot == std::string::npos)
            return std::string();
        const std::string ext = p.substr(dot);
        if (ext == ".pak" || ext == ".PAK")
            return p.substr(0, dot) + ".pkz";
        if (ext == ".pkz" || ext == ".PKZ")
            return p.substr(0, dot) + ".pak";
        return std::string();
    }

    inline std::string Resolve(const std::string& p, const std::string& manifestDir, const std::string& paksDir)
    {
        if (IsAbsolute(p))
            return p;
        const std::string sibling = SiblingSpelling(p);
        for (const std::string& name : { p, sibling })
        {
            if (name.empty())
                continue;
            if (FileExists(manifestDir + name))
                return manifestDir + name;
            if (!paksDir.empty() && FileExists(paksDir + name))
                return paksDir + name;
        }
        return manifestDir + p;
    }

    inline std::vector<std::string> Split(const std::string& line)
    {
        std::vector<std::string> out;
        std::istringstream is(line);
        std::string tok;
        while (is >> tok)
            out.push_back(tok);
        return out;
    }

    inline std::string Unescape(const std::string& text)
    {
        std::string out;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\\' && i + 1 < text.size())
            {
                const char n = text[++i];
                out.push_back(n == 'n' ? '\n' : n == 't' ? '\t' : n == 'r' ? '\r' : n);
            }
            else
                out.push_back(text[i]);
        }
        return out;
    }

    inline RZStringTable LoadTranslation(const std::string& path, const std::string& tableName, uint32_t salt, uint32_t languageMask)
    {
        RZStringTable table;
        table.name = tableName;
        table.languageMask = languageMask;
        std::ifstream list(path);
        if (!list)
            throw std::runtime_error("cannot open " + path);
        std::string line;
        uint32_t lastCrc = 0;
        bool haveLast = false;
        while (std::getline(list, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            std::vector<std::string> cols;
            size_t from = 0;
            for (int i = 0; i < 8; ++i)
            {
                const size_t tab = line.find('\t', from);
                if (tab == std::string::npos)
                    break;
                cols.push_back(line.substr(from, tab - from));
                from = tab + 1;
            }
            if (cols.size() != 8)
                throw std::runtime_error("expected 9 tab-separated columns: " + line);
            const uint32_t crc = static_cast<uint32_t>(std::strtoul(cols[5].c_str(), nullptr, 16));
            const uint32_t sub = static_cast<uint32_t>(std::strtoul(cols[4].c_str(), nullptr, 10));
            RZStringTable::SubString s;
            s.text = RZStringTable::FromUtf8(Unescape(line.substr(from)));
            s.startTime = static_cast<float>(std::atof(cols[6].c_str()));
            s.endTime = static_cast<float>(std::atof(cols[7].c_str()));
            if (!haveLast || crc != lastCrc || sub == 0)
            {
                RZStringTable::Entry e;
                e.nameCRC = crc ^ salt;
                table.entries.push_back(std::move(e));
                lastCrc = crc;
                haveLast = true;
            }
            table.entries.back().subs.push_back(std::move(s));
        }
        return table;
    }

    inline RZStringTable LoadStrings(const std::string& path, const std::string& tableName, uint32_t languageMask)
    {
        RZStringTable table;
        table.name = tableName;
        table.languageMask = languageMask;
        std::ifstream list(path);
        if (!list)
            throw std::runtime_error("cannot open " + path);
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
        return table;
    }

    inline RZTexture CopyTexture(const std::string& pakPath, const std::string& retailName, const std::string& newName,
                                 bool littleEndian)
    {
        PKPackage pkg;
        pkg.isLittleEndian = littleEndian;
        pkg.ReadFromFile(pakPath);
        for (const CMChunkResourceHeader& h : RZTexture::Headers(pkg))
        {
            if (h.GetName() != retailName)
                continue;
            RZTexture t = RZTexture::Load(pkg, h);
            t.name = newName;
            t.nameCRC = BUCRC().Generate(newName);
            return t;
        }
        throw std::runtime_error("no texture named " + retailName + " in " + pakPath);
    }

    inline RZTextStyle CopyTextStyle(const std::string& pakPath, const std::string& retailName, const std::string& newName,
                                     bool littleEndian)
    {
        PKPackage pkg;
        pkg.isLittleEndian = littleEndian;
        pkg.ReadFromFile(pakPath);
        for (const RZTextStyle& s : RZTextStyle::All(pkg))
            if (s.name == retailName)
                return s.Renamed(newName);
        throw std::runtime_error("no text style named " + retailName + " in " + pakPath);
    }

    inline RZFont CopyFont(const std::string& pakPath, const std::string& retailName, const std::string& newName,
                           const std::string& atlas, bool littleEndian)
    {
        PKPackage pkg;
        pkg.isLittleEndian = littleEndian;
        pkg.ReadFromFile(pakPath);
        for (const RZFont& f : RZFont::All(pkg))
        {
            if (f.name != retailName)
                continue;
            RZFont copy = f.Renamed(newName);
            return atlas.empty() ? copy : copy.WithAtlas(atlas);
        }
        throw std::runtime_error("no font named " + retailName + " in " + pakPath);
    }

    inline RZTexture BuildAtlas(const std::string& dir, uint32_t columns, uint32_t slots, const std::string& name)
    {
        if (!columns || !slots)
            throw std::runtime_error("atlas needs a column count and at least one slot");
        uint32_t cell = 0, height = 0;
        std::vector<uint8_t> sheet;
        const uint32_t rows = (slots + columns - 1) / columns;
        for (uint32_t slot = 0; slot < slots; ++slot)
        {
            const std::string path = dir + "/" + std::to_string(slot + 1) + ".png";
            std::ifstream f(path, std::ios::binary);
            if (!f)
                throw std::runtime_error("atlas: cannot open " + path);
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const BUPng::Image img = BUPng::Decode(bytes.data(), bytes.size());
            if (!cell)
            {
                if (img.width != img.height)
                    throw std::runtime_error("atlas: " + path + " is not square");
                cell = img.width;
                height = cell * rows;
                sheet.assign(size_t(cell) * columns * height * 4, 0);
            }
            else if (img.width != cell || img.height != cell)
            {
                throw std::runtime_error("atlas: " + path + " does not match the first cell's size");
            }
            const uint32_t x = (slot % columns) * cell;
            const uint32_t y = (slot / columns) * cell;
            for (uint32_t row = 0; row < cell; ++row)
                std::memcpy(sheet.data() + ((size_t(y + row) * cell * columns) + x) * 4,
                            img.rgba.data() + size_t(row) * cell * 4, size_t(cell) * 4);
        }
        return RZTexture::FromRGBA8(name, sheet.data(), cell * columns, height);
    }

    inline bool ParseFloats(const std::string& v, float* out, size_t n)
    {
        std::istringstream is(v);
        for (size_t i = 0; i < n; ++i)
        {
            char comma;
            if (!(is >> out[i])) return false;
            if (i + 1 < n && !(is >> comma)) return false;
        }
        return true;
    }

    inline uint32_t CrcOrName(const std::string& v)
    {
        if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X'))
            return static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 16));
        return BUCRC().Generate(v);
    }

    inline RZHudWindow ParseWindow(const std::vector<std::string>& t, const std::string& where)
    {
        uint32_t type = 0, prio = 1, flags = 0, textFlags = 0, alignH = 0, alignV = 0;
        uint32_t parent = 0xFFFFFFFFu, texture = 0, style = 0, string = 0xFFFFFFFFu;
        float rect[4] = { 0, 0, 1, 1 }, wide[4] = { 0, 0, 1, 1 };
        bool hasWide = false, hasStyle = false;
        for (size_t i = 2; i < t.size(); ++i)
        {
            const size_t eq = t[i].find('=');
            if (eq == std::string::npos) throw std::runtime_error(where + "expected key=value: " + t[i]);
            const std::string k = t[i].substr(0, eq), v = t[i].substr(eq + 1);
            if (k == "type") type = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 0));
            else if (k == "prio") prio = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 0));
            else if (k == "flags") flags = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 0));
            else if (k == "textflags") textFlags = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 0));
            else if (k == "parent") parent = CrcOrName(v);
            else if (k == "texture") texture = CrcOrName(v);
            else if (k == "style") { style = CrcOrName(v); hasStyle = true; }
            else if (k == "string") string = CrcOrName(v);
            else if (k == "rect") { if (!ParseFloats(v, rect, 4)) throw std::runtime_error(where + "bad rect"); }
            else if (k == "wide") { if (!ParseFloats(v, wide, 4)) throw std::runtime_error(where + "bad wide rect"); hasWide = true; }
            else if (k == "align") { float a[2]; if (!ParseFloats(v, a, 2)) throw std::runtime_error(where + "bad align"); alignH = uint32_t(a[0]); alignV = uint32_t(a[1]); }
            else throw std::runtime_error(where + "unknown window key: " + k);
        }
        RZHudWindow w = RZHudWindow::Make(t[1], type, parent, prio, rect, hasWide ? wide : nullptr, flags);
        if (texture) w.SetTexture(texture);
        if (hasStyle) w.SetText(style, string, textFlags, alignH, alignV);
        return w;
    }

    inline int Make(const std::string& manifestPath, const std::string& outPath, const std::string& paksDirIn, bool littleEndian)
    {
        std::ifstream mf(manifestPath);
        if (!mf)
            throw std::runtime_error("cannot open " + manifestPath);
        const std::string dir = DirOf(manifestPath);
        std::string paksDir = paksDirIn;
        if (!paksDir.empty() && paksDir.back() != '/' && paksDir.back() != '\\')
            paksDir += '/';

        PKPackageBuilder b;
        b.littleEndian = littleEndian;
        bool compress = false;
        struct StringFile
        {
            std::string path;
            std::string table;
            uint32_t languageMask;
        };
        std::vector<StringFile> stringFiles;
        std::string line;
        int lineNo = 0;
        while (std::getline(mf, line))
        {
            ++lineNo;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const size_t hash = line.find('#');
            if (hash != std::string::npos)
                line = line.substr(0, hash);
            const std::vector<std::string> t = Split(line);
            if (t.empty())
                continue;
            const std::string where = manifestPath + ":" + std::to_string(lineNo) + ": ";
            if (t[0] == "package" && t.size() >= 3)
            {
                b.packageId = static_cast<uint32_t>(std::strtoul(t[1].c_str(), nullptr, 0));
                b.packageName = t[2];
            }
            else if (t[0] == "languages" && t.size() >= 2)
            {
                b.languages.clear();
                for (size_t i = 1; i < t.size(); ++i)
                    b.languages.push_back(static_cast<uint32_t>(std::strtoul(t[i].c_str(), nullptr, 0)));
            }
            else if (t[0] == "strings" && t.size() >= 2)
            {
                StringFile file{ Resolve(t[1], dir, paksDir), std::string(), 0x1F };
                for (size_t i = 2; i < t.size(); ++i)
                {
                    if (t[i].rfind("lang=", 0) == 0)
                        file.languageMask = static_cast<uint32_t>(std::strtoul(t[i].c_str() + 5, nullptr, 0));
                    else
                        file.table = t[i];
                }
                stringFiles.push_back(std::move(file));
            }
            else if (t[0] == "translation" && t.size() >= 2)
            {
                uint32_t salt = 0;
                uint32_t languageMask = 0x1;
                std::string table = "Translation";
                for (size_t i = 2; i < t.size(); ++i)
                {
                    if (t[i].rfind("salt=", 0) == 0)
                        salt = static_cast<uint32_t>(std::strtoul(t[i].c_str() + 5, nullptr, 0));
                    else if (t[i].rfind("lang=", 0) == 0)
                        languageMask = static_cast<uint32_t>(std::strtoul(t[i].c_str() + 5, nullptr, 0));
                    else
                        table = t[i];
                }
                b.stringTables.push_back(LoadTranslation(Resolve(t[1], dir, paksDir), table, salt, languageMask));
            }
            else if (t[0] == "texture" && t.size() >= 5 && t[2] == "from")
                b.textures.push_back(CopyTexture(Resolve(t[3], dir, paksDir), t[4], t[1], littleEndian));
            else if (t[0] == "texture" && t.size() >= 4 && t[2] == "dds")
            {
                std::ifstream f(Resolve(t[3], dir, paksDir), std::ios::binary);
                if (!f)
                    throw std::runtime_error(where + "cannot open " + t[3]);
                const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                bool baseLevelOnly = false;
                RZTexture::DdsColor color = RZTexture::DdsColor::Gamma;
                for (size_t i = 4; i < t.size(); ++i)
                {
                    if (t[i] == "nomips")
                        baseLevelOnly = true;
                    else if (t[i] == "linear")
                        color = RZTexture::DdsColor::Linear;
                    else if (t[i] == "normal")
                        color = RZTexture::DdsColor::NormalMap;
                    else
                        throw std::runtime_error(where + "unknown texture option " + t[i]);
                }
                b.textures.push_back(RZTexture::FromDds(t[1], bytes, baseLevelOnly, color));
            }
            else if (t[0] == "texture" && t.size() >= 6 && t[2] == "atlas")
            {
                const uint32_t columns = static_cast<uint32_t>(std::strtoul(t[3].c_str(), nullptr, 0));
                const uint32_t slots = static_cast<uint32_t>(std::strtoul(t[4].c_str(), nullptr, 0));
                b.textures.push_back(BuildAtlas(Resolve(t[5], dir, paksDir), columns, slots, t[1]));
            }
            else if (t[0] == "texture" && t.size() >= 4 && t[2] == "png")
            {
                std::ifstream f(Resolve(t[3], dir, paksDir), std::ios::binary);
                if (!f)
                    throw std::runtime_error(where + "cannot open " + t[3]);
                const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                const BUPng::Image img = BUPng::Decode(bytes.data(), bytes.size());
                b.textures.push_back(RZTexture::FromRGBA8(t[1], img.rgba.data(), img.width, img.height));
            }
            else if (t[0] == "font" && t.size() >= 5 && t[2] == "from")
            {
                std::string atlas;
                for (size_t i = 5; i < t.size(); ++i)
                    if (t[i].rfind("atlas=", 0) == 0)
                        atlas = t[i].substr(6);
                b.fonts.push_back(CopyFont(Resolve(t[3], dir, paksDir), t[4], t[1], atlas, littleEndian));
            }
            else if (t[0] == "textstyle" && t.size() >= 5 && t[2] == "from")
                b.textStyles.push_back(CopyTextStyle(Resolve(t[3], dir, paksDir), t[4], t[1], littleEndian));
            else if (t[0] == "window" && t.size() >= 2)
                b.windows.push_back(ParseWindow(t, where));
            else if (t[0] == "windowgrid" && t.size() >= 4)
            {
                const uint32_t columns = static_cast<uint32_t>(std::strtoul(t[2].c_str(), nullptr, 0));
                const uint32_t count = static_cast<uint32_t>(std::strtoul(t[3].c_str(), nullptr, 0));
                float step[2] = { 0, 0 };
                std::vector<std::string> shared{ t[0], t[1] };
                for (size_t i = 4; i < t.size(); ++i)
                {
                    if (t[i].rfind("step=", 0) == 0)
                    {
                        if (!ParseFloats(t[i].substr(5), step, 2))
                            throw std::runtime_error(where + "bad step");
                    }
                    else
                    {
                        shared.push_back(t[i]);
                    }
                }
                if (!columns || !count)
                    throw std::runtime_error(where + "windowgrid needs a column count and a cell count");
                for (uint32_t cell = 0; cell < count; ++cell)
                {
                    std::vector<std::string> one = shared;
                    char suffix[8];
                    std::snprintf(suffix, sizeof(suffix), "%02u", cell);
                    one[1] = t[1] + suffix;
                    RZHudWindow w = ParseWindow(one, where);
                    const float dx = step[0] * static_cast<float>(cell % columns);
                    const float dy = step[1] * static_cast<float>(cell / columns);
                    w.Offset(dx, dy);
                    b.windows.push_back(std::move(w));
                }
            }
            else if (t[0] == "compress" && t.size() >= 2)
                compress = (t[1] == "true" || t[1] == "1" || t[1] == "yes");
            else
                throw std::runtime_error(where + "unknown directive: " + line);
        }
        if (!b.packageId || b.packageName.empty())
            throw std::runtime_error("manifest needs a `package <id> <name>` line");
        for (const StringFile& file : stringFiles)
            b.stringTables.push_back(LoadStrings(file.path, file.table.empty() ? b.packageName : file.table, file.languageMask));

        const PKPackage pkg = b.Build();
        pkg.WriteToFile(outPath, compress);

        std::printf("%s: package %u (0x%X) \"%s\"%s\n", outPath.c_str(), b.packageId, b.packageId, b.packageName.c_str(),
                    compress ? ", compressed container" : "");
        for (const RZStringTable& s : b.stringTables)
            std::printf("  strings   %-40s %zu entries, languages 0x%02X, handles 0x%08X + id\n", s.name.c_str(), s.entries.size(),
                        s.languageMask, b.packageId << 20);
        for (const RZTexture& t : b.textures)
            std::printf("  texture   %-40s crc %08X %ux%u %s, %zu bytes\n", t.name.c_str(), t.nameCRC, t.desc.width, t.desc.height,
                        t.desc.Format().Name(), t.gpuData.size());
        for (const RZFont& f : b.fonts)
            std::printf("  font      %-40s crc %08X %zu glyphs, atlas %08X\n", f.name.c_str(), f.nameCRC,
                        f.glyphs.size(), f.textureCRC);
        for (const RZTextStyle& s : b.textStyles)
            std::printf("  textstyle %-40s crc %08X font %08X\n", s.name.c_str(), s.nameCRC, s.fontCRC);
        for (const RZHudWindow& w : b.windows)
            std::printf("  window    %-40s crc %08X type %u\n", w.name.c_str(), w.nameCRC,
                        w.layouts.empty() ? 0u : w.layouts[0].header.type);
        std::printf("  %llu bytes raw\n", static_cast<unsigned long long>(pkg.GetPackageSize()));
        return 0;
    }
}
