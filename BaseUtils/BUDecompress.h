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
};

class BUCompress
{
public:
    using Options = BUCompressOptions;

    static constexpr uint8_t kChunkPadding = 0xDA;

    // Reproduces the retail packer's output byte for byte.
    static std::vector<uint8_t> Compress(const uint8_t* data, size_t size, const Options& opt = Options())
    {
        if (opt.sectorSize < 1024 || opt.bigChunkSize < opt.sectorSize)
            throw std::runtime_error("BUCompress: bad options");

        std::vector<std::vector<uint8_t>> sectors;
        std::vector<uint64_t> table;
        std::vector<uint8_t> inflated(opt.bigChunkSize);
        size_t lastUsed = 0;
        for (size_t pos = 0; pos < size;)
        {
            const size_t chunk = std::min<size_t>(opt.bigChunkSize, size - pos);
            Sector sector = PackSector(data + pos, chunk, pos + chunk == size, opt, inflated);
            pos += sector.input;
            table.push_back(pos);
            lastUsed = sector.used;
            sectors.push_back(std::move(sector.bytes));
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

        const uint64_t compDataSize = nbSectors ? uint64_t(nbSectors - 1) * opt.sectorSize + lastUsed : 0;

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

private:
    static constexpr long kSymbolsPerBlock = (1L << (8 + 6)) - 1;   // zlib's lit_bufsize - 1 at memLevel 8

    struct Sector
    {
        std::vector<uint8_t> bytes;
        size_t input = 0;
        size_t used = 0;
    };

    // The next chunk deflated from scratch and cut to one sector, which covers what the cut inflates to (4-aligned).
    static Sector PackSector(const uint8_t* data, size_t chunk, bool endsFile, const Options& opt,
                             std::vector<uint8_t>& inflated)
    {
        const size_t cap = size_t(opt.sectorSize) + 16;
        std::vector<uint8_t> stream = DeflateChunk(data, chunk, opt.level, cap, false);
        if (stream.size() < cap && EndsWithFullBlock(stream))
            stream = DeflateChunk(data, chunk, opt.level, cap, true);

        Sector sector;
        sector.used = std::min<size_t>(stream.size(), opt.sectorSize);
        if (stream.size() <= opt.sectorSize)
        {
            sector.input = chunk;
            stream.resize(opt.sectorSize, endsFile ? uint8_t(0) : kChunkPadding);
        }
        else
        {
            stream.resize(opt.sectorSize);
            sector.input = InflatableSize(stream, inflated) & ~size_t(3);
            if (!sector.input)
                throw std::runtime_error("BUCompress: a sector holds less than 4 bytes of input");
        }
        sector.bytes = std::move(stream);
        return sector;
    }

    static std::vector<uint8_t> DeflateChunk(const uint8_t* data, size_t size, int level, size_t cap, bool emptyBlock)
    {
        std::vector<uint8_t> out(cap);
        z_stream strm{};
        if (deflateInit(&strm, level) != Z_OK)
            throw std::runtime_error("BUCompress: deflateInit failed");
        strm.next_in = const_cast<Bytef*>(data);
        strm.avail_in = static_cast<uInt>(size);
        strm.next_out = out.data();
        strm.avail_out = static_cast<uInt>(out.size());
        int rc = deflate(&strm, emptyBlock ? Z_PARTIAL_FLUSH : Z_SYNC_FLUSH);
        if (emptyBlock && rc == Z_OK && strm.avail_out)
            rc = deflate(&strm, Z_SYNC_FLUSH);
        out.resize(out.size() - strm.avail_out);
        deflateEnd(&strm);
        if (rc != Z_OK && rc != Z_BUF_ERROR)
            throw std::runtime_error("BUCompress: deflate failed");
        return out;
    }

    static size_t InflatableSize(const std::vector<uint8_t>& stream, std::vector<uint8_t>& out)
    {
        z_stream strm{};
        if (inflateInit(&strm) != Z_OK)
            throw std::runtime_error("BUCompress: inflateInit failed");
        strm.next_in = const_cast<Bytef*>(stream.data());
        strm.avail_in = static_cast<uInt>(stream.size());
        strm.next_out = out.data();
        strm.avail_out = static_cast<uInt>(out.size());
        int rc = Z_OK;
        while (rc == Z_OK && strm.avail_in && strm.avail_out)
            rc = inflate(&strm, Z_NO_FLUSH);
        const size_t produced = strm.total_out;
        inflateEnd(&strm);
        return produced;
    }

    // zlib 1.2.3 (retail) follows a full block it sync-flushes with an empty fixed block; newer zlib does not.
    static bool EndsWithFullBlock(const std::vector<uint8_t>& stream)
    {
        static const uint8_t kLengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
        static const uint8_t kDistanceExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
        static const uint8_t kCodeLengthOrder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
        BitReader br{ stream.data(), stream.size() };
        try
        {
            br.Bits(16);
            long lastSymbols = -1;
            for (;;)
            {
                const uint32_t last = br.Bits(1);
                const uint32_t type = br.Bits(2);
                long symbols = -1;
                if (type == 0)
                {
                    br.pos = (br.pos + 7) & ~size_t(7);
                    const uint32_t len = br.Bits(16);
                    br.Bits(16);
                    if (len == 0)
                        return lastSymbols == kSymbolsPerBlock;
                    br.pos += size_t(len) * 8;
                }
                else if (type == 1 || type == 2)
                {
                    uint8_t lengths[320] = {};
                    int nlen = 288, ndist = 30;
                    if (type == 1)
                    {
                        std::fill(lengths, lengths + 144, uint8_t(8));
                        std::fill(lengths + 144, lengths + 256, uint8_t(9));
                        std::fill(lengths + 256, lengths + 280, uint8_t(7));
                        std::fill(lengths + 280, lengths + 288, uint8_t(8));
                        std::fill(lengths + 288, lengths + 318, uint8_t(5));
                    }
                    else
                    {
                        nlen = static_cast<int>(br.Bits(5)) + 257;
                        ndist = static_cast<int>(br.Bits(5)) + 1;
                        const int ncode = static_cast<int>(br.Bits(4)) + 4;
                        uint8_t codeLengths[19] = {};
                        for (int i = 0; i < ncode; ++i)
                            codeLengths[kCodeLengthOrder[i]] = static_cast<uint8_t>(br.Bits(3));
                        const Huffman lencode(codeLengths, 19);
                        for (int i = 0; i < nlen + ndist;)
                        {
                            const int s = lencode.Decode(br);
                            int repeat = 1;
                            uint8_t value = static_cast<uint8_t>(s);
                            if (s == 16)
                            {
                                if (i == 0)
                                    return false;
                                value = lengths[i - 1];
                                repeat = 3 + static_cast<int>(br.Bits(2));
                            }
                            else if (s == 17 || s == 18)
                            {
                                value = 0;
                                repeat = s == 17 ? 3 + static_cast<int>(br.Bits(3)) : 11 + static_cast<int>(br.Bits(7));
                            }
                            if (i + repeat > nlen + ndist)
                                return false;
                            while (repeat--)
                                lengths[i++] = value;
                        }
                    }
                    const Huffman lit(lengths, nlen), dist(lengths + nlen, ndist);
                    for (symbols = 0;; ++symbols)
                    {
                        const int s = lit.Decode(br);
                        if (s == 256)
                            break;
                        if (s > 256)
                        {
                            if (s > 285)
                                return false;
                            br.Bits(kLengthExtra[s - 257]);
                            const int d = dist.Decode(br);
                            if (d > 29)
                                return false;
                            br.Bits(kDistanceExtra[d]);
                        }
                    }
                }
                else
                    return false;
                if (last)
                    return false;
                lastSymbols = symbols;
            }
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    struct BitReader
    {
        const uint8_t* data;
        size_t size;
        size_t pos = 0;

        uint32_t Bits(int n)
        {
            if (pos + size_t(n) > size * 8)
                throw std::out_of_range("BitReader: past the end");
            uint32_t v = 0;
            for (int i = 0; i < n; ++i, ++pos)
                v |= uint32_t((data[pos >> 3] >> (pos & 7)) & 1) << i;
            return v;
        }
    };

    struct Huffman
    {
        uint16_t count[16] = {};
        std::vector<uint16_t> symbol;

        Huffman(const uint8_t* lengths, int n) : symbol(size_t(n))
        {
            for (int s = 0; s < n; ++s)
                ++count[lengths[s]];
            uint16_t offs[16] = {};
            for (int len = 1; len < 15; ++len)
                offs[len + 1] = static_cast<uint16_t>(offs[len] + count[len]);
            for (int s = 0; s < n; ++s)
                if (lengths[s])
                    symbol[offs[lengths[s]]++] = static_cast<uint16_t>(s);
        }

        int Decode(BitReader& br) const
        {
            int code = 0, first = 0, index = 0;
            for (int len = 1; len < 16; ++len)
            {
                code |= static_cast<int>(br.Bits(1));
                if (code - count[len] < first)
                    return symbol[size_t(index + (code - first))];
                index += count[len];
                first = (first + count[len]) << 1;
                code <<= 1;
            }
            throw std::runtime_error("BUCompress: bad Huffman code");
        }
    };
};
