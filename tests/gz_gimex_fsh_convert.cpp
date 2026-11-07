// Differential tests for the FSH back end's image conversion (lane MV; src/gz/GimexFshConvert.size.cpp, 0x885d30, 0x8885a1..0x888f97,
// 0x88cc20..0x88d5e8). Each function runs alone over seeded input, the original (by RVA, in the mapped image) against ours, the stack
// cleared before each call (alternately with 0x00 and 0xff), comparing the return value and every byte written (outputs sit between guard
// bytes). The functions that allocate (the plane reorderers, the mip chain) also compare the allocator's effect (blocks left, frees).
//
// Kept out: formats 0x16 and 0x1e on the 8-bit planes (RgbToYuv420, ReorderPlanes): with the stride equal to the width the original
// writes 0x21 bytes past the last tile and allocates 0x20, so it corrupts the heap (ReorderImage has that case with a padded stride);
// pixel copies into a destination too small for the original's tile offsets (the reorderers get dimensions the tilings and the
// interleave hold: multiples of the tile sizes, powers of two, whole swizzle pages); dimensions that make the original read past a buffer.
#define NOMINMAX
#include "difftest.h"
#include "free_recorder.h"
#include "gz/Gimex.h"
#include "gz/GimexFshConvert.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gimex::fsh;

namespace {

constexpr uint32_t kClampRound = 0x885d30;
constexpr uint32_t kYuvToRgbFloat = 0x8885a1;
constexpr uint32_t kYCbCrToRgb = 0x888939;
constexpr uint32_t kYuyvPairToBgra = 0x888a19;
constexpr uint32_t kAverageCb = 0x888b13;
constexpr uint32_t kAverageCr = 0x888c40;
constexpr uint32_t kAllocateMipChain = 0x888d6d;
constexpr uint32_t kReorderPlanes = 0x888e00;
constexpr uint32_t kDetectPixelFormat = 0x888efc;
constexpr uint32_t kRgbToYCbCrFloat = 0x888f97;
constexpr uint32_t kRgbToYuv420 = 0x88cc20;
constexpr uint32_t kRgbToYuyv = 0x88ce4e;
constexpr uint32_t kHighestBit = 0x88cf36;
constexpr uint32_t kReorderImage = 0x88cf96;
constexpr uint32_t kStoreLuma = 0x88d212;
constexpr uint32_t kRoundToMultiple = 0x88d362;
constexpr uint32_t kPackYuyv = 0x88d39e;
constexpr uint32_t kMipChainSize = 0x88d52e;
constexpr uint32_t kAlignedOffset = 0x88d5c9;
constexpr uint32_t kRgbToYCbCrInt = 0x88d5e8;
constexpr uint32_t kFreeMemory = 0x87da72;

__declspec(noinline) void ClearStack(uint8_t value)
{
    volatile uint8_t buffer[0x4000];
    for (size_t i = 0; i < sizeof(buffer); ++i) buffer[i] = value;
}

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
    double Unit() { return static_cast<double>(Next() >> 8) / 16777216.0; }
    int32_t Range(int32_t lo, int32_t hi) { return lo + static_cast<int32_t>(Next() % static_cast<uint32_t>(hi - lo + 1)); }
    void Bytes(uint8_t *bytes, size_t count)
    {
        for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(Next() >> 11);
    }
    void Bytes(std::vector<uint8_t> &bytes) { Bytes(bytes.data(), bytes.size()); }
};

uint8_t StackByte(int iter)
{
    return (iter & 1) ? 0xff : 0x00;
}

// A pixel byte source: random, dark, bright, extremes.
uint8_t PixelByte(Fill &fill, int32_t shape)
{
    switch (shape % 6) {
    case 0: return static_cast<uint8_t>(fill.Next() >> 9);
    case 1: return static_cast<uint8_t>(fill.Range(0, 12));
    case 2: return static_cast<uint8_t>(fill.Range(243, 255));
    case 3: return (fill.Next() & 1) ? 0xff : 0x00;
    case 4: return static_cast<uint8_t>(fill.Range(100, 160));
    default: return static_cast<uint8_t>(fill.Next() >> 9);
    }
}

uint32_t BitsOf(float v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    return bits;
}

float RandomPlane(Fill &fill, int32_t shape)
{
    switch (shape % 5) {
    case 0: return static_cast<float>(fill.Range(0, 255));
    case 1: return static_cast<float>(fill.Unit() * 255.0);
    case 2: return static_cast<float>(fill.Unit() * 700.0 - 300.0);
    case 3: return (fill.Next() & 1) ? 255.0f : 0.0f;
    default: return static_cast<float>(fill.Range(-2, 258));
    }
}

#define CHECK(ctx, cond, ...)                                                                                                            \
    do {                                                                                                                                   \
        ++(ctx).cases;                                                                                                                     \
        if (!(cond)) (ctx).Fail(__VA_ARGS__);                                                                                              \
    } while (0)

} // namespace

// ---- the float and integer converters ----

