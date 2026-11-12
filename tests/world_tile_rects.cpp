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

// ---- lane RZ: 0x7eec00, 0x7eec60, 0x7eed90, 0x7eedc0, 0x7eee50, 0x7eef00, 0x7ef0c0, 0x7ef1d0 ----

namespace {

// A rect whose two points are often a unit apart on an axis or diagonal (so every mask and kind of CellBetween turns up), else
// any pair of triples.
void Segment(std::mt19937 &r, float *rect)
{
    Triple(r, rect);
    Triple(r, rect + 3);
    if (r() % 4 != 0) {
        const float steps[] = {-1.0f, 0.0f, 1.0f};
        rect[3] = rect[0] + steps[r() % 3];
        rect[4] = rect[1] + steps[r() % 3];
        if (r() % 8 == 0) rect[3] = rect[0] + (r() % 2 ? 1.0000001f : 0.9999999f);
        if (r() % 8 == 0) rect[4] = rect[1] + FromBits(0x3f800000u + (r() % 80) - 40);
    }
}

// A cell: every mask (and some that name nothing) with every kind.
void Cell(std::mt19937 &r, int32_t *cell)
{
    const int32_t masks[] = {1, 2, 4, 8, 0x10, 0x20, 0, 3, 0x40, -1};
    cell[0] = static_cast<int32_t>(r() % 9) - 4;
    cell[1] = static_cast<int32_t>(r() % 9) - 4;
    cell[2] = static_cast<int32_t>(Level(r));
    cell[3] = masks[r() % 10];
    cell[4] = static_cast<int32_t>(r() % 7) - 1;
    if (r() % 10 == 0) cell[0] = static_cast<int32_t>(r()), cell[1] = static_cast<int32_t>(r());
}

} // namespace

DIFF_TEST(WorldTileRects_CellToQuadrants, 0x7eec60)
{
    using Q = worldmath::TileQuadrant;
    auto original = ctx.Original<Q *(__cdecl *)(Q *, const int32_t *)>(0x7eec60);
    std::mt19937 r(0x7eec60);
    for (int i = 0; i < 20000; ++i) {
        int32_t cell[5];
        Cell(r, cell);
        Words<4> x, y;
        Q *rx = original(reinterpret_cast<Q *>(x.w), cell);
        Q *ry = worldrect::CellToQuadrants(reinterpret_cast<Q *>(y.w), cell);
        ++ctx.cases;
        if (rx != reinterpret_cast<Q *>(x.w) || ry != reinterpret_cast<Q *>(y.w)) ctx.Fail("case %d: return value", i);
        if (std::memcmp(&x, &y, sizeof(x)) != 0) ctx.Fail("case %d: mask %#x kind %d", i, cell[3], cell[4]);
    }
}

DIFF_TEST(WorldTileRects_SegmentToQuadrants, 0x7eed90)
{
    using Q = worldmath::TileQuadrant;
    using Fn = Q *(__cdecl *)(Q *, const float *);
    const struct {
        uint32_t rva;
        Fn ours;
        const char *name;
    } kFns[] = {{0x7eed90, &worldrect::SegmentToQuadrants, "SegmentToQuadrants"},
                {0x7eec00, &worldrect::ReversedSegmentToQuadrants, "ReversedSegmentToQuadrants"}};
    std::mt19937 r(0x7eed90);
    for (int i = 0; i < 20000; ++i) {
        float rect[6];
        Segment(r, rect);
        for (const auto &f : kFns) {
            Words<4> x, y;
            Q *rx = ctx.Original<Fn>(f.rva)(reinterpret_cast<Q *>(x.w), rect);
            Q *ry = f.ours(reinterpret_cast<Q *>(y.w), rect);
            ++ctx.cases;
            if (rx != reinterpret_cast<Q *>(x.w) || ry != reinterpret_cast<Q *>(y.w)) ctx.Fail("case %d: %s return value", i, f.name);
            if (std::memcmp(&x, &y, sizeof(x)) != 0) ctx.Fail("case %d: %s rect %g %g %g %g", i, f.name, rect[0], rect[1], rect[3], rect[4]);
        }
    }
}

