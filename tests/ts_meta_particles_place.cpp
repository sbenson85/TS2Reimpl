// How a meta-particle is placed and drawn, and how it is emitted (rendering row E9): original vs ours. Mode 0 runs the original at
// its RVA, mode 1 ours (what the hook runs); every effect the tests use comes from the original Create (mode 0) or ours (mode 1), and
// after each step the fingerprint of the world (tests/ts_meta_particles_world.h) is compared: the results, the log of every fake's
// calls, every byte of the effect, the particles, the pool and the game's random generator. Half the seeds run at the 24-bit x87
// precision, half at the default.
//
//   Meta_Place_Basis    0xbdce90 / 0xbdcfd0 / 0xbdd110: the three bases from a direction (zero lengths, underflow, NaN)
//   Meta_Place_RotateZ  0xc017e0: a transform turned about z
//   Meta_Place_Place    0xc018f0: a particle's place, the alignment modes, damping, bank, spin curve, warps
//   Meta_Place_Tick     0xc03ea0: the draw tick on particles with the effect's fakes (colours, screw, bloom through the owner's world)
//   Meta_Place_Emit     0xbfdcb0 / 0xc01310 / 0xc01590: the continuous, burst and once-only emitters, called directly
//
// Covered RVAs: 0xbdce90, 0xbdcfd0, 0xbdd110, 0xc017e0, 0xc018f0, 0xc03ea0, 0xbfdcb0, 0xc01310, 0xc01590. The step (slot 5,
// 0xbfe210) is in tests/ts_meta_particles_effect.cpp (Meta_Effect_Frame): the whole row on our object.
//
// The original leaves byte +1 of the transform the place builds as the stack had it; the comparison masks it on both sides.
#include "ts_meta_particles_world.h"

#include <limits>

namespace {

using BasisFn = void(__cdecl *)(const float *, float *);

void PushWords(Blob &b, const void *words, size_t bytes)
{
    const auto *w = static_cast<const uint32_t *>(words);
    b.insert(b.end(), w, w + bytes / 4);
}

void PushPlaceWords(Blob &b, const EffectsTransform &t)
{
    const auto *w = reinterpret_cast<const uint32_t *>(&t);
    b.push_back(w[0] & 0xffff00ffu); // byte +1: the stack's in the original
    b.insert(b.end(), w + 1, w + sizeof(EffectsTransform) / 4);
}

float Special(std::mt19937 &r)
{
    switch (r() % 14) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return 1.0f;
    case 3: return -1.0f;
    case 4: return 1e-30f; // its square underflows
    case 5: return -1e-30f;
    case 6: return 1e30f; // its square overflows
    case 7: return Nan(r);
    case 8: return std::numeric_limits<float>::infinity();
    default: return Wild(r);
    }
}

// Release the effect the test made, as the other tests do.
void Release(Blob &b, Effect *object, void *e)
{
    gEffects[0] = nullptr;
    object->mRefCount = 1;
    b.push_back(CallSlot<uint32_t>(e, 13));
    Drain(b);
    PushPool(b);
    for (const FakeNode &n : gEffectFakes) b.push_back(n.refs);
    *GameAddress<void *>(kPoolFreeCell) = nullptr;
}

// An effect on the first owner, running, with the description `d` (the test keeps it alive).
Effect *Make(int mode, EffectsWorld &w, Description *d, void *&e)
{
    gDescriptions[0] = d;
    e = MakeEffect(mode, d);
    auto *object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
    gEffects[0] = object;
    gEffects[1] = nullptr;
    CallSlot<uint32_t>(e, 12);
    CallSlot<void>(e, 0, static_cast<void *>(&w.owners[1]));
    return object;
}

} // namespace

