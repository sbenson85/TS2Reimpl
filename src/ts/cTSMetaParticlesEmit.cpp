// The meta-particles effect's step and its three emitters (lane XE, rendering row E9): slot 5 (0xbfe210), the one-shot emitter
// (0xc01590), the burst emitter (0xc01310) and the continuous one (0xbfdcb0). Declared in src/ts/cTSMetaParticlesEffect.h.
// Scalar SSE single with the first-operand NaN rule of fx::Add / fx::Mul; the clocks are doubles; the x87 spots (the burst's rate
// times the event, the continuous emitter's step less the pause) go through x87::Rounded. Comparisons are written the way the
// original's comiss / jb pairs take them (a NaN takes the `jb` and `jbe` branches).
#include "ts/cTSMetaParticlesEffect.h"

#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "game/vtable.h"
#include "misc/X87.h"
#include "ts/ParticleCurve.h"
#include "ts/ParticleMath.h"

#include <bit>
#include <cstdint>

using fx::Add;
using fx::Div;
using fx::EffectsTransform;
using fx::Mul;
using fx::Sub;
using fx::Vector3;
using nGZCompGeom::convex::Mul87;
using nGZCompGeom::convex::Stored;
using nGZCompGeom::convex::X87Pick;

namespace {

constexpr uint32_t kFlagRestartable = 0x4;
constexpr uint32_t kFlagWalks = 0x800000;
constexpr uint32_t kFlagSurfaceQuadrant = 0x2000;
constexpr float kMagic = 12582912.0f;

uint32_t LowFlags(const cTSMetaParticlesDescription *d) { return static_cast<uint32_t>(d->mFlags); }

// An int as the double the original makes of it (cvtdq2pd plus 2^32 for a negative one).
double Fixed(int32_t v) { return static_cast<double>(v) + (v < 0 ? 4294967296.0 : 0.0); }

// The nearest whole number of x by the 2^23 + 2^22 trick, moved down when i - 1 converts to exactly x (the one-shot and burst
// counts; their input is x + 0.5 first).
int32_t CountDown(float x)
{
    const float shifted = Add(Add(x, 0.5f), kMagic);
    int32_t i = static_cast<int32_t>(std::bit_cast<uint32_t>(shifted) + 0xb4c00000u);
    if (static_cast<float>(i - 1) == x) i = i - 1;
    return i;
}

// fld held; fmul dword [memory]: a NaN in the register beats a signalling memory operand.
double Mul87Memory(double held, float memory)
{
    if (held != held && memory != memory && !(std::bit_cast<uint32_t>(memory) & 0x00400000u)) return x87::Rounded(held);
    return Mul87(held, static_cast<double>(memory));
}

// fld held; fsubr dword [memory]: memory - held.
double Subr87Memory(double held, float memory)
{
    if (held != held && memory != memory) {
        if (!(std::bit_cast<uint32_t>(memory) & 0x00400000u)) return x87::Rounded(held);
        return x87::Rounded(X87Pick(held, static_cast<double>(memory)));
    }
    return x87::Rounded(static_cast<double>(memory) - held);
}

} // namespace

// 0xc01590: the particles of a once-only effect come in one go, up to the rate's count.
void cTSMetaParticlesEffect::EmitOnce(float step)
{
    const cTSMetaParticlesDescription *const d = mDescription;
    const double clock = static_cast<double>(step) * mEmitPeriod + mEmitClock;
    mEmitClock = clock;
    const double thr = Fixed(mEmitted);
    if (thr > clock) return;
    const float rate = fx::EvaluateCurve(d->mRate, 0.0f);
    int32_t n = CountDown(rate) - mCount;
    if (n == 0) return;
    // ORIGINAL-BUG: a count below the live ones makes n negative, and the loop then runs about 2^32 times
    do {
        cTSMetaParticle *const p = NewParticle();
        if (p) {
            if (!p->mAlive) {
                RetireParticle(p);
            } else {
                mParticles.push_back(p);
                ++mCount;
            }
        }
    } while (--n != 0);
}

