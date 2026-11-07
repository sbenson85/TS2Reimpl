// Differential tests for the FSH back end's block decoders, header tests and RefPack coder (lane MR; src/gz/GimexFshBlocks.size.cpp,
// GimexFshRefPack.cpp, GimexFshRefPackEncode.size.cpp, and the helpers it shares in src/rz/RefPack.size.cpp). Each function runs
// alone over seeded synthetic input, the original (by RVA, in the mapped image) against ours:
// - the five header tests over all 65,536 values of the two bytes they read;
// - the colour block decoder over every pair of 16-bit colours, both forms, both byte orders, random indices and strides;
// - the alpha decoders over every pair of end values with random indices (the 4-bit one over random blocks);
// - the record decoder over every header kind (0x18fb, 0x1afb, 0x1cfb, 0x28fb, each with and without 0x80, and other values), flag
//   bytes, sizes from negative to a few thousand bytes (whole blocks and cut ones), with the premultiplication flag;
// - the RefPack decoder over streams the originals' own compressors (0x89698a, RefPack::Compress 0x9cc54d) made from data of
//   several shapes, all four headers (narrow and wide, with and without the compressed size), hand-built opcode streams of every
//   kind with random arguments (back references before the output read a shared random history), and damaged streams;
// - the match length over random buffers;
// - the compressor 0x89698a and its header writer 0x896db9 over data of several shapes and lengths (every length up to 40, window
//   edges, runs past the longest match), both `quick` values, with the codecs' allocator patched to a recording fake on both sides
//   (the sizes asked for, the frees, a scripted allocation failure).
// Compared: results, bytes read, every byte written (with margins), the source untouched, the allocator's log.
//
// Kept out: lengths under 0 (the encoder loops on a huge unsigned count); a source that is not readable up to the byte after its
// end (the matcher reads one byte past a match, as the original does); a decoder stream that writes before the history the test
// provides (131,072 bytes: the longest offset).
#define NOMINMAX
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/GimexFshBlocks.h"
#include "rz/RefPack.h"
#include "shape.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kDecodeAlpha4 = 0x894e0e;
constexpr uint32_t kDecodeAlpha8 = 0x894e67;
constexpr uint32_t kDecodeColourBlock = 0x894ff3;
constexpr uint32_t kHasHeader28fb = 0x895381;
constexpr uint32_t kHasHeader1afb = 0x8953ab;
constexpr uint32_t kHasHeader1cfb = 0x8953d5;
constexpr uint32_t kDecodeBlocks = 0x8953ff;
constexpr uint32_t kIsBlockCompressed = 0x8955c7;
constexpr uint32_t kDecodeRefPack = 0x895620;
constexpr uint32_t kIsRefPack = 0x895802;
constexpr uint32_t kMatchLength = 0x896960;
constexpr uint32_t kRefPackEncode = 0x89698a;
constexpr uint32_t kRefPackEncodeWithHeader = 0x896db9;
constexpr uint32_t kRefPackCompress = 0x9cc54d;
constexpr uint32_t kAllocate = 0x87da65;
constexpr uint32_t kFreeMemory = 0x87da72;

// ---- inputs ----

struct Fill {
    uint32_t state;
    explicit Fill(uint32_t seed) : state(seed * 2654435761u + 0x9e3779b9u) {}
    uint32_t Next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    void Bytes(uint8_t *bytes, size_t count)
    {
        for (size_t i = 0; i < count; i += 4) {
            const uint32_t word = Next();
            for (size_t k = 0; k < 4 && i + k < count; ++k) bytes[i + k] = static_cast<uint8_t>(word >> (8 * k));
        }
    }
    void Bytes(std::vector<uint8_t> &bytes) { Bytes(bytes.data(), bytes.size()); }
};

// Data of several shapes: what a compressor finds to match and what it doesn't.
enum class DataKind { Zeros, Period, Words, Random, Mixed, Far, Gradient, Count };

