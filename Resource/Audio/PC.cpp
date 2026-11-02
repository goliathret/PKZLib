#include "../RZAudioSample.h"
#include <algorithm>
#include <array>

namespace
{
    constexpr int kStep[] = {
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
        34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
        157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
        724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
        2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
        9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
    constexpr int kIndex[] = {-1, -1, -1, -1, 2, 4, 6, 8};

    struct AdpcmState
    {
        int sample = 0, index = 0;
        int16_t Decode(uint8_t nibble)
        {
            const int step = kStep[index];
            int delta = step >> 3;
            if (nibble & 1) delta += step >> 2;
            if (nibble & 2) delta += step >> 1;
            if (nibble & 4) delta += step;
            sample = std::clamp(sample + ((nibble & 8) ? -delta : delta), -32768, 32767);
            index = std::clamp(index + kIndex[nibble & 7], 0, 88);
            return int16_t(sample);
        }
    };
}

void RZAudioSample::ReadPCSampleData(const CMChunk& chunk)
{
    if (chunk.version != 7 || chunk.data.size() < 68)
        throw std::runtime_error("RZAudioSample: unsupported PC sample header");
    size_t at = 24;
    format.formatTag = chunk.Read<uint16_t>(at);
    format.channels = chunk.Read<uint16_t>(at);
    format.samplesPerSec = chunk.Read<uint32_t>(at);
    format.avgBytesPerSec = chunk.Read<uint32_t>(at);
    format.blockAlign = chunk.Read<uint16_t>(at);
    format.bitsPerSample = chunk.Read<uint16_t>(at);
    if (!format.channels || format.channels > 8 || !format.samplesPerSec || format.samplesPerSec > 192000)
        throw std::runtime_error("RZAudioSample: invalid PC audio format");
    at = 64;
    const auto dataSize = chunk.Read<uint32_t>(at);
    if (format.formatTag == 2)
    {
        at = 4;
        const auto seekSize = chunk.Read<uint32_t>(at);
        at = 20;
        const auto frames = chunk.Read<uint32_t>(at);
        if (format.channels > 2 || chunk.data.size() < 72 || !frames ||
            uint64_t(frames) * format.channels > uint64_t(dataSize) * 2)
            throw std::runtime_error("RZAudioSample: invalid PC ADPCM dimensions");
        AssignData(chunk, 72 + size_t(seekSize), dataSize);
        std::array<AdpcmState, 2> state;
        for (size_t ch = 0; ch < format.channels; ++ch)
        {
            at = 68 + ch * 2;
            state[ch].sample = chunk.Read<int16_t>(at);
        }
        std::vector<uint8_t> pcm(size_t(frames) * format.channels * 2);
        // SD stores raw IMA nibbles, low first.
        for (size_t i = 0; i < pcm.size() / 2; ++i)
        {
            const uint8_t nibble = (data[i / 2] >> ((i % 2) * 4)) & 15;
            const auto sample = uint16_t(state[i % format.channels].Decode(nibble));
            pcm[i * 2] = uint8_t(sample);
            pcm[i * 2 + 1] = uint8_t(sample >> 8);
        }
        data = std::move(pcm);
        format.formatTag = 1;
        format.bitsPerSample = 16;
        format.blockAlign = format.channels * 2;
        format.avgBytesPerSec = format.samplesPerSec * format.blockAlign;
        CMChunk wave;
        wave.isLittleEndian = true;
        wave.Append<uint16_t>(1);
        wave.Append(format.channels);
        wave.Append(format.samplesPerSec);
        wave.Append(format.avgBytesPerSec);
        wave.Append(format.blockAlign);
        wave.Append(format.bitsPerSample);
        pcWaveFormat = std::move(wave.data);
    }
    else if (format.formatTag == 1 || format.formatTag == 0xFFFE)
    {
        AssignData(chunk, 68, dataSize);
        const size_t formatSize = format.formatTag == 1 ? 16 : 40;
        pcWaveFormat.assign(chunk.data.begin() + 24, chunk.data.begin() + 24 + formatSize);
        if (!format.blockAlign || data.size() % format.blockAlign)
            throw std::runtime_error("RZAudioSample: invalid PC PCM data length");
    }
    else
        throw std::runtime_error("RZAudioSample: unsupported PC codec " + std::to_string(format.formatTag));
}

std::vector<uint8_t> RZAudioSample::BuildPCWave() const
{
    CMChunk wave;
    wave.isLittleEndian = true;
    wave.AppendBytes("RIFF", 4);
    wave.Append<uint32_t>(0);
    wave.AppendBytes("WAVEfmt ", 8);
    wave.Append<uint32_t>(uint32_t(pcWaveFormat.size()));
    wave.AppendBytes(pcWaveFormat.data(), pcWaveFormat.size());
    wave.AppendBytes("data", 4);
    wave.Append<uint32_t>(uint32_t(data.size()));
    wave.AppendBytes(data.data(), data.size());
    if (data.size() & 1)
        wave.data.push_back(0);
    const auto size = uint32_t(wave.data.size() - 8);
    for (size_t i = 0; i < 4; ++i)
        wave.data[4 + i] = uint8_t(size >> (i * 8));
    return std::move(wave.data);
}
