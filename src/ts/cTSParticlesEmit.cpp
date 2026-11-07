// cTSParticlesEffect's emitters (lane WX, rendering row E6): Emit 0xbb97a0 and the three it picks from, 0xbb5c70 (continuous),
// 0xbbbfa0 (bursts) and 0xbbc560 (once per step). The layout and the rest of the class: src/ts/cTSParticlesEffect.h.
#include "ts/cTSParticlesEffect.h"

#include "game/game.h"
#include "rz/RZMemory.h"
#include "rz/cRZRandom.h"
#include "ts/ParticleCurve.h"
#include "ts/cTSParticlesDescription.h"

#include <bit>
#include <cstdint>

using fx::Add;
using fx::Div;
using fx::Mul;
using fx::Sub;
using fx::Vector3;

namespace {

constexpr float kChunk = 0.06666667f;       // 1/15 s: the continuous emitter's longest piece (0xa0e1df80)
constexpr float kTiny = 1e-06f;             // 0xa0db13d0: the velocity's size under which a particle is not stretched; the limit's margin
constexpr float kPrerollMargin = 0.01f;     // 0xa0da74e8
constexpr uint32_t kElementCell = 0x107cfc8, kBlockCell = 0x107cfcc, kBlockListCell = 0x107cfd0;
constexpr uint64_t kFlagBursts = 0x1, kFlagOnce = 0x2, kFlagKeepLimit = 0x4;
constexpr uint64_t kFlagRateByScale = 0x800, kFlagRateByScaleSquared = 0x1000, kFlagRateByScaleCubed = 0x2000;
constexpr uint64_t kFlagClears = 0x40000000;

cRZRandom &Random() { return *GameAddress<cRZRandom>(0x10fd138); }
float RandomIn(float low, float high)
{
    return static_cast<float>(Random().RandomDoubleRangeUniform(static_cast<double>(low), static_cast<double>(high)));
}

// An unsigned count as the game converts it (a signed conversion plus the table's 2^32 for the top bit).
double AsDouble(int32_t value) { return static_cast<double>(static_cast<uint32_t>(value)); }

// The count a float rounds up to by the 2^23 + 2^22 trick on x + 0.5: the nearest integer, one lower when that one equals x.
int32_t CountUp(float x)
{
    const float shifted = Add(Add(x, 0.5f), 12582912.0f);
    int32_t index = static_cast<int32_t>(std::bit_cast<uint32_t>(shifted) + 0xb4c00000u);
    if (static_cast<float>(index - 1) == x) index = index - 1;
    return index;
}

float Abs(float x) { return std::bit_cast<float>(std::bit_cast<uint32_t>(x) & 0x7fffffffu); }

// A cell of the particle pool. The emitters carve a new block (RZMalloc, the old block list's head in the first word, then the
// cells chained, the last one ending the chain) when the free list has run dry.
cTSParticle *TakeFromPool()
{
    void **const freeList = GameAddress<void *>(cTSParticlesEffect::kPoolFreeCell);
    uintptr_t cell = reinterpret_cast<uintptr_t>(*freeList);
    if (!cell) {
        uint32_t *const raw = static_cast<uint32_t *>(RZMemory::Malloc(*GameAddress<uint32_t>(kBlockCell) + 8));
        *raw = *GameAddress<uint32_t>(kBlockListCell);
        cell = reinterpret_cast<uintptr_t>(raw + 1);
        *GameAddress<uint32_t *>(kBlockListCell) = raw;
        const uint32_t count = *GameAddress<uint32_t>(kBlockCell) / *GameAddress<uint32_t>(kElementCell);
        const uintptr_t last = cell + (count - 1) * *GameAddress<uint32_t>(kElementCell);
        if (cell < last) {
            uintptr_t at = cell;
            do {
                *reinterpret_cast<uintptr_t *>(at) = at + *GameAddress<uint32_t>(kElementCell);
                at += *GameAddress<uint32_t>(kElementCell);
            } while (at < last);
        }
        *reinterpret_cast<uint32_t *>(last) = 0;
        *freeList = reinterpret_cast<void *>(cell);
    }
    *freeList = *reinterpret_cast<void **>(cell);
    return reinterpret_cast<cTSParticle *>(cell);
}

} // namespace

