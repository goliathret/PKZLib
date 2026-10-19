#include "../Resource/RZHAnim.h"
#include "../Resource/RZGeometrySkin.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
bool Near(float a, float b)
{
    return std::abs(a - b) < .002f;
}
template <class Function> bool Rejects(Function function)
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
void Put32(CMChunk& c, size_t at, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        c.data.at(at + i) = uint8_t(value >> (c.isLittleEndian ? 8 * i : 24 - 8 * i));
}
PKPackage Fixture(bool little)
{
    PKPackage package;
    package.isLittleEndian = little;
    auto resource = CMChunk::Container(GenSub_Resource, 1);
    resource.AddChild(
        CMChunkResourceHeader::Build(99, kCMResourceType_HAnim, 31, 0, 4096, 0, "test clip", little));
    auto info = CMChunk::Leaf(0x4b2, 21, {});
    info.isLittleEndian = little;
    info.Append<uint32_t>(123);
    info.Append(1.f);
    info.Append(1.f / 30);
    info.data.resize(76);
    info.Append<uint32_t>(2);
    info.Append<uint32_t>(0);
    resource.AddChild(std::move(info));
    auto post = CMChunk::Container(Gen_PostLoadData, 1);
    post.offset = 4096;
    post.AddChild(
        CMChunkResourceHeader::Build(99, kCMResourceType_HAnim, 31, 0, 4096, 0, "test clip", little));
    auto table = CMChunk::Leaf(0x4bc, 1, {});
    table.isLittleEndian = little;
    table.Append<uint32_t>(0x10);
    table.Append<uint32_t>(1);
    table.Append<uint32_t>(0x20);
    table.Append<uint32_t>(0);
    post.AddChild(std::move(table));
    auto data = CMChunk::Leaf(0x4b1, 5, {});
    data.isLittleEndian = little;
    data.Append(0.f);
    data.Append<uint16_t>(2);
    data.Append<uint8_t>(1);
    data.Append<uint8_t>(0);
    data.Append<uint32_t>(0);
    for (int i = 0; i < 9; ++i)
        data.Append(0.f);
    auto align = [&](size_t n) {
        while (data.data.size() % n)
            data.Append<uint8_t>(0);
    };
    auto quat = [&] {
        align(2);
        for (int i = 0; i < 3; ++i)
            data.Append<int16_t>(0);
        data.Append<int16_t>(32767);
    };
    data.Append<uint8_t>(0);
    data.Append<uint8_t>(0);
    data.Append<uint16_t>(0x39f);
    for (int i = 0; i < 3; ++i)
        data.Append<int16_t>(0);
    data.Append<int16_t>(3277);
    data.Append<int16_t>(0);
    data.Append<int16_t>(0);
    data.Append<uint8_t>(30);
    quat();
    data.Append<uint8_t>(0);
    data.Append<uint8_t>(1);
    data.Append<uint16_t>(0x399);
    data.Append<int16_t>(0);
    data.Append<int16_t>(3277);
    data.Append<int16_t>(0);
    quat();
    align(4);
    Put32(data, 8, uint32_t(data.data.size() - 12));
    data.Append(1.f);
    data.Append<uint16_t>(0);
    data.Append<uint8_t>(0);
    data.Append<uint8_t>(255);
    data.Append<uint32_t>(0);
    post.AddChild(std::move(data));
    package.rootChunks.push_back(std::move(resource));
    package.rootChunks.push_back(std::move(post));
    return package;
}
RZHierarchy Rig()
{
    RZHierarchy rig;
    rig.bones.resize(3);
    rig.bones[0].crc = 0x20;
    rig.bones[1].crc = 0x10;
    rig.bones[1].parent = 0;
    rig.bones[2].crc = 0x30;
    rig.bones[2].parent = 1;
    rig.bones[2].bind.translation = {0, 0, 2, 0};
    return rig;
}
void DecodeAndMapping()
{
    for (bool little : {false, true})
    {
        auto package = Fixture(little);
        auto clip = RZHAnim::Load(package, package.rootChunks[0]);
        auto rig = Rig();
        Check(clip.Map(rig) == std::vector<int>({0, 1}), "CRC table order was mistaken for track order");
        auto pose = clip.Sample(rig, .5f);
        auto world = rig.WorldPose(pose);
        Check(Near(pose[0].translation[0], .5f), "Packed translation interpolation is wrong");
        Check(Near(world[1].m[3][0], .5f) && Near(world[1].m[3][1], 1.f), "Parent motion was not composed");
        Check(Near(world[2].m[3][2], 2.f), "Unanimated bone lost its bind pose");
        Check(Near(pose[0].scale[0], 1) && Near(pose[0].rotation[3], 1),
              "Identity channels were decoded incorrectly");
        rig.bones[1].crc = 0x40;
        Check(clip.Map(rig) == std::vector<int>({0, -1}), "Missing CRC was silently mapped to a bone");
        rig.bones[1].crc = 0x20;
        Check(Rejects([&] { clip.Map(rig); }), "Duplicate skeleton CRCs were accepted");
        package.rootChunks[1].children.back().data.resize(20);
        Check(Rejects([&] { RZHAnim::Load(package, package.rootChunks[0]); }),
              "Truncated keys were accepted");
    }
    auto rig = Rig();
    rig.bones[0].parent = 1;
    std::vector<CMTransforms> pose(3);
    Check(Rejects([&] { rig.WorldPose(pose); }), "Cyclic skeleton would recurse forever");
    auto package = Fixture(false);
    auto* table = package.rootChunks[1].FindChild(0x4bc);
    Put32(*table, 8, 0x10);
    Check(Rejects([&] { RZHAnim::Load(package, package.rootChunks[0]); }),
          "Ambiguous animation CRCs were accepted");
}
void SkinningAndRotation()
{
    auto rig = Rig();
    rig.bones.resize(2);
    rig.bones[1].parent = UINT32_MAX;
    RZHAnim clip;
    clip.info.duration = 1;
    clip.tracks.resize(1);
    clip.tracks[0].boneCRC = rig.bones[1].crc;
    RZHAnim::Segment rotation;
    rotation.start = 0;
    rotation.end = 1;
    rotation.interpolate = true;
    rotation.from = {0, 0, 0, 1};
    rotation.to = {0, 0, 1, 0};
    clip.tracks[0].rotation.push_back(rotation);
    auto world = rig.WorldPose(clip.Sample(rig, .5f));
    Check(Near(world[1].m[0][0], 0) && Near(world[1].m[0][1], 1),
          "Quaternion interpolation did not rotate 90 degrees");
    RZGeometrySkin skin;
    skin.meshes.resize(1);
    skin.meshes[0].resize(1);
    skin.meshes[0][0].bones = {1, 0, 0, 0};
    skin.meshes[0][0].weights = {1, 0, 0, 0};
    RZGeometry geometry;
    geometry.meshes.resize(1);
    geometry.meshes[0].positions = {1, 0, 0};
    geometry.meshes[0].normals = {1, 0, 0};
    auto output = geometry;
    skin.Apply(rig, world, geometry, output);
    Check(Near(output.meshes[0].positions[0], 0) && Near(output.meshes[0].positions[1], 1),
          "Mesh did not follow its mapped bone");
    Check(Near(output.meshes[0].normals[0], 0) && Near(output.meshes[0].normals[1], 1),
          "Mesh normals did not follow the bone");
    Check(geometry.meshes[0].positions == std::vector<float>({1, 0, 0}), "Skinning overwrote bind vertices");
    world[1] = BUMatrix{};
    world[1].m[0][0] = 2;
    geometry.meshes[0].normals = {1, 1, 0};
    skin.Apply(rig, world, geometry, output);
    Check(Near(output.meshes[0].normals[0], 1 / std::sqrt(5.f)) &&
              Near(output.meshes[0].normals[1], 2 / std::sqrt(5.f)),
          "Nonuniform bone scale did not use inverse-transpose normals");
    skin.meshes[0][0].bones[0] = 20;
    Check(Rejects([&] { skin.Apply(rig, world, geometry, output); }),
          "Invalid skin palette index was accepted");
}
void SkinLayouts()
{
    for (const auto format : {0x1603u, 0x1627u})
        for (bool little : {false, true})
        {
            PKPackage package;
            package.isLittleEndian = little;
            auto source = CMChunk::Container(GenSub_Resource, 1);
            source.AddChild(CMChunkResourceHeader::Build(1, kCMResourceType_Geometry, 31, 0, 4096, 0,
                                                         "shared costume", little));
            auto geo = CMChunk::Container(Geo_Geometry, 3);
            auto lod = CMChunk::Container(Geo_GeomLOD, 3);
            auto info = CMChunk::Leaf(Geo_GeomLODInfo, 6, std::vector<uint8_t>(24));
            info.isLittleEndian = little;
            lod.AddChild(info);
            auto descriptor = CMChunk::Leaf(Geo_GeomMesh, 8, {});
            descriptor.isLittleEndian = little;
            const uint32_t stride = format == 0x1603 ? 24 : 32;
            for (uint32_t value : {0u, 0u, stride, format, 1u, 0u, 3u, 2u, UINT32_MAX, 2u, 4u, 0u, 0u, 1u})
                descriptor.Append(value);
            lod.AddChild(descriptor);
            geo.AddChild(lod);
            source.AddChild(geo);
            auto post = CMChunk::Container(Gen_PostLoadData, 1);
            post.offset = 4096;
            post.AddChild(source.children[0]);
            auto buffers = CMChunk::Container(Geo_GeomLOD, 3);
            buffers.AddChild(info);
            auto vertices = CMChunk::Leaf(GenSub_VertexList, 1, std::vector<uint8_t>(stride));
            vertices.isLittleEndian = little;
            Put32(vertices, stride - 8, 0x00000100);
            Put32(vertices, stride - 4, 0x000040BF);
            buffers.AddChild(vertices);
            post.AddChild(buffers);
            package.rootChunks = {source, post};
            RZGeometry geometry;
            geometry.meshes.resize(1);
            geometry.meshes[0].positions = {0, 0, 0};
            geometry.meshes[0].normals = {0, 1, 0};
            geometry.materials.resize(1); // Shader pass does not declare the appended skin inputs.
            auto rig = Rig();
            auto header = CMChunkResourceHeader(source.children[0]);
            const auto skin = RZGeometrySkin::Load(package, header, geometry);
            Check(skin.meshes[0][0].bones[0] == 0 && skin.meshes[0][0].bones[1] == 1,
                  "Shared costume palette decoded incorrectly");
            Check(Near(skin.meshes[0][0].weights[0], 191.f / 255) &&
                      Near(skin.meshes[0][0].weights[1], 64.f / 255),
                  "Appended skin weights decoded incorrectly");
            rig.bones.resize(1);
            Check(Rejects([&] { skin.Validate(rig); }),
                  "Shared rig with a missing palette bone was accepted");
            rig = Rig();
            Put32(package.rootChunks[0].children[1].children[0].children[1], 12, format & ~0x1000u);
            Check(Rejects([&] { RZGeometrySkin::Load(package, header, geometry); }),
                  "Unskinned layout was guessed to contain skin weights");
        }
}
void MetadataAndByteTranslation()
{
    for (bool little : {false, true})
    {
        auto package = Fixture(little);
        auto& info = package.rootChunks[0].children[1];
        Put32(info, 20, 1);
        Put32(info, 24, 7);
        Put32(info, 80, 3);
        auto metadata = RZHAnim::Load(package, package.rootChunks[0], RZHAnim::LoadMode::Metadata);
        Check(metadata.IsAdditive() && metadata.info.displacementBone == 7 && metadata.info.boneCount == 2 &&
                  metadata.info.morphCount == 3 && metadata.tracks[0].translation.empty(),
              "Animation metadata was lost or eagerly decoded");
        Check(Rejects([&] { metadata.Sample(Rig(), 0); }), "Additive keys were treated as an absolute pose");
        Put32(info, 20, 0);
        info.data.resize(80);
        Check(Rejects([&] { RZHAnim::Load(package, package.rootChunks[0], RZHAnim::LoadMode::Metadata); }),
              "Truncated animation info was accepted");
        package = Fixture(little);
        auto stream = CMChunk::Leaf(HAnim_Resource, 5, {});
        stream.isLittleEndian = little;
        stream.Append(0.f);
        stream.Append<uint16_t>(1);
        stream.Append<uint8_t>(0);
        stream.Append<uint8_t>(0);
        stream.Append<uint32_t>(8);
        stream.Append<uint8_t>(0);
        stream.Append<uint8_t>(0);
        stream.Append<uint16_t>(0x2001); // Current translation, signed bytes.
        stream.Append<int8_t>(127);
        stream.Append<int8_t>(-127);
        stream.Append<int8_t>(0);
        stream.Append<uint8_t>(0);
        package.rootChunks[1].children.back() = std::move(stream);
        const auto clip = RZHAnim::Load(package, package.rootChunks[0]);
        const auto pose = clip.Sample(Rig(), 0);
        Check(Near(pose[0].translation[0], .05f) && Near(pose[0].translation[1], -.05f),
              "EOT byte translation range was confused with another platform's profile");
    }
}

