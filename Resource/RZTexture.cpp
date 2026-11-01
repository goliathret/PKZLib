#include "RZTexture.h"

RZTexture::Descriptor RZTexture::Descriptor::Parse(const CMChunk& data)
{
    if (data.GetMaskedID() != Texture_Data || data.data.size() < 8)
        throw std::runtime_error("RZTexture: not a Texture_Data chunk");
    if (data.isLittleEndian && data.data.size() >= kPCHeaderSize + 4 &&
        BUDds::ReadU32LE(data.data.data() + kPCHeaderSize) == BUDds::FourCC('D', 'D', 'S', ' '))
        return ParsePC(data);
    return ParseXbox(data);
}

void RZTexture::Descriptor::Write(CMChunk& data) const
{
    if (IsPC())
        throw std::runtime_error("RZTexture: preserve the original PC DDS wrapper when writing");
    WriteXbox(data);
}

std::vector<uint8_t> RZTexture::DecodeLevel0() const
{
    return desc.IsPC() ? DecodePC() : DecodeXbox();
}

std::vector<uint8_t> RZTexture::ToDds() const
{
    return desc.IsPC() ? gpuData : XboxDds();
}

void RZTexture::ReplaceDds(const std::vector<uint8_t>& file)
{
    const auto image = BUDds::Decode(file);
    if (desc.IsPC())
        ReplacePC(image);
    else
        ReplaceXbox(image);
}

void RZTexture::Reencode(const uint8_t* rgba, uint32_t width, uint32_t height)
{
    if (desc.IsPC())
        ReencodePC(rgba, width, height);
    else
        ReencodeXbox(rgba, width, height);
}

void RZTexture::SetLevels(const std::vector<BUDds::Level>& levels)
{
    if (desc.IsPC())
    {
        auto image = BUDds::Decode(gpuData);
        image.levels = levels;
        ReplacePC(image);
    }
    else
        SetXboxLevels(levels);
}

std::vector<BUDds::Level> RZTexture::Levels(uint32_t count) const
{
    if (!desc.IsPC())
        return XboxLevels(count);
    const auto image = BUDds::Decode(gpuData);
    std::vector<BUDds::Level> levels;
    for (size_t face = 0; face < image.Faces(); ++face)
        for (size_t mip = 0; mip < std::min<size_t>(count, image.Mips()); ++mip)
            levels.push_back(image.levels[face * image.Mips() + mip]);
    return levels;
}

void RZTexture::Store(PKPackage& pkg) const
{
    CMChunk* libraryHeader = FindLibraryHeader(pkg, nameCRC, originalHeader);
    if (!libraryHeader)
        throw std::runtime_error("RZTexture: no texture header for " + name);
    CMChunk* post = pkg.FindRootAtOffset(CMChunkResourceHeader(*libraryHeader).GetDataOffset());
    CMChunk* data = post ? post->FindChild(Texture_Data) : nullptr;
    if (!data)
        throw std::runtime_error("RZTexture: no Texture_Data for " + name);
    CMChunk payload = CMChunk::Leaf(Texture_Data, data->version, {});
    payload.isLittleEndian = data->isLittleEndian;
    if (desc.IsPC())
    {
        if (!data->isLittleEndian || data->version != dataVersion ||
            pcHeader.size() != Descriptor::kPCHeaderSize)
            throw std::runtime_error("RZTexture: the PC texture wrapper changed");
        payload.data = pcHeader;
        payload.AppendBytes(gpuData.data(), gpuData.size());
    }
    else
    {
        if (data->version != kTextureDataVersion)
            throw std::runtime_error("RZTexture: unsupported Xbox texture data version");
        Descriptor stored = desc;
        stored.UpdateLowHeader();
        stored.Write(payload);
        payload.AppendBytes(gpuData.data(), gpuData.size());
    }
    if (payload.data.size() != data->data.size())
        throw std::runtime_error("RZTexture: the new data for " + name + " changes the Texture_Data size");
    data->data = std::move(payload.data);

    const uint32_t crc = PostLoadCRC(data->data, data->isLittleEndian);
    auto updateCRC = [&](CMChunk& header) {
        if (desc.IsPC() && header.version <= 3 && CMChunkResourceHeader(header).GetPostLoadDataCRC() == 0)
            return;
        CMChunkResourceHeader::SetPostLoadDataCRC(header, crc);
    };
    updateCRC(*libraryHeader);
    if (CMChunk* postHeader = post->FindChild(GenSub_ResourceHeader))
        updateCRC(*postHeader);
}

std::vector<uint8_t> RZTexture::HalveRGBA8(const uint8_t* rgba, uint32_t width, uint32_t height)
{
    const uint32_t w = std::max(1u, width / 2), h = std::max(1u, height / 2);
    std::vector<uint8_t> out(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
    {
        const uint32_t y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
        for (uint32_t x = 0; x < w; ++x)
        {
            const uint32_t x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
            for (int c = 0; c < 4; ++c)
            {
                const uint32_t sum =
                    rgba[(size_t(y0) * width + x0) * 4 + c] + rgba[(size_t(y0) * width + x1) * 4 + c] +
                    rgba[(size_t(y1) * width + x0) * 4 + c] + rgba[(size_t(y1) * width + x1) * 4 + c];
                out[(size_t(y) * w + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
    return out;
}

std::vector<uint8_t> RZTexture::ReorderSlices(const std::vector<uint8_t>& source, uint32_t width,
                                              uint32_t height, uint32_t depth, SliceLayout output)
{
    std::vector<uint8_t> result(source.size());
    const size_t rowBytes = size_t(width) * 4;
    for (uint32_t z = 0; z < depth; ++z)
    {
        for (uint32_t y = 0; y < height; ++y)
        {
            const size_t sliceOffset = (size_t(z) * height + y) * rowBytes;
            const size_t stripOffset = (size_t(y) * depth + z) * rowBytes;
            if (output == SliceLayout::HorizontalStrip)
                std::copy_n(source.data() + sliceOffset, rowBytes, result.data() + stripOffset);
            else
                std::copy_n(source.data() + stripOffset, rowBytes, result.data() + sliceOffset);
        }
    }
    return result;
}
