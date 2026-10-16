#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "../BaseUtils/BUAABB.h"
#include "../Geo/MaterialInfo.h"
#include "../Package/CMChunk.h"
#include "../Package/PKPackage.h"
#include "ResourceHeader.h"

class RZGeometry
{
public:
    static constexpr uint16_t kMeshVersionEOT = 8;
    static constexpr uint32_t kMaxMeshVertices = 1000000;
    static constexpr uint32_t kMaxMeshIndices = 6000000;

    struct Mesh
    {
        uint32_t material = 0;
        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> texcoords;
        std::vector<uint32_t> indices;
        bool hasUV = false;
        bool integerUV = false;

        size_t VertexCount() const { return positions.size() / 3; }
        size_t TriangleCount() const { return indices.size() / 3; }

        size_t ByteSize() const
        {
            return (positions.size() + normals.size() + texcoords.size()) * sizeof(float) + indices.size() * sizeof(uint32_t);
        }
    };

    std::string name;
    std::vector<CMChunkMaterialInfo> materials;
    std::vector<Mesh> meshes;

    static RZGeometry Load(const PKPackage& pkg, const CMChunkResourceHeader& header)
    {
        if (header.GetName().empty())
            throw std::runtime_error("RZGeometry: the resource has no name, so its buffers cannot be matched");
        if (pkg.isLittleEndian)
            throw std::runtime_error("RZGeometry: only big-endian geometry is decoded");

        RZGeometry geometry;
        geometry.name = header.GetName();
        geometry.materials = LoadMaterials(pkg, header);

        const Lod lod = SelectLod(pkg, header);
        for (const CMChunk& chunk : lod.descriptors->children)
        {
            if (chunk.GetMaskedID() != Geo_GeomMesh)
                continue;
            if (!lod.inlined && chunk.version != kMeshVersionEOT)
                throw std::runtime_error("RZGeometry: unsupported Geo_GeomMesh version " + std::to_string(chunk.version) +
                                         " (expected EOT v8)");
            const MeshDescriptor descriptor = MeshDescriptor::Parse(chunk);
            if (!geometry.materials.empty() && descriptor.material >= geometry.materials.size())
                throw std::runtime_error("RZGeometry: mesh material index is out of range");
            Mesh mesh = DecodeMesh(*lod.vertices, *lod.indices, descriptor, geometry.materials);
            if (mesh.positions.empty() || mesh.indices.empty())
                throw std::runtime_error("RZGeometry: a mesh has no valid triangles or its buffers are truncated");
            geometry.meshes.push_back(std::move(mesh));
        }
        if (geometry.meshes.empty())
            throw std::runtime_error("RZGeometry: no drawable mesh in " + geometry.name);
        return geometry;
    }

    BUAABB Bounds() const
    {
        BUAABB bounds;
        bounds.mMin.Set(std::numeric_limits<float>::max());
        bounds.mMax.Set(std::numeric_limits<float>::lowest());
        for (const Mesh& mesh : meshes)
            for (size_t i = 0; i + 2 < mesh.positions.size(); i += 3)
            {
                const BUVector3 position(mesh.positions[i], mesh.positions[i + 1], mesh.positions[i + 2]);
                bounds.mMin.Min(position);
                bounds.mMax.Max(position);
            }
        return bounds;
    }

    static const CMChunk* FindResource(const PKPackage& pkg, const CMChunkResourceHeader& header)
    {
        return pkg.FindChunkIf([&](const CMChunk& chunk) {
            const uint32_t id = chunk.GetMaskedID();
            return (id == GenSub_Resource || id == Gen_PostLoadData) && chunk.FindChild(Geo_Geometry) &&
                   DescribesResource(chunk, header);
        });
    }

    static std::vector<CMChunkMaterialInfo> LoadMaterials(const PKPackage& pkg, const CMChunkResourceHeader& header)
    {
        std::vector<CMChunkMaterialInfo> materials;
        if (const CMChunk* resource = FindResource(pkg, header))
            resource->ForEachDescendant([&](const CMChunk& chunk) {
                if (chunk.GetMaskedID() == Geo_MaterialInfo)
                    materials.push_back(CMChunkMaterialInfo::Parse(chunk));
            });
        return materials;
    }

private:
    using VertexElement = CMChunkMaterialInfo::VertexElement;

    struct MeshDescriptor
    {
        uint32_t material = 0;
        uint32_t vertexOffset = 0;
        uint32_t stride = 0;
        uint32_t vertexFormat = 0;
        uint32_t vertexCount = 0;
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;

