#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "../Geo/MaterialInfo.h"
#include "../Package/PKPackage.h"
#include "../Resource/RZAudioSample.h"
#include "../Resource/RZGeometry.h"
#include "../Resource/RZTexture.h"
#include "../Resource/ResourceHeader.h"

namespace selftest
{
    template<typename Function>
    inline bool Throws(Function&& function)
    {
        try
        {
            function();
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }

    inline void PutBE32(std::vector<uint8_t>& bytes, size_t at, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            bytes[at + size_t(i)] = uint8_t(value >> (24 - 8 * i));
    }

    inline void TestPackageQueries(void (*Check)(bool, const char*))
    {
        for (bool littleEndian : { false, true })
        {
            PKPackage source;
            source.isLittleEndian = littleEndian;
            CMChunk root = CMChunk::Container(Root, 1);
            root.AddChild(CMChunk::Leaf(Gen_LevelHeader, 1, { 1, 2, 3, 4 }));
            source.rootChunks.push_back(std::move(root));

            PKPackage read;
            read.ReadFromMemoryAutoEndian(source.Serialize());
            Check(read.isLittleEndian == littleEndian && read.FindChunk(Gen_LevelHeader),
                  littleEndian ? "auto-endian read detects little-endian" : "auto-endian read keeps big-endian");
        }

        PKPackage pkg;
        CMChunk root = CMChunk::Container(Root, 1);
        CMChunk resource = CMChunk::Container(GenSub_Resource, 1);
        resource.AddChild(CMChunkResourceHeader::Build(0x1234, kCMResourceType_Texture, 1, 0, 0x40, 0, "owned"));
        CMChunk payload = CMChunk::Container(Texture, 6);
        payload.AddChild(CMChunk::Leaf(Texture_Data, 7, {}));
        resource.AddChild(std::move(payload));
        root.AddChild(std::move(resource));
        CMChunk post = CMChunk::Container(Gen_PostLoadData, 1);
        post.offset = 0x40;
        root.AddChild(std::move(post));
        pkg.rootChunks.push_back(std::move(root));

        const CMChunk& leaf = pkg.rootChunks[0].children[0].children[1].children[0];
        const auto owner = CMChunkResourceHeader::FindOwner(pkg, leaf);
        Check(owner && owner->GetName() == "owned", "FindOwner resolves a nested chunk's resource header");
        Check(!CMChunkResourceHeader::FindOwner(pkg, pkg.rootChunks[0]), "FindOwner finds no resource above the root");
        Check(pkg.FindRootAtOffset(0x40) == &pkg.rootChunks[0].children[1], "FindRootAtOffset reaches a Root child");
        Check(pkg.FindChunk(Texture_Data) == &leaf, "FindChunk searches the whole tree");

        const auto texture = RZTexture::FindHeader(pkg, 0x1234, 0, 0);
        Check(texture && owner->IsSameResource(*texture), "RZTexture::FindHeader matches by CRC");
        Check(!RZTexture::FindHeader(pkg, 0x9999, 0, 0), "RZTexture::FindHeader rejects an unknown CRC");
    }

    inline void TestAudioSample(void (*Check)(bool, const char*))
    {
        std::vector<uint8_t> payload(87, 0);
        PutBE32(payload, 4, 4);
        PutBE32(payload, 24, 3);
        PutBE32(payload, 28, 0x01660002);
        PutBE32(payload, 32, 48000);
        payload[84] = 1;
        payload[85] = 2;
        payload[86] = 3;

        PKPackage pkg;
        pkg.rootChunks.push_back(CMChunk::Leaf(Audio_SampleData, 1, payload));
        const RZAudioSample sample = RZAudioSample::FromChunk(pkg, pkg.rootChunks[0]);
        Check(sample.format.channels == 2 && sample.format.samplesPerSec == 48000 &&
                  sample.data == std::vector<uint8_t>({ 1, 2, 3 }),
              "Audio_SampleData yields its XMA2 format and payload");

        const std::vector<uint8_t> wave = sample.BuildWave();
        Check(wave.size() == 83 && std::memcmp(wave.data(), "RIFF", 4) == 0 && wave[4] == 75 && wave[20] == 0x66 &&
                  wave[21] == 0x01 && wave[36] == 34 && wave[80] == 1,
              "BuildWave writes a little-endian XMA2 RIFF");

        pkg.rootChunks[0].data.pop_back();
        Check(Throws([&] { RZAudioSample::FromChunk(pkg, pkg.rootChunks[0]); }), "a truncated sample is rejected");

        std::vector<uint8_t> cutsceneHeader(76, 0), cutsceneData(23, 0);
        std::copy(payload.begin() + 28, payload.begin() + 80, cutsceneHeader.begin() + 24);
        PutBE32(cutsceneData, 16, 3);
        cutsceneData[20] = 1;
        cutsceneData[21] = 2;
        cutsceneData[22] = 3;
        pkg.rootChunks.clear();
        pkg.rootChunks.push_back(CMChunk::Leaf(CutScene_Audio_SampleHeader, 1, cutsceneHeader));
        pkg.rootChunks.push_back(CMChunk::Leaf(CutScene_Audio_SampleData, 1, cutsceneData));
        Check(RZAudioSample::FromChunk(pkg, pkg.rootChunks[1]).data == sample.data,
              "a cutscene sample takes its format from CutScene_Audio_SampleHeader");
    }

    inline CMChunk BuildMaterialInfo(uint32_t textureCRC)
    {
        CMChunk chunk = CMChunk::Leaf(Geo_MaterialInfo, CMChunkMaterialInfo::kVersionEOT, std::vector<uint8_t>(244));
        std::memcpy(chunk.data.data(), "fixture", 7);
        for (size_t offset : { 100, 104, 108, 112 })
            PutBE32(chunk.data, offset, 1);
        chunk.Append<uint32_t>(textureCRC);
        chunk.Append<uint32_t>(0x01000000);
        chunk.Append<uint32_t>(0);
        chunk.AppendBytes(std::string(128, 'x').data(), 128);
        for (int i = 0; i < 4; ++i)
            chunk.Append(1.0f);
        chunk.Append(0.0f);
        chunk.Append<uint32_t>(0x00000000);
        chunk.Append<uint32_t>(0x01000000);
        chunk.Append<uint32_t>(0);

        const size_t pass = chunk.data.size();
        chunk.data.resize(pass + 188);
        PutBE32(chunk.data, pass, 0x42);
        PutBE32(chunk.data, pass + 176, CMChunkMaterialInfo::kPassMarker);
        PutBE32(chunk.data, pass + 180, CMChunkMaterialInfo::kTechniqueDepth);
        PutBE32(chunk.data, pass + 184, CMChunkMaterialInfo::kVariantSkinned);
        chunk.Append<uint32_t>(88);
        const size_t declaration = chunk.data.size();
        chunk.data.resize(declaration + 88);
        PutBE32(chunk.data, declaration + 24, 3);
        const uint32_t offsets[] = { 4, 16, 24 };
        const uint32_t types[] = { 0x2a23b9, 0x2a2190, 0x2c2359 };
        const uint8_t usages[] = { 0, 3, 5 };
        for (size_t i = 0; i < 3; ++i)
        {
            const size_t at = declaration + 52 + i * 12;
            PutBE32(chunk.data, at, offsets[i]);
            PutBE32(chunk.data, at + 4, types[i]);
            chunk.data[at + 9] = usages[i];
            chunk.data[at + 10] = i == 2 ? 1 : 0;
        }
        for (int shader = 0; shader < 2; ++shader)
        {
            chunk.Append<uint32_t>(1);
            chunk.Append<uint32_t>(0);
            chunk.Append<uint32_t>(0);
        }
        chunk.length = chunk.data.size();
        return chunk;
    }

    inline void TestMaterialInfo(void (*Check)(bool, const char*))
    {
        const CMChunk chunk = BuildMaterialInfo(0xABCD);
        const CMChunkMaterialInfo material = CMChunkMaterialInfo::Parse(chunk);
        Check(material.name == "fixture" && material.textureCRC == 0xABCD && material.uvSet == 1,
              "Geo_MaterialInfo yields its name, base texture and UV set");
        const CMChunkMaterialInfo::VertexDeclaration* declaration = material.FindDeclaration(0x42);
        Check(declaration && declaration->elements.size() == 3 && declaration->elements[0].offset == 4 &&
                  declaration->elements[2].GetFormat() == CMChunkMaterialInfo::VertexElement::kFormat_16_16 &&
                  declaration->elements[2].IsSigned() && declaration->elements[2].IsInteger(),
              "vertex declarations decode stream offsets and fetch formats");
        Check(material.passes.size() == 1 && material.ShaderContainer(material.passes[0].vertex).empty(),
              "an empty vertex shader record exports no container");
        Check(material.clamp == 1 && material.samplers[0].clamp == 1 && material.stages.size() == 1 &&
                  material.stages[0].sampler == 0 && material.stages[0].uvSet == 1,
              "samplers and stages decode their clamp flag, sampler and UV set");
        Check(material.passes[0].marker == CMChunkMaterialInfo::kPassMarker &&
                  material.passes[0].technique == CMChunkMaterialInfo::kTechniqueDepth &&
                  material.passes[0].variant == CMChunkMaterialInfo::kVariantSkinned &&
                  declaration->technique == CMChunkMaterialInfo::kTechniqueDepth && material.passes[0].vertex.key == 1 &&
                  material.passes[0].vertex.objectSize == CMChunkMaterialInfo::kVertexShaderObjectSize,
              "passes decode their marker, technique, vertex variant and shader key");

        CMChunk truncated = chunk;
        truncated.data.pop_back();
        Check(Throws([&] { CMChunkMaterialInfo::Parse(truncated); }), "a truncated material is rejected");
    }

    inline CMChunk BuildInlineGeometry(const std::string& name)
    {
        CMChunk resource = CMChunk::Container(Gen_PostLoadData, 1);
        resource.AddChild(CMChunkResourceHeader::Build(0, kCMResourceType_Geometry, 0, 0, 0, 0, name));
        CMChunk lod = CMChunk::Container(Geo_GeomLOD, 1);
        CMChunk descriptor = CMChunk::Leaf(Geo_GeomMesh, 1, {});
        for (uint32_t value : { 0u, 0u, 20u, 0u, 3u, 0u, 3u, 0u, 0u, 0u, 0u, 0u })
            descriptor.Append(value);
        CMChunk vertices = CMChunk::Leaf(GenSub_VertexList, 1, {});
        const float positions[3][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
        for (const auto& position : positions)
        {
            for (float component : position)
                vertices.Append(component);
            vertices.Append<uint32_t>(0x7FC00000);
            vertices.Append<uint32_t>(0x7FC00000);
        }
        CMChunk indices = CMChunk::Leaf(GenSub_IndexList, 1, {});
        for (uint16_t index : { 0, 2, 1 })
            indices.Append(index);
        lod.AddChild(std::move(descriptor));
        lod.AddChild(std::move(vertices));
        lod.AddChild(std::move(indices));
        resource.AddChild(std::move(lod));
        return resource;
    }

    inline void TestGeometry(void (*Check)(bool, const char*))
    {
        PKPackage pkg;
        pkg.rootChunks.push_back(BuildInlineGeometry("model"));
        const CMChunkResourceHeader header(pkg.rootChunks[0].children[0]);
        const RZGeometry geometry = RZGeometry::Load(pkg, header);
        Check(geometry.meshes.size() == 1 && geometry.meshes[0].VertexCount() == 3 && geometry.meshes[0].TriangleCount() == 1,
              "inline geometry decodes a padded-stride triangle strip");
        Check(geometry.meshes[0].positions[3] == 1.0f && geometry.meshes[0].normals[1] == 1.0f,
              "positions decode and a missing normal defaults to +Y");
        const BUAABB bounds = geometry.Bounds();
        Check(bounds.mMin.x == -1.0f && bounds.mMax.x == 1.0f && bounds.mMax.y == 1.0f, "Bounds spans every decoded vertex");

        pkg.isLittleEndian = true;
        Check(Throws([&] { RZGeometry::Load(pkg, header); }), "little-endian geometry is refused");
        pkg.isLittleEndian = false;

        const CMChunkResourceHeader missing(CMChunkResourceHeader::Build(0, kCMResourceType_Geometry, 0, 0, 0, 0, "other"));
        Check(Throws([&] { RZGeometry::Load(pkg, missing); }), "a missing resource does not decode unrelated geometry");
    }

    inline void TestResources(void (*Check)(bool, const char*))
    {
        TestPackageQueries(Check);
        TestAudioSample(Check);
        TestMaterialInfo(Check);
        TestGeometry(Check);
    }
}
