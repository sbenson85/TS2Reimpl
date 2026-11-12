// Differential tests of row LD2f (lane 261110i; src/sg/cWallDiffusionManager.h): Step 0x40c2e0 (the lights onto the
// runs), the blob pass 0x40edb0 with its row blends 0x4104a0 / 0x4104e0, and CollectBlobs 0x40fa40 (the corners' 44-byte
// records). Original (by RVA) against ours, every case at the x87's 53-bit and 24-bit precisions; records from
// tests/sg_wall_diffusion.h (wallfx) alike on both sides.
// - Stand-ins on both sides, logging into gLog (tests/sg_lighting_common.h) in call order: the world's level 0x7f2260
//   (the location's five words and the flag; for Step an answer whose low half is 0, 1 or 2, junk above), and for Step the
//   world DB's slot 52 (HasFloorOrGround: the tile's words, its quadrant byte, the two arguments' low bytes; 0, 1 or 0x80
//   in the low byte, junk above) through the cached world cell 0x10cc45c.
// - The outcomes: the log and the manager whole (wallfx::Dump: runs, segments' colour blocks, the key set's tree, change
//   lists); CollectBlobs' vector too (size, capacity, the records; the two padding bytes +0x2a / +0x2b left out: the
//   original's record is built on its stack, its padding stack junk).
#include "sg_wall_diffusion.h"
#include "worldfx.h"

#include "sg/cDiffusionManager.h"

#include <cmath>

using namespace wallfx;
using nTSSG::DiffusionBlob44;
using nTSSG::WallLight;

