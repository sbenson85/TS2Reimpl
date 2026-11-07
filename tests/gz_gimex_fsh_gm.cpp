// Differential tests for the FSH back end's GM image decoder (lane MR; src/gz/GimexFshGm.size.cpp, GimexFshGmBits.cpp). Each
// function runs alone, the original (by RVA, in the mapped image) against ours, on seeded synthetic input:
// - the bit reader over every count and bit position of random data;
// - the DC readers over every 5-bit prefix and every 10-bit code, with random follow-on bits;
// - the intra and inter block decoders over streams built from the code tables (every class of the next 16 bits, escapes of every
//   kind, runs past the end of the block) and over biased random bits, with random predictors, quantisers and pre-filled blocks;
// - the macroblock address reader over every 11-bit pattern at every bit offset, and chains of stuffing / escape codes;
// - the inverse transform (row, column, whole block) over random blocks of several magnitudes and sparse patterns;
// - the clipping tables, the colour conversion, the macroblock reader, the stream driver (both macroblock orders, intra and predicted
//   images of several sizes, the table flag) and the header functions.
// Compared: return values, the bit position, the DC predictors, the quantiser, all six blocks, every byte written (with margins).
//
// Kept out, and why. The original's two clipping tables are indexed without a range check: the column pass (0x895f18, the load at
// 0x8960b1 and its seven neighbours, and the shortcut at 0x895f71) with (x >> 14) into a table of -512..511, and the colour
// conversion (0x8963bd, the loads at 0x896484 and its eleven neighbours) with a value into a table of 0x3ff bytes: beyond them the
// original reads the globals next to the tables (the stream position, the other table). Ours clamps the index and counts it
// (GmState::clipOverruns); a case where ours counted is not compared on the data that went through the table (the block, or the
// pixels), only on the stream state. 0x8962e9's return value for the last macroblock is whatever the last column transform left in eax;
// its only caller (0x89662e) discards it, so it is not compared there. A stream is read four bytes at a time from the current byte, so
// every buffer here has slack after it (filled with random bytes the same for both sides).
#define NOMINMAX
#include "difftest.h"
#include "game/game.h"
#include "gz/GimexFshGm.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

using gimex::fsh::GmState;
using gimex::fsh::gGmState;

constexpr uint32_t kPeekBits = 0x895ee0;
constexpr uint32_t kGetBits = 0x895cdb;
constexpr uint32_t kReadChromaDc = 0x895d1f;
constexpr uint32_t kReadLumaDc = 0x895d8f;
constexpr uint32_t kDecodeIntraBlock = 0x89583b;
constexpr uint32_t kDecodeInterBlock = 0x895a44;
constexpr uint32_t kReadAddressIncrement = 0x895dff;
constexpr uint32_t kInitClipTables = 0x895e7c;
constexpr uint32_t kIdctRow = 0x896137;
constexpr uint32_t kIdctColumn = 0x895f18;
constexpr uint32_t kIdct8x8 = 0x895c47;
constexpr uint32_t kDecodeMacroblock = 0x8962e9;
constexpr uint32_t kConvertBlock = 0x8963bd;
constexpr uint32_t kDecodeGm = 0x89662e;
constexpr uint32_t kParseGmHeader = 0x8968dd;
constexpr uint32_t kIsGm = 0x89694e;

// the original's globals
constexpr uint32_t kStreamRva = 0x10e6c6c;
constexpr uint32_t kBitPositionRva = 0x10e7070;
constexpr uint32_t kPredictorsRva = 0x10e6c60;
constexpr uint32_t kBlocksRva = 0x10e6960;
constexpr uint32_t kQuantiserRva = 0x105dc3c;
constexpr uint32_t kReadyRva = 0x10e7878;
constexpr uint32_t kByteClipRva = 0x10e6c70;
constexpr uint32_t kWordClipRva = 0x10e7078;

// the original's tables (read here to build streams)
constexpr uint32_t kLumaDcShort = 0x105ddb0;
constexpr uint32_t kLumaDcLong = 0x105da10;
constexpr uint32_t kChromaDcShort = 0x105de10;
constexpr uint32_t kChromaDcLong = 0x105d690;

constexpr size_t kSlack = 16;

using Rng = std::mt19937;

struct Snapshot {
    const uint8_t *data = nullptr;
    int32_t bitPosition = 0;
    int32_t predictors[3] = {0, 0, 0};
    int16_t blocks[6][64] = {};
    int32_t quantiser = 6;
};

const uint8_t *&OriginalStream() { return *GameAddress<const uint8_t *>(kStreamRva); }
int32_t &OriginalBitPosition() { return *GameAddress<int32_t>(kBitPositionRva); }
int32_t *OriginalPredictors() { return GameAddress<int32_t>(kPredictorsRva); }
int16_t *OriginalBlocks() { return GameAddress<int16_t>(kBlocksRva); }
int32_t &OriginalQuantiser() { return *GameAddress<int32_t>(kQuantiserRva); }
int32_t &OriginalReady() { return *GameAddress<int32_t>(kReadyRva); }

