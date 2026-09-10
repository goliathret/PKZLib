#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "../BaseUtils/BUAABB.h"
#include "../BaseUtils/BUBSpline.h"
#include "../BaseUtils/BUBezier.h"
#include "../BaseUtils/BUBox.h"
#include "../BaseUtils/BUCRC.h"
#include "../BaseUtils/BUDecompress.h"
#include "../BaseUtils/BUGL.h"
#include "../BaseUtils/BUMath.h"
#include "../BaseUtils/BUMatrix.h"
#include "../BaseUtils/BURect.h"
#include "../BaseUtils/BUSphere.h"
#include "../BaseUtils/BUTruncate.h"
#include "../GameObj/GO3DObj.h"
#include "../Geo/GRVtxBuf.h"
#include "../Geo/GeomInfo.h"
#include "../Geo/GeomMesh.h"
#include "../Geo/MaterialInfo.h"
#include "../Graphics/GRClippable.h"
#include "../Graphics/TexturePreload.h"
#include "../Package/CMChunk.h"
#include "../Package/CMChunkTypes.h"
#include "../Package/PKChunkBuilders.h"
#include "../Package/PKPackage.h"
#include "../Resource/RZAnimMap.h"
#include "../Resource/RZCutscene.h"
#include "../Resource/RZHAnim.h"
#include "../Resource/RZHierarchy.h"
#include "../Resource/RZResource.h"
#include "../Resource/RZStringTable.h"
#include "../Resource/ResourceHeader.h"
#include "../Resource/ResourceTypes.h"
#include "../Resource/RZFont.h"
#include "../Resource/RZHud.h"
#include "selftest_textures.h"