namespace {

// A manager ours constructed on a junk block, filled from the seed; destroyed by ours.
struct Manager {
    Block<cWallDiffusionManager> block{1};
    cWallDiffusionManager *m;
    Manager(uint32_t seed, const Shape &shape)
    {
        m = cWallDiffusionManager::Construct(block.bytes, nullptr);
        Fill(*m, seed, shape);
    }
    ~Manager() { m->~cWallDiffusionManager(); }
    Manager(const Manager &) = delete;
    Manager &operator=(const Manager &) = delete;
};

// A blend scale: the blob pass's 0.8 and 0.4, 1 and 0 (the weight's 0), 3 (the weight's 2^16: its low half 0), the
// weight's int16 edges ((scale - 1) * 32768 at +-32768), a few ulps either way; wide, infinities, NaNs of their own
// payloads, Nice values.
float Scale(std::mt19937 &gen)
{
    static const float kScales[] = {0.8f, 0.4f, 1.0f, 0.0f, -0.0f, 3.0f, 2.0f, 0.5f, -1.0f, 1.0f / 32768.0f + 1.0f,
                                    1.0f - 1.0f / 32768.0f, 1.0f + 1.0f / 65536.0f, 1.0f - 1.0f / 65536.0f};
    auto ulps = [&](float f) {
        switch (gen() % 4) {
        case 0: return f;
        case 1: return std::nextafter(f, 1e30f);
        case 2: return std::nextafter(f, -1e30f);
        default: return std::nextafter(std::nextafter(f, 1e30f), 1e30f);
        }
    };
    switch (gen() % 10) {
    case 0:
    case 1:
    case 2: return ulps(kScales[gen() % std::size(kScales)]);
    case 3: return Wide(gen, 1e5f);
    case 4: return NaN(gen);
    case 5: return Fl(0x7f800000u | (gen() & 0x80000000u));
    default: return Nice(gen, 2.0f);
    }
}

// A run of 1-6 segments (FillRun's fields, NaNs now and then).
void BlendRun(WallDiffusionRun &run, std::mt19937 &gen)
{
    Shape shape;
    shape.minSegments = 1;
    shape.maxSegments = gen() % 3 ? 3 : 6;
    shape.nans = gen() % 4 == 0;
    FillRun(run, gen, shape, 1);
}

// The blob pass's and CollectBlobs' managers: up to 5 runs of 1-3 segments (both read the first or last segment
// unchecked), linked (prev / next -1 or a run), angles near the eight directions (the corners a quarter apart and not),
// NaNs now and then.
Shape CornerShape(uint32_t seed)
{
    Shape shape;
    shape.maxRuns = 5;
    shape.minSegments = 1;
    shape.maxSegments = 3;
    shape.linked = true;
    shape.directions = true;
    shape.nans = seed % 5 == 0;
    return shape;
}

// ---- Step's world ----

uint32_t gLevelSalt = 0;
uint32_t gFloorSalt = 0;

// The world's level 0x7f2260 for Step: the location's words and the flag logged; the answer's low half 0, 1 or 2 (the
// lights' level ids are drawn from the same), junk above.
uint32_t __cdecl StandLevelId(const uint32_t *location, uint32_t flag)
{
    uint32_t h = gLevelSalt ^ 0x7f2260u;
    for (int i = 0; i < 5; ++i) h = Mix(h, location[i]);
    h = Mix(h, flag);
    L(0x7f2260u);
    for (int i = 0; i < 5; ++i) L(location[i]);
    L(flag);
    return (h & 0xffff0000u) | ((h >> 5) % 3);
}

// The world DB's slot 52 (HasFloorOrGround: tile, quadrant byte, flag), logged and answered from a hash.
uint32_t FloorOrGround(worldfx::Fake *, const uint32_t *a)
{
    const auto *t = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    const uint32_t q[] = {52u, t[0], t[1], t[2], t[3] & 0xffu, a[1] & 0xffu, a[2] & 0xffu};
    uint32_t h = gFloorSalt ^ 0x52u;
    for (uint32_t w : q) {
        L(w);
        h = Mix(h, w);
    }
    const uint32_t low = h % 3 ? 0u : (h & 0x100u ? 1u : 0x80u);
    return (h & 0xffffff00u) | low;
}

struct StepRig {
    Fixture fixture;
    worldfx::Scope scope;
    shape::CodePatch level{0x7f2260u, shape::CodeOf(&StandLevelId)};
    bool logging;
    StepRig()
    {
        logging = worldfx::gLogging;
        worldfx::gLogging = false; // its entries hold the tile's stack address
        worldfx::gWorldDb.Reset(0x52);
        worldfx::gWorldDb.Set(52, 3, 0, &FloorOrGround);
        scope.Set(worldfx::kWorldDbCell, &worldfx::gWorldDb);
    }
    ~StepRig() { worldfx::gLogging = logging; }
    StepRig(const StepRig &) = delete;
    StepRig &operator=(const StepRig &) = delete;
};

// A light's falloff (the level loop's 1 - falloff): 0, 1, halves, quarters, negatives, above 1, wide, NaNs.
float Falloff(std::mt19937 &gen)
{
    static const float kFalloffs[] = {0.0f, -0.0f, 1.0f, 0.5f, 0.25f, 0.75f, -0.5f, 1.5f, 2.0f};
    switch (gen() % 6) {
    case 0: return Wide(gen, 4.0f);
    case 1: return NaN(gen);
    default: return kFalloffs[gen() % std::size(kFalloffs)];
    }
}

// A light for Step: a junk record (0x6c bytes), Light's fields (FillLight), the level -1..3 (or any word), the level id
// 0..2 (or any half), the box around a run's first segment's start (or anywhere), intensity, spread and falloff.
void StepLight(WallLight &l, std::mt19937 &gen, const cWallDiffusionManager &m)
{
    RandomBytes(&l, sizeof l, gen);
    FillLight(l, gen);
    l.mLevel = gen() % 8 ? int32_t(gen() % 5) - 1 : int32_t(gen());
    l.mLevelId = gen() % 8 ? int16_t(gen() % 3) : int16_t(gen());
    const float *at = nullptr;
    if (!m.mRuns.empty() && gen() % 4) at = m.mRuns[gen() % m.mRuns.size()].mSegments[0].mStart;
    for (int i = 0; i < 2; ++i) {
        const float c = at && !std::isnan(at[i]) ? at[i] : Nice(gen, 20.0f);
        l.mMin[i] = c - float(gen() % 5) * 0.25f;
        l.mMax[i] = c + float(gen() % 5) * 0.25f;
        if (gen() % 16 == 0) l.mMin[i] = NaN(gen);
    }
    l.mIntensity = Intensity(gen);
    l.mSpread = Spread(gen);
    l.mFalloff = Falloff(gen);
}

} // namespace

