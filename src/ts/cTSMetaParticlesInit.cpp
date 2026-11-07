// A new meta-particle (lane XA, rendering row E8): InitParticle 0xc006f0, the one function that writes a whole particle, and the
// ellipsoid helper 0xbdd5e0 it uses. Declared in src/ts/cTSMetaParticlesEffect.h. Scalar SSE single with the first-operand NaN
// rule of fx::Add / fx::Mul; the comparisons are written the way the original's comiss / jb pairs take them, and the game's
// randoms are drawn in the original's order.
#include "ts/cTSMetaParticlesEffect.h"

#include "compgeom/Rigid.h"
#include "game/game.h"
#include "game/vtable.h"
#include "ts/ParticleCurve.h"
#include "ts/ParticleMath.h"
#include "ts/cTSParticlesDescription.h"

#include <cmath>
#include <cstdint>
#include <new>
#include <string>

using fx::Add;
using fx::Div;
using fx::EffectsTransform;
using fx::Mul;
using fx::RandomIn;
using fx::SqrtF;
using fx::Vector3;

namespace {

constexpr uint32_t kLowSizeAlongVelocity = 0x10, kLowSurfaceSnap = 0x7c000, kLowEllipsoid = 0x800, kLowScaleSize = 0x400000,
                   kLowWalks = 0x1000000, kLowWalkRandomStart = 0x2000000, kLowFromPath = 0x40000000;
constexpr float kTurn = 0.15915494f; // 1 / 2 pi

// A random around 1: in [1 - x, x + 1).
float Around1(float x) { return RandomIn(fx::Sub(1.0f, x), Add(x, 1.0f)); }
// A subtraction that keeps the first operand's NaN, as subss does.
float Minus(float a, float b) { return fx::FirstNaN(a, a - b); }

// The visual-effect face of what the owner made (its own slots, as cTSMetaParticlesEffect.cpp's).
bool Make(void *owner, const char *name, void *effect)
{
    return VirtualCall<bool(__thiscall *)(void *, const char *, void *), 14>(owner, name, effect);
}

} // namespace

namespace fx {

// 0xbdd5e0 (cdecl): a random point of the box's ellipsoid by rejection: each of three randoms in its axis, kept when the sum of
// the squares over the squared half extents is at most 1 (a NaN loops on). Returns `out`.
Vector3 *__cdecl MetaRandomInEllipsoid(Vector3 *out, const float *box)
{
    float centre[3], inverse[3];
    for (int i = 0; i < 3; ++i) {
        centre[i] = Mul(Add(box[i], box[i + 3]), 0.5f);
        const float half = Minus(box[i + 3], centre[i]);
        const float square = Mul(half, half);
        inverse[i] = square > 0.0f ? Div(1.0f, square) : 0.0f;
    }
    float dot;
    do {
        out->x = RandomIn(box[0], box[3]);
        out->y = RandomIn(box[1], box[4]);
        out->z = RandomIn(box[2], box[5]);
        const float dx = Minus(out->x, centre[0]), dy = Minus(out->y, centre[1]), dz = Minus(out->z, centre[2]);
        const float squares[3] = {Mul(dx, dx), Mul(dy, dy), Mul(dz, dz)};
        dot = static_cast<float>(nGZCompGeom::Dot87(squares, inverse));
    } while (!(1.0f >= dot));
    return out;
}

} // namespace fx

