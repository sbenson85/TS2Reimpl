// Differential tests for the FSH back end's pixel functions (lane MX; src/gz/GimexFshPixels.size.cpp): the row copiers, the block
// reorderers, the block-coded image decoder, the untiler, the YUV converters, the per-pixel unpacker and the shape decoder. Each
// runs alone over seeded synthetic input, the original (by RVA, in the mapped image) against ours, with the codecs' allocator patched
// to a recording fake on both sides and the stack cleared before each call. Compared: results, every byte of every buffer
// (destinations are pre-filled with the same noise and carry margins past the end), the sources, the offsets and counters passed by
// pointer, and the allocator log (sizes asked for, frees) with the blocks left allocated.
//
// What is real and what is faked below the functions under test: the colour block decoders, the unswizzlers, the tile offsets and the
// colour converters are the original on one side and ours on the other (each verified by its own test). The Gm decoder (0x89662e) is
// patched to a recording fake in the shape decoder's test, since valid Gm streams need an encoder; its arguments and the rows that
// follow it are compared. YuvPixel (0x888939) and the pair converter (0x888a19) stay the original on both sides until lane MV's merge.
//
// Kept out: block-coded images whose coded size passes 0xf42400 (the 4-byte size header; the image would be 16 MB); sources the original
// reads past the end of (the buffers carry 4 KB of margin: the tile and swizzle readers stay inside it for the sizes drawn).
#include "gz_gimex_fsh_decode_common.h"

#include "gz/GimexFshGm.h"
#include "gz/GimexFshLeaves.h"

#include <algorithm>

namespace {

using namespace fshdecode;
using namespace gimex::fsh;

constexpr uint32_t kCopyRows = 0x88584d;
constexpr uint32_t kBlockIndex = 0x88828f;
constexpr uint32_t kBlocksToRows = 0x886466;
constexpr uint32_t kUnblockRows = 0x887f0a;
constexpr uint32_t kDecodeBlockImage = 0x88588e;
constexpr uint32_t kUntile = 0x8882bc;
constexpr uint32_t kAssemblePlanes = 0x885add;
constexpr uint32_t kYuvPixel = 0x88855e;
constexpr uint32_t kYuvFloats = 0x8885a1;
constexpr uint32_t kYuvToRgba = 0x888623;
constexpr uint32_t kYuvPairs = 0x888826;
constexpr uint32_t kUnpackPixel = 0x887507;
constexpr uint32_t kDecodeShape = 0x886e83;
constexpr uint32_t kDecodeGm = 0x89662e;

// Two copies of one buffer, both noise.
struct Twin {
    Buf a, b;
    Twin(std::mt19937 &rng, size_t size) : a(size)
    {
        RandomFill(rng, a.data(), size);
        b = a;
    }
};

// Gm decoder fake: logs its arguments and writes a pattern into rows of `stride` bytes.
int32_t __cdecl FakeDecodeGm(uint8_t *dest, int32_t stride, const uint8_t *data, int32_t width, int32_t height, uint32_t flags)
{
    gLog.insert(gLog.end(), {0x5000000u, Ordinal(dest), static_cast<uint32_t>(stride), static_cast<uint32_t>(width),
                             static_cast<uint32_t>(height), flags, data[0], data[1], data[4], data[5], data[6], data[7]});
    for (int32_t y = 0; y < height; ++y)
        for (int32_t x = 0; x < stride; ++x) dest[y * stride + x] = static_cast<uint8_t>(y * 31 + x * 7 + data[8]);
    return 1;
}

} // namespace

// 0x88584d: the packed copy of rows.
DIFF_TEST(GimexFshPixels_CopyRows, 0x88584d)
{
    using Fn = void(__cdecl *)(const uint8_t *, uint8_t *, int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kCopyRows);
    std::mt19937 rng(0x88584d);
    for (int round = 0; round < 3000; ++round) {
        const int32_t width = static_cast<int32_t>(rng() % 24);
        const int32_t rows = static_cast<int32_t>(Pick(rng, {0, 1, 2, 5, 17, 0xffffffff, 0x80000000, rng() % 12}));
        const int32_t stride = static_cast<int32_t>(rng() % 3 ? width + rng() % 9 : rng() % 30);
        Buf source(static_cast<size_t>((stride + 1) * 4 * (rows > 0 && rows < 100 ? rows : 1) + 64));
        RandomFill(rng, source.data(), source.size());
        Twin dest(rng, static_cast<size_t>(width * 4 * (rows > 0 && rows < 100 ? rows : 1) + 64));
        const int32_t unused = static_cast<int32_t>(rng());
        ++ctx.cases;
        ClearStack();
        original(source.data(), dest.a.data(), stride, unused, width, rows);
        ClearStack();
        CopyRows(source.data(), dest.b.data(), stride, unused, width, rows);
        CompareBytes(ctx, "CopyRows", round, dest.a.data(), dest.b.data(), dest.a.size());
    }
}

