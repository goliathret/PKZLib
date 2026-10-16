#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "../Package/CMChunk.h"
#include "../Package/PKPackage.h"
#include "ResourceHeader.h"

class RZAudioSample
{
public:
    struct Xma2Format
    {
        static constexpr uint16_t kFormatTag = 0x0166;
        static constexpr size_t kSize = 52;
        static constexpr uint16_t kExtraSize = 34;

        uint16_t formatTag = 0;
        uint16_t channels = 0;
        uint32_t samplesPerSec = 0;
        uint32_t avgBytesPerSec = 0;
        uint16_t blockAlign = 0;
        uint16_t bitsPerSample = 0;
        uint16_t cbSize = 0;
        uint16_t numStreams = 0;
        uint32_t channelMask = 0;
        uint32_t samplesEncoded = 0;
        uint32_t bytesPerBlock = 0;
        uint32_t playBegin = 0;
        uint32_t playLength = 0;
        uint32_t loopBegin = 0;
        uint32_t loopLength = 0;
        uint8_t loopCount = 0;
        uint8_t encoderVersion = 0;
        uint16_t blockCount = 0;

        static Xma2Format Parse(const CMChunk& chunk, size_t offset)
        {
            if (offset > chunk.data.size() || chunk.data.size() - offset < kSize)
                throw std::runtime_error("RZAudioSample: truncated XMA2 format");
            Xma2Format f;
            f.formatTag = chunk.Read<uint16_t>(offset);
            f.channels = chunk.Read<uint16_t>(offset);
            f.samplesPerSec = chunk.Read<uint32_t>(offset);
            f.avgBytesPerSec = chunk.Read<uint32_t>(offset);
            f.blockAlign = chunk.Read<uint16_t>(offset);
            f.bitsPerSample = chunk.Read<uint16_t>(offset);
            f.cbSize = chunk.Read<uint16_t>(offset);
            f.numStreams = chunk.Read<uint16_t>(offset);
            f.channelMask = chunk.Read<uint32_t>(offset);
            f.samplesEncoded = chunk.Read<uint32_t>(offset);
            f.bytesPerBlock = chunk.Read<uint32_t>(offset);
            f.playBegin = chunk.Read<uint32_t>(offset);
            f.playLength = chunk.Read<uint32_t>(offset);
            f.loopBegin = chunk.Read<uint32_t>(offset);
            f.loopLength = chunk.Read<uint32_t>(offset);
            f.loopCount = chunk.Read<uint8_t>(offset);
            f.encoderVersion = chunk.Read<uint8_t>(offset);
            f.blockCount = chunk.Read<uint16_t>(offset);
            return f;
        }

        bool IsValid() const { return formatTag == kFormatTag && channels > 0 && samplesPerSec > 0; }
    };

    std::string name;
    Xma2Format format;
    std::vector<uint8_t> data;

    static bool IsSampleChunk(const CMChunk& chunk)
    {
        return chunk.GetMaskedID() == Audio_SampleData || chunk.GetMaskedID() == CutScene_Audio_SampleData;
    }

    static RZAudioSample Load(const PKPackage& pkg, const CMChunkResourceHeader& header)
    {
        const CMChunk* post = pkg.FindRootAtOffset(header.GetDataOffset());
        const CMChunk* chunk = post && post->GetMaskedID() == Gen_PostLoadData ? post->FindChild(Audio_SampleData) : nullptr;
        if (!chunk)
            throw std::runtime_error("RZAudioSample: no Audio_SampleData at the data offset of " + header.GetName());
        RZAudioSample sample = FromChunk(pkg, *chunk);
        sample.name = header.GetName();
        return sample;
    }

    static RZAudioSample FromChunk(const PKPackage& pkg, const CMChunk& chunk)
    {
        RZAudioSample sample;
        if (chunk.GetMaskedID() == Audio_SampleData)
            sample.ReadSampleData(chunk);
        else if (chunk.GetMaskedID() == CutScene_Audio_SampleData)
            sample.ReadCutsceneData(pkg, chunk);
        else
            throw std::runtime_error("RZAudioSample: not an audio sample chunk");
        if (!sample.format.IsValid() || sample.data.empty())
            throw std::runtime_error("RZAudioSample: not a valid XMA2 sample");
        return sample;
    }

