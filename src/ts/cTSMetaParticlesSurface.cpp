// The meta-particles effect and the surface it lives over (lane XA, rendering row E8): the collision with the height field and
// the placing of a new particle on it. The surface object is the owner's: slot 5 the height at (x, y) (on the x87 stack), slot
// 6 the normal there. Scalar SSE single with the first-operand NaN rule of fx::Add / fx::Mul.
#include "ts/cTSMetaParticlesEffect.h"

#include "compgeom/Rigid.h"
#include "game/game.h"
#include "game/vtable.h"
#include "ts/ParticleCurve.h"
#include "ts/ParticleMath.h"
#include "ts/cTSParticlesDescription.h"

#include <cmath>
#include <cstdint>

using fx::Add;
using fx::Div;
using fx::EffectsTransform;
using fx::Mul;
using fx::SqrtF;
using fx::Sub;
using fx::Vector3;

namespace {

using SurfaceHeight = float(__thiscall *)(void *, float, float);
using SurfaceNormal = Vector3 *(__thiscall *)(void *, Vector3 *, float, float);

constexpr uint32_t kLowTerrainFollow = 0x400, kLowTerrainRepel = 0x100, kLowKeepBelow = 0x80, kLowHeightRange = 0x10000,
                   kLowOnTerrain = 0x4000, kLowOnBase = 0x8000, kLowWalkHeading = 0x1000000;
constexpr uint32_t kHighCeiling = 2;

} // namespace

// 0xbfe480.
bool cTSMetaParticlesEffect::CollideSurface(cTSMetaParticle *particle, const Vector3 *where, float step)
{
    void *const surface = mSurface.Get();
    if (!surface) return true;
    const cTSMetaParticlesDescription *const d = mDescription;
    const uint32_t low = static_cast<uint32_t>(d->mFlags);
    const uint32_t high = static_cast<uint32_t>(d->mFlags >> 32);
    Vector3 *const position = &particle->mPosition;
    Vector3 *const velocity = &particle->mVelocity;
    Vector3 point = *where;
    if (high & kHighCeiling) {
        // A ceiling over the effect: a particle above it is put back (a step back along its velocity if it is rising) and loses
        // the speed into it.
        Vector3 ceiling = mLocal.translation;
        ceiling.z = Add(ceiling.z, d->mLimitHeight);
        mWorld.Apply(&ceiling);
        if (where->z > ceiling.z) {
            if (velocity->z > 0.0f) {
                position->x = Sub(position->x, Mul(velocity->x, step));
                position->y = Sub(position->y, Mul(velocity->y, step));
                position->z = Sub(position->z, Mul(velocity->z, step));
            }
            if (position->z > ceiling.z) position->z = ceiling.z;
            point = *position;
            mWorld.Apply(&point);
            const Vector3 down = {0.0f, 0.0f, -1.0f};
            const float into = -static_cast<float>(nGZCompGeom::Dot87(&velocity->x, &down.x));
            if (into > 0.0f) {
                velocity->x = Add(Mul(into, 0.0f), velocity->x);
                velocity->y = Add(Mul(into, 0.0f), velocity->y);
                velocity->z = Sub(velocity->z, Mul(into, 1.0f));
            }
        }
    }
    if (low & kLowTerrainFollow) {
        const float height = VirtualCall<SurfaceHeight, 5>(surface, point.x, point.y);
        *position = {point.x, point.y, height};
        mWorld.ToLocal(position);
        return true;
    }
    if (low & kLowTerrainRepel) {
        // A field pushing the particle up and off the surface, stronger the nearer it is (as cTSParticlesEffect's).
        const float k = d->mTerrainRepel[2];
        const Vector3 v = *velocity;
        const float sx = Mul(v.x, k), sy = Mul(v.y, k), sz = Mul(v.z, k);
        const float length = SqrtF(Add(Add(Mul(v.y, v.y), Mul(v.x, v.x)), Mul(v.z, v.z)));
        const float divisor = length > 10.0f ? length : 10.0f;
        Vector3 p = {Add(Div(sx, divisor), position->x), Add(Div(sy, divisor), position->y), Add(Div(sz, divisor), position->z)};
        mWorld.Apply(&p);
        if (!(p.x > 0.0f) || !(p.y > 0.0f) || !(mSurfaceRange[0] > p.x) || !(mSurfaceRange[1] > p.y)) return true;
        const float over = p.z - VirtualCall<SurfaceHeight, 5>(surface, p.x, p.y);
        if (d->mTerrainRepel[4] > over) return false;
        if (!(d->mTerrainRepel[0] > over)) return true;
        Vector3 buffer;
        const Vector3 n = *VirtualCall<SurfaceNormal, 6>(surface, &buffer, p.x, p.y);
        const float t = Sub(d->mTerrainRepel[0], over);
        const float time = Mul(step, d->mTerrainRepel[1]);
        const float t2 = Mul(t, t);
        const float pushX = Mul(Mul(n.x, t2), time);
        const float pushY = Mul(Mul(n.y, t2), time);
        const float pushZ = Mul(Mul(Mul(n.z, t2), d->mTerrainRepel[3]), time);
        if (!(low & kLowWalkHeading)) {
            velocity->x = Add(pushX, velocity->x);
            velocity->y = Add(pushY, velocity->y);
            velocity->z = Add(pushZ, velocity->z);
            return true;
        }
        // The push turns the particle's walk instead.
        cTSMetaWalk &walk = particle->mWalks[0];
        if (walk.mHorizontal > 0.001f) {
            const float s = static_cast<float>(std::sin(static_cast<double>(walk.mHeading)));
            const float sinsum = Add(s, Div(pushY, walk.mHorizontal));
            const float c = static_cast<float>(std::cos(static_cast<double>(walk.mHeading)));
            const float cossum = Add(c, Div(pushX, walk.mHorizontal));
            walk.mHeading = static_cast<float>(std::atan2(static_cast<double>(sinsum), static_cast<double>(cossum)));
        }
        if (walk.mVertical > 0.001f) {
            const float c = static_cast<float>(std::cos(static_cast<double>(walk.mPitch)));
            walk.mPitch = static_cast<float>(std::acos(static_cast<double>(Add(c, Div(pushZ, walk.mVertical)))));
        }
        return true;
    }
    if (d->mBounce == 0.0f) {
        // No bounce: a particle under the surface is put on it and loses the speed into it.
        const float height = VirtualCall<SurfaceHeight, 5>(surface, point.x, point.y);
        if (!(low & kLowKeepBelow) && !(height > point.z)) return true;
        *position = {point.x, point.y, height};
        mWorld.ToLocal(position);
        Vector3 buffer;
        const Vector3 n = *VirtualCall<SurfaceNormal, 6>(surface, &buffer, point.x, point.y);
        const float into = -static_cast<float>(nGZCompGeom::Dot87(&velocity->x, &n.x));
        if (!(into > 0.0f)) return true;
        velocity->x = Add(Mul(n.x, into), velocity->x);
        velocity->y = Add(Mul(n.y, into), velocity->y);
        velocity->z = Add(Mul(n.z, into), velocity->z);
        return true;
    }
    // A bounce: only a particle under the surface, and then the surface's height is asked again (twice in all).
    if (!(VirtualCall<SurfaceHeight, 5>(surface, point.x, point.y) > point.z)) return true;
    const float height = VirtualCall<SurfaceHeight, 5>(surface, point.x, point.y);
    if (mHasSurfaceEffect[0]) {
        const Vector3 hit = {point.x, point.y, height};
        SpawnSurfaceEffect("_terrain", &hit);
    }
    const float death = d->mDeath;
    if (death != 0.0f) {
        if (death == 1.0f) return false;
        if (death >= static_cast<float>(fx::GameRandom().RandomDoubleUniform())) return false;
    }
    *position = {point.x, point.y, height};
    mWorld.ToLocal(position);
    Vector3 buffer;
    Vector3 n = *VirtualCall<SurfaceNormal, 6>(surface, &buffer, point.x, point.y);
    fx::MetaRotateBack(&mWorld, nullptr, &n);
    const double dot = nGZCompGeom::Dot87(&n.x, &velocity->x);
    const float k = static_cast<float>(dot * static_cast<double>(Add(d->mBounce, 1.0f)));
    // The original subtracts with subss from the velocity, so a NaN in the velocity wins over one in n * k.
    velocity->x = fx::FirstNaN(velocity->x, velocity->x - Mul(n.x, k));
    velocity->y = fx::FirstNaN(velocity->y, velocity->y - Mul(n.y, k));
    velocity->z = fx::FirstNaN(velocity->z, velocity->z - Mul(n.z, k));
    return true;
}