// 0x88828f: the block position.
DIFF_TEST(GimexFshPixels_BlockIndex, 0x88828f)
{
    using Fn = int32_t(__cdecl *)(int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kBlockIndex);
    std::mt19937 rng(0x88828f);
    for (int x = -3; x < 70; ++x)
        for (int y = -3; y < 70; ++y)
            for (int width = -2; width < 40; ++width) {
                ++ctx.cases;
                ClearStack();
                const int32_t a = original(x, y, width);
                ClearStack();
                const int32_t b = BlockIndex(x, y, width);
                if (a != b) ctx.Fail("x %d y %d width %d: original %d, ours %d", x, y, width, a, b);
            }
    for (int round = 0; round < 20000; ++round) {
        const int32_t x = static_cast<int32_t>(rng()), y = static_cast<int32_t>(rng()), w = static_cast<int32_t>(rng());
        ++ctx.cases;
        const int32_t a = original(x, y, w);
        const int32_t b = BlockIndex(x, y, w);
        if (a != b) ctx.Fail("x %d y %d width %d: original %d, ours %d", x, y, w, a, b);
    }
}

// 0x886466 / 0x887f0a: blocks to rows.
namespace {
void ReorderCases(difftest::Context &ctx, uint32_t rva, void(__cdecl *ours)(int32_t, int32_t, int32_t, const uint8_t *, uint8_t *), const char *name,
                  uint32_t seed)
{
    using Fn = void(__cdecl *)(int32_t, int32_t, int32_t, const uint8_t *, uint8_t *);
    const Fn original = ctx.Original<Fn>(rva);
    std::mt19937 rng(seed);
    for (int round = 0; round < 4000; ++round) {
        const int32_t width = static_cast<int32_t>(round < 400 ? round % 20 : rng() % 48);
        const int32_t height = static_cast<int32_t>(round < 400 ? round / 20 % 20 : rng() % 48);
        const int32_t stride = static_cast<int32_t>(width * 4 + (rng() % 3 ? 0 : (rng() % 5) * 4));
        Buf blocks(0x10000);
        RandomFill(rng, blocks.data(), blocks.size());
        Twin dest(rng, static_cast<size_t>(stride * (height + 4) + 256));
        ++ctx.cases;
        ClearStack();
        original(width, height, stride, blocks.data(), dest.a.data());
        ClearStack();
        ours(width, height, stride, blocks.data(), dest.b.data());
        CompareBytes(ctx, name, round, dest.a.data(), dest.b.data(), dest.a.size());
    }
}
} // namespace

DIFF_TEST(GimexFshPixels_BlocksToRows, 0x886466)
{
    ReorderCases(ctx, kBlocksToRows, &BlocksToRows, "BlocksToRows", 0x886466);
}

DIFF_TEST(GimexFshPixels_UnblockRows, 0x887f0a)
{
    ReorderCases(ctx, kUnblockRows, &UnblockRows, "UnblockRows", 0x887f0a);
}

