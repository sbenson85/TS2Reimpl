// Differential tests for the FSH colour record encoder (lane MS; src/gz/GimexFshColourBlock.size.cpp): the block encoder 0x896e1e, the
// strip driver 0x8976f6, the three leaves 0x89779a, 0x8978a7, 0x897987 and the record writer 0x897a29. Each runs alone over seeded
// pixels, the original (by RVA, in the mapped image) against ours.
//
// The original leaves tables on its stack uninitialised and reads them (see the top of GimexFshColourBlock.size.cpp). Ours reads zeros
// there, so the tests clear the stack just before every original call (ClearStack), and compare the result with ours. The record
// tests use blocks the original does not read garbage for (opaque, or transparent with two colours at most): there the earlier
// blocks' leftovers cannot reach the output. Every block is also decoded with the decoder of lane MR and compared with the original
// (which reaches the same bytes by construction; it checks the test's own layout).
//
// Compared: the return values, every byte written (with sentinel margins) and the source untouched.
#define NOMINMAX
#include "difftest.h"
#include "game/game.h"
#include "gz/GimexFshEncode.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kEncodeColourBlock = 0x896e1e;
constexpr uint32_t kEncodeColourBlocks = 0x8976f6;
constexpr uint32_t kNearestOfFour = 0x89779a;
constexpr uint32_t kNearestOfThree = 0x8978a7;
constexpr uint32_t kColourError = 0x897987;
constexpr uint32_t kEncodeColourRecord = 0x897a29;

using BlockFn = void(__cdecl *)(const uint32_t *, uint16_t *, int32_t, int32_t, int32_t);
using BlocksFn = void(__cdecl *)(const uint32_t *, uint8_t *, uint32_t, uint32_t);
using FourFn = uint8_t(__cdecl *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using ThreeFn = uint32_t(__cdecl *)(uint32_t, uint32_t, uint32_t, uint32_t);
using ErrorFn = uint32_t(__cdecl *)(const uint8_t *, const uint8_t *, uint8_t);
using RecordFn = int32_t(__stdcall *)(uint8_t *, const uint32_t *, int32_t, const uint32_t *);

__declspec(noinline) void ClearStack()
{
    volatile uint8_t buffer[0x4000];
    for (size_t i = 0; i < sizeof(buffer); ++i) buffer[i] = 0;
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
};

enum class Shape { Flat, GradientX, GradientY, Noise, Edges, FewColours, TwoColours, Transparent, TransparentFew, Count };

// The shapes whose blocks the original reads no leftover stack for (opaque, or two colours).
const Shape kQuiet[5] = {Shape::Flat, Shape::GradientX, Shape::GradientY, Shape::FewColours, Shape::TwoColours};

uint32_t Opaque(uint32_t value)
{
    return value | 0xff000000u;
}

// Fills `count` pixels (rows of `width`, `stride` apart).
void MakePixels(Fill &fill, Shape shape, std::vector<uint32_t> &pixels, int32_t width, int32_t height, int32_t stride)
{
    pixels.assign(static_cast<size_t>(stride) * height + 8, 0xdeadbeef);
    uint32_t palette[8];
    for (auto &p : palette) p = fill.Next();
    const uint32_t base = fill.Next();
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            uint32_t pixel = 0;
            switch (shape) {
            case Shape::Flat: pixel = Opaque(base); break;
            case Shape::GradientX: pixel = Opaque((x * 0x0a0a0a + base) & 0xffffff); break;
            case Shape::GradientY: pixel = Opaque((y * 0x101010 + x * 0x030201) ^ (base & 0xe0e0e0)); break;
            case Shape::Noise: pixel = fill.Next(); break;
            case Shape::Edges: {
                static const uint32_t kEdges[] = {0xff000000, 0xffffffff, 0xff0000ff, 0xffff0000, 0xff00ff00, 0xff070707, 0xfff8f8f8, 0x00ffffff};
                pixel = kEdges[fill.Next() % 8];
                break;
            }
            case Shape::FewColours: pixel = Opaque(palette[fill.Next() % (3 + base % 4)]); break;
            case Shape::TwoColours: pixel = Opaque(palette[fill.Next() % 2]); break;
            case Shape::Transparent: pixel = (fill.Next() % 3 == 0) ? (fill.Next() & 0x3fffffff) : fill.Next() | 0x40000000u; break;
            case Shape::TransparentFew: pixel = palette[fill.Next() % (2 + base % 5)]; break;
            default: break;
            }
            pixels[static_cast<size_t>(y) * stride + x] = pixel;
        }
    }
}

} // namespace