void Apply(const Snapshot &s)
{
    OriginalStream() = s.data;
    OriginalBitPosition() = s.bitPosition;
    std::memcpy(OriginalPredictors(), s.predictors, sizeof(s.predictors));
    std::memcpy(OriginalBlocks(), s.blocks, sizeof(s.blocks));
    OriginalQuantiser() = s.quantiser;
    gGmState.data = s.data;
    gGmState.bitPosition = s.bitPosition;
    std::memcpy(gGmState.dcPredictor, s.predictors, sizeof(s.predictors));
    std::memcpy(gGmState.blocks, s.blocks, sizeof(s.blocks));
    gGmState.quantiser = s.quantiser;
}

Snapshot ReadOriginal()
{
    Snapshot s;
    s.data = OriginalStream();
    s.bitPosition = OriginalBitPosition();
    std::memcpy(s.predictors, OriginalPredictors(), sizeof(s.predictors));
    std::memcpy(s.blocks, OriginalBlocks(), sizeof(s.blocks));
    s.quantiser = OriginalQuantiser();
    return s;
}

Snapshot ReadOurs()
{
    Snapshot s;
    s.data = gGmState.data;
    s.bitPosition = gGmState.bitPosition;
    std::memcpy(s.predictors, gGmState.dcPredictor, sizeof(s.predictors));
    std::memcpy(s.blocks, gGmState.blocks, sizeof(s.blocks));
    s.quantiser = gGmState.quantiser;
    return s;
}

// Compares the two sides' state; `blocks`: also the six blocks. Returns false (and fails) on a difference.
bool CompareState(difftest::Context &ctx, const char *what, bool blocks)
{
    const Snapshot a = ReadOriginal();
    const Snapshot b = ReadOurs();
    bool same = true;
    if (a.data != b.data) {
        ctx.Fail("%s: stream pointer differs", what);
        same = false;
    }
    if (a.bitPosition != b.bitPosition) {
        ctx.Fail("%s: bit position original %d, ours %d", what, a.bitPosition, b.bitPosition);
        same = false;
    }
    if (std::memcmp(a.predictors, b.predictors, sizeof(a.predictors)) != 0) {
        ctx.Fail("%s: predictors original %d %d %d, ours %d %d %d", what, a.predictors[0], a.predictors[1], a.predictors[2],
                 b.predictors[0], b.predictors[1], b.predictors[2]);
        same = false;
    }
    if (a.quantiser != b.quantiser) {
        ctx.Fail("%s: quantiser original %d, ours %d", what, a.quantiser, b.quantiser);
        same = false;
    }
    if (blocks && std::memcmp(a.blocks, b.blocks, sizeof(a.blocks)) != 0) {
        size_t first = 0;
        const int16_t *x = &a.blocks[0][0];
        const int16_t *y = &b.blocks[0][0];
        while (x[first] == y[first]) ++first;
        ctx.Fail("%s: blocks differ (block %zu entry %zu: original %d, ours %d)", what, first / 64, first % 64, x[first], y[first]);
        same = false;
    }
    return same;
}

// The two clipping tables on both sides, built by each side's own initialiser (the flag set).
void InitTables(difftest::Context &ctx)
{
    ctx.Original<void(__cdecl *)()>(kInitClipTables)();
    gimex::fsh::InitClipTables();
    OriginalReady() = 1;
    gGmState.tablesReady = 1;
}

// ---- inputs ----

struct BitWriter {
    std::vector<uint8_t> bytes;
    size_t bits = 0;
    void Put(uint32_t value, int count)
    {
        for (int i = count - 1; i >= 0; --i) {
            if (bits % 8 == 0) bytes.push_back(0);
            if ((value >> i) & 1) bytes.back() = static_cast<uint8_t>(bytes.back() | (0x80 >> (bits % 8)));
            ++bits;
        }
    }
    // the bytes with slack after them (random, so a read past the end is the same on both sides)
    std::vector<uint8_t> Finish(Rng &rng, size_t extra = 0)
    {
        std::vector<uint8_t> out = bytes;
        for (size_t i = 0; i < kSlack + extra; ++i) out.push_back(static_cast<uint8_t>(rng()));
        return out;
    }
};

std::vector<uint8_t> RandomStream(Rng &rng, size_t length, int mode)
{
    std::vector<uint8_t> out(length + kSlack);
    for (auto &byte : out) {
        uint32_t value = rng();
        switch (mode) {
        case 1: value &= rng(); break;
        case 2: value &= rng() & rng(); break;
        case 3: value |= rng(); break;
        case 4: value = (value & 0x0f) | 0x40; break;
        default: break;
        }
        byte = static_cast<uint8_t>(value);
    }
    return out;
}

const int8_t *Bytes(uint32_t rva) { return GameAddress<int8_t>(rva); }

// The code table entry for a 16-bit pattern, written from the table of classes (independent of src/gz/GimexFshGm.size.cpp's chain).
struct CodeClass {
    uint32_t low;
    uint32_t high;
    int shift;
    int base;
    uint32_t table;
    uint32_t flaggedTable;
};
const CodeClass kClasses[] = {
    {0x4000, 0x10000, 12, 4, 0x105d74c, 0x105d828},   // the flagged table also takes >> 8 here: see Entry
    {0x400, 0x4000, 8, 4, 0x105d770, 0x105d828},
    {0x200, 0x400, 6, 8, 0x105db1c, 0x105db34},
    {0x100, 0x200, 4, 0x10, 0x105db4c, 0x105db4c},
    {0x80, 0x100, 3, 0x10, 0x105db7c, 0x105db7c},
    {0x40, 0x80, 2, 0x10, 0x105dbac, 0x105dbac},
    {0x20, 0x40, 1, 0x10, 0x105dbdc, 0x105dbdc},
    {0x10, 0x20, 0, 0x10, 0x105dc0c, 0x105dc0c},
};

