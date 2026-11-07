// Differential tests for the memory mip chain's pixel kernels and DXT block decoders (lane OW, src/gz/MipKernels.cpp,
// src/gz/DxtBlocks.cpp): random buffers (and extreme fills so every carry path runs), original against ours; the
// destination is surrounded by canary bytes and compared whole, so an overrun or a short write fails.
// Every kernel is cdecl (dst, src-or-row-table, count), so they share one calling type here.
#include "difftest.h"

#include "gz/DxtBlocks.h"
#include "gz/MipKernels.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr size_t kPad = 16;
using Kernel = void(__cdecl *)(void *, const void *, int);

std::vector<uint8_t> RandomBytes(std::mt19937 &rng, size_t n, int mode)
{
    std::vector<uint8_t> v(n);
    for (auto &b : v) {
        const uint32_t r = rng();
        b = mode == 0 ? uint8_t(r) : mode == 1 ? uint8_t((r & 1) ? 0xff : 0x00) : mode == 2 ? uint8_t(0x7f + (r & 3) - 1) : uint8_t(r & 0x81);
    }
    return v;
}

std::vector<uint8_t> Canvas(size_t bytes) { return std::vector<uint8_t>(bytes + 2 * kPad, 0xa5); }

// Row reducers: count output pixels from 2 * count inputs (plus slack the filtered kernels read past).
void RowTest(difftest::Context &ctx, uint32_t rva, Kernel ours, size_t pixel, uint32_t seed)
{
    Kernel orig = ctx.Original<Kernel>(rva);
    std::mt19937 rng(seed);
    for (int i = 0; i < 300; ++i) {
        ++ctx.cases;
        const int count = 1 + (i % 17) + (i >= 200 ? 40 : 0);
        const auto src = RandomBytes(rng, (size_t(count) * 2 + 4) * pixel, i % 4);
        auto a = Canvas(size_t(count) * pixel), b = a;
        orig(a.data() + kPad, src.data(), count);
        ours(b.data() + kPad, src.data(), count);
        if (a != b) {
            ctx.Fail("case %d (count %d): output differs", i, count);
            return;
        }
    }
}

// Row combiners: `rows` points at four row pointers; each row holds count pixels.
void CombineTest(difftest::Context &ctx, uint32_t rva, Kernel ours, size_t pixel, uint32_t seed)
{
    Kernel orig = ctx.Original<Kernel>(rva);
    std::mt19937 rng(seed);
    for (int i = 0; i < 300; ++i) {
        ++ctx.cases;
        const int count = 1 + (i % 17) + (i >= 200 ? 40 : 0);
        std::vector<uint8_t> rowData[4];
        const uint8_t *rows[4];
        for (int r = 0; r < 4; ++r) {
            rowData[r] = RandomBytes(rng, size_t(count) * pixel, (i + r) % 4);
            rows[r] = rowData[r].data();
        }
        auto a = Canvas(size_t(count) * pixel), b = a;
        orig(a.data() + kPad, rows, count);
        ours(b.data() + kPad, rows, count);
        if (a != b) {
            ctx.Fail("case %d (count %d): output differs", i, count);
            return;
        }
    }
}