// 0x88588e: a block-coded image to rows.
DIFF_TEST(GimexFshPixels_DecodeBlockImage, 0x88588e)
{
    using Fn = void(__cdecl *)(uint8_t *, int32_t, uint8_t *, int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kDecodeBlockImage);
    HeapFakes heap;
    std::mt19937 rng(0x88588e);
    for (int round = 0; round < 5000; ++round) {
        const int32_t type = static_cast<int32_t>(Pick(rng, {0x60, 0x61, 0x62, 0x1e, 0x60, 0x61, 0x62, 0x1e, 0x1c, 0x7d, 0x20, rng() & 0xff}));
        const int32_t copyHeader = rng() % 5 != 0 ? ((type >= 0x60 && type <= 0x62) || type == 0x1e ? 1 : 0) : static_cast<int32_t>(rng() % 2);
        const int32_t width = static_cast<int32_t>(round < 200 ? 1 + round % 20 : 1 + rng() % 40);
        const int32_t height = static_cast<int32_t>(round < 200 ? 1 + round / 20 % 10 : 1 + rng() % 40);
        const int32_t pitch = static_cast<int32_t>(width * 4 + (rng() % 3 ? 0 : rng() % 8 * 4));
        Buf record(0x4000 + 16);
        RandomFill(rng, record.data(), record.size());
        // the self-describing form: the header the decoder reads (magic, size, dimensions in blocks)
        {
            const bool eight = type == 0x1e || rng() % 6 == 0;
            const int32_t bw = (eight ? (width + 7) & ~7 : (width + 3) & ~3) >> 2;
            const int32_t bh = (eight ? (height + 7) & ~7 : (height + 3) & ~3) >> 2;
            static const uint8_t kMagic[] = {0x18, 0x1a, 0x1c, 0x28};
            record[0] = eight ? 0x28 : kMagic[rng() % 3];
            record[1] = 0xfb;
            record[5] = 0;
            record[6] = static_cast<uint8_t>(bw);
            record[7] = static_cast<uint8_t>(bh);
        }
        Twin recordCopy(rng, 0);
        recordCopy.a = record;
        recordCopy.b = record;
        Twin dest(rng, static_cast<size_t>(std::max(pitch * height, width * height * 4) + 512));
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(-1);
            ClearStack();
            if (side == 0) original(recordCopy.a.data(), type, dest.a.data(), copyHeader, pitch, width, height);
            else DecodeBlockImage(recordCopy.b.data(), type, dest.b.data(), copyHeader, pitch, width, height);
            logs[side] = gLog;
            if (!gBlocks.empty()) {
                for (void *block : gBlocks) if (block != nullptr) logs[side].push_back(0x7000000u);
            }
        }
        ++ctx.cases;
        CompareBytes(ctx, "DecodeBlockImage dest", round, dest.a.data(), dest.b.data(), dest.a.size());
        CompareBytes(ctx, "DecodeBlockImage record", round, recordCopy.a.data(), recordCopy.b.data(), recordCopy.a.size());
        CompareLogs(ctx, "DecodeBlockImage", round, logs[0], logs[1]);
    }
}

// 0x8882bc: the untiler.
DIFF_TEST(GimexFshPixels_Untile, 0x8882bc)
{
    using Fn = uint8_t *(__cdecl *)(const uint8_t *, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kUntile);
    HeapFakes heap;
    std::mt19937 rng(0x8882bc);
    static const int32_t kBits[] = {4, 8, 0xc, 0xf, 0x10, 0x18, 0x20, 1, 2, 24, 0x21, 0x30, 0, 0xffffffff};
    static const int32_t kKinds[] = {0x40, 0x79, 0x7a, 0x16, 0x1e, 0x7b, 0x02, 0x7d, 0x60, 0x5e, 0x13, 0, 0x41};
    for (int round = 0; round < 30000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        int32_t bits = kBits[rng() % (sizeof(kBits) / 4)];
        const int32_t kind = kKinds[rng() % (sizeof(kKinds) / 4)];
        // kinds 0x16 / 0x1e on a tiled platform store 4 bytes per pixel whatever the depth: with fewer bits the original writes past the end
        if ((kind == 0x16 || kind == 0x1e) && bits <= 0x18) bits = 0x20;
        int32_t width = static_cast<int32_t>(Pick(rng, {0, 1, 2, 3, 4, 8, 16, 32, 5, 33, rng() % 70}));
        int32_t height = static_cast<int32_t>(Pick(rng, {0, 1, 2, 3, 4, 8, 16, 32, 5, 33, rng() % 70}));
        if (NeedsPowerOfTwo(platform)) {
            width = 1 << (rng() % 7);
            height = 1 << (rng() % 7);
        }
        // the row stride covers the pixels written (a smaller one makes the original write past its buffer: kept out)
        const int32_t bytesPerPixel = bits <= 8 ? 1 : bits <= 0x10 ? 2 : bits <= 0x18 ? 3 : bits <= 0x20 ? 4 : 0;
        const int32_t rowStride = width * bytesPerPixel + static_cast<int32_t>(rng() % 3 == 0 ? rng() % 9 : 0);
        const int32_t destPitch = static_cast<int32_t>(Pick(rng, {rowStride, width, width * 2, width * 4, rng() % 300}));
        Buf source(0x20000 + 4096);
        RandomFill(rng, source.data(), source.size());
        const int failAt = rng() % 10 == 0 ? 0 : -1;
        uint8_t *result[2];
        Buf content[2];
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(failAt);
            ClearStack();
            result[side] = side == 0 ? original(source.data(), destPitch, rowStride, bits, kind, platform, width, height)
                                     : Untile(source.data(), destPitch, rowStride, bits, kind, platform, width, height);
            logs[side] = gLog;
            if (result[side] != nullptr) logs[side].push_back(0x6000000u + Ordinal(result[side]));
            if (!gBlocks.empty() && gBlocks[0] != nullptr) content[side].assign(gBlocks[0] ? static_cast<uint8_t *>(gBlocks[0]) : nullptr,
                                                                                  static_cast<uint8_t *>(gBlocks[0]) + gSizes[0] + kSlack);
        }
        ++ctx.cases;
        if ((result[0] == nullptr) != (result[1] == nullptr)) ctx.Fail("round %d: original %s, ours %s", round, result[0] ? "a buffer" : "null", result[1] ? "a buffer" : "null");
        if (content[0] != content[1]) ctx.Fail("round %d (platform %d bits %d kind %02x %dx%d): the buffers differ", round, platform, bits, kind, width, height);
        CompareLogs(ctx, "Untile", round, logs[0], logs[1]);
    }
}