void HierarchyLoading()
{
    for (bool little : {false, true})
    {
        auto resource = CMChunk::Container(GenSub_Resource, 1);
        resource.AddChild(
            CMChunkResourceHeader::Build(12, kCMResourceType_Hierarchy, 31, 0, 0, 0, "rig", little));
        auto bones = CMChunk::Leaf(Hierarchy_Bones, 9, {});
        bones.isLittleEndian = little;
        for (uint32_t value : {2u, 0u, 0u, 0x20u, 1u, 0x10u, 0u})
            bones.Append(value);
        auto trs = CMChunk::Leaf(Hierarchy_BonesTRS, 3, {});
        trs.isLittleEndian = little;
        auto names = CMChunk::Leaf(Hierarchy_BoneNames, 1, std::vector<uint8_t>(128));
        names.data[0] = 'a';
        names.data[64] = 'b';
        for (size_t i = 0; i < 2; ++i)
        {
            CMTransforms bind;
            bind.translation[1] = float(i);
            auto local = bind.Matrix();
            auto inverse = bind.InverseMatrix();
            for (const auto& column : local.m)
                for (float value : column)
                    bones.Append(value);
            for (const auto& column : inverse.m)
                for (float value : column)
                    bones.Append(value);
            bones.Append<uint32_t>(i == 0 ? UINT32_MAX : 0);
            for (int word = 0; word < 4; ++word)
                bones.Append<uint32_t>(0);
            for (int axis = 0; axis < 3; ++axis)
                trs.Append(bind.translation[axis]);
            for (float value : bind.rotation)
                trs.Append(value);
            for (int axis = 0; axis < 3; ++axis)
                trs.Append(bind.scale[axis]);
        }
        resource.AddChild(bones);
        resource.AddChild(trs);
        resource.AddChild(names);
        PKPackage package;
        package.isLittleEndian = little;
        package.rootChunks.push_back(resource);
        const CMChunkResourceHeader header(resource.children[0]);
        const auto rig = RZHierarchy::Load(package, header);
        Check(rig.bones.size() == 2 && rig.bones[0].crc == 0x10 && rig.bones[1].crc == 0x20 &&
                  rig.bones[1].name == "b",
              "Hierarchy CRC/name data was read in table order");
        std::vector<CMTransforms> bind = {rig.bones[0].bind, rig.bones[1].bind};
        Check(Near(rig.WorldPose(bind)[1].m[3][1], 1), "Hierarchy TRS data was not loaded");
        auto wrongHeader =
            CMChunkResourceHeader::Build(99, kCMResourceType_Hierarchy, 31, 0, 0, 0, "rig", little);
        Check(Rejects([&] { RZHierarchy::Load(package, CMChunkResourceHeader(wrongHeader)); }),
              "A different hierarchy was substituted by name");
        Put32(package.rootChunks[0].children[1], 12 + 16 + 128, 1);
        Check(Rejects([&] { RZHierarchy::Load(package, header); }), "Cyclic hierarchy data was accepted");
    }
}
} // namespace
int main()
{
    try
    {
        DecodeAndMapping();
        SkinningAndRotation();
        SkinLayouts();
        MetadataAndByteTranslation();
        HierarchyLoading();
        std::puts("PASS: EOT packed keys, CRC mapping, bind pose, hierarchy cycles and CPU skinning");
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "%s\n", ex.what());
        return 1;
    }
}
