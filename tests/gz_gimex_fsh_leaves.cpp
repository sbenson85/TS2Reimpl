// Differential tests for FSH's back-end leaves (lane MQ; src/gz/GimexFshLeaves.size.cpp and GimexFshColour.size.cpp,
// 0x88374d..0x885849): the page swizzles, the words by platform, the colour converters, tiled addressing and the
// record-type pickers. Every function runs alone over seeded synthetic inputs, the original (by RVA, in the mapped
// image) against ours:
// - word -> colour converters: all 65,536 sixteen-bit words, then random 32-bit words and the edges;
// - colour -> word packers: every value of each channel against random others, random pixels, and the edge values of every
//   channel together; the luma functions (floating point) over all 16.7 million B, G, R values;
// - the swizzles: the block permutations on random buffers, the page functions on random pages (the callers' 128 x 128 and
//   128 x 64 and smaller ones), the whole-image converters over random sizes (1..300, 0, page edges) with the source and
//   destination padded and filled alike; compared: the result, every byte of the destination and its margin, and the
//   allocator's effect (blocks left, frees);
// - the arithmetic (tiled offsets, sizes, interleave, palette order, record types): exhaustive over the small range, then
//   random 32-bit values.
// Returns narrower than eax (a 16-bit packer's ax, a byte's al, a bool) are read through a function pointer of that type on
// both sides, so only the register the callers read is compared.
//
// Kept out: negative image sizes (memcpy of a negative count); sizes whose pages the destination can't hold (the tests pad
// generously and compare the margin); table indices past the 32 entries of the block-order tables (only blocks the callers'
// 128-wide pages reach are tested, and the tables are the image's on both sides).
#define NOMINMAX
#include "difftest.h"
#include "free_recorder.h"
#include "gz/GimexFshLeaves.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

using namespace gimex::fsh;

constexpr uint32_t kSwizzle16Offset = 0x88374d;
constexpr uint32_t kUnswizzleNibbles = 0x8837a5;
constexpr uint32_t kUnswizzleBytes = 0x883851;
constexpr uint32_t kUnswizzlePage4 = 0x883876;
constexpr uint32_t kUnswizzlePage8 = 0x883a33;
constexpr uint32_t kSwizzleNibbles = 0x883bf0;
constexpr uint32_t kSwizzleBytes = 0x883c9c;
constexpr uint32_t kSwizzlePage4 = 0x883cf7;
constexpr uint32_t kSwizzlePage8 = 0x883eb9;
constexpr uint32_t kReadWord = 0x88407b;
constexpr uint32_t kPutLittleEndian = 0x8840a3;
constexpr uint32_t kPutBigEndian = 0x8840f5;
constexpr uint32_t kPutWord = 0x884167;
constexpr uint32_t kIsBigEndianPlatform = 0x884193;
constexpr uint32_t kColour2101010 = 0x8841b0;
constexpr uint32_t kColour4444 = 0x88421e;
constexpr uint32_t kColour565 = 0x884285;
constexpr uint32_t kColour1555 = 0x8842df;
constexpr uint32_t kColour655 = 0x88433b;
constexpr uint32_t kColour6666 = 0x884393;
constexpr uint32_t kPack2101010 = 0x8843fa;
constexpr uint32_t kPack4444 = 0x88445c;
constexpr uint32_t kPack1555 = 0x8844da;
constexpr uint32_t kPack565 = 0x884519;
constexpr uint32_t kPack655 = 0x884546;
constexpr uint32_t kPack6666 = 0x884573;
constexpr uint32_t kTileOffset16 = 0x8845f1;
constexpr uint32_t kTileOffset32 = 0x884623;
constexpr uint32_t kTileOffset4 = 0x884655;
constexpr uint32_t kTileOffset8 = 0x884687;
constexpr uint32_t kColour3555 = 0x8846b7;
constexpr uint32_t kColour88 = 0x884767;
constexpr uint32_t kPackGrey44 = 0x884781;
constexpr uint32_t kPack3555 = 0x8847eb;
constexpr uint32_t kLuma = 0x88486e;
constexpr uint32_t kPackGrey88 = 0x8848ce;
constexpr uint32_t kIsGreyRamp = 0x884938;
constexpr uint32_t kPack556 = 0x884989;
constexpr uint32_t kColour1555Alt = 0x8849c7;
constexpr uint32_t kPack1555Alt = 0x884a34;
constexpr uint32_t kPaddedSize = 0x884a7e;
constexpr uint32_t kPageSize = 0x884ade;
constexpr uint32_t kSwizzle16 = 0x884b24;
constexpr uint32_t kSwizzle4 = 0x884b8b;
constexpr uint32_t kSwizzle8 = 0x884d93;
constexpr uint32_t kUnswizzle16 = 0x884fa8;
constexpr uint32_t kUnswizzle4 = 0x88500f;
constexpr uint32_t kUnswizzle8 = 0x88521a;
constexpr uint32_t kPaletteOrder = 0x885429;
constexpr uint32_t kTileOffset = 0x885450;
constexpr uint32_t kInterleavedIndex = 0x8854b6;
constexpr uint32_t kColour1555Swizzled = 0x88551d;
constexpr uint32_t kPack1555Swapped = 0x885579;
constexpr uint32_t kPaletteTypeA = 0x8855c0;
constexpr uint32_t kPaletteTypeB = 0x88563c;
constexpr uint32_t kPaletteTypeF = 0x88568b;
constexpr uint32_t kPaletteTypeC = 0x8856d9;
constexpr uint32_t kIsGreyImage = 0x88570d;
constexpr uint32_t kIsMipmapName = 0x88578d;
constexpr uint32_t kPaletteTypeD = 0x885801;
constexpr uint32_t kPaletteTypeE = 0x885825;

