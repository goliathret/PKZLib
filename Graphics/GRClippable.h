#pragma once
#include <cstdint>

class GRClipTreeNode
{
    BUBox mBBox;
    int32_t miFlags;
    GRClipTreeNode* mpChildren[2];
    uint32_t muiObjCount;
    uint32_t muiCityNodeCount;
    BUBox mBaseBox;

};

class GRClipEntryLink
{
public:
    GRClipEntryLink* pNext;
    GRClipEntryLink* pPrevious;
};

class GRClipEntry : public GRClipEntryLink
{
public:
    uint8_t uiType;
    uint8_t uiFlags;
    uint16_t uiRenderCamMask;
    GRClippable* pOwner;
    GRClipTreeNode* pParentNode;
};

class GRClippable
{
public:
    GRClipEntry* mpClipEntry;
};

class GRClippableRCMask : public GRClippable
{
public:
    uint32_t muiRenderCamMask;
}

class GRClippableObj : public GRClippableRCMask
{
public:

}

class GRClippableObjSphere : public GRClippableObj
{

}