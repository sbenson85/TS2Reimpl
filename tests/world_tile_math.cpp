// Differential tests for the tile maths of src/world/WorldTileMath.* (lane RY, 0x7dcb90-0x7ddea0): conversions between tile
// bytes, 16.16 values, tiles, positions and quadrant bytes. Floats are compared bit for bit. The ones that ask the world
// layer for a ground height are in tests/world_tile_ground.cpp (they need the fake world DB).
#include "difftest.h"

#include "world/WorldTileMath.h"

#include <cstdint>
#include <cstring>
#include <random>

namespace {

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}
float FromBits(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

// A float: any bit pattern, an edge value, or an ordinary position-like number.
float RandomFloat(std::mt19937 &r)
{
    switch (r() % 8) {
    case 0: return FromBits(r());
    case 1: {
        const uint32_t edges[] = {0u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x7f800000u, 0xff800000u, 0x7fc00000u, 0x00000001u, 0x007fffffu, 0x7f7fffffu, 0x3f000000u, 0xbf000000u, 0x3fc00000u};
        return FromBits(edges[r() % 13]);
    }
    case 2: return static_cast<float>(static_cast<int32_t>(r() % 41) - 20) * 0.5f;
    case 3: return static_cast<float>(static_cast<int32_t>(r() % 4001) - 2000) / 100.0f;
    default: return static_cast<float>(static_cast<int32_t>(r() % 400001) - 200000) / 1000.0f;
    }
}

// An angle: ordinary, multiples of pi/4 and a few edges (not infinities: the original's loops would not end).
float RandomAngle(std::mt19937 &r)
{
    switch (r() % 6) {
    case 0: return (static_cast<int32_t>(r() % 129) - 64) * 0.78539818525f;
    case 1: return (static_cast<int32_t>(r() % 129) - 64) * 0.78539818525f + (static_cast<int32_t>(r() % 3) - 1) * 1e-6f;
    case 2: return FromBits(0x7fc00000u);
    default: return (static_cast<int32_t>(r() % 200001) - 100000) / 1000.0f;
    }
}

int32_t RandomInt(std::mt19937 &r)
{
    switch (r() % 4) {
    case 0: return static_cast<int32_t>(r());
    case 1: return static_cast<int32_t>(r() % 41) - 20;
    case 2: return static_cast<int32_t>(r() % 65536) - 32768;
    default: return static_cast<int32_t>(r() % 2001) - 1000;
    }
}

template <size_t N> struct Words {
    uint32_t before = 0x5e5e5e5eu;
    uint32_t w[N];
    uint32_t after = 0xa5a5a5a5u;
    Words() { for (auto &x : w) x = 0xc3c3c3c3u; }
};

} // namespace

#define COMPARE_WORDS(a, b, fmt, ...) \
    do { \
        ++ctx.cases; \
        if (std::memcmp(&(a), &(b), sizeof(a)) != 0) ctx.Fail("case %d: " fmt, i, __VA_ARGS__); \
    } while (0)

DIFF_TEST(WorldTileMath_TileBytesToFloats, 0x7dcbd0)
{
    auto original = ctx.Original<void(__cdecl *)(float *, const int8_t *)>(0x7dcbd0);
    std::mt19937 r(0x7dcbd0);
    for (int i = 0; i < 4000; ++i) {
        int8_t bytes[4] = {static_cast<int8_t>(r()), static_cast<int8_t>(r()), static_cast<int8_t>(r()), static_cast<int8_t>(r())};
        Words<3> a, b;
        original(reinterpret_cast<float *>(a.w), bytes);
        worldmath::TileBytesToFloats(reinterpret_cast<float *>(b.w), bytes);
        COMPARE_WORDS(a, b, "bytes %d %d %d", bytes[0], bytes[1], bytes[2]);
    }
}

