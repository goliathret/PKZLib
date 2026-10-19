#include "RZHAnim.h"
#include "detail/AnimationReader.h"
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

using namespace pkzlib::animation_detail;

namespace
{
ANValue Interpolate(ANValue a, ANValue b, float t, bool rotation)
{
    float dot = 0;
    if (rotation)
    {
        a = Normalize(a);
        b = Normalize(b);
        for (int i = 0; i < 4; ++i)
            dot += a[i] * b[i];
        if (dot < 0)
        {
            for (float& x : b)
                x = -x;
            dot = -dot;
        }
    }
    float left = 1 - t, right = t;
    if (rotation && dot < .9995f)
    {
        const float angle = std::acos(std::clamp(dot, -1.f, 1.f));
        left = std::sin((1 - t) * angle) / std::sin(angle);
        right = std::sin(t * angle) / std::sin(angle);
    }
    ANValue value{};
    for (int i = 0; i < 4; ++i)
        value[i] = left * a[i] + right * b[i];
    return rotation ? Normalize(value) : value;
}
ANValue SampleChannel(const std::vector<RZHAnim::Segment>& keys, float time, const ANValue& fallback,
                      bool rotation)
{
    auto next = std::upper_bound(keys.begin(), keys.end(), time,
                                 [](float t, const RZHAnim::Segment& key) { return t < key.start; });
    if (next == keys.begin())
        return fallback;
    const RZHAnim::Segment& key = *std::prev(next);
    if (!key.interpolate)
        return key.from;
    const float t =
        key.end > key.start ? std::clamp((time - key.start) / (key.end - key.start), 0.f, 1.f) : 1.f;
    return Interpolate(key.from, key.to, t, rotation);
}
enum class ChannelKind
{
    Translation,
    Rotation,
    Scale
};

struct ChannelFlags
{
    uint16_t current, next, timed, half;
};
ChannelFlags FlagsFor(ChannelKind kind)
{
    switch (kind)
    {
    case ChannelKind::Translation:
        return {1, 2, 4, 8};
    case ChannelKind::Rotation:
        return {512, 1024, 2048, 0};
    case ChannelKind::Scale:
        return {16, 32, 64, 128};
    }
    throw std::runtime_error("Invalid animation channel kind.");
}

ANValue ReadChannelValue(Reader& reader, uint16_t flags, ChannelKind kind)
{
    ANValue value{};
    if (kind == ChannelKind::Rotation)
    {
        reader.Align(2);
        for (float& component : value)
            component = int16_t(reader.U16()) / 32767.f;
        return Normalize(value);
    }
    // ANJobsKF::JobReadKFBone*; these quantization ranges are the EOT Xbox profile.
    const bool byte = kind == ChannelKind::Translation && (flags & 8192);
    const bool half = (flags & FlagsFor(kind).half) != 0;
    if (!byte)
        reader.Align(half ? 2 : 4);
    for (int i = 0; i < 3; ++i)
    {
        if (byte)
            value[i] = int8_t(reader.U8()) * (.05f / 127.f);
        else if (half)
        {
            const float multiplier = kind == ChannelKind::Translation ? 10.f / 32767.f : 1.f / 16383.f;
            value[i] = int16_t(reader.U16()) * multiplier;
        }
        else
            value[i] = reader.F32();
    }
    return value;
}

void ReadChannel(Reader& reader, std::vector<RZHAnim::Segment>& keys, uint16_t flags, ChannelKind kind,
                 float time, float interval)
{
    const auto encoding = FlagsFor(kind);
    if (!(flags & (encoding.current | encoding.next)))
        return;
    RZHAnim::Segment key;
    key.start = time;
    key.end = time + interval;
    if (kind == ChannelKind::Rotation)
        key.from = {0, 0, 0, 1};
    else if (kind == ChannelKind::Scale)
        key.from = {1, 1, 1, 0};
    if (!keys.empty())
        key.from = keys.back().interpolate ? keys.back().to : keys.back().from;
    if (flags & encoding.current)
    {
        if (kind == ChannelKind::Scale && (flags & 256))
            key.from = {1, 1, 1, 0};
        else
            key.from = ReadChannelValue(reader, flags, kind);
    }
    key.interpolate = (flags & encoding.next) != 0;
    key.to = key.interpolate ? ReadChannelValue(reader, flags, kind) : key.from;
    if (flags & encoding.timed)
        key.end = time + reader.U8() * interval;
    keys.push_back(key);
}
void Decode(RZHAnim& clip, const CMChunk& chunk, bool little)
{
    if (chunk.version != 5)
        throw std::runtime_error("Only EOT v5 animation tracks are supported.");
    Reader r(chunk.data, little);
    while (r.at < r.data.size())
    {
        r.limit = r.data.size();
        r.Require(12);
        const size_t begin = r.at;
        const float time = r.F32();
        const uint16_t count = r.U16();
        const uint8_t flags = r.U8(), morphs = r.U8();
        const uint32_t size = r.U32();
        r.Require(size);
        const size_t end = r.at + size;
        r.limit = end;
        if (morphs == 255)
            break;
        if (time < 0 || time > clip.info.duration + clip.info.interval + .01f)
            throw std::runtime_error("Animation frame time is out of range.");
        if (flags & ~3)
            throw std::runtime_error("Unknown animation frame flags.");
        if (flags & 1)
            r.Skip(36);
        if (flags & 2)
        {
            const uint32_t root = r.U32();
            if (root & ~127u)
                throw std::runtime_error("Unknown animation root-motion flags.");
            for (int bit = 0; bit < 7; ++bit)
                if (root & (1u << bit))
                    r.Skip(12);
        }
        for (uint16_t i = 0; i < count; ++i)
        {
            const uint8_t control = r.U8(), index = r.U8();
            uint16_t bits = 0;
            if (index >= clip.tracks.size())
                throw std::runtime_error("Animation track index is out of range.");
            if (!control)
            {
                r.Align(2);
                bits = r.U16();
            }
            else if ((control & 3) == 1)
                bits = (control & 124) << 2;
            else if ((control & 3) == 2)
                bits = (control & 28) << 7;
            else if ((control & 3) == 3)
                bits = ((control & 60) >> 2) | ((control & 64) << 7);
            else
                throw std::runtime_error("Unknown animation track encoding.");
            auto& track = clip.tracks[index];
            ReadChannel(r, track.scale, bits, ChannelKind::Scale, time, clip.info.interval);
            ReadChannel(r, track.translation, bits, ChannelKind::Translation, time, clip.info.interval);
            ReadChannel(r, track.rotation, bits, ChannelKind::Rotation, time, clip.info.interval);
        }
        r.Align(4);
        if (!morphs && r.at != end)
            throw std::runtime_error("Animation frame size does not match its tracks.");
        r.at = end;
        if (r.at <= begin)
            throw std::runtime_error("Invalid animation frame size.");
    }
}
} // namespace