// ---- inputs ----

// A fast word generator (xorshift): fills big buffers without the cost of mt19937.
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
    void Bytes(std::vector<uint8_t> &bytes)
    {
        for (size_t i = 0; i < bytes.size(); i += 4) {
            const uint32_t word = Next();
            for (size_t k = 0; k < 4 && i + k < bytes.size(); ++k) bytes[i + k] = static_cast<uint8_t>(word >> (8 * k));
        }
    }
};

// The edge values of a channel: around every threshold the packers use.
const uint8_t kEdges[] = {0,    1,    2,    3,    7,    8,    0x1f, 0x3e, 0x3f, 0x40, 0x7e, 0x7f, 0x80,
                          0x81, 0xbe, 0xbf, 0xc0, 0xeb, 0xec, 0xed, 0xee, 0xf7, 0xf8, 0xfe, 0xff};

// ---- word -> word ----

using WordFn = uint32_t(__cdecl *)(uint32_t);

void WordCases(difftest::Context &ctx, uint32_t rva, WordFn ours, uint32_t seed)
{
    const WordFn original = ctx.Original<WordFn>(rva);
    auto check = [&](uint32_t word) {
        ++ctx.cases;
        const uint32_t a = original(word);
        const uint32_t b = ours(word);
        if (a != b) ctx.Fail("word %08x: original %08x, ours %08x", word, a, b);
    };
    for (uint32_t word = 0; word < 0x10000; ++word) check(word);
    std::mt19937 rng(seed);
    for (int i = 0; i < 200000; ++i) check(rng());
    for (uint32_t bit = 0; bit < 32; ++bit) {
        check(1u << bit);
        check(~(1u << bit));
        check(0xffffffffu >> bit);
        check(0xffffffffu << bit);
    }
}

// ---- B, G, R, A bytes -> word ----

template <typename R> void PixelCases(difftest::Context &ctx, uint32_t rva, R(__cdecl *ours)(const uint8_t *), uint32_t seed)
{
    using Fn = R(__cdecl *)(const uint8_t *);
    const Fn original = ctx.Original<Fn>(rva);
    alignas(4) uint8_t pixel[8] = {0, 0, 0, 0, 0xa5, 0xa5, 0xa5, 0xa5};
    auto check = [&]() {
        ++ctx.cases;
        const R a = original(pixel);
        const R b = ours(pixel);
        if (a != b) ctx.Fail("pixel %02x %02x %02x %02x: original %x, ours %x", pixel[0], pixel[1], pixel[2], pixel[3],
                             static_cast<unsigned>(a), static_cast<unsigned>(b));
    };
    std::mt19937 rng(seed);
    // every value of each channel against random others
    for (int channel = 0; channel < 4; ++channel) {
        for (int value = 0; value < 256; ++value) {
            for (int other = 0; other < 48; ++other) {
                for (int c = 0; c < 4; ++c) pixel[c] = static_cast<uint8_t>(rng());
                pixel[channel] = static_cast<uint8_t>(value);
                check();
            }
        }
    }
    // the edges of every channel together
    for (const uint8_t b : kEdges)
        for (const uint8_t g : kEdges)
            for (const uint8_t r : kEdges)
                for (const uint8_t a : kEdges) {
                    pixel[0] = b;
                    pixel[1] = g;
                    pixel[2] = r;
                    pixel[3] = a;
                    check();
                }
    for (int i = 0; i < 200000; ++i) {
        const uint32_t word = rng();
        std::memcpy(pixel, &word, 4);
        check();
    }
}

// The floating-point luma functions: all B, G, R values, the alpha cycling through its edges.
template <typename R> void LumaCases(difftest::Context &ctx, uint32_t rva, R(__cdecl *ours)(const uint8_t *))
{
    using Fn = R(__cdecl *)(const uint8_t *);
    const Fn original = ctx.Original<Fn>(rva);
    alignas(4) uint8_t pixel[4];
    uint32_t n = 0;
    for (uint32_t bgr = 0; bgr < 0x1000000; ++bgr) {
        pixel[0] = static_cast<uint8_t>(bgr);
        pixel[1] = static_cast<uint8_t>(bgr >> 8);
        pixel[2] = static_cast<uint8_t>(bgr >> 16);
        pixel[3] = kEdges[n++ % sizeof(kEdges)];
        ++ctx.cases;
        const R a = original(pixel);
        const R b = ours(pixel);
        if (a != b) {
            ctx.Fail("pixel %02x %02x %02x %02x: original %x, ours %x", pixel[0], pixel[1], pixel[2], pixel[3],
                     static_cast<unsigned>(a), static_cast<unsigned>(b));
            return;
        }
    }
}

// ---- (x, y, width) -> offset ----

using TileFn = int32_t(__cdecl *)(int32_t, int32_t, int32_t);

void TileCases(difftest::Context &ctx, uint32_t rva, TileFn ours, uint32_t seed)
{
    const TileFn original = ctx.Original<TileFn>(rva);
    auto check = [&](int32_t x, int32_t y, int32_t width) {
        ++ctx.cases;
        const int32_t a = original(x, y, width);
        const int32_t b = ours(x, y, width);
        if (a != b) ctx.Fail("x %d y %d width %d: original %d, ours %d", x, y, width, a, b);
    };
    for (int32_t width = -2; width < 70; ++width)
        for (int32_t y = -3; y < 40; ++y)
            for (int32_t x = -3; x < 40; ++x) check(x, y, width);
    std::mt19937 rng(seed);
    for (int i = 0; i < 100000; ++i) check(static_cast<int32_t>(rng() % 5000), static_cast<int32_t>(rng() % 5000),
                                           static_cast<int32_t>(rng() % 5000));
    for (int i = 0; i < 20000; ++i) check(static_cast<int32_t>(rng()), static_cast<int32_t>(rng()), static_cast<int32_t>(rng()));
}

