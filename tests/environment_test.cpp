#include "../Resource/RZEnvironment.h"
#include "../Resource/RZTexture.h"
#include <cstdio>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
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

    void Put32(CMChunk& chunk, size_t offset, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            chunk.data.at(offset + i) = uint8_t(value >> (24 - i * 8));
    }

    CMChunk Material(const char* name)
    {
        auto chunk = CMChunk::Leaf(Geo_MaterialInfo, 64, std::vector<uint8_t>(244));
        std::strncpy(reinterpret_cast<char*>(chunk.data.data()), name, 31);
        Put32(chunk, 100, 2);
        for (size_t at : {104, 108, 112})
            Put32(chunk, at, 1);
        for (uint32_t crc : {101, 202})
        {
            const size_t offset = chunk.data.size();
            chunk.data.resize(offset + 140);
            Put32(chunk, offset, crc);
        }
        for (int i = 0; i < 4; ++i)
            chunk.Append(1.0f);
        chunk.data.resize(chunk.data.size() + 16);
        const size_t pass = chunk.data.size();
        chunk.data.resize(pass + 188);
        Put32(chunk, pass, 0x42);
        chunk.Append<uint32_t>(64);
        const size_t declaration = chunk.data.size();
        chunk.data.resize(declaration + 64);
        Put32(chunk, declaration + 24, 1);
        Put32(chunk, declaration + 56, CMChunkMaterialInfo::VertexElement::kFormat_32_32_32_FLOAT);
        for (int shader = 0; shader < 2; ++shader)
        {
            chunk.Append<uint32_t>(1);
            chunk.Append<uint32_t>(0);
            chunk.Append<uint32_t>(0);
        }
        return chunk;
    }

    CMChunk Resource(uint32_t type, uint32_t crc, const char* name, uint32_t language = 1,
                     uint32_t quality = 0)
    {
        auto resource = CMChunk::Container(GenSub_Resource, 1);
        resource.AddChild(CMChunkResourceHeader::Build(crc, type, language, quality, 0, 0, name));
        return resource;
    }

    PKPackage Fixture()
    {
        PKPackage package;
        auto root = CMChunk::Container(Root, 1);
        auto resource = Resource(kCMResourceType_Environment, 55, "lab");
        auto render = CMChunk::Container(Env_Render, 1);
        auto materials = CMChunk::Container(RenderOctree_MaterialList, 3);
        materials.AddChild(Material("red"));
        materials.AddChild(Material("eyes"));
        render.AddChild(std::move(materials));
        auto world = CMChunk::Leaf(RenderOctree_Prims, 7, std::vector<uint8_t>(144));
        for (size_t i = 0; i < 2; ++i)
        {
            const size_t offset = i * 72;
            Put32(world, offset + 24, uint32_t(i));
            Put32(world, offset + 28, 3);
            Put32(world, offset + 32, 0x0C000003);
            Put32(world, offset + 36, 0x42);
            Put32(world, offset + 44, uint32_t(i * 3));
            Put32(world, offset + 56, 0x00010001);
        }
        render.AddChild(std::move(world));
        auto objects = CMChunk::Leaf(RenderOctree_Obj, 13, std::vector<uint8_t>(104));
        Put32(objects, 48, 0x00010000);
        Put32(objects, 60, 3);
        Put32(objects, 64, 0x00030C08);
        Put32(objects, 68, 0x42);
        Put32(objects, 88, 0x00010001);
        render.AddChild(std::move(objects));
        auto vertices = CMChunk::Container(GenSub_VertexBufferArray, 1);
        vertices.AddChild(CMChunk::Leaf(GenSub_VertexList, 2, std::vector<uint8_t>(36)));
        auto data = CMChunk::Leaf(GenSub_VertexList, 2, {});
        for (float value : {-1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 2.f, 0.f})
            data.Append(value);
        vertices.AddChild(std::move(data));
        render.AddChild(std::move(vertices));
        auto indices = CMChunk::Container(GenSub_IndexBufferArray, 1);
        indices.AddChild(CMChunk::Leaf(GenSub_IndexList, 2, std::vector<uint8_t>(12, 255)));
        data = CMChunk::Leaf(GenSub_IndexList, 2, {});
        for (uint16_t index : {0, 1, 2, 0, 1, 2})
            data.Append(index);
        indices.AddChild(std::move(data));
        render.AddChild(std::move(indices));
        resource.AddChild(std::move(render));
        root.AddChild(std::move(resource));
        root.AddChild(Resource(kCMResourceType_Texture, 101, "diffuse"));
        root.AddChild(Resource(kCMResourceType_Texture, 202, "specular"));
        root.AddChild(Resource(kCMResourceType_Texture, 101, "diffuse-low", 2, 1));
        package.rootChunks.push_back(std::move(root));
        return package;
    }

    CMChunkResourceHeader Header(const PKPackage& package, size_t index = 0)
    {
        return CMChunkResourceHeader(*package.rootChunks[0].children[index].FindChild(GenSub_ResourceHeader));
    }

    void DecodeTests()
    {
        auto package = Fixture();
        const auto environment = RZEnvironment::Load(package, Header(package));
        Check(environment.parts.size() == 3 && environment.parts[1].primitives.size() == 2,
              "Baked materials sharing a diffuse texture were not grouped");
        const auto world = environment.Geometry(0);
        const auto baked = environment.Geometry(1);
        const auto object = environment.Geometry(2);
        Check(world.meshes.size() == 2 && baked.meshes.size() == 2 && object.meshes.size() == 1,
              "World/object mesh counts differ");
        Check(object.meshes[0].positions[0] == -1 && object.meshes[0].positions[7] == 2 &&
                  object.meshes[0].indices.size() == 3 && baked.Bounds().mMax.y == 2,
              "Buffer banks, vertex layout, index strips or object bounds were decoded incorrectly");
        Check(!environment.parts[1].localSpace && environment.parts[2].localSpace,
              "Baked world space was confused with object local space");

        auto* render = package.rootChunks[0].children[0].FindChild(Env_Render);
        auto* primitives = render->FindChild(RenderOctree_Prims);
        primitives->data.pop_back();
        Check(Rejects([&] { RZEnvironment::Load(package, Header(package)); }),
              "Truncated world table accepted");
        primitives->data.push_back(0);
        Put32(*primitives, 56, 0x00090001);
        Check(Rejects([&] { RZEnvironment::Load(package, Header(package)); }),
              "Missing buffer bank accepted");
        Put32(*primitives, 56, 0x00010001);
        render->FindChild(RenderOctree_Obj)->data.pop_back();
        Check(Rejects([&] { RZEnvironment::Load(package, Header(package)); }),
              "Truncated object mesh accepted");
    }

} // namespace

int main()
{
    try
    {
        DecodeTests();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::puts("PASS: baked environment groups, object buffers, truncation");
}
