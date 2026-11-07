// The meta-particles effect's particle: how one particle moves (lane XA, rendering row E8). The object, slots and small helpers
// are in cTSMetaParticlesEffect.cpp, the declarations in cTSMetaParticlesEffect.h. Scalar SSE single with the first-operand NaN
// rule of fx::Add / fx::Mul.
#include "ts/cTSMetaParticlesEffect.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ts/ParticleCurve.h"
#include "ts/ParticleMath.h"

#include <cstdint>

using fx::Add;
using fx::Div;
using fx::EffectsTransform;
using fx::Mul;
using fx::RowFromY;
using fx::SqrtF;
using fx::Sub;
using fx::Vector3;

// 0xbff400.
void __fastcall fx::MetaRotateBack(const EffectsTransform *transform, void *, Vector3 *vector)
{
    if (!(transform->flags & EffectsTransform::kRotated)) return;
    const Vector3 in = *vector;
    const float(&m)[3][3] = transform->m;
    *vector = {RowFromY(in.x, in.y, in.z, m[0][0], m[1][0], m[2][0]), RowFromY(in.x, in.y, in.z, m[0][1], m[1][1], m[2][1]),
               RowFromY(in.x, in.y, in.z, m[0][2], m[1][2], m[2][2])};
}

namespace {

Vector3 Own(const fxdesc::Vector3 &v) { return {v.x, v.y, v.z}; }
uint32_t LowFlags(const cTSMetaParticlesDescription *d) { return static_cast<uint32_t>(d->mFlags); }

// The cubic's weights at `u` of a segment `dt` long: for the position those of the start and end positions and velocities, for
// the velocity (the derivative) the same four.
struct HermiteWeights {
    float startPosition, endPosition, startVelocity, endVelocity;     // the position
    float dEndPosition, dStartPosition, dStartVelocity, dEndVelocity; // the velocity
};
HermiteWeights Hermite(float u, float dt)
{
    const float u2 = Mul(u, u);
    const float twice = Add(u, u);
    const float cube = Sub(Mul(u2, u), u2);
    const float w3 = Sub(Mul(u2, 3.0f), twice);
    const float endPosition = Sub(u2, Add(cube, cube));
    const float dStartVelocity = Add(Sub(w3, twice), 1.0f);
    const float startPosition = Sub(1.0f, endPosition);
    const float slope = Sub(twice, Add(w3, w3));
    HermiteWeights w;
    w.startPosition = startPosition;
    w.endPosition = endPosition;
    w.endVelocity = Mul(cube, dt);
    w.startVelocity = Mul(Add(Sub(cube, u2), u), dt);
    w.dEndPosition = Div(slope, dt);
    w.dStartPosition = Div(Sub(0.0f, slope), dt);
    w.dStartVelocity = dStartVelocity;
    w.dEndVelocity = w3;
    return w;
}

float Sum4(float a0, float b0, float a1, float b1, float a2, float b2, float a3, float b3)
{
    return Add(Add(Add(Mul(a0, b0), Mul(a1, b1)), Mul(a2, b2)), Mul(a3, b3));
}

// The first point whose time is past `age`, else the last (the original's unrolled search, one loop). An empty list reads one
// element at its start (undefined; the callers have points).
const cTSParticlePathPoint *FindSegment(const cTSParticlePathPoint *begin, const cTSParticlePathPoint *end, float age)
{
    const cTSParticlePathPoint *p = begin;
    const uintptr_t last = reinterpret_cast<uintptr_t>(end) - sizeof(cTSParticlePathPoint);
    if (reinterpret_cast<uintptr_t>(begin) < last) {
        do {
            if (!(age >= p->mTime)) break; // comiss, jb: taken on NaN
            ++p;
        } while (reinterpret_cast<uintptr_t>(p) < last);
    }
    return p;
}

} // namespace

