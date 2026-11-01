#pragma once

#include <cstdint>
#include <fstream>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

#include "../BaseUtils/BUDecompress.h"
#include "CMChunk.h"

class PKMemoryStreamBuf : public std::streambuf
{
public:
    PKMemoryStreamBuf(const uint8_t* data, size_t size)
    {
        char* begin = const_cast<char*>(reinterpret_cast<const char*>(data));
        setg(begin, begin, begin + size);
    }

protected:
    pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override
    {
        char* base = eback();
        char* cur = dir == std::ios_base::beg ? base : dir == std::ios_base::cur ? gptr() : egptr();
        char* target = cur + off;
        if (target < base || target > egptr())
            return pos_type(off_type(-1));
        setg(base, target, egptr());
        return pos_type(target - base);
    }
    pos_type seekpos(pos_type pos, std::ios_base::openmode mode) override
    {
        return seekoff(off_type(pos), std::ios_base::beg, mode);
    }
};

class PKPackage
{
public:
    std::vector<CMChunk> rootChunks;
    std::vector<uint8_t> tailBytes;
    std::vector<std::string> warnings;
    bool isLittleEndian = false;

    bool wasCompressed = false;
    BUCompressHeader compressHeader{};

public:
    PKPackage() = default;

    uint64_t GetPackageSize() const
    {
        uint64_t total = tailBytes.size();
        for (const auto& c : rootChunks)
            total += c.SerializedSize();
        return total;
    }

    void ReadFromFile(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
            throw std::runtime_error("Failed to open package file: " + filePath);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        ReadFromMemory(bytes);
    }

    void ReadFromMemory(const std::vector<uint8_t>& bytes)
    {
        wasCompressed = BUDecompress::IsCompressed(bytes.data(), bytes.size());
        if (wasCompressed)
        {
            BUDecompress d;
            d.ParseHeader(bytes.data(), bytes.size());
            compressHeader = d.mCompHeader;
            const std::vector<uint8_t> raw = BUDecompress::Decompress(bytes.data(), bytes.size());
            ParseChunks(raw.data(), raw.size());
        }
        else
        {
            ParseChunks(bytes.data(), bytes.size());
        }
    }

    void ReadFromMemoryAutoEndian(const std::vector<uint8_t>& bytes)
    {
        std::vector<uint8_t> decompressed;
        const std::vector<uint8_t>* raw = &bytes;
        wasCompressed = BUDecompress::IsCompressed(bytes.data(), bytes.size());
        if (wasCompressed)
        {
            BUDecompress decoder;
            decoder.ParseHeader(bytes.data(), bytes.size());
            compressHeader = decoder.mCompHeader;
            decompressed = BUDecompress::Decompress(bytes);
            raw = &decompressed;
        }
        if (raw->size() >= 12)
        {
            const uint32_t big = BUDecompress::ReadU32(raw->data(), true) & CMChunk::kIdMask;
            const uint32_t little = BUDecompress::ReadU32(raw->data(), false) & CMChunk::kIdMask;
            if (big == Root || little == Root)
            {
                isLittleEndian = little == Root;
                ParseChunks(raw->data(), raw->size());
                return;
            }
        }
        isLittleEndian = false;
        try
        {
            ParseChunks(raw->data(), raw->size());
            if (!rootChunks.empty() && rootChunks.front().GetMaskedID() != 0)
                return;
        }
        catch (const std::exception&)
        {
        }
        isLittleEndian = true;
        ParseChunks(raw->data(), raw->size());
    }

    std::vector<uint8_t> Serialize() const
    {
        std::ostringstream out(std::ios::binary);
        for (const auto& chunk : rootChunks)
            chunk.Write(out, isLittleEndian);
        out.write(reinterpret_cast<const char*>(tailBytes.data()), static_cast<std::streamsize>(tailBytes.size()));
        const std::string s = out.str();
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    void WriteToFile(const std::string& filePath, bool compress = false,
                     const BUCompress::Options& options = BUCompress::Options()) const
    {
        std::vector<uint8_t> bytes = Serialize();
        if (compress)
            bytes = BUCompress::Compress(bytes, options);
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())
            throw std::runtime_error("Failed to open package file for writing: " + filePath);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file)
            throw std::runtime_error("Failed writing package file: " + filePath);
        file.close();
    }

    BUCompress::Options ContainerOptions() const
    {
        BUCompress::Options options;
        if (wasCompressed)
        {
            options.sectorSize = compressHeader.uiSectorSize;
            options.bigChunkSize = std::max(compressHeader.uiBigChunkSize, compressHeader.uiSectorSize);
            options.bigEndian32 = compressHeader.bigEndian;
            options.wideOffsets = compressHeader.wideOffsets;
            options.lookupFirst = compressHeader.lookupFirst;
        }
        return options;
    }

    void ComputeLengths()
    {
        for (auto& root : rootChunks)
            root.ComputeLength();
    }

    const CMChunk* FindRootAtOffset(uint64_t fileOffset) const
    {
        for (const auto& root : rootChunks)
        {
            if (root.offset == fileOffset)
                return &root;
            if (root.GetMaskedID() == Root)
                for (const auto& child : root.children)
                    if (child.offset == fileOffset)
                        return &child;
        }
        return nullptr;
    }

    CMChunk* FindRootAtOffset(uint64_t fileOffset)
    {
        return const_cast<CMChunk*>(static_cast<const PKPackage&>(*this).FindRootAtOffset(fileOffset));
    }

    template<typename Predicate>
    const CMChunk* FindChunkIf(Predicate&& matches) const
    {
        for (const auto& root : rootChunks)
        {
            if (matches(root))
                return &root;
            if (const CMChunk* found = root.FindDescendantIf(matches))
                return found;
        }
        return nullptr;
    }

    const CMChunk* FindChunk(uint32_t type) const
    {
        return FindChunkIf([type](const CMChunk& chunk) { return chunk.GetMaskedID() == (type & CMChunk::kIdMask); });
    }

    template<typename Visitor>
    void ForEachChunk(Visitor&& visit) const
    {
        for (const auto& root : rootChunks)
        {
            visit(root);
            root.ForEachDescendant(visit);
        }
    }

    CMChunk* FindRoot(uint32_t type)
    {
        for (auto& c : rootChunks)
            if (c.GetMaskedID() == (type & CMChunk::kIdMask))
                return &c;
        return nullptr;
    }
    const CMChunk* FindRoot(uint32_t type) const
    {
        for (const auto& c : rootChunks)
            if (c.GetMaskedID() == (type & CMChunk::kIdMask))
                return &c;
        return nullptr;
    }

