#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <zlib.h>

struct BUCompressHeader
{
    uint32_t uiMagicNumber;
    uint32_t uiSectorSize;
    uint32_t uiTotalHeaderSize;
    uint32_t uiBigChunkSize;
    uint64_t uiCompDataSize;
    uint64_t uiUncompFileSize;
    uint32_t uiNbSectorsCompData;
    uint32_t uiPadding;
};

class BUDecompress
{
public:
    static constexpr uint32_t kMagic = 0xBABEB1B0u;
    static constexpr uint32_t kDefaultSectorSize = 0x8000;
    static constexpr uint32_t kDefaultBigChunkSize = 0x40000;

    BUCompressHeader mCompHeader{};
    uint32_t muiNbSectors = 0;
    std::vector<uint64_t> mSectorTable;
    std::vector<uint32_t> mLookup;
    std::vector<uint8_t>* mpDecompBuffer = nullptr;
    uint32_t muiDecompSize = 0;
    bool mbCopy = false;
    bool mbBigEndian32 = true;
    z_stream_s mzStrm{};

    uint32_t GetNbSectors() const { return muiNbSectors; }
    const uint64_t* GetSectorTable() const { return mSectorTable.data(); }
    uint32_t GetDecompSize() const { return muiDecompSize; }
    bool GetCopy() const { return mbCopy; }
    const z_stream_s& GetStream() const { return mzStrm; }