// 0x885add: the planes of a YUV image.
DIFF_TEST(GimexFshPixels_AssemblePlanes, 0x885add)
{
    using Fn = uint8_t *(__cdecl *)(const uint8_t *, int32_t, int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kAssemblePlanes);
    HeapFakes heap;
    std::mt19937 rng(0x885add);
    static const int32_t kKinds[] = {0x1c, 0x6c, 0x40, 0x7b, 0x02, 0x16, 0x79};
    for (int round = 0; round < 10000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        const int32_t kind = kKinds[rng() % (sizeof(kKinds) / 4)];
        int32_t width = static_cast<int32_t>(Pick(rng, {0, 2, 4, 8, 16, 32, 6, 10, 34, 1, 3, rng() % 50}));
        int32_t height = static_cast<int32_t>(Pick(rng, {0, 2, 4, 8, 16, 32, 6, 10, 34, 1, 3, rng() % 50}));
        if (NeedsPowerOfTwo(platform)) {
            width = 2 << (rng() % 6);
            height = 2 << (rng() % 6);
        }
        // the PS2 swizzle works in pages: the half-size planes of a smaller image make the original read through a null pointer
        if (platform == 6 || platform == 0x10) {
            width = 32 << (rng() % 3);
            height = 32 << (rng() % 3);
        }
        const int32_t pitch = static_cast<int32_t>(Pick(rng, {width, width * 4, width + 2, rng() % 200}));
        Buf source(0x20000 + 4096);
        RandomFill(rng, source.data(), source.size());
        int failAt = rng() % 6 == 0 ? static_cast<int>(rng() % 4) : -1;
        // the page swizzlers (6, 0x10) use their own allocations unchecked: a failure there crashes the original (kept out)
        if ((platform == 6 || platform == 0x10) && failAt > 0) failAt = 0;
        uint8_t *result[2];
        Buf content[2];
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(failAt);
            ClearStack();
            result[side] = side == 0 ? original(source.data(), pitch, kind, platform, width, height)
                                     : AssemblePlanes(source.data(), pitch, kind, platform, width, height);
            logs[side] = gLog;
            if (result[side] != nullptr) {
                const uint32_t ordinal = Ordinal(result[side]);
                logs[side].push_back(0x6000000u + ordinal);
                if (ordinal != 0) content[side].assign(result[side], result[side] + gSizes[ordinal - 1] + kSlack);
            }
        }
        ++ctx.cases;
        if ((result[0] == nullptr) != (result[1] == nullptr)) ctx.Fail("round %d: original %s, ours %s", round, result[0] ? "planes" : "null", result[1] ? "planes" : "null");
        if (content[0] != content[1]) ctx.Fail("round %d (platform %d kind %02x %dx%d pitch %d): the planes differ", round, platform, kind, width, height, pitch);
        CompareLogs(ctx, "AssemblePlanes", round, logs[0], logs[1]);
    }
}

