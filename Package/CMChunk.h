#pragma once
#include <cstdint>
#include <vector>
#include <iostream>

#include "CMChunkTypes.h"
#include "../Data/Field.h"
#include <stdexcept>

class CMChunk
{
public:
    uint32_t id; // mask with 0x00FFFFFF (GetMaskedID)
    uint16_t hasChildren;
    uint16_t version;
    uint64_t length;

    uint8_t lengthFieldSize = 8;

    uint64_t offset = 0;

    std::vector<uint8_t> data; // Data of the chunk (Only applied if child chunk.)

    std::vector<Field> Fields;

    std::vector<CMChunk> children;

    bool isLittleEndian = false;

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

    const std::vector<CMChunk>& GetChildren() const { return children; }

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

    void SetData(const std::vector<uint8_t>& newData)
    {
        if (hasChildren)
        {
            throw std::runtime_error("Cannot set data for a chunk that has children");
        }
        data = newData;
        length = static_cast<uint64_t>(data.size());
    }

    static void ByteSwap(void* data, size_t size)
    {
        uint8_t* bytes = reinterpret_cast<uint8_t*>(data);

        for (size_t i = 0; i < size / 2; i++)
        {
            std::swap(bytes[i], bytes[size - i - 1]);
        }
    }

    void Write(std::ostream& stream, bool isLittleEndian) const
    {
        uint32_t outID = id;
        uint16_t outHasChildren = hasChildren;
        uint16_t outVersion = version;
        uint64_t outLength = length;

        if (!isLittleEndian)
        {
            ByteSwap(&outID, sizeof(outID));
            ByteSwap(&outVersion, sizeof(outVersion));
            ByteSwap(&outHasChildren, sizeof(outHasChildren));
            ByteSwap(&outLength, sizeof(outLength));
        }

        stream.write(reinterpret_cast<const char*>(&outID), sizeof(outID));
        stream.write(reinterpret_cast<const char*>(&outVersion), sizeof(outVersion));
        stream.write(reinterpret_cast<const char*>(&outHasChildren), sizeof(outHasChildren));
        stream.write(reinterpret_cast<const char*>(&outLength), sizeof(outLength));

        if (hasChildren)
        {
            for (const auto& child : children)
            {
                child.Write(stream, isLittleEndian);
            }
        }
        else
        {
            if (!data.empty())
            {
                stream.write(
                    reinterpret_cast<const char*>(data.data()),
                    data.size()
                );
            }
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

        uint8_t idBytes[sizeof(chunk.id)];
        stream.read(reinterpret_cast<char*>(idBytes), sizeof(idBytes));
        if (!stream)
            throw std::runtime_error("Failed to read chunk header");

        bool useShortLength = (idBytes[0] != 0x80);
        chunk.lengthFieldSize = useShortLength ? 4 : 8;

        std::memcpy(&chunk.id, idBytes, sizeof(chunk.id));
        stream.read(reinterpret_cast<char*>(&chunk.version), sizeof(chunk.version));
        stream.read(reinterpret_cast<char*>(&chunk.hasChildren), sizeof(chunk.hasChildren));
        
        if (useShortLength)
        {
            uint32_t length32 = 0;
            stream.read(reinterpret_cast<char*>(&length32), sizeof(length32));
            if (!stream)
                throw std::runtime_error("Failed to read chunk length");
            if (!isLittleEndian)
                ByteSwap(&length32, sizeof(length32));
            chunk.length = static_cast<uint64_t>(length32);
        }
        else
        {
            stream.read(reinterpret_cast<char*>(&chunk.length), sizeof(chunk.length));
            if (!stream)
                throw std::runtime_error("Failed to read chunk length");
            if (!isLittleEndian)
                ByteSwap(&chunk.length, sizeof(chunk.length));
        }

        if (!stream)
            throw std::runtime_error("Failed to read chunk header");

        if (!isLittleEndian)
        {
            ByteSwap(&chunk.id, sizeof(chunk.id));
            ByteSwap(&chunk.version, sizeof(chunk.version));
            ByteSwap(&chunk.hasChildren, sizeof(chunk.hasChildren));
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
                    child.lengthFieldSize +
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

    void Parse() { size_t offset = 0; for (const Field& field : Fields) { ReadField(field, offset); } }

    template<typename T>
    T Read(size_t& offset)
    {
        if (offset + sizeof(T) > data.size())
        {
            std::cout
                << "Read failed\n"
                << "Offset: 0x" << std::hex << offset << "\n"
                << "Need: 0x" << sizeof(T) << " bytes\n"
                << "Data size: 0x" << data.size() << "\n";

            throw std::runtime_error("RZGeomPrim data out of bounds");
        }

        T value;
        std::memcpy(&value, data.data() + offset, sizeof(T));
        offset += sizeof(T);

        // File is big-endian, system is little-endian
        if (isLittleEndian == false)
        {
            ByteSwap(&value, sizeof(T));
        }

        return value;
    }

    void ReadField(const Field& field, size_t& offset)
    {
        void* dst = reinterpret_cast<char*>(this) + field.objectOffset;

        switch (field.type)
        {
        case FieldType::UInt8:
            *reinterpret_cast<uint8_t*>(dst) = Read<uint8_t>(offset);
            break;

        case FieldType::UInt16:
            *reinterpret_cast<uint16_t*>(dst) = Read<uint16_t>(offset);
            break;

        case FieldType::UInt32:
            *reinterpret_cast<uint32_t*>(dst) = Read<uint32_t>(offset);
            break;

        case FieldType::UInt64:
            *reinterpret_cast<uint64_t*>(dst) = Read<uint64_t>(offset);
            break;

        case FieldType::Int8:
            *reinterpret_cast<int8_t*>(dst) = Read<int8_t>(offset);
            break;

        case FieldType::Int16:
            *reinterpret_cast<int16_t*>(dst) = Read<int16_t>(offset);
            break;

        case FieldType::Int32:
            *reinterpret_cast<int32_t*>(dst) = Read<int32_t>(offset);
            break;

        case FieldType::Int64:
            *reinterpret_cast<int64_t*>(dst) = Read<int64_t>(offset);
            break;

        case FieldType::Float:
            *reinterpret_cast<float*>(dst) = Read<float>(offset);
            break;

        case FieldType::Double:
            *reinterpret_cast<double*>(dst) = Read<double>(offset);
            break;

        case FieldType::CharArray:
            std::memcpy(dst, data.data() + offset, field.fileSize);
            offset += field.fileSize;
            break;
        }
    }

    const Field* GetField(const std::string& name, VersionFlags version) const
    {
        for (const Field& field : Fields)
        {
            if (name == field.name && HasVersion(field.versions, version))
            {
                return &field;
            }
        }

        return nullptr;
    }
    template<typename T>
    T& GetFieldValue(const Field& field)
    {
        return *reinterpret_cast<T*>(
            reinterpret_cast<char*>(this) + field.objectOffset);
    }

    template<typename T>
    const T& GetFieldValue(const Field& field) const
    {
        return *reinterpret_cast<const T*>(
            reinterpret_cast<const char*>(this) + field.objectOffset);
    }

    template<typename T>
    T GetFieldValue(const char* name, VersionFlags version)
    {
        const Field* field = GetField(name, version);

        if (!field)
            return T{};

        return GetFieldValue<T>(*field);
    }
protected:
    void SetFields(std::initializer_list<Field> fields)
    {
        Fields.assign(fields.begin(), fields.end());
    }
    size_t dataOffset = 0;
};