DIFF_TEST(GimexFshConvert_ClampRound, kClampRound)
{
    using Fn = int32_t(__cdecl *)(float);
    const Fn original = ctx.Original<Fn>(kClampRound);
    Fill fill(0x9101);
    auto one = [&](float x, int iter) {
        ClearStack(StackByte(iter));
        const int32_t a = original(x);
        ClearStack(StackByte(iter));
        const int32_t b = ClampRound(x);
        CHECK(ctx, a == b, "ClampRound(%g): original %d, ours %d", static_cast<double>(x), a, b);
    };
    int iter = 0;
    for (int32_t i = -300; i <= 600; ++i) {
        one(static_cast<float>(i), iter++);
        one(static_cast<float>(i) + 0.5f, iter++);
        one(static_cast<float>(i) + 0.4999f, iter++);
        one(static_cast<float>(i) - 0.5f, iter++);
    }
    for (int i = 0; i < 20000; ++i) one(static_cast<float>(fill.Unit() * 800.0 - 300.0), iter++);
    const float edges[] = {1.0e9f, -1.0e9f, 3.0e9f, -3.0e9f, 2147483648.0f, -2147483648.0f, 1.0e30f, -1.0e30f, 0.0f, -0.0f, 1.0e-30f};
    for (const float x : edges) one(x, iter++);
    const float nan = std::nanf("");
    one(nan, iter++);
    one(INFINITY, iter++);
    one(-INFINITY, iter++);
}

namespace {

struct Triple {
    float f[3 + 4];
};

} // namespace

DIFF_TEST(GimexFshConvert_YuvToRgbFloat, kYuvToRgbFloat)
{
    using Fn = void(__cdecl *)(float *, float *, float *, float, float, float);
    const Fn original = ctx.Original<Fn>(kYuvToRgbFloat);
    Fill fill(0x9102);
    for (int iter = 0; iter < 40000; ++iter) {
        const float y = RandomPlane(fill, iter / 3 + iter), u = RandomPlane(fill, iter / 5), v = RandomPlane(fill, iter / 7 + 1);
        Triple a, b;
        for (int k = 0; k < 7; ++k) a.f[k] = b.f[k] = static_cast<float>(k) + 0.25f;
        ClearStack(StackByte(iter));
        original(&a.f[1], &a.f[3], &a.f[5], y, u, v);
        ClearStack(StackByte(iter));
        YuvToRgbFloat(&b.f[1], &b.f[3], &b.f[5], y, u, v);
        CHECK(ctx, std::memcmp(&a, &b, sizeof a) == 0, "YuvToRgbFloat(%g, %g, %g): original %g %g %g, ours %g %g %g", static_cast<double>(y),
              static_cast<double>(u), static_cast<double>(v), static_cast<double>(a.f[1]), static_cast<double>(a.f[3]),
              static_cast<double>(a.f[5]), static_cast<double>(b.f[1]), static_cast<double>(b.f[3]), static_cast<double>(b.f[5]));
    }
}

DIFF_TEST(GimexFshConvert_RgbToYCbCrFloat, kRgbToYCbCrFloat)
{
    using Fn = void(__cdecl *)(float *, float *, float *, float, float, float);
    const Fn original = ctx.Original<Fn>(kRgbToYCbCrFloat);
    Fill fill(0x9103);
    for (int iter = 0; iter < 40000; ++iter) {
        const float r = RandomPlane(fill, iter / 3 + iter), g = RandomPlane(fill, iter / 5), b0 = RandomPlane(fill, iter / 7 + 1);
        Triple a, b;
        for (int k = 0; k < 7; ++k) a.f[k] = b.f[k] = static_cast<float>(k) + 0.25f;
        ClearStack(StackByte(iter));
        original(&a.f[1], &a.f[3], &a.f[5], r, g, b0);
        ClearStack(StackByte(iter));
        RgbToYCbCrFloat(&b.f[1], &b.f[3], &b.f[5], r, g, b0);
        CHECK(ctx, std::memcmp(&a, &b, sizeof a) == 0, "RgbToYCbCrFloat(%g, %g, %g): original %g %g %g, ours %g %g %g", static_cast<double>(r),
              static_cast<double>(g), static_cast<double>(b0), static_cast<double>(a.f[1]), static_cast<double>(a.f[3]),
              static_cast<double>(a.f[5]), static_cast<double>(b.f[1]), static_cast<double>(b.f[3]), static_cast<double>(b.f[5]));
    }
}

DIFF_TEST(GimexFshConvert_YCbCrToRgb, kYCbCrToRgb)
{
    using Fn = void(__cdecl *)(const int32_t *, int32_t *);
    const Fn original = ctx.Original<Fn>(kYCbCrToRgb);
    Fill fill(0x9104);
    auto one = [&](int32_t y, int32_t cb, int32_t cr, int iter) {
        const int32_t in[3] = {y, cb, cr};
        int32_t a[5] = {0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555};
        int32_t b[5];
        std::memcpy(b, a, sizeof a);
        ClearStack(StackByte(iter));
        original(in, a + 1);
        ClearStack(StackByte(iter));
        YCbCrToRgb(in, b + 1);
        CHECK(ctx, std::memcmp(a, b, sizeof a) == 0, "YCbCrToRgb(%d, %d, %d): original %d %d %d, ours %d %d %d", y, cb, cr, a[1], a[2], a[3], b[1],
              b[2], b[3]);
    };
    int iter = 0;
    for (int32_t y = 0; y < 256; y += 5)
        for (int32_t cb = 0; cb < 256; cb += 7)
            for (int32_t cr = 0; cr < 256; cr += 11) one(y, cb, cr, iter++);
    for (int i = 0; i < 40000; ++i) one(fill.Range(-100, 400), fill.Range(-200, 500), fill.Range(-200, 500), iter++);
    for (const int32_t y : {0, 1, 15, 16, 17, 235, 236, 255, 256, -1})
        for (const int32_t c : {0, 16, 127, 128, 129, 240, 255})
            for (const int32_t d : {0, 128, 255}) one(y, c, d, iter++);
}

