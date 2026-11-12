// Differential tests for the heights of nTSWorld's helper module (lane RZ, src/world/WorldHeights.*) and the tile-maths
// predicates next to them (0x7ddc50, 0x7ddd60, 0x7ddd90). Both sides run against the grid world of tests/worldgrid.h;
// the fake's call log is compared as well as the results. The ground height's unrounded x87 results are compared as
// doubles, at the harness's 53-bit precision and again at the game's 24-bit.
#include "difftest.h"
#include "worldgrid.h"

#include "world/WorldHeights.h"
#include "world/WorldTileMath.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

using namespace worldgrid;

namespace {

void StoreDouble(uint32_t *w, double h) { std::memcpy(w, &h, sizeof(h)); }

// A point near the grid: mostly the grid world's coordinates, with x or y moved by a few ulps off a whole number so the
// 31-ulp snapping is exercised on both sides of its edge.
float NearWhole(std::mt19937 &r, float v)
{
    if (r() % 3 != 0) return v;
    const float whole = float(int32_t(r() % uint32_t(gWorld.size + 2)) - 1);
    int32_t bits;
    std::memcpy(&bits, &whole, sizeof(bits));
    const int32_t delta = int32_t(r() % 80) - 40;
    if (whole == 0.0f) {
        // Around zero: the smallest floats of either sign.
        const uint32_t tiny = uint32_t(delta < 0 ? 0x80000000u | uint32_t(-delta) : uint32_t(delta));
        return FromBits(tiny);
    }
    return FromBits(uint32_t(bits + delta));
}

void GroundCases(difftest::Context &ctx, const char *label, uint32_t seed, int count)
{
    auto original = ctx.Original<double(__cdecl *)(const nTSWorld::WorldPoint *)>(0x7e4680);
    std::mt19937 r(seed);
    for (int i = 0; i < count; ++i) {
        RandomWorld(r);
        gWorld.rough = r() % 2 == 0;
        nTSWorld::WorldPoint p{NearWhole(r, RandomCoord(r)), NearWhole(r, RandomCoord(r)), RandomLevel(r)};
        const Outcome a = Run([&](uint32_t *w) { StoreDouble(w, original(&p)); return 0u; });
        const Outcome b = Run([&](uint32_t *w) { StoreDouble(w, worldheights::GroundHeightAt(&p)); return 0u; });
        CHECK_SAME(a, b, "%s case %d: %08x %08x level %d rough %d: %08x%08x vs %08x%08x", label, i, Bits(p.x), Bits(p.y),
                   p.level, int(gWorld.rough), a.w[1], a.w[0], b.w[1], b.w[0]);
    }
    gWorld.rough = false;
}

// HasFloorOrGround (slot 52): the tile (logged by content), the mask byte and the checkVisible byte. Only the low byte of
// each is the argument: the original pushes the quadrant's dword (three bytes of its stack after the byte) and the
// caller's bool dword as they are.
uint32_t gFloorAnswer = 1;
uint32_t HasFloorOrGround(Fake *, const uint32_t *a)
{
    const int32_t *t = LoggedTile(a);
    const uint32_t mask = a[1] & 0xff, visible = a[2] & 0xff;
    return (uint32_t(t[0] + t[1] + t[2]) + mask + visible + gFloorAnswer) % 3 == 0 ? 0u : 1u;
}

// The predicate's log, with the slot 52 entry's mask and bool words cut to their low bytes.
Outcome Masked(Outcome o)
{
    for (size_t i = 0; i + 3 < o.log.size();) {
        const uint32_t slot = o.log[i + 1], argc = o.log[i + 2];
        if (slot == 52) {
            o.log[i + 4] &= 0xff;
            o.log[i + 5] &= 0xff;
        }
        i += 3 + argc + (TileSlot(slot) ? 3 : 0);
    }
    return o;
}

} // namespace

DIFF_TEST(WorldHeights_GroundHeightAt, 0x7e4680)
{
    worldfx::Scope scope;
    ProgramWorld();
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    GroundCases(ctx, "53-bit", 0x7e4680, 30000);
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    GroundCases(ctx, "24-bit", 0x7e4681, 30000);
}

DIFF_TEST(WorldHeights_HasFloorOrGroundAt, 0x7ddc50)
{
    worldfx::Scope scope;
    ProgramWorld();
    gWorldDb.Set(52, 3, 0, &HasFloorOrGround);
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    auto original = ctx.Original<bool(__cdecl *)(const float *, bool)>(0x7ddc50);
    std::mt19937 r(0x7ddc50);
    for (int i = 0; i < 8000; ++i) {
        RandomWorld(r);
        gFloorAnswer = r() % 3;
        const int32_t level = RandomLevel(r);
        float pos[3] = {RandomCoord(r), RandomCoord(r), 0};
        std::memcpy(&pos[2], &level, sizeof(level));
        const bool visible = r() % 2 == 0;
        const Outcome a = Masked(Run([&](uint32_t *) { return uint32_t(original(pos, visible)); }));
        const Outcome b = Masked(Run([&](uint32_t *) { return uint32_t(worldheights::HasFloorOrGroundAt(pos, visible)); }));
        CHECK_SAME(a, b, "case %d: %08x %08x level %d visible %d", i, Bits(pos[0]), Bits(pos[1]), level, int(visible));
    }
    // No world DB: the cache is empty and the framework has no service object to acquire one from (both sides false, and
    // nothing is called).
    gFloorAnswer = 1;
    worldfx::Scope none;
    none.SetWord(worldfx::kWorldDbCell, 0);
    none.SetWord(0x10c9378, 0); // the framework (GetServiceObject's cell)
    for (int i = 0; i < 200; ++i) {
        float pos[3] = {RandomCoord(r), RandomCoord(r), 0};
        const bool visible = i % 2 == 0;
        const Outcome a = Masked(Run([&](uint32_t *) { return uint32_t(original(pos, visible)); }));
        const Outcome b = Masked(Run([&](uint32_t *) { return uint32_t(worldheights::HasFloorOrGroundAt(pos, visible)); }));
        CHECK_SAME(a, b, "no db case %d: %08x %08x", i, Bits(pos[0]), Bits(pos[1]));
        if (a.ret != 0 || !a.log.empty()) ctx.Fail("no db case %d: the original found a floor or called the fake", i);
    }
}

DIFF_TEST(WorldHeights_WithLevelOfGround, 0x7ddd60)
{
    worldfx::Scope scope;
    ProgramWorld();
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    using Fn = int32_t *(__cdecl *)(int32_t *, const float *);
    auto above = ctx.Original<Fn>(0x7ddd60);
    auto at = ctx.Original<Fn>(0x7ddd90);
    std::mt19937 r(0x7ddd60);
    for (int i = 0; i < 6000; ++i) {
        RandomWorld(r);
        const float in[3] = {RandomCoord(r), RandomCoord(r), RandomCoord(r) * 0.5f};
        const bool plusOne = i % 2 == 0;
        const Outcome a = Run([&](uint32_t *w) {
            int32_t *out = reinterpret_cast<int32_t *>(w);
            return Token(plusOne ? above(out, in) : at(out, in), w);
        });
        const Outcome b = Run([&](uint32_t *w) {
            int32_t *out = reinterpret_cast<int32_t *>(w);
            return Token(plusOne ? worldheights::WithLevelAboveGround(out, in) : worldheights::WithLevelOfGround(out, in), w);
        });
        CHECK_SAME(a, b, "case %d (%s): %08x %08x %08x", i, plusOne ? "0x7ddd60" : "0x7ddd90", Bits(in[0]), Bits(in[1]),
                   Bits(in[2]));
    }
}