const int8_t *Entry(uint32_t bits, bool flagged)
{
    for (const CodeClass &c : kClasses) {
        if (bits < c.low || bits >= c.high) continue;
        if (flagged && c.low == 0x4000) return Bytes(c.flaggedTable) + (static_cast<int>(bits >> 8) - 4) * 3;
        return Bytes(flagged ? c.flaggedTable : c.table) + (static_cast<int>(bits >> c.shift) - c.base) * 3;
    }
    return nullptr;
}

// A DC difference: a code from the short or long table, then the bits. `maxSize` bounds the difference's size.
void PutDc(BitWriter &w, Rng &rng, bool luma, int maxSize)
{
    const uint32_t shortTable = luma ? kLumaDcShort : kChromaDcShort;
    const uint32_t longTable = luma ? kLumaDcLong : kChromaDcLong;
    const int longBits = luma ? 9 : 10;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (rng() % 3 != 0) {
            const uint32_t index = rng() % 31;
            const int8_t *entry = Bytes(shortTable) + index * 2;
            if (entry[0] > maxSize) continue;
            w.Put(index >> (5 - entry[1]), entry[1]);
            if (entry[0] > 0) w.Put(rng(), entry[0]);
            return;
        }
        const uint32_t index = (0x1fu << (longBits - 5)) | (rng() & ((1u << (longBits - 5)) - 1));
        const int8_t *entry = Bytes(longTable) + index * 2;
        if (entry[0] > maxSize) continue;
        w.Put(index >> (longBits - entry[1]), entry[1]);
        if (entry[0] > 0) w.Put(rng(), entry[0]);
        return;
    }
    w.Put(0, 2); // size 0 in the short table
}

// Coefficient codes: `pairs` random ones, then the end-of-block code (or not, when `ending` is false).
void PutCoefficients(BitWriter &w, Rng &rng, bool inter, bool flagged, int pairs, bool ending)
{
    for (int i = 0; i < pairs; ++i) {
        const uint32_t cls = rng() % 10 < 4 ? rng() % 3 : rng() % 8;
        const CodeClass &c = kClasses[cls];
        uint32_t bits = c.low + rng() % (c.high - c.low);
        const int8_t *entry = Entry(bits, inter && flagged);
        if (entry[0] == 0x40) {
            bits = 0x4000 + rng() % 0x4000; // not the end code
            entry = Entry(bits, inter && flagged);
            if (entry[0] == 0x40) continue;
        }
        w.Put(bits >> (16 - entry[2]), entry[2]);
        if (entry[0] == 0x41) {
            w.Put(rng() % 4 == 0 ? rng() % 64 : rng() % 8, 6);
            if (inter) {
                w.Put(rng() % 3 == 0 ? rng() : (rng() % 2 ? 0x800 : 0) | (rng() % 40), 12);
            } else {
                switch (rng() % 5) {
                case 0: w.Put(0, 8); w.Put(rng(), 8); break;
                case 1: w.Put(0x80, 8); w.Put(rng(), 8); break;
                case 2: w.Put(0x81 + rng() % 127, 8); break;
                default: w.Put(1 + rng() % 127, 8); break;
                }
            }
        } else {
            w.Put(rng(), 1);
        }
    }
    if (ending) w.Put(2, 2); // '10'
}

// One block's data: the DC and the coefficients.
void PutBlock(BitWriter &w, Rng &rng, int block, bool inter, bool flagged, int maxDc, int pairs)
{
    PutDc(w, rng, block < 4, maxDc);
    PutCoefficients(w, rng, inter, flagged, pairs, true);
}

Snapshot RandomState(Rng &rng, const std::vector<uint8_t> &stream, int32_t bitPosition)
{
    Snapshot s;
    s.data = stream.data();
    s.bitPosition = bitPosition;
    for (int32_t &p : s.predictors) p = static_cast<int32_t>(rng() % 2001) - 1000;
    for (auto &block : s.blocks)
        for (int16_t &v : block) v = static_cast<int16_t>(rng());
    s.quantiser = 1 + static_cast<int32_t>(rng() % 31);
    return s;
}

} // namespace

// 0x895ee0: the next count bits, position unchanged.
DIFF_TEST(GimexFshGm_PeekBits, 0x895ee0)
{
    using Fn = uint32_t(__cdecl *)(int32_t);
    const Fn original = ctx.Original<Fn>(kPeekBits);
    Rng rng(0x895ee0);
    for (int stream = 0; stream < 30; ++stream) {
        const std::vector<uint8_t> data = RandomStream(rng, 64, stream % 5);
        for (int32_t position = 0; position < 400; ++position) {
            for (int32_t count = 1; count <= 16; ++count) {
                Snapshot s;
                s.data = data.data();
                s.bitPosition = position;
                Apply(s);
                ++ctx.cases;
                const uint32_t a = original(count);
                const uint32_t b = gimex::fsh::PeekBits(count);
                if (a != b || OriginalBitPosition() != gGmState.bitPosition || gGmState.bitPosition != position)
                    ctx.Fail("peek %d at %d: original %x, ours %x", count, position, a, b);
            }
        }
    }
}

