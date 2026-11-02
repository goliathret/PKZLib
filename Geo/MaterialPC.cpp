#include "MaterialInfo.h"
#include <algorithm>

CMChunkMaterialInfo CMChunkMaterialInfo::ParsePC(const CMChunk& chunk)
{
    const bool shattered = chunk.version == 41;
    const bool tasm2 = chunk.version == 13;
    if (!shattered && chunk.version != 9 && !tasm2)
        throw std::runtime_error("CMChunkMaterialInfo: unsupported PC version " + std::to_string(chunk.version));

    Reader reader(chunk);
    const size_t headerSize = shattered ? 368 : tasm2 ? 280 : 236;
    const size_t stageSize = shattered ? 28 : 44;
    const size_t samplerSize = shattered ? 140 : 152;
    reader.Skip(headerSize);
    const uint32_t samplerCount = shattered ? reader.Word(260) : reader.Byte(tasm2 ? 113 : 105);
    const uint32_t stageCount = shattered ? reader.Word(264) : reader.Byte(tasm2 ? 112 : 104);
    if (samplerCount > 12 || stageCount > 4)
        throw std::runtime_error("CMChunkMaterialInfo: invalid PC sampler or stage count");
    reader.Skip(size_t(samplerCount) * samplerSize + size_t(stageCount) * stageSize);

    CMChunkMaterialInfo material;
    material.source = &chunk;
    material.name = reader.String(0, shattered ? 32 : 64);
    const size_t samplerStart = shattered ? headerSize : headerSize + stageCount * stageSize;
    const size_t stageStart = shattered ? headerSize + samplerCount * samplerSize : headerSize;
    for (uint32_t i = 0; i < samplerCount; ++i)
    {
        const size_t at = samplerStart + i * samplerSize;
        Sampler sampler;
        sampler.crc = reader.Word(at + (shattered ? 0 : 128));
        sampler.clamp = reader.Byte(at + (shattered ? 4 : 132));
        sampler.sourcePath = reader.String(at + (shattered ? 12 : 0), 128);
        material.samplers.push_back(std::move(sampler));
    }
    for (uint32_t i = 0; i < stageCount; ++i)
    {
        const size_t at = stageStart + i * stageSize;
        Stage stage;
        for (size_t c = 0; c < 4; ++c)
            stage.color[c] = reader.Float(at + c * 4);
        stage.sampler = reader.Byte(at + (shattered ? 23 : 37));
        if (shattered)
        {
            stage.flags = reader.Half(at + 20);
            stage.blendMode = reader.Byte(at + 22);
            stage.uvSet = reader.Byte(at + 24);
            stage.maskSampler = reader.Byte(at + 25);
            stage.maskUvSet = reader.Byte(at + 26);
            stage.maskComponent = char(reader.Byte(at + 27));
        }
        material.stages.push_back(stage);
    }
    if (!material.stages.empty())
    {
        const auto& stage = material.stages.front();
        std::copy(stage.color, stage.color + 4, material.color);
        material.uvSet = stage.uvSet;
        if (stage.sampler < material.samplers.size())
        {
            const auto& sampler = material.samplers[stage.sampler];
            material.textureCRC = sampler.crc;
            material.clamp = sampler.clamp;
        }
    }
    return material;
}

CMChunkMaterialInfo::VertexDeclaration CMChunkMaterialInfo::PCDeclaration(uint32_t format, bool packedSkin)
{
    using V = VertexElement;
    VertexDeclaration declaration;
    declaration.format = format;
    uint16_t offset = 0;
    const auto add = [&](uint32_t type, uint8_t usage, uint8_t index = 0) {
        V element{0, offset, type, usage, index};
        declaration.elements.push_back(element);
        offset += uint16_t(element.Size());
    };
    if (!(format & 1))
        throw std::runtime_error("CMChunkMaterialInfo: PC vertices have no position");
    add(V::kFormat_32_32_32_FLOAT, V::kUsagePosition);
    if (format & 2)
        add(V::kFormat_32_32_32_FLOAT, V::kUsageNormal);
    if (format & 4)
        add(V::kFormat_32_32_32_32_FLOAT, 6);
    if (format & 8)
        add(V::kFormat_32_32_32_FLOAT, 7);
    if (format & 16)
        add(6, 10);
    for (uint8_t i = 0; i < ((format >> 5) & 15); ++i)
        add((format & 0x200) ? V::kFormat_16_16_FLOAT : V::kFormat_32_32_FLOAT, V::kUsageTexCoord, i);
    if (format & 0x1000)
    {
        add(6, 2);
        add(packedSkin ? 6 : V::kFormat_32_32_32_32_FLOAT, 1);
    }
    return declaration;
}
