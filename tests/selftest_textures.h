#pragma once

#include <cstdio>
#include <cstring>
#include <vector>

#include "../BaseUtils/BUPng.h"
#include "../Graphics/XenosTexture.h"
#include "../Package/PKPackageBuilder.h"
#include "../Resource/RZTexture.h"

namespace selftest
{
    inline void TestTextures(void (*Check)(bool, const char*))
    {

        const XenosTexture::D3DFormat dxt5 = XenosTexture::D3DFormat::Decode(XenosTexture::D3DFormat::kDXT5Tiled);
        Check(dxt5.format == XenosTexture::kFormat_DXT4_5 && dxt5.tiled && dxt5.endian == 1 && dxt5.BytesPerBlock() == 16,
              "0x1A207F54 decodes as tiled 8in16 DXT5");
        const XenosTexture::D3DFormat argb = XenosTexture::D3DFormat::Decode(XenosTexture::D3DFormat::kA8R8G8B8Tiled);
        Check(argb.format == XenosTexture::kFormat_8_8_8_8 && argb.endian == 2 && argb.BytesPerBlock() == 4,
              "0x18287F86 decodes as tiled 8in32 8_8_8_8");
        Check(XenosTexture::TiledSurfaceSize(512, 336, dxt5) == 196608, "512x336 DXT5 pads to 12 tiles of 16 KB");
        Check(XenosTexture::TiledSurfaceSize(32, 32, dxt5) == 16384, "a 32x32 DXT5 texture is one 16 KB tile");

        {
            const uint32_t pitch = 32;
            std::vector<uint8_t> seen(32 * 32 * 16, 0);
            bool inRange = true, unique = true;
            for (uint32_t y = 0; y < 32; ++y)
                for (uint32_t x = 0; x < 32; ++x)
                {
                    const uint32_t off = XenosTexture::TiledOffset2D(x, y, pitch, 4);
                    if (off + 16 > seen.size() || (off & 15)) { inRange = false; continue; }
                    if (seen[off]) unique = false;
                    seen[off] = 1;
                }
            Check(inRange && unique, "TiledOffset2D maps a 32x32-block tile onto itself once");
        }

        {
            const uint32_t W = 40, H = 24;
            std::vector<uint8_t> rgba(W * H * 4);
            for (uint32_t i = 0; i < W * H; ++i)
            {
                rgba[i * 4 + 0] = uint8_t(i); rgba[i * 4 + 1] = uint8_t(i * 3); rgba[i * 4 + 2] = uint8_t(i * 7); rgba[i * 4 + 3] = 255;
            }
            const RZTexture t = RZTexture::FromRGBA8("SelfTest_8888", rgba.data(), W, H);
            Check(t.gpuData.size() == 16384, "8888 texture data is padded to 16 KB");
            Check(t.DecodeLevel0() == rgba, "8888 tile/untile/decode round-trips");
        }

        {
            std::vector<uint8_t> rgba(16 * 8 * 4);
            for (size_t i = 0; i < rgba.size(); ++i)
                rgba[i] = uint8_t(i * 13);
            const std::vector<uint8_t> png = BUPng::EncodeRGBA8(rgba.data(), 16, 8);
            const BUPng::Image img = BUPng::Decode(png.data(), png.size());
            Check(img.width == 16 && img.height == 8 && img.rgba == rgba, "PNG encode/decode round-trips");
        }

        {
            const uint8_t block[8] = { 0x00, 0xF8, 0xE0, 0x07, 0x55, 0x55, 0x00, 0x00 };
            uint8_t out[64];
            XenosTexture::DecodeDXT1Block(block, out);
            Check(out[0] == 0 && out[1] == 255 && out[2] == 0 && out[8 * 4 + 0] == 255 && out[8 * 4 + 1] == 0,
                  "DXT1 block decodes endpoint colours");
        }

        {
            PKPackageBuilder b;
            b.packageId = 0x7EE;
            b.packageName = "SelfTest";
            std::vector<uint8_t> rgba(8 * 8 * 4, 200);
            b.textures.push_back(RZTexture::FromRGBA8("SelfTest_Tex", rgba.data(), 8, 8));
            const PKPackage pkg = b.Build();
            const std::vector<uint8_t> bytes = pkg.Serialize();
            PKPackage back;
            back.ReadFromMemory(bytes);
            const std::vector<CMChunkResourceHeader> headers = RZTexture::Headers(back);
            bool ok = headers.size() == 1;
            if (ok)
            {
                const RZTexture t = RZTexture::Load(back, headers[0]);
                ok = t.name == "SelfTest_Tex" && t.desc.width == 8 && t.DecodeLevel0() == rgba;
            }
            Check(ok, "package builder links the texture header to its post-load data");
        }
    }
}
