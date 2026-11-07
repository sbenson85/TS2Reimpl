// Differential tests for the tile-maths functions that ask the world layer for a ground height (lane RY, src/world/
// WorldTileMath.*): 0x7dcb90, 0x7dcc00, 0x7dd160, 0x7dd1b0, 0x7dd580, 0x7dd660, 0x7ddd30 and 0x7dddc0. Both sides run
// against the fake cIWorldDB of tests/worldfx.h, programmed as a small grid world with levels; the original ground height
// (0x7e4680) and tile value (0x7e2cc0) run on both sides, and the fake's call log is compared as well as the outputs.
#include "difftest.h"
#include "worldfx.h"

#include "world/WorldTileMath.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

using worldfx::Fake;
using worldfx::gLog;
using worldfx::gWorldDb;

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

// The programmed world: a size x size grid of tiles on the levels minLevel..maxLevel.
struct World {
    int32_t minLevel = 0, maxLevel = 2, size = 12;
    float wall = 3.0f;
};
World gWorld;

uint32_t MinLevel(Fake *, const uint32_t *) { return uint32_t(gWorld.minLevel); }
uint32_t MaxLevel(Fake *, const uint32_t *) { return uint32_t(gWorld.maxLevel); }
uint32_t MaxCoord(Fake *, const uint32_t *) { return uint32_t(gWorld.size - 1); }
uint32_t MinCoord(Fake *, const uint32_t *) { return 0; }
uint32_t IsInWorld(Fake *, const uint32_t *a)
{
    const float x = FromBits(a[0]), y = FromBits(a[1]);
    return (x >= 0.0f && x < float(gWorld.size) && y >= 0.0f && y < float(gWorld.size)) ? 1u : 0u;
}
// The tile slots take a pointer to {x, y, level}; the pointers differ between the two sides, so the tile's content goes into
// the log after the call's own entry, and Run() clears the pointer argument.
const int32_t *LoggedTile(const uint32_t *a)
{
    const int32_t *t = reinterpret_cast<const int32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 3; ++i) gLog.push_back(uint32_t(t[i]));
    return t;
}
uint32_t IsInside(Fake *, const uint32_t *a)
{
    const int32_t *t = LoggedTile(a);
    return (t[0] >= 0 && t[0] < gWorld.size && t[1] >= 0 && t[1] < gWorld.size && t[2] >= gWorld.minLevel &&
            t[2] <= gWorld.maxLevel)
               ? 1u
               : 0u;
}
uint32_t IsInsideCorners(Fake *, const uint32_t *a)
{
    const int32_t *t = LoggedTile(a);
    return (t[0] >= 0 && t[0] <= gWorld.size && t[1] >= 0 && t[1] <= gWorld.size && t[2] >= gWorld.minLevel &&
            t[2] <= gWorld.maxLevel)
               ? 1u
               : 0u;
}
float Elevation(Fake *, const uint32_t *a)
{
    const int32_t *t = LoggedTile(a);
    return float((t[0] * 31 + t[1] * 17 + t[2] * 7) & 15) * 0.3125f - 1.0f;
}
float WallHeight(Fake *, const uint32_t *) { return gWorld.wall; }

void ProgramWorld()
{
    gWorldDb.Reset(100);
    gWorldDb.Set(88, 0, 0, &MinLevel);
    gWorldDb.Set(89, 0, 0, &MaxLevel);
    gWorldDb.Set(83, 0, 0, &MaxCoord); // 0x14c
    gWorldDb.Set(81, 0, 0, &MinCoord); // 0x144
    gWorldDb.Set(84, 0, 0, &MaxCoord); // 0x150
    gWorldDb.Set(82, 0, 0, &MinCoord); // 0x148
    gWorldDb.Set(72, 2, 0, &IsInWorld);
    gWorldDb.Set(71, 1, 0, &IsInside);
    gWorldDb.Set(70, 1, 0, &IsInsideCorners);
    gWorldDb.SetFloatFn(62, 1, &Elevation);
    gWorldDb.SetFloatFn(95, 0, &WallHeight);
}