DIFF_TEST(GimexFshConvert_RgbToYCbCrInt, kRgbToYCbCrInt)
{
    using Fn = void(__cdecl *)(const int32_t *, int32_t *);
    const Fn original = ctx.Original<Fn>(kRgbToYCbCrInt);
    Fill fill(0x9105);
    auto one = [&](int32_t r, int32_t g, int32_t b0, int iter) {
        const int32_t in[3] = {r, g, b0};
        int32_t a[5] = {0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555};
        int32_t b[5];
        std::memcpy(b, a, sizeof a);
        ClearStack(StackByte(iter));
        original(in, a + 1);
        ClearStack(StackByte(iter));
        RgbToYCbCrInt(in, b + 1);
        CHECK(ctx, std::memcmp(a, b, sizeof a) == 0, "RgbToYCbCrInt(%d, %d, %d): original %d %d %d, ours %d %d %d", r, g, b0, a[1], a[2], a[3], b[1],
              b[2], b[3]);
    };
    int iter = 0;
    for (int32_t r = 0; r < 256; r += 3)
        for (int32_t g = 0; g < 256; g += 5)
            for (int32_t b0 = 0; b0 < 256; b0 += 17) one(r, g, b0, iter++);
    for (int i = 0; i < 40000; ++i) one(fill.Range(-80, 400), fill.Range(-80, 400), fill.Range(-80, 400), iter++);
    for (const int32_t v : {0, 1, 254, 255})
        for (const int32_t w : {0, 1, 254, 255})
            for (const int32_t x : {0, 1, 254, 255}) one(v, w, x, iter++);
}

DIFF_TEST(GimexFshConvert_YuyvPairToBgra, kYuyvPairToBgra)
{
    using Fn = void(__cdecl *)(uint8_t *, const uint8_t *);
    const Fn original = ctx.Original<Fn>(kYuyvPairToBgra);
    Fill fill(0x9106);
    for (int iter = 0; iter < 40000; ++iter) {
        uint8_t src[4];
        for (uint8_t &s : src) s = PixelByte(fill, iter / 4);
        uint8_t a[12], b[12];
        std::memset(a, 0xa5, sizeof a);
        std::memset(b, 0xa5, sizeof b);
        ClearStack(StackByte(iter));
        original(a + 2, src);
        ClearStack(StackByte(iter));
        YuyvPairToBgra(b + 2, src);
        CHECK(ctx, std::memcmp(a, b, sizeof a) == 0, "YuyvPairToBgra(%02x %02x %02x %02x): differs (first dst byte %02x vs %02x)", src[0], src[1],
              src[2], src[3], a[2], b[2]);
    }
    // Exhaustive over Y0 and the shared chroma with a fixed second Y.
    for (int y = 0; y < 256; ++y)
        for (int u = 0; u < 256; u += 3)
            for (int v = 0; v < 256; v += 5) {
                const uint8_t src[4] = {static_cast<uint8_t>(y), static_cast<uint8_t>(u), static_cast<uint8_t>(255 - y), static_cast<uint8_t>(v)};
                uint8_t a[8], b[8];
                std::memset(a, 0, sizeof a);
                std::memset(b, 0, sizeof b);
                original(a, src);
                YuyvPairToBgra(b, src);
                CHECK(ctx, std::memcmp(a, b, sizeof a) == 0, "YuyvPairToBgra(%d %d %d): differs", y, u, v);
            }
}

// ---- the rounding and bit helpers ----

DIFF_TEST(GimexFshConvert_RoundToMultiple, kRoundToMultiple)
{
    using Fn = int32_t(__cdecl *)(int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kRoundToMultiple);
    Fill fill(0x9107);
    int iter = 0;
    auto one = [&](int32_t v, int32_t d) {
        ClearStack(StackByte(iter++));
        const int32_t a = original(v, d);
        ClearStack(StackByte(iter));
        const int32_t b = RoundToMultiple(v, d);
        CHECK(ctx, a == b, "RoundToMultiple(%d, %d): original %d, ours %d", v, d, a, b);
    };
    for (int32_t d = -12; d <= 70; ++d) {
        if (d == 0) continue;
        for (int32_t v = -700; v <= 700; ++v) one(v, d);
    }
    for (int i = 0; i < 60000; ++i) one(fill.Range(-100000, 100000), fill.Range(1, 300));
    for (int i = 0; i < 2000; ++i) one(fill.Range(-1000000, 1000000), fill.Range(-300, -1));
    for (const int32_t d : {1, 2, 3, 255, 256, 1000}) {
        one(0, d);
        one(0x3fffffff, d);
        one(-0x3fffffff, d);
    }
}