// 0x88855e: one BGRA pixel from Y, U, V.
DIFF_TEST(GimexFshPixels_YuvPixel, 0x88855e)
{
    using Fn = void(__cdecl *)(uint8_t *, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kYuvPixel);
    std::mt19937 rng(0x88855e);
    for (int round = 0; round < 60000; ++round) {
        int32_t luma = static_cast<int32_t>(round < 256 * 4 ? round % 256 : static_cast<int32_t>(rng() % 700) - 220);
        int32_t u = static_cast<int32_t>(round % 3 ? rng() % 256 : static_cast<int32_t>(rng() % 700) - 220);
        int32_t v = static_cast<int32_t>(round % 3 ? rng() % 256 : static_cast<int32_t>(rng() % 700) - 220);
        if (round % 97 == 0) luma = static_cast<int32_t>(rng());
        Twin dest(rng, 16);
        ++ctx.cases;
        ClearStack();
        original(dest.a.data() + 4, luma, u, v);
        ClearStack();
        YuvPixel(dest.b.data() + 4, luma, u, v);
        CompareBytes(ctx, "YuvPixel", round, dest.a.data(), dest.b.data(), 16);
    }
}

// 0x8885a1: red, green and blue from Y, U, V (floats).
DIFF_TEST(GimexFshPixels_YuvFloats, 0x8885a1)
{
    using Fn = void(__cdecl *)(float *, float *, float *, float, float, float);
    const Fn original = ctx.Original<Fn>(kYuvFloats);
    std::mt19937 rng(0x8885a1);
    for (int round = 0; round < 100000; ++round) {
        float in[3];
        for (float &f : in) {
            if (round % 4 == 0) {
                const uint32_t bits = rng();
                std::memcpy(&f, &bits, 4);
            } else {
                f = static_cast<float>(static_cast<int32_t>(rng() % 70000) - 20000) / 100.0f;
            }
        }
        uint32_t a[3] = {0x5a5a5a5a, 0x5a5a5a5a, 0x5a5a5a5a}, b[3] = {0x5a5a5a5a, 0x5a5a5a5a, 0x5a5a5a5a};
        ++ctx.cases;
        ClearStack();
        original(reinterpret_cast<float *>(&a[0]), reinterpret_cast<float *>(&a[1]), reinterpret_cast<float *>(&a[2]), in[0], in[1], in[2]);
        ClearStack();
        YuvToRgbFloat( // MV's 0x8885a1 (MX's duplicate removed at merge)
            reinterpret_cast<float *>(&b[0]), reinterpret_cast<float *>(&b[1]), reinterpret_cast<float *>(&b[2]), in[0], in[1], in[2]);
        const bool nanA = (a[0] & 0x7f800000) == 0x7f800000 && (a[0] & 0x7fffff);
        if (std::memcmp(a, b, sizeof(a)) != 0 && !nanA)
            ctx.Fail("y %g u %g v %g: original %08x %08x %08x, ours %08x %08x %08x", in[0], in[1], in[2], a[0], a[1], a[2], b[0], b[1], b[2]);
    }
}

// 0x888623: YUV 4:2:0 planes to BGRA rows.
DIFF_TEST(GimexFshPixels_YuvToRgba, 0x888623)
{
    using Fn = void(__cdecl *)(uint8_t *, int32_t, const uint8_t *, int32_t, int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kYuvToRgba);
    HeapFakes heap;
    std::mt19937 rng(0x888623);
    static const int32_t kKinds[] = {0x1c, 0x6c, 0x40, 0x7b, 0x02, 0x16, 0x79};
    for (int round = 0; round < 6000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        const int32_t kind = kKinds[rng() % (sizeof(kKinds) / 4)];
        int32_t width = static_cast<int32_t>(round < 200 ? round % 20 : Pick(rng, {0, 2, 4, 8, 16, 6, 10, 1, 3, 5, rng() % 40}));
        int32_t height = static_cast<int32_t>(round < 200 ? round / 20 % 10 : Pick(rng, {0, 2, 4, 8, 16, 6, 10, 1, 3, 5, rng() % 40}));
        const int32_t untile = static_cast<int32_t>(rng() % 3 == 0 ? 1 : 0);
        if (untile && NeedsPowerOfTwo(platform)) {
            width = 2 << (rng() % 5);
            height = 2 << (rng() % 5);
        }
        if (untile && (platform == 6 || platform == 0x10)) {
            width = 32 << (rng() % 2);
            height = 32 << (rng() % 2);
        }
        const int32_t pitch = static_cast<int32_t>(width * 4 + (rng() % 3 ? 0 : rng() % 6 * 4));
        Buf planes(0x20000 + 4096);
        RandomFill(rng, planes.data(), planes.size());
        Twin dest(rng, static_cast<size_t>(std::max(pitch, width * 4) * (height + 2) + width * 8 + 512));
        int failAt = untile && rng() % 6 == 0 ? static_cast<int>(rng() % 4) : -1;
        // the page swizzlers (6, 0x10) use their own allocations unchecked: a failure there crashes the original (kept out)
        if ((platform == 6 || platform == 0x10) && failAt > 0) failAt = 0;
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(failAt);
            ClearStack();
            if (side == 0) original(dest.a.data(), pitch, planes.data(), width, height, kind, platform, untile);
            else YuvToRgba(dest.b.data(), pitch, planes.data(), width, height, kind, platform, untile);
            logs[side] = gLog;
            for (void *block : gBlocks) if (block != nullptr) logs[side].push_back(0x7000000u);
        }
        ++ctx.cases;
        CompareBytes(ctx, "YuvToRgba", round, dest.a.data(), dest.b.data(), dest.a.size());
        CompareLogs(ctx, "YuvToRgba", round, logs[0], logs[1]);
    }
}