void RandomWorld(std::mt19937 &r)
{
    gWorld.minLevel = int32_t(r() % 4) - 1;
    gWorld.maxLevel = gWorld.minLevel + int32_t(r() % 4); // max < min would make the original's two level branches chase each other
    gWorld.size = 2 + int32_t(r() % 14);
    const float walls[] = {3.0f, 0.0f, 2.5f, -1.0f, 0.1f};
    gWorld.wall = walls[r() % 5];
}

float RandomCoord(std::mt19937 &r)
{
    const float size = float(gWorld.size);
    switch (r() % 9) {
    case 0: return float(int32_t(r() % uint32_t(gWorld.size + 4)) - 2);
    case 1: return float(int32_t(r() % uint32_t(2 * gWorld.size + 8)) - 4) * 0.5f;
    case 2: return float(int32_t(r() % uint32_t(gWorld.size + 2))) + 1.0f / 1024.0f * float(int32_t(r() % 3));
    case 3: return float(int32_t(r() % uint32_t(gWorld.size + 2))) + 1.0f - 1.0f / 1048576.0f * float(r() % 3);
    case 4: return -2.0f + (size + 4.0f) * float(r() % 10001) / 10000.0f;
    case 5: return FromBits(r() % 64 == 0 ? 0x7fc00000u : r());
    default: return -1.0f + (size + 2.0f) * float(r() % 1000001) / 1000000.0f;
    }
}

int32_t RandomLevel(std::mt19937 &r) { return int32_t(r() % 10) - 3; }

// What one call left behind: four output words, a return token and the fake's call log.
struct Outcome {
    uint32_t w[4];
    uint32_t ret;
    std::vector<uint32_t> log;
};

template <class F> Outcome Run(F &&f)
{
    Outcome o;
    for (auto &w : o.w) w = 0xc5c5c5c5u;
    gLog.clear();
    o.ret = f(o.w);
    o.log = gLog;
    // Walk the entries {id, slot, argc, args..., [tile content]} and clear the tile pointers (slots 62, 70, 71).
    for (size_t i = 0; i + 3 < o.log.size();) {
        const uint32_t slot = o.log[i + 1], argc = o.log[i + 2];
        const bool tile = slot == 62 || slot == 70 || slot == 71;
        if (tile) o.log[i + 3] = 0;
        i += 3 + argc + (tile ? 3 : 0);
    }
    return o;
}

bool Same(const Outcome &a, const Outcome &b)
{
    for (int i = 0; i < 4; ++i)
        if (a.w[i] != b.w[i]) return false;
    return a.ret == b.ret && a.log == b.log;
}

uint32_t Token(const void *returned, const void *out) { return returned == out ? 1u : 0u; }

#define CHECK_SAME(a, b, ...)                                                                                          \
    do {                                                                                                               \
        ++ctx.cases;                                                                                                   \
        if (!Same(a, b)) {                                                                                             \
            ctx.Fail(__VA_ARGS__);                                                                                     \
        }                                                                                                            \
    } while (0)

} // namespace

DIFF_TEST(WorldTileGround_HeightAtLevel, 0x7dd1b0)
{
    worldfx::Scope scope;
    ProgramWorld();
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    auto original = ctx.Original<float(__cdecl *)(float, float, int32_t)>(0x7dd1b0);
    std::mt19937 r(0x7dd1b0);
    for (int i = 0; i < 6000; ++i) {
        RandomWorld(r);
        const float y = RandomCoord(r), x = RandomCoord(r);
        const int32_t level = RandomLevel(r);
        const Outcome a = Run([&](uint32_t *w) { w[0] = Bits(original(y, x, level)); return 0u; });
        const Outcome b = Run([&](uint32_t *w) { w[0] = Bits(worldmath::HeightAtLevel(y, x, level)); return 0u; });
        CHECK_SAME(a, b, "case %d: y %08x x %08x level %d", i, Bits(y), Bits(x), level);
    }
}