// 0xbff8d0.
bool cTSMetaParticlesEffect::SnapToSurface(cTSMetaParticle *particle)
{
    const cTSMetaParticlesDescription *const d = mDescription;
    const uint32_t low = static_cast<uint32_t>(d->mFlags);
    Vector3 p = particle->mPosition;
    mWorld.Rotate(&p);
    const float sx = Add(Mul(p.x, mWorld.scale), mWorld.translation.x);
    const float sy = Add(Mul(p.y, mWorld.scale), mWorld.translation.y);
    void *const surface = mSurface.Get();
    if (!surface || !(sx >= 0.0f) || !(sy >= 0.0f) || !(mSurfaceRange[0] > sx) || !(mSurfaceRange[1] > sy)) {
        particle->mAlive = 0;
        return false;
    }
    const float height = VirtualCall<SurfaceHeight, 5>(surface, sx, sy);
    if (low & kLowHeightRange) {
        const float rel = Sub(height, mSurfaceBase);
        if (d->mHeightRange.x > rel || rel > d->mHeightRange.y) {
            particle->mAlive = 0;
            return false;
        }
    }
    float z;
    if (low & kLowOnTerrain) {
        z = height;
        if ((low & kLowOnBase) && mSurfaceBase > height) z = mSurfaceBase;
    } else {
        if (!(low & kLowOnBase)) return true;
        z = mSurfaceBase;
    }
    z = Add(fx::RandomIn(mSource.mMin.z, mSource.mMax.z), z);
    p = {sx, sy, z};
    mWorld.ToLocal(&p);
    particle->mPosition = p;
    return true;
}
