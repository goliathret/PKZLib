#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

// Auto-generated resource-type enum + name lookup

#define CM_RESOURCE_TYPES \
    X(kCMResourceType_Invalid, 0) \
    X(kCMResourceType_LogicCode, 1) \
    X(kCMResourceType_Hierarchy, 2) \
    X(kCMResourceType_Geometry, 3) \
    X(kCMResourceType_Texture, 4) \
    X(kCMResourceType_HAnim, 5) \
    X(kCMResourceType_BinaryData, 6) \
    X(kCMResourceType_Font, 7) \
    X(kCMResourceType_ParticleDesc, 8) \
    X(kCMResourceType_AudioRPC_DEPRECATED, 9) \
    X(kCMResourceType_AudioBank, 10) \
    X(kCMResourceType_CutScene, 11) \
    X(kCMResourceType_AudioCue_DEPRECATED, 12) \
    X(kCMResourceType_AudioSound_DEPRECATED, 13) \
    X(kCMResourceType_AudioSample, 14) \
    X(kCMResourceType_AudioCategory_DEPRECATED, 15) \
    X(kCMResourceType_Billboard_DEPRECATED, 16) \
    X(kCMResourceType_MotionTrail, 17) \
    X(kCMResourceType_BSpline_TODEL, 18) \
    X(kCMResourceType_HAnimProc, 19) \
    X(kCMResourceType_Unused, 20) \
    X(kCMResourceType_ParamBlock, 21) \
    X(kCMResourceType_AudioReverb_DEPRECATED, 22) \
    X(kCMResourceType_AnimCue, 23) \
    X(kCMResourceType_AnimTree, 24) \
    X(kCMResourceType_TextStyle, 25) \
    X(kCMResourceType_AnimGraph_DEPRECATED, 26) \
    X(kCMResourceType_Curve, 27) \
    X(kCMResourceType_ResourcesList, 28) \
    X(kCMResourceType_CityBlock, 29) \
    X(kCMResourceType_ParamTree, 30) \
    X(kCMResourceType_ParamGraph, 31) \
    X(kCMResourceType_AudioInterleavedStreams_DEPRECATED, 32) \
    X(kCMResourceType_Unused2, 33) \
    X(kCMResourceType_AudioEvent, 34) \
    X(kCMResourceType_HeightMap, 35) \
    X(kCMResourceType_ProjectedDecalInstance, 36) \
    X(kCMResourceType_ProjectedDecal, 37) \
    X(kCMResourceType_AudioBankCutscene, 38) \
    X(kCMResourceType_Sequence, 39) \
    X(kCMResourceType_BaseCount, 40) \
    X(kCMResourceType_TotalCount, 41) \
    X(kCMResourceType_Environment, 0x86)

enum CMResourceType : uint32_t
{
#define X(name, value) name = value,
    CM_RESOURCE_TYPES
#undef X
};

inline std::string ToString(CMResourceType type)
{
    switch (type)
    {
#define X(name, value) case CMResourceType::name: return #name;
        CM_RESOURCE_TYPES
#undef X
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "Unknown(%u)", static_cast<uint32_t>(type));
    return buf;
}