// 0x895cdb: the next count bits, position moved on.
DIFF_TEST(GimexFshGm_GetBits, 0x895cdb)
{
    using Fn = uint32_t(__cdecl *)(int32_t);
    const Fn original = ctx.Original<Fn>(kGetBits);
    Rng rng(0x895cdb);
    for (int stream = 0; stream < 30; ++stream) {
        const std::vector<uint8_t> data = RandomStream(rng, 64, stream % 5);
        for (int32_t position = 0; position < 400; ++position) {
            for (int32_t count = 1; count <= 16; ++count) {
                Snapshot s;
                s.data = data.data();
                s.bitPosition = position;
                Apply(s);
                ++ctx.cases;
                const uint32_t a = original(count);
                const uint32_t b = gimex::fsh::GetBits(count);
                if (a != b || OriginalBitPosition() != gGmState.bitPosition) ctx.Fail("get %d at %d: original %x, ours %x", count, position, a, b);
            }
        }
    }
}

namespace {

void DcCases(difftest::Context &ctx, uint32_t rva, int32_t(__cdecl *ours)(), int longBits)
{
    using Fn = int32_t(__cdecl *)();
    const Fn original = ctx.Original<Fn>(rva);
    Rng rng(rva);
    // every code: the 10 bits (9 for luma) at the start, 0..7 bits in, with random bits after
    for (uint32_t code = 0; code < (1u << longBits); ++code) {
        for (int offset = 0; offset < 8; offset += 1 + static_cast<int>(code % 3)) {
            BitWriter w;
            w.Put(rng(), offset);
            w.Put(code, longBits);
            w.Put(rng(), 24);
            const std::vector<uint8_t> data = w.Finish(rng);
            Snapshot s;
            s.data = data.data();
            s.bitPosition = offset;
            Apply(s);
            ++ctx.cases;
            const int32_t a = original();
            const int32_t b = ours();
            if (a != b || OriginalBitPosition() != gGmState.bitPosition) {
                ctx.Fail("dc code %x at %d: original %d (%d), ours %d (%d)", code, offset, a, OriginalBitPosition(), b, gGmState.bitPosition);
            }
        }
    }
    for (int i = 0; i < 20000; ++i) {
        const std::vector<uint8_t> data = RandomStream(rng, 16, i % 5);
        Snapshot s;
        s.data = data.data();
        s.bitPosition = static_cast<int32_t>(rng() % 100);
        Apply(s);
        ++ctx.cases;
        const int32_t a = original();
        const int32_t b = ours();
        if (a != b || OriginalBitPosition() != gGmState.bitPosition) ctx.Fail("dc random %d: original %d, ours %d", i, a, b);
    }
}

} // namespace

// 0x895d8f / 0x895d1f: the luma and chroma DC differences.
DIFF_TEST(GimexFshGm_ReadLumaDc, 0x895d8f)
{
    DcCases(ctx, kReadLumaDc, &gimex::fsh::ReadLumaDc, 9);
}

DIFF_TEST(GimexFshGm_ReadChromaDc, 0x895d1f)
{
    DcCases(ctx, kReadChromaDc, &gimex::fsh::ReadChromaDc, 10);
}

namespace {

void BlockCases(difftest::Context &ctx, uint32_t rva, bool inter)
{
    using IntraFn = void(__cdecl *)(int32_t);
    using InterFn = void(__cdecl *)(int32_t, int32_t);
    Rng rng(rva);
    for (int i = 0; i < 12000; ++i) {
        const int block = static_cast<int>(rng() % 6);
        const int flag = inter ? static_cast<int>(rng() % 3) : 0; // 0, 1, 2 (any nonzero is the table flag)
        std::vector<uint8_t> stream;
        if (i % 4 == 0) {
            stream = RandomStream(rng, 40 + rng() % 200, static_cast<int>(rng() % 5));
        } else {
            BitWriter w;
            PutBlock(w, rng, block, inter, flag != 0, i % 7 == 0 ? 11 : 8, static_cast<int>(rng() % (i % 5 == 0 ? 90 : 14)));
            if (i % 9 == 0) PutBlock(w, rng, block, inter, flag != 0, 8, 5);
            stream = w.Finish(rng);
        }
        const int32_t start = i % 3 == 0 ? static_cast<int32_t>(rng() % 8) : 0;
        Snapshot s = RandomState(rng, stream, start);
        if (i % 6 == 0) s.quantiser = 1 + static_cast<int32_t>(rng() % 4);
        if (i % 11 == 0) s.quantiser = 31;
        Apply(s);
        ++ctx.cases;
        if (inter) {
            ctx.Original<InterFn>(rva)(block, flag);
            gimex::fsh::DecodeInterBlock(block, flag);
        } else {
            ctx.Original<IntraFn>(rva)(block);
            gimex::fsh::DecodeIntraBlock(block);
        }
        if (!CompareState(ctx, inter ? "inter block" : "intra block", true)) {
            ctx.Fail("  case %d: block %d flag %d start %d quantiser %d", i, block, flag, start, s.quantiser);
            return;
        }
    }
}

} // namespace

// 0x89583b: an intra block; 0x895a44: a predicted block.
DIFF_TEST(GimexFshGm_DecodeIntraBlock, 0x89583b)
{
    BlockCases(ctx, kDecodeIntraBlock, false);
}

DIFF_TEST(GimexFshGm_DecodeInterBlock, 0x895a44)
{
    BlockCases(ctx, kDecodeInterBlock, true);
}

