// Differential tests of row LD2i (lane 261110e; src/sg/cWallDiffusionManager.h): the propagation under
// cWallDiffusionManager::DiffuseMarked 0x40f0f0. The word spread (MMX, 0x411380), the segment's three spreads (along the
// run, across a reference, within itself), the run's two recursive walks over the references, one step, the bounded
// loop of steps and the manager's pass over its marked runs. Records come from tests/sg_wall_diffusion.h (wallfx) alike on
// both sides; every case runs at the x87's 53-bit and 24-bit precisions (the originals are SSE / MMX).
//
// The distance (the falloff's argument) is drawn where the falloff word 4096 / (1 + d) (Fixed16::SetFalloff's low half)
// changes character: d >= 0 (word <= 4096: a step's spread never raises a byte above its source, so the loop settles),
// d in (-1, 0) (word > 4096: a byte at 255 "rises" every step forever, 0x4113f2 compares before packuswb saturates), d
// near -1 / 1.4 and -1 (the word overflowing int16 and the magic window), d < -1 (negative words), infinities and NaNs of
// their own payloads (the payload's low 16 bits become the word). Only one operand in each sum is the drawn float, so no
// NaN order question arises. The loops (0x410890, 0x40f0f0) get unbounded step counts only where they settle.
//
// The walks (0x4108d0, 0x410fe0) have no cycle guard: a reference cycle recurses until the stack runs out (each call
// clears its targets' mFlag1A before testing it). Real data is acyclic: mAbove names the wall a level up and mBelow the
// one a level down, and each walk follows one of the two. The cases draw references to later runs only (climbing), which
// is acyclic, but let two paths reach one run (it is walked once per path: each walk re-clears its own targets' flags),
// and chain runs for depth (up to 12 deep).
#include "sg_wall_diffusion.h"

#include <climits>
#include <mmintrin.h>

using namespace wallfx;
using nTSSG::WallSegmentRef;

