#include "RZEnvironment.h"
#include <unordered_map>

namespace
{
    uint32_t Word(const CMChunk& chunk, size_t offset)
    {
        return chunk.Read<uint32_t>(offset);
    }

    uint16_t Short(const CMChunk& chunk, size_t offset)
    {
        return chunk.Read<uint16_t>(offset);
    }

    std::vector<const CMChunk*> Buffers(const CMChunk& render, CMChunkTypes arrayID, CMChunkTypes dataID)
    {
        const auto* array = render.FindChild(arrayID);
        return array ? array->FindChildren(dataID) : std::vector<const CMChunk*>{};
    }
} // namespace

RZEnvironment RZEnvironment::Load(const PKPackage& package, const CMChunkResourceHeader& header)
{
    if (package.isLittleEndian || header.GetResourceType() != kCMResourceType_Environment)
        throw std::runtime_error("RZEnvironment: expected an EOT Xbox environment");
    const auto* resource = package.FindChunkIf([&](const CMChunk& chunk) {
        if (chunk.GetMaskedID() != GenSub_Resource || !chunk.FindChild(Env_Render))
            return false;
        const auto* candidate = chunk.FindChild(GenSub_ResourceHeader);
        return candidate && CMChunkResourceHeader(*candidate).IsSameResource(header);
    });
    const auto* render = resource ? resource->FindChild(Env_Render) : nullptr;
    if (!render)
        throw std::runtime_error("RZEnvironment: render data is missing for " + header.GetName());

    RZEnvironment environment;
    environment.name = header.GetName();
    environment.vertexBuffers = Buffers(*render, GenSub_VertexBufferArray, GenSub_VertexList);
    environment.indexBuffers = Buffers(*render, GenSub_IndexBufferArray, GenSub_IndexList);
    if (const auto* list = render->FindChild(RenderOctree_MaterialList))
        list->ForEachDescendant([&](const CMChunk& chunk) {
            if (chunk.GetMaskedID() == Geo_MaterialInfo)
                environment.materials.push_back(CMChunkMaterialInfo::Parse(chunk));
        });
    environment.parts.push_back({"World geometry", {}});
    if (const auto* world = render->FindChild(RenderOctree_Prims))
        environment.ReadWorld(*world);
    if (const auto* objects = render->FindChild(RenderOctree_Obj))
        environment.ReadObjects(*objects);
    if (const auto* instances = render->FindChild(RenderOctree_Instances))
        if (instances->version == 6 && instances->data.size() % 140 == 0)
            environment.instanceCount = instances->data.size() / 140;
    return environment;
}

void RZEnvironment::ReadWorld(const CMChunk& chunk)
{
    constexpr size_t recordSize = 72;
    if (chunk.version != 7 || chunk.data.size() % recordSize)
        throw std::runtime_error("RZEnvironment: unsupported world primitive table version/layout");
    std::unordered_map<uint64_t, size_t> groups;
    for (size_t offset = 0; offset < chunk.data.size(); offset += recordSize)
    {
        if (!Word(chunk, offset + 28))
            continue;
        Primitive primitive;
        primitive.material = Word(chunk, offset + 24);
        primitive.indexCount = Word(chunk, offset + 28);
        primitive.stride = chunk.data[offset + 32];
        primitive.vertexCount = Short(chunk, offset + 34);
        primitive.vertexFormat = Word(chunk, offset + 36);
        primitive.vertexOffset = Word(chunk, offset + 40);
        primitive.indexOffset = Word(chunk, offset + 44);
        primitive.indexBuffer = Short(chunk, offset + 56);
        primitive.vertexBuffer = Short(chunk, offset + 58);
        Validate(primitive);
        parts[0].primitives.push_back(primitive);
        const auto& material = materials[primitive.material];
        const uint64_t key =
            material.textureCRC ? material.textureCRC : (uint64_t(1) << 32) + primitive.material;
        auto group = groups.find(key);
        if (group == groups.end())
        {
            group = groups.emplace(key, parts.size()).first;
            parts.push_back({"Baked - " + material.name, {}});
        }
        parts[group->second].primitives.push_back(primitive);
    }
}

