#pragma once
#include "../Package/CMChunk.h"

struct LODDistancesLayout
{
    float lod0;
    float lod1;
    float lod2;
    float lod3;
};

class LODDistances : public CMChunk
{
public:
    LODDistancesLayout layout{};

    LODDistances(const CMChunk& chunk)
        : CMChunk(chunk)
    {
        SetFields({
            { "LOD0",    FieldType::Float, offsetof(LODDistances, layout) + offsetof(LODDistancesLayout, lod0),   4, VersionFlags::Both },
            { "LOD1",    FieldType::Float, offsetof(LODDistances, layout) + offsetof(LODDistancesLayout, lod1),   4, VersionFlags::Both },
            { "LOD2",    FieldType::Float, offsetof(LODDistances, layout) + offsetof(LODDistancesLayout, lod2),   4, VersionFlags::Both },
            { "LOD3",    FieldType::Float, offsetof(LODDistances, layout) + offsetof(LODDistancesLayout, lod3),   4, VersionFlags::Both },
        });

        Parse();
    };
};