#define ROW_TEST(Name, Rva, Pixel)                                                                                  \
    DIFF_TEST(MipKernels_##Name, Rva)                                                                               \
    {                                                                                                               \
        RowTest(ctx, Rva, reinterpret_cast<Kernel>(&nGZGraphic4::mip::Name), Pixel, Rva);                           \
    }
#define COMBINE_TEST(Name, Rva, Pixel)                                                                              \
    DIFF_TEST(MipKernels_##Name, Rva)                                                                               \
    {                                                                                                               \
        CombineTest(ctx, Rva, reinterpret_cast<Kernel>(&nGZGraphic4::mip::Name), Pixel, Rva);                       \
    }

} // namespace

ROW_TEST(HalveRow8888, 0x185280, 4)
ROW_TEST(FilterRow8888, 0x1852c0, 4)
ROW_TEST(HalveRow2101010, 0x185100, 4)
ROW_TEST(FilterRow2101010, 0x185140, 4)
ROW_TEST(HalveRow8, 0x185400, 1)
ROW_TEST(FilterRow8, 0x185430, 1)
COMBINE_TEST(CombineTwoRows8888, 0x1862a0, 4)
COMBINE_TEST(CombineFourRows8888, 0x1862e0, 4)
COMBINE_TEST(CombineTwoRows2101010, 0x1861c0, 4)
COMBINE_TEST(CombineFourRows2101010, 0x186200, 4)
COMBINE_TEST(CombineTwoRows8, 0x186380, 1)
COMBINE_TEST(CombineFourRows8, 0x1863c0, 1)

namespace {

using Decoder = void(__cdecl *)(uint8_t *, int, const uint8_t *, int, int, int);

void DecoderTest(difftest::Context &ctx, uint32_t rva, Decoder ours, int blockBytes, uint32_t seed)
{
    Decoder orig = ctx.Original<Decoder>(rva);
    std::mt19937 rng(seed);
    for (int i = 0; i < 400; ++i) {
        ++ctx.cases;
        const int wide = (i % 7 == 0) ? 0 : 1 + int(rng() % 5), high = (i % 11 == 0) ? 0 : 1 + int(rng() % 4);
        const int sourcePitch = wide * blockBytes + int(rng() % 3) * 8;
        const int targetPitch = wide * 16 + int(rng() % 3) * 4;
        auto source = RandomBytes(rng, size_t(sourcePitch) * high + 16, i % 5 == 4 ? 1 : 0);
        // Force the DXT1 endpoint order and DXT5 alpha-endpoint order both ways.
        if (i % 3 == 0 && source.size() >= 4) std::swap(source[0], source[2]);
        const size_t targetBytes = size_t(targetPitch) * high * 4 + 16;
        auto a = Canvas(targetBytes), b = a;
        orig(a.data() + kPad, targetPitch, source.data(), sourcePitch, wide, high);
        ours(b.data() + kPad, targetPitch, source.data(), sourcePitch, wide, high);
        if (a != b) {
            ctx.Fail("case %d (%d x %d blocks): output differs", i, wide, high);
            return;
        }
    }
}

} // namespace

DIFF_TEST(DxtBlocks_DecodeDxt1, 0x1837f0) { DecoderTest(ctx, 0x1837f0, &nGZGraphic4::dxt::DecodeDxt1, 8, 0x1837f0); }
DIFF_TEST(DxtBlocks_DecodeDxt3, 0x1839e0) { DecoderTest(ctx, 0x1839e0, &nGZGraphic4::dxt::DecodeDxt3, 16, 0x1839e0); }
DIFF_TEST(DxtBlocks_DecodeDxt5, 0x183dd0) { DecoderTest(ctx, 0x183dd0, &nGZGraphic4::dxt::DecodeDxt5, 16, 0x183dd0); }

DIFF_TEST(DxtBlocks_PaintBlock, 0x186420)
{
    using Fn = void(__stdcall *)(uint8_t *, int, uint32_t, const uint32_t *);
    Fn orig = ctx.Original<Fn>(0x186420);
    std::mt19937 rng(0x186420);
    for (int i = 0; i < 200; ++i) {
        ++ctx.cases;
        const int pitch = 16 + int(rng() % 4) * 4;
        uint32_t palette[4];
        for (auto &p : palette) p = rng();
        const uint32_t indices = rng();
        auto a = Canvas(size_t(pitch) * 4), b = a;
        orig(a.data() + kPad, pitch, indices, palette);
        nGZGraphic4::dxt::PaintBlock(b.data() + kPad, pitch, indices, palette);
        if (a != b) {
            ctx.Fail("case %d: output differs", i);
            return;
        }
    }
}