RZHAnim RZHAnim::Load(const PKPackage& package, const CMChunk& resource, LoadMode mode)
{
    const auto* h = resource.FindChild(GenSub_ResourceHeader);
    const auto* info = Find(resource, HAnim_Info);
    if (!h || !info || info->version != 21)
        throw std::runtime_error("Missing EOT v21 animation info.");
    CMChunkResourceHeader header(*h);
    if (header.GetResourceType() != kCMResourceType_HAnim)
        throw std::runtime_error("Resource is not an animation.");
    RZHAnim clip;
    clip.name = header.GetName();
    const auto* post = package.FindRootAtOffset(header.GetDataOffset());
    const auto* ph = post ? post->FindChild(GenSub_ResourceHeader) : nullptr;
    if (!ph || !CMChunkResourceHeader(*ph).IsSameResource(header))
        throw std::runtime_error("Animation post-load resource does not match.");
    Reader r(info->data, package.isLittleEndian);
    r.Require(84); // HAnim_Info v21, consumed by RZHAnim::Load in the engine.
    clip.info.hierarchyCRC = r.U32();
    clip.info.duration = r.F32();
    clip.info.interval = r.F32();
    if (clip.info.duration < 0 || clip.info.duration > 86400 || clip.info.interval <= 0 ||
        clip.info.interval > 1)
        throw std::runtime_error("Invalid animation timing.");
    r.Skip(8);
    clip.info.flags = r.U32();
    clip.info.displacementBone = r.U32();
    for (int i = 0; i < 3; ++i)
        clip.info.displacement.translation[i] = r.F32();
    for (float& value : clip.info.displacement.rotation)
        value = r.F32();
    for (int i = 0; i < 3; ++i)
        clip.info.displacement.scale[i] = r.F32();
    r.Skip(8);
    const size_t count = clip.info.boneCount = r.U32();
    clip.info.morphCount = r.U32();
    if (count > 255)
        throw std::runtime_error("Too many animation tracks.");
    clip.tracks.resize(count);
    const auto* crcs = Find(*post, HAnim_BoneCRC);
    if (count && (!crcs || crcs->data.size() != count * 8))
        throw std::runtime_error("Animation bone CRC table is missing or truncated.");
    if (count)
    {
        Reader table(crcs->data, package.isLittleEndian);
        std::unordered_set<uint32_t> hashes, indices;
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t crc = table.U32(), index = table.U32();
            if (index >= count || !indices.insert(index).second || !hashes.insert(crc).second)
                throw std::runtime_error("Ambiguous animation bone mapping.");
            clip.tracks[index].boneCRC = crc;
        }
    }
    if (mode == LoadMode::Metadata)
        return clip;
    size_t buffers = 0;
    std::function<void(const CMChunk&)> read = [&](const CMChunk& c) {
        if (c.GetMaskedID() == HAnim_Resource)
        {
            Decode(clip, c, package.isLittleEndian);
            ++buffers;
        }
        for (auto& child : c.children)
            read(child);
    };
    read(*post);
    if (!buffers)
        throw std::runtime_error("Animation keyframe buffers are missing.");
    for (auto& track : clip.tracks)
        for (auto* channel : {&track.translation, &track.rotation, &track.scale})
            std::stable_sort(channel->begin(), channel->end(),
                             [](const Segment& a, const Segment& b) { return a.start < b.start; });
    return clip;
}
std::vector<int> RZHAnim::Map(const RZHierarchy& skeleton) const
{
    std::unordered_map<uint32_t, int> bones;
    for (size_t i = 0; i < skeleton.bones.size(); ++i)
        if (!bones.emplace(skeleton.bones[i].crc, int(i)).second)
            throw std::runtime_error("Ambiguous skeleton bone CRC.");
    std::vector<int> map;
    for (auto& track : tracks)
    {
        auto it = bones.find(track.boneCRC);
        map.push_back(it == bones.end() ? -1 : it->second);
    }
    return map;
}
std::vector<CMTransforms> RZHAnim::Sample(const RZHierarchy& skeleton, float time) const
{
    if (IsAdditive())
        throw std::runtime_error(
            "Additive animation requires a base pose; standalone sampling is unsupported.");
    if (!std::isfinite(time))
        throw std::runtime_error("Invalid animation sample time.");
    time = std::clamp(time, 0.f, info.duration);
    auto map = Map(skeleton);
    std::vector<CMTransforms> pose;
    for (auto& bone : skeleton.bones)
        pose.push_back(bone.bind);
    for (size_t i = 0; i < tracks.size(); ++i)
        if (map[i] >= 0)
        {
            const auto& track = tracks[i];
            auto& transform = pose[size_t(map[i])];
            transform.translation = SampleChannel(track.translation, time, transform.translation, false);
            transform.rotation = SampleChannel(track.rotation, time, transform.rotation, true);
            transform.scale = SampleChannel(track.scale, time, transform.scale, false);
        }
    return pose;
}
