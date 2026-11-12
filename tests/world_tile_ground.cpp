// Differential tests for the tile-maths functions that ask the world layer for a ground height (lane RY, src/world/
// WorldTileMath.*): 0x7dcb90, 0x7dcc00, 0x7dd160, 0x7dd1b0, 0x7dd580, 0x7dd660, 0x7ddd30 and 0x7dddc0. Both sides run
// against the fake cIWorldDB of tests/worldfx.h, programmed by tests/worldgrid.h as a small grid world with levels; the original ground height
// (0x7e4680) and tile value (0x7e2cc0) run on both sides, and the fake's call log is compared as well as the outputs.
#include "difftest.h"
#include "worldgrid.h"

#include "world/WorldTileMath.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

using namespace worldgrid;

DIFF_TEST(WorldTileGround_HeightAtLevel, 0x7dd1b0)
{
    worldfx::Scope scope;
    ProgramWorld();
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    auto original = ctx.Original<double(__cdecl *)(float, float, int32_t)>(0x7dd1b0);
    std::mt19937 r(0x7dd1b0);
    for (int i = 0; i < 6000; ++i) {
        RandomWorld(r);
        const float y = RandomCoord(r), x = RandomCoord(r);
        const int32_t level = RandomLevel(r);
        const Outcome a = Run([&](uint32_t *w) { const double h = original(y, x, level); std::memcpy(w, &h, sizeof(h)); return 0u; });
        const Outcome b = Run([&](uint32_t *w) { const double h = worldmath::HeightAtLevel(y, x, level); std::memcpy(w, &h, sizeof(h)); return 0u; });
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