DIFF_TEST(WorldTileRects_IsUnitSegment, 0x7eedc0)
{
    auto original = ctx.Original<uint32_t(__cdecl *)(const float *)>(0x7eedc0);
    std::mt19937 r(0x7eedc0);
    for (int i = 0; i < 40000; ++i) {
        float rect[6];
        Segment(r, rect);
        if (r() % 3 == 0) {
            // Distances right at the 32-ulp edge on each side of 1.0 (and of -1.0).
            const int32_t d = static_cast<int32_t>(r() % 80) - 40;
            rect[3] = rect[0] - (r() % 2 ? 1.0f : -1.0f) * FromBits(0x3f800000u + d);
            if (r() % 2) rect[0] = 0.0f, rect[3] = (r() % 2 ? 1.0f : -1.0f) * FromBits(0x3f800000u + d);
            if (r() % 2) rect[1] = 0.0f, rect[4] = (r() % 2 ? 1.0f : -1.0f) * FromBits(0x3f800000u + static_cast<int32_t>(r() % 80) - 40);
        }
        const uint32_t ra = original(rect) & 0xff;
        const uint32_t rb = worldrect::IsUnitSegment(rect) ? 1u : 0u;
        ++ctx.cases;
        if (ra != rb) ctx.Fail("case %d: rect %.9g %.9g %.9g %.9g: %u vs %u", i, rect[0], rect[1], rect[3], rect[4], ra, rb);
    }
}

DIFF_TEST(WorldTileRects_RotateRect, 0x7eee50)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, const float *, const int32_t *)>(0x7eee50);
    std::mt19937 r(0x7eee50);
    for (int i = 0; i < 20000; ++i) {
        float rect[6];
        Segment(r, rect);
        const int32_t facing = r() % 4 ? static_cast<int32_t>(r() % 9) - 1 : static_cast<int32_t>(r());
        Words<6> x, y;
        float *rx = original(reinterpret_cast<float *>(x.w), rect, &facing);
        float *ry = worldrect::RotateRect(reinterpret_cast<float *>(y.w), rect, &facing);
        ++ctx.cases;
        if (rx != reinterpret_cast<float *>(x.w) || ry != reinterpret_cast<float *>(y.w)) ctx.Fail("case %d: return value", i);
        if (std::memcmp(&x, &y, sizeof(x)) != 0) ctx.Fail("case %d: facing %d rect %g %g %g %g", i, facing, rect[0], rect[1], rect[3], rect[4]);
    }
}

DIFF_TEST(WorldTileRects_EdgeEndpoints, 0x7ef0c0)
{
    auto original = ctx.Original<void(__cdecl *)(const int32_t *, float *, float *)>(0x7ef0c0);
    std::mt19937 r(0x7ef0c0);
    for (int i = 0; i < 20000; ++i) {
        int32_t cell[5];
        Cell(r, cell);
        Words<3> xa, xb, ya, yb;
        original(cell, reinterpret_cast<float *>(xa.w), reinterpret_cast<float *>(xb.w));
        worldrect::EdgeEndpoints(cell, reinterpret_cast<float *>(ya.w), reinterpret_cast<float *>(yb.w));
        ++ctx.cases;
        if (std::memcmp(&xa, &ya, sizeof(xa)) != 0 || std::memcmp(&xb, &yb, sizeof(xb)) != 0)
            ctx.Fail("case %d: cell %d %d mask %#x kind %d", i, cell[0], cell[1], cell[3], cell[4]);
    }
}

DIFF_TEST(WorldTileRects_CellToSegment, 0x7ef1d0)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, const int32_t *)>(0x7ef1d0);
    std::mt19937 r(0x7ef1d0);
    for (int i = 0; i < 20000; ++i) {
        int32_t cell[5];
        Cell(r, cell);
        Words<6> x, y;
        float *rx = original(reinterpret_cast<float *>(x.w), cell);
        float *ry = worldrect::CellToSegment(reinterpret_cast<float *>(y.w), cell);
        ++ctx.cases;
        if (rx != reinterpret_cast<float *>(x.w) || ry != reinterpret_cast<float *>(y.w)) ctx.Fail("case %d: return value", i);
        if (std::memcmp(&x, &y, sizeof(x)) != 0) ctx.Fail("case %d: cell %d %d mask %#x kind %d", i, cell[0], cell[1], cell[3], cell[4]);
    }
}

DIFF_TEST(WorldTileRects_CanonicalSegment, 0x7eef00)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, const float *)>(0x7eef00);
    std::mt19937 r(0x7eef00);
    for (int i = 0; i < 20000; ++i) {
        float rect[6];
        Segment(r, rect);
        Words<6> x, y;
        float *rx = original(reinterpret_cast<float *>(x.w), rect);
        float *ry = worldrect::CanonicalSegment(reinterpret_cast<float *>(y.w), rect);
        ++ctx.cases;
        if (rx != reinterpret_cast<float *>(x.w) || ry != reinterpret_cast<float *>(y.w)) ctx.Fail("case %d: return value", i);
        if (std::memcmp(&x, &y, sizeof(x)) != 0) ctx.Fail("case %d: rect %g %g %g %g", i, rect[0], rect[1], rect[3], rect[4]);
    }
}