        static MeshDescriptor Parse(const CMChunk& chunk)
        {
            if (chunk.data.size() < 48)
                throw std::runtime_error("RZGeometry: invalid mesh descriptor");
            size_t at = 0;
            MeshDescriptor d;
            d.material = chunk.Read<uint32_t>(at);
            d.vertexOffset = chunk.Read<uint32_t>(at);
            d.stride = chunk.Read<uint32_t>(at);
            d.vertexFormat = chunk.Read<uint32_t>(at);
            d.vertexCount = chunk.Read<uint32_t>(at);
            d.indexOffset = chunk.Read<uint32_t>(at);
            d.indexCount = chunk.Read<uint32_t>(at);
            if (!d.vertexCount || !d.indexCount || !d.stride)
                throw std::runtime_error("RZGeometry: invalid mesh descriptor");
            return d;
        }
    };

    struct Lod
    {
        const CMChunk* descriptors = nullptr;
        const CMChunk* vertices = nullptr;
        const CMChunk* indices = nullptr;
        bool inlined = false;
    };

    struct Layout
    {
        int position = 0;
        int normal = -1;
        int uv = -1;
        uint32_t normalType = VertexElement::kFormat_10_11_11;
        uint32_t uvType = VertexElement::kFormat_16_16 | VertexElement::kSigned | VertexElement::kInteger;
    };

    static bool DescribesResource(const CMChunk& chunk, const CMChunkResourceHeader& header)
    {
        const CMChunk* h = chunk.FindChild(GenSub_ResourceHeader);
        if (!h)
            return false;
        const CMChunkResourceHeader candidate(*h);
        return candidate.GetResourceType() == kCMResourceType_Geometry && candidate.IsSameResource(header);
    }

    static bool HasInlineBuffers(const CMChunk& chunk)
    {
        for (const CMChunk& lod : chunk.children)
            if (lod.GetMaskedID() == Geo_GeomLOD && lod.FindChild(GenSub_VertexList) && lod.FindChild(GenSub_IndexList) &&
                lod.FindChild(Geo_GeomMesh))
                return true;
        return false;
    }

    static uint32_t LodIndex(const CMChunk& lod)
    {
        const CMChunk* info = lod.FindChild(Geo_GeomLODInfo);
        if (!info || info->data.size() < 16)
            throw std::runtime_error("RZGeometry: missing Geo_GeomLODInfo");
        size_t at = 12;
        return info->Read<uint32_t>(at);
    }

    static Lod SelectLod(const PKPackage& pkg, const CMChunkResourceHeader& header)
    {
        const CMChunk* source = nullptr;
        const CMChunk* post = nullptr;
        const CMChunk* resource = FindResource(pkg, header);
        const CMChunk* geometry = resource ? resource->FindChild(Geo_Geometry) : nullptr;
        if (geometry && geometry->FindChild(Geo_GeomLOD))
        {
            source = geometry;
            post = pkg.FindRootAtOffset(header.GetDataOffset());
            if (!post || post->GetMaskedID() != Gen_PostLoadData)
                throw std::runtime_error("RZGeometry: no Gen_PostLoadData at the data offset of " + header.GetName());
            const CMChunk* postHeader = post->FindChild(GenSub_ResourceHeader);
            if (!postHeader || CMChunkResourceHeader(*postHeader).GetCRC() != header.GetCRC() ||
                CMChunkResourceHeader(*postHeader).GetResourceType() != kCMResourceType_Geometry)
                throw std::runtime_error("RZGeometry: the post-load header does not match " + header.GetName());
        }
        else
        {
            source = post = pkg.FindChunkIf(
                [&](const CMChunk& chunk) { return HasInlineBuffers(chunk) && DescribesResource(chunk, header); });
        }
        if (!source)
            throw std::runtime_error("RZGeometry: the geometry resource or its buffers are missing for " + header.GetName());

        Lod lod;
        lod.inlined = source == post;
        uint32_t level = std::numeric_limits<uint32_t>::max();
        for (const CMChunk& candidate : source->children)
        {
            if (candidate.GetMaskedID() != Geo_GeomLOD || !candidate.FindChild(Geo_GeomMesh))
                continue;
            const uint32_t index = lod.inlined ? 0 : LodIndex(candidate);
            if (index < level)
            {
                level = index;
                lod.descriptors = &candidate;
            }
        }
        if (!lod.descriptors)
            throw std::runtime_error("RZGeometry: no Geo_GeomMesh descriptors in " + header.GetName());

        const CMChunk* buffers = nullptr;
        for (const CMChunk& candidate : post->children)
        {
            if (candidate.GetMaskedID() != Geo_GeomLOD || (!lod.inlined && LodIndex(candidate) != level))
                continue;
            if (buffers)
                throw std::runtime_error("RZGeometry: duplicate LOD buffers in " + header.GetName());
            buffers = &candidate;
        }
        lod.vertices = buffers ? buffers->FindChild(GenSub_VertexList) : nullptr;
        lod.indices = buffers ? buffers->FindChild(GenSub_IndexList) : nullptr;
        if (!lod.vertices || !lod.indices)
            throw std::runtime_error("RZGeometry: vertex or index buffers are missing for LOD " + std::to_string(level));
        return lod;
    }

