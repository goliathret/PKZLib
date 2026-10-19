#pragma once
#include "AnimationMath.h"
#include "../../Package/CMChunk.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace pkzlib::animation_detail
{
struct Reader
{
    const std::vector<uint8_t>& data;
    bool little;
    size_t at = 0, limit;
    Reader(const std::vector<uint8_t>& bytes, bool le) : data(bytes), little(le), limit(bytes.size()) {}
    void Require(size_t n) const
    {
        if (at > limit || n > limit - at)
            throw std::runtime_error("Animation data is truncated.");
    }
    void Skip(size_t n)
    {
        Require(n);
        at += n;
    }
    void Align(size_t n) { Skip((n - at % n) % n); }
    uint8_t U8()
    {
        Require(1);
        return data[at++];
    }
    uint16_t U16()
    {
        uint16_t a = U8(), b = U8();
        return little ? a | (b << 8) : (a << 8) | b;
    }
    uint32_t U32()
    {
        uint32_t a = U16(), b = U16();
        return little ? a | (b << 16) : (a << 16) | b;
    }
    float F32()
    {
        const uint32_t bits = U32();
        float value;
        std::memcpy(&value, &bits, 4);
        if (!std::isfinite(value))
            throw std::runtime_error("Animation contains a non-finite value.");
        return value;
    }
};
inline const CMChunk* Find(const CMChunk& chunk, uint32_t id)
{
    if (chunk.GetMaskedID() == id)
        return &chunk;
    for (const auto& child : chunk.children)
        if (auto* found = Find(child, id))
            return found;
    return nullptr;
}
} // namespace pkzlib::animation_detail
