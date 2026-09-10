#pragma once

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../BaseUtils/BUPng.h"
#include "../Package/PKPackageBuilder.h"
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

    inline std::string Resolve(const std::string& p, const std::string& manifestDir, const std::string& paksDir)
    {
        if (IsAbsolute(p))
            return p;
        if (FileExists(manifestDir + p))
            return manifestDir + p;
        if (!paksDir.empty() && FileExists(paksDir + p))
            return paksDir + p;
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
        std::string stringsFile;
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
                stringsFile = Resolve(t[1], dir, paksDir);
            else if (t[0] == "texture" && t.size() >= 5 && t[2] == "from")
                b.textures.push_back(CopyTexture(Resolve(t[3], dir, paksDir), t[4], t[1], littleEndian));
            else if (t[0] == "texture" && t.size() >= 4 && t[2] == "png")
            {
                std::ifstream f(Resolve(t[3], dir, paksDir), std::ios::binary);
                if (!f)
                    throw std::runtime_error(where + "cannot open " + t[3]);
                const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                const BUPng::Image img = BUPng::Decode(bytes.data(), bytes.size());
                b.textures.push_back(RZTexture::FromRGBA8(t[1], img.rgba.data(), img.width, img.height));
            }
            else if (t[0] == "textstyle" && t.size() >= 5 && t[2] == "from")
                b.textStyles.push_back(CopyTextStyle(Resolve(t[3], dir, paksDir), t[4], t[1], littleEndian));
            else if (t[0] == "window" && t.size() >= 2)
                b.windows.push_back(ParseWindow(t, where));
            else if (t[0] == "compress" && t.size() >= 2)
                compress = (t[1] == "true" || t[1] == "1" || t[1] == "yes");
            else
                throw std::runtime_error(where + "unknown directive: " + line);
        }
        if (!b.packageId || b.packageName.empty())
            throw std::runtime_error("manifest needs a `package <id> <name>` line");
        if (!stringsFile.empty())
            b.stringTables.push_back(LoadStrings(stringsFile, b.packageName, 0x1F));

        const PKPackage pkg = b.Build();
        pkg.WriteToFile(outPath, compress);

        std::printf("%s: package %u (0x%X) \"%s\"%s\n", outPath.c_str(), b.packageId, b.packageId, b.packageName.c_str(),
                    compress ? ", compressed container" : "");
        for (const RZStringTable& s : b.stringTables)
            std::printf("  strings   %-40s %zu entries, handles 0x%08X + id\n", s.name.c_str(), s.entries.size(), b.packageId << 20);
        for (const RZTexture& t : b.textures)
            std::printf("  texture   %-40s crc %08X %ux%u %s, %zu bytes\n", t.name.c_str(), t.nameCRC, t.desc.width, t.desc.height,
                        t.desc.Format().Name(), t.gpuData.size());
        for (const RZTextStyle& s : b.textStyles)
            std::printf("  textstyle %-40s crc %08X font %08X\n", s.name.c_str(), s.nameCRC, s.fontCRC);
        for (const RZHudWindow& w : b.windows)
            std::printf("  window    %-40s crc %08X type %u\n", w.name.c_str(), w.nameCRC,
                        w.layouts.empty() ? 0u : w.layouts[0].header.type);
        std::printf("  %llu bytes raw\n", static_cast<unsigned long long>(pkg.GetPackageSize()));
        return 0;
    }
}