    static Layout LegacyLayout(uint32_t vertexFormat)
    {
        Layout layout;
        int offset = 12;
        if (vertexFormat & 0x0002)
        {
            layout.normal = offset;
            offset += 4;
        }
        for (uint32_t flag : { 0x0004u, 0x0008u, 0x0010u })
            if (vertexFormat & flag)
                offset += 4;
        if ((vertexFormat >> 5) & 15)
            layout.uv = offset;
        return layout;
    }

    static Layout DeclaredLayout(const CMChunkMaterialInfo& material, const MeshDescriptor& descriptor)
    {
        const CMChunkMaterialInfo::VertexDeclaration* declaration = material.FindDeclaration(descriptor.vertexFormat);
        if (!declaration)
            throw std::runtime_error("RZGeometry: no matching vertex declaration for " + material.name);

        Layout layout;
        bool hasPosition = false;
        for (const VertexElement& element : declaration->elements)
        {
            if (element.stream != 0)
                continue;
            if (element.offset > descriptor.stride || element.Size() > descriptor.stride - element.offset)
                throw std::runtime_error("RZGeometry: a vertex declaration exceeds the mesh stride");
            if (element.usage == VertexElement::kUsagePosition && element.index == 0)
            {
                if (element.GetFormat() != VertexElement::kFormat_32_32_32_FLOAT)
                    throw std::runtime_error("RZGeometry: unsupported packed position format");
                layout.position = element.offset;
                hasPosition = true;
            }
            if (element.usage == VertexElement::kUsageNormal && element.index == 0)
            {
                layout.normal = element.offset;
                layout.normalType = element.type;
            }
            if (element.usage == VertexElement::kUsageTexCoord && element.index == material.uvSet)
            {
                layout.uv = element.offset;
                layout.uvType = element.type;
            }
        }
        if (!hasPosition)
            throw std::runtime_error("RZGeometry: the vertex declaration has no base position");
        return layout;
    }

    static float UnpackSigned(uint32_t bits, int width, float scale)
    {
        const int value = static_cast<int>(bits);
        const int half = 1 << (width - 1);
        return float(value >= half ? value - (half << 1) : value) / scale;
    }

    static Mesh DecodeMesh(const CMChunk& vertexList, const CMChunk& indexList, const MeshDescriptor& descriptor,
                           const std::vector<CMChunkMaterialInfo>& materials)
    {
        Mesh mesh;
        mesh.material = descriptor.material;
        const uint32_t stride = descriptor.stride;
        if (stride < 12 || descriptor.vertexCount < 3 || descriptor.indexCount < 3)
            return mesh;
        if (descriptor.vertexCount > kMaxMeshVertices || descriptor.indexCount > kMaxMeshIndices)
            throw std::runtime_error("RZGeometry: a mesh exceeds 1M vertices or 6M indices");
        if (descriptor.vertexOffset + size_t(descriptor.vertexCount) * stride > vertexList.data.size())
            return mesh;
        if ((size_t(descriptor.indexOffset) + descriptor.indexCount) * 2 > indexList.data.size())
            return mesh;

        const Layout layout = descriptor.material < materials.size()
                                  ? DeclaredLayout(materials[descriptor.material], descriptor)
                                  : LegacyLayout(descriptor.vertexFormat);
        if (!DecodeVertices(vertexList, descriptor, layout, mesh))
            return mesh;
        mesh.indices = DecodeIndices(indexList, descriptor);
        return mesh;
    }