DIFF_TEST(WorldTileGround_PositionAtGround, 0x7dd160)
{
    worldfx::Scope scope;
    ProgramWorld();
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    auto original = ctx.Original<void(__cdecl *)(float *, float, float, int32_t)>(0x7dd160);
    std::mt19937 r(0x7dd160);
    for (int i = 0; i < 6000; ++i) {
        RandomWorld(r);
        const float y = RandomCoord(r), x = RandomCoord(r);
        const int32_t level = RandomLevel(r);
        const Outcome a = Run([&](uint32_t *w) { original(reinterpret_cast<float *>(w), y, x, level); return 0u; });
        const Outcome b = Run([&](uint32_t *w) { worldmath::PositionAtGround(reinterpret_cast<float *>(w), y, x, level); return 0u; });
        CHECK_SAME(a, b, "case %d: y %08x x %08x level %d", i, Bits(y), Bits(x), level);
    }
}

DIFF_TEST(WorldTileGround_TileBytesToPosition, 0x7dcb90)
{
    worldfx::Scope scope;
    ProgramWorld();
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    auto original = ctx.Original<float *(__cdecl *)(float *, const int8_t *)>(0x7dcb90);
    std::mt19937 r(0x7dcb90);
    for (int i = 0; i < 6000; ++i) {
        RandomWorld(r);
        int8_t bytes[3] = {int8_t(int32_t(r() % uint32_t(gWorld.size + 4)) - 2), int8_t(int32_t(r() % uint32_t(gWorld.size + 4)) - 2),
                           int8_t(int32_t(r() % 10) - 3)};
        if (r() % 16 == 0) bytes[r() % 3] = int8_t(r());
        const Outcome a = Run([&](uint32_t *w) { return Token(original(reinterpret_cast<float *>(w), bytes), w); });
        const Outcome b = Run([&](uint32_t *w) { return Token(worldmath::TileBytesToPosition(reinterpret_cast<float *>(w), bytes), w); });
        CHECK_SAME(a, b, "case %d: bytes %d %d %d", i, bytes[0], bytes[1], bytes[2]);
    }
}

DIFF_TEST(WorldTileGround_FixedToPosition, 0x7dcc00)
{
    worldfx::Scope scope;
    ProgramWorld();
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    auto original = ctx.Original<float *(__cdecl *)(float *, const int32_t *, int32_t)>(0x7dcc00);
    std::mt19937 r(0x7dcc00);
    for (int i = 0; i < 6000; ++i) {
        RandomWorld(r);
        int32_t fixed[2] = {int32_t(r() % uint32_t((gWorld.size + 4) * 65536)) - 2 * 65536,
                            int32_t(r() % uint32_t((gWorld.size + 4) * 65536)) - 2 * 65536};
        if (r() % 8 == 0) fixed[0] = int32_t(r() % 5) * 32768;
        if (r() % 8 == 0) fixed[1] = int32_t(r() % 5) * 65536;
        const int32_t level = RandomLevel(r);
        const Outcome a = Run([&](uint32_t *w) { return Token(original(reinterpret_cast<float *>(w), fixed, level), w); });
        const Outcome b = Run([&](uint32_t *w) { return Token(worldmath::FixedToPosition(reinterpret_cast<float *>(w), fixed, level), w); });
        CHECK_SAME(a, b, "case %d: fixed %d %d level %d", i, fixed[0], fixed[1], level);
    }
}

DIFF_TEST(WorldTileGround_QuadrantCentreOnGround, 0x7dd580)
{
    worldfx::Scope scope;
    ProgramWorld();
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    auto original = ctx.Original<float *(__cdecl *)(float *, const worldmath::TileQuadrant *)>(0x7dd580);
    std::mt19937 r(0x7dd580);
    for (int i = 0; i < 6000; ++i) {
        RandomWorld(r);
        worldmath::TileQuadrant tile;
        std::memset(&tile, 0xc5, sizeof(tile));
        tile.x = int32_t(r() % uint32_t(gWorld.size + 4)) - 2;
        tile.y = int32_t(r() % uint32_t(gWorld.size + 4)) - 2;
        tile.level = RandomLevel(r);
        tile.quadrant = uint8_t(r() % 16);
        const Outcome a = Run([&](uint32_t *w) { return Token(original(reinterpret_cast<float *>(w), &tile), w); });
        const Outcome b = Run([&](uint32_t *w) { return Token(worldmath::QuadrantCentreOnGround(reinterpret_cast<float *>(w), &tile), w); });
        CHECK_SAME(a, b, "case %d: tile %d %d %d q %d", i, tile.x, tile.y, tile.level, tile.quadrant);
    }
}

