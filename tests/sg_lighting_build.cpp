// Differential tests for nTSSG::cLightingManager's light-builder leaves (lane 261109j, scenegraph LG4a;
// src/sg/cLightingManagerBuild.cpp): the vector predicates 0x2dedb0 / 0x2def70 / 0x2df0a0, the removers 0x2dff00 /
// 0x2dffd0, the lerp evaluation 0x2dfc80, the portal-object refresh 0x2d2830, the scene relight 0x2e4b50, the sun height
// rebuild 0x2e4300, the sky sample clear 0x2e0210, the portal sampler 0x2d8b40 and its record pass 0x2d88b0. Hook
// shape: the original runs on the same object image as ours.
//
// The world is tests/sg_lighting_build.h's (on tests/sg_lighting_world.h's): fake objects, scene, iterator, lerp sets,
// sun height map builders and stopwatch, all logging, with the vectors over pools and the switch bytes drawn. The
// sampler's two passes (0x2d8410 SampleRoomTiles, 0x2d8e00 SampleWallPortals) are stand-ins on both sides that log
// and write drawn sums and weights (zeros, NaNs and negatives among them) into the samples.
//
// Compared: results, the log, the object's bytes, the light sets, the sky samples, the pools and the switch bytes; at
// the x87's 53-bit and 24-bit precision where floats are computed.
#include "sg_lighting_build.h"

namespace {

// The predicates' vectors: components with ties (equal sizes, +0 / -0, the same size of opposite signs) and NaNs.
void DrawVectors(std::mt19937 &gen, float *v, int count)
{
    static const float kPool[] = {0.0f, -0.0f, 1.0f, -1.0f, 2.0f, -2.0f, 0.5f, 3.0f};
    const unsigned mode = gen() % 4;
    for (int i = 0; i < 3 * count; ++i) {
        const unsigned roll = gen() % 12;
        if (mode == 0) v[i] = kPool[gen() % std::size(kPool)];
        else if (roll == 0) v[i] = NaN(gen);
        else if (roll < 4) v[i] = kPool[gen() % std::size(kPool)];
        else v[i] = Nice(gen, 4.0f);
    }
}

using Predicate = int32_t(__cdecl *)(const float *);

void PredicateTest(difftest::Context &ctx, uint32_t rva, Predicate ours, int count, const char *what)
{
    auto original = ctx.Original<Predicate>(rva);
    AtBothPrecisions(ctx, what, 20000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + rva);
        alignas(16) float v[18];
        DrawVectors(gen, v, count);
        float copy[18];
        std::memcpy(copy, v, sizeof v);
        ctx.Mark("original");
        const int32_t a = original(v);
        ctx.Mark("ours");
        const int32_t b = ours(copy);
        ++ctx.cases;
        if (a != b) ctx.Fail("%s seed %u: %d vs %d", label, seed, a, b);
        if (std::memcmp(v, copy, sizeof v)) ctx.Fail("%s seed %u: input changed", label, seed);
    });
}

// One case of a member with no arguments: both sides from the same draw.
template <class Original, class Ours>
void MemberCase(difftest::Context &ctx, uint32_t seed, uint32_t salt, const char *label, Original original, Ours ours)
{
    Blob outcome[2];
    for (int side = 0; side < 2; ++side) {
        std::mt19937 gen(seed * 7919u + salt);
        const uint32_t world = gen();
        Build(world);
        BuildLighting(gen);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        if (side == 0) original();
        else ours();
        outcome[side] = EndBuild();
    }
    svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
}

} // namespace

DIFF_TEST(sg_lighting_build_largest6, 0x2dedb0)
{
    PredicateTest(ctx, 0x2dedb0, &cLightingManager::LargestOfSix, 6, "largest of six");
}

DIFF_TEST(sg_lighting_build_largest4, 0x2def70)
{
    PredicateTest(ctx, 0x2def70, &cLightingManager::LargestOfFour, 4, "largest of four");
}

DIFF_TEST(sg_lighting_build_smallest6, 0x2df0a0)
{
    PredicateTest(ctx, 0x2df0a0, &cLightingManager::SmallestOfSix, 6, "smallest of six");
}

DIFF_TEST(sg_lighting_build_remove_built, 0x2dff00)
{
    BuildFixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2dff00);
    for (uint32_t seed = 0; seed < 3000; ++seed)
        MemberCase(ctx, seed, 0x2dff00, "remove built", [&] { original(gObject); }, [] { Ours()->RemoveBuiltLights(); });
}

DIFF_TEST(sg_lighting_build_remove_samples, 0x2dffd0)
{
    BuildFixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2dffd0);
    for (uint32_t seed = 0; seed < 3000; ++seed)
        MemberCase(ctx, seed, 0x2dffd0, "remove samples", [&] { original(gObject); }, [] { Ours()->RemoveSampleLights(); });
}

