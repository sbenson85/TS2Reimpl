// A new particle (lane WV, rendering row E5): InitParticle 0xbbb390, the one function that writes a whole particle, drawing the
// game's randoms in the original's order. Declared in src/ts/cTSParticlesEffect.h. All scalar single with the first-operand NaN
// rule of fx::Add / fx::Mul; the comparisons are written the way the original's comiss / jb pairs take them.
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSParticlesEffect.h"

#include "compgeom/Frames.h"
#include "compgeom/Rigid.h"
#include "game/vtable.h"
#include "ts/ParticleCurve.h"
#include "ts/ParticleMath.h"

#include <cmath>

using fx::Add;
using fx::Div;
using fx::Mul;
using fx::RandomIn;
using fx::Sub;
using fx::Vector3;

namespace {

constexpr uint32_t kFlagVelocityFromPosition = 0x40; // low word: the velocity points away from the centre
constexpr uint32_t kFlagEllipsoid = 0x100;           // low word: the position is in an ellipsoid, not the box
constexpr uint32_t kFlagStretch = 0x20;              // low word: stretched along the view's axes
constexpr uint32_t kFlagPlaceAtStart = 0x8;          // high word: a path starting at time 0 places the particle at its first point
constexpr uint32_t kFlagFacingAxis = 0x20;           // high word: a random facing axis
constexpr float kTinyLength = 1.4210856e-20f;        // a squared velocity this small (ordered) is replaced by (speed, 0, 0)

// A random around 1: in [1 - x, x + 1).
float Around1(float x) { return RandomIn(Sub(1.0f, x), Add(x, 1.0f)); }
// A random in [-x, x), plus a half (the x87 sum of the stored float and the constant, rounded to a float).
float AroundHalf(float x)
{
    const float r = RandomIn(-x, x);
    return static_cast<float>(static_cast<double>(r) + 0.5);
}

} // namespace

