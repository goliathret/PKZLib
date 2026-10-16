#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "../Package/CMChunk.h"

class CMChunkMaterialInfo
{
public:
    static constexpr uint16_t kVersionEOT = 64; // Need to Separate this at some point
    static constexpr size_t kHeaderSize = 244;
    static constexpr size_t kSamplerSize = 140;
    static constexpr size_t kStageSize = 32;
    static constexpr size_t kPassHeaderSize = 188;
    static constexpr size_t kDeclarationHeaderSize = 52;
    static constexpr size_t kVertexElementSize = 12;
    static constexpr size_t kVertexShaderObjectSize = 0x368;
    static constexpr size_t kPixelShaderObjectSize = 0x28;
    static constexpr uint32_t kShaderContainerMagic = 0x102A1100u;
    static constexpr uint32_t kDeclarationVariantMask = 0xD000u;

    struct Sampler
    {
        uint32_t crc = 0;
        uint8_t addressMode = 0;
        std::string sourcePath;
    };

    struct VertexElement
    {
        enum Usage : uint8_t
        {
            kUsagePosition = 0,
            kUsageNormal = 3,
            kUsageTexCoord = 5,
        };

        enum Format : uint32_t
        {
            kFormat_10_11_11 = 16,
            kFormat_16_16 = 25,
            kFormat_32_32_FLOAT = 37,
            kFormat_32_32_32_32_FLOAT = 38,
            kFormat_32_32_32_FLOAT = 57,
        };

        static constexpr uint32_t kFormatMask = 0x3F;
        static constexpr uint32_t kSigned = 0x100;
        static constexpr uint32_t kInteger = 0x200;

        uint16_t stream = 0;
        uint16_t offset = 0;
        uint32_t type = 0;
        uint8_t usage = 0;
        uint8_t index = 0;

        uint32_t GetFormat() const { return type & kFormatMask; }
        bool IsSigned() const { return (type & kSigned) != 0; }
        bool IsInteger() const { return (type & kInteger) != 0; }

        size_t Size() const
        {
            switch (GetFormat())
            {
            case kFormat_32_32_32_FLOAT: return 12;
            case kFormat_32_32_FLOAT: return 8;
            case kFormat_32_32_32_32_FLOAT: return 16;
            default: return 4;
            }
        }
    };

    struct VertexDeclaration
    {
        uint32_t format = 0;
        uint32_t slot = 0;
        uint32_t set = 0;
        std::vector<VertexElement> elements;
    };

    struct ShaderRecord
    {
        size_t virtualOffset = 0;
        size_t virtualSize = 0;
        size_t physicalOffset = 0;
        size_t physicalSize = 0;
        size_t headerSize = 0;
    };

    struct Pass
    {
        uint32_t slot = 0;
        uint32_t set = 0;
        uint32_t format = 0;
        ShaderRecord vertex;
        ShaderRecord pixel;
    };

    const CMChunk* source = nullptr;
    std::string name;
    uint32_t flags = 0;
    std::vector<Sampler> samplers;
    std::vector<Pass> passes;
    std::vector<VertexDeclaration> declarations;
    float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    uint32_t textureCRC = 0;
    uint8_t uvSet = 0;
    uint8_t addressMode = 0;

    static CMChunkMaterialInfo Parse(const CMChunk& chunk)
    {
        if (chunk.GetMaskedID() != Geo_MaterialInfo)
            throw std::runtime_error("CMChunkMaterialInfo: not a Geo_MaterialInfo chunk");
        if (chunk.version != kVersionEOT)
            throw std::runtime_error("CMChunkMaterialInfo: unsupported version " + std::to_string(chunk.version) +
                                     " (expected EOT v64)");

        Reader reader(chunk);
        reader.Skip(kHeaderSize);

        CMChunkMaterialInfo material;
        material.source = &chunk;
        material.name = reader.String(0, 32);
        material.flags = reader.Word(32);

        const uint32_t samplerCount = reader.Word(100);
        const uint32_t stageCount = reader.Word(104);
        const uint32_t slotCount = reader.Word(108);
        const uint32_t setCount = reader.Word(112);
        if (samplerCount > 12 || stageCount > 4 || slotCount > 32 || setCount > 8)
            throw std::runtime_error("CMChunkMaterialInfo: unsupported sampler, stage or pass counts in " + material.name);

        const size_t stageStart = kHeaderSize + size_t(samplerCount) * kSamplerSize;
        reader.Skip(size_t(samplerCount) * kSamplerSize + size_t(stageCount) * kStageSize);

        for (uint32_t i = 0; i < samplerCount; ++i)
        {
            const size_t at = kHeaderSize + size_t(i) * kSamplerSize;
            material.samplers.push_back({ reader.Word(at), reader.Byte(at + 4), reader.String(at + 12, 128) });
        }

        if (stageCount)
        {
            for (int i = 0; i < 4; ++i)
                material.color[i] = reader.Float(stageStart + size_t(i) * 4);
            const uint8_t sampler = reader.Byte(stageStart + 23);
            material.uvSet = reader.Byte(stageStart + 24);
            if (sampler < samplerCount)
            {
                material.textureCRC = material.samplers[sampler].crc;
                material.addressMode = material.samplers[sampler].addressMode;
            }
        }

        for (uint32_t i = 0; i < slotCount * setCount; ++i)
            material.ReadPass(reader);
        return material;
    }

