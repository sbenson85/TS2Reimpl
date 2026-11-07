// The emitters of cTSParticlesEffect (lane WX, rendering row E6): original vs ours, on the world of tests/ts_particles_world.h.
//
//   Particles_Emit            0xbb97a0 Emit: the delay, the preroll's cap, the pick of the emitter
//   Particles_EmitContinuous  0xbb5c70
//   Particles_EmitBursts      0xbbbfa0
//   Particles_EmitOnce        0xbbc560
//
// Each seed makes an effect from a random description, started as the Frame test starts it; then the effect's emitting state (the
// clock, the counter, the delay, the box, the rate scale, the transform, the pool) is set at random, and several steps are run on
// the original (mode 0) and on ours (mode 1) from the same seed. The whole effect, the particles, the pool and the game's generator
// are compared after each. The emitters are called directly, not through slot 5, so the rest of the step does not hide them.
#include "ts_particles_world.h"
#include <limits>

#include "ts/ParticleCurve.h"

namespace {

void SeedRandom(uint32_t seed)
{
    auto *random = GameAddress<cRZRandom>(0x10fd138);
    random->mIntegerSeed = seed * 2654435761u + 12345u;
    random->mRealSeed = int32_t(seed * 40503u + 77u);
}

using EmitFn = void(__thiscall *)(Effect *, float);

enum class Which { kEmit, kContinuous, kBursts, kOnce };

// What the original's side did, for the coverage check: a branch no seed reached proves nothing.
struct EmitCover {
    uint32_t calls = 0, emitted = 0, stopped = 0, delayed = 0, nothing = 0, stretched = 0, still = 0, noStretch = 0, limitEdge = 0,
             clockEdge = 0, delayEdge = 0, keepLimit = 0, rateByScale = 0, boost = 0, bigStep = 0, emptyPool = 0, filledPool = 0,
             shortLife = 0, screwShortcut = 0, screwPath = 0, nanStep = 0, second = 0, bothPrecisions = 0, prerollCap = 0,
             lifeEdge = 0, pauseEdge = 0, zeroPause = 0, restEdge = 0, boostEdge = 0, oldBlock = 0;
};

float Pow2(std::mt19937 &r) { return std::ldexp(1.0f, int(r() % 5) - 3); }

void RunEmit(difftest::Context &ctx, const char *name, uint32_t rva, Which which)
{
    gCtx = &ctx;
    Overrides overrides;
    NodeWorld nodes;
    gEv.reserve(1 << 20);
    shape::StandIn alloc(0x1be3b0, &RZMemory::Malloc, &ArenaMalloc);
    const EmitFn original = ctx.Original<EmitFn>(rva);
    EmitCover cover;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            char mark[64];
            std::snprintf(mark, sizeof(mark), "emit seed %u mode %d", seed, mode);
            ctx.Mark(mark);
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            const bool count = mode == 0; // the original's side is the one that tells what ran
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 104729u + 31);
            auto description = std::make_unique<cTSParticlesDescription>();
            cTSParticlesDescription &d = *description;
            uint64_t flags = 0;
            switch (which) {
            case Which::kBursts: flags = 1; break;
            case Which::kOnce: flags = 2; break;
            case Which::kEmit: flags = r() % 3; break;
            default: break;
            }
            if (which != Which::kEmit && r() % 8 == 0) flags |= 3; // the two flags at once: only Emit looks (once first)
            if (r() % 4 == 0) flags |= 4;
            switch (r() % 4) {
            case 0: flags |= 0x800; break;
            case 1: flags |= 0x1000; break;
            case 2: flags |= 0x2000; break;
            default: break;
            }
            if (r() % 6 == 0) flags |= 0x40000000;
            if (r() % 4 == 0) flags |= 0x40;
            if (r() % 4 == 0) flags |= 0x100;
            RandomDescription(d, r, true, uint32_t(flags));
            for (float &v : d.mRate.mValues) v /= 8.0f; // counts a few at a time: the arena holds a few hundred particles
            if (which == Which::kBursts || which == Which::kOnce) d.mRateMode = r() % 3 == 0 ? 1 + r() % 4 : 0;
            if (r() % 3 == 0) d.m30 = {-1.0f, d.m30.y};
            if (r() % 6 == 0) d.mLife = {0.01f, 0.03f}; // dies in the first integration
            d.mPreroll = r() % 3 == 0 ? float(1 + r() % 3) * 0.25f : 0.0f;
            d.m160 = r() % 3 == 0 ? Positive(r) / 2.0f : (r() % 2 ? 0.0f : -1.0f);
            switch (r() % 6) {
            case 0: d.mStretch = 0.0f; break;
            case 1: d.mStretch = -0.0f; break;
            case 2: d.mStretch = Tame(r); break;
            default: d.mStretch = Positive(r); break;
            }
            const auto tame3 = [&] { return fxdesc::Vector3{Tame(r), Tame(r), Tame(r)}; };
            d.m38 = tame3();
            d.m44 = tame3();
            if (r() % 5 == 0) d.m38 = d.m44 = {0.0f, 0.0f, 0.0f}; // no velocity: not stretched
            if (r() % 12 == 0) d.m38 = d.m44 = {std::bit_cast<float>(0x358637bdu), 0.0f, 0.0f}; // the threshold itself (1e-6 as a float)
            if (r() % 8 == 0) { // only one axis moving (the longest component must be found on each)
                const float v = Tame(r);
                const unsigned axis = r() % 3;
                d.m38 = d.m44 = {axis == 0 ? v : 0.0f, axis == 1 ? v : 0.0f, axis == 2 ? v : 0.0f};
            }
            d.mEmitVolume = {r() % 3 == 0 ? 0.0f : Tame(r), Tame(r)};
            d.mSourceRing = -1.0f;
            d.mPath.mPoints.clear();
            d.mScrewRadius.mValues.clear();
            const bool shortcut = r() % 2 == 0;
            d.mScrewRadius.mKind = cTSEffectCurve::kList;
            if (shortcut) {
                d.mScrewRadius.mValues.push_back(r() % 8 == 0 ? -0.0f : 0.0f);
                if (r() % 6 == 0) { // neither a list nor a wave: no shortcut (the curve reads 0 there)
                    d.mScrewRadius.mKind = 3;
                    if (r() % 2) d.mScrewRadiusVary = std::numeric_limits<float>::infinity(); // 0 times the particle's factor is then a NaN
                }
            } else {
                d.mScrewRadius.mValues.push_back(r() % 3 == 0 ? 0.0f : Positive(r)); // (a first value of 0 with a second one: no shortcut)
                if (r() % 3 == 0) d.mScrewRadius.mValues.push_back(Positive(r));
            }
            d.mScrewAngleToSpeed = uint8_t(r() % 3);
            d.mScrewRadiusMax = Positive(r) * 2.0f;
            gDescriptions[0] = description.get();
            void *const e = MakeEffect(mode, description.get());
            Effect &o = *static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
            gEffects[0] = &o;
            gEffects[1] = nullptr;
            CallSlot<uint32_t>(e, 12);
            CallSlot<void>(e, 0, static_cast<void *>(&w.owners[1]));
            {
                EffectsTransform local{}, parent{};
                local.scale = parent.scale = 1.0f;
                local.m[0][0] = local.m[1][1] = local.m[2][2] = 1.0f;
                parent.m[0][0] = parent.m[1][1] = parent.m[2][2] = 1.0f;
                CallSlot<void>(e, 6, &local, &parent);
            }
            CallSlot<void>(e, 2, r() % 2);
            gEv.clear();
            SeedRandom(seed);