// 0x895dff: the macroblock address increment.
DIFF_TEST(GimexFshGm_ReadAddressIncrement, 0x895dff)
{
    using Fn = int32_t(__cdecl *)();
    const Fn original = ctx.Original<Fn>(kReadAddressIncrement);
    Rng rng(0x895dff);
    auto run = [&](const std::vector<uint8_t> &data, int32_t position) {
        Snapshot s;
        s.data = data.data();
        s.bitPosition = position;
        Apply(s);
        ++ctx.cases;
        const int32_t a = original();
        const int32_t b = gimex::fsh::ReadAddressIncrement();
        if (a != b || OriginalBitPosition() != gGmState.bitPosition)
            ctx.Fail("address at %d: original %d (%d), ours %d (%d)", position, a, OriginalBitPosition(), b, gGmState.bitPosition);
    };
    // every 11-bit pattern at every offset, with random bits after
    for (uint32_t code = 0; code < 2048; ++code) {
        for (int offset = 0; offset < 8; ++offset) {
            BitWriter w;
            w.Put(0, offset);
            w.Put(code, 11);
            run(w.Finish(rng), offset);
        }
    }
    // chains of stuffing (0xf) and escape (8) patterns
    for (int i = 0; i < 6000; ++i) {
        BitWriter w;
        const int offset = static_cast<int>(rng() % 8);
        w.Put(0, offset);
        const int chain = 1 + static_cast<int>(rng() % 6);
        for (int k = 0; k < chain; ++k) w.Put(rng() % 2 ? 0xf : 8, 11);
        w.Put(rng(), 11);
        run(w.Finish(rng), offset);
    }
}

// 0x895e7c: the clipping tables.
DIFF_TEST(GimexFshGm_InitClipTables, 0x895e7c)
{
    uint8_t *originalBytes = GameAddress<uint8_t>(kByteClipRva);
    int16_t *originalWords = GameAddress<int16_t>(kWordClipRva);
    for (int pattern = 0; pattern < 3; ++pattern) {
        std::memset(originalBytes, 0xa5 + pattern, 0x400);
        std::memset(gGmState.byteClip, 0xa5 + pattern, sizeof(gGmState.byteClip));
        for (int i = 0; i < 1024; ++i) {
            originalWords[i] = static_cast<int16_t>(0x5a5a + pattern);
            gGmState.wordClip[i] = static_cast<int16_t>(0x5a5a + pattern);
        }
        ++ctx.cases;
        ctx.Original<void(__cdecl *)()>(kInitClipTables)();
        gimex::fsh::InitClipTables();
        if (std::memcmp(originalBytes, gGmState.byteClip, sizeof(gGmState.byteClip)) != 0) ctx.Fail("byte table differs (pattern %d)", pattern);
        if (originalBytes[0x3ff] != static_cast<uint8_t>(0xa5 + pattern)) ctx.Fail("the original wrote past its byte table (pattern %d)", pattern);
        if (std::memcmp(originalWords, gGmState.wordClip, sizeof(gGmState.wordClip)) != 0) ctx.Fail("word table differs (pattern %d)", pattern);
    }
}

namespace {

// Random coefficient blocks: dense, sparse (rows / columns / one coefficient), small, large.
void FillBlock(Rng &rng, int16_t *block, int kind)
{
    std::memset(block, 0, 64 * sizeof(int16_t));
    switch (kind % 8) {
    case 0:
        for (int i = 0; i < 64; ++i) block[i] = static_cast<int16_t>(static_cast<int>(rng() % 201) - 100);
        break;
    case 1:
        block[0] = static_cast<int16_t>(static_cast<int>(rng() % 4001) - 2000);
        break;
    case 2:
        for (int i = 0; i < 1 + static_cast<int>(rng() % 6); ++i) block[rng() % 64] = static_cast<int16_t>(static_cast<int>(rng() % 801) - 400);
        break;
    case 3:
        for (int i = 0; i < 64; ++i) block[i] = static_cast<int16_t>(rng());
        break;
    case 4:
        for (int i = 0; i < 8; ++i) block[(rng() % 8) * 8 + i] = static_cast<int16_t>(static_cast<int>(rng() % 601) - 300);
        break;
    case 5:
        for (int i = 0; i < 8; ++i) block[i * 8 + rng() % 8] = static_cast<int16_t>(static_cast<int>(rng() % 601) - 300);
        break;
    case 6:
        for (int i = 0; i < 64; ++i) block[i] = static_cast<int16_t>(static_cast<int>(rng() % 17) - 8);
        break;
    default:
        for (int i = 0; i < 64; ++i) block[i] = rng() % 3 == 0 ? static_cast<int16_t>(static_cast<int>(rng() % 2001) - 1000) : 0;
        break;
    }
}

} // namespace

// 0x896137: one row of the inverse transform.
DIFF_TEST(GimexFshGm_IdctRow, 0x896137)
{
    using Fn = void(__cdecl *)(int16_t *);
    const Fn original = ctx.Original<Fn>(kIdctRow);
    Rng rng(0x896137);
    for (int i = 0; i < 60000; ++i) {
        int16_t a[64] = {};
        int16_t row[8];
        FillBlock(rng, a, i);
        std::memcpy(row, a, sizeof(row));
        if (i % 5 == 0) {
            for (int k = 1; k < 8; ++k) row[k] = 0; // the DC-only shortcut
            if (i % 10 == 0) row[0] = static_cast<int16_t>(rng());
        }
        int16_t x[12], y[12];
        for (int k = 0; k < 12; ++k) x[k] = y[k] = static_cast<int16_t>(0x3c3c + k);
        std::memcpy(x + 2, row, sizeof(row));
        std::memcpy(y + 2, row, sizeof(row));
        ++ctx.cases;
        original(x + 2);
        gimex::fsh::IdctRow(y + 2);
        if (std::memcmp(x, y, sizeof(x)) != 0) {
            ctx.Fail("row %d differs: in %d %d %d %d %d %d %d %d", i, row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7]);
            return;
        }
    }
}

