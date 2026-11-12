// Differential tests of row LD2l (lane 261110j; src/sg/cWallDiffusionManager.h): the blends Blend 0x40f6a0 and
// BlendUnlit 0x40f800 (the floor's light onto every run, the changed runs into the key set) and their per-run halves
// BlendLightMap 0x40f390 and BlendFloors 0x40f1c0. Original (by RVA) against ours, every case at the x87's 53-bit and
// 24-bit precisions; records from tests/sg_wall_diffusion.h (wallfx) alike on both sides.
// - Fakes on both sides, logging into gLog (tests/sg_lighting_common.h) in call order: the scene graph system's getter
//   0x20defb (a fake system) and its slot 48 (the light map manager: a fake, or null); the light map's AddRef (slot 1),
//   Release (slot 2) and GetFloorLight (slot 16), and the floor colours' GetFloorColor (slot 23, the cIDiffusionManager
//   BlendUnlit is given). The two samplers log the receiver and the position's three words (x, y and the level word) and
//   write a colour drawn from them (Channel's rounding points, wide values, NaNs, infinities); the real bodies (0x2c76a0,
//   0x3c5d60 through 0x408e30) store out+0, +4 and +8 only, so the fakes write those 12 bytes and difftest::FillOut
//   measures the caller's slot (12 in both originals' frames, the step's double after it) and logs it.
// - The outcomes: the bool in al (per run), the log, and the run or the manager whole (wallfx::DumpRun / Dump: the
//   segments' packed colours MergeMax wrote, the key set's tree with the blended runs).
#include "sg_wall_diffusion.h"
#include "stack_out.h"

#include "sg/cDiffusionManager.h"

using namespace wallfx;
using nTSSG::cIDiffusionManager;