    static uint32_t ReadU32(const uint8_t* p, bool bigEndian)
    {
        return bigEndian ? (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]
                         : (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
    }
    static uint64_t ReadU64(const uint8_t* p, bool bigEndian)
    {
        return bigEndian ? (uint64_t(ReadU32(p, true)) << 32) | ReadU32(p + 4, true)
                         : (uint64_t(ReadU32(p + 4, false)) << 32) | ReadU32(p, false);
    }

    static bool IsCompressed(const uint8_t* file, size_t size)
    {
        if (size < 28)
            return false;
        return ReadU32(file, true) == kMagic || ReadU32(file, false) == kMagic;
    }

    void ParseHeader(const uint8_t* file, size_t size)
    {
        if (size < 28)
            throw std::runtime_error("BUDecompress: file too small for a header");
        if (ReadU32(file, true) == kMagic)
        {
            mbBigEndian32 = true;
            mCompHeader.uiMagicNumber = kMagic;
            mCompHeader.uiSectorSize = ReadU32(file + 4, true);
            mCompHeader.uiTotalHeaderSize = ReadU32(file + 8, true);
            mCompHeader.uiBigChunkSize = ReadU32(file + 12, true);
            mCompHeader.uiNbSectorsCompData = ReadU32(file + 16, true);
            mCompHeader.uiCompDataSize = ReadU32(file + 20, true);
            mCompHeader.uiUncompFileSize = ReadU32(file + 24, true);
            mCompHeader.uiPadding = 0;
        }
        else if (ReadU32(file, false) == kMagic)
        {
            if (size < 40)
                throw std::runtime_error("BUDecompress: file too small for a 64-bit header");
            mbBigEndian32 = false;
            mCompHeader.uiMagicNumber = kMagic;
            mCompHeader.uiSectorSize = ReadU32(file + 4, false);
            mCompHeader.uiTotalHeaderSize = ReadU32(file + 8, false);
            mCompHeader.uiBigChunkSize = ReadU32(file + 12, false);
            mCompHeader.uiCompDataSize = ReadU64(file + 16, false);
            mCompHeader.uiUncompFileSize = ReadU64(file + 24, false);
            mCompHeader.uiNbSectorsCompData = ReadU32(file + 32, false);
            mCompHeader.uiPadding = ReadU32(file + 36, false);
        }
        else
        {
            throw std::runtime_error("BUDecompress: bad magic");
        }

        const uint32_t sectorSize = mCompHeader.uiSectorSize;
        if (sectorSize == 0 || mCompHeader.uiTotalHeaderSize > size)
            throw std::runtime_error("BUDecompress: bad header sizes");

        muiNbSectors = mCompHeader.uiNbSectorsCompData;
        const uint64_t nbUncompSectors = (mCompHeader.uiUncompFileSize + sectorSize - 1) / sectorSize;

        size_t off = mbBigEndian32 ? 28 : 40;
        const size_t entry = mbBigEndian32 ? 4 : 8;
        if (off + muiNbSectors * entry + nbUncompSectors * 4 > mCompHeader.uiTotalHeaderSize)
            throw std::runtime_error("BUDecompress: tables exceed the header");

        mSectorTable.resize(muiNbSectors);
        for (uint32_t i = 0; i < muiNbSectors; ++i, off += entry)
            mSectorTable[i] = mbBigEndian32 ? ReadU32(file + off, true) : ReadU64(file + off, false);

        mLookup.resize(static_cast<size_t>(nbUncompSectors));
        for (uint64_t j = 0; j < nbUncompSectors; ++j, off += 4)
            mLookup[static_cast<size_t>(j)] = ReadU32(file + off, mbBigEndian32);

        muiDecompSize = static_cast<uint32_t>(mCompHeader.uiUncompFileSize);
    }

    uint64_t SectorStart(uint32_t sector) const { return sector ? mSectorTable[sector - 1] : 0; }
    uint64_t SectorEnd(uint32_t sector) const { return mSectorTable[sector]; }

    size_t DecompressSector(const uint8_t* file, size_t size, uint32_t sector, uint8_t* out, size_t outCap) const
    {
        if (sector >= muiNbSectors)
            throw std::runtime_error("BUDecompress: sector out of range");
        const uint64_t want = SectorEnd(sector) - SectorStart(sector);
        if (want > outCap)
            throw std::runtime_error("BUDecompress: output buffer too small for the sector");

        const uint64_t begin = uint64_t(mCompHeader.uiTotalHeaderSize) + uint64_t(sector) * mCompHeader.uiSectorSize;
        if (begin >= size)
            throw std::runtime_error("BUDecompress: sector beyond the file");
        const size_t avail = static_cast<size_t>(std::min<uint64_t>(mCompHeader.uiSectorSize, size - begin));

        z_stream strm{};
        if (inflateInit(&strm) != Z_OK)
            throw std::runtime_error("BUDecompress: inflateInit failed");
        strm.next_in = const_cast<Bytef*>(file + begin);
        strm.avail_in = static_cast<uInt>(avail);
        strm.next_out = out;
        strm.avail_out = static_cast<uInt>(outCap);

        int rc = Z_OK;
        while (rc == Z_OK && strm.avail_in && strm.avail_out)
            rc = inflate(&strm, Z_NO_FLUSH);
        const size_t produced = strm.total_out;
        inflateEnd(&strm);
        if (produced < want)
            throw std::runtime_error("BUDecompress: sector " + std::to_string(sector) + " produced " +
                                     std::to_string(produced) + " bytes, table expects " + std::to_string(want) +
                                     " (zlib rc " + std::to_string(rc) + ")");
        return static_cast<size_t>(want);
    }

    static std::vector<uint8_t> Decompress(const uint8_t* file, size_t size)
    {
        BUDecompress d;
        d.ParseHeader(file, size);
        std::vector<uint8_t> out;
        out.resize(static_cast<size_t>(d.mCompHeader.uiUncompFileSize));
        std::vector<uint8_t> scratch(std::max<uint32_t>(d.mCompHeader.uiBigChunkSize, d.mCompHeader.uiSectorSize * 8));
        size_t pos = 0;
        for (uint32_t i = 0; i < d.muiNbSectors; ++i)
        {
            const size_t n = d.DecompressSector(file, size, i, scratch.data(), scratch.size());
            if (pos + n > out.size())
                throw std::runtime_error("BUDecompress: sector table overruns the uncompressed size");
            std::memcpy(out.data() + pos, scratch.data(), n);
            pos += n;
        }
        if (pos != out.size())
            throw std::runtime_error("BUDecompress: sector table does not cover the uncompressed size");
        return out;
    }

    static std::vector<uint8_t> Decompress(const std::vector<uint8_t>& file)
    {
        return Decompress(file.data(), file.size());
    }

    void DecompressInto(const uint8_t* file, size_t size, std::vector<uint8_t>& out)
    {
        out = Decompress(file, size);
        mpDecompBuffer = &out;
        muiDecompSize = static_cast<uint32_t>(out.size());
    }
};

struct BUCompressOptions
{
    uint32_t sectorSize = BUDecompress::kDefaultSectorSize;
    uint32_t bigChunkSize = BUDecompress::kDefaultBigChunkSize;
    int level = Z_BEST_SPEED;
    bool bigEndian32 = true;
    uint32_t feedSize = 4096;
};

class BUCompress
{
public:
    using Options = BUCompressOptions;

