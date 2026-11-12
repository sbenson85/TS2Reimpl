// Differential tests of nTSSG::cFloorColorGrid's step and the rebuild's leaves (lane 261109q, scenegraph LD2c;
// src/sg/cFloorColorGrid.h): Spread, ResetColors, LightReaches, ApplyLight, Propagate, Step, GetColor, LevelAt,
// MarkFloorTiles, LinkSamples and PruneCorner. Bottom-up, each on grids built alike on both sides (tests/
// sg_floor_color_grid.h, the light-pass shape: every link's cells in range, no m18 bit pointing off the sub-cell grid).
//
// The world DB 0x20e0b2 stands in (tests/sg_floor_color_world.h): a worldfx::Fake whose slots (39 the partitions, 52 HasFloorOrGround, 53
// HasAnyFloorOrGround, 55 HasAnyFloorAbove, 71 IsInside; the partitions' 80) answer from a hash of the case's seed and
// what they are asked (the tile's x, y and level, the quadrant byte and the flags' low bytes; never a stack address or
// the bytes the callers leave unset), with junk above the bool's byte, and log what they were asked. The stopwatch's
// Restart 0x1c95ec stands in, counted. Byte arguments carry junk upper bytes and ours is called through the original's
// own signature. Colours, ambients, scales, falloffs and keys carry NaNs of their own payloads; the light boxes and
// positions stay where the original reads inside its vectors.
#include "sg_floor_color_grid.h"
#include "worldfx.h"
#include "sg_floor_color_world.h"

#include "sg/cFloorColorGrid.h"

#include <cmath>

using namespace floorfx;
using namespace floorworld;
using nTSSG::FloorColorSample;
using nTSSG::FloorLight;

namespace {

// ---- the cases' data ----

FloorColorSample Sample(std::mt19937 &gen, bool nans, int16_t key)
{
    FloorColorSample s;
    RandomBytes(&s, sizeof s, gen);
    for (float &f : s.mColor) f = Colour(gen, nans);
    s.mLit = Flag(gen);
    s.mLevel = LevelKey(gen, key);
    return s;
}

// A light: its box in tiles around the grid (corners either way round, whole, half and odd coordinates), its level
// near the grid's, the samples' level id mostly the grid's key.
FloorLight Light(std::mt19937 &gen, const cFloorColorGrid &g, int16_t key, bool nans)
{
    FloorLight l;
    RandomBytes(&l, sizeof l, gen);
    for (float &c : l.mColor) c = Colour(gen, nans);
    l.mLevel = g.mLevel + int32_t(gen() % 5) - 1;
    const auto coord = [&](uint32_t tiles) {
        switch (gen() % 4) {
        case 0: return float(int32_t(gen() % (tiles + 3)) - 1);
        case 1: return float(int32_t(gen() % (2 * tiles + 5)) - 2) * 0.5f;
        default: return float(gen() % ((tiles + 2) * 256)) / 256.0f - 1.0f;
        }
    };
    l.mMin[0] = coord(g.mWidth);
    l.mMin[1] = coord(g.mHeight);
    l.mMax[0] = gen() % 4 ? l.mMin[0] + float(gen() % 3) * 0.5f + (gen() % 2 ? 0.25f : 0.0f) : coord(g.mWidth);
    l.mMax[1] = gen() % 4 ? l.mMin[1] + float(gen() % 3) * 0.5f + (gen() % 2 ? 0.25f : 0.0f) : coord(g.mHeight);
    l.mLevelId = gen() % 4 ? key : int16_t(gen() % 4);
    l.mScale = Scale(gen);
    static const float kFalloffs[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 0.999f};
    l.mFalloff = gen() % 6 == 0 ? (nans ? NaNOf(gen()) : Nice(gen)) : kFalloffs[gen() % std::size(kFalloffs)];
    static const float kKeys[] = {0.1f, 0.5f, 0.505f, 0.509f, 0.52f, 1.0f, 1.0f, 2.0f};
    l.mKey = gen() % 8 == 0 ? Nice(gen) : kKeys[gen() % std::size(kKeys)];
    return l;
}

// The grid built for the light passes, with the case's sample level key; m40 and the ambients as Ambients makes them.
struct Grids : Pair {
    Grids(uint32_t seed, bool nans, int16_t key, std::mt19937 &gen) : Pair(seed, ShapeOf(nans, key))
    {
        Ambients((*this)[0], (*this)[1], gen, nans);
    }
    static Shape ShapeOf(bool nans, int16_t key)
    {
        Shape s;
        s.lightable = true;
        s.nans = nans;
        s.key = key;
        return s;
    }
};

Blob Words(const void *p, size_t n)
{
    const auto *w = static_cast<const uint32_t *>(p);
    return Blob(w, w + n / 4);
}

Blob LightWords(const FloorLight &l) { return Words(&l, sizeof l); }

} // namespace