std::vector<uint8_t> MakeData(DataKind shape, size_t length, uint32_t seed)
{
    std::mt19937 rng(seed);
    Fill fill(seed + 1);
    std::vector<uint8_t> data(length);
    switch (shape) {
    case DataKind::Zeros:
        break;
    case DataKind::Period: {
        const size_t period = 1 + rng() % 40;
        std::vector<uint8_t> unit(period);
        fill.Bytes(unit);
        for (size_t i = 0; i < length; ++i) data[i] = unit[i % period];
        break;
    }
    case DataKind::Words: {
        static const char *const kWords[] = {"the ", "sims ", "shape ", "texture ", "alpha ", "block ", "of ", "and ", "\n", "0123 "};
        size_t at = 0;
        while (at < length) {
            const char *word = kWords[rng() % (sizeof(kWords) / sizeof(kWords[0]))];
            for (; *word != 0 && at < length; ++word) data[at++] = static_cast<uint8_t>(*word);
        }
        break;
    }
    case DataKind::Random:
        fill.Bytes(data);
        break;
    case DataKind::Mixed: {
        size_t at = 0;
        while (at < length) {
            const size_t run = 1 + rng() % 300;
            const uint32_t kind = rng() % 4;
            for (size_t k = 0; k < run && at < length; ++k, ++at) {
                if (kind == 0) data[at] = 0;
                else if (kind == 1) data[at] = static_cast<uint8_t>(at / 7);
                else if (kind == 2 && at > 100) data[at] = data[at - 1 - rng() % 100];
                else data[at] = static_cast<uint8_t>(rng());
            }
        }
        break;
    }
    case DataKind::Far: {
        // random blocks repeated at long distances, so the 3- and 4-byte opcodes carry big offsets
        fill.Bytes(data);
        const size_t block = 64 + rng() % 600;
        for (size_t at = block * 2; at + block < length; at += block * (3 + rng() % 400)) {
            const size_t back = 1 + rng() % std::min<size_t>(at, 0x20000);
            for (size_t k = 0; k < block && at + k < length; ++k) data[at + k] = data[at + k - back];
        }
        break;
    }
    default:
        for (size_t i = 0; i < length; ++i) data[i] = static_cast<uint8_t>(i * 3 + (i >> 8));
        break;
    }
    return data;
}

// ---- the recording allocator (both sides' allocator and free) ----

std::vector<uint32_t> gLog;
std::vector<void *> gBlocks;
int gFailAt = -1;
int gCalls = 0;

void *__stdcall FakeAllocate(uint32_t size)
{
    const int index = gCalls++;
    gLog.push_back(0x1000000u);
    gLog.push_back(size);
    if (index == gFailAt) return nullptr;
    void *const block = std::malloc(size != 0 ? size : 1);
    std::memset(block, 0xa5, size);
    gBlocks.push_back(block);
    return block;
}

uint8_t __stdcall FakeRelease(void *block)
{
    uint32_t ordinal = 0;
    for (size_t i = 0; i < gBlocks.size(); ++i)
        if (gBlocks[i] == block && block != nullptr) ordinal = static_cast<uint32_t>(i) + 1;
    gLog.push_back(0x2000000u);
    gLog.push_back(block == nullptr ? 0xffffffffu : ordinal);
    if (ordinal != 0) {
        std::free(block);
        gBlocks[ordinal - 1] = nullptr;
    }
    return 1;
}

void ResetHeap(int failAt)
{
    for (void *block : gBlocks) std::free(block); // what the run leaked
    gBlocks.clear();
    gLog.clear();
    gCalls = 0;
    gFailAt = failAt;
}

struct HeapFakes {
    shape::CodePatch allocate{kAllocate, reinterpret_cast<const void *>(&FakeAllocate)};
    shape::CodePatch release{kFreeMemory, reinterpret_cast<const void *>(&FakeRelease)};
    shape::CodePatch ourAllocate{reinterpret_cast<void *>(&gimex::Allocate), reinterpret_cast<const void *>(&FakeAllocate)};
    shape::CodePatch ourRelease{reinterpret_cast<void *>(&gimex::FreeMemory), reinterpret_cast<const void *>(&FakeRelease)};
    ~HeapFakes() { ResetHeap(-1); }
};

// ---- header tests ----

void HeaderCases(difftest::Context &ctx, uint32_t rva, uint32_t(__stdcall *ours)(const uint8_t *))
{
    using Fn = uint32_t(__stdcall *)(const uint8_t *);
    const Fn original = ctx.Original<Fn>(rva);
    alignas(4) uint8_t header[4] = {0, 0, 0xa5, 0xa5};
    for (uint32_t value = 0; value < 0x10000; ++value) {
        header[0] = static_cast<uint8_t>(value >> 8);
        header[1] = static_cast<uint8_t>(value);
        ++ctx.cases;
        const uint32_t a = original(header);
        const uint32_t b = ours(header);
        if (a != b) ctx.Fail("header %04x: original %u, ours %u", value, a, b);
    }
}

void HeaderBoolCases(difftest::Context &ctx, uint32_t rva, bool(__stdcall *ours)(const uint8_t *))
{
    using Fn = bool(__stdcall *)(const uint8_t *); // only al is set
    const Fn original = ctx.Original<Fn>(rva);
    alignas(4) uint8_t header[4] = {0, 0, 0xa5, 0xa5};
    for (uint32_t value = 0; value < 0x10000; ++value) {
        header[0] = static_cast<uint8_t>(value >> 8);
        header[1] = static_cast<uint8_t>(value);
        ++ctx.cases;
        const bool a = original(header);
        const bool b = ours(header);
        if (a != b) ctx.Fail("header %04x: original %d, ours %d", value, a, b);
    }
}

// The buffers of a block decoder: both sides start alike.
struct Pair {
    std::vector<uint8_t> a, b;
    explicit Pair(size_t size, Fill &fill) : a(size), b()
    {
        fill.Bytes(a);
        b = a;
    }
};

void ReportBuffers(difftest::Context &ctx, const char *what, const Pair &out)
{
    size_t first = 0;
    while (first < out.a.size() && out.a[first] == out.b[first]) ++first;
    ctx.Fail("%s: outputs differ (first byte %zu: original %02x, ours %02x)", what, first, out.a[first], out.b[first]);
}

} // namespace

