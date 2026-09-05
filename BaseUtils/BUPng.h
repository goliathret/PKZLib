#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <zlib.h>

namespace BUPng
{
    inline void PutBE32(std::vector<uint8_t>& v, uint32_t x)
    {
        v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
    }
    inline uint32_t GetBE32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

    inline void WriteChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
    {
        PutBE32(out, static_cast<uint32_t>(data.size()));
        const size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        const uint32_t crc = static_cast<uint32_t>(crc32(0, out.data() + start, static_cast<uInt>(out.size() - start)));
        PutBE32(out, crc);
    }

    inline std::vector<uint8_t> EncodeRGBA8(const uint8_t* rgba, uint32_t width, uint32_t height)
    {
        std::vector<uint8_t> raw;
        raw.reserve(size_t(height) * (size_t(width) * 4 + 1));
        for (uint32_t y = 0; y < height; ++y)
        {
            raw.push_back(0);
            raw.insert(raw.end(), rgba + size_t(y) * width * 4, rgba + size_t(y + 1) * width * 4);
        }
        uLongf destLen = compressBound(static_cast<uLong>(raw.size()));
        std::vector<uint8_t> idat(destLen);
        if (compress2(idat.data(), &destLen, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK)
            throw std::runtime_error("BUPng: compress failed");
        idat.resize(destLen);

        std::vector<uint8_t> out = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        std::vector<uint8_t> ihdr;
        PutBE32(ihdr, width);
        PutBE32(ihdr, height);
        ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });
        WriteChunk(out, "IHDR", ihdr);
        WriteChunk(out, "IDAT", idat);
        WriteChunk(out, "IEND", {});
        return out;
    }

    struct Image
    {
        uint32_t width = 0, height = 0;
        std::vector<uint8_t> rgba;
    };

    inline uint8_t Paeth(int a, int b, int c)
    {
        const int p = a + b - c;
        const int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
        if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
        if (pb <= pc) return static_cast<uint8_t>(b);
        return static_cast<uint8_t>(c);
    }

    inline Image Decode(const uint8_t* data, size_t size)
    {
        static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        if (size < 8 || std::memcmp(data, sig, 8) != 0)
            throw std::runtime_error("BUPng: not a PNG");
        uint32_t width = 0, height = 0;
        uint8_t depth = 0, color = 0, interlace = 0;
        std::vector<uint8_t> idat;
        size_t pos = 8;
        while (pos + 8 <= size)
        {
            const uint32_t len = GetBE32(data + pos);
            const char* type = reinterpret_cast<const char*>(data + pos + 4);
            const uint8_t* body = data + pos + 8;
            if (pos + 12 + len > size)
                throw std::runtime_error("BUPng: truncated chunk");
            if (!std::memcmp(type, "IHDR", 4))
            {
                width = GetBE32(body); height = GetBE32(body + 4);
                depth = body[8]; color = body[9]; interlace = body[12];
            }
            else if (!std::memcmp(type, "IDAT", 4))
                idat.insert(idat.end(), body, body + len);
            else if (!std::memcmp(type, "IEND", 4))
                break;
            pos += 12 + len;
        }
        if (!width || !height || depth != 8 || interlace != 0)
            throw std::runtime_error("BUPng: only 8-bit non-interlaced PNGs are supported");
        uint32_t channels;
        switch (color)
        {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: throw std::runtime_error("BUPng: palette PNGs are not supported");
        }
        const size_t stride = size_t(width) * channels;
        std::vector<uint8_t> raw(size_t(height) * (stride + 1));
        uLongf destLen = static_cast<uLongf>(raw.size());
        if (uncompress(raw.data(), &destLen, idat.data(), static_cast<uLong>(idat.size())) != Z_OK || destLen != raw.size())
            throw std::runtime_error("BUPng: inflate failed");

        std::vector<uint8_t> pixels(size_t(height) * stride);
        std::vector<uint8_t> prev(stride, 0);
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint8_t filter = raw[y * (stride + 1)];
            const uint8_t* src = raw.data() + y * (stride + 1) + 1;
            uint8_t* dst = pixels.data() + y * stride;
            for (size_t i = 0; i < stride; ++i)
            {
                const int a = i >= channels ? dst[i - channels] : 0;
                const int b = prev[i];
                const int c = i >= channels ? prev[i - channels] : 0;
                int v = src[i];
                switch (filter)
                {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += Paeth(a, b, c); break;
                default: throw std::runtime_error("BUPng: bad filter");
                }
                dst[i] = static_cast<uint8_t>(v);
            }
            std::memcpy(prev.data(), dst, stride);
        }

        Image img;
        img.width = width;
        img.height = height;
        img.rgba.resize(size_t(width) * height * 4);
        for (size_t i = 0; i < size_t(width) * height; ++i)
        {
            const uint8_t* p = pixels.data() + i * channels;
            uint8_t* q = img.rgba.data() + i * 4;
            switch (channels)
            {
            case 1: q[0] = q[1] = q[2] = p[0]; q[3] = 255; break;
            case 2: q[0] = q[1] = q[2] = p[0]; q[3] = p[1]; break;
            case 3: q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = 255; break;
            default: std::memcpy(q, p, 4); break;
            }
        }
        return img;
    }
}
