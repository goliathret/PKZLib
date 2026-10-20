#pragma once
#include "RZGeometry.h"

class RZEnvironment
{
public:
    struct Primitive
    {
        uint32_t material = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t stride = 0;
        uint32_t vertexFormat = 0;
        uint32_t vertexOffset = 0;
        uint32_t indexOffset = 0;
        uint16_t vertexBuffer = 0;
        uint16_t indexBuffer = 0;
    };

    struct Part
    {
        std::string name;
        std::vector<Primitive> primitives;
        bool localSpace = false;
    };

    std::string name;
    std::vector<CMChunkMaterialInfo> materials;
    std::vector<Part> parts;
    size_t instanceCount = 0;

    // The package owns the buffers and must outlive this resource view.
    static RZEnvironment Load(const PKPackage& package, const CMChunkResourceHeader& header);
    RZGeometry Geometry(size_t part) const;

private:
    std::vector<const CMChunk*> vertexBuffers;
    std::vector<const CMChunk*> indexBuffers;

    void ReadWorld(const CMChunk& chunk);
    void ReadObjects(const CMChunk& chunk);
    void Validate(const Primitive& primitive) const;
};