// 0x895381 / 0x8953ab / 0x8953d5: the 0x28fb / 0x1afb / 0x1cfb header tests (100 or 0).
DIFF_TEST(GimexFshBlocks_HasHeader28fb, 0x895381)
{
    HeaderCases(ctx, kHasHeader28fb, &gimex::fsh::HasHeader28fb);
}

DIFF_TEST(GimexFshBlocks_HasHeader1afb, 0x8953ab)
{
    HeaderCases(ctx, kHasHeader1afb, &gimex::fsh::HasHeader1afb);
}

DIFF_TEST(GimexFshBlocks_HasHeader1cfb, 0x8953d5)
{
    HeaderCases(ctx, kHasHeader1cfb, &gimex::fsh::HasHeader1cfb);
}

// 0x8955c7: whether a header is a block kind (only al).
DIFF_TEST(GimexFshBlocks_IsBlockCompressed, 0x8955c7)
{
    HeaderBoolCases(ctx, kIsBlockCompressed, &gimex::fsh::IsBlockCompressed);
}

// 0x895802: whether a header is RefPack's (only al).
DIFF_TEST(GimexFshBlocks_IsRefPack, 0x895802)
{
    HeaderBoolCases(ctx, kIsRefPack, &gimex::fsh::IsRefPack);
}

// 0x894e0e: the explicit 4-bit alpha block.
DIFF_TEST(GimexFshBlocks_DecodeAlpha4, 0x894e0e)
{
    using Fn = void(__cdecl *)(uint8_t *, int32_t, const uint8_t *);
    const Fn original = ctx.Original<Fn>(kDecodeAlpha4);
    Fill fill(0x894e0e);
    std::mt19937 rng(0x894e0e);
    for (int i = 0; i < 6000; ++i) {
        uint8_t block[8 + 8];
        fill.Bytes(block, sizeof(block));
        if (i == 0) std::memset(block, 0, 8);
        if (i == 1) std::memset(block, 0xff, 8);
        const int32_t stride = i % 3 == 0 ? 16 : static_cast<int32_t>(rng() % 90);
        Pair out(4 * 90 + 64, fill);
        ++ctx.cases;
        original(out.a.data(), stride, block);
        gimex::fsh::DecodeAlpha4(out.b.data(), stride, block);
        if (out.a != out.b) ReportBuffers(ctx, "alpha4", out);
    }
}

// 0x894e67: the interpolated alpha block, every pair of end values (the stride argument is not read).
DIFF_TEST(GimexFshBlocks_DecodeAlpha8, 0x894e67)
{
    using Fn = void(__cdecl *)(uint8_t *, int32_t, const uint8_t *);
    const Fn original = ctx.Original<Fn>(kDecodeAlpha8);
    Fill fill(0x894e67);
    for (uint32_t ends = 0; ends < 0x10000; ++ends) {
        for (int k = 0; k < 2; ++k) {
            uint8_t block[16];
            fill.Bytes(block, sizeof(block));
            block[0] = static_cast<uint8_t>(ends);
            block[1] = static_cast<uint8_t>(ends >> 8);
            if (k == 1) std::memset(block + 2, ends & 1 ? 0xff : 0x00, 6);
            const int32_t stride = k == 0 ? 0x10 : static_cast<int32_t>(fill.Next() % 200) - 50;
            Pair out(64 + 32, fill);
            ++ctx.cases;
            original(out.a.data(), stride, block);
            gimex::fsh::DecodeAlpha8(out.b.data(), stride, block);
            if (out.a != out.b) {
                ReportBuffers(ctx, "alpha8", out);
                return;
            }
        }
    }
}

// 0x894ff3: the colour block, every pair of colours, both forms and byte orders.
DIFF_TEST(GimexFshBlocks_DecodeColourBlock, 0x894ff3)
{
    using Fn = void(__cdecl *)(uint8_t *, int32_t, const uint8_t *, uint32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kDecodeColourBlock);
    Fill fill(0x894ff3);
    const uint32_t forms[] = {0, 4, 1};
    const int32_t endians[] = {0, 100, 1};
    for (uint32_t colours = 0; colours < 0x10000; ++colours) {
        for (int variant = 0; variant < 4; ++variant) {
            for (int pass = 0; pass < 2; ++pass) {
                uint8_t block[12];
                fill.Bytes(block, sizeof(block));
                const uint32_t second = fill.Next() & 0xffff;
                // first colour from the loop, the second random; the order is also swapped in the second pass
                const uint32_t first = colours;
                const uint32_t c0 = pass == 0 ? first : second;
                const uint32_t c1 = pass == 0 ? second : first;
                const int32_t endian = (variant & 1) != 0 ? endians[1 + pass] : endians[0];
                const uint32_t form = (variant & 2) != 0 ? forms[1 + pass] : forms[0];
                if (endian != 0) {
                    block[0] = static_cast<uint8_t>(c0 >> 8);
                    block[1] = static_cast<uint8_t>(c0);
                    block[2] = static_cast<uint8_t>(c1 >> 8);
                    block[3] = static_cast<uint8_t>(c1);
                } else {
                    block[0] = static_cast<uint8_t>(c0);
                    block[1] = static_cast<uint8_t>(c0 >> 8);
                    block[2] = static_cast<uint8_t>(c1);
                    block[3] = static_cast<uint8_t>(c1 >> 8);
                }
                const int32_t stride = (variant == 0 && pass == 0) ? 0x10 : static_cast<int32_t>(fill.Next() % 70);
                Pair out(4 * 70 + 64, fill);
                ++ctx.cases;
                original(out.a.data(), stride, block, form, endian);
                gimex::fsh::DecodeColourBlock(out.b.data(), stride, block, form, endian);
                if (out.a != out.b) {
                    ctx.Fail("colours %04x %04x form %u endian %d stride %d", c0, c1, form, endian, stride);
                    ReportBuffers(ctx, "colour block", out);
                    return;
                }
            }
        }
    }
}