// ---- (alpha, red, green, blue) -> type ----

using TypeFn = int32_t(__cdecl *)(int32_t, int32_t, int32_t, int32_t);

void TypeCases(difftest::Context &ctx, uint32_t rva, TypeFn ours)
{
    const TypeFn original = ctx.Original<TypeFn>(rva);
    for (int32_t a = -2; a <= 12; ++a)
        for (int32_t r = -2; r <= 12; ++r)
            for (int32_t g = -2; g <= 12; ++g)
                for (int32_t b = -2; b <= 12; ++b) {
                    ++ctx.cases;
                    const int32_t x = original(a, r, g, b);
                    const int32_t y = ours(a, r, g, b);
                    if (x != y) ctx.Fail("bits %d %d %d %d: original %d, ours %d", a, r, g, b, x, y);
                }
}

// ---- block permutations ----

using BlockFn = int32_t(__cdecl *)(const uint8_t *, uint8_t *);

void BlockCases(difftest::Context &ctx, uint32_t rva, BlockFn ours, uint32_t seed)
{
    const BlockFn original = ctx.Original<BlockFn>(rva);
    Fill fill(seed);
    // The permutation reads 0x100 bytes and writes 0x100; the margin after the output catches a longer write.
    std::vector<uint8_t> in(0x100), outA(0x180), outB(0x180);
    for (int i = 0; i < 400; ++i) {
        fill.Bytes(in);
        if (i == 0) std::memset(in.data(), 0, in.size());
        if (i == 1) std::memset(in.data(), 0xff, in.size());
        if (i == 2) for (size_t k = 0; k < in.size(); ++k) in[k] = static_cast<uint8_t>(k);
        fill.Bytes(outA);
        outB = outA;
        ++ctx.cases;
        const int32_t a = original(in.data(), outA.data());
        const int32_t b = ours(in.data(), outB.data());
        if (a != b || outA != outB) ctx.Fail("case %d: original returns %d, ours %d; outputs %s", i, a, b, outA == outB ? "equal" : "differ");
    }
}

// ---- pages ----

using PageFn = int32_t(__cdecl *)(int32_t, int32_t, const void *, void *);

void PageCases(difftest::Context &ctx, uint32_t rva, PageFn ours, uint32_t seed, int32_t fullHeight)
{
    const PageFn original = ctx.Original<PageFn>(rva);
    Fill fill(seed);
    constexpr size_t kPageBytes = 0x2000;
    const int32_t sizes[][2] = {{0x80, fullHeight}, {0x80, fullHeight}, {0x40, fullHeight}, {0x80, 0x20}, {0x20, 0x10}, {0x80, 0}, {0, 0x40}};
    std::vector<uint8_t> source(kPageBytes + 0x200), destA(kPageBytes + 0x400), destB;
    for (int i = 0; i < 60; ++i) {
        const int32_t width = sizes[i % 7][0];
        const int32_t height = sizes[i % 7][1];
        fill.Bytes(source);
        fill.Bytes(destA);
        destB = destA;
        long long blocks[2];
        size_t frees[2];
        int32_t result[2];
        for (int side = 0; side < 2; ++side) {
            difftest::AllocationCounter counter;
            difftest::FreeRecorder recorder;
            std::vector<uint8_t> &dest = side == 0 ? destA : destB;
            result[side] = side == 0 ? original(width, height, source.data(), dest.data())
                                     : ours(width, height, source.data(), dest.data());
            recorder.Stop();
            counter.Stop();
            blocks[side] = counter.Blocks();
            frees[side] = recorder.Count();
        }
        ++ctx.cases;
        if (result[0] != result[1] || destA != destB || blocks[0] != blocks[1] || frees[0] != frees[1])
            ctx.Fail("case %d (%d x %d): original %d (%lld blocks, %zu frees), ours %d (%lld blocks, %zu frees), pages %s", i, width,
                     height, result[0], blocks[0], frees[0], result[1], blocks[1], frees[1], destA == destB ? "equal" : "differ");
    }
}

// ---- whole images ----

using ImageFn = int32_t(__cdecl *)(const uint8_t *, uint8_t *, int32_t, int32_t);

void ImageCases(difftest::Context &ctx, uint32_t rva, ImageFn ours, uint32_t seed, size_t sourceScale, size_t destScale, int cases)
{
    const ImageFn original = ctx.Original<ImageFn>(rva);
    Fill fill(seed);
    std::mt19937 rng(seed);
    const int32_t pool[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 48, 63, 64, 65, 100, 127, 128, 129, 130, 191, 192, 200, 255, 256, 257, 300};
    for (int i = 0; i < cases; ++i) {
        int32_t width = pool[rng() % (sizeof(pool) / sizeof(pool[0]))];
        int32_t height = pool[rng() % (sizeof(pool) / sizeof(pool[0]))];
        if (i % 5 == 4) {
            width = static_cast<int32_t>(rng() % 301);
            height = static_cast<int32_t>(rng() % 301);
        }
        std::vector<uint8_t> source(sourceScale * (static_cast<size_t>(width) + 260) * (static_cast<size_t>(height) + 260));
        std::vector<uint8_t> destA(destScale * (static_cast<size_t>(width) + 260) * (static_cast<size_t>(height) + 260));
        fill.Bytes(source);
        fill.Bytes(destA);
        std::vector<uint8_t> destB = destA;
        long long blocks[2];
        size_t frees[2];
        int32_t result[2];
        for (int side = 0; side < 2; ++side) {
            difftest::AllocationCounter counter;
            difftest::FreeRecorder recorder;
            std::vector<uint8_t> &dest = side == 0 ? destA : destB;
            result[side] = side == 0 ? original(source.data(), dest.data(), width, height)
                                     : ours(source.data(), dest.data(), width, height);
            recorder.Stop();
            counter.Stop();
            blocks[side] = counter.Blocks();
            frees[side] = recorder.Count();
        }
        ++ctx.cases;
        if (result[0] != result[1] || destA != destB || blocks[0] != blocks[1] || frees[0] != frees[1]) {
            size_t first = 0;
            while (first < destA.size() && destA[first] == destB[first]) ++first;
            ctx.Fail("case %d (%d x %d): original %d (%lld blocks, %zu frees), ours %d (%lld blocks, %zu frees); destinations %s (first byte %zu)",
                     i, width, height, result[0], blocks[0], frees[0], result[1], blocks[1], frees[1], destA == destB ? "equal" : "differ", first);
        }
    }
}

} // namespace

