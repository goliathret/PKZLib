#include "RZGeometrySkin.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
uint32_t U32(const CMChunk& c, size_t at)
{
    return c.Read<uint32_t>(at);
}
BUVector3 TransformPoint(const BUMatrix& m, const BUVector3& v, bool direction)
{
    return BUVector3(m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z + (direction ? 0 : m.m[3][0]),
                     m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z + (direction ? 0 : m.m[3][1]),
                     m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z + (direction ? 0 : m.m[3][2]));
}
BUMatrix NormalMatrix(const BUMatrix& m)
{
    const BUVector3 x(m.m[0][0], m.m[0][1], m.m[0][2]), y(m.m[1][0], m.m[1][1], m.m[1][2]),
        z(m.m[2][0], m.m[2][1], m.m[2][2]);
    const float determinant = x.Dot(y.Cross(z));
    if (std::abs(determinant) < 1e-10f)
        return m;
    const BUVector3 columns[] = {y.Cross(z) * (1 / determinant), z.Cross(x) * (1 / determinant),
                                 x.Cross(y) * (1 / determinant)};
    BUMatrix result;
    for (size_t i = 0; i < 3; ++i)
    {
        result.m[i][0] = columns[i].x;
        result.m[i][1] = columns[i].y;
        result.m[i][2] = columns[i].z;
    }
    return result;
}
} // namespace
RZGeometrySkin RZGeometrySkin::Load(const PKPackage& package, const CMChunkResourceHeader& header,
                                    const RZGeometry& geometry)
{
    const auto lod = RZGeometry::SelectLod(package, header, false);
    const auto* descriptors = lod.descriptors;
    const auto* vertices = lod.vertices;
    const auto* post = package.FindRootAtOffset(header.GetDataOffset());
    const auto* boneTable = post ? post->FindChild(Geo_BoneToSkinningBoneTable) : nullptr;
    std::vector<uint32_t> skinToBone;
    if (boneTable)
    {
        if (boneTable->version != 1 || boneTable->data.size() < 2)
            throw std::runtime_error("Invalid skin bone table.");
        const auto& table = boneTable->data;
        if (table[0] > table.size() - 2)
            throw std::runtime_error("Truncated skin bone table.");
        skinToBone.resize(table[1], UINT32_MAX);
        for (uint32_t bone = 0; bone < table[0]; ++bone)
        {
            const uint8_t index = table[bone + 2];
            if (index == 255)
                continue;
            if (index >= skinToBone.size() || skinToBone[index] != UINT32_MAX)
                throw std::runtime_error("Invalid or duplicate skin bone mapping.");
            skinToBone[index] = bone;
        }
    }
    RZGeometrySkin result;
    size_t meshIndex = 0;
    for (const auto& d : descriptors->children)
    {
        if (d.GetMaskedID() != Geo_GeomMesh)
            continue;
        if ((d.version != 8 && d.version != 10) || d.data.size() < 48 || meshIndex >= geometry.meshes.size())
            throw std::runtime_error("Unsupported skin mesh descriptor.");
        const uint32_t material = geometry.meshes[meshIndex].material;
        const uint32_t offset = U32(d, 4), stride = U32(d, 8), format = U32(d, 12);
        const uint32_t count = U32(d, 16), paletteSize = U32(d, 28);
        const size_t paletteOffset = d.version == 10 ? 48 + ((size_t(U32(d, 0)) + 3) & ~size_t(3)) : 48;
        if (paletteOffset > d.data.size())
            throw std::runtime_error("Truncated skin material list.");
        if (count != geometry.meshes[meshIndex].VertexCount() || material >= geometry.materials.size() ||
            !paletteSize || paletteSize > 256 || paletteSize > (d.data.size() - paletteOffset) / 4 || !stride ||
            offset > vertices->data.size() || count > (vertices->data.size() - offset) / stride)
            throw std::runtime_error("Skin vertex or palette data is invalid.");
        const auto* materialSource = geometry.materials[material].source;
        const bool pcVertices = materialSource && materialSource->isLittleEndian;
        const auto pc = pcVertices
                            ? CMChunkMaterialInfo::PCDeclaration(format, d.version == 10)
                            : CMChunkMaterialInfo::VertexDeclaration{};
        const auto* decl = pcVertices ? &pc : geometry.materials[material].FindDeclaration(format);
        const CMChunkMaterialInfo::VertexElement* indices = nullptr;
        const CMChunkMaterialInfo::VertexElement* weights = nullptr;
        if (decl)
            for (const auto& e : decl->elements)
                if (e.stream == 0 && e.index == 0)
                {
                    if (e.usage == 2)
                        indices = &e;
                    if (e.usage == 1)
                        weights = &e;
                }
        CMChunkMaterialInfo::VertexElement packedIndices, packedWeights;
        const uint32_t baseFormat = format & 0xFFF;
        if (!indices && !weights && (format & 0x1000) &&
            ((baseFormat == 0x603 && stride == 24) || (baseFormat == 0x627 && stride == 32)))
        {
            // Some passes only declare the unskinned inputs; EOT appends two UBYTE4 skin fields.
            packedIndices.offset = uint16_t(stride - 8);
            packedWeights.offset = uint16_t(stride - 4);
            packedIndices.type = packedWeights.type = 6;
            indices = &packedIndices;
            weights = &packedWeights;
        }
        const bool rigid = pcVertices && !(format & 0x1000) && paletteSize == 1;
        const bool floatWeights = weights &&
            weights->GetFormat() == CMChunkMaterialInfo::VertexElement::kFormat_32_32_32_32_FLOAT;
        if (!rigid && (!indices || !weights || indices->GetFormat() != 6 || (!floatWeights && weights->GetFormat() != 6) ||
            indices->offset > stride || 4 > stride - indices->offset || weights->offset > stride ||
            weights->Size() > stride - weights->offset))
            throw std::runtime_error("This mesh does not have supported skin indices and weights.");
        std::vector<uint32_t> palette;
        for (uint32_t i = 0; i < paletteSize; ++i)
        {
            uint32_t bone = U32(d, paletteOffset + i * 4);
            if (boneTable)
            {
                if (bone >= skinToBone.size() || skinToBone[bone] == UINT32_MAX)
                    throw std::runtime_error("Skin palette has no hierarchy bone mapping.");
                bone = skinToBone[bone];
            }
            if (bone >= 512)
                throw std::runtime_error("Skin palette bone exceeds the supported hierarchy size.");
            result.requiredBoneCount = std::max(result.requiredBoneCount, bone + 1);
            palette.push_back(bone);
        }
        std::vector<Influence> skin(count);
        for (uint32_t v = 0; v < count; ++v)
        {
            if (rigid)
            {
                skin[v].bones[0] = palette[0];
                skin[v].weights[0] = 1;
                continue;
            }
            const size_t base = offset + size_t(v) * stride;
            const uint32_t packedIndices = U32(*vertices, base + indices->offset);
            const uint32_t packedWeights = floatWeights ? 0 : U32(*vertices, base + weights->offset);
            float sum = 0;
            for (size_t i = 0; i < 4; ++i)
            {
                const uint32_t index = (packedIndices >> (8 * i)) & 255;
                size_t at = base + weights->offset + i * 4;
                const float weight = floatWeights ? vertices->Read<float>(at)
                                                   : float((packedWeights >> (8 * i)) & 255) / 255;
                if (!std::isfinite(weight) || weight < 0 || weight > 1.001f)
                    throw std::runtime_error("Invalid vertex skin weight.");
                if (!weight)
                    continue;
                if (index >= palette.size())
                    throw std::runtime_error("Vertex skin index is outside the bone palette.");
                skin[v].bones[i] = palette[index];
                skin[v].weights[i] = weight;
                sum += skin[v].weights[i];
            }
            if (sum < .001f)
                throw std::runtime_error("Skinned vertex has no bone weights.");
            for (float& w : skin[v].weights)
                w /= sum;
        }
        result.meshes.push_back(std::move(skin));
        ++meshIndex;
    }
    if (result.meshes.size() != geometry.meshes.size())
        throw std::runtime_error("Skin mesh count mismatch.");
    return result;
}
void RZGeometrySkin::Validate(const RZHierarchy& skeleton) const
{
    if (requiredBoneCount > skeleton.bones.size())
        throw std::runtime_error("Skin palette references a missing skeleton bone.");
}