namespace {

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

// A distance (see the top). `settles`: whether the loop of steps settles at it (d >= 0 or +-0, +inf: word <= 4096).
float Distance(std::mt19937 &gen)
{
    auto ulps = [&](float f) {
        switch (gen() % 4) {
        case 0: return f;
        case 1: return std::nextafter(f, 1e30f);
        case 2: return std::nextafter(f, -1e30f);
        default: return std::nextafter(std::nextafter(f, -1e30f), -1e30f);
        }
    };
    switch (gen() % 12) {
    case 0: return gen() % 2 ? 0.0f : -0.0f;
    case 1: return ulps(0.137f);
    case 2: return std::fabs(Nice(gen, 1.0f));
    case 3: return ulps(-1.0f / 1.4f);
    case 4: return ulps(-1.0f) + (gen() % 3 == 0 ? 1e-4f * float(int(gen() % 21) - 10) : 0.0f);
    case 5: return -float(gen() % 1000) / 1000.0f; // (-1, 0]
    case 6: return Fl(0x7f800000u | (gen() & 0x80000000u));
    case 7:
    case 8: return NaN(gen);
    case 9: return Wide(gen, 100.0f);
    case 10: return -1.0f - float(gen() % 1000) / 100.0f;
    default: return float(gen() % 1000) / 100.0f;
    }
}

bool Settles(float d) { return d >= 0.0f; } // NaN: false; -0: true

// A packed word in one of a case's modes: 0 any word, 1 the case's one word (alike everywhere: nothing rises at d >= 0),
// 2 mostly 0 with a bright byte now and then, 3 bytes from the edges (0, 1, 15-17, 128, 254, 255).
uint32_t PackedWord(std::mt19937 &gen, uint32_t mode, uint32_t uniform)
{
    static const uint8_t kBytes[] = {0, 1, 15, 16, 17, 128, 254, 255};
    switch (mode) {
    case 0: return gen();
    case 1: return uniform;
    case 2: return gen() % 5 ? 0u : (gen() % 2 ? 0xffu : gen() & 0xffu) << (8 * (gen() % 4));
    default: {
        uint32_t w = 0;
        for (int i = 0; i < 4; ++i) w = w << 8 | kBytes[gen() % std::size(kBytes)];
        return w;
    }
    }
}

void Packs(WallDiffusionSegment &s, std::mt19937 &gen, uint32_t mode, uint32_t uniform)
{
    for (uint32_t &w : s.mPacked) w = PackedWord(gen, mode, uniform);
}

// Every segment's packed words in one mode (drawn per case).
void Packs(cWallDiffusionManager &m, std::mt19937 &gen)
{
    const uint32_t mode = gen() % 4;
    const uint32_t uniform = gen();
    for (WallDiffusionRun &r : m.mRuns)
        for (WallDiffusionSegment &s : r.mSegments) Packs(s, gen, mode, uniform);
}

// Referenced runs for the walks: ValidRefs' climbing references, or (a quarter of the cases) a chain, each segment's
// reference the next run that has segments (the walk as deep as the runs are many).
void Refs(cWallDiffusionManager &m, std::mt19937 &gen)
{
    ValidRefs(m, gen, true);
    if (gen() % 4) return;
    const uint32_t runs = uint32_t(m.mRuns.size());
    for (uint32_t r = 0; r < runs; ++r) {
        uint32_t t = r + 1;
        while (t < runs && m.mRuns[t].mSegments.empty()) ++t;
        if (t == runs) continue;
        for (WallDiffusionSegment &s : m.mRuns[r].mSegments) {
            const WallSegmentRef ref{int32_t(gen() % 4), int32_t(t), int32_t(gen() % m.mRuns[t].mSegments.size())};
            if (gen() % 4) s.mAbove = ref;
            if (gen() % 4) s.mBelow = ref;
        }
    }
}

// A walks' manager: up to 6 runs of 0-4 segments (or 12 runs of 1-2, for depth), references as Refs, packed words as
// Packs.
Shape WalkShape(uint32_t seed)
{
    Shape shape;
    if (seed % 5 == 0) {
        shape.maxRuns = 12;
        shape.minSegments = 1;
        shape.maxSegments = 2;
    } else {
        shape.maxRuns = 6;
        shape.maxSegments = 4;
    }
    return shape;
}

void Append(Blob &o, const Blob &d) { o.insert(o.end(), d.begin(), d.end()); }

} // namespace

// ---- the word spread --------------------------------------------------------------------------------------------------

// SpreadWords 0x411380 (stdcall-shaped: ecx never read) over a 32-word buffer: `from` and `to` anywhere in its first
// half (overlapping, equal, either before the other: the loop reads to[i] and from[i], then writes to[i]), counts -2..12
// and INT_MIN; the falloff word at the falloffs' edges (0, 1, 4095-4097, 0x7fff, 0x8000, 0xffff) or any, junk above it.
// al only (eax's upper bytes are the packsswb's, 0x41140d), and the buffer. The original leaves MMX state set (its
// caller empties it, 0x410c10 / 0x410d50 / 0x411160), so the case empties it.
DIFF_TEST(sg_wall_propagate_spread_words, 0x411380)
{
    using Spread = uint32_t(__stdcall *)(const void *, const uint32_t *, uint32_t *, int32_t);
    auto spread = ctx.Original<Spread>(0x411380);
    static const uint16_t kWords[] = {0, 1, 4095, 4096, 4097, 0x7fff, 0x8000, 0xffff, 2048, 8191};
    SidesAtBothPrecisions(ctx, "spread words", 3000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        uint32_t falloff = gen() & 0xffff0000u;
        if (gen() % 3 == 0) {
            Fixed16 f;
            f.SetFalloff(gen() % 2, Distance(gen));
            falloff = f.mValue;
        } else
            falloff |= gen() % 3 ? kWords[gen() % std::size(kWords)] : gen() & 0xffffu;
        uint32_t buffer[32];
        const uint32_t mode = gen() % 4, uniform = gen();
        for (uint32_t &w : buffer) w = PackedWord(gen, mode, uniform);
        const uint32_t from = gen() % 17, to = gen() % 4 ? gen() % 17 : from;
        const int32_t count = gen() % 20 == 0 ? INT_MIN : int32_t(gen() % 15) - 2;
        uint32_t al;
        if (side == 0) {
            al = spread(&falloff, buffer + from, buffer + to, count);
            _mm_empty();
        } else
            al = WallDiffusionSegment::SpreadWords(reinterpret_cast<const Fixed16 *>(&falloff), buffer + from,
                                                   buffer + to, count);
        Blob o{falloff, from, to, uint32_t(count), al & 0xffu};
        Words(o, buffer, sizeof buffer);
        return o;
    });
}