DIFF_TEST(WorldTileMath_Fixed16ToFloats, 0x7dcc90)
{
    auto original = ctx.Original<void(__cdecl *)(float *, const int32_t *)>(0x7dcc90);
    std::mt19937 r(0x7dcc90);
    for (int i = 0; i < 4000; ++i) {
        int32_t in[2] = {RandomInt(r), RandomInt(r)};
        Words<2> a, b;
        original(reinterpret_cast<float *>(a.w), in);
        worldmath::Fixed16ToFloats(reinterpret_cast<float *>(b.w), in);
        COMPARE_WORDS(a, b, "in %d %d", in[0], in[1]);
    }
}

DIFF_TEST(WorldTileMath_FacingQuaternion, 0x7dce00)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, int32_t)>(0x7dce00);
    std::mt19937 r(0x7dce00);
    for (int i = 0; i < 4000; ++i) {
        const int32_t facing = RandomInt(r);
        Words<4> a, b;
        float *ra = original(reinterpret_cast<float *>(a.w), facing);
        float *rb = worldmath::FacingQuaternion(reinterpret_cast<float *>(b.w), facing);
        if (ra != reinterpret_cast<float *>(a.w) || rb != reinterpret_cast<float *>(b.w)) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "facing %d", facing);
    }
}

DIFF_TEST(WorldTileMath_FacingAngle, 0x7dcee0)
{
    auto original = ctx.Original<float(__cdecl *)(int32_t)>(0x7dcee0);
    std::mt19937 r(0x7dcee0);
    for (int i = 0; i < 4000; ++i) {
        const int32_t facing = RandomInt(r);
        const float a = original(facing), b = worldmath::FacingAngle(facing);
        ++ctx.cases;
        if (Bits(a) != Bits(b)) ctx.Fail("case %d: facing %d: %08x vs %08x", i, facing, Bits(a), Bits(b));
    }
}

DIFF_TEST(WorldTileMath_FacingToNotch, 0x7dcf10)
{
    auto original = ctx.Original<int32_t(__cdecl *)(int32_t)>(0x7dcf10);
    std::mt19937 r(0x7dcf10);
    for (int i = 0; i < 4000; ++i) {
        const int32_t facing = RandomInt(r);
        ++ctx.cases;
        if (original(facing) != worldmath::FacingToNotch(facing)) ctx.Fail("case %d: facing %d", i, facing);
    }
}

DIFF_TEST(WorldTileMath_FacingPtrToNotch, 0x7dd090)
{
    auto original = ctx.Original<int32_t(__cdecl *)(const int32_t *)>(0x7dd090);
    std::mt19937 r(0x7dd090);
    for (int i = 0; i < 4000; ++i) {
        const int32_t facing = RandomInt(r);
        ++ctx.cases;
        if (original(&facing) != worldmath::FacingPtrToNotch(&facing)) ctx.Fail("case %d: facing %d", i, facing);
    }
}

DIFF_TEST(WorldTileMath_AngleToNotch, 0x7dcf30)
{
    auto original = ctx.Original<int32_t(__cdecl *)(float)>(0x7dcf30);
    std::mt19937 r(0x7dcf30);
    for (int i = 0; i < 20000; ++i) {
        const float angle = RandomAngle(r);
        ++ctx.cases;
        const int32_t a = original(angle), b = worldmath::AngleToNotch(angle);
        if (a != b) ctx.Fail("case %d: angle %08x: %d vs %d", i, Bits(angle), a, b);
    }
}

DIFF_TEST(WorldTileMath_AngleToFacing, 0x7dcfe0)
{
    auto original = ctx.Original<int32_t(__cdecl *)(float)>(0x7dcfe0);
    std::mt19937 r(0x7dcfe0);
    for (int i = 0; i < 20000; ++i) {
        const float angle = RandomAngle(r);
        ++ctx.cases;
        // The result is the low 16 bits of eax; what the high half holds is whatever the compiler left.
        const int16_t a = static_cast<int16_t>(original(angle)), b = worldmath::AngleToFacing(angle);
        if (a != b) ctx.Fail("case %d: angle %08x: %d vs %d", i, Bits(angle), a, b);
    }
}

