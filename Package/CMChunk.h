#pragma once
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "CMChunkTypes.h"

class CMChunk
{
public:
    static constexpr uint32_t kWideLengthFlag = 0x80000000u;
    static constexpr uint32_t kIdMask = 0x00FFFFFFu;

    uint32_t id = 0;
    uint16_t hasChildren = 0;
    uint16_t version = 0;
    uint64_t length = 0;

    uint8_t lengthFieldSize = 4;

    uint64_t offset = 0;

    std::vector<uint8_t> data;
    std::vector<CMChunk> children;

    bool isLittleEndian = false;

    CMChunk() = default;

    CMChunk(uint32_t id, uint16_t hasChildren, uint16_t version, uint64_t length)
        : id(id), hasChildren(hasChildren), version(version), length(length)
    {
        lengthFieldSize = (id & kWideLengthFlag) ? 8 : 4;
    }

    static CMChunk Leaf(uint32_t type, uint16_t version, std::vector<uint8_t> payload, bool wideLength = false)
    {
        CMChunk c;
        c.id = (type & kIdMask) | (wideLength ? kWideLengthFlag : 0);
        c.version = version;
        c.hasChildren = 0;
        c.lengthFieldSize = wideLength ? 8 : 4;
        c.data = std::move(payload);
        c.length = c.data.size();
        return c;
    }

    static CMChunk Container(uint32_t type, uint16_t version, bool wideLength = false)
    {
        CMChunk c;
        c.id = (type & kIdMask) | (wideLength ? kWideLengthFlag : 0);
        c.version = version;
        c.hasChildren = 1;
        c.lengthFieldSize = wideLength ? 8 : 4;
        return c;
    }

    CMChunk& AddChild(CMChunk child)
    {
        if (!hasChildren)
            throw std::runtime_error("Cannot add a child to a leaf chunk");
        children.push_back(std::move(child));
        return children.back();
    }

    uint32_t GetMaskedID() const { return id & kIdMask; }
    CMChunkTypes GetIDToEnum() const { return static_cast<CMChunkTypes>(GetMaskedID()); }
    uint32_t GetID() const { return id; }
    uint32_t GetVersion() const { return version; }
    uint64_t GetLength() const { return length; }
    bool GetHasChildren() const { return hasChildren != 0; }
    bool HasWideLength() const { return lengthFieldSize == 8; }
    uint32_t HeaderSize() const { return 8u + lengthFieldSize; }

    std::vector<CMChunk> FindInChildren(uint32_t searchID) const
    {
        std::vector<CMChunk> found;
        for (const auto& child : children)
            if (child.GetMaskedID() == (searchID & kIdMask))
                found.push_back(child);
        return found;
    }
    std::vector<CMChunk> FindInChildren(CMChunkTypes searchType) const
    {
        return FindInChildren(static_cast<uint32_t>(searchType));
    }

    std::vector<CMChunk*> FindChildren(uint32_t searchID)
    {
        std::vector<CMChunk*> found;
        for (auto& child : children)
            if (child.GetMaskedID() == (searchID & kIdMask))
                found.push_back(&child);
        return found;
    }
    std::vector<const CMChunk*> FindChildren(uint32_t searchID) const
    {
        std::vector<const CMChunk*> found;
        for (const auto& child : children)
            if (child.GetMaskedID() == (searchID & kIdMask))
                found.push_back(&child);
        return found;
    }
    CMChunk* FindChild(uint32_t searchID)
    {
        for (auto& child : children)
            if (child.GetMaskedID() == (searchID & kIdMask))
                return &child;
        return nullptr;
    }
    const CMChunk* FindChild(uint32_t searchID) const
    {
        for (const auto& child : children)
            if (child.GetMaskedID() == (searchID & kIdMask))
                return &child;
        return nullptr;
    }

    std::vector<CMChunk> GetChildren() const { return children; }

    void SetData(const std::vector<uint8_t>& newData)
    {
        if (hasChildren)
            throw std::runtime_error("Cannot set data for a chunk that has children");
        data = newData;
        length = static_cast<uint64_t>(data.size());
    }

    uint64_t PayloadSize() const
    {
        if (!hasChildren)
            return data.size();
        uint64_t total = 0;
        for (const auto& child : children)
            total += child.SerializedSize();
        return total;
    }

    uint64_t SerializedSize() const { return HeaderSize() + PayloadSize(); }

    uint64_t ComputeLength()
    {
        if (hasChildren)
        {
            uint64_t total = 0;
            for (auto& child : children)
                total += child.HeaderSize() + child.ComputeLength();
            length = total;
        }
        else
        {
            length = data.size();
        }
        return length;
    }

    static void ByteSwap(void* data, size_t size)
    {
        uint8_t* bytes = reinterpret_cast<uint8_t*>(data);
        for (size_t i = 0; i < size / 2; i++)
            std::swap(bytes[i], bytes[size - i - 1]);
    }