    std::vector<uint8_t> BuildWave() const
    {
        std::vector<uint8_t> out;
        out.reserve(12 + 8 + Xma2Format::kSize + 8 + data.size());
        const auto bytes = [&](const char* text) { out.insert(out.end(), text, text + 4); };
        const auto u8 = [&](uint8_t value) { out.push_back(value); };
        const auto u16 = [&](uint16_t value) {
            u8(uint8_t(value));
            u8(uint8_t(value >> 8));
        };
        const auto u32 = [&](uint32_t value) {
            u16(uint16_t(value));
            u16(uint16_t(value >> 16));
        };

        bytes("RIFF");
        u32(0);
        bytes("WAVE");
        bytes("fmt ");
        u32(uint32_t(Xma2Format::kSize));
        u16(format.formatTag);
        u16(format.channels);
        u32(format.samplesPerSec);
        u32(format.avgBytesPerSec);
        u16(format.blockAlign);
        u16(format.bitsPerSample);
        u16(format.cbSize ? format.cbSize : Xma2Format::kExtraSize);
        u16(format.numStreams);
        u32(format.channelMask);
        u32(format.samplesEncoded);
        u32(format.bytesPerBlock);
        u32(format.playBegin);
        u32(format.playLength);
        u32(format.loopBegin);
        u32(format.loopLength);
        u8(format.loopCount);
        u8(format.encoderVersion);
        u16(format.blockCount);
        bytes("data");
        u32(uint32_t(data.size()));
        out.insert(out.end(), data.begin(), data.end());

        const uint32_t riffSize = uint32_t(out.size() - 8);
        for (int i = 0; i < 4; ++i)
            out[4 + size_t(i)] = uint8_t(riffSize >> (8 * i));
        return out;
    }

private:
    static constexpr size_t kSampleDataHeaderSize = 80;
    static constexpr size_t kCutsceneDataHeaderSize = 20;
    static constexpr size_t kCutsceneHeaderFormatOffset = 24;

    void ReadSampleData(const CMChunk& chunk)
    {
        if (chunk.data.size() < kSampleDataHeaderSize)
            throw std::runtime_error("RZAudioSample: truncated Audio_SampleData");
        size_t at = 4;
        const uint32_t seekTableSize = chunk.Read<uint32_t>(at);
        at = 24;
        const uint32_t dataSize = chunk.Read<uint32_t>(at);
        format = Xma2Format::Parse(chunk, 28);
        AssignData(chunk, kSampleDataHeaderSize + size_t(seekTableSize), dataSize);
    }

    void ReadCutsceneData(const PKPackage& pkg, const CMChunk& chunk)
    {
        if (chunk.data.size() < kCutsceneDataHeaderSize)
            throw std::runtime_error("RZAudioSample: truncated CutScene_Audio_SampleData");
        size_t at = 16;
        const uint32_t dataSize = chunk.Read<uint32_t>(at);
        AssignData(chunk, kCutsceneDataHeaderSize, dataSize);

        const CMChunk* header = pkg.FindChunk(CutScene_Audio_SampleHeader);
        if (!header || header->data.size() < kCutsceneHeaderFormatOffset + Xma2Format::kSize)
            throw std::runtime_error("RZAudioSample: no CutScene_Audio_SampleHeader for the cutscene sample");
        format = Xma2Format::Parse(*header, kCutsceneHeaderFormatOffset);
    }

    void AssignData(const CMChunk& chunk, size_t offset, uint32_t size)
    {
        if (offset > chunk.data.size() || size > chunk.data.size() - offset)
            throw std::runtime_error("RZAudioSample: the sample payload exceeds its chunk");
        data.assign(chunk.data.begin() + std::ptrdiff_t(offset), chunk.data.begin() + std::ptrdiff_t(offset + size));
    }
};
