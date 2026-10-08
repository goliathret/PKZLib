#pragma once

#include <cstdint>
#include <vector>

// PNG <-> RGBA8, on stb (BUPng.cpp).
namespace BUPng
{
    struct Image
    {
        uint32_t width = 0, height = 0;
        std::vector<uint8_t> rgba;
    };

    std::vector<uint8_t> EncodeRGBA8(const uint8_t* rgba, uint32_t width, uint32_t height);

    Image Decode(const uint8_t* bytes, size_t size);

    inline Image Decode(const std::vector<uint8_t>& file) { return Decode(file.data(), file.size()); }
}
