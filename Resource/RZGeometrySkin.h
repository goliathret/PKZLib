#pragma once
#include "RZGeometry.h"
#include "RZHierarchy.h"

class RZGeometrySkin
{
public:
    struct Influence
    {
        std::array<uint32_t, 4> bones{};
        std::array<float, 4> weights{};
    };
    std::vector<std::vector<Influence>> meshes;
    uint32_t requiredBoneCount = 0;

    static RZGeometrySkin Load(const PKPackage& package, const CMChunkResourceHeader& header,
                               const RZGeometry& geometry);
    void Validate(const RZHierarchy& skeleton) const;
    void Apply(const RZHierarchy& skeleton, const std::vector<BUMatrix>& world, const RZGeometry& original,
               RZGeometry& output) const;
};
