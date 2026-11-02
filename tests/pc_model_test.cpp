#include "../Resource/RZGeometrySkin.h"
#include <cmath>
#include <cstdio>

namespace
{
    void Check(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
    template<class Function> bool Rejects(Function function)
    {
        try { function(); } catch (const std::exception&) { return true; }
        return false;
    }
    void Put32(CMChunk& chunk, size_t at, uint32_t value)
    {
        for (size_t i = 0; i < 4; ++i)
            chunk.data.at(at + i) = uint8_t(value >> (8 * i));
    }
    CMChunk Material(uint16_t version)
    {
        const bool sd = version == 41, tasm2 = version == 13;
        const size_t header = sd ? 368 : tasm2 ? 280 : 236;
        const size_t stage = sd ? 28 : 44, sampler = sd ? 140 : 152;
        auto chunk = CMChunk::Leaf(Geo_MaterialInfo, version, std::vector<uint8_t>(header + stage + sampler));
        chunk.isLittleEndian = true;
        std::memcpy(chunk.data.data(), "diffuse", 7);
        if (sd)
        {
            Put32(chunk, 260, 1);
            Put32(chunk, 264, 1);
        }
        else
            chunk.data[tasm2 ? 112 : 104] = chunk.data[tasm2 ? 113 : 105] = 1;
        const size_t textureAt = sd ? header : header + stage;
        const size_t stageAt = sd ? header + sampler : header;
        Put32(chunk, textureAt + (sd ? 0 : 128), 123);
        for (size_t c = 0; c < 4; ++c)
            Put32(chunk, stageAt + c * 4, 0x3F800000);
        return chunk;
    }
    PKPackage Model(uint16_t materialVersion)
    {
        const bool sd = materialVersion == 41;
        PKPackage package;
        package.isLittleEndian = true;
        auto resource = CMChunk::Container(GenSub_Resource, 1);
        auto header = CMChunkResourceHeader::Build(77, 3, 31, 0, 4096, 0, "model", true);
        resource.AddChild(header);
        auto geometry = CMChunk::Container(Geo_Geometry, sd ? 3 : 4);
        auto post = CMChunk::Container(Gen_PostLoadData, 1);
        post.offset = 4096;
        post.AddChild(header);
        auto descriptor = CMChunk::Leaf(Geo_GeomMesh, sd ? 8 : 10, {});
        descriptor.isLittleEndian = true;
        const uint32_t stride = sd ? 48 : 36;
        for (uint32_t word : {sd ? 0u : 1u, 0u, stride, 0x1623u, 3u, 0u, 3u, 1u, ~0u, 1u, 4u, 0u})
            descriptor.Append(word);
        if (!sd)
            descriptor.Append<uint32_t>(0);
        descriptor.Append<uint32_t>(sd ? 5 : 0);
        auto lod = CMChunk::Container(Geo_GeomLOD, 3);
        auto info = CMChunk::Leaf(Geo_GeomLODInfo, 6, std::vector<uint8_t>(20));
        info.isLittleEndian = true;
        lod.AddChild(info);
        lod.AddChild(std::move(descriptor));
        auto buffers = CMChunk::Container(Geo_GeomLOD, 3);
        buffers.AddChild(info);
        auto vertices = CMChunk::Leaf(GenSub_VertexList, 3, {});
        vertices.isLittleEndian = true;
        for (size_t i = 0; i < 3; ++i)
        {
            for (float f : {float(i == 1), float(i == 2), 0.f, 0.f, 0.f, 1.f})
                vertices.Append(f);
            vertices.Append<uint16_t>(i == 1 ? 0x3C00 : 0);
            vertices.Append<uint16_t>(i == 2 ? 0x3800 : 0);
            vertices.Append<uint32_t>(0xFFFFFF00);
            if (sd)
                for (float f : {1.f, 0.f, 0.f, 0.f}) vertices.Append(f);
            else
                vertices.Append<uint32_t>(255);
        }
        auto indices = CMChunk::Leaf(GenSub_IndexList, 2, {});
        indices.isLittleEndian = true;
        for (uint16_t i : {0, 2, 1}) indices.Append(i);
        buffers.AddChild(std::move(vertices));
        buffers.AddChild(std::move(indices));
        if (sd)
        {
            geometry.AddChild(Material(materialVersion));
            geometry.AddChild(std::move(lod));
        }
        else
        {
            post.AddChild(Material(materialVersion));
            auto mapping = CMChunk::Leaf(Geo_BoneToSkinningBoneTable, 1, {6, 1, 255, 255, 255, 255, 255, 0});
            post.AddChild(std::move(mapping));
            post.AddChild(std::move(lod));
        }
        resource.AddChild(std::move(geometry));
        post.AddChild(std::move(buffers));
        package.rootChunks.push_back(std::move(resource));
        package.rootChunks.push_back(std::move(post));
        return package;
    }
    void Models()
    {
        for (uint16_t version : {41, 9, 13})
        {
            auto package = Model(version);
            const CMChunkResourceHeader header(package.rootChunks[0].children[0]);
            const auto geometry = RZGeometry::Load(package, header);
            const auto& mesh = geometry.meshes.at(0);
            Check(mesh.VertexCount() == 3 && mesh.TriangleCount() == 1, "PC mesh buffers or LOD selection changed");
            Check(mesh.normals[2] == 1 && mesh.texcoords[2] == 1 && mesh.texcoords[5] == .5f && !mesh.integerUV,
                  "PC float normals or half UVs were decoded as Xbox packed values");
            Check(geometry.materials.at(0).textureCRC == 123, "PC base texture was not resolved from its stage");
            const auto skin = RZGeometrySkin::Load(package, header, geometry);
            Check(skin.meshes[0][0].bones[0] == 5 && skin.meshes[0][0].weights[0] == 1,
                  "PC float/byte weights or skin-to-hierarchy mapping changed");
            auto* vertices = package.rootChunks[1].children.back().FindChild(GenSub_VertexList);
            vertices->data.pop_back();
            Check(Rejects([&] { RZGeometry::Load(package, header); }), "Truncated PC vertices were accepted");
            auto material = Material(version);
            material.data.pop_back();
            Check(Rejects([&] { CMChunkMaterialInfo::Parse(material); }), "Truncated PC material was accepted");
        }
        auto legacy = Model(41);
        legacy.rootChunks[0].children[0] = CMChunkResourceHeader::Build(77, 3, 31, 0, 4096, 0,
                                                                       "model", true, false, 3);
        legacy.rootChunks[1].children[0] = CMChunkResourceHeader::Build(88, 3, 31, 0, 8192, 0,
                                                                       "model", true, false, 3);
        const CMChunkResourceHeader alias(legacy.rootChunks[0].children[0]);
        Check(RZGeometry::Load(legacy, alias).meshes.size() == 1, "Legacy SD buffers with a stale CRC were lost");
        legacy.rootChunks[1].children[0] = CMChunkResourceHeader::Build(88, 3, 31, 0, 8192, 0,
                                                                       "another model", true, false, 3);
        Check(Rejects([&] { RZGeometry::Load(legacy, alias); }), "A different SD model was substituted by offset");
        auto package = Model(9);
        auto* table = package.rootChunks[1].FindChild(Geo_BoneToSkinningBoneTable);
        table->data[2] = 0;
        const CMChunkResourceHeader header(package.rootChunks[0].children[0]);
        const auto geometry = RZGeometry::Load(package, header);
        Check(Rejects([&] { RZGeometrySkin::Load(package, header, geometry); }), "Duplicate skin bone mapping was accepted");
    }
}
int main()
{
    try { Models(); std::puts("PASS: PC models, materials, UVs and skin mappings"); }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