// ---- the row blends ---------------------------------------------------------------------------------------------------

// BlendRow 0x4104e0 (thiscall, ret 0xc; void): a FillSegment segment, Color's colours, Scale's scales, rows 0-3. The
// segment whole (its padding too: neither side writes it).
DIFF_TEST(sg_wall_passes_blend_row, 0x4104e0)
{
    using BlendFn = void(__thiscall *)(void *, const float *, float, int32_t);
    auto blend = ctx.Original<BlendFn>(0x4104e0);
    SidesAtBothPrecisions(ctx, "blend row", 6000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed ^ 0x4104e0u);
        Shape shape;
        shape.nans = gen() % 4 == 0;
        WallDiffusionSegment s;
        FillSegment(s, gen, shape);
        float rgb[3];
        Color(rgb, gen);
        const float scale = Scale(gen);
        const int32_t row = int32_t(gen() % 4);
        if (side == 0)
            blend(&s, rgb, scale, row);
        else
            s.BlendRow(rgb, scale, row);
        Blob o{U(rgb[0]), U(rgb[1]), U(rgb[2]), U(scale), uint32_t(row)};
        DumpSegment(o, s, true);
        return o;
    });
}

// BlendRowAt 0x4104a0 (thiscall, ret 0xc; void): a run of 1-6 segments, rows k across all of them (segment k / 4, row
// k % 4). The run whole.
DIFF_TEST(sg_wall_passes_blend_row_at, 0x4104a0)
{
    using BlendFn = void(__thiscall *)(void *, const float *, float, uint32_t);
    auto blend = ctx.Original<BlendFn>(0x4104a0);
    SidesAtBothPrecisions(ctx, "blend row at", 3000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed ^ 0x4104a0u);
        WallDiffusionRun run;
        BlendRun(run, gen);
        float rgb[3];
        Color(rgb, gen);
        const float scale = Scale(gen);
        const uint32_t k = gen() % uint32_t(run.mSegments.size() * 4);
        if (side == 0)
            blend(&run, rgb, scale, k);
        else
            run.BlendRowAt(rgb, scale, k);
        Blob o{U(rgb[0]), U(rgb[1]), U(rgb[2]), U(scale), k};
        DumpRun(o, run, false);
        return o;
    });
}

// ---- the blob pass ----------------------------------------------------------------------------------------------------

// BlobPass 0x40edb0 (thiscall, plain ret; void) on CornerShape's managers (mFlag19 drawn 0, 1, 2, 0x80, 0xff: equal and
// unequal neighbours; 1-segment and longer runs): the log (AmbientFor's level queries) and the manager whole.
DIFF_TEST(sg_wall_passes_blob_pass, 0x40edb0)
{
    Fixture fixture;
    QueryModes modes;
    using Pass = void(__thiscall *)(void *);
    auto original = ctx.Original<Pass>(0x40edb0);
    SidesAtBothPrecisions(ctx, "blob pass", 3000, false, [&](uint32_t seed, int side) {
        Manager mm(seed, CornerShape(seed));
        cWallDiffusionManager &m = *mm.m;
        gQuerySalt = seed;
        gLog.clear();
        if (side == 0)
            original(&m);
        else
            m.BlobPass();
        Blob o = gLog;
        const Blob d = Dump(m);
        o.insert(o.end(), d.begin(), d.end());
        return o;
    });
}

