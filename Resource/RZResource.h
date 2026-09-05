#pragma once

#include <cstdio>
#include <functional>
#include <vector>

#include "../Package/CMChunk.h"
#include "../Package/PKPackage.h"
#include "ResourceHeader.h"

class RZResourceMgr
{
public:
    enum Res
    {
        kComplete,
        kSkip,
        kIncomplete,
        kError
    };

    virtual ~RZResourceMgr() = default;

    virtual Res OnResource(const CMChunk& library, const CMChunkResourceHeader& header, const CMChunk* payload,
                           PKPackage* pPackage)
    {
        (void)library; (void)header; (void)payload; (void)pPackage;
        ++muiResourcesSeen;
        return kComplete;
    }

    Res LoadChunk(const CMChunk& chunk, PKPackage* pPackage)
    {
        switch (chunk.GetIDToEnum())
        {
        case Gen_HierarchyLibrary:
        case Gen_HAnimLibrary:
        case Gen_GeometryLibrary:
        case Gen_TextureLibrary:
        case Gen_FontLibrary:
        case Gen_ParticleLibrary:
        case Gen_BinaryDataLibrary:
        case Gen_CutSceneLibrary:
        case Gen_AudioSoundLibrary:
        case Gen_AudioCueLibrary:
        case Gen_AudioSampleLibrary:
        case Gen_AudioCategoryLibrary:
        case Gen_AudioRPCLibrary:
        case Gen_MaterialAnimLibrary:
        case Gen_MotionTrailLibrary:
        case Gen_BSplineLibrary:
        case Gen_ParamBlockLibrary:
        case Gen_AudioReverbLibrary:
        case Gen_AnimCueLibrary:
        case Gen_AnimTreeLibrary:
        case Gen_TextStyleLibrary:
        case Gen_CurveLibrary:
        case Gen_ResourcesListLibrary:
        case Gen_ParamTreeLibrary:
        case Gen_ParamGraphLibrary:
        case Gen_AudioInterleavedStreamsLibrary:
        case Gen_Hud3dLibrary:
        case Gen_WwiseAudioLibrary:
        case Gen_HeightMapLibrary:
        case Gen_DecalLibrary:
        case Gen_WwiseAudioCutsceneLibrary:
        case Gen_WwiseMediaLibrary:
        case Gen_SequenceLibrary:
        case Gen_StringTableLibrary:
        case Gen_HUDLibrary:
        case Gen_GameObjLibrary:
            return LoadRezLibrary(chunk, pPackage);

        case Gen_LogicLibrary:
            return LoadLogicLibrary(chunk, pPackage);

        case Gen_AudioLibrary:
        {

            Res result = kComplete;
            for (const CMChunk& sub : chunk.children)
            {
                const Res r = LoadChunk(sub, pPackage);
                if (r != kComplete && r != kSkip)
                    result = r;
            }
            return result;
        }

        default:
            if (mbVerbose)
                std::printf("Unhandled chunk type %s in RZResourceMgr::LoadChunk\n", ToString(chunk.GetIDToEnum()).c_str());
            return kSkip;
        }
    }

    Res LoadPackage(PKPackage& package)
    {
        Res result = kComplete;
        for (const CMChunk& root : package.rootChunks)
        {
            const std::vector<const CMChunk*> libs = root.GetMaskedID() == Root ? Pointers(root.children)
                                                                                 : std::vector<const CMChunk*>{ &root };
            for (const CMChunk* lib : libs)
            {
                const Res r = LoadChunk(*lib, &package);
                if (r != kComplete && r != kSkip)
                    result = r;
            }
        }
        return result;
    }

    uint32_t GetResourcesSeen() const { return muiResourcesSeen; }
    void SetVerbose(bool v) { mbVerbose = v; }

protected:
    Res LoadRezLibrary(const CMChunk& library, PKPackage* pPackage)
    {
        if (!library.GetHasChildren())
            return kSkip;
        Res result = kComplete;
        for (const CMChunk* res : library.FindChildren(GenSub_Resource))
        {
            const CMChunk* h = res->FindChild(GenSub_ResourceHeader);
            if (!h)
            {
                result = kIncomplete;
                continue;
            }
            CMChunkResourceHeader header(*h);
            const CMChunk* payload = nullptr;
            for (const CMChunk& child : res->children)
            {
                if (child.GetMaskedID() != GenSub_ResourceHeader && child.GetMaskedID() != GenSub_RefResourceArray)
                {
                    payload = &child;
                    break;
                }
            }
            const Res r = OnResource(library, header, payload, pPackage);
            if (r == kError)
                return kError;
            if (r == kIncomplete)
                result = kIncomplete;
        }
        return result;
    }

    Res LoadLogicLibrary(const CMChunk& library, PKPackage* pPackage)
    {
        return LoadRezLibrary(library, pPackage);
    }

    static std::vector<const CMChunk*> Pointers(const std::vector<CMChunk>& v)
    {
        std::vector<const CMChunk*> out;
        out.reserve(v.size());
        for (const CMChunk& c : v)
            out.push_back(&c);
        return out;
    }

    uint32_t muiResourcesSeen = 0;
    bool mbVerbose = false;
};