private:
    void ParseChunks(const uint8_t* data, size_t size)
    {
        rootChunks.clear();
        tailBytes.clear();
        warnings.clear();
        PKMemoryStreamBuf buf(data, size);
        std::istream stream(&buf);
        size_t pos = 0;
        while (pos < size)
        {

            if (size - pos < 12 || IsZeroRun(data + pos, std::min<size_t>(12, size - pos)))
            {
                tailBytes.assign(data + pos, data + size);
                break;
            }
            stream.seekg(static_cast<std::streamoff>(pos));
            CMChunk chunk;
            if (RecoverLegacyPCTexturePost(data, size, pos, stream, chunk))
                warnings.push_back("Recovered a malformed texture container length at " +
                                   std::to_string(pos));
            else
            {
                const uint32_t id = BUDecompress::ReadU32(data + pos, !isLittleEndian);
                const bool wide = (id >> 24) == 0x80;
                const size_t headerSize = wide ? 16 : 12;
                if (size - pos < headerSize)
                    throw std::runtime_error("Truncated chunk header at file offset " + std::to_string(pos));
                const uint64_t length = wide ? BUDecompress::ReadU64(data + pos + 8, !isLittleEndian)
                                             : BUDecompress::ReadU32(data + pos + 8, !isLittleEndian);
                if (length > size - pos - headerSize)
                    throw std::runtime_error("Chunk at file offset " + std::to_string(pos) +
                                             " extends beyond the end of the file");
                chunk = CMChunk::Read(stream, isLittleEndian);
            }
            pos += static_cast<size_t>(chunk.HeaderSize() + chunk.length);
            rootChunks.push_back(std::move(chunk));
        }
    }

    // Recover only complete SD PC texture records with an invalid outer length.
    bool RecoverLegacyPCTexturePost(const uint8_t* data, size_t size, size_t pos, std::istream& stream,
                            CMChunk& result)
    {
        if (!isLittleEndian || size - pos < 24 ||
            BUDecompress::ReadU32(data + pos, false) != Gen_PostLoadData ||
            BUDecompress::ReadU32(data + pos + 4, false) >> 16 != 1)
            return false;
        const uint32_t declared = BUDecompress::ReadU32(data + pos + 8, false);
        if (declared <= size - pos - 12)
            return false;
        const size_t header = pos + 12;
        if (BUDecompress::ReadU32(data + header, false) != GenSub_ResourceHeader ||
            BUDecompress::ReadU32(data + header + 4, false) != 3 ||
            BUDecompress::ReadU32(data + header + 8, false) != 88 || size - header < 112)
            return false;
        const size_t texture = header + 100;
        if (BUDecompress::ReadU32(data + texture, false) != Texture_Data ||
            BUDecompress::ReadU32(data + texture + 4, false) != 5)
            return false;
        const uint32_t length = BUDecompress::ReadU32(data + texture + 8, false);
        if (length < 156 || length > size - texture - 12 ||
            BUDecompress::ReadU32(data + texture + 12 + 28, false) != 0x20534444)
            return false;
        result = CMChunk::Container(Gen_PostLoadData, uint16_t(BUDecompress::ReadU32(data + pos + 4, false)));
        result.isLittleEndian = true;
        result.offset = pos;
        stream.seekg(static_cast<std::streamoff>(header));
        result.AddChild(CMChunk::Read(stream, true));
        result.AddChild(CMChunk::Read(stream, true));
        result.ComputeLength();
        return true;
    }

    static bool IsZeroRun(const uint8_t* p, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            if (p[i])
                return false;
        return true;
    }
};