// 0xbbb390 (ret 4).
void cTSParticlesEffect::InitParticle(cTSParticle *p)
{
    const cTSParticlesDescription *const d = mDescription;
    const uint32_t lowFlags = static_cast<uint32_t>(d->mFlags);
    const uint32_t highFlags = static_cast<uint32_t>(d->mFlags >> 32);
    p->mAlive = 1;
    p->mAge = 0.0f;
    p->mLife = RandomIn(d->mLife.x, d->mLife.y);
    p->mFrameOffset = static_cast<uint8_t>(mFirstFrame);

    // The path's first point: its position (and velocity) are the particle's start, when it is placed at time 0 the position too.
    bool placed = false;
    bool haveVelocity = false;
    float speed = 0.0f; // the velocity's length before the transform; 0 when the path gave the velocity
    if (!d->mPath.mPoints.empty()) {
        const cTSParticlePathPoint &first = d->mPath.mPoints.front();
        p->mPathPosition = {first.mPosition.x, first.mPosition.y, first.mPosition.z};
        if ((highFlags & kFlagPlaceAtStart) && first.mTime == 0.0f) {
            p->mPosition = {first.mPosition.x, first.mPosition.y, first.mPosition.z};
            placed = true;
        }
        p->mVelocity = {first.mVelocity.x, first.mVelocity.y, first.mVelocity.z};
        p->mPathVelocity = p->mVelocity;
        haveVelocity = true;
    }

    if (!placed) {
        Vector3 at;
        const float *const box = &mSource.mMin.x;
        if (d->mSourceRing >= 0.0f) fx::RandomOnRing(&at, box, d->mSourceRing);
        else if (lowFlags & kFlagEllipsoid) fx::RandomInEllipsoid(&at, box);
        else fx::RandomInBox(&at, box);
        p->mPosition = at;

        if (!haveVelocity) {
            if (lowFlags & kFlagVelocityFromPosition) {
                p->mVelocity = p->mPosition;
            } else {
                Vector3 v;
                fx::RandomInBox(&v, &d->m38.x);
                p->mVelocity = v;
            }
            speed = Mul(RandomIn(d->mEmitVolume.x, d->mEmitVolume.y), mSpeedScale);
            const Vector3 v = p->mVelocity;
            const float length2 = Add(Add(Mul(v.x, v.x), Mul(v.y, v.y)), Mul(v.z, v.z));
            if (kTinyLength >= std::fabs(length2)) {
                p->mVelocity = {speed, 0.0f, 0.0f};
            } else {
                const float scale = Div(speed, fx::SqrtF(length2));
                p->mVelocity = {Mul(scale, v.x), Mul(scale, v.y), Mul(scale, v.z)};
            }
            // A bomb pushes away from its centre (a NaN strength counts as set).
            const float bomb = d->mBomb;
            if (!(bomb == 0.0f)) {
                const Vector3 from = p->mPosition;
                const float dx = Sub(from.x, d->mBombCentre.x), dy = Sub(from.y, d->mBombCentre.y), dz = Sub(from.z, d->mBombCentre.z);
                const float distance2 = Add(Add(Mul(dx, dx), Mul(dy, dy)), Mul(dz, dz));
                if (distance2 > 0.0f) {
                    const float k = Div(bomb, fx::SqrtF(distance2));
                    p->mVelocity = {Add(Mul(dx, k), p->mVelocity.x), Add(Mul(dy, k), p->mVelocity.y), Add(Mul(dz, k), p->mVelocity.z)};
                }
            }
        }
    }

    // Into the effect's space: the position rotated, scaled and moved; the velocity only rotated.
    mLocal.Apply(&p->mPosition);
    mLocal.Rotate(&p->mVelocity);

    if (highFlags & kFlagFacingAxis) {
        Vector3 raw;
        raw.x = RandomIn(d->m38.x, d->m44.x);
        raw.y = RandomIn(d->m38.y, d->m44.y);
        raw.z = RandomIn(d->m38.z, d->m44.z);
        Vector3 axis;
        fx::Normalize(&axis, &raw);
        p->mAxis = axis;
        mLocal.Rotate(&p->mAxis);
    } else {
        p->mAxis = {0.0f, 0.0f, 0.0f};
    }

    // Stretched along the view: the position moves along the velocity by the size curve and the velocity's length in the view's
    // axes (its first and third columns) over the stretch.
    if ((lowFlags & kFlagStretch) && mScene.Get()) {
        float view[7]; // slot 140 (the view's transform, 0x1c bytes: position, then the rotation quaternion) fills this
        const float *const transform =
            VirtualCall<const float *(__thiscall *)(void *, float *, int), 140>(mScene.Get(), view, 0);
        nGZCompGeom::FrameMath frame{};
        frame.mUnknown0 = 1;
        frame.SetRotation(transform + 3);
        const float across[3] = {frame.mMatrix[2], frame.mMatrix[5], frame.mMatrix[8]};
        const float along[3] = {frame.mMatrix[0], frame.mMatrix[3], frame.mMatrix[6]};
        const float velocity[3] = {p->mVelocity.x, p->mVelocity.y, p->mVelocity.z};
        const float dotAcross = static_cast<float>(nGZCompGeom::Dot87(velocity, across));
        const float dotAlong = static_cast<float>(nGZCompGeom::Dot87(velocity, along));
        const float length = fx::SqrtF(Add(Mul(dotAlong, dotAlong), Mul(dotAcross, dotAcross)));
        const float ratio = Div(length, d->mStretch);
        // ORIGINAL-BUG (probably): the size curve is read at 0, not at the ratio the code just computed.
        const float curve = fx::EvaluateCurve(d->mSize, 0.0f);
        p->mPosition = {Add(Div(Mul(Mul(Mul(velocity[0], 0.5f), ratio), curve), speed), p->mPosition.x),
                        Add(Div(Mul(Mul(Mul(velocity[1], 0.5f), ratio), curve), speed), p->mPosition.y),
                        Add(Div(Mul(Mul(Mul(velocity[2], 0.5f), ratio), curve), speed), p->mPosition.z)};
    }

    p->mSize = Mul(Around1(d->mSizeVary), mSpawnSize);
    p->mAspect = Around1(d->mAlphaVary);
    p->mPivot[0] = AroundHalf(d->mPivotVary.x);
    p->mPivot[1] = AroundHalf(d->mPivotVary.y);
    p->mExtra = RandomIn(-d->mPivotVary.z, d->mPivotVary.z);
    p->mAlpha = Around1(d->mAspectVary);
    p->mColor = {Around1(d->mColorVary.x), Around1(d->mColorVary.y), Around1(d->mColorVary.z)};
    p->mScrewRadius = Around1(d->mScrewRadiusVary);
    p->mScrewAngle = Around1(d->mScrewAngleVary);

    // The rotations: at least one, a pair of bounds each (a range gives a random, otherwise the lower bound), written from +0x50
    // on for as many as the description counts (the count is a byte: more than the two the description holds read on past them).
    const unsigned count = d->mRotateCount > 1 ? d->mRotateCount : 1u;
    const float *const bounds = reinterpret_cast<const float *>(reinterpret_cast<const uint8_t *>(d) + 0x180);
    float *const rotations = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(p) + 0x50);
    for (unsigned i = 0; i < count; ++i) {
        const float low = bounds[2 * i], high = bounds[2 * i + 1];
        rotations[i] = high > low ? RandomIn(low, high) : low;
    }

    if (mConstrain.Get()) {
        VirtualCall<void(__thiscall *)(void *, fx::EffectsTransform *, fx::EffectsTransform *, Vector3 *, const void *), 3>(
            mConstrain.Get(), &mLocal, &mWorld, &p->mPosition, &d->mConstrainOffset);
    }
}
