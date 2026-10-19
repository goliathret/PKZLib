#include "RZHierarchy.h"
#include "detail/AnimationReader.h"
#include <functional>
#include <unordered_set>

using namespace pkzlib::animation_detail;

RZHierarchy RZHierarchy::Load(const CMChunk& resource, bool little)
{
    const auto* header = resource.FindChild(GenSub_ResourceHeader);
    const auto* bones = Find(resource, Hierarchy_Bones);
    const auto* trs = Find(resource, Hierarchy_BonesTRS);
    const auto* names = Find(resource, Hierarchy_BoneNames);
    if (!header || !bones || bones->version != 9)
        throw std::runtime_error("Missing EOT v9 hierarchy bones.");
    RZHierarchy result;
    CMChunkResourceHeader h(*header);
    if (h.GetResourceType() != kCMResourceType_Hierarchy)
        throw std::runtime_error("Resource is not a hierarchy.");
    result.name = h.GetName();
    result.crc = h.GetCRC();
    Reader r(bones->data, little);
    const size_t count = r.U32();
    r.Skip(8);
    if (!count || count > 512)
        throw std::runtime_error("Invalid hierarchy bone count.");
    r.Require(count * (8 + 148));
    result.bones.resize(count);
    std::unordered_set<uint32_t> hashes, indices;
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t hash = r.U32(), index = r.U32();
        if (index >= count || !hashes.insert(hash).second || !indices.insert(index).second)
            throw std::runtime_error("Invalid or ambiguous hierarchy bone CRC table.");
        result.bones[index].crc = hash;
    }
    for (size_t i = 0; i < count; ++i)
    {
        Bone& bone = result.bones[i];
        for (auto& column : bone.localBind.m)
            for (float& x : column)
                x = r.F32();
        for (auto& column : bone.inverseBind.m)
            for (float& x : column)
                x = r.F32();
        bone.parent = r.U32();
        r.Skip(16);
        if (bone.parent != UINT32_MAX && bone.parent >= count)
            throw std::runtime_error("Hierarchy parent index is out of range.");
        if (names && names->data.size() / 64 > i)
        {
            const char* text = reinterpret_cast<const char*>(names->data.data() + i * 64);
            bone.name.assign(text, strnlen(text, 64));
        }
    }
    if (!trs || trs->version != 3 || trs->data.size() != count * 40)
        throw std::runtime_error("Animation requires a complete v3 hierarchy bind pose.");
    Reader t(trs->data, little);
    for (auto& bone : result.bones)
    {
        for (int i = 0; i < 3; ++i)
            bone.bind.translation[i] = t.F32();
        for (float& x : bone.bind.rotation)
            x = t.F32();
        bone.bind.rotation = Normalize(bone.bind.rotation);
        for (int i = 0; i < 3; ++i)
            bone.bind.scale[i] = t.F32();
    }
    std::vector<CMTransforms> bind;
    for (auto& bone : result.bones)
        bind.push_back(bone.bind);
    result.WorldPose(bind);
    return result;
}
RZHierarchy RZHierarchy::Load(const PKPackage& package, const CMChunkResourceHeader& header)
{
    const auto* resource = package.FindChunkIf([&](const CMChunk& chunk) {
        const auto* candidate = chunk.FindChild(GenSub_ResourceHeader);
        return chunk.GetMaskedID() == GenSub_Resource && candidate &&
               CMChunkResourceHeader(*candidate).IsSameResource(header);
    });
    if (!resource)
        throw std::runtime_error("Hierarchy resource is missing from the package.");
    return Load(*resource, package.isLittleEndian);
}

std::vector<BUMatrix> RZHierarchy::WorldPose(const std::vector<CMTransforms>& local) const
{
    if (local.size() != bones.size())
        throw std::runtime_error("Pose size does not match the skeleton.");
    std::vector<BUMatrix> world(bones.size());
    std::vector<uint8_t> visited(bones.size());
    std::function<void(size_t)> visit = [&](size_t i) {
        if (visited[i] == 2)
            return;
        if (visited[i] == 1)
            throw std::runtime_error("Hierarchy contains a parent cycle.");
        visited[i] = 1;
        const uint32_t parent = bones[i].parent;
        if (parent != UINT32_MAX)
        {
            if (parent >= bones.size())
                throw std::runtime_error("Invalid bone parent.");
            visit(parent);
        }
        world[i] = parent == UINT32_MAX ? local[i].Matrix() : world[parent] * local[i].Matrix();
        visited[i] = 2;
    };
    for (size_t i = 0; i < bones.size(); ++i)
        visit(i);
    return world;
}
