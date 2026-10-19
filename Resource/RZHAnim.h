#pragma once
#include "RZHierarchy.h"

struct RZAnimInfo
{
    uint32_t hierarchyCRC = 0, flags = 0, displacementBone = UINT32_MAX;
    uint32_t boneCount = 0, morphCount = 0;
    float duration = 0, interval = 0;
    CMTransforms displacement;
};

class RZHAnim
{
public:
    enum class LoadMode
    {
        Metadata,
        Keyframes
    };

    struct Segment
    {
        float start = 0, end = 0;
        ANValue from{}, to{};
        bool interpolate = false;
    };
    struct Track
    {
        uint32_t boneCRC = 0;
        std::vector<Segment> translation, rotation, scale;
    };

    std::string name;
    RZAnimInfo info;
    std::vector<Track> tracks;

    // EOT v21 info and v5 packed keys. Metadata loading does not decode the key buffers.
    static RZHAnim Load(const PKPackage& package, const CMChunk& resource,
                        LoadMode mode = LoadMode::Keyframes);
    bool IsAdditive() const { return (info.flags & 1) != 0; }
    std::vector<int> Map(const RZHierarchy& skeleton) const;
    std::vector<CMTransforms> Sample(const RZHierarchy& skeleton, float time) const;
};