// 0x888826: packed 4:2:2 pairs to BGRA.
DIFF_TEST(GimexFshPixels_YuvPairs, 0x888826)
{
    using Fn = void(__cdecl *)(uint8_t *, int32_t, const uint8_t *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kYuvPairs);
    std::mt19937 rng(0x888826);
    for (int round = 0; round < 4000; ++round) {
        const int32_t width = static_cast<int32_t>(round < 200 ? round % 20 : rng() % 40);
        const int32_t rows = static_cast<int32_t>(Pick(rng, {0, 1, 2, 3, 0xffffffff, rng() % 12}));
        Buf source(0x4000);
        RandomFill(rng, source.data(), source.size());
        Twin dest(rng, static_cast<size_t>(width * 4 * (rows > 0 && rows < 100 ? rows : 1) + 128));
        const int32_t unused = static_cast<int32_t>(rng());
        ++ctx.cases;
        ClearStack();
        original(dest.a.data(), unused, source.data(), width, rows);
        ClearStack();
        YuvPairs(dest.b.data(), unused, source.data(), width, rows);
        CompareBytes(ctx, "YuvPairs", round, dest.a.data(), dest.b.data(), dest.a.size());
    }
}

// 0x887507: one pixel of every record type.
DIFF_TEST(GimexFshPixels_UnpackPixel, 0x887507)
{
    using Fn = void(__cdecl *)(uint32_t, int32_t *, int32_t *, uint8_t *, const uint8_t *, uint32_t *);
    const Fn original = ctx.Original<Fn>(kUnpackPixel);
    std::mt19937 rng(0x887507);
    for (int round = 0; round < 60000; ++round) {
        const uint32_t type = round < 256 * 40 ? static_cast<uint32_t>(round % 256) : (rng() & 0xff) | (rng() % 16 == 0 ? rng() & 0xffffff00 : 0);
        Buf source(64);
        RandomFill(rng, source.data(), source.size());
        if (round % 5 == 0) std::memset(source.data(), round % 2 ? 0xff : 0, 8);
        Twin dest(rng, 96);
        int32_t destOffset[2], sourceOffset[2];
        uint32_t column[2];
        destOffset[0] = destOffset[1] = static_cast<int32_t>(rng() % 40);
        sourceOffset[0] = sourceOffset[1] = static_cast<int32_t>(rng() % 40);
        column[0] = column[1] = rng() % 20;
        ++ctx.cases;
        ClearStack();
        original(type, &destOffset[0], &sourceOffset[0], dest.a.data(), source.data(), &column[0]);
        ClearStack();
        UnpackPixel(type, &destOffset[1], &sourceOffset[1], dest.b.data(), source.data(), &column[1]);
        if (destOffset[0] != destOffset[1] || sourceOffset[0] != sourceOffset[1] || column[0] != column[1])
            ctx.Fail("type %02x column %u: offsets original %d / %d (col %u), ours %d / %d (col %u)", type, column[0], destOffset[0],
                     sourceOffset[0], column[0], destOffset[1], sourceOffset[1], column[1]);
        CompareBytes(ctx, "UnpackPixel", round, dest.a.data(), dest.b.data(), dest.a.size());
    }
}