DIFF_TEST(Meta_Place_Basis, 0xbdce90)
{
    gCtx = &ctx;
    static const uint32_t kRva[3] = {0xbdce90, 0xbdcfd0, 0xbdd110};
    static const BasisFn kOurs[3] = {&fx::MetaBasisA, &fx::MetaBasisB, &fx::MetaBasisC};
    uint32_t zero[3] = {}, xBigger[3] = {}, yBigger[3] = {};
    for (uint32_t seed = 0; seed < 9000; ++seed) {
        Blob blobs[2];
        const unsigned which = seed % 3;
        for (int mode = 0; mode < 2; ++mode) {
            ctx.Mark("basis");
            std::unique_ptr<Precision> precision;
            if (seed / 3 % 2) precision = std::make_unique<Precision>();
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 2953u + 7);
            float in[3] = {Special(r), Special(r), Special(r)};
            if (r() % 6 == 0) in[1] = in[0];
            else if (r() % 9 == 0) in[1] = -in[0];
            else if (r() % 8 == 0) in[0] = in[1] = r() % 2 ? 0.0f : -0.0f;
            else if (r() % 5 == 0) in[0] = in[1] = 1e-30f; // the sum of squares underflows
            else if (r() % 5 == 0) for (float &v : in) v = Tame(r);
            float out[9];
            for (float &v : out) v = Wild(r);
            if (mode) kOurs[which](in, out);
            else ctx.Original<BasisFn>(kRva[which])(in, out);
            PushWords(b, out, sizeof(out));
            zero[which] += in[0] == 0.0f && in[1] == 0.0f;
            xBigger[which] += std::fabs(in[0]) > std::fabs(in[1]);
            yBigger[which] += std::fabs(in[1]) > std::fabs(in[0]);
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Place_Basis", seed);
    }
    for (unsigned i = 0; i < 3; ++i)
        if (zero[i] < 100 || xBigger[i] < 300 || yBigger[i] < 300) ctx.Fail("basis %u: %u zero, %u x, %u y", i, zero[i], xBigger[i], yBigger[i]);
}

DIFF_TEST(Meta_Place_RotateZ, 0xc017e0)
{
    gCtx = &ctx;
    using RotateFn = void(__thiscall *)(EffectsTransform *, float);
    for (uint32_t seed = 0; seed < 5000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            ctx.Mark("rotatez");
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 4099u + 3);
            EffectsTransform t;
            RandomTransform(t, r);
            for (uint8_t &x : t.pad) x = uint8_t(r());
            if (r() % 4 == 0) t.flags = uint8_t(r());
            if (r() % 8 == 0) t.Version() = 0xffff; // the version word wraps
            static const float kAngles[] = {0.0f, 1.0f, -1.0f, 3.1415927f, 1.5707964f, 6.2831855f, 1e10f, 123456.7f,
                                            std::numeric_limits<float>::infinity()};
            const float angle = r() % 3 == 0 ? kAngles[r() % std::size(kAngles)] : Wild(r);
            if (mode) fx::MetaRotateZ(&t, nullptr, angle);
            else ctx.Original<RotateFn>(0xc017e0)(&t, angle);
            PushWords(b, &t, sizeof(t));
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Place_RotateZ", seed);
    }
}