// ---- the segment's spreads --------------------------------------------------------------------------------------------

// SpreadAlong 0x410c10 on a junk segment: either neighbour null or a junk segment (now and then the same one for both,
// or this one), the packed words in one of PackedWord's modes, the distance drawn. al only (0x410c10 returns the or of
// the spreads' bytes), and all three blocks.
DIFF_TEST(sg_wall_propagate_spread_along, 0x410c10)
{
    using Along = uint32_t(__thiscall *)(void *, const void *, const void *, float);
    auto along = ctx.Original<Along>(0x410c10);
    SidesAtBothPrecisions(ctx, "spread along", 2000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        Block<WallDiffusionSegment> b[3]{Block<WallDiffusionSegment>(seed), Block<WallDiffusionSegment>(seed + 1),
                                         Block<WallDiffusionSegment>(seed + 2)};
        const uint32_t mode = gen() % 4, uniform = gen();
        for (auto &s : b) Packs(*s.Get(), gen, mode, uniform);
        const float d = Distance(gen);
        auto pick = [&](int k) -> WallDiffusionSegment * {
            switch (gen() % 8) {
            case 0: return nullptr;
            case 1: return b[0].Get();
            case 2: return b[3 - k].Get();
            default: return b[k].Get();
            }
        };
        WallDiffusionSegment *prev = pick(1), *next = pick(2);
        const uint32_t al = side == 0 ? along(b[0].Get(), prev, next, d) : uint32_t(b[0].Get()->SpreadAlong(prev, next, d));
        Blob o{U(d), uint32_t(prev ? prev - b[0].Get() : -1), uint32_t(next ? next - b[0].Get() : -1), al & 0xffu};
        for (auto &s : b) Words(o, s.bytes, sizeof s.bytes);
        return o;
    });
}

// SpreadAcross 0x410d50 between two junk segments (now and then one segment with itself), the direction byte (0, 1, 2,
// 0x80, 0xff: only the low byte tested, 0x410d6b) with junk above it. al only, and both blocks.
DIFF_TEST(sg_wall_propagate_spread_across, 0x410d50)
{
    using Across = uint32_t(__thiscall *)(void *, void *, float, uint32_t);
    auto across = ctx.Original<Across>(0x410d50);
    SidesAtBothPrecisions(ctx, "spread across", 2000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        Block<WallDiffusionSegment> b[2]{Block<WallDiffusionSegment>(seed), Block<WallDiffusionSegment>(seed + 1)};
        const uint32_t mode = gen() % 4, uniform = gen();
        for (auto &s : b) Packs(*s.Get(), gen, mode, uniform);
        const float d = Distance(gen);
        const uint32_t up = ByteArg(gen, Flag(gen));
        WallDiffusionSegment *other = gen() % 10 ? b[1].Get() : b[0].Get();
        const uint32_t al =
            side == 0 ? across(b[0].Get(), other, d, up) : uint32_t(b[0].Get()->SpreadAcross(other, d, uint8_t(up)));
        Blob o{U(d), up, uint32_t(other == b[0].Get()), al & 0xffu};
        for (auto &s : b) Words(o, s.bytes, sizeof s.bytes);
        return o;
    });
}