namespace {

void TransformCases(difftest::Context &ctx, uint32_t rva, void(__cdecl *ours)(int16_t *), bool column)
{
    using Fn = void(__cdecl *)(int16_t *);
    const Fn original = ctx.Original<Fn>(rva);
    InitTables(ctx);
    Rng rng(rva);
    int skipped = 0;
    for (int i = 0; i < 60000; ++i) {
        int16_t x[80], y[80];
        for (int k = 0; k < 80; ++k) x[k] = y[k] = static_cast<int16_t>(0x3c3c + k);
        int16_t block[64];
        FillBlock(rng, block, i);
        if (i % 7 == 0 && column) { // columns that are zero below the first row: the shortcut
            for (int r = 1; r < 8; ++r) block[r * 8] = 0;
        }
        if (i % 13 == 0) {
            for (int k = 1; k < 64; ++k) block[k] = 0;
        }
        std::memcpy(x + 8, block, sizeof(block));
        std::memcpy(y + 8, block, sizeof(block));
        gGmState.clipOverruns = 0;
        ++ctx.cases;
        original(x + 8 + (column ? i % 8 : 0));
        ours(y + 8 + (column ? i % 8 : 0));
        if (gGmState.clipOverruns != 0) {
            ++skipped;
            continue; // the original read outside its clipping table here
        }
        if (std::memcmp(x, y, sizeof(x)) != 0) {
            ctx.Fail("transform %d differs", i);
            return;
        }
    }
    if (skipped > 30000) ctx.Fail("too many cases skipped (%d)", skipped);
}

} // namespace

// 0x895f18: one column.
DIFF_TEST(GimexFshGm_IdctColumn, 0x895f18)
{
    TransformCases(ctx, kIdctColumn, &gimex::fsh::IdctColumn, true);
}

// 0x895c47: the whole 8 x 8 transform.
DIFF_TEST(GimexFshGm_Idct8x8, 0x895c47)
{
    TransformCases(ctx, kIdct8x8, &gimex::fsh::Idct8x8, false);
}

namespace {

// A macroblock's stream: the quantiser prefix, six blocks, then address bits.
std::vector<uint8_t> MacroblockStream(Rng &rng, bool inter, bool flagged, int maxDc, int pairs, int style)
{
    BitWriter w;
    switch (style % 4) {
    case 0: w.Put(1, 1); break;                                                  // no new quantiser
    case 1: w.Put(1, 2); w.Put(1 + rng() % 4, 5); break;                         // '01' and a quantiser
    case 2: w.Put(0, 2); break;                                                  // '00'
    default: w.Put(rng(), 1 + rng() % 2); w.Put(rng(), 5); break;
    }
    for (int block = 0; block < 6; ++block) PutBlock(w, rng, block, inter, flagged, maxDc, static_cast<int>(rng() % (pairs + 1)));
    for (int k = 0; k < 4; ++k) w.Put(rng(), 11);
    return w.Finish(rng);
}

} // namespace

// 0x8962e9: one macroblock.
DIFF_TEST(GimexFshGm_DecodeMacroblock, 0x8962e9)
{
    using Fn = int32_t(__cdecl *)(int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kDecodeMacroblock);
    InitTables(ctx);
    Rng rng(0x8962e9);
    int blockComparisons = 0;
    for (int i = 0; i < 6000; ++i) {
        const bool inter = i % 3 == 1;
        const int flag = static_cast<int>(rng() % 3);
        const int32_t isLast = i % 4 == 0 ? 1 : 0;
        const std::vector<uint8_t> stream = MacroblockStream(rng, inter, flag != 0, i % 5 == 0 ? 8 : 5, i % 2 ? 12 : 4, i);
        Snapshot s = RandomState(rng, stream, static_cast<int32_t>(rng() % 8));
        s.quantiser = 1 + static_cast<int32_t>(rng() % (i % 3 == 0 ? 31 : 3));
        for (int32_t &p : s.predictors) p = static_cast<int32_t>(rng() % 101) - 50;
        Apply(s);
        gGmState.clipOverruns = 0;
        ++ctx.cases;
        const int32_t a = original(isLast, inter, flag);
        const int32_t b = gimex::fsh::DecodeMacroblock(isLast, inter, flag);
        const bool blocks = gGmState.clipOverruns == 0;
        if (blocks) ++blockComparisons;
        if (isLast == 0 && a != b) {
            ctx.Fail("macroblock %d: returned original %d, ours %d", i, a, b);
            return;
        }
        if (!CompareState(ctx, "macroblock", blocks)) {
            ctx.Fail("  case %d: inter %d flag %d last %d", i, inter, flag, isLast);
            return;
        }
    }
    if (blockComparisons < 3000) ctx.Fail("only %d cases compared the blocks", blockComparisons);
}