namespace {

// A block-compressed record of `size` decoded bytes.
std::vector<uint8_t> MakeRecord(Fill &fill, std::mt19937 &rng, uint32_t type, bool wide, int64_t size, uint8_t flags, int64_t dataBlocks)
{
    const size_t perBlock = (type == 0x1a || type == 0x1c || type == 0x9a || type == 0x9c) ? 16 : 8;
    std::vector<uint8_t> record(8 + static_cast<size_t>(std::max<int64_t>(dataBlocks, 1)) * perBlock + 32);
    fill.Bytes(record);
    record[0] = static_cast<uint8_t>(type | (wide ? 0x80 : 0));
    record[1] = 0xfb;
    if (wide) {
        record[2] = static_cast<uint8_t>(size >> 24);
        record[3] = static_cast<uint8_t>(size >> 16);
        record[4] = static_cast<uint8_t>(size >> 8);
        record[5] = static_cast<uint8_t>(size);
        record[6] = flags;
    } else {
        record[2] = static_cast<uint8_t>(size >> 16);
        record[3] = static_cast<uint8_t>(size >> 8);
        record[4] = static_cast<uint8_t>(size);
        record[5] = flags;
    }
    (void)rng;
    return record;
}

} // namespace

// 0x8953ff: the record decoder (stdcall), every kind of header, flags and sizes.
DIFF_TEST(GimexFshBlocks_DecodeBlocks, 0x8953ff)
{
    using Fn = int32_t(__stdcall *)(uint8_t *, const uint8_t *, int32_t *);
    const Fn original = ctx.Original<Fn>(kDecodeBlocks);
    Fill fill(0x8953ff);
    std::mt19937 rng(0x8953ff);
    const uint32_t types[] = {0x18, 0x1a, 0x1c, 0x28, 0x10, 0x11, 0x00, 0x19, 0x1b, 0x1d, 0x29, 0x38, 0x08, 0x2a, 0x1e};
    const int64_t sizeChoices[] = {1, 2, 3, 4, 5, 63, 64, 65, 127, 128, 129, 191, 192, 193, 256, 255, 1000, 0, 0};
    for (int i = 0; i < 6000; ++i) {
        const uint32_t type = types[rng() % (sizeof(types) / sizeof(types[0]))];
        const bool wide = rng() % 2 != 0;
        int64_t size;
        const uint32_t pick = rng() % 10;
        if (pick < 5) size = sizeChoices[rng() % (sizeof(sizeChoices) / sizeof(sizeChoices[0]))];
        else if (pick < 8) size = 1 + rng() % 3000;
        else if (pick == 8) size = -static_cast<int64_t>(1 + rng() % 70);
        else size = rng() % 8;
        if (!wide && size < 0) size = size & 0xffffff; // a narrow size is 24 bits: keep the test's buffers small
        if (!wide && size > 6000) size = 6000;
        const uint8_t flags = static_cast<uint8_t>(rng());
        const int64_t blocks = size > 0 ? ((size - 1) >> 6) + 1 : 0;
        std::vector<uint8_t> record = MakeRecord(fill, rng, type, wide, size, flags, blocks);
        const std::vector<uint8_t> recordBefore = record;
        const size_t pixelBytes = static_cast<size_t>(blocks) * 64 + 512;
        Pair out(pixelBytes, fill);
        int32_t consumed[2] = {0x5a5a5a5a, 0x5a5a5a5a};
        const bool withConsumed = rng() % 4 != 0;
        ++ctx.cases;
        const int32_t a = original(out.a.data(), record.data(), withConsumed ? &consumed[0] : nullptr);
        const int32_t b = gimex::fsh::DecodeBlocks(out.b.data(), record.data(), withConsumed ? &consumed[1] : nullptr);
        if (a != b || consumed[0] != consumed[1] || out.a != out.b || record != recordBefore) {
            ctx.Fail("case %d: type %02x wide %d size %lld flags %02x: original %d consumed %d, ours %d consumed %d; outputs %s", i,
                     type, wide, static_cast<long long>(size), flags, a, consumed[0], b, consumed[1],
                     out.a == out.b ? "equal" : "differ");
            if (out.a != out.b) ReportBuffers(ctx, "records", out);
            return;
        }
    }
}