// 0xbfed50.
void cTSMetaParticlesEffect::Steer(cTSMetaParticle *particle, float step)
{
    const cTSParticlePathPoint *begin = mPath.data();
    const cTSParticlePathPoint *end = begin + mPath.size();
    if (mPath.empty()) {
        begin = mDescription->mPath.mPoints.data();
        end = begin + mDescription->mPath.mPoints.size();
    }
    const float age = particle->mAge;
    const cTSParticlePathPoint *const point = FindSegment(begin, end, age);
    float endTime = point->mTime;
    if (endTime == 0.0f) {
        endTime = particle->mLife;
    } else if (endTime < age) { // past the last point: straight on
        particle->mPosition.x = Add(Mul(particle->mVelocity.x, step), particle->mPosition.x);
        particle->mPosition.y = Add(Mul(particle->mVelocity.y, step), particle->mPosition.y);
        particle->mPosition.z = Add(Mul(particle->mVelocity.z, step), particle->mPosition.z);
        return;
    }
    Vector3 target = Own(point->mPosition), targetVelocity = Own(point->mVelocity);
    float dt = Add(Sub(endTime, age), step);
    mLocal.Rotate(&target);
    const float scale = mLocal.scale;
    const Vector3 &shift = mLocal.translation;
    const float targetX = Add(Mul(target.x, scale), shift.x), targetY = Add(Mul(target.y, scale), shift.y);
    float targetZ = Add(Mul(target.z, scale), shift.z);
    mLocal.Rotate(&targetVelocity);
    const float velocityX = Mul(targetVelocity.x, scale), velocityY = Mul(targetVelocity.y, scale), velocityZ = Mul(targetVelocity.z, scale);
    const cTSMetaParticlesDescription *const d = mDescription;
    if (LowFlags(d) & 0x8000) targetZ = Sub(mSurfaceBase, mWorld.translation.z);
    const float dz = Sub(particle->mPosition.z, targetZ);
    if (d->mTractorResetSpeed != 0.0f) {
        const float dx = Sub(particle->mPosition.x, targetX), dy = Sub(particle->mPosition.y, targetY);
        const float distance = SqrtF(Add(Add(Mul(dx, dx), Mul(dy, dy)), Mul(dz, dz)));
        if (Mul(d->mTractorResetSpeed, dt) < distance) { // too far from the path: the age goes back to the previous point's
            float before = 0.0f;
            if (begin < point) before = point[-1].mTime;
            dt = Add(dt, Sub(age, before));
            particle->mAge = Sub(age, Sub(age, before));
        }
    }
    const HermiteWeights w = Hermite(Div(step, dt), dt);
    const Vector3 position = particle->mPosition, velocity = particle->mVelocity;
    particle->mPosition.x = Add(
        Add(Add(Mul(position.x, w.startPosition), Mul(targetX, w.endPosition)), Mul(velocity.x, w.startVelocity)),
        Mul(w.endVelocity, velocityX));
    particle->mPosition.y = Add(
        Add(Add(Mul(position.y, w.startPosition), Mul(targetY, w.endPosition)), Mul(velocity.y, w.startVelocity)),
        Mul(w.endVelocity, velocityY));
    particle->mPosition.z = Add(
        Add(Add(Mul(position.z, w.startPosition), Mul(targetZ, w.endPosition)), Mul(velocity.z, w.startVelocity)),
        Mul(w.endVelocity, velocityZ));
    particle->mVelocity.x = Add(
        Add(Add(Mul(targetX, w.dEndPosition), Mul(position.x, w.dStartPosition)), Mul(velocity.x, w.dStartVelocity)),
        Mul(w.dEndVelocity, velocityX));
    particle->mVelocity.y = Add(
        Add(Add(Mul(targetY, w.dEndPosition), Mul(position.y, w.dStartPosition)), Mul(velocity.y, w.dStartVelocity)),
        Mul(w.dEndVelocity, velocityY));
    particle->mVelocity.z = Add(
        Add(Add(Mul(targetZ, w.dEndPosition), Mul(position.z, w.dStartPosition)), Mul(velocity.z, w.dStartVelocity)),
        Mul(w.dEndVelocity, velocityZ));
}

