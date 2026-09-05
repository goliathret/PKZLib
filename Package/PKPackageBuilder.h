#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "../Resource/RZHud.h"
#include "../Resource/RZStringTable.h"
#include "../Resource/RZTextStyle.h"
#include "../Resource/RZTexture.h"
#include "CMChunk.h"
#include "PKChunkBuilders.h"
#include "PKPackage.h"

class PKPackageBuilder
{
public:
    uint32_t packageId = 0;
    std::string packageName;
    std::string buildStamp;
    std::vector<uint32_t> languages = { 1, 2, 5, 3, 4 };
    std::string exportQuality = "HiRez", exportPlatform = "Xbox360", exportRegion = "WorldWide";
    float lodDistances[4] = { 0.0f, 3.0f, 6.0f, 18.0f };
    bool littleEndian = false;

    std::vector<RZStringTable> stringTables;
    std::vector<RZTexture> textures;
    std::vector<RZHudWindow> windows;
    std::vector<RZTextStyle> textStyles;

    PKPackage Build() const
    {
        if (packageId == 0 || packageId >= 4096)
            throw std::runtime_error("PKPackageBuilder: package id must be 1..4095");

        PKPackage pass1 = Assemble(std::vector<uint64_t>(textures.size(), 0));
        std::vector<uint64_t> offsets(textures.size());
        uint64_t off = pass1.rootChunks.front().SerializedSize();
        for (size_t i = 0; i < textures.size(); ++i)
        {
            offsets[i] = off;
            off += pass1.rootChunks[1 + i].SerializedSize();
        }

        return Assemble(offsets);
    }

private:
    PKPackage Assemble(const std::vector<uint64_t>& textureOffsets) const
    {
        PKPackage pkg;
        pkg.isLittleEndian = littleEndian;
        CMChunk root = PKChunkBuilders::Root(littleEndian);
        root.AddChild(PKChunkBuilders::PackageHeader(packageId, buildStamp.empty() ? packageName : buildStamp, "", littleEndian));
        root.AddChild(PKChunkBuilders::Languages(languages, littleEndian));
        root.AddChild(PKChunkBuilders::ExportContext(exportQuality, exportPlatform, exportRegion, littleEndian));
        root.AddChild(PKChunkBuilders::LodDistances(lodDistances[0], lodDistances[1], lodDistances[2], lodDistances[3], littleEndian));

        struct Slot { uint32_t id; uint16_t version; };
        static const Slot kOrder[] = {
            { Gen_StringTableLibrary, 4 }, { Gen_TextureLibrary, 2 },      { Gen_FontLibrary, 4 },
            { Gen_TextStyleLibrary, 2 },   { Gen_HierarchyLibrary, 3 },    { Gen_AudioCategoryLibrary, 4 },
            { Gen_AudioRPCLibrary, 4 },    { Gen_AudioSoundLibrary, 4 },   { Gen_AudioCueLibrary, 4 },
            { Gen_AudioReverbLibrary, 2 }, { Gen_ParticleLibrary, 4 },     { Gen_HUDLibrary, 4 },
            { Gen_FoliageLibrary, 4 },     { Gen_CutSceneLibrary, 4 },     { Gen_MotionTrailLibrary, 4 },
            { Gen_ParamBlockLibrary, 2 },  { Gen_AnimCueLibrary, 2 },      { Gen_AnimTreeLibrary, 2 },
            { Gen_AnimationGraphLibrary, 2 }, { Gen_CurveLibrary, 1 },     { Gen_ResourcesListLibrary, 1 },
            { Gen_GameObjLibrary, 4 },
        };
        for (const Slot& slot : kOrder)
        {
            if (slot.id == Gen_StringTableLibrary)
            {
                root.AddChild(RZStringTable::BuildLibrary(stringTables, littleEndian));
                continue;
            }
            if (slot.id == Gen_TextureLibrary && !textures.empty())
            {
                CMChunk lib = CMChunk::Container(Gen_TextureLibrary, 2);
                lib.isLittleEndian = littleEndian;
                CMChunk info = CMChunk::Leaf(GenSub_Info, 1, {});
                info.isLittleEndian = littleEndian;
                info.Append<uint32_t>(static_cast<uint32_t>(textures.size()));
                lib.AddChild(std::move(info));
                for (size_t i = 0; i < textures.size(); ++i)
                    lib.AddChild(textures[i].BuildLibraryResource(textureOffsets[i], littleEndian));
                root.AddChild(std::move(lib));
                continue;
            }
            if (slot.id == Gen_TextStyleLibrary && !textStyles.empty())
            {
                root.AddChild(RZTextStyle::BuildLibrary(textStyles, littleEndian));
                continue;
            }
            if (slot.id == Gen_HUDLibrary && !windows.empty())
            {
                root.AddChild(RZHudWindow::BuildLibrary(windows, littleEndian));
                continue;
            }
            CMChunk lib = CMChunk::Container(slot.id, slot.version);
            lib.isLittleEndian = littleEndian;
            CMChunk info = CMChunk::Leaf(GenSub_Info, 1, {});
            info.isLittleEndian = littleEndian;
            info.Append<uint32_t>(0);
            lib.AddChild(std::move(info));
            if (slot.id == Gen_FoliageLibrary)
            {

                CMChunk fh = CMChunk::Leaf(Foliage_LibraryHeader, 1, std::vector<uint8_t>(12, 0));
                fh.isLittleEndian = littleEndian;
                lib.AddChild(std::move(fh));
            }
            root.AddChild(std::move(lib));
        }
        pkg.rootChunks.push_back(std::move(root));
        for (size_t i = 0; i < textures.size(); ++i)
            pkg.rootChunks.push_back(textures[i].BuildPostLoadData(textureOffsets[i], littleEndian));
        return pkg;
    }
};