// ---- CollectBlobs -----------------------------------------------------------------------------------------------------

// CollectBlobs 0x40fa40 (thiscall, ret 4; void) on CornerShape's managers (mFlag1A / mFlag1B drawn: the pass clears
// them first) into a vector of 0-3 junk records, spare capacity now and then (the pass clears it; its appends grow it or
// not): the vector (size, capacity, each record less its padding) and the manager whole.
DIFF_TEST(sg_wall_passes_collect_blobs, 0x40fa40)
{
    Fixture fixture;
    using Collect = void(__thiscall *)(void *, std::vector<DiffusionBlob44> *);
    auto original = ctx.Original<Collect>(0x40fa40);
    SidesAtBothPrecisions(ctx, "collect blobs", 3000, false, [&](uint32_t seed, int side) {
        Manager mm(seed, CornerShape(seed));
        cWallDiffusionManager &m = *mm.m;
        std::mt19937 gen(seed ^ 0x40fa40u);
        std::vector<DiffusionBlob44> out;
        Reserve(out, gen);
        for (uint32_t i = gen() % 4; i > 0; --i) {
            DiffusionBlob44 junk;
            RandomBytes(&junk, sizeof junk, gen);
            out.push_back(junk);
        }
        Reserve(out, gen);
        if (side == 0)
            original(&m, &out);
        else
            m.CollectBlobs(&out);
        Blob o{uint32_t(out.size()), uint32_t(out.capacity())};
        for (const DiffusionBlob44 &b : out) {
            for (int i = 0; i < 10; ++i) o.push_back(b.words[i]);
            o.push_back(b.words[10] & 0xffffu);
        }
        const Blob d = Dump(m);
        o.insert(o.end(), d.begin(), d.end());
        return o;
    });
}

// ---- Step -------------------------------------------------------------------------------------------------------------

// Step 0x40c2e0 (thiscall, ret 4; void): a manager of up to 4 runs of 1-3 segments (linked, angles near the directions,
// each segment's level word 0..3 mostly; references valid only to later runs, so Walk ends) and 0-3 StepLight lights (spare
// capacity now and then). The log (the level queries of the level-id test and AmbientFor, slot 52's answers in the level
// loop) and the manager whole (the runs' colours from Light and Walk's flags, the key set's tree with the lit runs).
DIFF_TEST(sg_wall_passes_step, 0x40c2e0)
{
    StepRig rig;
    using StepFn = void(__thiscall *)(void *, const std::vector<WallLight> *);
    auto original = ctx.Original<StepFn>(0x40c2e0);
    SidesAtBothPrecisions(ctx, "step", 2500, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.maxRuns = 4;
        shape.minSegments = 1;
        shape.maxSegments = 3;
        shape.linked = true;
        shape.directions = true;
        shape.nans = seed % 6 == 0;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        std::mt19937 gen(seed ^ 0x40c2e0u);
        ValidRefs(m, gen, true);
        for (WallDiffusionRun &r : m.mRuns)
            for (WallDiffusionSegment &s : r.mSegments)
                if (gen() % 8) s.mLocation[2] = gen() % 4;
        std::vector<WallLight> lights;
        Reserve(lights, gen);
        for (uint32_t i = gen() % 4; i > 0; --i) {
            WallLight l;
            StepLight(l, gen, m);
            lights.push_back(l);
        }
        gLevelSalt = seed;
        gFloorSalt = seed * 3u + 1u;
        gLog.clear();
        if (side == 0)
            original(&m, &lights);
        else
            m.Step(&lights);
        Blob o = gLog;
        const Blob d = Dump(m);
        o.insert(o.end(), d.begin(), d.end());
        Words(o, lights.data(), lights.size() * sizeof(WallLight));
        return o;
    });
}