    void Write(std::ostream& stream, bool isLittleEndian) const
    {
        const uint64_t payload = PayloadSize();
        if (lengthFieldSize == 4 && payload > 0xFFFFFFFFull)
            throw std::runtime_error("Chunk payload exceeds a 32-bit length field");

        uint32_t outID = (id & kIdMask) | (lengthFieldSize == 8 ? kWideLengthFlag : 0u);
        uint16_t outHasChildren = hasChildren;
        uint16_t outVersion = version;

        if (!isLittleEndian)
        {
            ByteSwap(&outID, sizeof(outID));
            ByteSwap(&outVersion, sizeof(outVersion));
            ByteSwap(&outHasChildren, sizeof(outHasChildren));
        }

        stream.write(reinterpret_cast<const char*>(&outID), sizeof(outID));
        stream.write(reinterpret_cast<const char*>(&outVersion), sizeof(outVersion));
        stream.write(reinterpret_cast<const char*>(&outHasChildren), sizeof(outHasChildren));

        if (lengthFieldSize == 8)
        {
            uint64_t outLength = payload;
            if (!isLittleEndian)
                ByteSwap(&outLength, sizeof(outLength));
            stream.write(reinterpret_cast<const char*>(&outLength), sizeof(outLength));
        }
        else
        {
            uint32_t outLength = static_cast<uint32_t>(payload);
            if (!isLittleEndian)
                ByteSwap(&outLength, sizeof(outLength));
            stream.write(reinterpret_cast<const char*>(&outLength), sizeof(outLength));
        }

        if (hasChildren)
        {
            for (const auto& child : children)
                child.Write(stream, isLittleEndian);
        }
        else if (!data.empty())
        {
            stream.write(reinterpret_cast<const char*>(data.data()),
                         static_cast<std::streamsize>(data.size()));
        }

        if (!stream)
            throw std::runtime_error("Failed writing chunk");
    }

    static CMChunk Read(std::istream& stream, bool isLittleEndian)
    {
        CMChunk chunk;
        chunk.isLittleEndian = isLittleEndian;

        std::streampos startPos = stream.tellg();
        if (startPos == std::streampos(-1))
            throw std::runtime_error("Failed to get stream position for chunk offset");
        chunk.offset = static_cast<uint64_t>(startPos);

        uint32_t rawID = 0;
        stream.read(reinterpret_cast<char*>(&rawID), sizeof(rawID));
        stream.read(reinterpret_cast<char*>(&chunk.version), sizeof(chunk.version));
        stream.read(reinterpret_cast<char*>(&chunk.hasChildren), sizeof(chunk.hasChildren));
        if (!stream)
            throw std::runtime_error("Failed to read chunk header");

        if (!isLittleEndian)
        {
            ByteSwap(&rawID, sizeof(rawID));
            ByteSwap(&chunk.version, sizeof(chunk.version));
            ByteSwap(&chunk.hasChildren, sizeof(chunk.hasChildren));
        }
        chunk.id = rawID;

        const bool wideLength = (rawID >> 24) == 0x80;
        chunk.lengthFieldSize = wideLength ? 8 : 4;

        if (wideLength)
        {
            stream.read(reinterpret_cast<char*>(&chunk.length), sizeof(chunk.length));
            if (!stream)
                throw std::runtime_error("Failed to read chunk length");
            if (!isLittleEndian)
                ByteSwap(&chunk.length, sizeof(chunk.length));
        }
        else
        {
            uint32_t length32 = 0;
            stream.read(reinterpret_cast<char*>(&length32), sizeof(length32));
            if (!stream)
                throw std::runtime_error("Failed to read chunk length");
            if (!isLittleEndian)
                ByteSwap(&length32, sizeof(length32));
            chunk.length = length32;
        }

        if (chunk.hasChildren)
        {
            uint64_t bytesRead = 0;
            while (bytesRead < chunk.length)
            {
                CMChunk child = Read(stream, isLittleEndian);
                bytesRead += child.HeaderSize() + child.length;
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
                stream.read(reinterpret_cast<char*>(chunk.data.data()),
                            static_cast<std::streamsize>(chunk.length));
                if (!stream)
                    throw std::runtime_error("Failed to read chunk data");
            }
        }

        return chunk;
    }

    template<typename T>
    T Read(size_t& offset) const
    {
        if (offset + sizeof(T) > data.size())
        {
            throw std::runtime_error("Chunk payload read out of bounds (offset 0x" +
                                     ToHex(offset) + ", need " + std::to_string(sizeof(T)) +
                                     " bytes, have 0x" + ToHex(data.size()) + ")");
        }
        T value;
        std::memcpy(&value, data.data() + offset, sizeof(T));
        offset += sizeof(T);
        if (!isLittleEndian)
            ByteSwap(&value, sizeof(T));
        return value;
    }

    void ReadBytes(size_t& offset, void* dst, size_t count) const
    {
        if (offset + count > data.size())
            throw std::runtime_error("Chunk payload read out of bounds (offset 0x" + ToHex(offset) +
                                     ", need " + std::to_string(count) + " bytes, have 0x" +
                                     ToHex(data.size()) + ")");
        std::memcpy(dst, data.data() + offset, count);
        offset += count;
    }

    template<typename T>
    void Append(T value)
    {
        if (hasChildren)
            throw std::runtime_error("Cannot append data to a container chunk");
        if (!isLittleEndian)
            ByteSwap(&value, sizeof(T));
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
        data.insert(data.end(), p, p + sizeof(T));
        length = data.size();
    }

    void AppendBytes(const void* src, size_t count)
    {
        if (hasChildren)
            throw std::runtime_error("Cannot append data to a container chunk");
        const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
        data.insert(data.end(), p, p + count);
        length = data.size();
    }

private:
    static std::string ToHex(uint64_t v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(v));
        return buf;
    }
};