DIFF_TEST(Meta_Place_Place, 0xc018f0)
{
    gCtx = &ctx;
    MetaWorld world;
    using PlaceFn = void(__thiscall *)(void *, Particle *, float, float, Vector3 *, EffectsTransform *);
    gEv.reserve(1 << 16);
    uint32_t modes[8] = {}, damped = 0, unset = 0, warped = 0, banked = 0;
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            ctx.Mark("place");
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 6007u + 1);
            auto first = std::make_unique<Description>();
            RandomDescription(*first, r, false, 0, 0);
            RandomLook(*first, r, seed % 4 == 0);
            void *e = nullptr;
            Effect *const t = Make(mode, w, first.get(), e);
            RandomTransform(t->mLocal, r);
            t->mTime = r() % 4 == 0 ? double(Wild(r)) : double(Tame(r)) * 10.0;
            alignas(16) uint8_t block[sizeof(Particle)];
            for (uint8_t &x : block) x = uint8_t(r());
            auto *p = reinterpret_cast<Particle *>(block);
            switch (r() % 6) {
            case 0: p->mVelocity = {0.0f, 0.0f, 0.0f}; break;
            case 1: {
                static const Vector3 axes[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                p->mVelocity = axes[r() % 6];
                break;
            }
            case 2: p->mVelocity = {1e-30f, -1e-30f, 0.0f}; break;
            default: p->mVelocity = {Wild(r), Wild(r), Wild(r)}; break;
            }
            static const float kHeadings[] = {0.5f, -0.5f, 0.75f, -0.75f, 0.0f, 0.25f, -0.25f, 0.49999997f};
            p->mHeading = r() % 4 == 0 ? -999.0f : r() % 3 == 0 ? kHeadings[r() % std::size(kHeadings)] : r() % 2 ? Tame(r) * 0.25f : Wild(r);
            p->mBank = r() % 2 ? Tame(r) : Wild(r);
            p->mSpin = r() % 2 ? Tame(r) : Wild(r);
            const float time = r() % 3 ? float(r() % 1025) / 1024.0f : Wild(r);
            const float scale = r() % 2 ? Positive(r) : Wild(r);
            Vector3 pos = {Wild(r), Wild(r), Wild(r)};
            alignas(16) EffectsTransform out;
            for (uint8_t &x : reinterpret_cast<uint8_t(&)[sizeof(out)]>(out)) x = uint8_t(r());
            if (mode) t->BuildParticlePlace(p, time, scale, &pos, &out);
            else ctx.Original<PlaceFn>(0xc018f0)(t, p, time, scale, &pos, &out);
            PushPlaceWords(b, out);
            b.insert(b.end(), {Bits(pos.x), Bits(pos.y), Bits(pos.z)});
            PushWords(b, block, sizeof(block));
            Fingerprint(b);
            ++modes[first->mAlignment % 8];
            damped += first->mDamp > 0.0f && p->mHeading != -999.0f;
            unset += first->mDamp > 0.0f;
            warped += !first->mWarps.empty();
            banked += first->mBank.x != 0.0f;
            Release(b, t, e);
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Place_Place", seed);
    }
    if (modes[2] < 300 || modes[3] < 300 || modes[4] < 300 || modes[0] + modes[1] + modes[5] + modes[6] < 300 || damped < 300 ||
        warped < 300 || banked < 300)
        ctx.Fail("place: modes %u %u %u spin-only %u, damped %u, warped %u, banked %u", modes[2], modes[3], modes[4],
                 modes[0] + modes[1] + modes[5] + modes[6], damped, warped, banked);
    (void)unset;
}

DIFF_TEST(Meta_Place_Tick, 0xc03ea0)
{
    gCtx = &ctx;
    MetaWorld world;
    gEv.reserve(1 << 18);
    uint32_t drawn = 0, bloomed = 0, screwed = 0, singleColour = 0, manyColours = 0;
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            ctx.Mark("tick");
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 7351u + 5);
            auto first = std::make_unique<Description>();
            RandomDescription(*first, r, false, 0, 0);
            RandomLook(*first, r, seed % 3 == 0);
            if (r() % 4 == 0) first->mScrew = r() % 2 ? 0.0f : Tame(r);
            // ORIGINAL-BUG: with no colours the original finds the key of -t and reads the vector far out of range, so a colour is given
            if (first->mColors.empty()) first->mColors.push_back(fxdesc::Color{0.25f, 0.5f, 0.75f});
            void *e = nullptr;
            Effect *const t = Make(mode, w, first.get(), e);
            FillPool(12, r);
            RandomTransform(t->mLocal, r);
            RandomTransform(t->mWorld, r);
            t->mEvent3 = r() % 3 ? Positive(r) : Wild(r);
            const unsigned count = r() % 6;
            for (unsigned i = 0; i < count; ++i) {
                Particle *const p = TakeParticle(r);
                if (!p) break;
                p->mLife = Positive(r);
                p->mAge = p->mLife * float(r() % 1024) / 1024.0f; // a key inside the colours
                p->mVelocity = r() % 5 ? Vector3{Tame(r), Tame(r), Tame(r)} : Vector3{Wild(r), Wild(r), Wild(r)};
                p->mHeading = r() % 3 == 0 ? -999.0f : Tame(r) * 0.25f;
                p->mSpin = Tame(r);
                p->mBank = Tame(r);
                p->mSizeVary = r() % 4 ? Positive(r) : Wild(r);
                p->mAlpha = r() % 4 ? Positive(r) : Wild(r);
                p->mColor = {Positive(r), Positive(r), r() % 4 ? Positive(r) : Wild(r)};
                if (mode) t->AddParticle(p);
                else Helper<void(__thiscall *)(void *, Particle *)>(0xbfdc30, t, p);
            }
            Fingerprint(b);
            if (mode) t->TickParticles();
            else Helper<void(__thiscall *)(void *)>(0xc03ea0, t);
            Fingerprint(b);
            drawn += count > 0 && !first->mColors.empty();
            bloomed += count > 0 && !first->mColors.empty() && (first->mBloom[0] | first->mBloom[2]);
            screwed += count > 0 && first->mScrew != 0.0f;
            singleColour += first->mColors.size() == 1;
            manyColours += first->mColors.size() > 1;
            Release(b, t, e);
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Place_Tick", seed);
    }
    if (drawn < 1000 || bloomed < 300 || screwed < 300 || singleColour < 100 || manyColours < 300)
        ctx.Fail("tick: %u drawn, %u bloomed, %u screwed, %u one colour, %u more", drawn, bloomed, screwed, singleColour, manyColours);
}