// SpreadWithin 0x411160 on a junk segment (in place: later words read earlier ones' new values). al only, and the block.
DIFF_TEST(sg_wall_propagate_spread_within, 0x411160)
{
    using Within = uint32_t(__thiscall *)(void *, float);
    auto within = ctx.Original<Within>(0x411160);
    SidesAtBothPrecisions(ctx, "spread within", 3000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        Block<WallDiffusionSegment> b(seed);
        Packs(*b.Get(), gen, gen() % 4, gen());
        const float d = Distance(gen);
        const uint32_t al = side == 0 ? within(b.Get(), d) : uint32_t(b.Get()->SpreadWithin(d));
        Blob o{U(d), al & 0xffu};
        Words(o, b.bytes, sizeof b.bytes);
        return o;
    });
}

// ---- the walks --------------------------------------------------------------------------------------------------------

// The walk `Fn` (DiffuseWithin 0x410fe0 or DiffuseBetween 0x4108d0) from a random run of WalkShape's manager, the
// direction and apply bytes (0, 1, 2, 0x80, 0xff) with junk above them, the distance drawn: al only, and the manager
// whole (every segment's packed words and every run's mFlag1A).
template <class Original, class Ours>
void WalkCases(difftest::Context &ctx, const char *what, uint32_t salt, Original original, Ours ours)
{
    Fixture fixture;
    SidesAtBothPrecisions(ctx, what, 600, false, [&](uint32_t seed, int side) {
        Manager mm(seed, WalkShape(seed));
        cWallDiffusionManager &m = *mm.m;
        std::mt19937 gen(seed ^ salt);
        Refs(m, gen);
        Packs(m, gen);
        Blob o;
        if (!m.mRuns.empty()) {
            const uint32_t start = gen() % 3 ? 0 : gen() % uint32_t(m.mRuns.size());
            const float d = Distance(gen);
            const uint32_t up = ByteArg(gen, Flag(gen));
            const uint32_t apply = ByteArg(gen, Flag(gen));
            const uint32_t al = side == 0 ? original(&m.mRuns[start], &m.mRuns, d, up, apply)
                                          : uint32_t(ours(m.mRuns[start], &m.mRuns, d, uint8_t(up), uint8_t(apply)));
            o = {start, U(d), up, apply, al & 0xffu};
        }
        Append(o, Dump(m));
        return o;
    });
}

using WalkFn = uint32_t(__thiscall *)(void *, void *, float, uint32_t, uint32_t);

DIFF_TEST(sg_wall_propagate_diffuse_within, 0x410fe0)
{
    WalkCases(ctx, "diffuse within", 0x410fe0u, ctx.Original<WalkFn>(0x410fe0),
              [](WallDiffusionRun &r, std::vector<WallDiffusionRun> *runs, float d, uint8_t up, uint8_t apply) {
                  return r.DiffuseWithin(runs, d, up, apply);
              });
}

DIFF_TEST(sg_wall_propagate_diffuse_between, 0x4108d0)
{
    WalkCases(ctx, "diffuse between", 0x4108d0u, ctx.Original<WalkFn>(0x4108d0),
              [](WallDiffusionRun &r, std::vector<WallDiffusionRun> *runs, float d, uint8_t up, uint8_t apply) {
                  return r.DiffuseBetween(runs, d, up, apply);
              });
}

// ---- the step and the loop --------------------------------------------------------------------------------------------