namespace {

uint32_t gSampleSalt = 0;

// A sampled colour: the receiver's slot, the position's words logged; the colour a hash of them (Channel's families:
// the packer's rounding points, edges, wide values, NaNs, infinities), written whole (12 bytes) and the caller's slot
// filled past it (FillOut), its size logged.
float *Sample(uint32_t slot, float *out, const float *position)
{
    uint32_t words[3];
    std::memcpy(words, position, sizeof words);
    L(slot);
    uint32_t h = gSampleSalt ^ slot;
    for (uint32_t w : words) {
        L(w);
        h = Mix(h, w);
    }
    std::mt19937 gen(h);
    float rgb[3];
    Channels(rgb, gen);
    std::memcpy(out, rgb, sizeof rgb);
    L(uint32_t(difftest::FillOut(out, sizeof rgb)));
    return out;
}

void *gLightsVT[17];
FakeObject gLights{gLightsVT};
void *gSystemVT[49];
FakeObject gSystem{gSystemVT};
void *gFloorsVT[24];
FakeObject gFloors{gFloorsVT};
FakeObject *gSystemLights = &gLights; // slot 48's answer

uint32_t __fastcall LightsAddRef(FakeObject *self, void *)
{
    L(0xadd0001u);
    L(self == &gLights);
    return 2;
}
uint32_t __fastcall LightsRelease(FakeObject *self, void *)
{
    L(0x2e10002u);
    L(self == &gLights);
    return 1;
}
float *__fastcall FloorLight(FakeObject *self, void *, float *out, const float *position)
{
    L(self == &gLights);
    return Sample(16, out, position);
}
float *__fastcall FloorColor(FakeObject *self, void *, float *out, const float *position)
{
    L(self == &gFloors);
    return Sample(23, out, position);
}
void *__fastcall SystemLights(FakeObject *self, void *)
{
    L(48);
    L(self == &gSystem);
    return gSystemLights;
}
void *__cdecl StandSystem()
{
    L(0x20defbu);
    return &gSystem;
}

struct Fakes {
    Fixture fixture;
    shape::CodePatch system{0x20defbu, shape::CodeOf(&StandSystem)};
    Fakes()
    {
        gLightsVT[1] = reinterpret_cast<void *>(&LightsAddRef);
        gLightsVT[2] = reinterpret_cast<void *>(&LightsRelease);
        gLightsVT[16] = reinterpret_cast<void *>(&FloorLight);
        gSystemVT[48] = reinterpret_cast<void *>(&SystemLights);
        gFloorsVT[23] = reinterpret_cast<void *>(&FloorColor);
    }
    Fakes(const Fakes &) = delete;
    Fakes &operator=(const Fakes &) = delete;
};

// A weight (the bright or dark contribution): 2 (a scale of 1: the colours' rounding points kept), the game's values
// and a few ulps of them, zeros of both signs (the blends skip them; a NaN goes on), wide values, NaNs of their own
// payloads, infinities, denormals.
float Weight(std::mt19937 &gen)
{
    static const float kWeights[] = {2.0f, 2.0f, 1.0f, 0.5f, 0.8f, 0.4f, 4.0f, -2.0f, 1e-30f, 3.0f};
    switch (gen() % 12) {
    case 0:
    case 1:
    case 2:
    case 3: return kWeights[gen() % std::size(kWeights)];
    case 4: return std::nextafter(kWeights[gen() % std::size(kWeights)], gen() % 2 ? 1e30f : -1e30f);
    case 5: return NaN(gen);
    case 6: return gen() % 2 ? 0.0f : -0.0f;
    case 7: return Fl(0x7f800000u | (gen() & 0x80000000u));
    case 8: return Wide(gen, 1e3f);
    default: return Nice(gen, 4.0f);
    }
}

// Empty runs given storage: both blends read the first segment's level word before the count is tested
// (0x40f213 / 0x40f41c), so a run with no block would fault on both sides alike; the word itself is never used.
void Storage(WallDiffusionRun &run, std::mt19937 &gen)
{
    if (run.mSegments.empty()) run.mSegments.reserve(1 + gen() % 2);
}

// Every segment's below reference valid ({level, run, segment} non-negative): nothing is sampled (the null light map's
// cases; the originals call slot 16 on it unchecked otherwise).
void AllBelowValid(WallDiffusionRun &run, std::mt19937 &gen)
{
    for (WallDiffusionSegment &s : run.mSegments) s.mBelow = {int32_t(gen() % 4), int32_t(gen() % 6), int32_t(gen() % 6)};
}

// Each side's six packed words (MergeMax's) high now and then: all bytes 0xff, or each 0xf0 and above, so a merge
// changes nothing or a little (random words almost always change, and the blends' result is the or of every merge).
void Saturate(WallDiffusionRun &run, std::mt19937 &gen)
{
    for (WallDiffusionSegment &s : run.mSegments)
        for (int side = 0; side < 2; ++side) {
            if (gen() % 2) continue;
            const bool full = gen() % 2;
            for (int k = 0; k < 6; ++k) s.mPacked[6 * side + k] = full ? 0xffffffffu : 0xf0f0f0f0u | gen();
        }
}

// A run of 0-6 segments (FillRun's fields: references valid, none or any words; NaNs now and then; level words small
// or any; Saturate's packed words), its block given.
void BlendRun(WallDiffusionRun &run, std::mt19937 &gen)
{
    Shape shape;
    shape.minSegments = gen() % 8 ? 1 : 0;
    shape.maxSegments = gen() % 3 ? 3 : 6;
    shape.nans = gen() % 4 == 0;
    FillRun(run, gen, shape, 1);
    Saturate(run, gen);
    Storage(run, gen);
}

// The managers: up to 6 runs of 0-4 segments, mFlag19 drawn (Flag: 0, 1, 2, 0x80, 0xff), Saturate's packed words (runs
// blended without a change stay out of the key set), every empty run given a block;
// the key set Fill's (small keys and any words, so at and above 2^31 too) and now and then 0x80000000, 0x7fffffff,
// 0xffffffff and a run's index besides (the signed order's edges; the insert finds it there).
struct Manager {
    Block<cWallDiffusionManager> block{1};
    cWallDiffusionManager *m;
    Manager(uint32_t seed, std::mt19937 &gen)
    {
        m = cWallDiffusionManager::Construct(block.bytes, nullptr);
        Shape shape;
        shape.maxRuns = 6;
        shape.maxSegments = 4;
        shape.nans = seed % 5 == 0;
        Fill(*m, seed, shape);
        for (WallDiffusionRun &r : m->mRuns) {
            Saturate(r, gen);
            Storage(r, gen);
        }
        if (gen() % 3 == 0)
            for (uint32_t k : {0x80000000u, 0x7fffffffu, 0xffffffffu}) m->mMarkedRuns.insert(int32_t(k));
        if (!m->mRuns.empty() && gen() % 3 == 0) m->mMarkedRuns.insert(int32_t(gen() % m->mRuns.size()));
    }
    ~Manager() { m->~cWallDiffusionManager(); }
    Manager(const Manager &) = delete;
    Manager &operator=(const Manager &) = delete;
};

} // namespace

// ---- the per-run blends -----------------------------------------------------------------------------------------------