// The three emitters, called on their own on a running effect, several times, the emission's clocks set to ones they end on.
DIFF_TEST(Meta_Place_Emit, 0xc01310)
{
    gCtx = &ctx;
    MetaWorld world;
    gEv.reserve(1 << 18);
    static const uint32_t kRva[3] = {0xbfdcb0, 0xc01310, 0xc01590}; // continuous, burst, once
    uint32_t emitted[3] = {}, limited[3] = {};
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Blob blobs[2];
        const unsigned which = seed % 3;
        for (int mode = 0; mode < 2; ++mode) {
            ctx.Mark("emit");
            std::unique_ptr<Precision> precision;
            if (seed / 3 % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 8081u + 9);
            uint32_t low = which == 2 ? 2u : which == 1 ? 1u : 0u;
            for (uint32_t bit : {0x4u, 0x80000u, 0x100000u, 0x200000u, 0x800000u, 0x2000000u})
                if (r() % 3 == 0) low |= bit;
            auto first = std::make_unique<Description>();
            RandomDescription(*first, r, true, low, 0);
            void *e = nullptr;
            Effect *const t = Make(mode, w, first.get(), e);
            EffectsTransform local{}, parent{};
            local.scale = parent.scale = r() % 2 ? 1.0f : Positive(r);
            local.m[0][0] = local.m[1][1] = local.m[2][2] = 1.0f;
            parent.m[0][0] = parent.m[1][1] = parent.m[2][2] = 1.0f;
            CallSlot<void>(e, 6, &local, &parent);
            CallSlot<void>(e, 2, r() % 2);
            Fingerprint(b);
            for (uint32_t call = 0; call < 6; ++call) {
                b.push_back(0x0b000000u | call);
                if (!t->mRunning) break;
                const float step = r() % 5 == 0 ? 0.0f : Positive(r) * 0.5f;
                if (call % 2 == 0) (void)TameEmission(t, r);
                // The edges of the emitters' comparisons: the clock exactly on the next emission, and (burst) the age exactly the life.
                if (call % 2 == 0 && which != 0 && step == 0.0f && r() % 2) {
                    auto *const d = const_cast<Description *>(t->mDescription);
                    d->mRateVary = 1.0f;
                    t->mEmitClock = double(t->mEmitted) + (r() % 2 ? 0.0 : double(d->mLife.y));
                }
                // The continuous emitter: the clock exactly on the threshold (the next count), or past it with the pause drawing
                // exactly 0 from a zero-wide range, so the step ends with nothing left and the delay untouched.
                if (call % 2 == 0 && which == 0 && step == 0.0f && r() % 2) {
                    auto *const d = const_cast<Description *>(t->mDescription);
                    if (r() % 2) {
                        t->mEmitClock = double(t->mEmitted) + 1.0;
                    } else {
                        d->m30 = {0.0f, 0.0f};
                        t->mEmitClock = double(t->mEmitted) + 1.5;
                        t->mDelay = 0.75f;
                    }
                }
                // The once-only emitter's count is above any the effect holds up to 100 (and a negative one never ends, in the game).
                if (which == 2 && t->mCount > 100) break;
                if (mode) {
                    if (which == 0) t->EmitContinuous(step);
                    else if (which == 1) t->EmitBurst(step);
                    else t->EmitOnce(step);
                } else {
                    Helper<void(__thiscall *)(void *, float)>(kRva[which], t, step);
                }
                Fingerprint(b);
            }
            emitted[which] += t->mCount > 0;
            limited[which] += first->mRateMode != 0;
            Release(b, t, e);
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Place_Emit", seed);
    }
    for (unsigned i = 0; i < 3; ++i)
        if (emitted[i] < 300) ctx.Fail("emitter %u: %u runs made particles", i, emitted[i]);
    (void)limited;
}