// DiffuseStep 0x411300 from a random run of WalkShape's manager: al only, and the manager whole.
DIFF_TEST(sg_wall_propagate_diffuse_step, 0x411300)
{
    Fixture fixture;
    using StepFn = uint32_t(__thiscall *)(void *, void *, float);
    auto step = ctx.Original<StepFn>(0x411300);
    SidesAtBothPrecisions(ctx, "diffuse step", 600, false, [&](uint32_t seed, int side) {
        Manager mm(seed, WalkShape(seed));
        cWallDiffusionManager &m = *mm.m;
        std::mt19937 gen(seed ^ 0x411300u);
        Refs(m, gen);
        Packs(m, gen);
        Blob o;
        if (!m.mRuns.empty()) {
            const uint32_t start = gen() % 3 ? 0 : gen() % uint32_t(m.mRuns.size());
            const float d = Distance(gen);
            const uint32_t al = side == 0 ? step(&m.mRuns[start], &m.mRuns, d)
                                          : uint32_t(m.mRuns[start].DiffuseStep(&m.mRuns, d));
            o = {start, U(d), al & 0xffu};
        }
        Append(o, Dump(m));
        return o;
    });
}

// A step count: 0, 1, 2, up to 40, or (where the distance settles the loop) 0xffffffff, INT_MIN or any word.
uint32_t Steps(std::mt19937 &gen, float d)
{
    switch (gen() % 6) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 2;
    case 3:
        if (Settles(d)) return gen() % 2 ? 0xffffffffu : gen() % 3 ? 0x80000000u : gen();
        [[fallthrough]];
    default: return gen() % 41;
    }
}

// Diffuse 0x410890 from a random run of WalkShape's manager, Steps' count (the loop ends at the count or at the first
// step that raised nothing): al only, and the manager whole.
DIFF_TEST(sg_wall_propagate_diffuse, 0x410890)
{
    Fixture fixture;
    using DiffuseFn = uint32_t(__thiscall *)(void *, void *, uint32_t, float);
    auto diffuse = ctx.Original<DiffuseFn>(0x410890);
    SidesAtBothPrecisions(ctx, "diffuse", 500, false, [&](uint32_t seed, int side) {
        Manager mm(seed, WalkShape(seed));
        cWallDiffusionManager &m = *mm.m;
        std::mt19937 gen(seed ^ 0x410890u);
        Refs(m, gen);
        Packs(m, gen);
        Blob o;
        if (!m.mRuns.empty()) {
            const uint32_t start = gen() % 3 ? 0 : gen() % uint32_t(m.mRuns.size());
            const float d = Distance(gen);
            const uint32_t steps = Steps(gen, d);
            const uint32_t al = side == 0 ? diffuse(&m.mRuns[start], &m.mRuns, steps, d)
                                          : uint32_t(m.mRuns[start].Diffuse(&m.mRuns, steps, d));
            o = {start, U(d), steps, al & 0xffu};
        }
        Append(o, Dump(m));
        return o;
    });
}

// ---- the manager ------------------------------------------------------------------------------------------------------

// DiffuseMarked 0x40f0f0 on WalkShape's manager: the marked set refilled with valid run indices (0x40f11c indexes the
// runs unchecked), the threshold (the distance) drawn, the step count Steps' (as the signed setting); the manager whole
// (the set emptied: its head's links and size).
DIFF_TEST(sg_wall_propagate_diffuse_marked, 0x40f0f0)
{
    Fixture fixture;
    using MarkedFn = void(__thiscall *)(void *);
    auto marked = ctx.Original<MarkedFn>(0x40f0f0);
    SidesAtBothPrecisions(ctx, "diffuse marked", 500, false, [&](uint32_t seed, int side) {
        Manager mm(seed, WalkShape(seed));
        cWallDiffusionManager &m = *mm.m;
        std::mt19937 gen(seed ^ 0x40f0f0u);
        Refs(m, gen);
        Packs(m, gen);
        m.mMarkedRuns.clear();
        const uint32_t runs = uint32_t(m.mRuns.size());
        const uint32_t keys = runs ? gen() % (runs + 1) : 0;
        for (uint32_t i = 0; i < keys; ++i) m.mMarkedRuns.insert(gen() % runs);
        m.mThreshold = Distance(gen);
        m.mPropagationSteps = int32_t(Steps(gen, m.mThreshold));
        if (side == 0)
            marked(&m);
        else
            m.DiffuseMarked();
        return Dump(m);
    });
}