DIFF_TEST(WorldTileMath_TileToBytes, 0x7dd0b0)
{
    auto original = ctx.Original<void *(__cdecl *)(int8_t *, const int32_t *)>(0x7dd0b0);
    std::mt19937 r(0x7dd0b0);
    for (int i = 0; i < 4000; ++i) {
        const int32_t tile[3] = {RandomInt(r), RandomInt(r), RandomInt(r)};
        Words<1> a, b;
        void *ra = original(reinterpret_cast<int8_t *>(a.w), tile);
        void *rb = worldmath::TileToBytes(reinterpret_cast<int8_t *>(b.w), tile);
        // Three bytes are written; the fourth is whatever the buffer held.
        if (ra != static_cast<void *>(a.w) || rb != static_cast<void *>(b.w)) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "tile %d %d %d", tile[0], tile[1], tile[2]);
    }
}

DIFF_TEST(WorldTileMath_FloatsToFixed16, 0x7dd120)
{
    auto original = ctx.Original<void *(__cdecl *)(int32_t *, const float *)>(0x7dd120);
    std::mt19937 r(0x7dd120);
    for (int i = 0; i < 6000; ++i) {
        const float in[2] = {RandomFloat(r), RandomFloat(r)};
        Words<2> a, b;
        void *ra = original(reinterpret_cast<int32_t *>(a.w), in);
        void *rb = worldmath::FloatsToFixed16(reinterpret_cast<int32_t *>(b.w), in);
        if (ra != static_cast<void *>(a.w) || rb != static_cast<void *>(b.w)) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "in %08x %08x", Bits(in[0]), Bits(in[1]));
    }
}

DIFF_TEST(WorldTileMath_NeighbourTile, 0x7dd1e0)
{
    auto original = ctx.Original<void *(__cdecl *)(int32_t *, const int32_t *, uint32_t)>(0x7dd1e0);
    std::mt19937 r(0x7dd1e0);
    for (int i = 0; i < 6000; ++i) {
        const int32_t tile[3] = {RandomInt(r), RandomInt(r), RandomInt(r)};
        const uint32_t direction = i % 3 == 0 ? r() % 256 : r() % 16;
        Words<3> a, b;
        void *ra = original(reinterpret_cast<int32_t *>(a.w), tile, direction);
        void *rb = worldmath::NeighbourTile(reinterpret_cast<int32_t *>(b.w), tile, static_cast<uint8_t>(direction));
        if (ra != static_cast<void *>(a.w) || rb != static_cast<void *>(b.w)) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "direction %u", direction);
    }
}

DIFF_TEST(WorldTileMath_CornerTile, 0x7dda90)
{
    auto original = ctx.Original<void *(__cdecl *)(int32_t *, const int32_t *, int32_t)>(0x7dda90);
    std::mt19937 r(0x7dda90);
    for (int i = 0; i < 6000; ++i) {
        const int32_t tile[3] = {RandomInt(r), RandomInt(r), RandomInt(r)};
        const int32_t corner = i % 4 == 0 ? RandomInt(r) : static_cast<int32_t>(r() % 10);
        Words<3> a, b;
        void *ra = original(reinterpret_cast<int32_t *>(a.w), tile, corner);
        void *rb = worldmath::CornerTile(reinterpret_cast<int32_t *>(b.w), tile, corner);
        if (ra != static_cast<void *>(a.w) || rb != static_cast<void *>(b.w)) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "corner %d", corner);
    }
}

DIFF_TEST(WorldTileMath_TileToFloats, 0x7dde50)
{
    auto original = ctx.Original<void *(__cdecl *)(float *, const int32_t *)>(0x7dde50);
    std::mt19937 r(0x7dde50);
    for (int i = 0; i < 4000; ++i) {
        const int32_t tile[3] = {RandomInt(r), RandomInt(r), RandomInt(r)};
        Words<3> a, b;
        void *ra = original(reinterpret_cast<float *>(a.w), tile);
        void *rb = worldmath::TileToFloats(reinterpret_cast<float *>(b.w), tile);
        if (ra != static_cast<void *>(a.w) || rb != static_cast<void *>(b.w)) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "tile %d %d %d", tile[0], tile[1], tile[2]);
    }
}

