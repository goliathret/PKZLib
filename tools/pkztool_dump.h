#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "../BaseUtils/BUPng.h"
#include "../Package/PKPackage.h"
#include "../Resource/RZFont.h"
#include "../Resource/RZHud.h"
#include "../Resource/RZTexture.h"

namespace pkztool
{

    inline std::vector<std::pair<uint32_t, std::string>> NameTable(const PKPackage& pkg)
    {
        std::vector<std::pair<uint32_t, std::string>> out;
        for (const CMChunk& root : pkg.rootChunks)
            if (root.GetMaskedID() == Root)
                for (const CMChunk& lib : root.children)
                    for (const CMChunk* res : lib.FindChildren(GenSub_Resource))
                        if (const CMChunk* h = res->FindChild(GenSub_ResourceHeader))
                        {
                            CMChunkResourceHeader header(*h);
                            out.emplace_back(header.GetCRC(), header.GetName());
                        }
        return out;
    }

    inline std::string NameOf(const std::vector<std::pair<uint32_t, std::string>>& names, uint32_t crc)
    {
        if (crc == 0xFFFFFFFFu)
            return "-";
        for (const auto& p : names)
            if (p.first == crc)
                return p.second;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08X", crc);
        return buf;
    }

    inline int Font(const std::string& path, const std::string& only, bool littleEndian)
    {
        PKPackage pkg;
        pkg.isLittleEndian = littleEndian;
        pkg.ReadFromFile(path);
        const auto names = NameTable(pkg);
        int n = 0;
        for (const RZFont& f : RZFont::All(pkg))
        {
            if (!only.empty() && f.name != only)
                continue;
            ++n;
            std::printf("== %s (crc %08X): %u glyphs, atlas %s, info %u %.6f %.4f %.4f %u %u 0x%X\n", f.name.c_str(), f.nameCRC,
                        f.glyphCount, NameOf(names, f.textureCRC).c_str(), f.info1, f.info2, f.info3, f.info4, f.info5, f.info6, f.info7);
            std::printf("   code    flags  u0     v0     u1     v1     advance height width\n");
            for (const RZFont::Glyph& g : f.glyphs)
                std::printf("   U+%04X  %u  %.4f %.4f %.4f %.4f  %.4f %.4f %.4f\n", g.codePoint, g.flags, g.u0, g.v0, g.u1, g.v1,
                            g.advance, g.height, g.width);
        }
        if (!n)
            std::printf("no fonts%s\n", only.empty() ? "" : " with that name");
        return 0;
    }

    inline int FontAtlas(const std::string& path, const std::string& fontName, const std::string& out, bool littleEndian)
    {
        PKPackage pkg;
        pkg.isLittleEndian = littleEndian;
        pkg.ReadFromFile(path);
        for (const RZFont& f : RZFont::All(pkg))
        {
            if (f.name != fontName)
                continue;
            for (const CMChunkResourceHeader& h : RZTexture::Headers(pkg))
            {
                if (h.GetCRC() != f.textureCRC)
                    continue;
                const RZTexture t = RZTexture::Load(pkg, h);
                std::vector<uint8_t> rgba = t.DecodeLevel0();
                const uint32_t W = t.desc.width, H = t.desc.height;
                auto plot = [&](int x, int y, const uint8_t* c) {
                    if (x < 0 || y < 0 || x >= int(W) || y >= int(H)) return;
                    std::memcpy(rgba.data() + (size_t(y) * W + x) * 4, c, 4);
                };
                for (const RZFont::Glyph& g : f.glyphs)
                {
                    const uint8_t red[4] = { 255, 0, 0, 255 }, green[4] = { 0, 255, 0, 255 };
                    const uint8_t* c = (g.flags & 1) ? green : red;
                    const int x0 = int(g.u0 * W + 0.5f), y0 = int(g.v0 * H + 0.5f), x1 = int(g.u1 * W + 0.5f) - 1, y1 = int(g.v1 * H + 0.5f) - 1;
                    for (int x = x0; x <= x1; ++x) { plot(x, y0, c); plot(x, y1, c); }
                    for (int y = y0; y <= y1; ++y) { plot(x0, y, c); plot(x1, y, c); }
                }
                std::ofstream o(out, std::ios::binary);
                const std::vector<uint8_t> png = BUPng::EncodeRGBA8(rgba.data(), W, H);
                o.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
                std::printf("%s: atlas %s %ux%u with %u glyph boxes -> %s\n", fontName.c_str(), t.name.c_str(), W, H, f.glyphCount, out.c_str());
                return 0;
            }
            std::fprintf(stderr, "font %s: atlas texture %08X is not in this package\n", fontName.c_str(), f.textureCRC);
            return 1;
        }
        std::fprintf(stderr, "no font named %s\n", fontName.c_str());
        return 1;
    }

    inline int Hud(const std::string& path, const std::string& only, bool littleEndian)
    {
        PKPackage pkg;
        pkg.isLittleEndian = littleEndian;
        pkg.ReadFromFile(path);
        const auto names = NameTable(pkg);
        int n = 0;
        for (const RZHudWindow& w : RZHudWindow::All(pkg))
        {
            if (!only.empty() && w.name != only)
                continue;
            ++n;
            std::printf("== %s (crc %08X)\n", w.name.c_str(), w.nameCRC);
            for (const RZHudWindow::Layout& l : w.layouts)
            {
                const auto& H = l.header;
                std::printf("   %s type %u flags 0x%X rect (%.3f, %.3f  %.3f x %.3f) scale (%.2f, %.2f) parent %s prio %u pivot (%.2f, %.2f)\n",
                            H.Wide() ? "wide  " : "normal", H.type, H.flags, H.x, H.y, H.w, H.h, H.scaleX, H.scaleY,
                            NameOf(names, H.parentCRC).c_str(), H.priority, H.pivotX, H.pivotY);
                std::printf("          colors");
                for (int i = 0; i < 4; ++i)
                    std::printf(" (%.2f %.2f %.2f %.2f)", H.colors[i * 4], H.colors[i * 4 + 1], H.colors[i * 4 + 2], H.colors[i * 4 + 3]);
                std::printf("\n");
                if (l.hasText)
                    std::printf("          text: style %s string %s flags 0x%X spacing %.1f margin %.3f scale (%.2f, %.2f) align %u/%u\n",
                                NameOf(names, l.text.styleCRC).c_str(), NameOf(names, l.text.stringCRC).c_str(), l.text.flags,
                                l.text.lineSpacing, l.text.margin, l.text.scaleX, l.text.scaleY, l.text.alignH, l.text.alignV);
                if (l.has2D)
                    std::printf("          2d: texture %s\n", NameOf(names, l.quad.textureCRC).c_str());
            }
            if (!w.refs.empty())
            {
                std::printf("   refs:");
                for (const auto& r : w.refs)
                    std::printf(" [%u] %s", r.first, NameOf(names, r.second).c_str());
                std::printf("\n");
            }
        }
        if (!n)
            std::printf("no HUD windows%s\n", only.empty() ? "" : " with that name");
        return 0;
    }
}
