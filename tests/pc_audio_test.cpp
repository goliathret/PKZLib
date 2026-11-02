#include "../Resource/RZAudioSample.h"
#include <cstdio>

namespace
{
    void Check(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
    void Put16(CMChunk& c, size_t at, uint16_t value)
    {
        c.data.at(at) = uint8_t(value);
        c.data.at(at + 1) = uint8_t(value >> 8);
    }
    void Put32(CMChunk& c, size_t at, uint32_t value)
    {
        Put16(c, at, uint16_t(value));
        Put16(c, at + 2, uint16_t(value >> 16));
    }
    CMChunk Sample(uint16_t channels, uint32_t frames, const std::vector<uint8_t>& data)
    {
        auto c = CMChunk::Leaf(Audio_SampleData, 7, std::vector<uint8_t>(72));
        c.isLittleEndian = true;
        Put32(c, 20, frames);
        Put16(c, 24, 2);
        Put16(c, 26, channels);
        Put32(c, 28, 44100);
        Put16(c, 68, 100);
        Put16(c, 70, uint16_t(-100));
        Put32(c, 64, uint32_t(data.size()));
        c.data.insert(c.data.end(), data.begin(), data.end());
        return c;
    }
    void Samples()
    {
        PKPackage p;
        auto mono = RZAudioSample::FromChunk(p, Sample(1, 3, {0x71, 0x01}));
        Check(mono.IsPC() && mono.data == std::vector<uint8_t>({101, 0, 112, 0, 118, 0}),
              "Mono ADPCM nibble order, predictor or step updates changed");
        const auto wav = mono.BuildWave();
        Check(wav.size() == 50 && wav[20] == 1 && wav[22] == 1 && wav[40] == 6 && wav[44] == 101,
              "PC ADPCM was exported with an encoded or invalid WAV header");
        auto stereo = RZAudioSample::FromChunk(p, Sample(2, 2, {0x71, 0x11}));
        Check(stereo.data == std::vector<uint8_t>({101, 0, 167, 255, 102, 0, 173, 255}),
              "Stereo ADPCM shared predictors or interleaved channels incorrectly");
        auto seek = Sample(1, 3, {0x71, 0x01});
        Put32(seek, 4, 4);
        seek.data.insert(seek.data.begin() + 72, {0, 0, 0, 0});
        Check(RZAudioSample::FromChunk(p, seek).data == mono.data, "Seek table bytes were decoded as audio");
        auto pcm = Sample(1, 2, {});
        pcm.data.resize(68);
        Put16(pcm, 24, 1);
        Put16(pcm, 36, 2);
        Put16(pcm, 38, 16);
        Put32(pcm, 64, 4);
        pcm.data.insert(pcm.data.end(), {1, 2, 3, 4});
        Check(RZAudioSample::FromChunk(p, pcm).data == std::vector<uint8_t>({1, 2, 3, 4}), "PCM samples changed");
        auto bad = Sample(2, 20, {0x71});
        bool rejected = false;
        try { RZAudioSample::FromChunk(p, bad); } catch (const std::exception&) { rejected = true; }
        Check(rejected, "Truncated ADPCM was accepted");
    }
}
int main()
{
    try { Samples(); std::puts("PASS: PC ADPCM, PCM and WAV export"); }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