DIFF_TEST(WorldTileMath_CornerPosition, 0x7dda60)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, const int32_t *, int32_t)>(0x7dda60);
    std::mt19937 r(0x7dda60);
    for (int i = 0; i < 6000; ++i) {
        const int32_t tile[3] = {RandomInt(r), RandomInt(r), RandomInt(r)};
        const int32_t corner = static_cast<int32_t>(r() % 10);
        Words<3> a, b;
        float *ra = original(reinterpret_cast<float *>(a.w), tile, corner);
        float *rb = worldmath::CornerPosition(reinterpret_cast<float *>(b.w), tile, corner);
        if (ra != reinterpret_cast<float *>(a.w) || rb != reinterpret_cast<float *>(b.w)) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "corner %d", corner);
    }
}

DIFF_TEST(WorldTileMath_SnapToQuarters, 0x7dd470)
{
    auto original = ctx.Original<void(__cdecl *)(float *, const float *)>(0x7dd470);
    std::mt19937 r(0x7dd470);
    for (int i = 0; i < 20000; ++i) {
        const float in[3] = {RandomFloat(r), RandomFloat(r), RandomFloat(r)};
        Words<3> a, b;
        original(reinterpret_cast<float *>(a.w), in);
        worldmath::SnapToQuarters(reinterpret_cast<float *>(b.w), in);
        COMPARE_WORDS(a, b, "in %08x %08x %08x", Bits(in[0]), Bits(in[1]), Bits(in[2]));
    }
}

DIFF_TEST(WorldTileMath_RoundPositionDown, 0x7dd870)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, const float *)>(0x7dd870);
    std::mt19937 r(0x7dd870);
    for (int i = 0; i < 20000; ++i) {
        const float in[3] = {RandomFloat(r), RandomFloat(r), RandomFloat(r)};
        Words<3> a, b;
        float *ra = original(reinterpret_cast<float *>(a.w), in);
        float *rb = worldmath::RoundPositionDown(reinterpret_cast<float *>(b.w), in);
        if (ra != reinterpret_cast<float *>(a.w) || rb != reinterpret_cast<float *>(b.w)) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "in %08x %08x %08x", Bits(in[0]), Bits(in[1]), Bits(in[2]));
    }
}

DIFF_TEST(WorldTileMath_RoundPositionToTile, 0x7dd8f0)
{
    auto original = ctx.Original<int32_t *(__cdecl *)(int32_t *, const float *)>(0x7dd8f0);
    std::mt19937 r(0x7dd8f0);
    for (int i = 0; i < 20000; ++i) {
        const float in[3] = {RandomFloat(r), RandomFloat(r), RandomFloat(r)};
        Words<3> a, b;
        int32_t *ra = original(reinterpret_cast<int32_t *>(a.w), in);
        int32_t *rb = worldmath::RoundPositionToTile(reinterpret_cast<int32_t *>(b.w), in);
        if (ra != reinterpret_cast<int32_t *>(a.w) || rb != reinterpret_cast<int32_t *>(b.w)) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "in %08x %08x %08x", Bits(in[0]), Bits(in[1]), Bits(in[2]));
    }
}

DIFF_TEST(WorldTileMath_RoundPositionToTileB, 0x7dddf0)
{
    auto original = ctx.Original<int32_t *(__cdecl *)(int32_t *, const float *)>(0x7dddf0);
    std::mt19937 r(0x7dddf0);
    for (int i = 0; i < 20000; ++i) {
        const float in[3] = {RandomFloat(r), RandomFloat(r), RandomFloat(r)};
        Words<3> a, b;
        int32_t *ra = original(reinterpret_cast<int32_t *>(a.w), in);
        int32_t *rb = worldmath::RoundPositionToTileB(reinterpret_cast<int32_t *>(b.w), in);
        if (ra != reinterpret_cast<int32_t *>(a.w) || rb != reinterpret_cast<int32_t *>(b.w)) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "in %08x %08x %08x", Bits(in[0]), Bits(in[1]), Bits(in[2]));
    }
}