// 0x886e83: the shape decoder over records of every type.
DIFF_TEST(GimexFshPixels_DecodeShape, 0x886e83)
{
    using Fn = int32_t(__cdecl *)(uint8_t *, uint8_t *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kDecodeShape);
    HeapFakes heap;
    shape::CodePatch gmOriginal{kDecodeGm, reinterpret_cast<const void *>(&FakeDecodeGm)};
    shape::CodePatch gmOurs{reinterpret_cast<void *>(&DecodeGm), reinterpret_cast<const void *>(&FakeDecodeGm)};
    std::mt19937 rng(0x886e83);
    int gmRounds = 0, blockRounds = 0, yuvRounds = 0, pairRounds = 0, tiledRounds = 0;
    for (int round = 0; round < 40000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        const bool big = platform == 5 || platform == 7 || platform == 0xf || platform == 0x11;
        int type = round < 256 * 20 ? round % 256 : static_cast<int>(rng() & 0xff);
        {
            // A type the unpacker has no case for never advances the original's pixel loop (it hangs): kept out, as a known type
            // in its place. (The unpacker's own test takes every type.)
            static const uint8_t kKnown[] = {0x01, 0x10, 0x18, 0x58, 0x7a, 0x02, 0x11, 0x12, 0x19, 0x41, 0x59, 0x64, 0x7b, 0x03, 0x04, 0x43,
                                             0x5d, 0x05, 0x13, 0x14, 0x5b, 0x15, 0x16, 0x1e, 0x5e, 0x42, 0x44, 0x5a, 0x5c, 0x60, 0x61, 0x62,
                                             0x7d, 0x65, 0x66, 0x67, 0x6a, 0x6d, 0x78, 0x7e, 0x7f, 0x40, 0x79, 0x1c, 0x6c, 0x68, 0x63};
            bool known = false;
            for (uint8_t k : kKnown) known |= k == (type & 0x7f);
            if (!known) type = (type & 0x80) | kKnown[static_cast<size_t>(type) % sizeof(kKnown)];
        }
        Buf record = MakeRecord(rng, type, big ? rng() % 8 != 0 : rng() % 8 == 0, 0x6000 + 4096);
        uint32_t width = Pick(rng, {1, 2, 3, 4, 5, 7, 8, 9, 16, 17, 32, 6, 10, rng() % 36});
        uint32_t height = Pick(rng, {1, 2, 3, 4, 5, 7, 8, 9, 16, 17, 32, 6, 10, rng() % 36});
        Put(&record[8], rng() % 8 ? 0x20 : rng() % 0x40, 4, big);
        // the attribute word: coded (2) and tiled (0x4000) now and then, never the face bit's concern here
        {
            uint32_t attr = 0;
            if (rng() % 5 == 0) attr |= 2;
            if (rng() % 4 == 0) attr |= 0x4000;
            Put(&record[1], attr, 3, big);
            // the untiler's interleaved layout overruns its buffer on sides that are not powers of two (kept out)
            if ((attr & 0x4000) != 0 && NeedsPowerOfTwo(platform)) {
                width = 1u << (rng() % 6);
                height = 1u << (rng() % 6);
            }
        }
        Put(&record[0x18], width, 4, big);
        Put(&record[0x1c], height, 4, big);
        const int codec = static_cast<int>(rng() % 6);
        {
            // pick the magic at +0x20 (the pixels' first bytes) when the record is coded
            const uint32_t word = gimex::fsh::ReadWord(record.data() + 1, 3, platform);
            if ((word & 2) != 0) {
                static const uint8_t kBlock[][2] = {{0x18, 0xfb}, {0x1a, 0xfb}, {0x1c, 0xfb}, {0x28, 0xfb}};
                if (codec < 3) {
                    const uint8_t *m = kBlock[rng() % 4];
                    record[0x20] = m[0];
                    record[0x21] = m[1];
                    if (m[0] != 0) { /* a block header: dimensions in blocks as the decoder wrote them */
                        record[0x25] = 0;
                        // the dimensions the decoder will round to: 8 for the 0x28 header, else 4 (a larger header would overrun its buffer)
                        const uint32_t round = m[0] == 0x28 ? 7 : 3;
                        record[0x26] = static_cast<uint8_t>(((width + round) & ~round) >> 2);
                        record[0x27] = static_cast<uint8_t>(((height + round) & ~round) >> 2);
                    }
                } else if (codec == 3) {
                    record[0x20] = 'G';
                    record[0x21] = 'M';
                } else {
                    // RefPack or nothing: the shape decoder reads these pixels plainly. (The 0x80 block headers carry a 4-byte size and
                    // other dimension bytes; random ones overrun the decoder's buffer in the original, so none is drawn.)
                    record[0x20] = codec == 4 ? 0x10 : 0x00;
                    record[0x21] = codec == 4 ? 0xfb : 0x00;
                }
            }
        }
        {
            // the pixels start where the offset word says; the codec test reads +0x20, the block decoder the pixels' own first bytes
            uint32_t off8 = ReadWord(record.data() + 8, 4, platform);
            if (off8 < 0x20 && (ReadWord(record.data() + 1, 3, platform) & 2) != 0) {
                off8 = 0x20; // pixels inside the header would overwrite the dimensions with the codec's header
                Put(&record[8], off8, 4, big);
            }
            if (off8 > 0x20 && off8 < 0x100) {
                uint8_t header[8];
                std::memcpy(header, &record[0x20], 8);
                std::memcpy(&record[off8], header, 8);
            }
        }
        const int32_t pitch = static_cast<int32_t>(width * 4 + (rng() % 3 ? 0 : rng() % 6 * 4) + (rng() % 20 == 0 ? 64 : 0));
        Twin dest(rng, static_cast<size_t>(std::max<int32_t>(pitch, static_cast<int32_t>(width) * 4) * (height + 20) + 1024));
        Twin recordCopy(rng, 0);
        recordCopy.a = record;
        recordCopy.b = record;
        int failAt = rng() % 8 == 0 ? static_cast<int>(rng() % 4) : -1;
        // the page swizzlers (6, 0x10) use their own allocations unchecked: a failure there crashes the original (kept out)
        if ((platform == 6 || platform == 0x10) && failAt > 0) failAt = 0;
        // the block and Gm paths use their buffers unchecked (the copy, the decoded rows): a failed allocation crashes the original (kept out)
        if ((gimex::fsh::ReadWord(record.data() + 1, 3, platform) & 2) != 0 || (type & 0x7f) == 0x60 || (type & 0x7f) == 0x61 ||
            (type & 0x7f) == 0x62 || (type & 0x7f) == 0x1e)
            failAt = -1;
        int32_t result[2];
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(failAt);
            ClearStack();
            result[side] = side == 0 ? original(recordCopy.a.data(), dest.a.data(), pitch, platform)
                                     : DecodeShape(recordCopy.b.data(), dest.b.data(), pitch, platform);
            logs[side] = gLog;
            for (void *block : gBlocks) if (block != nullptr) logs[side].push_back(0x7000000u);
        }
        ++ctx.cases;
        if (result[0] != result[1]) ctx.Fail("round %d type %02x platform %d: original %d, ours %d", round, type, platform, result[0], result[1]);
        if (!CompareBytes(ctx, "DecodeShape dest", round, dest.a.data(), dest.b.data(), dest.a.size()))
            ctx.Fail("  (type %02x platform %d %ux%u pitch %d attr %02x%02x%02x)", type, platform, width, height, pitch, record[1], record[2], record[3]);
        CompareBytes(ctx, "DecodeShape record", round, recordCopy.a.data(), recordCopy.b.data(), recordCopy.a.size());
        CompareLogs(ctx, "DecodeShape", round, logs[0], logs[1]);
        for (uint32_t w : logs[0]) { if (w == 0x5000000u) ++gmRounds; }
        const uint32_t attribute = gimex::fsh::ReadWord(record.data() + 1, 3, platform);
        const uint8_t first = record[0];
        if (first == 0x1c || first == 0x6c) ++yuvRounds;
        else if (first == 0x68 || first == 0x63) ++pairRounds;
        else if ((attribute & 2) != 0 && (record[0x20] == 0x18 || record[0x20] == 0x1a || record[0x20] == 0x1c || record[0x20] == 0x28)) ++blockRounds;
        else if ((attribute & 0x4000) != 0) ++tiledRounds;
    }
    // the branches must have run, or a passing test says nothing about them
    if (gmRounds < 50 || blockRounds < 100 || yuvRounds < 100 || pairRounds < 100 || tiledRounds < 100)
        ctx.Fail("branches run too rarely: Gm %d, block %d, YUV %d, pairs %d, tiled %d", gmRounds, blockRounds, yuvRounds, pairRounds, tiledRounds);
}