// 0x88374d: the bit offset of pixel (x, y) in a swizzled 16-bit image (width over 15 swizzles).
DIFF_TEST(GimexFshLeaves_Swizzle16Offset, 0x88374d)
{
    using Fn = int32_t(__cdecl *)(uint32_t, uint32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kSwizzle16Offset);
    auto check = [&](uint32_t x, uint32_t y, int32_t width) {
        ++ctx.cases;
        const int32_t a = original(x, y, width);
        const int32_t b = Swizzle16Offset(x, y, width);
        if (a != b) ctx.Fail("x %u y %u width %d: original %d, ours %d", x, y, width, a, b);
    };
    for (int32_t width = -2; width < 80; ++width)
        for (uint32_t y = 0; y < 70; ++y)
            for (uint32_t x = 0; x < 70; ++x) check(x, y, width);
    std::mt19937 rng(0x88374d);
    for (int i = 0; i < 100000; ++i) check(rng() % 600, rng() % 600, static_cast<int32_t>(rng() % 600));
    for (int i = 0; i < 20000; ++i) check(rng(), rng(), static_cast<int32_t>(rng()));
}

// 0x8837a5 (swizzled -> linear nibbles) and 0x883bf0 (linear -> swizzled): the 0x100-byte block permutations.
DIFF_TEST(GimexFshLeaves_UnswizzleNibbles, 0x8837a5)
{
    BlockCases(ctx, kUnswizzleNibbles, &UnswizzleNibbles, 0x8837a5);
}

DIFF_TEST(GimexFshLeaves_SwizzleNibbles, 0x883bf0)
{
    BlockCases(ctx, kSwizzleNibbles, &SwizzleNibbles, 0x883bf0);
}

// 0x883851 and 0x883c9c: the byte block permutations.
DIFF_TEST(GimexFshLeaves_UnswizzleBytes, 0x883851)
{
    BlockCases(ctx, kUnswizzleBytes, &UnswizzleBytes, 0x883851);
}

DIFF_TEST(GimexFshLeaves_SwizzleBytes, 0x883c9c)
{
    BlockCases(ctx, kSwizzleBytes, &SwizzleBytes, 0x883c9c);
}

// 0x883876 / 0x883a33 / 0x883cf7 / 0x883eb9: one page.
DIFF_TEST(GimexFshLeaves_UnswizzlePage4, 0x883876)
{
    PageCases(ctx, kUnswizzlePage4, &UnswizzlePage4, 0x883876, 0x80);
}

DIFF_TEST(GimexFshLeaves_UnswizzlePage8, 0x883a33)
{
    PageCases(ctx, kUnswizzlePage8, &UnswizzlePage8, 0x883a33, 0x40);
}

DIFF_TEST(GimexFshLeaves_SwizzlePage4, 0x883cf7)
{
    PageCases(ctx, kSwizzlePage4, &SwizzlePage4, 0x883cf7, 0x80);
}

DIFF_TEST(GimexFshLeaves_SwizzlePage8, 0x883eb9)
{
    PageCases(ctx, kSwizzlePage8, &SwizzlePage8, 0x883eb9, 0x40);
}

// 0x884b24 / 0x884fa8: whole 16-bit images (the destination of the swizzle reaches past the image; padded and compared).
DIFF_TEST(GimexFshLeaves_Swizzle16, 0x884b24)
{
    ImageCases(ctx, kSwizzle16, &Swizzle16, 0x884b24, 2, 8, 120);
}

DIFF_TEST(GimexFshLeaves_Unswizzle16, 0x884fa8)
{
    ImageCases(ctx, kUnswizzle16, &Unswizzle16, 0x884fa8, 8, 2, 120);
}

// 0x884b8b / 0x884d93 / 0x88500f / 0x88521a: whole 4 and 8-bit images in pages.
DIFF_TEST(GimexFshLeaves_Swizzle4, 0x884b8b)
{
    ImageCases(ctx, kSwizzle4, &Swizzle4, 0x884b8b, 2, 4, 160);
}

DIFF_TEST(GimexFshLeaves_Swizzle8, 0x884d93)
{
    ImageCases(ctx, kSwizzle8, &Swizzle8, 0x884d93, 2, 4, 160);
}

DIFF_TEST(GimexFshLeaves_Unswizzle4, 0x88500f)
{
    ImageCases(ctx, kUnswizzle4, &Unswizzle4, 0x88500f, 4, 2, 160);
}

DIFF_TEST(GimexFshLeaves_Unswizzle8, 0x88521a)
{
    ImageCases(ctx, kUnswizzle8, &Unswizzle8, 0x88521a, 4, 2, 160);
}

