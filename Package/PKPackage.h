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
            CMChunk chunk = CMChunk::Read(stream, isLittleEndian);
            pos += static_cast<size_t>(chunk.HeaderSize() + chunk.length);
            rootChunks.push_back(std::move(chunk));
        }
    }

    static bool IsZeroRun(const uint8_t* p, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            if (p[i])
                return false;
        return true;
    }
};
