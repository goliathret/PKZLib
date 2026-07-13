#pragma once

#include <cstdint>
#include <vector>

#include "../Package/CMChunk.h"

class RZGeomPrim : public CMChunk
{
public:
    enum VertexFormatFlags : uint32_t
    {
        VF_HasColor = 0x0002,
        VF_UVCountMask = 0x00E0,
        VF_BlendMask        = 0x0700, // Bits 8-10: number/type of bone influences
        VF_HardwareSkinning = 0x0800, // Hardware skinning enabled
        VF_HasNormals       = 0x1000, // Packed normal present
    };

    uint32_t uiMaterialCount;
    uint8_t* pui8MaterialIdxList;
    uint32_t* pVertexBuffer;
    uint32_t uiVertexStride;
    int32_t iVertexFormat;
    uint32_t uiVertexCount;
    uint32_t* pDisplayList;
    uint32_t uiDisplayListSize;
    uint32_t uiFlags;
    uint32_t uiBonePaletteSize;
    uint32_t* ppMorphTargets;
    uint32_t* psBMeshData;
    uint32_t* psBMeshDisplayList;
    uint32_t uiConstantDiffuse;
    std::vector<uint32_t> uiBonePalette;

    bool HasNormals() const
    {
        return (static_cast<uint32_t>(iVertexFormat) & VF_HasNormals) != 0;
    }

    bool UsesHardwareSkinning() const
    {
        return (static_cast<uint32_t>(iVertexFormat) & VF_HardwareSkinning) != 0;
    }

    uint32_t GetBlendMode() const
    {
        return (static_cast<uint32_t>(iVertexFormat) & VF_BlendMask) >> 8;
    }

    bool RequiresSoftwareSkinning() const
    {
        return !UsesHardwareSkinning() &&
            (GetBlendMode() != 0) &&
            (uiBonePaletteSize > 1);
    }

    uint32_t GetBonePaletteSize() const
    {
        return uiBonePaletteSize;
    }

    uint32_t GetMaterialCount() const
    {
        return uiMaterialCount;
    }

    uint32_t GetVertexCount() const
    {
        return uiVertexCount;
    }

    uint32_t GetDisplayListSize() const
    {
        return uiDisplayListSize;
    }

    uint32_t GetVertexStride() const
    {
        return uiVertexStride;
    }

    uint32_t GetFlags() const
    {
        return uiFlags;
    }

    uint32_t GetConstantDiffuse() const
    {
        return uiConstantDiffuse;
    }

    int32_t GetVertexFormat() const
    {
        return iVertexFormat;
    }
};