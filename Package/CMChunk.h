#pragma once
#include <cstdint>
#include <vector>
#include <iostream>

#include "CMChunkTypes.h"
#include <stdexcept>

class CMChunk
{
public:
    uint32_t id; // mask with 0x00FFFFFF (GetMaskedID)
    uint16_t hasChildren;
    uint16_t version;
    uint64_t length;

    std::vector<uint8_t> data; // Data of the chunk (Only applied if child chunk.)

    std::vector<CMChunk> children;

    CMChunk() = default;

    CMChunk(uint32_t id, uint16_t hasChildren, uint16_t version, uint64_t length)
        : id(id), hasChildren(hasChildren), version(version), length(length)
    {}

    uint32_t GetMaskedID() const
    {
        return id & 0x00FFFFFF;
    }

    std::vector<CMChunk> FindInChildren(uint32_t searchID) const
    {
        std::vector<CMChunk> foundChunks;
        for (const auto& child : children)
        {
            if (child.GetMaskedID() == searchID)
            {
                foundChunks.push_back(child);
            }
        }
        return foundChunks;
    }

    std::vector<CMChunk> FindInChildren(CMChunkTypes searchType) const
    {
        return FindInChildren(static_cast<uint32_t>(searchType));
    }

    std::vector<CMChunk> GetChildren() const
    {
        return children;
    }

    CMChunkTypes GetIDToEnum() const
    {
        return static_cast<CMChunkTypes>(GetMaskedID());
    }

    uint32_t GetID() const
    {
        return id;
    }

    uint32_t GetVersion() const
    {
        return version;
    }

    uint64_t GetLength() const
    {
        return length;
    }

    bool GetHasChildren() const
    {
        return hasChildren != 0;
    }

    static void ByteSwap(void* data, size_t size)
    {
        uint8_t* bytes = reinterpret_cast<uint8_t*>(data);

        for (size_t i = 0; i < size / 2; i++)
        {
            std::swap(bytes[i], bytes[size - i - 1]);
        }
    }

    static CMChunk Read(std::istream& stream, bool isLittleEndian)
    {
        CMChunk chunk;

        stream.read(reinterpret_cast<char*>(&chunk.id), sizeof(chunk.id));
        stream.read(reinterpret_cast<char*>(&chunk.version), sizeof(chunk.version));
        stream.read(reinterpret_cast<char*>(&chunk.hasChildren), sizeof(chunk.hasChildren));
        stream.read(reinterpret_cast<char*>(&chunk.length), sizeof(chunk.length));

        if (!stream)
            throw std::runtime_error("Failed to read chunk header");

        if (!isLittleEndian)
        {
            ByteSwap(&chunk.id, sizeof(chunk.id));
            ByteSwap(&chunk.version, sizeof(chunk.version));
            ByteSwap(&chunk.hasChildren, sizeof(chunk.hasChildren));
            ByteSwap(&chunk.length, sizeof(chunk.length));
        }

        if (chunk.hasChildren)
        {
            uint64_t bytesRead = 0;

            while (bytesRead < chunk.length)
            {
                auto child = Read(stream, isLittleEndian);

                uint64_t childSize =
                    sizeof(child.id) +
                    sizeof(child.hasChildren) +
                    sizeof(child.version) +
                    sizeof(child.length) +
                    child.length;

                bytesRead += childSize;

                chunk.children.push_back(std::move(child));
            }

            if (bytesRead != chunk.length)
                throw std::runtime_error("Child chunk length mismatch");
        }
        else
        {
            if (chunk.length > 1024ULL * 1024ULL * 1024ULL)
                throw std::runtime_error("Invalid chunk length");

            chunk.data.resize(static_cast<size_t>(chunk.length));

            if (chunk.length > 0)
            {
                stream.read(
                    reinterpret_cast<char*>(chunk.data.data()),
                    static_cast<std::streamsize>(chunk.length)
                );

                if (!stream)
                    throw std::runtime_error("Failed to read chunk data");
            }
        }

        return chunk;
    }
};