namespace {

// Blocks that keep the conversion inside its table: luma and chroma samples of moderate size.
void FillSamples(Rng &rng, Snapshot &s, int range)
{
    for (auto &block : s.blocks)
        for (int16_t &v : block) v = static_cast<int16_t>(static_cast<int>(rng() % (2 * range + 1)) - range);
}

} // namespace

// 0x8963bd: one 8 x 8 luma block with its chroma into B, G, R, A.
DIFF_TEST(GimexFshGm_ConvertBlock, 0x8963bd)
{
    using Fn = void(__cdecl *)(uint8_t *, int32_t, int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kConvertBlock);
    InitTables(ctx);
    Rng rng(0x8963bd);
    int compared = 0;
    for (int i = 0; i < 6000; ++i) {
        Snapshot s;
        s.data = nullptr;
        FillSamples(rng, s, i % 4 == 0 ? 400 : i % 4 == 1 ? 256 : i % 4 == 2 ? 150 : 100);
        Apply(s);
        const int32_t pixelStride = 16 + static_cast<int32_t>(rng() % 20);
        const int32_t stride = i % 5 == 0 ? pixelStride * 4 + static_cast<int32_t>(rng() % 4) : pixelStride * 4;
        const int block = static_cast<int>(rng() % 4);
        const int chromaRow = rng() % 2 ? 4 : 0;
        const int chromaColumn = rng() % 2 ? 4 : 0;
        std::vector<uint8_t> a(static_cast<size_t>(pixelStride) * 4 * 24 + 64);
        for (auto &byte : a) byte = static_cast<uint8_t>(rng());
        std::vector<uint8_t> b = a;
        const size_t offset = 16 + rng() % 8 * 4;
        gGmState.clipOverruns = 0;
        ++ctx.cases;
        original(a.data() + offset, stride, 0, block, chromaRow, chromaColumn);
        gimex::fsh::ConvertBlock(b.data() + offset, stride, 0, block, chromaRow, chromaColumn);
        if (gGmState.clipOverruns != 0) continue;
        ++compared;
        if (a != b) {
            size_t first = 0;
            while (a[first] == b[first]) ++first;
            ctx.Fail("convert %d (block %d, chroma %d,%d, stride %d): first difference at byte %zu: original %02x, ours %02x", i, block,
                     chromaRow, chromaColumn, stride, first, a[first], b[first]);
            return;
        }
    }
    if (compared < 2500) ctx.Fail("only %d conversions compared", compared);
}

namespace {

struct Image {
    std::vector<uint8_t> stream;
    int32_t width, height;
};

// A GM stream of the given size: the header and then, per macroblock, a structured macroblock.
std::vector<uint8_t> MakeImage(Rng &rng, int32_t width, int32_t height, bool inter, bool flagged, int style)
{
    BitWriter w;
    w.Put('G', 8);
    w.Put('M', 8);
    w.Put(static_cast<uint32_t>((width + 15) / 16), 8);
    w.Put(static_cast<uint32_t>((height + 15) / 16), 8);
    w.Put(rng(), 32);
    w.Put(rng(), 8);
    const int count = (width + 15) / 16 * ((height + 15) / 16);
    for (int mb = 0; mb < count; ++mb) {
        switch ((style + mb) % 4) {
        case 0: w.Put(1, 1); break;
        case 1: w.Put(1, 2); w.Put(1 + rng() % 3, 5); break;
        case 2: w.Put(0, 2); break;
        default: w.Put(1, 1); break;
        }
        for (int block = 0; block < 6; ++block) PutBlock(w, rng, block, inter, flagged, 5, static_cast<int>(rng() % 6));
        w.Put(rng() % 2 ? 1 : 0, 1);
        w.Put(rng(), 11);
    }
    return w.Finish(rng, 64);
}

} // namespace

