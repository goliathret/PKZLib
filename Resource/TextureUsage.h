#pragma once
#include "ResourceHeader.h"
#include <unordered_map>

class TextureUsageIndex
{
public:
    struct Usage
    {
        const CMChunk* resource = nullptr;
        std::string resourceName;
        std::string materialName;
        std::string partName;
        uint32_t languageMask = 0;
        uint32_t qualityLevel = 0;
        size_t material = 0;
        int environmentPart = -1;
        std::vector<size_t> samplers;
        bool baseColor = false;
    };

    // Entries point into the package; rebuild after edits or loading another package.
    void Build(const PKPackage* package);
    std::vector<Usage> Find(const CMChunkResourceHeader& texture) const;
    const std::vector<std::string>& Warnings() const { return warnings; }

private:
    void AddMaterial(const Usage& owner, const class CMChunkMaterialInfo& material);
    const PKPackage* package = nullptr;
    std::unordered_map<uint32_t, std::vector<Usage>> usages;
    std::vector<std::string> warnings;
};
