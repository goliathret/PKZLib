#pragma once

#include <cstring>
#include <stdexcept>
#include <string>

#include "../Package/CMChunk.h"
#include "ResourceTypes.h"

class CMChunkResourceHeader : public CMChunk
{
public:
    static constexpr size_t kNameSize = 64;
    static constexpr size_t kSize32 = 88;
    static constexpr size_t kSize64 = 92;

    uint32_t uiCRC = 0;
    uint32_t uiResourceType = 0;
    uint32_t uiLanguageMask = 0;
    uint32_t uiQualityLevel = 0;
    uint64_t uiDataOffset = 0;
    uint32_t uiPostLoadDataCRC = 0;
    char     zName[kNameSize] = {};
    bool     bWideOffset = false;

    CMChunkResourceHeader(const CMChunk& chunk)
        : CMChunk(chunk)
    {
        Parse();
    }

    static CMChunk Build(uint32_t crc, uint32_t type, uint32_t languageMask, uint32_t qualityLevel,
                         uint64_t dataOffset, uint32_t postLoadDataCRC, const std::string& name,
                         bool littleEndian = false, bool wideOffset = false, uint16_t version = 5)
    {
        CMChunk c = CMChunk::Leaf(static_cast<uint32_t>(GenSub_ResourceHeader), version, {}, wideOffset);
        c.isLittleEndian = littleEndian;
        c.Append<uint32_t>(crc);
        c.Append<uint32_t>(type);
        c.Append<uint32_t>(languageMask);
        if (wideOffset)
        {
            c.Append<uint32_t>(qualityLevel);
            c.Append<uint64_t>(dataOffset);
        }
        else
        {
            c.Append<uint32_t>(static_cast<uint32_t>(dataOffset));
            c.Append<uint32_t>(qualityLevel);
        }
        c.Append<uint32_t>(postLoadDataCRC);
        char buf[kNameSize] = {};
        std::strncpy(buf, name.c_str(), kNameSize - 1);
        c.AppendBytes(buf, kNameSize);
        return c;
    }

    CMResourceType GetResourceType() const { return static_cast<CMResourceType>(uiResourceType); }
    std::string GetResourceTypeName() const { return ToString(GetResourceType()); }
    uint32_t GetRawResourceType() const { return uiResourceType; }
    uint32_t GetLanguageMask() const { return uiLanguageMask; }
    uint32_t GetQualityLevel() const { return uiQualityLevel; }
    uint32_t GetPostLoadDataCRC() const { return uiPostLoadDataCRC; }
    uint32_t GetCRC() const { return uiCRC; }
    uint32_t GetDataOffset32() const { return static_cast<uint32_t>(uiDataOffset); }
    uint64_t GetDataOffset() const { return uiDataOffset; }

    std::string GetName() const
    {
        return std::string(zName, strnlen(zName, sizeof(zName)));
    }

private:
    void Parse()
    {
        if (data.size() == kSize64)
            bWideOffset = true;
        else if (data.size() == kSize32)
            bWideOffset = false;
        else if (data.size() >= kSize64 && lengthFieldSize == 8)
            bWideOffset = true;
        else if (data.size() >= kSize32)
            bWideOffset = false;
        else
            throw std::runtime_error("Resource header too short (" + std::to_string(data.size()) + " bytes)");

        size_t offset = 0;
        uiCRC             = Read<uint32_t>(offset);
        uiResourceType    = Read<uint32_t>(offset);
        uiLanguageMask    = Read<uint32_t>(offset);
        if (bWideOffset)
        {
            uiQualityLevel = Read<uint32_t>(offset);
            uiDataOffset   = Read<uint64_t>(offset);
        }
        else
        {

            uiDataOffset   = Read<uint32_t>(offset);
            uiQualityLevel = Read<uint32_t>(offset);
        }
        uiPostLoadDataCRC = Read<uint32_t>(offset);
        ReadBytes(offset, zName, sizeof(zName));
        zName[kNameSize - 1] = '\0';
    }
};