void RZEnvironment::ReadObjects(const CMChunk& chunk)
{
    constexpr size_t headerSize = 60;
    constexpr size_t meshSize = 44;
    if (chunk.version != 13)
        throw std::runtime_error("RZEnvironment: unsupported object table version " +
                                 std::to_string(chunk.version));
    size_t offset = 0;
    size_t object = 0;
    while (offset < chunk.data.size())
    {
        if (chunk.data.size() - offset < headerSize)
            throw std::runtime_error("RZEnvironment: truncated object header");
        const uint16_t count = Short(chunk, offset + 48);
        offset += headerSize;
        if (size_t(count) * meshSize > chunk.data.size() - offset)
            throw std::runtime_error("RZEnvironment: truncated object meshes");
        Part part;
        part.name = "Object " + std::to_string(object++);
        part.localSpace = true;
        for (uint16_t mesh = 0; mesh < count; ++mesh, offset += meshSize)
        {
            Primitive primitive;
            primitive.indexCount = Word(chunk, offset);
            primitive.vertexCount = Short(chunk, offset + 4);
            primitive.stride = chunk.data[offset + 6];
            primitive.vertexFormat = Word(chunk, offset + 8);
            primitive.vertexOffset = Word(chunk, offset + 12);
            primitive.indexOffset = Word(chunk, offset + 16);
            primitive.material = Word(chunk, offset + 20);
            primitive.indexBuffer = Short(chunk, offset + 28);
            primitive.vertexBuffer = Short(chunk, offset + 30);
            Validate(primitive);
            part.primitives.push_back(primitive);
        }
        if (!part.primitives.empty())
            part.name += " - " + materials[part.primitives[0].material].name;
        parts.push_back(std::move(part));
    }
}

void RZEnvironment::Validate(const Primitive& primitive) const
{
    if (primitive.material >= materials.size() || primitive.vertexBuffer >= vertexBuffers.size() ||
        primitive.indexBuffer >= indexBuffers.size())
        throw std::runtime_error("RZEnvironment: material or buffer index is out of range");
    if (primitive.stride < 12 || primitive.vertexCount < 3 || primitive.indexCount < 3 ||
        primitive.indexCount > RZGeometry::kMaxMeshIndices)
        throw std::runtime_error("RZEnvironment: invalid mesh dimensions");
    const auto& vertices = vertexBuffers[primitive.vertexBuffer]->data;
    const auto& indices = indexBuffers[primitive.indexBuffer]->data;
    if (primitive.vertexOffset + size_t(primitive.vertexCount) * primitive.stride > vertices.size() ||
        (size_t(primitive.indexOffset) + primitive.indexCount) * 2 > indices.size())
        throw std::runtime_error("RZEnvironment: mesh exceeds its vertex or index buffer");
}

RZGeometry RZEnvironment::Geometry(size_t partIndex) const
{
    RZGeometry geometry;
    const auto& part = parts.at(partIndex);
    geometry.name = name + " / " + part.name;
    geometry.materials = materials;
    size_t bytes = 0;
    for (const auto& primitive : part.primitives)
    {
        RZGeometry::MeshDescriptor descriptor;
        descriptor.material = primitive.material;
        descriptor.vertexCount = primitive.vertexCount;
        descriptor.indexCount = primitive.indexCount;
        descriptor.stride = primitive.stride;
        descriptor.vertexFormat = primitive.vertexFormat;
        descriptor.vertexOffset = primitive.vertexOffset;
        descriptor.indexOffset = primitive.indexOffset;
        auto mesh = RZGeometry::DecodeMesh(*vertexBuffers[primitive.vertexBuffer],
                                           *indexBuffers[primitive.indexBuffer], descriptor, materials);
        if (mesh.positions.empty())
            throw std::runtime_error("RZEnvironment: invalid positions in " + part.name);
        bytes += mesh.ByteSize();
        if (bytes > 128 * 1024 * 1024)
            throw std::runtime_error("RZEnvironment: part exceeds the 128 MiB preview budget");
        if (!mesh.indices.empty())
            geometry.meshes.push_back(std::move(mesh));
    }
    if (geometry.meshes.empty())
        throw std::runtime_error("RZEnvironment: no drawable meshes in " + part.name);
    return geometry;
}