// 0xc02f30.
bool cTSMetaParticlesEffect::FollowPath(cTSMetaParticle *particle, float)
{
    const cTSMetaParticlesDescription *const d = mDescription;
    const cTSParticlePathPoint *begin = mPath.data();
    const cTSParticlePathPoint *end = begin + mPath.size();
    if (mPath.empty()) {
        begin = d->mPath.mPoints.data();
        end = begin + d->mPath.mPoints.size();
    }
    const float age = particle->mAge;
    const cTSParticlePathPoint *const point = FindSegment(begin, end, age);
    float endTime = point->mTime;
    if (endTime == 0.0f) endTime = particle->mLife;
    else if (endTime < age) return false;
    Vector3 startPosition, startVelocity;
    float startTime;
    if (begin < point) { // the segment's start is the point before
        startPosition = Own(point[-1].mPosition);
        startVelocity = Own(point[-1].mVelocity);
        startTime = point[-1].mTime;
    } else { // the first: from the middles of the description's direction and source boxes
        const float speed = d->mEmitVolume.x;
        startPosition = {Mul(Add(d->mSourceMin.x, d->mSourceMax.x), 0.5f), Mul(Add(d->mSourceMin.y, d->mSourceMax.y), 0.5f),
                         Mul(Add(d->mSourceMin.z, d->mSourceMax.z), 0.5f)};
        startVelocity = {Mul(Mul(Add(d->m38.x, d->m44.x), 0.5f), speed), Mul(Mul(Add(d->m38.y, d->m44.y), 0.5f), speed),
                         Mul(Mul(Add(d->m38.z, d->m44.z), 0.5f), speed)};
        startTime = 0.0f;
    }
    const float dt = Sub(endTime, startTime);
    if (1e-06f >= dt) return true; // comiss, jae: not taken on NaN
    const HermiteWeights w = Hermite(Div(Sub(age, startTime), dt), dt);
    const cTSParticlePathPoint &e = *point;
    particle->mPosition = {
        Sum4(w.startPosition, startPosition.x, w.endPosition, e.mPosition.x, w.startVelocity, startVelocity.x, e.mVelocity.x,
             w.endVelocity),
        Sum4(startPosition.y, w.startPosition, e.mPosition.y, w.endPosition, startVelocity.y, w.startVelocity, e.mVelocity.y,
             w.endVelocity),
        Sum4(startPosition.z, w.startPosition, e.mPosition.z, w.endPosition, startVelocity.z, w.startVelocity, e.mVelocity.z,
             w.endVelocity)};
    particle->mVelocity = {
        Sum4(w.dEndPosition, e.mPosition.x, w.dStartPosition, startPosition.x, w.dStartVelocity, startVelocity.x, e.mVelocity.x,
             w.dEndVelocity),
        Sum4(e.mPosition.y, w.dEndPosition, startPosition.y, w.dStartPosition, startVelocity.y, w.dStartVelocity, e.mVelocity.y,
             w.dEndVelocity),
        Sum4(e.mPosition.z, w.dEndPosition, startPosition.z, w.dStartPosition, startVelocity.z, w.dStartVelocity, e.mVelocity.z,
             w.dEndVelocity)};
    mLocal.Rotate(&particle->mPosition);
    const float scale = mLocal.scale;
    particle->mPosition = {Mul(scale, particle->mPosition.x), Mul(scale, particle->mPosition.y), Mul(scale, particle->mPosition.z)};
    particle->mPosition = {Add(particle->mPosition.x, mLocal.translation.x), Add(particle->mPosition.y, mLocal.translation.y),
                           Add(particle->mPosition.z, mLocal.translation.z)};
    mLocal.Rotate(&particle->mVelocity);
    particle->mVelocity = {Mul(scale, particle->mVelocity.x), Mul(scale, particle->mVelocity.y), Mul(scale, particle->mVelocity.z)};
    return true;
}

