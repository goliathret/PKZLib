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
    static constexpr size_t kPassDescriptionSize = 176;
    static constexpr size_t kPassHeaderSize = 188;
    static constexpr uint32_t kPassMarker = 0x55;
    static constexpr size_t kDeclarationHeaderSize = 52;
    static constexpr size_t kDeclarationCountOffset = 24;
    static constexpr size_t kVertexElementSize = 12;
    static constexpr size_t kVertexShaderObjectSize = 0x368;
    static constexpr size_t kPixelShaderObjectSize = 0x28;
    static constexpr uint32_t kShaderContainerMagic = 0x102A1100u;
    static constexpr uint32_t kDeclarationVariantMask = 0xD000u;
    static constexpr uint32_t kTechniqueCount = 11;
    static constexpr uint32_t kVariantCount = 5;
    static constexpr uint8_t kNoSampler = 0xFF;

    enum Technique : uint32_t
    {
        kTechnique0G0SM = 0,
        kTechnique2G1SM = 1,
        kTechnique4G1SM = 2,
        kTechnique6G4SM = 3,
        kTechnique1G1I1SM = 4,
        kTechniqueDepth = 5,
        kTechniqueConstantColor = 6,
        kTechniqueShadowReceiver = 7,
        kTechniqueZPassNormal = 8,
        kTechniquePrelighted = 9,
        kTechniqueSSEdgeExtrude = 10,
    };

    enum Variant : uint32_t
    {
        kVariantRigid = 0,
        kVariantSkinned = 1,
        kVariantMorph = 2,
        kVariantSkinnedMorph = 3,
        kVariantInstanced = 4,
    };

    struct Sampler
    {
        uint32_t crc = 0;
        uint8_t clamp = 0;
        uint8_t filterFlags = 0;
        uint32_t dynamicIndex = 0;
        std::string sourcePath;
    };

    struct Stage
    {
        enum Flags : uint16_t
        {
            kVertexColor = 1,
            kVertexAlpha = 2,
            kEnvironmentMap = 4,
            kDescriptorBit19 = 0x800,
        };

        enum BlendMode : uint8_t
        {
            kReplace = 0,
            kAdd = 1,
            kSubtract = 2,
            kBlend = 3,
        };

        float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        float param = 0.0f;
        uint16_t flags = 0;
        uint8_t blendMode = kReplace;
        uint8_t sampler = kNoSampler;
        uint8_t uvSet = 0;
        uint8_t maskSampler = kNoSampler;
        uint8_t maskUvSet = 0;
        char maskComponent = 0;
    };

    struct Header
    {
        float specularIntensity = 0.0f;
        float specularPower = 0.0f;
        float normalMapScale = 0.0f;
        float normalMap2Scale = 0.0f;
        float emissiveIntensity = 0.0f;
        uint8_t alphaRef = 0;
        float distortionAlpha = 0.0f;
        float distortionScale = 0.0f;
        uint32_t techniqueCount = 0;
        uint32_t variantCount = 0;
        uint32_t specularColor = 0;
        float edgeExtrusionBias = 0.0f;
        uint32_t edgeColor = 0;
        uint8_t lightmapSampler = kNoSampler;
        float depthBias = 0.0f;
        float rimLightParam = 0.0f;
        float rimLightColor[4] = {};
        float edgeExtrusionThickness = 0.0f;
        uint32_t runtimeFlags = 0;
    };

    static constexpr uint32_t kFlagText3D = 0x10;
    static constexpr uint32_t kFlagDepthBias = 0x100;
    static constexpr uint32_t kFlagAlphaWrite = 0x200;
    static constexpr uint32_t kFlagDistortion = 0x400000;
    static constexpr uint32_t kFlagEmissive = 0x04000000;
    static constexpr uint32_t kFlagRimLight = 0x08000000;
    static constexpr uint32_t kFlagNormalMap = 0x10000000;

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
        uint32_t technique = 0;
        uint32_t variant = 0;
        std::vector<VertexElement> elements;
    };

    struct ShaderRecord
    {
        size_t virtualOffset = 0;
        size_t virtualSize = 0;
        size_t physicalOffset = 0;
        size_t physicalSize = 0;
        size_t objectSize = 0;
        uint64_t key = 0;
    };

    struct Pass
    {
        uint32_t technique = 0;
        uint32_t variant = 0;
        uint32_t format = 0;
        ShaderRecord vertex;
        ShaderRecord pixel;
        uint32_t marker = kPassMarker;
    };

    const CMChunk* source = nullptr;
    std::string name;
    uint32_t flags = 0;
    Header header;
    std::vector<Sampler> samplers;
    std::vector<Stage> stages;
    std::vector<Pass> passes;
    std::vector<VertexDeclaration> declarations;
    float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    uint32_t textureCRC = 0;
    uint8_t uvSet = 0;
    uint8_t clamp = 0;

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

        const uint32_t samplerCount = reader.Word(100);
        const uint32_t stageCount = reader.Word(104);
        const uint32_t techniqueCount = reader.Word(108);
        const uint32_t variantCount = reader.Word(112);
        if (samplerCount > 12 || stageCount > 4 || techniqueCount > kTechniqueCount || variantCount > kVariantCount)
            throw std::runtime_error("CMChunkMaterialInfo: unsupported sampler, stage or pass counts in " + material.name);
        material.ReadHeader(reader);

        const size_t stageStart = kHeaderSize + size_t(samplerCount) * kSamplerSize;
        reader.Skip(size_t(samplerCount) * kSamplerSize + size_t(stageCount) * kStageSize);

        for (uint32_t i = 0; i < samplerCount; ++i)
        {
            const size_t at = kHeaderSize + size_t(i) * kSamplerSize;
            material.samplers.push_back({ reader.Word(at), reader.Byte(at + 4), reader.Byte(at + 5), reader.Word(at + 8),
                                          reader.String(at + 12, 128) });
        }

        for (uint32_t i = 0; i < stageCount; ++i)
        {
            const size_t at = stageStart + size_t(i) * kStageSize;
            Stage stage;
            for (int c = 0; c < 4; ++c)
                stage.color[c] = reader.RawFloat(at + size_t(c) * 4);
            stage.param = reader.RawFloat(at + 16);
            stage.flags = reader.Half(at + 20);
            stage.blendMode = reader.Byte(at + 22);
            stage.sampler = reader.Byte(at + 23);
            stage.uvSet = reader.Byte(at + 24);
            stage.maskSampler = reader.Byte(at + 25);
            stage.maskUvSet = reader.Byte(at + 26);
            stage.maskComponent = static_cast<char>(reader.Byte(at + 27));
            material.stages.push_back(stage);
        }

        if (!material.stages.empty())
        {
            const Stage& stage = material.stages[0];
            for (int c = 0; c < 4; ++c)
                material.color[c] = reader.Float(stageStart + size_t(c) * 4);
            material.uvSet = stage.uvSet;
            if (stage.sampler < samplerCount)
            {
                material.textureCRC = material.samplers[stage.sampler].crc;
                material.clamp = material.samplers[stage.sampler].clamp;
            }
        }

        for (uint32_t i = 0; i < techniqueCount * variantCount; ++i)
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
        if (!source || record.virtualSize < record.objectSize)
            throw std::runtime_error("CMChunkMaterialInfo: invalid shader object image in " + name);

        Reader reader(*source);
        reader.Require(record.virtualOffset, record.virtualSize);
        reader.Require(record.physicalOffset, record.physicalSize);

        const size_t start = record.virtualOffset + record.objectSize;
        const size_t size = record.virtualSize - record.objectSize;
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

        uint16_t Half(size_t at) const
        {
            Require(at, 2);
            return chunk.Read<uint16_t>(at);
        }

        uint64_t Long(size_t at) const
        {
            Require(at, 8);
            return chunk.Read<uint64_t>(at);
        }

        float RawFloat(size_t at) const
        {
            Require(at, 4);
            return chunk.Read<float>(at);
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

    void ReadHeader(const Reader& reader)
    {
        flags = reader.Word(32);
        header.specularIntensity = reader.RawFloat(44);
        header.specularPower = reader.RawFloat(48);
        header.normalMapScale = reader.RawFloat(56);
        header.normalMap2Scale = reader.RawFloat(60);
        header.emissiveIntensity = reader.RawFloat(64);
        header.alphaRef = reader.Byte(75);
        header.distortionAlpha = reader.RawFloat(92);
        header.distortionScale = reader.RawFloat(96);
        header.techniqueCount = reader.Word(108);
        header.variantCount = reader.Word(112);
        header.specularColor = reader.Word(120);
        header.edgeExtrusionBias = reader.RawFloat(124);
        header.edgeColor = reader.Word(128);
        header.lightmapSampler = reader.Byte(132);
        header.depthBias = reader.RawFloat(212);
        header.rimLightParam = reader.RawFloat(216);
        for (size_t i = 0; i < 4; ++i)
            header.rimLightColor[i] = reader.RawFloat(220 + i * 4);
        header.edgeExtrusionThickness = reader.RawFloat(236);
        header.runtimeFlags = reader.Word(240);
    }

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
        pass.marker = reader.Word(passStart + kPassDescriptionSize);
        pass.technique = reader.Word(passStart + 180);
        pass.variant = reader.Word(passStart + 184);

        if (declarationSize >= kDeclarationHeaderSize)
        {
            const uint32_t count = reader.Word(declarationStart + kDeclarationCountOffset);
            if (count > 32 || count > (declarationSize - kDeclarationHeaderSize) / kVertexElementSize)
                throw std::runtime_error("CMChunkMaterialInfo: invalid vertex declaration in " + name);

            VertexDeclaration declaration;
            declaration.format = pass.format;
            declaration.technique = pass.technique;
            declaration.variant = pass.variant;
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

    static ShaderRecord ReadShader(Reader& reader, size_t objectSize, bool alwaysPresent)
    {
        ShaderRecord record;
        record.key = reader.Next();
        if (record.key == 0)
        {
            record.key = reader.Long(reader.position);
            reader.Skip(8);
        }
        const uint32_t virtualSize = reader.Next();
        const uint32_t physicalSize = reader.Next();

        record.objectSize = objectSize;
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