    const VertexDeclaration* FindDeclaration(uint32_t vertexFormat) const
    {
        for (const VertexDeclaration& declaration : declarations)
            if (declaration.format == vertexFormat)
                return &declaration;
        for (const VertexDeclaration& declaration : declarations)
            if ((declaration.format & kDeclarationVariantMask) == (vertexFormat & kDeclarationVariantMask))
                return &declaration;
        return nullptr;
    }

    std::vector<uint8_t> ShaderContainer(const ShaderRecord& record) const
    {
        if (!record.virtualSize)
            return {};
        if (!source || record.virtualSize < record.headerSize)
            throw std::runtime_error("CMChunkMaterialInfo: invalid shader object image in " + name);

        Reader reader(*source);
        reader.Require(record.virtualOffset, record.virtualSize);
        reader.Require(record.physicalOffset, record.physicalSize);

        const size_t start = record.virtualOffset + record.headerSize;
        const size_t size = record.virtualSize - record.headerSize;
        if (size < 36 || (reader.Word(start) & ~1u) != kShaderContainerMagic || reader.Word(start + 4) != size ||
            reader.Word(start + 8) != record.physicalSize)
            throw std::runtime_error("CMChunkMaterialInfo: unsupported shader container layout in " + name);

        const auto& bytes = source->data;
        std::vector<uint8_t> container(bytes.begin() + std::ptrdiff_t(start), bytes.begin() + std::ptrdiff_t(start + size));
        container.insert(container.end(), bytes.begin() + std::ptrdiff_t(record.physicalOffset),
                         bytes.begin() + std::ptrdiff_t(record.physicalOffset + record.physicalSize));
        return container;
    }

private:
    class Reader
    {
    public:
        explicit Reader(const CMChunk& chunk) : chunk(chunk) {}

        size_t position = 0;

        void Require(size_t at, size_t count) const
        {
            if (at > chunk.data.size() || count > chunk.data.size() - at)
                throw std::runtime_error("CMChunkMaterialInfo: truncated material data");
        }

        void Skip(size_t count)
        {
            Require(position, count);
            position += count;
        }

        void Align()
        {
            Skip((4 - position % 4) % 4);
        }

        uint32_t Next()
        {
            const uint32_t value = Word(position);
            position += 4;
            return value;
        }

        uint32_t Word(size_t at) const
        {
            Require(at, 4);
            return chunk.Read<uint32_t>(at);
        }

        uint8_t Byte(size_t at) const
        {
            Require(at, 1);
            return chunk.data[at];
        }

        float Float(size_t at) const
        {
            Require(at, 4);
            const float value = chunk.Read<float>(at);
            if (!std::isfinite(value))
                throw std::runtime_error("CMChunkMaterialInfo: non-finite material color");
            return value;
        }

        std::string String(size_t at, size_t capacity) const
        {
            Require(at, capacity);
            const char* text = reinterpret_cast<const char*>(chunk.data.data() + at);
            return std::string(text, strnlen(text, capacity));
        }

    private:
        const CMChunk& chunk;
    };

    void ReadPass(Reader& reader)
    {
        const size_t passStart = reader.position;
        reader.Skip(kPassHeaderSize);
        const uint32_t declarationSize = reader.Next();
        const size_t declarationStart = reader.position;
        reader.Skip(declarationSize);
        reader.Align();

        Pass pass;
        pass.format = reader.Word(passStart);
        pass.slot = reader.Word(passStart + 180);
        pass.set = reader.Word(passStart + 184);

        if (declarationSize >= kDeclarationHeaderSize)
        {
            const uint32_t count = reader.Word(declarationStart + 24);
            if (count > 32 || count > (declarationSize - kDeclarationHeaderSize) / kVertexElementSize)
                throw std::runtime_error("CMChunkMaterialInfo: invalid vertex declaration in " + name);

            VertexDeclaration declaration;
            declaration.format = pass.format;
            declaration.slot = pass.slot;
            declaration.set = pass.set;
            for (uint32_t i = 0; i < count; ++i)
            {
                const size_t at = declarationStart + kDeclarationHeaderSize + size_t(i) * kVertexElementSize;
                const uint32_t streamOffset = reader.Word(at);
                declaration.elements.push_back({ uint16_t(streamOffset >> 16), uint16_t(streamOffset), reader.Word(at + 4),
                                                 reader.Byte(at + 9), reader.Byte(at + 10) });
            }
            declarations.push_back(std::move(declaration));
        }

        pass.vertex = ReadShader(reader, kVertexShaderObjectSize, true);
        pass.pixel = ReadShader(reader, kPixelShaderObjectSize, false);
        passes.push_back(pass);
    }

    static ShaderRecord ReadShader(Reader& reader, size_t headerSize, bool alwaysPresent)
    {
        if (reader.Next() == 0)
            reader.Skip(8);
        const uint32_t virtualSize = reader.Next();
        const uint32_t physicalSize = reader.Next();

        ShaderRecord record;
        record.headerSize = headerSize;
        if (!alwaysPresent && !virtualSize)
            return record;

        record.virtualOffset = reader.position;
        record.virtualSize = virtualSize;
        reader.Skip(virtualSize);
        reader.Align();
        record.physicalOffset = reader.position;
        record.physicalSize = physicalSize;
        reader.Skip(physicalSize);
        reader.Align();
        return record;
    }
};
