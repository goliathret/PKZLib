#pragma once

#include "../Graphics/GRClippable.h"

class GOGameObj
{
public:
    virtual ~GOGameObj() = default;
};

class GO3dObj : public GRClippableObjSphere, public GOGameObj
{
public:
};
