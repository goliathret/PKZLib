#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "../BaseUtils/BUCRC.h"
#include "../Package/CMChunk.h"
#include "../Package/PKPackage.h"
#include "ResourceHeader.h"

class RZHudWindow
{
public:
    struct Header
    {
        uint32_t type = 0, flags = 0, pad2 = 0;
        float x = 0, y = 0, w = 1, h = 1;
        float f7 = 0, f8 = 0;
        float scaleX = 1, scaleY = 1;
        uint32_t parentCRC = 0xFFFFFFFFu;
        uint32_t priority = 0;
        float colors[16] = {};
        uint32_t pad29 = 0;
        float pivotX = 0.5f, pivotY = 0.5f;
        uint32_t pad32 = 0, pad33 = 0;
        bool Wide() const { return (flags & 0x10) != 0; }
    };
    struct Text
    {
        uint32_t flags = 0, styleCRC = 0;
        uint32_t pad[4] = {};
        uint32_t stringCRC = 0xFFFFFFFFu;
        uint32_t one = 1;
        float lineSpacing = 10.0f, margin = 0.01f, scaleX = 1, scaleY = 1;
        uint32_t alignH = 0, alignV = 0, tail = 0xFFFFFFFFu;
    };
    struct Quad2D
    {
        uint32_t textureCRC = 0;
        float f[19] = {};
    };
    struct Layout
    {
        Header header;
        bool hasText = false;
        Text text;
        bool has2D = false;
        Quad2D quad;
    };

    std::string name;
    uint32_t nameCRC = 0;
    std::vector<Layout> layouts;
    std::vector<std::pair<uint32_t, uint32_t>> refs;

    static RZHudWindow Parse(const CMChunk& resource)
    {
        RZHudWindow w;
        if (const CMChunk* h = resource.FindChild(GenSub_ResourceHeader))
        {
            CMChunkResourceHeader header(*h);
            w.name = header.GetName();
            w.nameCRC = header.GetCRC();
        }
        const CMChunk* hud = resource.FindChild(HUD);
        if (!hud)
            throw std::runtime_error("RZHudWindow: no HUD chunk in " + w.name);
        for (const CMChunk* win : hud->FindChildren(HUD_Window))
        {
            Layout l;
            if (const CMChunk* hh = win->FindChild(HUD_WindowHeader))
            {
                size_t off = 0;
                Header& H = l.header;
                H.type = hh->Read<uint32_t>(off); H.flags = hh->Read<uint32_t>(off); H.pad2 = hh->Read<uint32_t>(off);
                H.x = hh->Read<float>(off); H.y = hh->Read<float>(off); H.w = hh->Read<float>(off); H.h = hh->Read<float>(off);
                H.f7 = hh->Read<float>(off); H.f8 = hh->Read<float>(off);
                H.scaleX = hh->Read<float>(off); H.scaleY = hh->Read<float>(off);
                H.parentCRC = hh->Read<uint32_t>(off); H.priority = hh->Read<uint32_t>(off);
                for (float& c : H.colors) c = hh->Read<float>(off);
                H.pad29 = hh->Read<uint32_t>(off);
                H.pivotX = hh->Read<float>(off); H.pivotY = hh->Read<float>(off);
                H.pad32 = hh->Read<uint32_t>(off); H.pad33 = hh->Read<uint32_t>(off);
            }
            if (const CMChunk* t = win->FindChild(HUD_TextWindow))
            {
                size_t off = 0;
                Text& T = l.text;
                T.flags = t->Read<uint32_t>(off); T.styleCRC = t->Read<uint32_t>(off);
                for (uint32_t& p : T.pad) p = t->Read<uint32_t>(off);
                T.stringCRC = t->Read<uint32_t>(off); T.one = t->Read<uint32_t>(off);
                T.lineSpacing = t->Read<float>(off); T.margin = t->Read<float>(off);
                T.scaleX = t->Read<float>(off); T.scaleY = t->Read<float>(off);
                T.alignH = t->Read<uint32_t>(off); T.alignV = t->Read<uint32_t>(off); T.tail = t->Read<uint32_t>(off);
                l.hasText = true;
            }
            if (const CMChunk* q = win->FindChild(HUD_2DWindow))
            {
                size_t off = 0;
                l.quad.textureCRC = q->Read<uint32_t>(off);
                for (float& v : l.quad.f) v = q->Read<float>(off);
                l.has2D = true;
            }
            w.layouts.push_back(l);
        }
        if (const CMChunk* r = resource.FindChild(GenSub_RefResourceArray))
        {
            size_t off = 0;
            while (off + 8 <= r->data.size())
            {
                const uint32_t type = r->Read<uint32_t>(off);
                const uint32_t crc = r->Read<uint32_t>(off);
                w.refs.emplace_back(type, crc);
            }
        }
        return w;
    }