    static std::vector<uint8_t> Compress(const uint8_t* data, size_t size, const Options& opt = Options())
    {
        if (opt.sectorSize < 1024 || opt.feedSize == 0 || opt.feedSize >= opt.sectorSize / 2 ||
            opt.bigChunkSize < opt.sectorSize)
            throw std::runtime_error("BUCompress: bad options");

        std::vector<std::vector<uint8_t>> sectors;
        std::vector<uint64_t> table;
        size_t pos = 0;
        while (pos < size)
        {

            std::vector<uint8_t> sector(opt.sectorSize, 0);
            z_stream strm{};
            if (deflateInit(&strm, opt.level) != Z_OK)
                throw std::runtime_error("BUCompress: deflateInit failed");
            strm.next_out = sector.data();
            strm.avail_out = static_cast<uInt>(sector.size());
            size_t committed = pos;
            bool sectorFull = false;

            const size_t sectorInputCap = pos + opt.bigChunkSize;
            while (committed < size && committed < sectorInputCap && !sectorFull)
            {
                const size_t piece = std::min<size_t>({ size_t(opt.feedSize), size - committed, sectorInputCap - committed });
                const Bytef* pieceStart = data + committed;
                const uInt outBefore = strm.avail_out;
                strm.next_in = const_cast<Bytef*>(pieceStart);
                strm.avail_in = static_cast<uInt>(piece);
                const int rc = deflate(&strm, Z_SYNC_FLUSH);
                if (rc != Z_OK && rc != Z_BUF_ERROR)
                {
                    deflateEnd(&strm);
                    throw std::runtime_error("BUCompress: deflate failed");
                }
                if (strm.avail_in != 0 || strm.avail_out == 0)
                {

                    sectorFull = true;
                    (void)outBefore;
                    break;
                }
                committed += piece;
            }
            deflateEnd(&strm);
            if (committed == pos)
                throw std::runtime_error("BUCompress: a single piece does not fit a sector");
            sectors.push_back(std::move(sector));
            table.push_back(committed);
            pos = committed;
        }

        const uint32_t nbSectors = static_cast<uint32_t>(sectors.size());
        uint32_t maxSectorSpan = 0;
        {
            uint64_t previous = 0;
            for (uint64_t v : table)
            {
                maxSectorSpan = std::max<uint32_t>(maxSectorSpan, static_cast<uint32_t>(v - previous));
                previous = v;
            }
            if (maxSectorSpan == 0)
                maxSectorSpan = opt.sectorSize;
        }
        const uint64_t nbUncompSectors = (uint64_t(size) + opt.sectorSize - 1) / opt.sectorSize;
        std::vector<uint32_t> lookup(static_cast<size_t>(nbUncompSectors));
        {
            uint32_t s = 0;
            for (uint64_t j = 0; j < nbUncompSectors; ++j)
            {
                const uint64_t lastByte = std::min<uint64_t>((j + 1) * opt.sectorSize, size) - 1;
                while (s + 1 < nbSectors && table[s] <= lastByte)
                    ++s;
                lookup[static_cast<size_t>(j)] = s;
            }
        }

        const size_t fixedHeader = opt.bigEndian32 ? 28 : 40;
        const size_t entry = opt.bigEndian32 ? 4 : 8;
        const size_t tablesSize = fixedHeader + nbSectors * entry + lookup.size() * 4;
        const uint64_t headerSize = ((tablesSize + opt.sectorSize - 1) / opt.sectorSize) * opt.sectorSize;

        uint64_t compDataSize = uint64_t(nbSectors) * opt.sectorSize;
        if (nbSectors)
        {
            const auto& last = sectors.back();
            size_t used = last.size();
            while (used > 0 && last[used - 1] == 0)
                --used;
            compDataSize = uint64_t(nbSectors - 1) * opt.sectorSize + used;
        }

        std::vector<uint8_t> out;
        out.reserve(static_cast<size_t>(headerSize + uint64_t(nbSectors) * opt.sectorSize));
        auto put32 = [&](uint32_t v) {
            if (opt.bigEndian32)
                out.insert(out.end(), { uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) });
            else
                out.insert(out.end(), { uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24) });
        };
        auto put64le = [&](uint64_t v) {
            for (int i = 0; i < 8; ++i)
                out.push_back(uint8_t(v >> (8 * i)));
        };

        put32(BUDecompress::kMagic);
        put32(opt.sectorSize);
        put32(static_cast<uint32_t>(headerSize));
        put32(maxSectorSpan);
        if (opt.bigEndian32)
        {
            put32(nbSectors);
            put32(static_cast<uint32_t>(compDataSize));
            put32(static_cast<uint32_t>(size));
            for (uint64_t v : table)
                put32(static_cast<uint32_t>(v));
        }
        else
        {
            put64le(compDataSize);
            put64le(size);
            put32(nbSectors);
            put32(0);
            for (uint64_t v : table)
                put64le(v);
        }
        for (uint32_t v : lookup)
            put32(v);
        out.resize(static_cast<size_t>(headerSize), 0);
        for (const auto& s : sectors)
            out.insert(out.end(), s.begin(), s.end());
        return out;
    }

    static std::vector<uint8_t> Compress(const std::vector<uint8_t>& data, const Options& opt = Options())
    {
        return Compress(data.data(), data.size(), opt);
    }
};