// 0x896960: the match length (shared by the FSH compressor and cRZFastCompression3's original encoder at 0x9cf4f3).
DIFF_TEST(GimexFshBlocks_MatchLength, 0x896960)
{
    using Fn = uint32_t(__cdecl *)(const uint8_t *, const uint8_t *, uint32_t);
    const Fn original = ctx.Original<Fn>(kMatchLength);
    Fill fill(0x896960);
    std::mt19937 rng(0x896960);
    std::vector<uint8_t> a(1200), b(1200);
    for (int i = 0; i < 20000; ++i) {
        fill.Bytes(a);
        b = a;
        const uint32_t cut = rng() % 1100;
        if (i % 7 != 0) b[cut] = static_cast<uint8_t>(b[cut] ^ (1 + rng() % 255));
        const uint32_t limit = i % 5 == 0 ? rng() % 1100 : (i % 5 == 1 ? cut : (i % 5 == 2 ? cut + 1 : 0x404));
        ++ctx.cases;
        const uint32_t x = original(a.data(), b.data(), limit);
        const uint32_t y = RefPack::MatchLength(a.data(), b.data(), limit);
        if (x != y) ctx.Fail("case %d: cut %u limit %u: original %u, ours %u", i, cut, limit, x, y);
    }
}

namespace {

// ---- RefPack streams ----

// A header for `body` (the opcode stream with its end opcode): narrow or wide, with or without the compressed-size field.
std::vector<uint8_t> WithHeader(const std::vector<uint8_t> &body, uint32_t decodedSize, bool wide, bool withCompressedSize)
{
    std::vector<uint8_t> stream;
    stream.push_back(static_cast<uint8_t>((wide ? 0x90 : 0x10) | (withCompressedSize ? 1 : 0)));
    stream.push_back(0xfb);
    const size_t sizeBytes = wide ? 4 : 3;
    auto put = [&](uint32_t value) {
        for (size_t k = 0; k < sizeBytes; ++k) stream.push_back(static_cast<uint8_t>(value >> (8 * (sizeBytes - 1 - k))));
    };
    if (withCompressedSize) put(static_cast<uint32_t>(body.size() + 2 + 2 * sizeBytes));
    put(decodedSize);
    stream.insert(stream.end(), body.begin(), body.end());
    return stream;
}

using EncodeFn = int32_t(__cdecl *)(const uint8_t *, int32_t, uint8_t *, int32_t, int32_t);

// The original compressor's opcode stream for `data` (the FSH one or cRZFastCompression3's).
std::vector<uint8_t> CompressWith(difftest::Context &ctx, const std::vector<uint8_t> &data, bool fshCompressor, int32_t quick)
{
    std::vector<uint8_t> source(data);
    source.resize(data.size() + 64, 0x5c);
    std::vector<uint8_t> out(data.size() + data.size() / 16 + 256);
    int32_t written;
    if (fshCompressor) {
        const EncodeFn fsh = ctx.Original<EncodeFn>(kRefPackEncode);
        written = fsh(source.data(), static_cast<int32_t>(data.size()), out.data(), 0x20000, quick);
    } else {
        using Compress = int32_t(__cdecl *)(const uint8_t *, int32_t, uint8_t *, const int32_t *);
        const Compress compress = ctx.Original<Compress>(kRefPackCompress);
        const int32_t flags = quick;
        written = compress(source.data(), static_cast<int32_t>(data.size()), out.data(), &flags) - 5;
        out.erase(out.begin(), out.begin() + 5); // the 5-byte header
    }
    out.resize(static_cast<size_t>(written));
    return out;
}

// One decode on both sides over a shared history: the stream is copied after a fill of 0x20100 history bytes, then the
// destination (history identical on both sides).
void DecodeCase(difftest::Context &ctx, const char *label, const std::vector<uint8_t> &stream, size_t destBytes, Fill &fill)
{
    using Fn = int32_t(__stdcall *)(uint8_t *, const uint8_t *, int32_t *);
    const Fn original = ctx.Original<Fn>(kDecodeRefPack);
    constexpr size_t kHistory = 0x20100;
    std::vector<uint8_t> padded(stream);
    padded.resize(stream.size() + 256, 0xfc); // a stream cut short ends at the first end opcode past it; a literal run may eat 112 of the pad
    Pair out(kHistory + destBytes, fill);
    int32_t consumed[2] = {0x5a5a5a5a, 0x5a5a5a5a};
    ++ctx.cases;
    const int32_t a = original(out.a.data() + kHistory, padded.data(), &consumed[0]);
    const int32_t b = gimex::fsh::DecodeRefPack(out.b.data() + kHistory, padded.data(), &consumed[1]);
    if (a != b || consumed[0] != consumed[1] || out.a != out.b) {
        ctx.Fail("%s: original %d consumed %d, ours %d consumed %d; outputs %s", label, a, consumed[0], b, consumed[1],
                 out.a == out.b ? "equal" : "differ");
        if (out.a != out.b) ReportBuffers(ctx, label, out);
    }
}

// A random valid opcode stream: `opcodes` copies of every kind with random arguments; back references reach into what was
// written or, early on, into the history. Returns the stream body (end opcode included) and the bytes it writes.
std::vector<uint8_t> MakeOpcodes(std::mt19937 &rng, int opcodes, size_t &written)
{
    std::vector<uint8_t> body;
    written = 0;
    auto literals = [&](uint32_t count) {
        for (uint32_t k = 0; k < count; ++k) body.push_back(static_cast<uint8_t>(rng()));
        written += count;
    };
    for (int i = 0; i < opcodes; ++i) {
        switch (rng() % 5) {
        case 0: { // 2 bytes: 0-3 literals, length 3-10, offset 1..1024
            const uint32_t count = rng() % 4, length = rng() % 8, offset = rng() % 1024;
            body.push_back(static_cast<uint8_t>((offset >> 3 & 0x60) | length << 2 | count));
            body.push_back(static_cast<uint8_t>(offset));
            literals(count);
            written += length + 3;
            break;
        }
        case 1: { // 3 bytes: 0-3 literals, length 4-67, offset 1..16384
            const uint32_t count = rng() % 4, length = rng() % 64, offset = rng() % 16384;
            body.push_back(static_cast<uint8_t>(0x80 | length));
            body.push_back(static_cast<uint8_t>(count << 6 | offset >> 8));
            body.push_back(static_cast<uint8_t>(offset));
            literals(count);
            written += length + 4;
            break;
        }
        case 2: { // 4 bytes: 0-3 literals, length 5-1028, offset 1..131072
            const uint32_t count = rng() % 4, length = rng() % 1024, offset = rng() % 131072;
            body.push_back(static_cast<uint8_t>(0xc0 | (offset >> 17 & 1) << 4 | (length >> 8) << 2 | count));
            body.push_back(static_cast<uint8_t>(offset >> 8));
            body.push_back(static_cast<uint8_t>(offset));
            body.push_back(static_cast<uint8_t>(length));
            literals(count);
            written += length + 5;
            break;
        }
        case 3: { // literal run of 4..112
            const uint32_t count = 4 + 4 * (rng() % 28);
            body.push_back(static_cast<uint8_t>(0xe0 | (count / 4 - 1)));
            literals(count);
            break;
        }
        default: { // overlapping reference: offset 1 (a run)
            const uint32_t length = rng() % 8;
            body.push_back(static_cast<uint8_t>(length << 2));
            body.push_back(0);
            written += length + 3;
            break;
        }
        }
    }
    const uint32_t tail = rng() % 4;
    body.push_back(static_cast<uint8_t>(0xfc + tail));
    literals(tail);
    return body;
}

} // namespace