// 0xc04a00 (ret 8): a step cut into pieces of 1/15 s, each: the move (or the steering), the force, the walks, the attractors, the drag, then the
// colliders and the surface over the new position. false (and not alive): an attractor's stop, a collider or the surface killed it.
bool cTSMetaParticlesEffect::IntegrateSubsteps(cTSMetaParticle *particle, float step)
{
    constexpr float kPiece = 0.06666667f;
    float remaining = step;
    const cTSMetaParticlesDescription *const d = mDescription;
    do {
        const Vector3 oldPosition = particle->mPosition;
        const float dt = kPiece < remaining ? kPiece : remaining; // minss: a NaN step is the step
        const uint32_t low = LowFlags(d);
        Vector3 *const position = &particle->mPosition;
        Vector3 *const velocity = &particle->mVelocity;
        if (low & 0x20000000) {
            Steer(particle, dt);
        } else {
            const Vector3 v = *velocity;
            position->x = Add(Mul(v.x, dt), position->x);
            position->y = Add(Mul(v.y, dt), position->y);
            position->z = Add(Mul(v.z, dt), position->z);
        }
        {
            const Vector3 f = mForce;
            velocity->x = Add(Mul(f.x, dt), velocity->x);
            velocity->y = Add(Mul(f.y, dt), velocity->y);
            velocity->z = Add(Mul(f.z, dt), velocity->z);
        }
        if (low & 0x1000000) {
            Vector3 push = {0.0f, 0.0f, 0.0f};
            cTSMetaWalk &first = particle->mWalks[0], &second = particle->mWalks[1];
            first.mClock = Sub(first.mClock, dt);
            second.mClock = Sub(second.mClock, dt);
            const bool firstDone = 0.0f > first.mClock;
            const bool secondDone = 0.0f > second.mClock;
            if (firstDone) first.Next(&d->mWalks[0]);
            first.Apply(&push);
            if (secondDone) second.Next(&d->mWalks[1]);
            second.Apply(&push);
            velocity->x = Add(Mul(push.x, dt), velocity->x);
            velocity->y = Add(Mul(push.y, dt), velocity->y);
            velocity->z = Add(Mul(push.z, dt), velocity->z);
        }
        if (!mAttractors.empty()) {
            char stop;
            Vector3 pull;
            const Vector3 *const f = cTSParticleAttractorState::Sum(&mAttractors, nullptr, &pull, position, &stop);
            velocity->x = Add(Mul(f->x, dt), velocity->x);
            velocity->y = Add(Mul(f->y, dt), velocity->y);
            velocity->z = Add(Mul(f->z, dt), velocity->z);
            if (stop) {
                particle->mAlive = 0;
                return false;
            }
        }
        const float keep = Sub(1.0f, Mul(dt, d->mDrag));
        velocity->x = Mul(velocity->x, keep);
        velocity->y = Mul(velocity->y, keep);
        velocity->z = Mul(velocity->z, keep);
        if (!mColliders.empty() && !CollideColliders(particle, &oldPosition)) {
            particle->mAlive = 0;
            return false;
        }
        // The new position in the surface's space.
        Vector3 point = *position;
        mWorld.Rotate(&point);
        point = {Add(Mul(point.x, mWorld.scale), mWorld.translation.x), Add(Mul(point.y, mWorld.scale), mWorld.translation.y),
                 Add(Mul(point.z, mWorld.scale), mWorld.translation.z)};
        if (point.x > 0.0f && point.y > 0.0f && mSurfaceRange[0] > point.x && mSurfaceRange[1] > point.y) {
            if (low & 0x140) {
                if (!CollideSurface(particle, &point, dt)) {
                    particle->mAlive = 0;
                    return false;
                }
            }
        } else if (low & 0x200) { // off the surface's edge
            particle->mAlive = 0;
            return false;
        }
        remaining = Sub(remaining, dt);
    } while (remaining > 0.0f);
    return particle->mAlive != 0;
}

// 0xc04840 (ret 8).
bool cTSMetaParticlesEffect::Integrate(cTSMetaParticle *particle, float step)
{
    const float age = Add(step, particle->mAge);
    particle->mAge = age;
    if (age >= particle->mLife) { // comiss, jb: a NaN age goes on
        particle->mAge = particle->mLife;
        particle->mAlive = 0;
        return false;
    }
    const cTSMetaParticlesDescription *const d = mDescription;
    const uint32_t low = LowFlags(d);
    if (low & 0x40000000) return FollowPath(particle, step);
    // The flags that need the sub-steps: 0x29000360 of the low word (the high word's mask is zero), or drag worth more than 0.05 a step.
    if ((low & 0x29000360u) != 0 || Mul(step, d->mDrag) > 0.05f)
        return IntegrateSubsteps(particle, step);
    // One Euler step, trapezoid on the position.
    const Vector3 force = mForce, velocity = particle->mVelocity;
    float vx = Add(Mul(force.x, step), velocity.x), vy = Add(Mul(force.y, step), velocity.y), vz = Add(Mul(force.z, step), velocity.z);
    const float half = Mul(step, 0.5f);
    const float keep = Sub(1.0f, Mul(step, mDescription->mDrag));
    vx = Mul(vx, keep);
    vy = Mul(vy, keep);
    vz = Mul(vz, keep);
    particle->mVelocity = {vx, vy, vz};
    particle->mPosition = {Add(Mul(Add(vx, velocity.x), half), particle->mPosition.x),
                           Add(Mul(Add(vy, velocity.y), half), particle->mPosition.y),
                           Add(Mul(Add(vz, velocity.z), half), particle->mPosition.z)};
    return true;
}
