#include "TextureUsage.h"
#include "RZEnvironment.h"
#include "RZTexture.h"
#include <algorithm>
#include <set>

void TextureUsageIndex::AddMaterial(const Usage& owner, const CMChunkMaterialInfo& material)
{
    std::set<uint32_t> textures;
    if (material.textureCRC)
        textures.insert(material.textureCRC);
    for (const auto& sampler : material.samplers)
        if (sampler.crc)
            textures.insert(sampler.crc);
    for (uint32_t crc : textures)
    {
        Usage usage = owner;
        usage.materialName = material.name;
        usage.baseColor = material.textureCRC == crc;
        for (size_t slot = 0; slot < material.samplers.size(); ++slot)
            if (material.samplers[slot].crc == crc)
                usage.samplers.push_back(slot);
        usages[crc].push_back(std::move(usage));
    }
}

void TextureUsageIndex::Build(const PKPackage* source)
{
    package = source;
    usages.clear();
    warnings.clear();
    if (!package)
        return;
    package->ForEachChunk([&](const CMChunk& chunk) {
        if (chunk.GetMaskedID() != GenSub_Resource)
            return;
        const auto* rawHeader = chunk.FindChild(GenSub_ResourceHeader);
        if (!rawHeader)
            return;
        try
        {
            const CMChunkResourceHeader header(*rawHeader);
            const auto type = header.GetResourceType();
            if (type != kCMResourceType_Geometry && type != kCMResourceType_Environment)
                return;
            Usage owner;
            owner.resource = &chunk;
            owner.resourceName = header.GetName();
            owner.languageMask = header.GetLanguageMask();
            owner.qualityLevel = header.GetQualityLevel();
            if (type == kCMResourceType_Environment)
            {
                const auto environment = RZEnvironment::Load(*package, header);
                for (size_t part = 1; part < environment.parts.size(); ++part)
                {
                    owner.environmentPart = int(part);
                    owner.partName = environment.parts[part].name;
                    std::set<uint32_t> materials;
                    for (const auto& primitive : environment.parts[part].primitives)
                        materials.insert(primitive.material);
                    for (auto material : materials)
                    {
                        owner.material = material;
                        AddMaterial(owner, environment.materials[material]);
                    }
                }
            }
            else
            {
                size_t material = 0;
                chunk.ForEachDescendant([&](const CMChunk& child) {
                    if (child.GetMaskedID() != Geo_MaterialInfo)
                        return;
                    owner.material = material++;
                    AddMaterial(owner, CMChunkMaterialInfo::Parse(child));
                });
            }
        }
        catch (const std::exception& error)
        {
            warnings.push_back("Texture references at " + std::to_string(chunk.offset) + ": " + error.what());
        }
    });
}

std::vector<TextureUsageIndex::Usage> TextureUsageIndex::Find(const CMChunkResourceHeader& texture) const
{
    std::vector<Usage> result;
    const auto found = usages.find(texture.GetCRC());
    if (!package || found == usages.end())
        return result;
    for (const auto& usage : found->second)
    {
        const auto resolved =
            RZTexture::FindHeader(*package, texture.GetCRC(), usage.languageMask, usage.qualityLevel);
        if (resolved && resolved->IsSameResource(texture))
            result.push_back(usage);
    }
    return result;
}