DIFF_TEST(WorldTileGround_TileCentreOnGround, 0x7dd660)
{
    worldfx::Scope scope;
    ProgramWorld();
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    auto original = ctx.Original<float *(__cdecl *)(float *, const int32_t *)>(0x7dd660);
    std::mt19937 r(0x7dd660);
    for (int i = 0; i < 6000; ++i) {
        RandomWorld(r);
        const int32_t tile[3] = {int32_t(r() % uint32_t(gWorld.size + 4)) - 2, int32_t(r() % uint32_t(gWorld.size + 4)) - 2, RandomLevel(r)};
        const Outcome a = Run([&](uint32_t *w) { return Token(original(reinterpret_cast<float *>(w), tile), w); });
        const Outcome b = Run([&](uint32_t *w) { return Token(worldmath::TileCentreOnGround(reinterpret_cast<float *>(w), tile), w); });
        CHECK_SAME(a, b, "case %d: tile %d %d %d", i, tile[0], tile[1], tile[2]);
    }
}

DIFF_TEST(WorldTileGround_PositionToTileCentre, 0x7dd690)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, const float *)>(0x7dd690);
    std::mt19937 r(0x7dd690);
    for (int i = 0; i < 6000; ++i) {
        gWorld.size = 12;
        float position[3] = {RandomCoord(r), RandomCoord(r), FromBits(r() % 4 == 0 ? r() : uint32_t(RandomLevel(r)))};
        const Outcome a = Run([&](uint32_t *w) { return Token(original(reinterpret_cast<float *>(w), position), w); });
        const Outcome b = Run([&](uint32_t *w) { return Token(worldmath::PositionToTileCentre(reinterpret_cast<float *>(w), position), w); });
        CHECK_SAME(a, b, "case %d: position %08x %08x %08x", i, Bits(position[0]), Bits(position[1]), Bits(position[2]));
    }
}

DIFF_TEST(WorldTileGround_WithGroundHeight, 0x7dddc0)
{
    worldfx::Scope scope;
    ProgramWorld();
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    auto original = ctx.Original<float *(__cdecl *)(float *, const float *)>(0x7dddc0);
    std::mt19937 r(0x7dddc0);
    for (int i = 0; i < 6000; ++i) {
        RandomWorld(r);
        const int32_t level = RandomLevel(r);
        float in[3] = {RandomCoord(r), RandomCoord(r), 0};
        std::memcpy(&in[2], &level, sizeof(level));
        const Outcome a = Run([&](uint32_t *w) { return Token(original(reinterpret_cast<float *>(w), in), w); });
        const Outcome b = Run([&](uint32_t *w) { return Token(worldmath::WithGroundHeight(reinterpret_cast<float *>(w), in), w); });
        CHECK_SAME(a, b, "case %d: in %08x %08x level %d", i, Bits(in[0]), Bits(in[1]), level);
    }
}

DIFF_TEST(WorldTileGround_WithTileValue, 0x7ddd30)
{
    worldfx::Scope scope;
    ProgramWorld();
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    auto original = ctx.Original<int32_t *(__cdecl *)(int32_t *, const int32_t *)>(0x7ddd30);
    std::mt19937 r(0x7ddd30);
    for (int i = 0; i < 6000; ++i) {
        RandomWorld(r);
        float in[3] = {RandomCoord(r), RandomCoord(r), RandomCoord(r) * 0.5f};
        const Outcome a = Run([&](uint32_t *w) { return Token(original(reinterpret_cast<int32_t *>(w), reinterpret_cast<const int32_t *>(in)), w); });
        const Outcome b = Run([&](uint32_t *w) { return Token(worldmath::WithTileValue(reinterpret_cast<int32_t *>(w), reinterpret_cast<const int32_t *>(in)), w); });
        CHECK_SAME(a, b, "case %d: in %08x %08x %08x", i, Bits(in[0]), Bits(in[1]), Bits(in[2]));
    }
}