// 0x895620: the RefPack decoder (stdcall).
DIFF_TEST(GimexFshBlocks_DecodeRefPack, 0x895620)
{
    Fill fill(0x895620);
    std::mt19937 rng(0x895620);
    // streams from the originals' compressors, all four headers
    int index = 0;
    for (int shapeIndex = 0; shapeIndex < static_cast<int>(DataKind::Count); ++shapeIndex) {
        const size_t lengths[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 11, 40, 100, 255, 256, 1000, 4097, 20000, 70000, 150000};
        for (const size_t length : lengths) {
            const std::vector<uint8_t> data = MakeData(static_cast<DataKind>(shapeIndex), length, 0x895620 + index++);
            for (int variant = 0; variant < 4; ++variant) {
                const bool fshCompressor = variant < 2;
                const std::vector<uint8_t> body = CompressWith(ctx, data, fshCompressor, variant & 1);
                for (int header = 0; header < 4; ++header) {
                    const std::vector<uint8_t> stream = WithHeader(body, static_cast<uint32_t>(length), (header & 1) != 0, (header & 2) != 0);
                    DecodeCase(ctx, "compressed data", stream, length + 1200, fill);
                    if (length > 20000) break; // the large ones: narrow and wide forms once
                }
                if (length > 70000) break;
            }
        }
    }
    // random valid opcodes of every kind, back references into the history
    for (int i = 0; i < 400; ++i) {
        size_t written;
        const std::vector<uint8_t> body = MakeOpcodes(rng, 1 + static_cast<int>(rng() % 30), written);
        DecodeCase(ctx, "opcodes", WithHeader(body, static_cast<uint32_t>(written), (rng() & 1) != 0, (rng() & 2) != 0), written + 1200, fill);
    }
    // damaged: flipped bytes, cut short, a header byte changed
    for (int i = 0; i < 600; ++i) {
        const std::vector<uint8_t> data = MakeData(static_cast<DataKind>(rng() % static_cast<uint32_t>(DataKind::Count)), 100 + rng() % 600, 0x895621 + i);
        std::vector<uint8_t> stream = WithHeader(CompressWith(ctx, data, true, 0), static_cast<uint32_t>(data.size()), (rng() & 1) != 0, (rng() & 2) != 0);
        const uint32_t damage = rng() % 4;
        if (damage == 0) stream[rng() % stream.size()] = static_cast<uint8_t>(rng());
        else if (damage == 1) stream.resize(1 + rng() % stream.size());
        else if (damage == 2) stream[rng() % 8 % stream.size()] ^= static_cast<uint8_t>(1 + rng() % 255);
        else for (int k = 0; k < 4; ++k) stream[rng() % stream.size()] = static_cast<uint8_t>(rng());
        if (stream.size() < 8) stream.resize(8, 0);
        DecodeCase(ctx, "damaged", stream, 1100000, fill);
    }
    // a null source and no consumed pointer
    {
        using Fn = int32_t(__stdcall *)(uint8_t *, const uint8_t *, int32_t *);
        const Fn original = ctx.Original<Fn>(kDecodeRefPack);
        uint8_t dest[16] = {1, 2, 3};
        int32_t c0 = 77, c1 = 77;
        ++ctx.cases;
        const int32_t a = original(dest, nullptr, &c0);
        const int32_t b = gimex::fsh::DecodeRefPack(dest, nullptr, &c1);
        if (a != b || c0 != c1) ctx.Fail("null source: original %d / %d, ours %d / %d", a, c0, b, c1);
        const std::vector<uint8_t> stream = WithHeader({0xfc}, 0, false, false);
        uint8_t destA[16] = {9}, destB[16] = {9};
        const int32_t x = original(destA, stream.data(), nullptr);
        const int32_t y = gimex::fsh::DecodeRefPack(destB, stream.data(), nullptr);
        if (x != y || std::memcmp(destA, destB, sizeof(destA)) != 0) ctx.Fail("no consumed pointer: original %d, ours %d", x, y);
    }
}