// 0x884193: the platforms that read big-endian (only al is set).
DIFF_TEST(GimexFshLeaves_IsBigEndianPlatform, 0x884193)
{
    using Fn = bool(__cdecl *)(int32_t);
    const Fn original = ctx.Original<Fn>(kIsBigEndianPlatform);
    for (int32_t platform = -300; platform < 600; ++platform) {
        ++ctx.cases;
        if (original(platform) != IsBigEndianPlatform(platform)) ctx.Fail("platform %d differs", platform);
    }
    for (const int32_t platform : {0x7fffffff, static_cast<int32_t>(0x80000000), -1, 0x10005, 0x105, 0x107}) {
        ++ctx.cases;
        if (original(platform) != IsBigEndianPlatform(platform)) ctx.Fail("platform %d differs", platform);
    }
}

// 0x88407b: a number of `width` bytes, big or little-endian by platform.
DIFF_TEST(GimexFshLeaves_ReadWord, 0x88407b)
{
    using Fn = uint32_t(__cdecl *)(const void *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kReadWord);
    std::mt19937 rng(0x88407b);
    for (int i = 0; i < 20000; ++i) {
        uint8_t bytes[8];
        for (uint8_t &byte : bytes) byte = static_cast<uint8_t>(rng());
        const int32_t width = static_cast<int32_t>(rng() % 8) - 1;
        const int32_t platform = i % 3 == 0 ? static_cast<int32_t>(rng() % 24) : (i % 3 == 1 ? 2 : static_cast<int32_t>(rng() % 3 == 0 ? 0x11 : 5 + rng() % 3));
        ++ctx.cases;
        const uint32_t a = original(bytes, width, platform);
        const uint32_t b = ReadWord(bytes, width, platform);
        if (a != b) ctx.Fail("width %d platform %d: original %08x, ours %08x", width, platform, a, b);
    }
}

// 0x8840a3 and 0x8840f5 and 0x884167: store the low bytes of a value (a guard of 8 bytes shows any extra write).
template <typename Fn> void PutCases(difftest::Context &ctx, uint32_t rva, Fn ours, bool byPlatform, uint32_t seed)
{
    const Fn original = ctx.Original<Fn>(rva);
    std::mt19937 rng(seed);
    for (int i = 0; i < 20000; ++i) {
        uint8_t a[12], b[12];
        for (uint8_t &byte : a) byte = static_cast<uint8_t>(rng());
        std::memcpy(b, a, sizeof(a));
        const uint32_t value = rng();
        const int32_t width = static_cast<int32_t>(rng() % 8) - 1;
        const int32_t platform = static_cast<int32_t>(rng() % 24);
        ++ctx.cases;
        if (byPlatform) {
            reinterpret_cast<void(__cdecl *)(void *, uint32_t, int32_t, int32_t)>(original)(a + 4, value, width, platform);
            reinterpret_cast<void(__cdecl *)(void *, uint32_t, int32_t, int32_t)>(ours)(b + 4, value, width, platform);
        } else {
            reinterpret_cast<void(__cdecl *)(void *, uint32_t, int32_t)>(original)(a + 4, value, width);
            reinterpret_cast<void(__cdecl *)(void *, uint32_t, int32_t)>(ours)(b + 4, value, width);
        }
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("value %08x width %d platform %d: stores differ", value, width, platform);
    }
}

DIFF_TEST(GimexFshLeaves_PutLittleEndian, 0x8840a3)
{
    using Fn = void(__cdecl *)(void *, uint32_t, int32_t);
    PutCases<Fn>(ctx, kPutLittleEndian, &PutLittleEndian, false, 0x8840a3);
}

DIFF_TEST(GimexFshLeaves_PutBigEndian, 0x8840f5)
{
    using Fn = void(__cdecl *)(void *, uint32_t, int32_t);
    PutCases<Fn>(ctx, kPutBigEndian, &PutBigEndian, false, 0x8840f5);
}

DIFF_TEST(GimexFshLeaves_PutWord, 0x884167)
{
    using Fn = void(__cdecl *)(void *, uint32_t, int32_t, int32_t);
    PutCases<Fn>(ctx, kPutWord, &PutWord, true, 0x884167);
}

// The word -> B, G, R, A converters.
DIFF_TEST(GimexFshLeaves_Colour2101010, 0x8841b0)
{
    WordCases(ctx, kColour2101010, &Colour2101010, 0x8841b0);
}

DIFF_TEST(GimexFshLeaves_Colour4444, 0x88421e)
{
    WordCases(ctx, kColour4444, &Colour4444, 0x88421e);
}

DIFF_TEST(GimexFshLeaves_Colour565, 0x884285)
{
    WordCases(ctx, kColour565, &Colour565, 0x884285);
}

DIFF_TEST(GimexFshLeaves_Colour1555, 0x8842df)
{
    WordCases(ctx, kColour1555, &Colour1555, 0x8842df);
}

DIFF_TEST(GimexFshLeaves_Colour655, 0x88433b)
{
    WordCases(ctx, kColour655, &Colour655, 0x88433b);
}

DIFF_TEST(GimexFshLeaves_Colour6666, 0x884393)
{
    WordCases(ctx, kColour6666, &Colour6666, 0x884393);
}

DIFF_TEST(GimexFshLeaves_Colour3555, 0x8846b7)
{
    WordCases(ctx, kColour3555, &Colour3555, 0x8846b7);
}

DIFF_TEST(GimexFshLeaves_Colour88, 0x884767)
{
    WordCases(ctx, kColour88, &Colour88, 0x884767);
}