DIFF_TEST(GimexFshConvert_HighestBit, kHighestBit)
{
    using Fn = uint32_t(__cdecl *)(uint32_t);
    const Fn original = ctx.Original<Fn>(kHighestBit);
    Fill fill(0x9108);
    int iter = 0;
    auto one = [&](uint32_t v) {
        ClearStack(StackByte(iter++));
        const uint32_t a = original(v);
        ClearStack(StackByte(iter));
        const uint32_t b = HighestBit(v);
        CHECK(ctx, a == b, "HighestBit(%#x): original %u, ours %u", v, a, b);
    };
    one(0);
    for (int bit = 0; bit < 32; ++bit) {
        const uint32_t p = 1u << bit;
        one(p);
        one(p - 1);
        one(p + 1);
        one(p | (p - 1));
        one(~(p - 1));
    }
    one(0xffffffff);
    for (int i = 0; i < 30000; ++i) {
        uint32_t v = fill.Next();
        v >>= fill.Range(0, 31);
        one(v);
    }
}

DIFF_TEST(GimexFshConvert_AlignedOffset, kAlignedOffset)
{
    using Fn = int32_t(__cdecl *)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kAlignedOffset);
    Fill fill(0x9109);
    for (int iter = 0; iter < 60000; ++iter) {
        const int32_t a = fill.Range(-3, 12), count = fill.Range(-3, 12), b = fill.Range(-5000, 100000), f = fill.Range(-100, 100000);
        const int32_t align = (iter % 4 == 0) ? fill.Range(0, 100) : ((1 << fill.Range(0, 12)) - 1);
        const int32_t unused = static_cast<int32_t>(fill.Next());
        ClearStack(StackByte(iter));
        const int32_t x = original(a, b, unused, align, count, f);
        ClearStack(StackByte(iter));
        const int32_t y = AlignedOffset(a, b, unused, align, count, f);
        CHECK(ctx, x == y, "AlignedOffset(%d, %d, _, %d, %d, %d): original %d, ours %d", a, b, align, count, f, x, y);
    }
}

DIFF_TEST(GimexFshConvert_MipChainSize, kMipChainSize)
{
    using Fn = int32_t(__cdecl *)(int32_t, int32_t, uint32_t, uint32_t, int32_t, int32_t *, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kMipChainSize);
    Fill fill(0x910a);
    for (int iter = 0; iter < 60000; ++iter) {
        const int32_t levels = fill.Range(-1, 12), count = fill.Range(-2, 20), last = fill.Range(-1, 13);
        int32_t width = (iter % 3 == 0) ? fill.Range(0, 5000) : (1 << fill.Range(0, 11)), height = (iter % 3 == 0) ? fill.Range(0, 5000) : (1 << fill.Range(0, 11));
        if (iter % 11 == 0) width = -fill.Range(1, 300); // negative sizes: no caller passes them, the original's arithmetic is compared anyway
        if (iter % 13 == 0) height = -fill.Range(1, 300);
        const int32_t bits = (iter % 2) ? fill.Range(0, 40) : (fill.Range(0, 5) * 4 + 4);
        const uint32_t align = (iter % 5 == 0) ? static_cast<uint32_t>(fill.Range(0, 300)) : ((1u << fill.Range(0, 10)) - 1);
        const uint32_t unused = fill.Next();
        int32_t outA[3] = {0x5a5a5a5a, 0x1234, 0x6b6b6b6b};
        int32_t outB[3] = {0x5a5a5a5a, 0x1234, 0x6b6b6b6b};
        ClearStack(StackByte(iter));
        const int32_t a = original(levels, count, unused, align, last, outA + 1, width, height, bits);
        ClearStack(StackByte(iter));
        const int32_t b = MipChainSize(levels, count, unused, align, last, outB + 1, width, height, bits);
        CHECK(ctx, a == b && std::memcmp(outA, outB, sizeof outA) == 0,
              "MipChainSize(%d, %d, _, %u, %d, _, %d, %d, %d): original %d (%d), ours %d (%d)", levels, count, align, last, width, height, bits, a,
              outA[1], b, outB[1]);
    }
}

DIFF_TEST(GimexFshConvert_AllocateMipChain, kAllocateMipChain)
{
    using Fn = void *(__cdecl *)(int32_t, int32_t, int32_t, int32_t *, int32_t, uint32_t);
    using FreeFn = uint8_t(__stdcall *)(void *);
    const Fn original = ctx.Original<Fn>(kAllocateMipChain);
    const FreeFn freeOriginal = ctx.Original<FreeFn>(kFreeMemory);
    Fill fill(0x910b);
    for (int iter = 0; iter < 20000; ++iter) {
        const int32_t width = (iter % 3 == 0) ? fill.Range(0, 600) : (1 << fill.Range(0, 9)), height = (iter % 3 == 0) ? fill.Range(0, 600) : (1 << fill.Range(0, 9));
        const int32_t bytes = (iter % 2) ? fill.Range(0, 40) : (fill.Range(0, 5) * 4 + 4);
        const int32_t levels = fill.Range(-1, 11);
        const uint32_t align = (iter % 5 == 0) ? static_cast<uint32_t>(fill.Range(0, 300)) : ((1u << fill.Range(0, 10)) - 1);
        int32_t outA[3] = {0x5a5a5a5a, 0x1234, 0x6b6b6b6b};
        int32_t outB[3] = {0x5a5a5a5a, 0x1234, 0x6b6b6b6b};
        void *pa, *pb;
        long long blocks[2];
        size_t frees[2];
        for (int side = 0; side < 2; ++side) {
            difftest::AllocationCounter counter;
            difftest::FreeRecorder recorder;
            ClearStack(StackByte(iter));
            if (side == 0) {
                pa = original(width, height, bytes, outA + 1, levels, align);
                freeOriginal(pa);
            } else {
                pb = AllocateMipChain(width, height, bytes, outB + 1, levels, align);
                gimex::FreeMemory(pb);
            }
            recorder.Stop();
            counter.Stop();
            blocks[side] = counter.Blocks();
            frees[side] = recorder.Count();
        }
        CHECK(ctx, std::memcmp(outA, outB, sizeof outA) == 0 && blocks[0] == blocks[1] && frees[0] == frees[1],
              "AllocateMipChain(%d, %d, %d, _, %d, %u): original total %d (%lld blocks, %zu frees), ours %d (%lld blocks, %zu frees)", width, height,
              bytes, levels, align, outA[1], blocks[0], frees[0], outB[1], blocks[1], frees[1]);
    }
}

