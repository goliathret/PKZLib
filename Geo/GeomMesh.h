#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "../Package/CMChunk.h"
#include "../Data/Field.h"

struct RZGeomPrimLayout
{
    uint32_t uiMaterialCount;
    uint8_t  pui8MaterialIdxList[4];
    uint32_t pVertexBuffer;
    uint32_t uiVertexStride;
    int32_t  iVertexFormat;
    uint32_t uiUnknownV10;
    uint32_t uiVertexCount;
    uint32_t pDisplayList;
    uint32_t uiDisplayListSize;
    uint32_t uiFlags;
    uint32_t uiBonePaletteSize;
    uint32_t ppMorphTargets;
    uint32_t psBMeshData;
    uint32_t psBMeshDisplayList;
    uint32_t uiConstantDiffuse;
};

class RZGeomPrim : public CMChunk
{
public:
    enum VertexFormatFlags : uint32_t
    {
        VF_HasColor          = 0x0002,
        VF_UVCountMask       = 0x00E0,
        VF_UVCountShift      = 5,
        VF_BlendMask         = 0x0700,
        VF_HardwareSkinning  = 0x0800,
        VF_HasNormals        = 0x1000,
        VF_Unknown2000       = 0x2000,
        VF_HasExtraAttrib    = 0x10000,
    };

    RZGeomPrimLayout layout{};
    std::vector<uint32_t> uiBonePalette;

    RZGeomPrim(const CMChunk& chunk)
        : CMChunk(chunk)
    {
        SetFields({
            { "MaterialCount",    FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, uiMaterialCount),     4, VersionFlags::Both },
            { "VertexBuffer",     FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, pVertexBuffer),       4, VersionFlags::Both },
            { "VertexStride",     FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, uiVertexStride),      4, VersionFlags::Both },
            { "VertexFormat",     FieldType::Int32,     offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, iVertexFormat),       4, VersionFlags::Both },
            { "UnknownV10",       FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, uiUnknownV10),        4, VersionFlags::Both },
            { "VertexCount",      FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, uiVertexCount),       4, VersionFlags::Both },
            { "DisplayList",      FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, pDisplayList),        4, VersionFlags::Both },
            { "DisplayListSize",  FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, uiDisplayListSize),   4, VersionFlags::Both },
            { "Flags",            FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, uiFlags),             4, VersionFlags::Both },
            { "BonePaletteSize",  FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, uiBonePaletteSize),   4, VersionFlags::Both },
            { "MorphTargets",     FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, ppMorphTargets),      4, VersionFlags::Both },
            { "BMeshData",        FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, psBMeshData),         4, VersionFlags::Both },
            { "BMeshDisplayList", FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, psBMeshDisplayList),  4, VersionFlags::Both },
            { "ConstantDiffuse",  FieldType::UInt32,    offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, uiConstantDiffuse),   4, VersionFlags::Both },
            { "MaterialIdxList",  FieldType::CharArray, offsetof(RZGeomPrim, layout) + offsetof(RZGeomPrimLayout, pui8MaterialIdxList), 4, VersionFlags::Both },
            });

        // Version 10 inserts a word after the vertex format and no longer stores the flags.
        const char* absent = version >= 10 ? "Flags" : "UnknownV10";
        Fields.erase(std::remove_if(Fields.begin(), Fields.end(),
                                    [absent](const Field& f) { return std::strcmp(f.name, absent) == 0; }),
                     Fields.end());

        Parse();

        if (GetBonePaletteSize() > 0x10000)
            throw std::runtime_error("RZGeomPrim: implausible bone palette size");

        uiBonePalette.reserve(GetBonePaletteSize());
        for (uint32_t i = 0; i < GetBonePaletteSize(); i++)
            uiBonePalette.push_back(Read<uint32_t>(dataOffset));
    }

    uint32_t GetBonePaletteSize() const
    {
        const Field* field = GetField("BonePaletteSize", VersionFlags::Both);
        return field ? GetFieldValue<uint32_t>(*field) : 0;
    }

    uint32_t GetMaterialCount() const
    {
        const Field* field = GetField("MaterialCount", VersionFlags::Both);
        return field ? GetFieldValue<uint32_t>(*field) : 0;
    }

    uint8_t GetMaterialIndex(uint32_t i) const
    {
        if (i >= 4)
            throw std::out_of_range("RZGeomPrim material index");
        return layout.pui8MaterialIdxList[i];
    }

    uint32_t GetVertexCount() const
    {
        const Field* field = GetField("VertexCount", VersionFlags::Both);
        return field ? GetFieldValue<uint32_t>(*field) : 0;
    }

    uint32_t GetDisplayListSize() const
    {
        const Field* field = GetField("DisplayListSize", VersionFlags::Both);
        return field ? GetFieldValue<uint32_t>(*field) : 0;
    }

    uint32_t GetVertexStride() const
    {
        const Field* field = GetField("VertexStride", VersionFlags::Both);
        return field ? GetFieldValue<uint32_t>(*field) : 0;
    }

    uint32_t GetFlags() const
    {
        const Field* field = GetField("Flags", VersionFlags::Both);
        return field ? GetFieldValue<uint32_t>(*field) : 0;
    }

    uint32_t GetConstantDiffuse() const
    {
        const Field* field = GetField("ConstantDiffuse", VersionFlags::Both);
        return field ? GetFieldValue<uint32_t>(*field) : 0;
    }

    int32_t GetVertexFormat() const
    {
        const Field* field = GetField("VertexFormat", VersionFlags::Both);
        return field ? GetFieldValue<int32_t>(*field) : 0;
    }

    bool HasNormals() const { return (static_cast<uint32_t>(GetVertexFormat()) & VF_HasNormals) != 0; }
    bool HasColor() const { return (static_cast<uint32_t>(GetVertexFormat()) & VF_HasColor) != 0; }
    bool HasExtraAttrib() const { return (static_cast<uint32_t>(GetVertexFormat()) & VF_HasExtraAttrib) != 0; }
    uint32_t GetUVCount() const { return (static_cast<uint32_t>(GetVertexFormat()) & VF_UVCountMask) >> VF_UVCountShift; }
    bool UsesHardwareSkinning() const { return (static_cast<uint32_t>(GetVertexFormat()) & VF_HardwareSkinning) != 0; }
    uint32_t GetBlendMode() const { return (static_cast<uint32_t>(GetVertexFormat()) & VF_BlendMask) >> 8; }

    bool RequiresSoftwareSkinning() const
    {
        return !UsesHardwareSkinning() && (GetBlendMode() != 0) && (GetBonePaletteSize() > 1);
    }

    uint32_t ComputeVertexStride() const
    {
        return 12 + (HasNormals() ? 4 : 0) + (HasExtraAttrib() ? 4 : 0) + (HasColor() ? 4 : 0) + 4 * GetUVCount() +
               (GetBlendMode() != 0 ? 8 : 0);
    }

private:

    void Parse()
    {
        dataOffset = 0;

        for (const Field& field : Fields)
        {
            ReadField(field, dataOffset);
        }
    }
};