DIFF_TEST(sg_lighting_build_evaluate, 0x2dfc80)
{
    BuildFixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, float)>(0x2dfc80);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 gen(seed);
        const float t = AnyFloat(gen, 1.0f);
        MemberCase(ctx, seed, 0x2dfc80, "evaluate", [&] { original(gObject, t); }, [&] { Ours()->EvaluateLerpLights(t); });
    }
}

DIFF_TEST(sg_lighting_build_portal_objects, 0x2d2830)
{
    BuildFixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2d2830);
    for (uint32_t seed = 0; seed < 3000; ++seed)
        MemberCase(ctx, seed, 0x2d2830, "portal objects", [&] { original(gObject); }, [] { Ours()->RefreshPortalObjects(); });
}

DIFF_TEST(sg_lighting_build_relight, 0x2e4b50)
{
    BuildFixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2e4b50);
    for (uint32_t seed = 0; seed < 3000; ++seed)
        MemberCase(ctx, seed, 0x2e4b50, "relight", [&] { original(gObject); }, [] { Ours()->RelightSceneObjects(); });
}

DIFF_TEST(sg_lighting_build_sun_heights, 0x2e4300)
{
    BuildFixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2e4300);
    AtBothPrecisions(ctx, "sun heights", 3000, [&](uint32_t seed, const char *label) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0x2e4300u);
            const uint32_t world = gen();
            Build(world);
            BuildLighting(gen);
            // the sun direction (the current set's first light, +0x34) now and then with NaNs
            float *const dir = const_cast<float *>(reinterpret_cast<const float *>(reinterpret_cast<const uint8_t *>(&Current()) + 0x34));
            float *const places[] = {dir, dir + 1, dir + 2};
            MaybeNaN(gen, places, 3);
            gLog.clear();
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) original(gObject);
            else Ours()->RebuildSunHeights();
            outcome[side] = EndBuild();
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

DIFF_TEST(sg_lighting_build_sample_clear, 0x2e0210)
{
    BuildFixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2e0210);
    for (uint32_t seed = 0; seed < 500; ++seed) {
        const int s = seed % 4;
        MemberCase(ctx, seed, 0x2e0210, "sample clear", [&] { original(gSky[s]); },
                   [&] { reinterpret_cast<cSkySample *>(gSky[s])->Clear(); });
    }
}

namespace {

// The sampler's two passes: each logs, and 0x2d8410's writes drawn sums and weights into the samples.
float gPassValues[4][24];
uint32_t gPassMask;
void __fastcall StandPass8410(cLightingManager *self, void *)
{
    gLog.push_back(0x2d8410), gLog.push_back(P(self) == P(gObject));
    for (int s = 0; s < 4; ++s)
        for (int i = 0; i < 24; ++i)
            if (gPassMask >> ((s * 24 + i) % 32) & 1) gSky[s][i] = gPassValues[s][i];
}
void __fastcall StandPass8e00(cLightingManager *self, void *)
{
    gLog.push_back(0x2d8e00), gLog.push_back(P(self) == P(gObject));
    for (int s = 0; s < 4; ++s) gLog.push_back(U(gSky[s][18])), gLog.push_back(U(gSky[s][23]));
}

} // namespace

DIFF_TEST(sg_lighting_build_sample_portals, 0x2d8b40)
{
    BuildFixture fixture;
    shape::StandIn pass1{0x2d8410u, &cLightingManager::SampleRoomTiles, &StandPass8410};
    shape::StandIn pass2{0x2d8e00u, &cLightingManager::SampleWallPortals, &StandPass8e00};
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2d8b40);
    AtBothPrecisions(ctx, "sample portals", 3000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 31u + 0x2d8b40u);
        for (auto &s : gPassValues) {
            for (int i = 0; i < 18; ++i) s[i] = AnyFloat(gen, 4.0f);
            for (int i = 18; i < 24; ++i) s[i] = gen() % 3 == 0 ? 0.0f : gen() % 5 == 0 ? -0.0f : AnyFloat(gen, 2.0f);
        }
        gPassMask = gen() % 4 ? ~0u : gen();
        MemberCase(ctx, seed, 0x2d8b40, label, [&] { original(gObject); }, [] { Ours()->SamplePortals(); });
    });
}

DIFF_TEST(sg_lighting_build_portal_records, 0x2d88b0)
{
    BuildFixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2d88b0);
    AtBothPrecisions(ctx, "portal records", 3000, [&](uint32_t seed, const char *label) {
        MemberCase(ctx, seed, 0x2d88b0, label, [&] { original(gObject); }, [] { Ours()->AddPortalRecords(); });
    });
}