DIFF_TEST(WorldTileMath_PositionToTile, 0x7dd9d0)
{
    auto original = ctx.Original<void *(__cdecl *)(int32_t *, const float *)>(0x7dd9d0);
    std::mt19937 r(0x7dd9d0);
    for (int i = 0; i < 20000; ++i) {
        const float in[3] = {RandomFloat(r), RandomFloat(r), RandomFloat(r)};
        Words<3> a, b;
        void *ra = original(reinterpret_cast<int32_t *>(a.w), in);
        void *rb = worldmath::PositionToTile(reinterpret_cast<int32_t *>(b.w), in);
        if (ra != static_cast<void *>(a.w) || rb != static_cast<void *>(b.w)) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "in %08x %08x %08x", Bits(in[0]), Bits(in[1]), Bits(in[2]));
    }
}

DIFF_TEST(WorldTileMath_PositionToTileQuadrant, 0x7dd6c0)
{
    auto original = ctx.Original<void *(__cdecl *)(void *, const float *)>(0x7dd6c0);
    std::mt19937 r(0x7dd6c0);
    for (int i = 0; i < 20000; ++i) {
        const float in[3] = {RandomFloat(r), RandomFloat(r), RandomFloat(r)};
        Words<4> a, b;
        void *ra = original(a.w, in);
        void *rb = worldmath::PositionToTileQuadrant(reinterpret_cast<worldmath::TileQuadrant *>(b.w), in);
        if (ra != a.w || rb != b.w) ctx.Fail("case %d: return value", i);
        // Only the quadrant byte of the fourth word is written.
        a.w[3] &= 0xff;
        b.w[3] &= 0xff;
        COMPARE_WORDS(a, b, "in %08x %08x %08x", Bits(in[0]), Bits(in[1]), Bits(in[2]));
    }
}

DIFF_TEST(WorldTileMath_QuadrantOfPoint, 0x7ddb80)
{
    auto original = ctx.Original<uint8_t(__cdecl *)(float, float)>(0x7ddb80);
    std::mt19937 r(0x7ddb80);
    for (int i = 0; i < 30000; ++i) {
        float x = RandomFloat(r), y = RandomFloat(r);
        if (i % 3 == 0) {
            // Near and on the diagonals of a tile.
            const float f = static_cast<float>(r() % 17) / 16.0f;
            x = static_cast<float>(static_cast<int32_t>(r() % 9) - 4) + f;
            y = static_cast<float>(static_cast<int32_t>(r() % 9) - 4) + (i % 6 == 0 ? f : 1.0f - f);
        }
        ++ctx.cases;
        const uint8_t a = original(x, y), b = worldmath::QuadrantOfPoint(x, y);
        if (a != b) ctx.Fail("case %d: x %08x y %08x: %u vs %u", i, Bits(x), Bits(y), a, b);
    }
}

DIFF_TEST(WorldTileMath_QuadrantOfPositionXY, 0x7ddc10)
{
    auto original = ctx.Original<uint8_t(__cdecl *)(const float *)>(0x7ddc10);
    std::mt19937 r(0x7ddc10);
    for (int i = 0; i < 10000; ++i) {
        const float in[3] = {RandomFloat(r), RandomFloat(r), RandomFloat(r)};
        ++ctx.cases;
        const uint8_t a = original(in), b = worldmath::QuadrantOfPositionXY(in);
        if (a != b) ctx.Fail("case %d: %u vs %u", i, a, b);
    }
}

DIFF_TEST(WorldTileMath_QuadrantOffset, 0x7dd5b0)
{
    auto original = ctx.Original<void(__cdecl *)(float *, uint8_t)>(0x7dd5b0);
    for (unsigned q = 0; q < 256; ++q) {
        Words<2> a, b;
        original(reinterpret_cast<float *>(a.w), static_cast<uint8_t>(q));
        worldmath::QuadrantOffset(reinterpret_cast<float *>(b.w), static_cast<uint8_t>(q));
        const int i = static_cast<int>(q);
        COMPARE_WORDS(a, b, "quadrant %d", i);
    }
}