DIFF_TEST(GimexFshLeaves_Colour1555Alt, 0x8849c7)
{
    WordCases(ctx, kColour1555Alt, &Colour1555Alt, 0x8849c7);
}

DIFF_TEST(GimexFshLeaves_Colour1555Swizzled, 0x88551d)
{
    WordCases(ctx, kColour1555Swizzled, &Colour1555Swizzled, 0x88551d);
}

// The B, G, R, A -> word packers.
DIFF_TEST(GimexFshLeaves_Pack2101010, 0x8843fa)
{
    PixelCases<uint32_t>(ctx, kPack2101010, &Pack2101010, 0x8843fa);
}

DIFF_TEST(GimexFshLeaves_Pack4444, 0x88445c)
{
    PixelCases<uint32_t>(ctx, kPack4444, &Pack4444, 0x88445c);
}

DIFF_TEST(GimexFshLeaves_Pack1555, 0x8844da)
{
    PixelCases<uint16_t>(ctx, kPack1555, &Pack1555, 0x8844da);
}

DIFF_TEST(GimexFshLeaves_Pack565, 0x884519)
{
    PixelCases<uint16_t>(ctx, kPack565, &Pack565, 0x884519);
}

DIFF_TEST(GimexFshLeaves_Pack655, 0x884546)
{
    PixelCases<uint16_t>(ctx, kPack655, &Pack655, 0x884546);
}

DIFF_TEST(GimexFshLeaves_Pack6666, 0x884573)
{
    PixelCases<uint32_t>(ctx, kPack6666, &Pack6666, 0x884573);
}

DIFF_TEST(GimexFshLeaves_Pack3555, 0x8847eb)
{
    PixelCases<uint16_t>(ctx, kPack3555, &Pack3555, 0x8847eb);
}

DIFF_TEST(GimexFshLeaves_Pack556, 0x884989)
{
    PixelCases<uint16_t>(ctx, kPack556, &Pack556, 0x884989);
}

DIFF_TEST(GimexFshLeaves_Pack1555Alt, 0x884a34)
{
    PixelCases<uint32_t>(ctx, kPack1555Alt, &Pack1555Alt, 0x884a34);
}

DIFF_TEST(GimexFshLeaves_Pack1555Swapped, 0x885579)
{
    PixelCases<uint16_t>(ctx, kPack1555Swapped, &Pack1555Swapped, 0x885579);
}

// The luma functions (SSE2 doubles in the original: 0.587 G + 0.299 R + 0.114 B, then rounded).
DIFF_TEST(GimexFshLeaves_PackGrey44, 0x884781)
{
    LumaCases<uint8_t>(ctx, kPackGrey44, &PackGrey44);
}

DIFF_TEST(GimexFshLeaves_Luma, 0x88486e)
{
    LumaCases<int32_t>(ctx, kLuma, &Luma);
}

DIFF_TEST(GimexFshLeaves_PackGrey88, 0x8848ce)
{
    LumaCases<uint16_t>(ctx, kPackGrey88, &PackGrey88);
}

// 0x884938: a strictly increasing grey ramp of 4-byte entries.
DIFF_TEST(GimexFshLeaves_IsGreyRamp, 0x884938)
{
    using Fn = int32_t(__cdecl *)(const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kIsGreyRamp);
    std::mt19937 rng(0x884938);
    for (int i = 0; i < 20000; ++i) {
        const int32_t count = i % 7 == 0 ? static_cast<int32_t>(rng() % 3) - 1 : static_cast<int32_t>(rng() % 12);
        uint8_t entries[16 * 4 + 4];
        int level = i % 5 == 0 ? static_cast<int>(rng() % 256) : -1;
        for (int e = 0; e < 17; ++e) {
            level += i % 3 == 0 ? static_cast<int>(rng() % 3) : 1 + static_cast<int>(rng() % 8);
            const uint8_t v = static_cast<uint8_t>(level);
            entries[e * 4] = entries[e * 4 + 1] = entries[e * 4 + 2] = v;
            entries[e * 4 + 3] = static_cast<uint8_t>(rng());
        }
        switch (rng() % 6) {
        case 0: entries[(rng() % 16) * 4 + rng() % 3] = static_cast<uint8_t>(rng()); break;
        case 1: entries[(rng() % 16) * 4 + 2] = entries[(rng() % 16) * 4 + 2] - 1; break;
        default: break;
        }
        const bool null = i % 11 == 0;
        ++ctx.cases;
        const int32_t a = original(null ? nullptr : entries, count);
        const int32_t b = IsGreyRamp(null ? nullptr : entries, count);
        if (a != b) ctx.Fail("case %d (count %d%s): original %d, ours %d", i, count, null ? ", null" : "", a, b);
    }
}

// 0x884a7e / 0x884ade: padded sizes.
DIFF_TEST(GimexFshLeaves_PaddedSize, 0x884a7e)
{
    using Fn = void(__cdecl *)(uint32_t *, uint32_t *, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kPaddedSize);
    std::mt19937 rng(0x884a7e);
    for (int i = 0; i < 40000; ++i) {
        const int32_t bits = i < 3000 ? (i % 40) : static_cast<int32_t>(rng() % 3 ? (rng() % 40) : rng());
        const int32_t w = i % 4 == 0 ? static_cast<int32_t>(rng()) : static_cast<int32_t>(rng() % 70);
        const int32_t h = i % 4 == 1 ? static_cast<int32_t>(rng()) : static_cast<int32_t>(rng() % 70);
        uint32_t a[2] = {0x5a5a5a5a, 0xa5a5a5a5}, b[2] = {0x5a5a5a5a, 0xa5a5a5a5};
        ++ctx.cases;
        original(&a[0], &a[1], w, h, bits);
        PaddedSize(&b[0], &b[1], w, h, bits);
        if (a[0] != b[0] || a[1] != b[1]) ctx.Fail("%d x %d bits %d: original %u x %u, ours %u x %u", w, h, bits, a[0], a[1], b[0], b[1]);
    }
}