namespace {

// Both compressors on one input; the allocator's log, the output and the result compared.
struct EncodeRun {
    int32_t result = 0;
    std::vector<uint8_t> dest;
    std::vector<uint32_t> log;
    size_t leaked = 0;
};

template <typename Call> EncodeRun RunEncode(size_t destBytes, uint8_t fillByte, int failAt, Call call)
{
    EncodeRun run;
    run.dest.assign(destBytes, fillByte);
    ResetHeap(failAt);
    run.result = call(run.dest.data());
    run.log = gLog;
    for (void *block : gBlocks) run.leaked += block != nullptr ? 1 : 0;
    ResetHeap(-1);
    return run;
}

bool SameRun(const EncodeRun &a, const EncodeRun &b)
{
    return a.result == b.result && a.dest == b.dest && a.log == b.log && a.leaked == b.leaked;
}

void ReportRun(difftest::Context &ctx, const char *label, const EncodeRun &a, const EncodeRun &b)
{
    size_t first = 0;
    while (first < a.dest.size() && first < b.dest.size() && a.dest[first] == b.dest[first]) ++first;
    ctx.Fail("%s: original returns %d (%zu log words, %zu blocks left), ours %d (%zu log words, %zu blocks left); output %s (first byte %zu)",
             label, a.result, a.log.size(), a.leaked, b.result, b.log.size(), b.leaked, a.dest == b.dest ? "equal" : "differ", first);
}

} // namespace