// ---- Spread 0x409080 ----

DIFF_TEST(sg_floor_color_grid_spread, 0x409080)
{
    using Fn = int32_t(__stdcall *)(FloorColorSample *, const FloorColorSample *, uint32_t, float);
    auto original = ctx.Original<Fn>(0x409080);
    auto ours = reinterpret_cast<Fn>(&cFloorColorGrid::Spread);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::mt19937 gen(seed ^ 0x5b7ead);
        const bool nans = seed % 3 == 0;
        FloorColorSample to[2], from[2];
        to[0] = to[1] = Sample(gen, nans, 0);
        from[0] = from[1] = Sample(gen, nans, 0);
        if (gen() % 2) {
            // The neighbour a little brighter or dimmer (the threshold's either side).
            for (int k = 0; k < 3; ++k) from[0].mColor[k] = from[1].mColor[k] = to[0].mColor[k] * (0.5f + float(gen() % 8) / 4.0f);
        }
        const uint32_t dir = ByteArg(gen, uint8_t(gen() % 4 == 0 ? gen() : gen() % 8));
        const float rate = Scale(gen);
        const uint32_t nulls = gen() % 10;
        FloorColorSample *ta = nulls == 0 ? nullptr : &to[0], *tb = nulls == 0 ? nullptr : &to[1];
        const FloorColorSample *fa = nulls == 1 ? nullptr : &from[0], *fb = nulls == 1 ? nullptr : &from[1];
        const int32_t a = original(ta, fa, dir, rate);
        const int32_t b = ours(tb, fb, dir, rate);
        ++ctx.cases;
        if (a != b) ctx.Fail("Spread seed %u: %d vs %d", seed, a, b);
        Compare(ctx, "Spread to", seed, Words(&to[0], sizeof to[0]), Words(&to[1], sizeof to[1]));
        Compare(ctx, "Spread from", seed, Words(&from[0], sizeof from[0]), Words(&from[1], sizeof from[1]));
    }
}

// ---- ResetColors 0x408860 ----

DIFF_TEST(sg_floor_color_grid_reset_colors, 0x408860)
{
    Fixture fixture;
    using Fn = void(__thiscall *)(void *);
    auto original = ctx.Original<Fn>(0x408860);
    for (uint32_t seed = 0; seed < 400; ++seed) {
        std::mt19937 gen(seed ^ 0x4e5e7);
        Grids p(seed, seed % 2 == 0, 0, gen);
        original(p[0]);
        p[1]->ResetColors();
        Compare(ctx, "ResetColors", seed, Dump(*p[0]), Dump(*p[1]));
    }
}

// ---- LightReaches 0x406ff0 and ApplyLight 0x4089a0 ----

DIFF_TEST(sg_floor_color_grid_light_reaches, 0x406ff0)
{
    Fixture fixture;
    World world;
    using Fn = bool(__thiscall *)(void *, const FloorLight *);
    auto original = ctx.Original<Fn>(0x406ff0);
    auto ours = reinterpret_cast<Fn>(shape::MemberCode(&cFloorColorGrid::LightReaches));
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed ^ 0x11647);
        const int16_t key = int16_t(gen() % 3);
        Grids p(seed, false, key, gen);
        world.Case(seed, gen);
        for (int i = 0; i < 6; ++i) {
            const FloorLight light = Light(gen, *p[0], key, seed % 3 == 0);
            world.log.clear();
            const uint32_t a = original(p[0], &light) ? 1u : 0u;
            const Blob logA = world.log;
            world.log.clear();
            const uint32_t b = ours(p[1], &light) ? 1u : 0u;
            ++ctx.cases;
            if (a != b) ctx.Fail("LightReaches seed %u light %d: %u vs %u", seed, i, a, b);
            Compare(ctx, "LightReaches world", seed, logA, world.log);
        }
        Compare(ctx, "LightReaches", seed, Dump(*p[0]), Dump(*p[1]));
    }
}