// 0xc006f0 (ret 4).
void cTSMetaParticlesEffect::InitParticle(cTSMetaParticle *particle)
{
    const cTSMetaParticlesDescription *const d = mDescription;
    const uint32_t low = static_cast<uint32_t>(d->mFlags);
    Vector3 *const position = &particle->mPosition;
    Vector3 *const velocity = &particle->mVelocity;
    particle->mLife = RandomIn(d->mLife.x, d->mLife.y);
    particle->mAge = 0.0f;

    // The path: the effect's points, or the description's when it has none; a first point at time 0 starts the particle.
    const std::vector<cTSParticlePathPoint> &points = mPath.empty() ? d->mPath.mPoints : mPath;
    bool fromPath = false;
    float speed = 0.0f;
    Vector3 direction = {0.0f, 0.0f, 0.0f};
    if ((low & kLowFromPath) && !points.empty() && points.front().mTime == 0.0f) {
        const cTSParticlePathPoint &first = points.front();
        *position = {first.mPosition.x, first.mPosition.y, first.mPosition.z};
        *velocity = {first.mVelocity.x, first.mVelocity.y, first.mVelocity.z};
        fromPath = true;
        const float last = points.back().mTime;
        if (last > 0.0f) particle->mLife = last;
    } else {
        Vector3 at;
        const float *const box = &mSource.mMin.x;
        if (d->mSourceRing >= 0.0f) fx::RandomOnRing(&at, box, d->mSourceRing);
        else if (low & kLowEllipsoid) fx::MetaRandomInEllipsoid(&at, box);
        else fx::RandomInBox(&at, box);
        *position = at;

        direction.x = RandomIn(d->m38.x, d->m44.x);
        direction.y = RandomIn(d->m38.y, d->m44.y);
        direction.z = RandomIn(d->m38.z, d->m44.z);
        speed = Mul(RandomIn(d->mEmitVolume.x, d->mEmitVolume.y), mSpeedScale);
        *velocity = direction;
        const float length2 = Add(Add(Mul(direction.x, direction.x), Mul(direction.y, direction.y)), Mul(direction.z, direction.z));
        if (length2 > 0.0f) {
            const float k = Div(speed, SqrtF(length2));
            *velocity = {Mul(k, direction.x), Mul(k, direction.y), Mul(k, direction.z)};
        }
        // A bomb pushes away from its centre.
        if (d->mBomb > 0.0f) {
            const float dx = Minus(position->x, d->mBombCentre.x), dy = Minus(position->y, d->mBombCentre.y),
                        dz = Minus(position->z, d->mBombCentre.z);
            const float distance2 = Add(Add(Mul(dx, dx), Mul(dy, dy)), Mul(dz, dz));
            if (distance2 > 1e-6f) {
                const float k = Div(d->mBomb, SqrtF(distance2));
                velocity->x = Add(Mul(dx, k), velocity->x);
                velocity->y = Add(Mul(k, dy), velocity->y);
                velocity->z = Add(Mul(dz, k), velocity->z);
            }
        }
    }

    // Into the effect's space: the position rotated, scaled and moved; the velocity only rotated.
    mLocal.Apply(position);
    mLocal.Rotate(velocity);

    if (mSurface.Get() && (d->mFlags & kLowSurfaceSnap) != 0) {
        if (!SnapToSurface(particle)) return;
    }

    if (!fromPath && (low & kLowSizeAlongVelocity)) {
        // ORIGINAL-BUG: the size curve is read at 0, not at the particle's age (as cTSParticlesEffect's).
        const float curve = fx::EvaluateCurve(d->mSize, 0.0f);
        position->x = Add(Div(Mul(Mul(velocity->x, 0.5f), curve), speed), position->x);
        position->y = Add(Div(Mul(Mul(velocity->y, 0.5f), curve), speed), position->y);
        position->z = Add(Div(Mul(Mul(velocity->z, 0.5f), curve), speed), position->z);
    }

    particle->mSizeVary = Mul(Mul(Around1(d->mSizeVary), mLifeScale), mEvent101[1]);
    particle->mWord28 = Around1(d->mAlphaVary);
    particle->mAlpha = Around1(d->mAspectVary);
    particle->mColor.x = Around1(d->mColorVary.x);
    particle->mColor.y = Around1(d->mColorVary.y);
    particle->mColor.z = Around1(d->mColorVary.z);
    float spin = 1.0f;
    if (d->mRotateVary > 0.0f) spin = RandomIn(-d->mRotateVary, d->mRotateVary);
    particle->mSpin = spin;
    if (low & kLowScaleSize) particle->mSizeVary = Mul(mLocal.scale, particle->mSizeVary);
    const float turn = static_cast<float>(std::atan2(static_cast<double>(mLocal.m[1][0]), static_cast<double>(mLocal.m[0][0])));
    if (d->mAlignment == 1) particle->mSpin = Add(Mul(turn, kTurn), spin);
    particle->mBank = 0.0f;
    if (static_cast<uint8_t>(d->mAlignment - 2) <= 2) particle->mHeading = -999.0f;
    else particle->mHeading = Mul(turn, kTurn);

    // The particle's own effect: the description's name, made by the owner, put where the particle is.
    const std::string name = d->mEffect;
    particle->mEffect.Reset();
    if (Make(mOwner.Get(), name.c_str(), particle->mEffect.Address())) {
        void *const effect = particle->mEffect.Get();
        if (mHasSpawnArgument) VirtualCall<void(__thiscall *)(void *, uint32_t, void *), 14>(effect, 1, mSpawnArgument.Get());
        if (!mNameOverride.mString.mString.empty()) {
            // A string of its own for the effect to keep (nobody here releases it).
            cRZString *const text = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
            text->Construct(mNameOverride.mString, 0, 0xffffffffu);
            text->mRefCount = 0;
            VirtualCall<void(__thiscall *)(void *, uint32_t, void *), 14>(effect, 0, text);
        }
        if (mFirstFrame >= 0.0f) VirtualCall<bool(__thiscall *)(void *, uint32_t, void *, void *), 13>(effect, 0xd, &mFirstFrame, reinterpret_cast<void *>(1));
        VirtualCall<void(__thiscall *)(void *, const EffectsTransform *), 9>(effect, &mWorld);
        EffectsTransform place;
        place.Reset();
        place.translation = *position;
        place.flags |= EffectsTransform::kTranslated;
        ++place.Version();
        VirtualCall<void(__thiscall *)(void *, const EffectsTransform *), 10>(effect, &place);
        if (!mVisible) VirtualCall<void(__thiscall *)(void *, bool), 8>(effect, true);
        VirtualCall<void(__thiscall *)(void *, uint32_t), 3>(effect, 0);
    }

    if (!fromPath && (low & kLowWalks)) {
        const bool randomStart = (low & kLowWalkRandomStart) != 0;
        for (int i = 0; i < 2; ++i) particle->mWalks[i].Init(&d->mWalks[i], &direction, randomStart);
    }
    particle->mAlive = 1;
}