// 0xc01310: a burst of the rate's count (scaled by the events) each period, each particle as old as its share of the step.
void cTSMetaParticlesEffect::EmitBurst(float step)
{
    const cTSMetaParticlesDescription *const d = mDescription;
    double clock = static_cast<double>(step) * mEmitPeriod + mEmitClock;
    mEmitClock = clock;
    double thr = Fixed(mEmitted);
    if (thr > clock) return;
    const float rate = fx::EvaluateCurve(d->mRate, 0.0f);
    const float scaled = Mul(mRateScale, mEvent101[0]);
    const float total = Stored(Mul87Memory(static_cast<double>(rate), scaled));
    const int32_t n = CountDown(total);
    if (clock < thr || clock != clock) return;
    for (;;) {
        const float age = static_cast<float>((clock - Fixed(mEmitted)) * static_cast<double>(d->mRateVary));
        if (d->mLife.y > age && n != 0) {
            // ORIGINAL-BUG: a negative count loops about 2^32 times
            int32_t left = n;
            do {
                cTSMetaParticle *const p = NewParticle();
                if (p) {
                    if (p->mAlive && Integrate(p, age))
                        AddParticle(p);
                    else
                        RetireParticle(p);
                }
            } while (--left != 0);
        }
        ++mEmitted;
        const uint32_t mode = d->mRateMode;
        if (mode != 0 && static_cast<uint32_t>(mEmitted) >= mode) {
            Stop(0);
            return;
        }
        if (d->m30.x >= 0.0f) {
            const float jitter = fx::RandomIn(d->m30.x, d->m30.y);
            const double clock2 = mEmitClock - static_cast<double>(jitter) * mEmitPeriod;
            mEmitClock = clock2;
            const double thr2 = Fixed(mEmitted);
            if (thr2 > clock2) {
                mEmitClock = thr2;
                mDelay = static_cast<float>(static_cast<double>(d->mRateVary) * (thr2 - clock2));
                return;
            }
        }
        clock = mEmitClock;
        thr = Fixed(mEmitted);
        if (clock < thr || clock != clock) return;
    }
}

// 0xbfdcb0: particles at the rate (scaled by the events, and by the effect's scale as the flags say), in pieces of at most 1/15 s.
void cTSMetaParticlesEffect::EmitContinuous(float step)
{
    const cTSMetaParticlesDescription *const d = mDescription;
    float rate = Mul(mRateScale, mEvent101[0]);
    const uint32_t low = LowFlags(d);
    const float sc = mLocal.scale;
    if (low & 0x80000)
        rate = Mul(rate, sc);
    else if (low & 0x100000)
        rate = Mul(Mul(sc, sc), rate);
    else if (low & 0x200000)
        rate = Mul(Mul(Mul(sc, sc), sc), rate);

    float left = step;
    bool hit = false;
    for (;;) {
        float chunk = 0.06666667f < left ? 0.06666667f : left;
        const int32_t first = mEmitted;
        const int32_t next = first + 1;
        const double clock = static_cast<double>(chunk) * mEmitPeriod + mEmitClock;
        mEmitClock = clock;
        const double thr = Fixed(next);
        int32_t at = first;
        double clockNow = clock;
        if (clock > thr) {
            mEmitted = next;
            at = next;
            const uint32_t maxCount = d->mRateMode;
            if (maxCount != 0 && static_cast<uint32_t>(next) == maxCount) {
                if (!(low & kFlagRestartable)) {
                    Stop(0);
                    return;
                }
                mEmitted = static_cast<int32_t>(maxCount - 1);
                at = static_cast<int32_t>(maxCount - 1);
                clockNow = static_cast<double>(Sub(static_cast<float>(Fixed(static_cast<int32_t>(d->mRateMode))), 1.0e-6f));
                mEmitClock = clockNow;
            } else if (d->m30.x >= 0.0f) {
                hit = true;
                mEmitClock = thr;
                clockNow = thr;
                chunk = static_cast<float>(static_cast<double>(chunk) - static_cast<double>(d->mRateVary) * (clock - thr));
            }
        }
        const float fraction = static_cast<float>(clockNow - Fixed(at));
        const float curve = fx::EvaluateCurve(d->mRate, fraction);
        const float x = Add(Mul(Mul(chunk, rate), curve), mEmitRemainder);
        const float shifted = Add(Sub(x, 0.5f), kMagic);
        int32_t n = static_cast<int32_t>(std::bit_cast<uint32_t>(shifted) + 0xb4c00000u);
        if (static_cast<float>(n + 1) == x) n = n + 1;
        mEmitRemainder = Sub(x, static_cast<float>(n));
        if (n > 0) {
            const float per = Div(chunk, static_cast<float>(n));
            float age = left;
            for (int32_t k = 0; k < n; ++k) {
                cTSMetaParticle *const p = NewParticle();
                if (p) {
                    if (!p->mAlive) {
                        RetireParticle(p);
                    } else if (Integrate(p, age)) {
                        mParticles.push_back(p);
                        ++mCount;
                    } else {
                        RetireParticle(p);
                    }
                }
                age = Sub(age, per);
            }
        }
        left = Sub(left, chunk);
        if (hit) {
            const float jitter = fx::RandomIn(d->m30.x, d->m30.y);
            left = Stored(Subr87Memory(static_cast<double>(jitter), left));
            if (0.0f > left) {
                mDelay = -left;
                return;
            }
        }
        if (!(left > 0.0f)) return;
    }
}