DIFF_TEST(sg_floor_color_grid_apply_light, 0x4089a0)
{
    Fixture fixture;
    World world;
    using Fn = bool(__thiscall *)(void *, const FloorLight *, int32_t *, int32_t *);
    auto original = ctx.Original<Fn>(0x4089a0);
    auto ours = reinterpret_cast<Fn>(shape::MemberCode(&cFloorColorGrid::ApplyLight));
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed ^ 0xa991);
        const int16_t key = int16_t(gen() % 3);
        const bool nans = seed % 3 == 0;
        Grids p(seed, nans, key, gen);
        world.Case(seed, gen);
        int32_t box[2][4] = {{0, 0, -1, -1}, {0, 0, -1, -1}};
        if (gen() % 3 == 0)
            for (int k = 0; k < 4; ++k) box[0][k] = box[1][k] = int32_t(gen() % 12) - 2;
        for (int i = 0; i < 4; ++i) {
            const FloorLight light = Light(gen, *p[0], key, nans);
            world.log.clear();
            const uint32_t a = original(p[0], &light, box[0], box[0] + 2) ? 1u : 0u;
            const Blob logA = world.log;
            world.log.clear();
            const uint32_t b = ours(p[1], &light, box[1], box[1] + 2) ? 1u : 0u;
            ++ctx.cases;
            if (a != b) ctx.Fail("ApplyLight seed %u light %d: %u vs %u", seed, i, a, b);
            Compare(ctx, "ApplyLight world", seed, logA, world.log);
            Compare(ctx, "ApplyLight box", seed, Words(box[0], 16), Words(box[1], 16));
        }
        Compare(ctx, "ApplyLight", seed, Dump(*p[0]), Dump(*p[1]));
    }
}

// ---- Propagate 0x409170 ----

DIFF_TEST(sg_floor_color_grid_propagate, 0x409170)
{
    Fixture fixture;
    using Fn = void(__thiscall *)(void *, int32_t, int32_t, int32_t, int32_t, uint32_t, float);
    auto original = ctx.Original<Fn>(0x409170);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed ^ 0x960);
        const bool nans = seed % 4 == 0;
        Grids p(seed, nans, 0, gen);
        // Mostly real colours, so the spread runs several passes.
        if (!nans)
            for (size_t c = 0; c < p[0]->mCells.size(); ++c)
                for (int s = 0; s < 2; ++s)
                    for (int k = 0; k < 3; ++k)
                        if (gen() % 3)
                            p[0]->mCells[c].mSamples[s].mColor[k] = p[1]->mCells[c].mSamples[s].mColor[k] =
                                float(gen() % 4096) / 1024.0f;
        const int32_t w2 = int32_t(p[0]->mWidth * 2), h2 = int32_t(p[0]->mHeight * 2);
        const int32_t minX = int32_t(gen() % uint32_t(w2 + 4)) - 2, minY = int32_t(gen() % uint32_t(h2 + 4)) - 2;
        const int32_t maxX = gen() % 5 == 0 ? minX - 1 : minX + int32_t(gen() % uint32_t(w2 + 2));
        const int32_t maxY = gen() % 5 == 0 ? minY - 1 : minY + int32_t(gen() % uint32_t(h2 + 2));
        const uint32_t passes = gen() % 4 == 0 ? 40 : gen() % 6;
        const float rate = gen() % 3 ? float(1 + gen() % 64) / 64.0f : Scale(gen);
        original(p[0], minX, minY, maxX, maxY, passes, rate);
        p[1]->Propagate(minX, minY, maxX, maxY, passes, rate);
        Compare(ctx, "Propagate", seed, Dump(*p[0]), Dump(*p[1]));
    }
}

// ---- Step 0x405d70 ----