DIFF_TEST(GimexFshLeaves_PageSize, 0x884ade)
{
    using Fn = void(__cdecl *)(uint32_t *, uint32_t *, uint32_t, uint32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kPageSize);
    std::mt19937 rng(0x884ade);
    for (int i = 0; i < 40000; ++i) {
        const int32_t bits = i < 3000 ? (i % 40) : static_cast<int32_t>(rng() % 3 ? (rng() % 40) : rng());
        const uint32_t w = i % 4 == 0 ? rng() : rng() % 100;
        const uint32_t h = i % 4 == 1 ? rng() : rng() % 100;
        uint32_t a[2] = {0x5a5a5a5a, 0xa5a5a5a5}, b[2] = {0x5a5a5a5a, 0xa5a5a5a5};
        ++ctx.cases;
        original(&a[0], &a[1], w, h, bits);
        PageSize(&b[0], &b[1], w, h, bits);
        if (a[0] != b[0] || a[1] != b[1]) ctx.Fail("%u x %u bits %d: original %u x %u, ours %u x %u", w, h, bits, a[0], a[1], b[0], b[1]);
    }
}

// 0x8845f1 / 0x884623 / 0x884655 / 0x884687 / 0x885450: tiled byte offsets.
DIFF_TEST(GimexFshLeaves_TileOffset16, 0x8845f1)
{
    TileCases(ctx, kTileOffset16, &TileOffset16, 0x8845f1);
}

DIFF_TEST(GimexFshLeaves_TileOffset32, 0x884623)
{
    TileCases(ctx, kTileOffset32, &TileOffset32, 0x884623);
}

DIFF_TEST(GimexFshLeaves_TileOffset4, 0x884655)
{
    TileCases(ctx, kTileOffset4, &TileOffset4, 0x884655);
}

DIFF_TEST(GimexFshLeaves_TileOffset8, 0x884687)
{
    TileCases(ctx, kTileOffset8, &TileOffset8, 0x884687);
}

DIFF_TEST(GimexFshLeaves_TileOffset, 0x885450)
{
    using Fn = int32_t(__cdecl *)(int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kTileOffset);
    std::mt19937 rng(0x885450);
    for (int32_t bits = -2; bits < 40; ++bits) {
        for (int i = 0; i < 3000; ++i) {
            const int32_t x = static_cast<int32_t>(rng() % 200), y = static_cast<int32_t>(rng() % 200), w = static_cast<int32_t>(rng() % 200);
            ++ctx.cases;
            const int32_t a = original(x, y, w, bits);
            const int32_t b = TileOffset(x, y, w, bits);
            if (a != b) ctx.Fail("x %d y %d width %d bits %d: original %d, ours %d", x, y, w, bits, a, b);
        }
    }
}