            // The emitting state.
            const double periods[] = {0.25, 0.5, 1.0, 2.0, 1.0 / 3.0, double(Positive(r))};
            o.mEmitPeriod = periods[r() % 6];
            o.mEmitted = int32_t(r() % 4);
            if (d.mRateMode != 0 && r() % 3 == 0) o.mEmitted = int32_t(d.mRateMode - 1 - (d.mRateMode > 1 && r() % 2)); // the tick that ends it
            const double next = double(o.mEmitted + 1);
            o.mEmitClock = double(o.mEmitted) + double(r() % 64) / 64.0;
            o.mEmitRemainder = r() % 3 ? 0.0f : float(r() % 64) / 64.0f;
            o.mScale17c = r() % 4 == 0 ? 1.0f : Pow2(r);
            o.mMaxSize = Positive(r);
            o.mSizeScale = r() % 4 == 0 ? 1.0f : Positive(r);
            o.mDelay = r() % 5 == 0 ? float(1 + r() % 8) / 32.0f : (r() % 12 == 0 ? -0.5f : 0.0f);
            o.mLocal.scale = r() % 3 == 0 ? 1.0f : float(1 + r() % 3) * 0.5f;
            o.mLocal.translation = {Tame(r) / 64.0f, Tame(r) / 64.0f, Tame(r) / 64.0f};
            o.mPreviousPosition = r() % 3 == 0 ? o.mLocal.translation : Vector3{Tame(r) / 64.0f, Tame(r) / 64.0f, Tame(r) / 64.0f};
            if (r() % 3 == 0) { // the box already holds something (min then max); or it is the empty one (min over max)
                for (int k = 0; k < 3; ++k) {
                    o.mBound[k] = Tame(r);
                    o.mBound[3 + k] = o.mBound[k] + (r() % 5 ? Positive(r) : 0.0f);
                }
            } else if (r() % 2) {
                for (int k = 0; k < 3; ++k) {
                    o.mBound[k] = 1.0f;
                    o.mBound[3 + k] = -1.0f;
                }
            }
            bool nan = false, edge = false, lifeEdge = false, pauseEdge = false, zeroPause = false, restEdge = false, boostEdge = false, oldBlock = false;
            // The edges, built on purpose.
            float steps[3];
            for (float &s : steps) s = float(1 + r() % 16) / 32.0f;
            const unsigned special = r() % 17;
            if (special == 0) { // the clock lands exactly on the counter (a strict comparison in each emitter)
                o.mEmitPeriod = 0.5;
                steps[0] = 0.0625f;
                o.mEmitClock = (which == Which::kContinuous ? next : double(o.mEmitted)) - 0.03125;
                o.mDelay = 0.0f;
                edge = true;
            } else if (special == 1) { // the delay is used up exactly
                o.mDelay = steps[0];
                edge = true;
            } else if (special == 2) {
                steps[0] = 0.0f;
            } else if (special == 3) {
                steps[0] = -steps[0];
            } else if (special == 4) {
                steps[0] = which == Which::kOnce ? std::bit_cast<float>(0x7fc00000u) : -0.0f; // a NaN step hangs the other emitters (also in the game)
                nan = true;
            } else if (special == 5) {
                d.mStretch = std::bit_cast<float>(0x7fc00000u);
                nan = true;
            } else if (special == 6) {
                (which == Which::kOnce || which == Which::kBursts ? o.mEmitRemainder : o.mDelay) = std::bit_cast<float>(0x7fc00000u); // (a NaN count hangs the continuous one)
                nan = true;
            } else if (special == 7) {
                o.mBound[1] = std::bit_cast<float>(0x7fc00000u);
                nan = true;
            } else if (special == 8) { // the burst is late by exactly the particles' longest life (the bursts' strict comparison)
                d.mRateVary = 2.0f;
                d.mLife = {0.25f, 1.5f};
                o.mEmitPeriod = 0.5;
                steps[0] = 0.0625f;
                o.mEmitClock = double(o.mEmitted) + 0.71875;
                o.mDelay = 0.0f;
                lifeEdge = true;
            } else if (special == 9) { // the pause takes the clock back exactly to the next tick
                d.m30 = {0.25f, 0.25f};
                d.mRateMode = 0;
                o.mEmitPeriod = 0.5;
                steps[0] = 0.0625f;
                o.mEmitClock = double(o.mEmitted) + 1.09375;
                o.mDelay = 0.0f;
                pauseEdge = true;
            } else if (special == 10) { // a pause that starts at 0
                d.m30 = {0.0f, Positive(r)};
                zeroPause = true;
            } else if (special == 11) { // the pause uses the step up exactly (nothing is left over)
                d.m30 = {0.03125f, 0.03125f};
                d.mRateMode = 0;
                d.mRateVary = 1.0f;
                o.mEmitPeriod = 1.0;
                steps[0] = 0.0625f;
                o.mEmitClock = double(o.mEmitted + 1) - 0.03125;
                o.mDelay = 0.0f;
                restEdge = true;
            } else if (special == 12) { // a boost of 0 over a step so small that the speed ratio overflows
                d.m160 = 0.0f;
                steps[0] = std::bit_cast<float>(1u);
                o.mDelay = 0.0f;
                o.mLocal.translation = {1.0f, 0.0f, 0.0f};
                o.mPreviousPosition = {0.0f, 0.0f, 0.0f};
                boostEdge = true;
            }
            (void)edge;
            if (which == Which::kOnce || (which == Which::kEmit && (flags & 2))) { // never under the live ones (the loop would wrap)
                o.mCount = 0;
            }
            uint32_t pool = r() % 3 == 0 ? 0 : (r() % 2 ? 1 + r() % 6 : 40 + r() % 8);
            FillPool(pool, r);
            if (pool == 0 && r() % 2) { // an earlier block is already on the list (the next one links to it); this one ends the chain
                auto *old = reinterpret_cast<uint32_t *>(gArena + gArenaUsed);
                gArenaUsed += 16;
                old[0] = 0;
                *GameAddress<void *>(kPoolListCell) = old;
                oldBlock = true;
            }
            if (count) {
                cover.emptyPool += pool == 0;
                cover.filledPool += pool != 0;
                cover.shortLife += d.mLife.y < 0.05f;
                cover.screwShortcut += shortcut;
                cover.screwPath += !shortcut;
                cover.noStretch += d.mStretch == 0.0f;
                cover.stretched += d.mStretch != 0.0f && d.mStretch == d.mStretch;
                cover.rateByScale += (flags & 0x3800) != 0;
                cover.boost += d.m160 > 0.0f;
                cover.keepLimit += (flags & 4) && d.mRateMode != 0;
                cover.nanStep += nan;
                cover.clockEdge += special == 0;
                cover.delayEdge += special == 1;
                cover.lifeEdge += lifeEdge;
                cover.pauseEdge += pauseEdge;
                cover.zeroPause += zeroPause;
                cover.restEdge += restEdge;
                cover.boostEdge += boostEdge;
                cover.oldBlock += oldBlock;
                cover.limitEdge += d.mRateMode != 0 && uint32_t(o.mEmitted) + 1 == d.mRateMode;
                cover.still += d.m38.x == 0.0f && d.m38.y == 0.0f && d.m38.z == 0.0f;
                cover.bothPrecisions += seed % 2;
            }
            Fingerprint(b);
            for (int call = 0; call < 3; ++call) {
                const float step = steps[call];
                b.push_back(0x0b0000c0u + call);
                const int32_t before = o.mCount;
                const bool wasDelayed = o.mDelay > 0.0f;
                if (count) {
                    ++cover.calls;
                    cover.second += call == 1;
                    cover.bigStep += step > 0.1f;
                    cover.prerollCap += d.mPreroll > 0.0f && step > d.mPreroll - 0.01f;
                }
                if (mode)
                    switch (which) {
                    case Which::kEmit: o.Emit(step); break;
                    case Which::kContinuous: o.EmitContinuous(step); break;
                    case Which::kBursts: o.EmitBursts(step); break;
                    default: o.EmitOnce(step); break;
                    }
                else
                    original(&o, step);
                if (count) {
                    cover.emitted += o.mCount > before;
                    cover.stopped += o.mStopping;
                    cover.delayed += o.mDelay > 0.0f && !wasDelayed;
                    cover.nothing += o.mCount == before;
                }
                Fingerprint(b);
                if (o.mStopping) break;
            }
            gEffects[0] = nullptr;
            o.mRefCount = 1;
            b.push_back(CallSlot<uint32_t>(e, 13));
            Drain(b);
            for (const FakeOwner &owner : w.owners) b.push_back(owner.refs);
        }
        Same(ctx, blobs[0], blobs[1], name, seed);
    }
    char coverName[96];
    std::snprintf(coverName, sizeof(coverName), "%s coverage", name);
    const bool once = which == Which::kOnce;
    const bool reached = cover.emitted > 200 && cover.nothing && cover.stretched && cover.noStretch && cover.still && cover.clockEdge &&
                         cover.emptyPool && cover.filledPool && cover.shortLife && cover.screwShortcut && cover.screwPath &&
                         cover.nanStep && cover.second && cover.bigStep && cover.bothPrecisions;
    // The stop and the pause are not the once-emitter's; the keep-limit flag, the movement boost and the rate by scale are the
    // continuous emitter's alone (and Emit's, which runs it).
    const bool ticks = once || (cover.stopped && cover.delayed && cover.limitEdge);
    const bool edges = (which == Which::kBursts ? cover.lifeEdge && cover.pauseEdge
                                                : once || (cover.restEdge && cover.boostEdge)) && cover.oldBlock;
    const bool more = ticks && edges && (which != Which::kEmit || (cover.delayEdge && cover.prerollCap)) &&
                      (which == Which::kBursts || once || (cover.keepLimit && cover.boost && cover.rateByScale));
    if (!(reached && more))
        ctx.Fail("%s: emitted %u nothing %u stopped %u delayed %u stretched %u noStretch %u still %u limitEdge %u clockEdge %u delayEdge %u "
                 "emptyPool %u filledPool %u shortLife %u shortcut %u screwPath %u nan %u second %u bigStep %u keep %u prec %u preroll %u "
                 "boost %u scale %u rest %u boostEdge %u life %u pause %u oldBlock %u",
                 name, cover.emitted, cover.nothing, cover.stopped, cover.delayed, cover.stretched, cover.noStretch, cover.still,
                 cover.limitEdge, cover.clockEdge, cover.delayEdge, cover.emptyPool, cover.filledPool, cover.shortLife,
                 cover.screwShortcut, cover.screwPath, cover.nanStep, cover.second, cover.bigStep, cover.keepLimit, cover.bothPrecisions,
                 cover.prerollCap, cover.boost, cover.rateByScale, cover.restEdge, cover.boostEdge, cover.lifeEdge, cover.pauseEdge,
                 cover.oldBlock);
}

} // namespace