    CMChunk Build(bool littleEndian = false) const
    {
        CMChunk res = CMChunk::Container(GenSub_Resource, 1);
        res.isLittleEndian = littleEndian;
        res.AddChild(CMChunkResourceHeader::Build(nameCRC, 0x80, 0x1F, 0, 0, 0xFFFFFFFFu, name, littleEndian));
        CMChunk hud = CMChunk::Container(HUD, 2);
        hud.isLittleEndian = littleEndian;
        for (const Layout& l : layouts)
        {
            CMChunk win = CMChunk::Container(HUD_Window, 1);
            win.isLittleEndian = littleEndian;
            CMChunk hh = CMChunk::Leaf(HUD_WindowHeader, 7, {});
            hh.isLittleEndian = littleEndian;
            const Header& H = l.header;
            hh.Append<uint32_t>(H.type); hh.Append<uint32_t>(H.flags); hh.Append<uint32_t>(H.pad2);
            hh.Append<float>(H.x); hh.Append<float>(H.y); hh.Append<float>(H.w); hh.Append<float>(H.h);
            hh.Append<float>(H.f7); hh.Append<float>(H.f8);
            hh.Append<float>(H.scaleX); hh.Append<float>(H.scaleY);
            hh.Append<uint32_t>(H.parentCRC); hh.Append<uint32_t>(H.priority);
            for (float c : H.colors) hh.Append<float>(c);
            hh.Append<uint32_t>(H.pad29);
            hh.Append<float>(H.pivotX); hh.Append<float>(H.pivotY);
            hh.Append<uint32_t>(H.pad32); hh.Append<uint32_t>(H.pad33);
            win.AddChild(std::move(hh));
            if (l.hasText)
            {
                CMChunk t = CMChunk::Leaf(HUD_TextWindow, 5, {});
                t.isLittleEndian = littleEndian;
                const Text& T = l.text;
                t.Append<uint32_t>(T.flags); t.Append<uint32_t>(T.styleCRC);
                for (uint32_t p : T.pad) t.Append<uint32_t>(p);
                t.Append<uint32_t>(T.stringCRC); t.Append<uint32_t>(T.one);
                t.Append<float>(T.lineSpacing); t.Append<float>(T.margin);
                t.Append<float>(T.scaleX); t.Append<float>(T.scaleY);
                t.Append<uint32_t>(T.alignH); t.Append<uint32_t>(T.alignV); t.Append<uint32_t>(T.tail);
                win.AddChild(std::move(t));
            }
            if (l.has2D)
            {
                CMChunk q = CMChunk::Leaf(HUD_2DWindow, 3, {});
                q.isLittleEndian = littleEndian;
                q.Append<uint32_t>(l.quad.textureCRC);
                for (float v : l.quad.f) q.Append<float>(v);
                win.AddChild(std::move(q));
            }
            hud.AddChild(std::move(win));
        }
        res.AddChild(std::move(hud));
        if (!refs.empty())
        {
            CMChunk r = CMChunk::Leaf(GenSub_RefResourceArray, 1, {});
            r.isLittleEndian = littleEndian;
            for (const auto& p : refs) { r.Append<uint32_t>(p.first); r.Append<uint32_t>(p.second); }
            res.AddChild(std::move(r));
        }
        return res;
    }

    static void DefaultQuad(Quad2D& q)
    {
        static const float kQuad[19] = { 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0 };
        for (int i = 0; i < 19; ++i) q.f[i] = kQuad[i];
    }

    static RZHudWindow Make(const std::string& name, uint32_t type, uint32_t parentCRC, uint32_t priority,
                            const float rect[4], const float* wideRect = nullptr, uint32_t flags = 0)
    {
        RZHudWindow w;
        w.name = name;
        w.nameCRC = BUCRC().Generate(name);
        for (int i = 0; i < 2; ++i)
        {
            Layout l;
            l.header.type = type;
            l.header.flags = flags | (i ? 0x10u : 0u);
            const float* r = (i && wideRect) ? wideRect : rect;
            l.header.x = r[0]; l.header.y = r[1]; l.header.w = r[2]; l.header.h = r[3];
            l.header.parentCRC = parentCRC;
            l.header.priority = priority;
            for (float& c : l.header.colors) c = 1.0f;
            w.layouts.push_back(l);
        }
        return w;
    }

    void Offset(float dx, float dy)
    {
        for (Layout& layout : layouts)
        {
            layout.header.x += dx;
            layout.header.y += dy;
        }
    }

    void SetTexture(uint32_t textureCRC)
    {
        for (Layout& l : layouts)
        {
            l.has2D = true;
            l.quad.textureCRC = textureCRC;
            DefaultQuad(l.quad);
        }
        refs.emplace_back(4u, textureCRC);
    }

    void SetText(uint32_t styleCRC, uint32_t stringCRC, uint32_t textFlags, uint32_t alignH, uint32_t alignV)
    {
        for (Layout& l : layouts)
        {
            l.hasText = true;
            l.text.flags = textFlags;
            l.text.styleCRC = styleCRC;
            l.text.stringCRC = stringCRC;
            l.text.alignH = alignH;
            l.text.alignV = alignV;
        }
        refs.emplace_back(25u, styleCRC);
    }

    static CMChunk BuildLibrary(const std::vector<RZHudWindow>& windows, bool littleEndian = false)
    {
        CMChunk lib = CMChunk::Container(Gen_HUDLibrary, 4);
        lib.isLittleEndian = littleEndian;
        CMChunk info = CMChunk::Leaf(GenSub_Info, 1, {});
        info.isLittleEndian = littleEndian;
        info.Append<uint32_t>(static_cast<uint32_t>(windows.size()));
        lib.AddChild(std::move(info));
        for (const RZHudWindow& w : windows)
            lib.AddChild(w.Build(littleEndian));
        return lib;
    }

    static std::vector<RZHudWindow> All(const PKPackage& pkg)
    {
        std::vector<RZHudWindow> out;
        for (const CMChunk& root : pkg.rootChunks)
            if (root.GetMaskedID() == Root)
                for (const CMChunk* lib : root.FindChildren(Gen_HUDLibrary))
                    for (const CMChunk* res : lib->FindChildren(GenSub_Resource))
                        out.push_back(Parse(*res));
        return out;
    }
};