DIFF_TEST(GimexFshEncodeColour_Block, kEncodeColourBlock)
{
    const BlockFn original = ctx.Original<BlockFn>(kEncodeColourBlock);
    Fill fill(0x4d53);
    std::vector<uint32_t> pixels;
    for (int iter = 0; iter < 60000; ++iter) {
        const int32_t width = 1 + fill.Next() % 4;
        const int32_t height = 1 + fill.Next() % 4;
        const int32_t stride = width + fill.Next() % 6;
        const Shape shape = static_cast<Shape>(fill.Next() % static_cast<uint32_t>(Shape::Count));
        MakePixels(fill, shape, pixels, width, height, stride);
        const std::vector<uint32_t> before = pixels;
        uint16_t expected[16];
        uint16_t actual[16];
        std::memset(expected, 0xcc, sizeof expected);
        std::memset(actual, 0xcc, sizeof actual);
        ClearStack();
        original(pixels.data(), expected, stride, width, height);
        ClearStack();
        gimex::fsh::EncodeColourBlock(pixels.data(), actual, stride, width, height);
        ++ctx.cases;
        if (std::memcmp(expected, actual, sizeof expected) != 0 || pixels != before)
            ctx.Fail("block %dx%d stride %d shape %d iter %d: %04x %04x %04x vs %04x %04x %04x", width, height, stride, static_cast<int>(shape),
                     iter, expected[0], expected[1], expected[2], actual[0], actual[1], actual[2]);
    }
}

DIFF_TEST(GimexFshEncodeColour_NearestOfFour, kNearestOfFour)
{
    const FourFn original = ctx.Original<FourFn>(kNearestOfFour);
    Fill fill(0x1001);
    for (int iter = 0; iter < 200000; ++iter) {
        uint32_t slot[5];
        for (auto &s : slot) {
            const uint32_t r = fill.Next();
            s = (r & 0x1f) | (((r >> 8) & 0x3f) << 8) | (((r >> 16) & 0x1f) << 16);
            if (iter % 7 == 0) s = fill.Next() & 0x00ffffff;
        }
        if (iter % 5 == 0) slot[4] = slot[iter % 4];
        const uint8_t expected = original(slot[0], slot[1], slot[2], slot[3], slot[4]);
        const uint8_t actual = gimex::fsh::NearestOfFour(slot[0], slot[1], slot[2], slot[3], slot[4]);
        ++ctx.cases;
        if (expected != actual) ctx.Fail("nearest of four %08x %08x %08x %08x %08x: %u vs %u", slot[0], slot[1], slot[2], slot[3], slot[4], expected, actual);
    }
}

DIFF_TEST(GimexFshEncodeColour_NearestOfThree, kNearestOfThree)
{
    const ThreeFn original = ctx.Original<ThreeFn>(kNearestOfThree);
    Fill fill(0x1002);
    for (int iter = 0; iter < 200000; ++iter) {
        uint32_t slot[4];
        for (auto &s : slot) {
            const uint32_t r = fill.Next();
            s = (r & 0x1f) | (((r >> 8) & 0x3f) << 8) | (((r >> 16) & 0x1f) << 16);
            if (iter % 7 == 0) s = fill.Next() & 0x00ffffff;
        }
        if (iter % 5 == 0) slot[3] = slot[iter % 3];
        const uint32_t expected = original(slot[0], slot[1], slot[2], slot[3]);
        const uint32_t actual = gimex::fsh::NearestOfThree(slot[0], slot[1], slot[2], slot[3]);
        ++ctx.cases;
        if (expected != actual) ctx.Fail("nearest of three %08x %08x %08x %08x: %u vs %u", slot[0], slot[1], slot[2], slot[3], expected, actual);
    }
}

DIFF_TEST(GimexFshEncodeColour_Error, kColourError)
{
    const ErrorFn original = ctx.Original<ErrorFn>(kColourError);
    Fill fill(0x1003);
    for (int iter = 0; iter < 100000; ++iter) {
        uint8_t palette[16 * 4];
        uint8_t chosen[16 * 3];
        for (auto &b : palette) b = static_cast<uint8_t>(fill.Next());
        for (auto &b : chosen) b = static_cast<uint8_t>(fill.Next());
        if (iter % 3 == 0)
            for (int i = 0; i < 16; ++i) palette[i * 4 + 3] = static_cast<uint8_t>(1 + fill.Next() % 16);
        const uint8_t count = static_cast<uint8_t>(fill.Next() % 17);
        const uint32_t expected = original(palette, chosen, count);
        const uint32_t actual = gimex::fsh::ColourError(palette, chosen, count);
        ++ctx.cases;
        if (expected != actual) ctx.Fail("colour error count %u: %u vs %u", count, expected, actual);
    }
}

