#pragma once
#include "AnimationTypes.h"
#include "ResourceHeader.h"
#include <string>
#include <vector>

class RZHierarchy
{
public:
    struct Bone
    {
        std::string name;
        uint32_t crc = 0, parent = UINT32_MAX;
        CMTransforms bind;
        BUMatrix localBind, inverseBind;
    };

    std::string name;
    uint32_t crc = 0;
    std::vector<Bone> bones;

    static RZHierarchy Load(const CMChunk& resource, bool littleEndian);
    static RZHierarchy Load(const PKPackage& package, const CMChunkResourceHeader& header);
    std::vector<BUMatrix> WorldPose(const std::vector<CMTransforms>& local) const;
};