namespace
{
    int gFailures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? " ok " : "FAIL", what);
        if (!ok)
            ++gFailures;
    }

    void TestChunkRoundTrip()
    {
        for (bool le : { false, true })
        {
            for (bool wide : { false, true })
            {
                CMChunk root = CMChunk::Container(Root, 1, wide);
                root.isLittleEndian = le;
                CMChunk leaf = CMChunk::Leaf(Gen_Languages, 1, {}, wide);
                leaf.isLittleEndian = le;
                leaf.Append<uint32_t>(1);
                leaf.Append<uint32_t>(2);
                root.AddChild(leaf);
                CMChunk inner = CMChunk::Container(Gen_StringTableLibrary, 4, wide);
                inner.AddChild(CMChunk::Leaf(GenSub_Info, 1, { 0, 0, 0, 3 }, wide));
                root.AddChild(inner);

                std::ostringstream out(std::ios::binary);
                root.Write(out, le);
                const std::string bytes = out.str();
                const size_t hdr = wide ? 16 : 12;
                Check(bytes.size() == hdr + (hdr + 8) + (hdr + hdr + 4), "chunk serialized size");
                Check((static_cast<uint8_t>(bytes[le ? 3 : 0]) == 0x80) == wide, "id flag byte follows the length width");

                std::istringstream in(bytes, std::ios::binary);
                CMChunk back = CMChunk::Read(in, le);
                Check(back.GetMaskedID() == Root && back.children.size() == 2, "chunk tree reads back");
                Check(back.lengthFieldSize == (wide ? 8 : 4), "length field width detected from the decoded id");
                Check(back.length == root.PayloadSize(), "container length recomputed from children");

                back.FindChild(Gen_Languages)->SetData({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 });
                std::ostringstream out2(std::ios::binary);
                back.Write(out2, le);
                Check(out2.str().size() == bytes.size() + 4, "parent length follows a resized leaf");
            }
        }
    }

    void TestContainer()
    {
        std::vector<uint8_t> data(300000);
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<uint8_t>((i * 7) ^ (i >> 5));
        for (bool be32 : { true, false })
        {
            BUCompress::Options opt;
            opt.bigEndian32 = be32;
            const std::vector<uint8_t> packed = BUCompress::Compress(data, opt);
            Check(BUDecompress::IsCompressed(packed.data(), packed.size()), "container magic recognised");
            BUDecompress d;
            d.ParseHeader(packed.data(), packed.size());
            Check(d.mCompHeader.uiUncompFileSize == data.size(), "container records the uncompressed size");
            Check(d.mLookup.size() == (data.size() + 0x7FFF) / 0x8000, "lookup table has one entry per 32K sector");
            const std::vector<uint8_t> back = BUDecompress::Decompress(packed);
            Check(back == data, be32 ? "32-bit big-endian container round-trips" : "64-bit little-endian container round-trips");

            bool lookupOk = true;
            for (size_t j = 0; j < d.mLookup.size(); ++j)
            {
                const uint64_t last = std::min<uint64_t>((j + 1) * 0x8000, data.size()) - 1;
                const uint32_t s = d.mLookup[j];
                if (!(d.SectorStart(s) <= last && last < d.SectorEnd(s)))
                    lookupOk = false;
            }
            Check(lookupOk, "lookup names the sector holding each uncompressed sector's end");
        }
    }

    void TestStringTable()
    {
        RZStringTable t;
        t.name = "SelfTest";
        t.languageMask = 0x1F;
        t.Add("HELLO", RZStringTable::FromUtf8("Hello"));
        t.Add("WORLD", RZStringTable::FromUtf8("W\xC3\xB6rld \xE2\x82\xAC"));
        CMChunk lib = RZStringTable::BuildLibrary({ t });
        std::ostringstream out(std::ios::binary);
        lib.Write(out, false);
        std::istringstream in(out.str(), std::ios::binary);
        CMChunk back = CMChunk::Read(in, false);
        std::vector<RZStringTable> tables = RZStringTable::ParseLibrary(back);
        Check(tables.size() == 1 && tables[0].entries.size() == 2, "string library parses back");
        Check(tables[0].name == "SelfTest" && tables[0].languageMask == 0x1F, "resource header carries name and languages");
        Check(tables[0].entries[0].nameCRC == BUCRC().Generate("hello"), "entry CRC is BUCRC of the upper-cased name");
        Check(RZStringTable::ToUtf8(tables[0].entries[1].Text()) == "W\xC3\xB6rld \xE2\x82\xAC", "UTF-16 text survives");
        Check(tables[0].entries[1].subs[0].offsetU16 == 6, "second string starts after 'Hello' + NUL");
        const CMChunk* hdr = back.FindChild(GenSub_Resource)->FindChild(GenSub_ResourceHeader);
        Check(hdr && hdr->data.size() == 88, "resource header is the 88-byte Edge of Time form");
    }

    void TestSphere()
    {
        BUSphere s;
        s.mOrigin.Set(0.0f);
        s.mfRadius = 0.0f;
        BUVector3 p(10.0f, 0.0f, 0.0f);
        s.AddEveryPoints(&p, 1);
        Check(std::fabs(s.mOrigin.x - 5.0f) < 1e-4f && std::fabs(s.mfRadius - 5.0f) < 1e-4f, "Ritter expansion centres between the points");
        Check(s.IsPointInside(BUVector3(9.99f, 0.0f, 0.0f)), "the added point is inside");
        Check(!s.IsIntersectingCone(BUVector3(100, 0, 0), BUVector3(1, 0, 0), 1.0f, 0.0f), "zero-length cone does not divide by zero");
    }

    void TestMatrix()
    {
        const float c = std::cos(0.7f), sn = std::sin(0.7f);
        BUMatrix m(2 * c, 2 * sn, 0, 0,
                   -3 * sn, 3 * c, 0, 0,
                   0, 0, 4, 0,
                   5, 6, 7, 1);
        BUVector3 scale, trans;
        BUQuaternion rot;
        m.Decompose(scale, rot, trans);
        Check(std::fabs(scale.x - 2) < 1e-4f && std::fabs(scale.y - 3) < 1e-4f && std::fabs(scale.z - 4) < 1e-4f, "decompose recovers the scale");
        Check(trans.x == 5 && trans.y == 6 && trans.z == 7, "decompose recovers the translation");
        BUVector3 r;
        rot.Rotate(r, BUVector3(1, 0, 0));
        Check(std::fabs(r.x - c) < 1e-4f && std::fabs(r.y - sn) < 1e-4f, "decompose recovers the rotation");

        BUMatrix neg(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
        neg.Decompose(scale, rot, trans);
        BUMatrix rebuilt = BUMatrix::FromQuat(rot);
        Check(std::fabs(rebuilt.m[0][0] * scale.x - (-1.0f)) < 1e-4f && std::fabs(rebuilt.m[1][1] * scale.y - 1.0f) < 1e-4f &&
                  std::fabs(rebuilt.m[2][2] * scale.z - 1.0f) < 1e-4f,
              "negative determinant keeps the mirrored axis in the scale");
    }

    void TestGeomPrim()
    {
        auto build = [](uint16_t version, bool insertV10Word) {
            CMChunk c = CMChunk::Leaf(Geo_GeomMesh, version, {});
            c.Append<uint32_t>(2);
            c.Append<uint32_t>(0x1000);
            c.Append<uint32_t>(32);
            c.Append<int32_t>(0x11022);
            if (insertV10Word)
                c.Append<uint32_t>(0xFFFFFFFF);
            c.Append<uint32_t>(123);
            c.Append<uint32_t>(0x2000);
            c.Append<uint32_t>(456);
            if (!insertV10Word)
                c.Append<uint32_t>(7);
            c.Append<uint32_t>(3);
            c.Append<uint32_t>(0);
            c.Append<uint32_t>(0);
            c.Append<uint32_t>(0);
            c.Append<uint32_t>(0xFFFFFFFF);
            c.Append<uint32_t>(0x01020000);
            c.Append<uint32_t>(5);
            c.Append<uint32_t>(29);
            c.Append<uint32_t>(34);
            return c;
        };
        RZGeomPrim v7(build(7, false));
        Check(v7.uiVertexCount == 123 && v7.uiFlags == 7 && v7.uiBonePaletteSize == 3, "v7 fields");
        Check(v7.uiBonePalette.size() == 3 && v7.uiBonePalette[2] == 34, "v7 bone palette starts after the material index word");
        Check(v7.GetMaterialIndex(0) == 1 && v7.GetMaterialIndex(1) == 2, "material indices read from the packed word");
        RZGeomPrim v10(build(10, true));
        Check(v10.uiVertexCount == 123 && v10.uiDisplayListSize == 456 && v10.uiBonePaletteSize == 3, "v10 fields");
        Check(v10.uiBonePalette.size() == 3 && v10.uiBonePalette[0] == 5, "v10 bone palette");
        Check(v7.ComputeVertexStride() == 12 + 4 + 4 + 4 + 4, "stride from format bits");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try
    {
        TestChunkRoundTrip();
        TestContainer();
        TestStringTable();
        TestSphere();
        TestMatrix();
        TestGeomPrim();
        selftest::TestTextures(&Check);
    }
    catch (const std::exception& e)
    {
        std::printf("[FAIL] uncaught exception: %s\n", e.what());
        ++gFailures;
    }
    std::printf("%s (%d failure%s)\n", gFailures ? "FAILED" : "PASSED", gFailures, gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
