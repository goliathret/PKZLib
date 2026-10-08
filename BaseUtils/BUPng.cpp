#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // stb_image_write uses sprintf
#endif

#include "BUPng.h"

#include <climits>
#include <stdexcept>
#include <string>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace BUPng
{
    std::vector<uint8_t> EncodeRGBA8(const uint8_t* rgba, uint32_t width, uint32_t height)
    {
        if (width > INT_MAX / 4 || height > INT_MAX)
            throw std::runtime_error("BUPng: image too large");
        std::vector<uint8_t> out;
        auto append = [](void* context, void* data, int size) {
            auto* bytes = static_cast<const uint8_t*>(data);
            static_cast<std::vector<uint8_t>*>(context)->insert(static_cast<std::vector<uint8_t>*>(context)->end(),
                                                                 bytes, bytes + size);
        };
        if (!stbi_write_png_to_func(append, &out, int(width), int(height), 4, rgba, int(width) * 4))
            throw std::runtime_error("BUPng: PNG encoding failed");
        return out;
    }

    Image Decode(const uint8_t* bytes, size_t size)
    {
        if (size > size_t(INT_MAX))
            throw std::runtime_error("BUPng: file too large");
        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(bytes, int(size), &width, &height, &channels, 4);
        if (!pixels)
            throw std::runtime_error(std::string("BUPng: ") + stbi_failure_reason());
        Image img;
        img.width = uint32_t(width);
        img.height = uint32_t(height);
        img.rgba.assign(pixels, pixels + size_t(width) * size_t(height) * 4);
        stbi_image_free(pixels);
        return img;
    }
}