DIFF_TEST(Particles_Emit, 0xbb97a0) { RunEmit(ctx, "Particles_Emit", 0xbb97a0, Which::kEmit); }
DIFF_TEST(Particles_EmitContinuous, 0xbb5c70) { RunEmit(ctx, "Particles_EmitContinuous", 0xbb5c70, Which::kContinuous); }
DIFF_TEST(Particles_EmitBursts, 0xbbbfa0) { RunEmit(ctx, "Particles_EmitBursts", 0xbbbfa0, Which::kBursts); }
DIFF_TEST(Particles_EmitOnce, 0xbbc560) { RunEmit(ctx, "Particles_EmitOnce", 0xbbc560, Which::kOnce); }

// ---- the box's Transform 0xbc0cb0 ----

DIFF_TEST(Particles_BoxTransform, 0xbc0cb0)
{
    using TransformFn = void(__thiscall *)(cTSParticleBox *, const EffectsTransform *);
    const TransformFn original = ctx.Original<TransformFn>(0xbc0cb0);
    uint32_t rotated = 0, plain = 0, nans = 0, flat = 0, reversed = 0;
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            std::mt19937 r(seed * 7919u + 5);
            EffectsTransform t;
            RandomTransform(t, r);
            float v[6];
            for (float &x : v) x = r() % 12 == 0 ? std::bit_cast<float>(0x7fc00000u | (r() & 0x3fffffu)) : testhelp::FewOrUniform(r, -3.0f, 3.0f);
            if (r() % 6 == 0) std::copy(v, v + 3, v + 3);        // a flat box (a point)
            if (r() % 6 == 0) std::swap_ranges(v, v + 3, v + 3); // a reversed one
            cTSParticleBox box;
            std::memcpy(&box, v, sizeof(v));
            if (mode == 0) {
                rotated += (t.flags & EffectsTransform::kRotated) != 0;
                plain += (t.flags & EffectsTransform::kRotated) == 0;
                for (float x : v) nans += x != x;
                flat += v[0] == v[3] && v[1] == v[4] && v[2] == v[5];
                reversed += v[0] > v[3];
                original(&box, &t);
            } else {
                box.Transform(&t);
            }
            const auto *words = reinterpret_cast<const uint32_t *>(&box);
            blobs[mode].assign(words, words + 6);
        }
        Same(ctx, blobs[0], blobs[1], "Particles_BoxTransform", seed);
    }
    if (!rotated || !plain || !nans || !flat || !reversed)
        ctx.Fail("Particles_BoxTransform: rotated %u plain %u nan %u flat %u reversed %u", rotated, plain, nans, flat, reversed);
}