DIFF_TEST(WorldTileMath_QuadrantCentre, 0x7dd500)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, const void *)>(0x7dd500);
    std::mt19937 r(0x7dd500);
    for (int i = 0; i < 6000; ++i) {
        worldmath::TileQuadrant tile = {RandomInt(r), RandomInt(r), RandomInt(r), static_cast<uint8_t>(i % 5 == 0 ? r() : r() % 16)};
        Words<3> a, b;
        float *ra = original(reinterpret_cast<float *>(a.w), &tile);
        float *rb = worldmath::QuadrantCentre(reinterpret_cast<float *>(b.w), &tile);
        if (ra != reinterpret_cast<float *>(a.w) || rb != reinterpret_cast<float *>(b.w)) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "tile %d %d %d q %u", tile.x, tile.y, tile.level, tile.quadrant);
    }
}

DIFF_TEST(WorldTileMath_QuadrantBytes, 0x7dd800)
{
    auto isCorner = ctx.Original<uint32_t(__cdecl *)(int32_t)>(0x7dd800);
    auto isSide = ctx.Original<uint32_t(__cdecl *)(int32_t)>(0x7dd840);
    auto opposite = ctx.Original<uint32_t(__cdecl *)(int32_t)>(0x7dd960);
    auto compass = ctx.Original<uint32_t(__cdecl *)(const uint32_t *)>(0x7ddb40);
    for (int q = -300; q < 600; ++q) {
        ++ctx.cases;
        // The argument is a byte: the callee reads its low byte.
        if ((isCorner(q) & 0xff) != worldmath::IsCornerQuadrant(static_cast<int8_t>(q))) ctx.Fail("IsCornerQuadrant %d", q);
        if ((isSide(q) & 0xff) != worldmath::IsSideQuadrant(static_cast<int8_t>(q))) ctx.Fail("IsSideQuadrant %d", q);
        if ((opposite(q) & 0xff) != worldmath::OppositeQuadrant(static_cast<int8_t>(q))) ctx.Fail("OppositeQuadrant %d", q);
        const uint32_t c = static_cast<uint32_t>(q);
        if ((compass(&c) & 0xff) != worldmath::CompassToQuadrant(&c)) ctx.Fail("CompassToQuadrant %d", q);
    }
}

DIFF_TEST(WorldTileMath_StepQuadrantTile, 0x7dd250)
{
    auto original = ctx.Original<void *(__cdecl *)(void *, const void *, uint32_t)>(0x7dd250);
    std::mt19937 r(0x7dd250);
    for (int i = 0; i < 20000; ++i) {
        const uint32_t quadrants[] = {1, 2, 4, 8, 3, 6, 9, 12, 15, 0, 5, 7, 255};
        worldmath::TileQuadrant tile = {RandomInt(r), RandomInt(r), RandomInt(r), static_cast<uint8_t>(quadrants[r() % 13])};
        const uint32_t step = i % 7 == 0 ? r() : r() % 6;
        Words<4> a, b;
        void *ra = original(a.w, &tile, step);
        void *rb = worldmath::StepQuadrantTile(reinterpret_cast<worldmath::TileQuadrant *>(b.w), &tile, step);
        if (ra != a.w || rb != b.w) ctx.Fail("case %d: return value", i);
        a.w[3] &= 0xff;
        b.w[3] &= 0xff;
        COMPARE_WORDS(a, b, "tile %d %d %d q %u step %u", tile.x, tile.y, tile.level, tile.quadrant, step);
    }
}

DIFF_TEST(WorldTileMath_KindedTileSet, 0x7dde80)
{
    auto original = ctx.Original<void *(__thiscall *)(void *, const int32_t *)>(0x7dde80);
    std::mt19937 r(0x7dde80);
    for (int i = 0; i < 2000; ++i) {
        const int32_t tile[3] = {RandomInt(r), RandomInt(r), RandomInt(r)};
        Words<4> a, b;
        void *ra = original(a.w, tile);
        void *rb = static_cast<void *>(reinterpret_cast<worldmath::KindedTile *>(b.w)->Set(tile));
        if (ra != a.w || rb != b.w) ctx.Fail("case %d: return value", i);
        COMPARE_WORDS(a, b, "tile %d %d %d", tile[0], tile[1], tile[2]);
    }
}