// 0x89662e: the decoder.
DIFF_TEST(GimexFshGm_DecodeGm, 0x89662e)
{
    using Fn = int32_t(__cdecl *)(uint8_t *, int32_t, const uint8_t *, int32_t, int32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(kDecodeGm);
    Rng rng(0x89662e);
    int compared = 0;
    const int32_t sizes[][2] = {{16, 16}, {32, 16}, {16, 32}, {32, 32}, {17, 16}, {16, 20}, {33, 18}, {48, 32}, {1, 1}, {64, 16}, {0, 16}, {16, 0}};
    for (int i = 0; i < 1500; ++i) {
        const int32_t *size = sizes[i % (sizeof(sizes) / sizeof(sizes[0]))];
        const uint32_t flags = rng() % 8;
        const bool inter = (flags & 3) == 3; // only the row-order walk (bit 0) passes the other flags on
        const bool flagged = (flags & 5) == 5;
        const int32_t width = size[0];
        const int32_t height = size[1];
        const int32_t alignedWidth = (width + 15) & ~15;
        const int32_t alignedHeight = (height + 15) & ~15;
        const int32_t stride = (alignedWidth + static_cast<int32_t>(rng() % 3) * 4) * 4 + (i % 7 == 0 ? static_cast<int32_t>(rng() % 4) : 0);
        std::vector<uint8_t> stream = MakeImage(rng, width, height, inter, flagged, i);
        // The decoder reads the bit stream four bytes at a time and may read past the data (case 439 read the first byte of an unmapped page past an
        // exact-sized vector): zeroed slack after the data, the same for both sides, so both read defined bytes.
        stream.resize(stream.size() + 4096, 0);
        const size_t destBytes = static_cast<size_t>(stride) * (alignedHeight + 2) + 256;
        std::vector<uint8_t> a(destBytes), b;
        for (auto &byte : a) byte = static_cast<uint8_t>(rng());
        b = a;
        // both sides start from the same (random) state, the tables ready or not
        InitTables(ctx);
        const bool lazy = i % 5 == 0;
        if (lazy) {
            OriginalReady() = 0;
            gGmState.tablesReady = 0;
            std::memset(GameAddress<uint8_t>(kByteClipRva), 0x77, 0x3ff);
            std::memset(gGmState.byteClip, 0x77, sizeof(gGmState.byteClip));
        }
        Snapshot s = RandomState(rng, stream, 0);
        s.quantiser = 1 + static_cast<int32_t>(rng() % 3);
        s.bitPosition = static_cast<int32_t>(rng() % 50); // the decoder starts again at bit 0 whatever this was
        Apply(s);
        gGmState.clipOverruns = 0;
        ++ctx.cases;
        const int32_t ra = original(a.data() + 64, stride, stream.data(), width, height, flags);
        const int32_t rb = gimex::fsh::DecodeGm(b.data() + 64, stride, stream.data(), width, height, flags);
        if (ra != rb) {
            ctx.Fail("decode %d: returned original %d, ours %d", i, ra, rb);
            return;
        }
        if (!CompareState(ctx, "decode", false)) {
            ctx.Fail("  case %d: %dx%d flags %u", i, width, height, flags);
            return;
        }
        if (OriginalReady() != gGmState.tablesReady) ctx.Fail("decode %d: tables flag original %d, ours %d", i, OriginalReady(), gGmState.tablesReady);
        if (gGmState.clipOverruns != 0) continue; // the original read outside a clipping table
        ++compared;
        if (a != b) {
            size_t first = 0;
            while (a[first] == b[first]) ++first;
            ctx.Fail("decode %d (%dx%d flags %u stride %d): first difference at byte %zu: original %02x, ours %02x", i, width, height, flags,
                     stride, first, a[first], b[first]);
            return;
        }
    }
    if (compared < 400) ctx.Fail("only %d images compared", compared);
    // a null stream: the tables are built, nothing else
    {
        uint8_t a[16] = {1}, b[16] = {1};
        OriginalReady() = 0;
        gGmState.tablesReady = 0;
        ++ctx.cases;
        const int32_t ra = original(a, 64, nullptr, 16, 16, 0);
        const int32_t rb = gimex::fsh::DecodeGm(b, 64, nullptr, 16, 16, 0);
        if (ra != 0 || rb != 0 || OriginalReady() != 1 || gGmState.tablesReady != 1) ctx.Fail("null stream: %d / %d, flags %d / %d", ra, rb, OriginalReady(), gGmState.tablesReady);
    }
}

// 0x8968dd: the header reader.
DIFF_TEST(GimexFshGm_ParseGmHeader, 0x8968dd)
{
    using Fn = int32_t(__cdecl *)(const uint8_t *, int32_t *, int32_t *, int32_t *, int32_t *, int32_t *);
    const Fn original = ctx.Original<Fn>(kParseGmHeader);
    Rng rng(0x8968dd);
    for (int i = 0; i < 30000; ++i) {
        uint8_t header[12];
        for (auto &byte : header) byte = static_cast<uint8_t>(rng());
        if (i % 3 != 0) {
            header[0] = 'G';
            header[1] = 'M';
        }
        if (i % 7 == 0) header[7] = static_cast<uint8_t>(header[7] | 0x08);
        if (i % 11 == 0) header[6] &= 0xcf; // the 0x300000 bits clear
        int32_t x[5], y[5];
        for (int k = 0; k < 5; ++k) x[k] = y[k] = 0x5a5a0000 + k;
        ++ctx.cases;
        const int32_t a = original(header, &x[0], &x[1], &x[2], &x[3], &x[4]);
        const int32_t b = gimex::fsh::ParseGmHeader(header, &y[0], &y[1], &y[2], &y[3], &y[4]);
        if (a != b || std::memcmp(x, y, sizeof(x)) != 0) {
            ctx.Fail("header %02x%02x %02x %02x %02x%02x%02x%02x %02x: result %d / %d; outputs original %d %d %d %d %d, ours %d %d %d %d %d", header[0],
                     header[1], header[2], header[3], header[7], header[6], header[5], header[4], header[8], a, b, x[0], x[1], x[2], x[3], x[4],
                     y[0], y[1], y[2], y[3], y[4]);
            return;
        }
    }
}

// 0x89694e: whether the header starts with 'GM'.
DIFF_TEST(GimexFshGm_IsGm, 0x89694e)
{
    using Fn = int32_t(__cdecl *)(const uint8_t *);
    const Fn original = ctx.Original<Fn>(kIsGm);
    alignas(4) uint8_t header[4] = {0, 0, 0xa5, 0xa5};
    for (uint32_t value = 0; value < 0x10000; ++value) {
        header[0] = static_cast<uint8_t>(value);
        header[1] = static_cast<uint8_t>(value >> 8);
        ++ctx.cases;
        const int32_t a = original(header);
        const int32_t b = gimex::fsh::IsGm(header);
        if (a != b) ctx.Fail("header %04x: original %d, ours %d", value, a, b);
    }
}
