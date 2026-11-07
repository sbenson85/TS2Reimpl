// Differential tests for the tile rects of src/world/WorldTileRects.* (lane RY, 0x7ef060, 0x7ef260, 0x7ef3b0). Floats are
// compared bit for bit; outputs are canary-filled; the return value (the output pointer, left in eax) is compared.
#include "difftest.h"

#include "world/WorldTileRects.h"

#include <cstdint>
#include <cstring>
#include <random>

namespace {

float FromBits(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

// A coordinate: often from a small set so that equal and neighbouring values are common; sometimes any bit pattern.
float Coord(std::mt19937 &r)
{
    switch (r() % 6) {
    case 0: return FromBits(r());
    case 1: {
        const uint32_t edges[] = {0u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x7f800000u, 0xff800000u, 0x7fc00000u, 0x4f000000u, 0xcf000000u, 0x3f000000u};
        return FromBits(edges[r() % 10]);
    }
    case 2:
    case 3: return static_cast<float>(static_cast<int32_t>(r() % 7) - 3) + (r() % 2 ? 0.0f : 0.5f);
    default: return static_cast<float>(static_cast<int32_t>(r() % 40001) - 20000) / 100.0f;
    }
}

uint32_t Level(std::mt19937 &r) { return r() % 3 ? r() % 40 : r(); }

template <size_t N> struct Words {
    uint32_t before = 0x5e5e5e5eu;
    uint32_t w[N];
    uint32_t after = 0xa5a5a5a5u;
    Words() { for (auto &x : w) x = 0xc3c3c3c3u; }
};

void Triple(std::mt19937 &r, float *t)
{
    t[0] = Coord(r);
    t[1] = Coord(r);
    const uint32_t l = Level(r);
    std::memcpy(&t[2], &l, 4);
}

} // namespace

DIFF_TEST(WorldTileRects_CellBetween, 0x7ef260)
{
    auto original = ctx.Original<int32_t *(__cdecl *)(int32_t *, const float *, const float *)>(0x7ef260);
    std::mt19937 r(0x7ef260);
    for (int i = 0; i < 20000; ++i) {
        float a[3], b[3];
        Triple(r, a);
        Triple(r, b);
        if (r() % 3 == 0) b[0] = a[0];
        if (r() % 3 == 0) b[1] = a[1];
        Words<5> x, y;
        int32_t *rx = original(reinterpret_cast<int32_t *>(x.w), a, b);
        int32_t *ry = worldrect::CellBetween(reinterpret_cast<int32_t *>(y.w), a, b);
        ++ctx.cases;
        if (rx != reinterpret_cast<int32_t *>(x.w) || ry != reinterpret_cast<int32_t *>(y.w)) ctx.Fail("case %d: return value", i);
        if (std::memcmp(&x, &y, sizeof(x)) != 0) ctx.Fail("case %d: a %g %g b %g %g", i, a[0], a[1], b[0], b[1]);
    }
}

DIFF_TEST(WorldTileRects_CellOfRect, 0x7ef3b0)
{
    auto original = ctx.Original<int32_t *(__cdecl *)(int32_t *, const float *)>(0x7ef3b0);
    std::mt19937 r(0x7ef3b0);
    for (int i = 0; i < 20000; ++i) {
        float rect[6];
        Triple(r, rect);
        Triple(r, rect + 3);
        if (r() % 3 == 0) rect[3] = rect[0];
        if (r() % 3 == 0) rect[4] = rect[1];
        Words<5> x, y;
        int32_t *rx = original(reinterpret_cast<int32_t *>(x.w), rect);
        int32_t *ry = worldrect::CellOfRect(reinterpret_cast<int32_t *>(y.w), rect);
        ++ctx.cases;
        if (rx != reinterpret_cast<int32_t *>(x.w) || ry != reinterpret_cast<int32_t *>(y.w)) ctx.Fail("case %d: return value", i);
        if (std::memcmp(&x, &y, sizeof(x)) != 0) ctx.Fail("case %d: rect %g %g %g %g", i, rect[0], rect[1], rect[3], rect[4]);
    }
}

DIFF_TEST(WorldTileRects_OffsetRect, 0x7ef060)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, const float *, const float *)>(0x7ef060);
    std::mt19937 r(0x7ef060);
    for (int i = 0; i < 20000; ++i) {
        float rect[6], p[3];
        Triple(r, rect);
        Triple(r, rect + 3);
        Triple(r, p);
        Words<6> x, y;
        float *rx = original(reinterpret_cast<float *>(x.w), rect, p);
        float *ry = worldrect::OffsetRect(reinterpret_cast<float *>(y.w), rect, p);
        ++ctx.cases;
        if (rx != reinterpret_cast<float *>(x.w) || ry != reinterpret_cast<float *>(y.w)) ctx.Fail("case %d: return value", i);
        if (std::memcmp(&x, &y, sizeof(x)) != 0) ctx.Fail("case %d: rect %g %g %g %g p %g %g", i, rect[0], rect[1], rect[3], rect[4], p[0], p[1]);
    }
}