DIFF_TEST(sg_floor_color_grid_step, 0x405d70)
{
    Fixture fixture;
    World world;
    using Fn = void(__thiscall *)(void *, const std::vector<FloorLight> *);
    auto original = ctx.Original<Fn>(0x405d70);
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        std::mt19937 gen(seed ^ 0x57e9);
        const int16_t key = int16_t(gen() % 3);
        const bool nans = seed % 3 == 0;
        Grids p(seed, nans, key, gen);
        world.Case(seed, gen);
        const int32_t steps = int32_t(gen() % 6);
        p[0]->mSteps = p[1]->mSteps = steps;
        if (gen() % 2) p[0]->mThreshold = p[1]->mThreshold = float(1 + gen() % 32) / 32.0f;
        std::vector<FloorLight> lights(gen() % 7);
        bool keyNaN = false; // one NaN key at most (the exchange sort then still ends)
        for (FloorLight &l : lights) {
            l = Light(gen, *p[0], key, nans);
            if (std::isnan(l.mKey)) {
                if (keyNaN) l.mKey = 0.5f;
                keyNaN = true;
            }
        }
        const std::vector<FloorLight> before = lights;
        world.log.clear();
        gRestarts = 0;
        original(p[0], &lights);
        const Blob logA = world.log;
        const int restartsA = gRestarts;
        world.log.clear();
        gRestarts = 0;
        p[1]->Step(&lights);
        ++ctx.cases;
        if (restartsA != gRestarts) ctx.Fail("Step seed %u: %d vs %d restarts", seed, restartsA, gRestarts);
        Compare(ctx, "Step world", seed, logA, world.log);
        Compare(ctx, "Step", seed, Dump(*p[0]), Dump(*p[1]));
        for (size_t i = 0; i < lights.size(); ++i)
            Compare(ctx, "Step lights", seed, LightWords(before[i]), LightWords(lights[i]));
    }
}

// ---- GetColor 0x408e30 ----

DIFF_TEST(sg_floor_color_grid_get_color, 0x408e30)
{
    Fixture fixture;
    using Fn = float *(__thiscall *)(void *, float *, const float *);
    auto original = ctx.Original<Fn>(0x408e30);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed ^ 0x6e7c);
        const bool nans = seed % 3 == 0;
        Grids p(seed, nans, 0, gen);
        for (int i = 0; i < 32; ++i) {
            float pos[3];
            const auto coord = [&](uint32_t tiles) {
                switch (gen() % 4) {
                case 0: return float(gen() % tiles);
                case 1: return float(gen() % (2 * tiles)) * 0.5f;
                case 2: return std::nextafter(float(1 + gen() % tiles), 0.0f);
                default: return float(gen() % (tiles * 1024)) / 1024.0f;
                }
            };
            pos[0] = coord(p[0]->mWidth);
            pos[1] = coord(p[0]->mHeight);
            pos[2] = Nice(gen);
            uint32_t out[2][4];
            RandomBytes(out[0], sizeof out[0], gen);
            std::memcpy(out[1], out[0], sizeof out[0]);
            float *const a = original(p[0], reinterpret_cast<float *>(out[0]), pos);
            float *const b = p[1]->GetColor(reinterpret_cast<float *>(out[1]), pos);
            ++ctx.cases;
            if (a != reinterpret_cast<float *>(out[0]) || b != reinterpret_cast<float *>(out[1]))
                ctx.Fail("GetColor seed %u: returned %p / %p", seed, a, b);
            Compare(ctx, "GetColor", seed, Words(out[0], 16), Words(out[1], 16));
        }
        Compare(ctx, "GetColor grid", seed, Dump(*p[0]), Dump(*p[1]));
    }
}

// ---- LevelAt 0x408790 ----

DIFF_TEST(sg_floor_color_grid_level_at, 0x408790)
{
    World world;
    using Fn = uint32_t(__cdecl *)(void *, float, float, int32_t);
    auto original = ctx.Original<Fn>(0x408790);
    auto ours = reinterpret_cast<Fn>(&cFloorColorGrid::LevelAt);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed ^ 0x1e7e1);
        world.Case(seed, gen);
        const float x = Nice(gen), y = Nice(gen);
        const int32_t level = int32_t(gen() % 8) - 2;
        world.log.clear();
        const uint32_t a = original(&world.db, x, y, level);
        const Blob logA = world.log;
        world.log.clear();
        const uint32_t b = ours(&world.db, x, y, level);
        ++ctx.cases;
        if (a != b) ctx.Fail("LevelAt seed %u: %#x vs %#x", seed, a, b);
        Compare(ctx, "LevelAt world", seed, logA, world.log);
    }
}