// 0x8854b6: the bit-interleaved index of pixel (x, y).
DIFF_TEST(GimexFshLeaves_InterleavedIndex, 0x8854b6)
{
    using Fn = int32_t(__cdecl *)(int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kInterleavedIndex);
    auto check = [&](int32_t x, int32_t y, int32_t w, int32_t h) {
        ++ctx.cases;
        const int32_t a = original(x, y, w, h);
        const int32_t b = InterleavedIndex(x, y, w, h);
        if (a != b) ctx.Fail("x %d y %d width %d height %d: original %d, ours %d", x, y, w, h, a, b);
    };
    for (int32_t w = 0; w <= 40; ++w)
        for (int32_t h = 0; h <= 40; ++h)
            for (int32_t y = 0; y < 10; ++y)
                for (int32_t x = 0; x < 10; ++x) check(x, y, w, h);
    std::mt19937 rng(0x8854b6);
    const int32_t sides[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
    for (int i = 0; i < 100000; ++i) {
        const int32_t w = i % 3 ? sides[rng() % 11] : static_cast<int32_t>(rng() % 1100);
        const int32_t h = i % 3 ? sides[rng() % 11] : static_cast<int32_t>(rng() % 1100);
        check(static_cast<int32_t>(rng() % 1100), static_cast<int32_t>(rng() % 1100), w, h);
    }
}

// 0x885429: palette index order (8..15 up 8, 16..23 to 24).
DIFF_TEST(GimexFshLeaves_PaletteOrder, 0x885429)
{
    using Fn = uint32_t(__cdecl *)(uint32_t);
    const Fn original = ctx.Original<Fn>(kPaletteOrder);
    std::mt19937 rng(0x885429);
    for (uint32_t index = 0; index < 4096; ++index) {
        ++ctx.cases;
        if (original(index) != PaletteOrder(index)) ctx.Fail("index %u differs", index);
    }
    for (int i = 0; i < 20000; ++i) {
        const uint32_t index = rng();
        ++ctx.cases;
        if (original(index) != PaletteOrder(index)) ctx.Fail("index %08x differs", index);
    }
}

// 0x8855c0 / 0x88563c / 0x88568b / 0x8856d9 / 0x885801 / 0x885825: the record type for the channel bits.
DIFF_TEST(GimexFshLeaves_PaletteTypeForBitsA, 0x8855c0)
{
    TypeCases(ctx, kPaletteTypeA, &PaletteTypeForBitsA);
}

DIFF_TEST(GimexFshLeaves_PaletteTypeForBitsB, 0x88563c)
{
    TypeCases(ctx, kPaletteTypeB, &PaletteTypeForBitsB);
}

DIFF_TEST(GimexFshLeaves_PaletteTypeForBitsF, 0x88568b)
{
    TypeCases(ctx, kPaletteTypeF, &PaletteTypeForBitsF);
}

DIFF_TEST(GimexFshLeaves_PaletteTypeForBitsC, 0x8856d9)
{
    TypeCases(ctx, kPaletteTypeC, &PaletteTypeForBitsC);
}

DIFF_TEST(GimexFshLeaves_PaletteTypeForBitsD, 0x885801)
{
    TypeCases(ctx, kPaletteTypeD, &PaletteTypeForBitsD);
}

DIFF_TEST(GimexFshLeaves_PaletteTypeForBitsE, 0x885825)
{
    TypeCases(ctx, kPaletteTypeE, &PaletteTypeForBitsE);
}

// 0x88570d: whether an image is grey (an 8-bit one by its palette entries first..count, else by its pixels). Only al.
DIFF_TEST(GimexFshLeaves_IsGreyImage, 0x88570d)
{
    using Fn = bool(__cdecl *)(const uint8_t *, const gimex::Info *);
    const Fn original = ctx.Original<Fn>(kIsGreyImage);
    std::mt19937 rng(0x88570d);
    std::vector<uint8_t> infoBytes(sizeof(gimex::Info));
    for (int i = 0; i < 6000; ++i) {
        gimex::Info *info = reinterpret_cast<gimex::Info *>(infoBytes.data());
        for (uint8_t &byte : infoBytes) byte = static_cast<uint8_t>(rng());
        const bool indexed = i % 2 == 0;
        info->bitsPerPixel = indexed ? 8 : (rng() % 3 == 0 ? 4 : 32);
        info->width = static_cast<int32_t>(rng() % 12) - (i % 17 == 0 ? 2 : 0);
        info->height = static_cast<int32_t>(rng() % 9) - (i % 19 == 0 ? 2 : 0);
        info->unknown20 = rng() % 40;
        info->paletteCount = i % 5 == 0 ? rng() % 40 : info->unknown20 + rng() % 8;
        const bool grey = rng() % 3 != 0;
        for (int e = 0; e < 256; ++e) {
            info->palette[e][0] = info->palette[e][1] = info->palette[e][2] = static_cast<uint8_t>(rng());
        }
        // A non-grey palette differs in exactly one channel of one entry (inside the range, or just outside it).
        // A grey palette may differ outside the range (before its first entry and after its last): not looked at.
        if (grey && info->unknown20 > 0 && rng() % 2 == 0) info->palette[rng() % info->unknown20][rng() % 3] ^= 0x55;
        if (grey && info->paletteCount < 255 && rng() % 2 == 0) info->palette[info->paletteCount][rng() % 3] ^= 0x55;
        if (!grey) {
            const uint32_t span = info->paletteCount > info->unknown20 ? info->paletteCount - info->unknown20 : 1;
            const uint32_t entry = (info->unknown20 + rng() % (span + 1)) % 256;
            info->palette[entry][rng() % 3] ^= static_cast<uint8_t>(1 + rng() % 255);
        }
        std::vector<uint8_t> pixels(static_cast<size_t>(12 * 9 * 4 + 16));
        for (size_t p = 0; p + 4 <= pixels.size(); p += 4) {
            const uint8_t v = static_cast<uint8_t>(rng());
            pixels[p] = pixels[p + 1] = pixels[p + 2] = v;
            pixels[p + 3] = static_cast<uint8_t>(rng());
            if (!grey && rng() % 60 == 0) pixels[p + rng() % 3] ^= static_cast<uint8_t>(1 + rng() % 255);
        }
        ++ctx.cases;
        const bool a = original(pixels.data(), info);
        const bool b = IsGreyImage(pixels.data(), info);
        if (a != b) ctx.Fail("case %d (%s, %d x %d, palette %u..%u): original %d, ours %d", i, indexed ? "indexed" : "direct",
                             info->width, info->height, info->unknown20, info->paletteCount, a, b);
    }
}

// 0x88578d: whether a name is a mipmap's ("mm" and a digit, or "_mm" and a digit).
DIFF_TEST(GimexFshLeaves_IsMipmapName, 0x88578d)
{
    using Fn = bool(__cdecl *)(const char *, int32_t);
    const Fn original = ctx.Original<Fn>(kIsMipmapName);
    std::mt19937 rng(0x88578d);
    const char alphabet[] = {'m', 'm', '_', '_', '0', '5', '9', 'a', '/', ':', 'M', ' '};
    for (int i = 0; i < 60000; ++i) {
        char name[40];
        const size_t length = rng() % 14;
        for (size_t c = 0; c < sizeof(name); ++c) name[c] = static_cast<char>(rng());
        for (size_t c = 0; c < length; ++c) name[c] = alphabet[rng() % sizeof(alphabet)];
        name[length] = '\0';
        if (i % 3 == 0 && length >= 4) {
            const size_t at = rng() % (length - 3);
            name[at] = '_';
            name[at + 1] = 'm';
            name[at + 2] = 'm';
            name[at + 3] = static_cast<char>('0' + rng() % 10);
        }
        const int32_t lengths[] = {0, 0, 1, 4, 5, 6, static_cast<int32_t>(length), static_cast<int32_t>(length) + 3, -1, 20};
        const int32_t argument = lengths[rng() % 10];
        ++ctx.cases;
        const bool a = original(name, argument);
        const bool b = IsMipmapName(name, argument);
        if (a != b) ctx.Fail("name \"%s\" length %d: original %d, ours %d", name, argument, a, b);
    }
}