// BlendLightMap 0x40f390 (thiscall, ret 4; bool in al): a BlendRun run, a Weight; the light map the fake (or null, a
// sixth of the cases, with every below reference valid). The al byte, the log (the system, AddRef / Release, each
// sample's position) and the run whole.
DIFF_TEST(sg_wall_blend_light_map, 0x40f390)
{
    Fakes fakes;
    using BlendFn = uint8_t(__thiscall *)(void *, float);
    auto original = ctx.Original<BlendFn>(0x40f390);
    SidesAtBothPrecisions(ctx, "blend light map", 3000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed ^ 0x40f390u);
        WallDiffusionRun run;
        BlendRun(run, gen);
        const float weight = Weight(gen);
        gSystemLights = &gLights;
        if (gen() % 6 == 0) {
            gSystemLights = nullptr;
            AllBelowValid(run, gen);
        }
        gSampleSalt = seed;
        gLog.clear();
        const uint32_t changed = side == 0 ? uint32_t(original(&run, weight)) : uint32_t(run.BlendLightMap(weight));
        Blob o{changed};
        o.insert(o.end(), gLog.begin(), gLog.end());
        DumpRun(o, run, false);
        return o;
    });
}

// BlendFloors 0x40f1c0 (thiscall, ret 8; bool in al): as BlendLightMap with the floor colours' fake for the floors.
DIFF_TEST(sg_wall_blend_floors, 0x40f1c0)
{
    Fakes fakes;
    using BlendFn = uint8_t(__thiscall *)(void *, cIDiffusionManager *, float);
    auto original = ctx.Original<BlendFn>(0x40f1c0);
    auto *floors = reinterpret_cast<cIDiffusionManager *>(&gFloors);
    SidesAtBothPrecisions(ctx, "blend floors", 3000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed ^ 0x40f1c0u);
        WallDiffusionRun run;
        BlendRun(run, gen);
        const float weight = Weight(gen);
        gSampleSalt = seed;
        gLog.clear();
        const uint32_t changed =
            side == 0 ? uint32_t(original(&run, floors, weight)) : uint32_t(run.BlendFloors(floors, weight));
        Blob o{changed};
        o.insert(o.end(), gLog.begin(), gLog.end());
        DumpRun(o, run, false);
        return o;
    });
}

// ---- the blends -------------------------------------------------------------------------------------------------------

// Blend 0x40f6a0 (thiscall, ret 8; void): a Manager, the bright and dark weights (Weight: zeros skip, NaNs blend); the
// light map the fake, or null (a sixth of the cases, every run's below references valid). The log and the manager whole.
DIFF_TEST(sg_wall_blend, 0x40f6a0)
{
    Fakes fakes;
    using BlendFn = void(__thiscall *)(void *, float, float);
    auto original = ctx.Original<BlendFn>(0x40f6a0);
    SidesAtBothPrecisions(ctx, "blend", 2500, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed ^ 0x40f6a0u);
        Manager mm(seed, gen);
        cWallDiffusionManager &m = *mm.m;
        const float bright = Weight(gen);
        const float dark = Weight(gen);
        gSystemLights = &gLights;
        if (gen() % 6 == 0) {
            gSystemLights = nullptr;
            for (WallDiffusionRun &r : m.mRuns) AllBelowValid(r, gen);
        }
        gSampleSalt = seed;
        gLog.clear();
        if (side == 0)
            original(&m, bright, dark);
        else
            m.Blend(bright, dark);
        Blob o = gLog;
        const Blob d = Dump(m);
        o.insert(o.end(), d.begin(), d.end());
        return o;
    });
}

// BlendUnlit 0x40f800 (thiscall, ret 0xc; void): as Blend with the floor colours' fake for the floors.
DIFF_TEST(sg_wall_blend_unlit, 0x40f800)
{
    Fakes fakes;
    using BlendFn = void(__thiscall *)(void *, float, float, cIDiffusionManager *);
    auto original = ctx.Original<BlendFn>(0x40f800);
    auto *floors = reinterpret_cast<cIDiffusionManager *>(&gFloors);
    SidesAtBothPrecisions(ctx, "blend unlit", 2500, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed ^ 0x40f800u);
        Manager mm(seed, gen);
        cWallDiffusionManager &m = *mm.m;
        const float bright = Weight(gen);
        const float dark = Weight(gen);
        gSampleSalt = seed;
        gLog.clear();
        if (side == 0)
            original(&m, bright, dark, floors);
        else
            m.BlendUnlit(bright, dark, floors);
        Blob o = gLog;
        const Blob d = Dump(m);
        o.insert(o.end(), d.begin(), d.end());
        return o;
    });
}