    static bool DecodeVertices(const CMChunk& vertexList, const MeshDescriptor& descriptor, const Layout& layout, Mesh& mesh)
    {
        const auto readFloat = [&](size_t at) { return vertexList.Read<float>(at); };
        const auto readU16 = [&](size_t at) { return vertexList.Read<uint16_t>(at); };
        const auto readU32 = [&](size_t at) { return vertexList.Read<uint32_t>(at); };

        const uint32_t uvFormat = layout.uvType & VertexElement::kFormatMask;
        const uint32_t normalFormat = layout.normalType & VertexElement::kFormatMask;
        const bool uvSigned = (layout.uvType & VertexElement::kSigned) != 0;
        const bool uvInteger = (layout.uvType & VertexElement::kInteger) != 0;
        mesh.hasUV = layout.uv >= 0;
        mesh.integerUV = uvFormat == VertexElement::kFormat_16_16 && uvInteger;
        mesh.positions.reserve(size_t(descriptor.vertexCount) * 3);
        mesh.normals.reserve(size_t(descriptor.vertexCount) * 3);
        mesh.texcoords.reserve(size_t(descriptor.vertexCount) * 2);

        bool finite = true;
        for (uint32_t v = 0; v < descriptor.vertexCount; ++v)
        {
            const size_t vertex = descriptor.vertexOffset + size_t(v) * descriptor.stride;
            const size_t position = vertex + size_t(layout.position);
            const float x = readFloat(position), y = readFloat(position + 4), z = readFloat(position + 8);
            finite = finite && IsPlausiblePosition(x, y, z);
            mesh.positions.insert(mesh.positions.end(), { x, y, z });

            float u = 0.0f, t = 0.0f;
            if (mesh.hasUV)
            {
                const size_t uv = vertex + size_t(layout.uv);
                if (uvFormat == VertexElement::kFormat_16_16)
                {
                    u = uvSigned ? float(int16_t(readU16(uv))) : float(readU16(uv));
                    t = uvSigned ? float(int16_t(readU16(uv + 2))) : float(readU16(uv + 2));
                    if (!uvInteger)
                    {
                        const float divisor = uvSigned ? 32767.0f : 65535.0f;
                        u = std::max(-1.0f, u / divisor);
                        t = std::max(-1.0f, t / divisor);
                    }
                }
                else if (uvFormat == VertexElement::kFormat_32_32_FLOAT)
                {
                    u = readFloat(uv);
                    t = readFloat(uv + 4);
                }
                else
                    throw std::runtime_error("RZGeometry: unsupported UV vertex format");
                if (!std::isfinite(u) || !std::isfinite(t))
                    throw std::runtime_error("RZGeometry: non-finite UV");
            }
            mesh.texcoords.insert(mesh.texcoords.end(), { u, t });

            float nx = 0.0f, ny = 1.0f, nz = 0.0f;
            if (layout.normal >= 0)
            {
                const size_t normal = vertex + size_t(layout.normal);
                if (normalFormat == VertexElement::kFormat_10_11_11)
                {
                    const uint32_t packed = readU32(normal);
                    nx = UnpackSigned(packed & 0x7FF, 11, 1024.0f);
                    ny = UnpackSigned((packed >> 11) & 0x7FF, 11, 1024.0f);
                    nz = UnpackSigned((packed >> 22) & 0x3FF, 10, 512.0f);
                }
                else if (normalFormat == VertexElement::kFormat_32_32_32_FLOAT)
                {
                    nx = readFloat(normal);
                    ny = readFloat(normal + 4);
                    nz = readFloat(normal + 8);
                }
                else
                    throw std::runtime_error("RZGeometry: unsupported normal vertex format");
                if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz))
                    throw std::runtime_error("RZGeometry: non-finite normal");
            }
            mesh.normals.insert(mesh.normals.end(), { nx, ny, nz });
        }

        if (finite)
            return true;
        mesh.positions.clear();
        mesh.normals.clear();
        return false;
    }

    static bool IsPlausiblePosition(float x, float y, float z)
    {
        constexpr float kLimit = 1.0e5f;
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::fabs(x) < kLimit && std::fabs(y) < kLimit &&
               std::fabs(z) < kLimit;
    }

    static std::vector<uint32_t> DecodeIndices(const CMChunk& indexList, const MeshDescriptor& descriptor)
    {
        constexpr uint16_t kRestart = 0xFFFF;
        constexpr uint16_t kPadding = 0xAAAA;
        const auto readIndex = [&](uint32_t i) {
            size_t at = (size_t(descriptor.indexOffset) + i) * 2;
            return indexList.Read<uint16_t>(at);
        };

        uint32_t count = descriptor.indexCount;
        while (count > 0 && descriptor.vertexCount <= kPadding && readIndex(count - 1) == kPadding)
            --count;
        if (count < 3)
            return {};

        std::vector<uint32_t> triangles;
        std::vector<uint16_t> strip;
        const auto flush = [&]() {
            for (size_t i = 0; i + 2 < strip.size(); ++i)
            {
                const uint16_t a = strip[i], b = strip[i + 1], c = strip[i + 2];
                if (a == b || b == c || a == c)
                    continue;
                if (i % 2 == 0)
                    triangles.insert(triangles.end(), { b, a, c });
                else
                    triangles.insert(triangles.end(), { a, b, c });
            }
            strip.clear();
        };
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint16_t index = readIndex(i);
            if (index == kRestart)
            {
                flush();
                continue;
            }
            if (index >= descriptor.vertexCount)
                throw std::runtime_error("RZGeometry: a mesh index exceeds the vertex count");
            strip.push_back(index);
        }
        flush();
        return triangles;
    }
};