// 0x89698a: the compressor.
DIFF_TEST(GimexFshBlocks_RefPackEncode, 0x89698a)
{
    const EncodeFn original = ctx.Original<EncodeFn>(kRefPackEncode);
    HeapFakes heap;
    std::mt19937 rng(0x89698a);
    int seed = 0;
    auto check = [&](const char *label, const std::vector<uint8_t> &data, int32_t quick, int failAt) {
        std::vector<uint8_t> source(data);
        source.resize(data.size() + 64, 0x5c);
        const size_t destBytes = data.size() + data.size() / 8 + 300;
        const uint8_t fillByte = static_cast<uint8_t>(0xd0 + (seed++ & 7));
        const EncodeRun a = RunEncode(destBytes, fillByte, failAt, [&](uint8_t *dest) {
            return original(source.data(), static_cast<int32_t>(data.size()), dest, 0x20000, quick);
        });
        const EncodeRun b = RunEncode(destBytes, fillByte, failAt, [&](uint8_t *dest) {
            return gimex::fsh::RefPackEncode(source.data(), static_cast<int32_t>(data.size()), dest, 0x20000, quick);
        });
        ++ctx.cases;
        if (!SameRun(a, b)) ReportRun(ctx, label, a, b);
    };
    for (size_t length = 0; length <= 40; ++length)
        for (int shapeIndex = 0; shapeIndex < static_cast<int>(DataKind::Count); ++shapeIndex)
            for (int quick = 0; quick < 2; ++quick) check("short", MakeData(static_cast<DataKind>(shapeIndex), length, 0x89698a + static_cast<uint32_t>(length * 7 + shapeIndex)), quick, -1);
    for (int i = 0; i < 300; ++i) {
        const DataKind shape = static_cast<DataKind>(rng() % static_cast<uint32_t>(DataKind::Count));
        const size_t length = i % 4 == 0 ? rng() % 200 : rng() % 20000;
        check("random", MakeData(shape, length, 0x89698b + static_cast<uint32_t>(i)), static_cast<int32_t>(rng() & 1), -1);
    }
    // the window edges and runs past the longest match
    const size_t edges[] = {0x1ffff - 1, 0x1ffff, 0x20000, 0x20001, 0x20002, 0x20000 + 0x404, 0x30000};
    for (const size_t length : edges) {
        for (int shapeIndex = 0; shapeIndex < static_cast<int>(DataKind::Count); ++shapeIndex) {
            check("window", MakeData(static_cast<DataKind>(shapeIndex), length, 0x89698c + static_cast<uint32_t>(shapeIndex)), 0, -1);
            check("window quick", MakeData(static_cast<DataKind>(shapeIndex), length, 0x89698c + static_cast<uint32_t>(shapeIndex)), 1, -1);
        }
    }
    {
        // a long run, a period of 0x20000 exactly and one past it
        for (const size_t period : {size_t{0x1ffff}, size_t{0x20000}, size_t{0x20001}, size_t{0x1fffe}}) {
            std::vector<uint8_t> data(period * 2 + 20);
            Fill fill(static_cast<uint32_t>(period));
            fill.Bytes(data.data(), period);
            for (size_t k = period; k < data.size(); ++k) data[k] = data[k - period];
            check("period", data, 0, -1);
        }
        check("run", std::vector<uint8_t>(6000, 0x41), 0, -1);
        check("run quick", std::vector<uint8_t>(6000, 0x41), 1, -1);
    }
    // an allocation that fails: the first (nothing else happens) and the second (the first is left allocated)
    for (int failAt = 0; failAt < 2; ++failAt)
        for (int i = 0; i < 4; ++i) check("allocation failure", MakeData(DataKind::Mixed, 100 + static_cast<size_t>(i) * 90, 77), 0, failAt);
}

// 0x896db9: the header writer (stdcall; its fourth argument is not read).
DIFF_TEST(GimexFshBlocks_RefPackEncodeWithHeader, 0x896db9)
{
    using Fn = int32_t(__stdcall *)(uint8_t *, const uint8_t *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kRefPackEncodeWithHeader);
    HeapFakes heap;
    std::mt19937 rng(0x896db9);
    int seed = 0;
    for (int i = 0; i < 200; ++i) {
        const DataKind shape = static_cast<DataKind>(rng() % static_cast<uint32_t>(DataKind::Count));
        const size_t length = i < 30 ? static_cast<size_t>(i) : (i % 3 == 0 ? rng() % 300 : rng() % 30000);
        const std::vector<uint8_t> data = MakeData(shape, length, 0x896db9 + static_cast<uint32_t>(i));
        std::vector<uint8_t> source(data);
        source.resize(data.size() + 64, 0x5c);
        const int32_t unused = static_cast<int32_t>(rng());
        const size_t destBytes = data.size() + data.size() / 8 + 400;
        const uint8_t fillByte = static_cast<uint8_t>(0xe0 + (seed++ & 7));
        const EncodeRun a = RunEncode(destBytes, fillByte, -1, [&](uint8_t *dest) {
            return original(dest, source.data(), static_cast<int32_t>(data.size()), unused);
        });
        const EncodeRun b = RunEncode(destBytes, fillByte, -1, [&](uint8_t *dest) {
            return gimex::fsh::RefPackEncodeWithHeader(dest, source.data(), static_cast<int32_t>(data.size()), unused);
        });
        ++ctx.cases;
        if (!SameRun(a, b)) ReportRun(ctx, "with header", a, b);
    }
    // the size field across its range, and the wide header at 16 MB and more (a run, so the work is small)
    for (const size_t length : {static_cast<size_t>(0x9000), static_cast<size_t>(0x123456), static_cast<size_t>(0xffffff),
                                static_cast<size_t>(0x1000000), static_cast<size_t>(0x1000001)}) {
        std::vector<uint8_t> source(length + 64, 0x33);
        const size_t destBytes = length / 100 + 4096;
        const EncodeRun a = RunEncode(destBytes, 0xcc, -1, [&](uint8_t *dest) {
            return original(dest, source.data(), static_cast<int32_t>(length), 0);
        });
        const EncodeRun b = RunEncode(destBytes, 0xcc, -1, [&](uint8_t *dest) {
            return gimex::fsh::RefPackEncodeWithHeader(dest, source.data(), static_cast<int32_t>(length), 0);
        });
        ++ctx.cases;
        if (!SameRun(a, b)) ReportRun(ctx, "wide header", a, b);
        const uint8_t expectFirst = length > 0xffffff ? 0x90 : 0x10;
        if (a.dest[0] != expectFirst) ctx.Fail("the header form is wrong (first byte %02x)", a.dest[0]);
    }
}