void RZGeometrySkin::Apply(const RZHierarchy& skeleton, const std::vector<BUMatrix>& world,
                           const RZGeometry& original, RZGeometry& output) const
{
    if (world.size() != skeleton.bones.size() || meshes.size() != original.meshes.size() ||
        meshes.size() != output.meshes.size())
        throw std::runtime_error("Skin pose does not match the mesh.");
    std::vector<BUMatrix> matrices, normals;
    for (size_t i = 0; i < world.size(); ++i)
    {
        matrices.push_back(world[i] * skeleton.bones[i].inverseBind);
        normals.push_back(NormalMatrix(matrices.back()));
    }
    for (size_t m = 0; m < meshes.size(); ++m)
    {
        const auto& source = original.meshes[m];
        auto& target = output.meshes[m];
        if (meshes[m].size() != source.VertexCount() || source.normals.size() != source.positions.size() ||
            target.positions.size() != source.positions.size() ||
            target.normals.size() != source.normals.size())
            throw std::runtime_error("Skin vertex count mismatch.");
        for (size_t v = 0; v < meshes[m].size(); ++v)
        {
            const BUVector3 point(source.positions[v * 3], source.positions[v * 3 + 1],
                                  source.positions[v * 3 + 2]);
            const BUVector3 normal(source.normals[v * 3], source.normals[v * 3 + 1],
                                   source.normals[v * 3 + 2]);
            BUVector3 p(0, 0, 0), n(0, 0, 0);
            for (size_t i = 0; i < 4; ++i)
                if (meshes[m][v].weights[i] > 0)
                {
                    const auto& matrix = matrices.at(meshes[m][v].bones[i]);
                    p = p + TransformPoint(matrix, point, false) * meshes[m][v].weights[i];
                    n = n + TransformPoint(normals.at(meshes[m][v].bones[i]), normal, true) *
                                meshes[m][v].weights[i];
                }
            if (n.Length() > 1e-8f)
                n.Normalize();
            target.positions[v * 3] = p.x;
            target.positions[v * 3 + 1] = p.y;
            target.positions[v * 3 + 2] = p.z;
            target.normals[v * 3] = n.x;
            target.normals[v * 3 + 1] = n.y;
            target.normals[v * 3 + 2] = n.z;
        }
    }
}