// ---- the 2x2 block writers ----

namespace {

struct Pixels4 {
    uint8_t p[4][4];
};

void MakePixels(Fill &fill, int iter, Pixels4 &px)
{
    const int32_t shape = (iter / 3) % 8;
    for (auto &pixel : px.p)
        for (uint8_t &v : pixel) v = PixelByte(fill, shape == 6 ? 1 : shape == 7 ? 2 : shape);
}

} // namespace

static void AverageCases(difftest::Context &ctx, uint32_t rva, uint8_t *(__cdecl *ours)(uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *, int32_t), const char *name, uint32_t seed)
{
    using Fn = uint8_t *(__cdecl *)(uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(rva);
    Fill fill(seed);
    for (int iter = 0; iter < 60000; ++iter) {
        Pixels4 px;
        MakePixels(fill, iter, px);
        const int32_t d = (iter % 7 == 0) ? fill.Range(-9, 70) : fill.Range(1, 40);
        if (d == 0) continue;
        uint8_t a[6], b[6];
        std::memset(a, 0xc3, sizeof a);
        std::memset(b, 0xc3, sizeof b);
        ClearStack(StackByte(iter));
        uint8_t *ra = original(a + 2, px.p[0], px.p[1], px.p[2], px.p[3], d);
        ClearStack(StackByte(iter));
        uint8_t *rb = ours(b + 2, px.p[0], px.p[1], px.p[2], px.p[3], d);
        CHECK(ctx, ra - a == rb - b && std::memcmp(a, b, sizeof a) == 0, "%s iter %d (d=%d): original %02x return +%d, ours %02x return +%d", name, iter, d,
              a[2], static_cast<int>(ra - a), b[2], static_cast<int>(rb - b));
    }
}

DIFF_TEST(GimexFshConvert_AverageCb, kAverageCb)
{
    AverageCases(ctx, kAverageCb, &AverageCb, "AverageCb", 0x910c);
}

DIFF_TEST(GimexFshConvert_AverageCr, kAverageCr)
{
    AverageCases(ctx, kAverageCr, &AverageCr, "AverageCr", 0x910d);
}

DIFF_TEST(GimexFshConvert_StoreLuma, kStoreLuma)
{
    using Fn = uint8_t *(__cdecl *)(uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kStoreLuma);
    Fill fill(0x910e);
    for (int iter = 0; iter < 60000; ++iter) {
        Pixels4 px;
        MakePixels(fill, iter, px);
        const int32_t d = (iter % 7 == 0) ? fill.Range(-9, 70) : fill.Range(1, 40);
        if (d == 0) continue;
        const int32_t stride = fill.Range(2, 30);
        uint8_t a[80], b[80];
        std::memset(a, 0xc3, sizeof a);
        std::memset(b, 0xc3, sizeof b);
        ClearStack(StackByte(iter));
        uint8_t *ra = original(a + 4, px.p[0], px.p[1], px.p[2], px.p[3], d, stride);
        ClearStack(StackByte(iter));
        uint8_t *rb = StoreLuma(b + 4, px.p[0], px.p[1], px.p[2], px.p[3], d, stride);
        CHECK(ctx, ra - a == rb - b && std::memcmp(a, b, sizeof a) == 0, "StoreLuma iter %d (d=%d, stride %d): differs", iter, d, stride);
    }
}

DIFF_TEST(GimexFshConvert_PackYuyv, kPackYuyv)
{
    using Fn = void(__cdecl *)(uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *);
    const Fn original = ctx.Original<Fn>(kPackYuyv);
    Fill fill(0x910f);
    for (int iter = 0; iter < 60000; ++iter) {
        Pixels4 px;
        MakePixels(fill, iter, px);
        uint8_t a[12], b[12];
        std::memset(a, 0xc3, sizeof a);
        std::memset(b, 0xc3, sizeof b);
        ClearStack(StackByte(iter));
        original(a + 4, px.p[0], px.p[1], px.p[2], px.p[3]);
        ClearStack(StackByte(iter));
        PackYuyv(b + 4, px.p[0], px.p[1], px.p[2], px.p[3]);
        CHECK(ctx, std::memcmp(a, b, sizeof a) == 0, "PackYuyv iter %d: original %02x %02x %02x %02x, ours %02x %02x %02x %02x", iter, a[4], a[5], a[6], a[7],
              b[4], b[5], b[6], b[7]);
    }
}

// ---- whole images ----

DIFF_TEST(GimexFshConvert_RgbToYuyv, kRgbToYuyv)
{
    using Fn = int32_t(__cdecl *)(const gimex::Info *, int32_t, const uint8_t *, uint8_t *);
    const Fn original = ctx.Original<Fn>(kRgbToYuyv);
    Fill fill(0x9110);
    static gimex::Info info;
    for (int iter = 0; iter < 6000; ++iter) {
        const int32_t width = (iter < 100) ? iter % 10 : fill.Range(0, 11), height = (iter < 100) ? iter / 10 : fill.Range(0, 7);
        std::memset(&info, 0, sizeof info);
        info.width = width;
        info.height = height;
        const int32_t stride = width * 4 + fill.Range(0, 8);
        std::vector<uint8_t> src(static_cast<size_t>(stride) * (height + 2) + 64);
        for (size_t i = 0; i < src.size(); ++i) src[i] = PixelByte(fill, (iter / 5) % 6);
        const size_t dstBytes = static_cast<size_t>(((width + 1) & ~1) * 2) * (height + 1) + 64;
        std::vector<uint8_t> a(dstBytes), b;
        fill.Bytes(a);
        b = a;
        ClearStack(StackByte(iter));
        const int32_t ra = original(&info, stride, src.data(), a.data() + 16);
        ClearStack(StackByte(iter));
        const int32_t rb = RgbToYuyv(&info, stride, src.data(), b.data() + 16);
        CHECK(ctx, ra == rb && a == b, "RgbToYuyv %d x %d: original %d, ours %d, bytes %s", width, height, ra, rb, a == b ? "equal" : "differ");
    }
}

namespace {

struct YuvCase {
    int32_t width, height, aw, ah, mode, pixelFormat;
    uint32_t flags;
};

} // namespace

DIFF_TEST(GimexFshConvert_RgbToYuv420, kRgbToYuv420)
{
    using Fn = int32_t(__cdecl *)(const gimex::Info *, int32_t, const uint8_t *, uint8_t *, int32_t, int32_t, int32_t, uint32_t, int32_t, int32_t, int32_t,
                                  int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kRgbToYuv420);
    Fill fill(0x9111);
    static gimex::Info info;
    const int32_t formats[] = {0x17, 0x1f, 0x40, 0x79, 0x7a, 0x01, 0x00, 0x2a};
    const int32_t pow2[] = {2, 4, 8, 16};
    for (int iter = 0; iter < 1500; ++iter) {
        YuvCase c = {};
        c.pixelFormat = formats[fill.Next() % 8];
        switch (iter % 6) {
        case 0: // no alpha, any mode
            c.width = fill.Range(0, 13);
            c.height = fill.Range(0, 13);
            c.mode = static_cast<int32_t>(fill.Next() % 6) + 2;
            c.flags = fill.Next() & ~0x2000u;
            break;
        case 1: // alpha, tiled planes
            c.width = fill.Range(1, 13);
            c.height = fill.Range(1, 13);
            c.mode = 5;
            c.aw = 16 * fill.Range(1, 3);
            c.ah = 8 * fill.Range(1, 4);
            c.flags = 0x2000;
            break;
        case 2: // alpha, interleaved planes (powers of two)
            c.width = pow2[fill.Next() % 4];
            c.height = pow2[fill.Next() % 4];
            c.mode = (fill.Next() & 1) ? 2 : 8;
            c.flags = 0x2000 | (fill.Next() & 0x1fff);
            break;
        case 3: // alpha, planes untouched modes
            c.width = fill.Range(1, 13);
            c.height = fill.Range(1, 13);
            c.mode = (fill.Next() & 1) ? 3 : 0;
            c.aw = fill.Range(1, 9);
            c.ah = fill.Range(1, 9);
            c.flags = 0x2000;
            break;
        case 4: // tiled mode with the flag clear, alpha dims given
            c.width = fill.Range(1, 13);
            c.height = fill.Range(1, 13);
            c.mode = 5;
            c.aw = fill.Range(1, 40);
            c.ah = fill.Range(1, 40);
            c.flags = 0;
            break;
        default: // alpha, swizzled planes (whole pages)
            c.width = (iter % 12 == 5) ? 256 : fill.Range(1, 9);
            c.height = (iter % 12 == 5) ? 256 : fill.Range(1, 9);
            c.mode = (iter % 12 == 5) ? 6 : 7;
            c.flags = 0x2000;
            break;
        }
        std::memset(&info, 0, sizeof info);
        info.width = c.width;
        info.height = c.height;
        const int32_t stride = c.width * 4 + fill.Range(0, 8);
        std::vector<uint8_t> src(static_cast<size_t>(stride) * (c.height + 3) + 64);
        for (size_t i = 0; i < src.size(); ++i) src[i] = PixelByte(fill, (iter / 7) % 6);
        const int32_t lumaStep = fill.Range(1, 40), cbStep = fill.Range(1, 40), crStep = fill.Range(1, 40);
        const size_t dstBytes = 160000;
        std::vector<uint8_t> a(dstBytes), b;
        fill.Bytes(a);
        b = a;
        int32_t result[2];
        long long blocks[2];
        size_t frees[2];
        for (int side = 0; side < 2; ++side) {
            difftest::AllocationCounter counter;
            difftest::FreeRecorder recorder;
            ClearStack(StackByte(iter));
            if (side == 0)
                result[0] = original(&info, stride, src.data(), a.data() + 32, lumaStep, cbStep, crStep, c.flags, c.mode, c.pixelFormat, 77, c.aw, c.ah);
            else
                result[1] = RgbToYuv420(&info, stride, src.data(), b.data() + 32, lumaStep, cbStep, crStep, c.flags, c.mode, c.pixelFormat, 77, c.aw, c.ah);
            recorder.Stop();
            counter.Stop();
            blocks[side] = counter.Blocks();
            frees[side] = recorder.Count();
        }
        CHECK(ctx, result[0] == result[1] && a == b && blocks[0] == blocks[1] && frees[0] == frees[1],
              "RgbToYuv420 iter %d (%d x %d, mode %d, flags %#x, alpha %d x %d, format %#x): original %d (%lld blocks, %zu frees), ours %d (%lld blocks, %zu frees), bytes %s",
              iter, c.width, c.height, c.mode, c.flags, c.aw, c.ah, c.pixelFormat, result[0], blocks[0], frees[0], result[1], blocks[1], frees[1],
              a == b ? "equal" : "differ");
    }
}

// ---- the plane reorderers ----

namespace {

struct ReorderCase {
    int32_t width, height, bits, format, mode, stride;
};

const int32_t kOtherModes[] = {0, 1, 3, 4, 7, 9, -1};

// Dimensions the original's tilings, interleave and swizzles can hold in the allocated buffer.
ReorderCase MakeReorderCase(Fill &fill, int iter)
{
    const int32_t formats[] = {0x16, 0x1e, 0x40, 0x79, 0x7a, 0x01, 0x00, 0x2a, 0x55};
    const int32_t pow2[] = {1, 2, 4, 8, 16, 32};
    ReorderCase c = {};
    c.format = formats[fill.Next() % 9];
    const int32_t bitsPool[] = {4, 8, 16, 15, 24, 32, 1, 8, 8, 32, 16, 0x21, 0x40}; // 0x21, 0x40: past every branch (coordinator, MV merge)
    c.bits = bitsPool[fill.Next() % 13];
    const int32_t bytes = c.bits <= 8 ? 1 : c.bits <= 16 ? 2 : c.bits <= 24 ? 3 : c.bits <= 32 ? 4 : 1;
    switch (iter % 4) {
    case 0: // tiled
        c.mode = 5;
        c.width = 32 * fill.Range(1, 3);
        c.height = 16 * fill.Range(1, 3);
        c.stride = c.width * bytes + fill.Range(64, 100);
        break;
    case 1: // interleaved
        c.mode = (fill.Next() & 1) ? 2 : 8;
        c.width = pow2[fill.Next() % 6];
        c.height = pow2[fill.Next() % 6];
        c.stride = c.width * bytes + fill.Range(0, 20);
        break;
    case 2: // swizzled
        c.mode = 6;
        c.width = 128 * fill.Range(1, 2);
        c.height = 128 * fill.Range(1, 2);
        c.stride = c.bits == 4 ? c.width / 2 : c.width * bytes;
        break;
    default: // other modes
        c.mode = kOtherModes[fill.Next() % 7];
        c.width = fill.Range(0, 50);
        c.height = fill.Range(0, 50);
        c.stride = fill.Range(0, 100);
        break;
    }
    return c;
}

} // namespace

DIFF_TEST(GimexFshConvert_ReorderImage, kReorderImage)
{
    using Fn = uint8_t *(__cdecl *)(const uint8_t *, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
    using FreeFn = uint8_t(__stdcall *)(void *);
    const Fn original = ctx.Original<Fn>(kReorderImage);
    const FreeFn freeOriginal = ctx.Original<FreeFn>(kFreeMemory);
    Fill fill(0x9112);
    for (int iter = 0; iter < 2400; ++iter) {
        const ReorderCase c = MakeReorderCase(fill, iter);
        const size_t srcBytes = static_cast<size_t>(c.height + 2) * (static_cast<size_t>(c.stride) + 64) + 4096;
        std::vector<uint8_t> src(srcBytes);
        fill.Bytes(src);
        const size_t destBytes = static_cast<size_t>(c.height) * c.stride + 0x20;
        uint8_t *pa = nullptr, *pb = nullptr;
        long long blocks[2];
        size_t frees[2];
        std::vector<uint8_t> da, db;
        for (int side = 0; side < 2; ++side) {
            difftest::AllocationCounter counter;
            difftest::FreeRecorder recorder;
            ClearStack(StackByte(iter));
            uint8_t *p = side == 0 ? original(src.data(), c.width, c.height, c.bits, c.format, c.mode, c.stride)
                                   : ReorderImage(src.data(), c.width, c.height, c.bits, c.format, c.mode, c.stride);
            std::vector<uint8_t> copy;
            if (p != nullptr) copy.assign(p, p + destBytes);
            (side == 0 ? pa : pb) = p;
            (side == 0 ? da : db) = copy;
            if (p != nullptr) {
                if (side == 0) freeOriginal(p);
                else gimex::FreeMemory(p);
            }
            recorder.Stop();
            counter.Stop();
            blocks[side] = counter.Blocks();
            frees[side] = recorder.Count();
        }
        CHECK(ctx, (pa == nullptr) == (pb == nullptr) && da == db && blocks[0] == blocks[1] && frees[0] == frees[1],
              "ReorderImage iter %d (%d x %d, bits %d, format %#x, mode %d, stride %d): %s (%lld/%zu vs %lld/%zu blocks/frees)", iter, c.width, c.height, c.bits,
              c.format, c.mode, c.stride, da == db ? "same buffer" : "buffers differ", blocks[0], frees[0], blocks[1], frees[1]);
    }
}

DIFF_TEST(GimexFshConvert_ReorderPlanes, kReorderPlanes)
{
    using Fn = void(__cdecl *)(uint8_t *, int32_t, int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kReorderPlanes);
    Fill fill(0x9113);
    const int32_t formats[] = {0x17, 0x1f, 0x40, 0x79, 0x7a, 0x01, 0x00, 0x2a};
    const int32_t pow2[] = {2, 4, 8, 16, 32};
    for (int iter = 0; iter < 1500; ++iter) {
        int32_t width, height, mode;
        switch (iter % 4) {
        case 0: mode = 5; width = 16 * fill.Range(1, 4); height = 8 * fill.Range(1, 6); break;
        case 1: mode = (fill.Next() & 1) ? 2 : 8; width = pow2[fill.Next() % 5]; height = pow2[fill.Next() % 5]; break;
        case 2: mode = 6; width = 256; height = 256; break;
        default: mode = (fill.Next() & 1) ? 3 : 0; width = fill.Range(0, 30); height = fill.Range(0, 30); break;
        }
        if (mode == 6 && (iter % 16) != 2) { mode = 4; }
        const int32_t format = formats[fill.Next() % 8];
        std::vector<uint8_t> a(static_cast<size_t>(width) * height * 2 + 4096), b;
        fill.Bytes(a);
        b = a;
        long long blocks[2];
        size_t frees[2];
        for (int side = 0; side < 2; ++side) {
            difftest::AllocationCounter counter;
            difftest::FreeRecorder recorder;
            ClearStack(StackByte(iter));
            if (side == 0) original(a.data() + 16, width, height, format, mode, 0x5a5a5a5a);
            else ReorderPlanes(b.data() + 16, width, height, format, mode);
            recorder.Stop();
            counter.Stop();
            blocks[side] = counter.Blocks();
            frees[side] = recorder.Count();
        }
        CHECK(ctx, a == b && blocks[0] == blocks[1] && frees[0] == frees[1],
              "ReorderPlanes iter %d (%d x %d, mode %d, format %#x): bytes %s (%lld/%zu vs %lld/%zu blocks/frees)", iter, width, height, mode, format,
              a == b ? "equal" : "differ", blocks[0], frees[0], blocks[1], frees[1]);
    }
}

// ---- the pixel format sniffer (the original 0x88888f runs on both sides) ----

DIFF_TEST(GimexFshConvert_DetectPixelFormat, kDetectPixelFormat)
{
    using Fn = void(__cdecl *)(const uint8_t *, const void *, int32_t, int32_t *, int32_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kDetectPixelFormat);
    Fill fill(0x9114);
    const uint8_t kinds[][2] = {{0xfb, 0x1a}, {0xfb, 0x1c}, {0xfb, 0x28}, {0xfb, 0x9a}, {0xfb, 0x9c}, {0xfb, 0x18}, {0xfb, 0x10}, {0xfb, 0x11}, {0x00, 0x00}};
    for (int iter = 0; iter < 30000; ++iter) {
        uint8_t header[0x40];
        fill.Bytes(header, sizeof header);
        header[1] = static_cast<uint8_t>((fill.Next() & 1) ? (header[1] | 2) : (header[1] & ~2));
        header[2] = static_cast<uint8_t>((fill.Next() & 1) ? (header[2] | 2) : (header[2] & ~2));
        header[3] = static_cast<uint8_t>((fill.Next() & 1) ? (header[3] | 2) : (header[3] & ~2));
        const uint32_t kind = fill.Next() % 12;
        if (kind < 9) {
            header[0x20] = kinds[kind][0];
            header[0x21] = kinds[kind][1];
        }
        uint8_t data[4];
        fill.Bytes(data, sizeof data);
        const uint32_t dataKind = fill.Next() % 5;
        if (dataKind == 0) data[0] = 0x61;
        if (dataKind == 1) data[0] = 0x62;
        const int32_t force = (fill.Next() % 3) ? 0 : static_cast<int32_t>(fill.Next());
        const int32_t platform = fill.Range(0, 20);
        int32_t outA[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
        int32_t outB[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
        ClearStack(StackByte(iter));
        original(header, data, force, outA + 1, outA + 2, platform);
        ClearStack(StackByte(iter));
        DetectPixelFormat(header, data, force, outB + 1, outB + 2, platform);
        CHECK(ctx, std::memcmp(outA, outB, sizeof outA) == 0, "DetectPixelFormat iter %d (platform %d, force %d): original %d %d, ours %d %d", iter, platform,
              force, outA[1], outA[2], outB[1], outB[2]);
    }
}