// The tail every emitter shares (they have it inlined, with a few operands the other way round).
void cTSParticlesEffect::LaunchParticle(cTSParticle *particle, float step, EmitStyle style)
{
    if (!particle->mAlive || !IntegrateParticle(particle, step)) {
        RetireParticle(particle);
        return;
    }
    AddParticle(particle);
    const cTSParticlesDescription *const d = mDescription;
    const cTSEffectCurve &screw = d->mScrewRadius;
    Vector3 position;
    if (screw.mKind == 0 && screw.mValues.size() == 1 && screw.mValues[0] == 0.0f)
        position = particle->mPosition;
    else
        fx::ParticlePosition(&position, particle, d, Div(particle->mAge, particle->mLife));

    // The longest component of the velocity (a NaN loses to the next one, as the original's compare and max do).
    const float ax = Abs(particle->mVelocity.x), ay = Abs(particle->mVelocity.y), az = Abs(particle->mVelocity.z);
    float longest = az > ay ? az : ay;
    longest = longest > ax ? longest : ax;
    cTSParticleBox &box = Box();
    if (kTiny > longest || d->mStretch == 0.0f) {
        box.Extend(&position);
        return;
    }
    const float factor = Div(Mul(Mul(mMaxSize, 0.5f), mSizeScale), d->mStretch);
    const Vector3 &v = particle->mVelocity;
    Vector3 offset;
    if (style == EmitStyle::kOnce)
        offset = {Mul(factor, v.x), Mul(factor, v.y), Mul(factor, v.z)};
    else
        offset = {Mul(v.x, factor), Mul(v.y, factor), Mul(v.z, factor)};
    Vector3 front;
    switch (style) {
    case EmitStyle::kContinuous:
        front = {Add(position.x, offset.x), Add(position.y, offset.y), Add(position.z, offset.z)};
        break;
    case EmitStyle::kBursts:
        front = {Add(offset.x, position.x), Add(offset.y, position.y), Add(offset.z, position.z)};
        break;
    default:
        front = {Add(offset.x, position.x), Add(position.y, offset.y), Add(offset.z, position.z)};
        break;
    }
    box.Extend(&front);
    const Vector3 back = {Sub(position.x, offset.x), Sub(position.y, offset.y), Sub(position.z, offset.z)};
    box.Extend(&back);
}

// 0xbb97a0.
void cTSParticlesEffect::Emit(float step)
{
    if (mStopping) return;
    if (0.0f < mDelay) {
        step = Sub(mDelay, step);
        mDelay = step;
        if (0.0f < step) return;
        step = -step;
    }
    const float preroll = mDescription->mPreroll;
    if (0.0f < preroll) {
        const float capped = Sub(preroll, kPrerollMargin);
        step = capped < step ? capped : step;
    }
    const uint64_t flags = mDescription->mFlags;
    if (flags & kFlagOnce)
        EmitOnce(step);
    else if (flags & kFlagBursts)
        EmitBursts(step);
    else
        EmitContinuous(step);
}

// 0xbb5c70. The pieces of a step are at most 1/15 s; in each the clock moves on by the piece, and one that passes the next whole
// number of emissions ticks the counter (and may end the effect). The curve at the clock's fraction gives a rate, the piece times
// it a (fractional) number of particles, and the rest carries over. A pause (the description's second pair of times) is taken at
// each tick, and the flag stays set for the pieces after it.
void cTSParticlesEffect::EmitContinuous(float step)
{
    if (0.0f >= step) return;
    const float whole = step;
    float rate = mScale17c;
    {
        const uint64_t flags = mDescription->mFlags;
        const float scale = mLocal.scale;
        if (flags & kFlagRateByScale)
            rate = Mul(rate, scale);
        else if (flags & kFlagRateByScaleSquared)
            rate = Mul(Mul(scale, scale), rate);
        else if (flags & kFlagRateByScaleCubed)
            rate = Mul(Mul(Mul(scale, scale), scale), rate);
    }
    const float dx = Sub(mLocal.translation.x, mPreviousPosition.x);
    const float dy = Sub(mLocal.translation.y, mPreviousPosition.y);
    const float dz = Sub(mLocal.translation.z, mPreviousPosition.z);
    if (mDescription->m160 > 0.0f) {
        const float length = fx::SqrtF(Add(Add(Mul(dx, dx), Mul(dy, dy)), Mul(dz, dz)));
        rate = Mul(Add(Mul(Div(length, whole), mDescription->m160), 1.0f), rate);
    }
    bool paused = false;
    float rest = whole;
    do {
        float piece = kChunk < rest ? kChunk : rest;
        const double pieceWide = static_cast<double>(piece);
        mEmitClock = pieceWide * mEmitPeriod + mEmitClock;
        const int32_t next = mEmitted + 1;
        if (mEmitClock > AsDouble(next)) {
            mEmitted = next;
            if (mDescription->mFlags & kFlagClears) ClearParticles();
            const cTSParticlesDescription *const d = mDescription;
            const uint32_t limit = d->mRateMode;
            if (limit != 0 && static_cast<uint32_t>(mEmitted) == limit) {
                if (!(d->mFlags & kFlagKeepLimit)) {
                    Stop(0);
                    return;
                }
                mEmitted = static_cast<int32_t>(limit - 1);
                mEmitClock = static_cast<double>(Sub(static_cast<float>(AsDouble(static_cast<int32_t>(limit))), kTiny));
            } else if (d->m30.x >= 0.0f) {
                paused = true;
                const double tick = AsDouble(mEmitted);
                const double over = mEmitClock - tick;
                mEmitClock = tick;
                piece = static_cast<float>(pieceWide - over * static_cast<double>(d->mRateVary));
            }
        }
        const cTSParticlesDescription *const d = mDescription;
        const float clockFraction = static_cast<float>(mEmitClock - AsDouble(mEmitted));
        const float wanted = Add(Mul(Mul(piece, rate), fx::EvaluateCurve(d->mRate, clockFraction)), mEmitRemainder);
        const int32_t count = fx::FindKey(wanted).mIndex;
        const float countF = static_cast<float>(count);
        mEmitRemainder = Sub(wanted, countF);
        const float perParticle = Div(piece, countF);
        if (count > 0) {
            float at = rest;
            for (int32_t left = count; left != 0; --left) {
                cTSParticle *const particle = TakeFromPool();
                InitParticle(particle);
                const float back = Div(at, whole);
                particle->mPosition.x = Sub(particle->mPosition.x, Mul(dx, back));
                particle->mPosition.y = Sub(particle->mPosition.y, Mul(dy, back));
                particle->mPosition.z = Sub(particle->mPosition.z, Mul(dz, back));
                LaunchParticle(particle, at, EmitStyle::kContinuous);
                at = Sub(at, perParticle);
            }
        }
        rest = Sub(rest, piece);
        if (paused) {
            rest = Sub(rest, RandomIn(mDescription->m30.x, mDescription->m30.y));
            if (0.0f > rest) {
                mDelay = -rest;
                return;
            }
        }
    } while (rest > 0.0f);
}