// 0xbffb30's tail is in cTSMetaParticlesEffect.cpp; this is slot 5, 0xbfe210.
void cTSMetaParticlesEffect::AddToScene(void *stepArg, void *statsArg)
{
    const float step = std::bit_cast<float>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stepArg)));
    int32_t *const stats = static_cast<int32_t *>(statsArg);
    const cTSMetaParticlesDescription *const d = mDescription;

    float local = step;
    if (mPrerollPending) {
        local = Add(step, d->mPreroll);
        mPrerollPending = false;
    }
    mTime = static_cast<double>(local) + mTime;
    if (LowFlags(d) & kFlagSurfaceQuadrant) ChooseSourceQuadrant(0);
    for (cTSParticleAttractorState &attractor : mAttractors) attractor.Follow(&mLocal);
    {
        const float k = d->mGlobalWind;
        const Vector3 &s = *reinterpret_cast<const Vector3 *>(mSettings->mBefore);
        mForce.x = Add(Mul(s.x, k), d->mForce.x);
        mForce.y = Add(Mul(s.y, k), d->mForce.y);
        mForce.z = Add(Mul(s.z, k), d->mForce.z);
    }
    if (LowFlags(d) & kFlagWalks) {
        for (const cTSMetaWalk &walk : mWalks) walk.Apply(&mForce);
    }
    if (!mColliderState.empty() && !mCollidersPlaced) {
        for (const cTSParticleColliderState &c : mColliders)
            VirtualCall<void(__thiscall *)(void *, const void *, void *, void *, void *), 4>(
                c.mObject, c.mDescription, &mLocal, &mWorld, mColliderState.data() + c.mStateOffset);
        mCollidersPlaced = true;
    }
    for (auto it = mParticles.begin(); it != mParticles.end();) {
        cTSMetaParticle *const p = *it;
        if (p->mAlive && Integrate(p, local)) {
            ++it;
        } else {
            it = mParticles.erase(it);
            RetireParticle(p);
            --mCount;
        }
    }
    if (!mFollowerSet) Emit(local);
    TickParticles();
    PruneSpawned();
    if (mStopping && mCount == 0 && mSpawned.empty()) {
        mRunning = false;
        return;
    }
    if (LowFlags(d) & kFlagWalks) TickWalks(local);
    stats[6] += mCount;
}
