#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "../Package/CMChunk.h"

class RZGeomPrim : public CMChunk
{
public:
    enum VertexFormatFlags : uint32_t
    {
        VF_HasColor         = 0x0002,
        VF_UVCountMask      = 0x00E0,
        VF_UVCountShift     = 5,
        VF_BlendMask        = 0x0700,
        VF_HardwareSkinning = 0x0800,
        VF_HasNormals       = 0x1000,
        VF_Unknown2000      = 0x2000,
        VF_HasExtraAttrib   = 0x10000,
    };

    uint32_t uiMaterialCount = 0;
    uint8_t  pui8MaterialIdxList[4] = {};
    uint32_t pVertexBuffer = 0;
    uint32_t uiVertexStride = 0;
    int32_t  iVertexFormat = 0;
    uint32_t uiUnknownV10 = 0;
    uint32_t uiVertexCount = 0;
    uint32_t pDisplayList = 0;
    uint32_t uiDisplayListSize = 0;
    uint32_t uiFlags = 0;
    uint32_t uiBonePaletteSize = 0;
    uint32_t ppMorphTargets = 0;
    uint32_t psBMeshData = 0;
    uint32_t psBMeshDisplayList = 0;
    uint32_t uiConstantDiffuse = 0;
    std::vector<uint32_t> uiBonePalette;

    RZGeomPrim(const CMChunk& chunk)
        : CMChunk(chunk)
    {
        Parse();
    }

    bool HasNormals() const { return (static_cast<uint32_t>(iVertexFormat) & VF_HasNormals) != 0; }
    bool HasColor() const { return (static_cast<uint32_t>(iVertexFormat) & VF_HasColor) != 0; }
    bool HasExtraAttrib() const { return (static_cast<uint32_t>(iVertexFormat) & VF_HasExtraAttrib) != 0; }
    uint32_t GetUVCount() const { return (static_cast<uint32_t>(iVertexFormat) & VF_UVCountMask) >> VF_UVCountShift; }
    bool UsesHardwareSkinning() const { return (static_cast<uint32_t>(iVertexFormat) & VF_HardwareSkinning) != 0; }
    uint32_t GetBlendMode() const { return (static_cast<uint32_t>(iVertexFormat) & VF_BlendMask) >> 8; }

    bool RequiresSoftwareSkinning() const
    {
        return !UsesHardwareSkinning() && (GetBlendMode() != 0) && (uiBonePaletteSize > 1);
    }

    uint32_t ComputeVertexStride() const
    {
        return 12 + (HasNormals() ? 4 : 0) + (HasExtraAttrib() ? 4 : 0) + (HasColor() ? 4 : 0) + 4 * GetUVCount() +
               (GetBlendMode() != 0 ? 8 : 0);
    }

    uint8_t GetMaterialIndex(uint32_t i) const
    {
        if (i >= 4)
            throw std::out_of_range("RZGeomPrim material index");
        return pui8MaterialIdxList[i];
    }

    uint32_t GetBonePaletteSize() const { return uiBonePaletteSize; }
    uint32_t GetMaterialCount() const { return uiMaterialCount; }
    uint32_t GetVertexCount() const { return uiVertexCount; }
    uint32_t GetDisplayListSize() const { return uiDisplayListSize; }
    uint32_t GetVertexStride() const { return uiVertexStride; }
    uint32_t GetFlags() const { return uiFlags; }
    uint32_t GetConstantDiffuse() const { return uiConstantDiffuse; }
    int32_t GetVertexFormat() const { return iVertexFormat; }

private:
    void Parse()
    {
        size_t offset = 0;
        uiMaterialCount = Read<uint32_t>(offset);
        pVertexBuffer = Read<uint32_t>(offset);
        uiVertexStride = Read<uint32_t>(offset);
        iVertexFormat = Read<int32_t>(offset);
        if (version >= 10)
        {
            uiUnknownV10 = Read<uint32_t>(offset);
            uiVertexCount = Read<uint32_t>(offset);
            pDisplayList = Read<uint32_t>(offset);
            uiDisplayListSize = Read<uint32_t>(offset);
        }
        else
        {
            uiVertexCount = Read<uint32_t>(offset);
            pDisplayList = Read<uint32_t>(offset);
            uiDisplayListSize = Read<uint32_t>(offset);
            uiFlags = Read<uint32_t>(offset);
        }
        uiBonePaletteSize = Read<uint32_t>(offset);
        ppMorphTargets = Read<uint32_t>(offset);
        psBMeshData = Read<uint32_t>(offset);
        psBMeshDisplayList = Read<uint32_t>(offset);
        uiConstantDiffuse = Read<uint32_t>(offset);
        ReadBytes(offset, pui8MaterialIdxList, sizeof(pui8MaterialIdxList));

        if (uiBonePaletteSize > 0x10000)
            throw std::runtime_error("RZGeomPrim: implausible bone palette size");
        uiBonePalette.reserve(uiBonePaletteSize);
        for (uint32_t i = 0; i < uiBonePaletteSize; i++)
            uiBonePalette.push_back(Read<uint32_t>(offset));
    }
};