// ---- MarkFloorTiles 0x40a9e0 ----

DIFF_TEST(sg_floor_color_grid_mark_floor_tiles, 0x40a9e0)
{
    Fixture fixture;
    World world;
    using Fn = void(__thiscall *)(void *);
    auto original = ctx.Original<Fn>(0x40a9e0);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed ^ 0x3a9c);
        Grids p(seed, false, 0, gen);
        world.Case(seed, gen);
        world.log.clear();
        original(p[0]);
        const Blob logA = world.log;
        world.log.clear();
        p[1]->MarkFloorTiles();
        Compare(ctx, "MarkFloorTiles world", seed, logA, world.log);
        Compare(ctx, "MarkFloorTiles", seed, Dump(*p[0]), Dump(*p[1]));
    }
}

// ---- LinkSamples 0x40a610 ----

DIFF_TEST(sg_floor_color_grid_link_samples, 0x40a610)
{
    using Fn = void(__stdcall *)(FloorColorCell *, FloorColorCell *, uint32_t);
    auto original = ctx.Original<Fn>(0x40a610);
    auto ours = reinterpret_cast<Fn>(&cFloorColorGrid::LinkSamples);
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        std::mt19937 gen(seed ^ 0x714c);
        FloorColorCell cells[2][2];
        RandomBytes(cells[0], sizeof cells[0], gen);
        // The lit flags and link bits mostly 0 or 1 each, so every combination of the four comes up.
        for (FloorColorCell &c : cells[0])
            for (FloorColorSample &s : c.mSamples) {
                s.mLit = gen() % 4 ? uint8_t(gen() % 2) : Flag(gen);
                if (gen() % 2) s.m18 = gen() % 2 ? 0xff : 0;
            }
        std::memcpy(cells[1], cells[0], sizeof cells[0]);
        const uint32_t dir = ByteArg(gen, uint8_t(gen() % 4 == 0 ? gen() : gen() % 8));
        original(&cells[0][0], &cells[0][1], dir);
        ours(&cells[1][0], &cells[1][1], dir);
        Compare(ctx, "LinkSamples", seed, Words(cells[0], sizeof cells[0]), Words(cells[1], sizeof cells[1]));
    }
}

// ---- PruneCorner 0x40a790 ----

DIFF_TEST(sg_floor_color_grid_prune_corner, 0x40a790)
{
    Fixture fixture;
    using Fn = void(__thiscall *)(void *, int32_t, int32_t, uint32_t);
    auto original = ctx.Original<Fn>(0x40a790);
    auto ours = reinterpret_cast<Fn>(shape::MemberCode(&cFloorColorGrid::PruneCorner));
    for (uint32_t seed = 0; seed < 800; ++seed) {
        std::mt19937 gen(seed ^ 0x9c0e);
        Grids p(seed, false, 0, gen);
        // Lit samples mostly, so the corner's both sides get looked at.
        for (size_t c = 0; c < p[0]->mCells.size(); ++c)
            for (int s = 0; s < 2; ++s)
                if (gen() % 3) p[0]->mCells[c].mSamples[s].mLit = p[1]->mCells[c].mSamples[s].mLit = 1;
        for (int i = 0; i < 24; ++i) {
            const int32_t x = int32_t(gen() % (p[0]->mWidth * 2)), y = int32_t(gen() % (p[0]->mHeight * 2));
            // The callers' corner directions 1..7 (the original reads its direction table with the whole byte).
            const uint32_t dir = ByteArg(gen, uint8_t(1 + gen() % 7));
            original(p[0], x, y, dir);
            ours(p[1], x, y, dir);
        }
        Compare(ctx, "PruneCorner", seed, Dump(*p[0]), Dump(*p[1]));
    }
}