// 0xbbbfa0. Each time the clock passes the counter, a burst of the curve's count (at time 0, times the rate scale): each particle
// is advanced by the time the clock is past the tick. A count below zero loops the original's way (down through the wrap).
void cTSParticlesEffect::EmitBursts(float step)
{
    mEmitClock = static_cast<double>(step) * mEmitPeriod + mEmitClock;
    const double clock = mEmitClock;
    if (AsDouble(mEmitted) > clock) return;
    const int32_t count = CountUp(Mul(fx::EvaluateCurve(mDescription->mRate, 0.0f), mScale17c));
    if (clock < AsDouble(mEmitted)) return;
    for (;;) {
        if (mDescription->mFlags & kFlagClears) ClearParticles();
        const cTSParticlesDescription *d = mDescription;
        const float late = static_cast<float>((mEmitClock - AsDouble(mEmitted)) * static_cast<double>(d->mRateVary));
        if (d->mLife.y > late && count != 0) {
            for (int32_t left = count; left != 0; --left) {
                cTSParticle *const particle = TakeFromPool();
                InitParticle(particle);
                LaunchParticle(particle, late, EmitStyle::kBursts);
            }
        }
        d = mDescription;
        const uint32_t ticks = static_cast<uint32_t>(++mEmitted);
        if (d->mRateMode != 0 && d->mRateMode <= ticks) {
            Stop(0);
            return;
        }
        if (d->m30.x >= 0.0f) {
            const float pause = RandomIn(d->m30.x, d->m30.y);
            const double wide = static_cast<double>(pause);
            const double tick = AsDouble(mEmitted);
            const double moved = mEmitClock - wide * mEmitPeriod;
            mEmitClock = moved;
            if (tick > moved) {
                mEmitClock = tick;
                mDelay = static_cast<float>(static_cast<double>(mDescription->mRateVary) * (tick - moved));
                return;
            }
        }
        if (mEmitClock < AsDouble(mEmitted)) return;
    }
}

// 0xbbc560. When the clock has passed the counter, the live particles are made up to the curve's count (at time 0).
void cTSParticlesEffect::EmitOnce(float step)
{
    mEmitClock = static_cast<double>(step) * mEmitPeriod + mEmitClock;
    if (AsDouble(mEmitted) > mEmitClock) return;
    const float wanted = fx::EvaluateCurve(mDescription->mRate, 0.0f);
    // ORIGINAL-BUG: a count under the live particles' makes the loop run down through zero (about 2^32 particles).
    int32_t left = CountUp(wanted) - mCount;
    for (; left != 0; --left) {
        cTSParticle *const particle = TakeFromPool();
        InitParticle(particle);
        LaunchParticle(particle, step, EmitStyle::kOnce);
    }
}