DIFF_TEST(GimexFshEncodeColour_Blocks, kEncodeColourBlocks)
{
    const BlocksFn original = ctx.Original<BlocksFn>(kEncodeColourBlocks);
    Fill fill(0x1004);
    std::vector<uint32_t> pixels;
    for (int iter = 0; iter < 4000; ++iter) {
        const uint32_t width = (fill.Next() % 5) * 4; // a width that is no multiple of 4 reads the previous block's leftover index slots (the only caller passes 4)
        const uint32_t rows = fill.Next() % 14;
        // Opaque colours or two at most: the blocks the original reads no leftovers for (it runs the blocks one after the other).
        const Shape shape = kQuiet[fill.Next() % 5];
        MakePixels(fill, shape, pixels, static_cast<int32_t>(width), static_cast<int32_t>(rows), static_cast<int32_t>(width));
        const std::vector<uint32_t> before = pixels;
        const size_t bytes = ((width + 3) / 4) * ((rows + 3) / 4) * 8 + 16;
        std::vector<uint8_t> expected(bytes, 0xcc);
        std::vector<uint8_t> actual(bytes, 0xcc);
        ClearStack();
        original(pixels.data(), expected.data(), width, rows);
        ClearStack();
        gimex::fsh::EncodeColourBlocks(pixels.data(), actual.data(), width, rows);
        ++ctx.cases;
        if (expected != actual || pixels != before) ctx.Fail("blocks %ux%u shape %d iter %d", width, rows, static_cast<int>(shape), iter);
    }
}

DIFF_TEST(GimexFshEncodeColour_Record, kEncodeColourRecord)
{
    const RecordFn original = ctx.Original<RecordFn>(kEncodeColourRecord);
    Fill fill(0x1005);
    std::vector<uint32_t> pixels;
    static const uint32_t kFlagSets[] = {0, 4, 8, 0x100, 0x104, 0x200, 0x204, 0x108, 0x300, 0x10, 0xffffffff, 0x8000, 0xc};
    for (int iter = 0; iter < 3000; ++iter) {
        const int32_t size = static_cast<int32_t>(fill.Next() % 400); // negative sizes make the original loop over a huge unsigned row count (kept out)
        const int32_t rows = (size < 0 ? 0 : size) / 16;
        const Shape shape = kQuiet[fill.Next() % 5];
        MakePixels(fill, shape, pixels, 4, rows + 4, 4);
        const std::vector<uint32_t> before = pixels;
        uint32_t flagWord = kFlagSets[fill.Next() % (sizeof kFlagSets / sizeof kFlagSets[0])];
        const uint32_t *flags = (iter % 9 == 0) ? nullptr : &flagWord;
        std::vector<uint8_t> expected(2048, 0xcc);
        std::vector<uint8_t> actual(2048, 0xcc);
        ClearStack();
        const int32_t expectedReturn = original(expected.data(), pixels.data(), size, flags);
        ClearStack();
        const int32_t actualReturn = gimex::fsh::EncodeColourRecord(actual.data(), pixels.data(), size, flags);
        ++ctx.cases;
        if (expectedReturn != actualReturn || expected != actual || pixels != before)
            ctx.Fail("record size %d flags %08x iter %d: return %d vs %d", size, flags ? *flags : 0, iter, expectedReturn, actualReturn);
    }
}

// Sizes above 16,000,000 take the wide header (the size written big-endian in four bytes, the flag byte after it).
DIFF_TEST(GimexFshEncodeColour_RecordWide, kEncodeColourRecord)
{
    const RecordFn original = ctx.Original<RecordFn>(kEncodeColourRecord);
    static const uint32_t kFlagSets[] = {0, 0x100, 0x200, 8, 4, 0x104};
    static const int32_t kSizes[] = {16000000, 16000001, 16000016, 16000240};
    for (const int32_t size : kSizes) {
        for (const uint32_t flagWord : kFlagSets) {
            const int32_t rows = size / 16;
            std::vector<uint32_t> pixels(static_cast<size_t>(rows + 4) * 4, 0xff336699);
            for (size_t i = 0; i < pixels.size(); i += 7) pixels[i] = 0xff9966cc;
            const size_t bytes = static_cast<size_t>((rows + 3) / 4) * 8 + 64;
            std::vector<uint8_t> expected(bytes, 0xcc);
            std::vector<uint8_t> actual(bytes, 0xcc);
            ClearStack();
            const int32_t expectedReturn = original(expected.data(), pixels.data(), size, &flagWord);
            ClearStack();
            const int32_t actualReturn = gimex::fsh::EncodeColourRecord(actual.data(), pixels.data(), size, &flagWord);
            ++ctx.cases;
            if (expectedReturn != actualReturn || expected != actual)
                ctx.Fail("wide record size %d flags %08x: return %d vs %d", size, flagWord, expectedReturn, actualReturn);
        }
    }
}
