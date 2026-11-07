// How a particle moves (lane WV, rendering row E5): the integrators and the path they follow. Declarations in
// src/ts/cTSParticlesEffect.h and src/ts/cTSParticlesDescription.h. All scalar SSE single with the first-operand NaN rule of
// fx::Add / fx::Mul; the comparisons are written the way the original's comiss / jb pairs take them (a NaN goes the "below" way).
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSParticlesEffect.h"

#include "compgeom/ConvexMath.h"
#include "compgeom/Rigid.h"
#include "ts/ParticleCurve.h"

using fx::Add;
using fx::Div;
using fx::Mul;
using fx::Sub;
using fx::Vector3;

namespace {

constexpr float kSubstep = 0.06666667f;      // the longest piece a step is cut into (1/15 s)
constexpr float kSimpleLimit = 0.05f;        // above this step times drag, the step is cut into pieces
constexpr uint64_t kSubstepFlags = 0x04028018; // the description's flag bits that need pieces (the game's global at 0x107dda8)

// The cubic's weights at u, in the order the original computes them (Evaluate and SteerAlongPath share the arithmetic).
struct Basis {
    float a, b, c, d, e, f, g;
    explicit Basis(float u)
    {
        const float u2 = Mul(u, u), u3 = Mul(u2, u), twoU = Add(u, u);
        a = Sub(Mul(u2, 3.0f), twoU);
        b = Sub(u3, u2);
        c = Sub(u2, Add(b, b));
        d = Add(Sub(a, twoU), 1.0f);
        e = Sub(1.0f, c);
        f = Sub(twoU, Add(a, a));
        g = Add(Sub(b, u2), u);
    }
};

// The age moved on by the step: false (and the particle dead) when the life is used up. A NaN on either side lives on.
bool Age(cTSParticle *p, float step)
{
    const float age = Add(step, p->mAge);
    p->mAge = age;
    if (age >= p->mLife) {
        p->mAlive = 0;
        return false;
    }
    return true;
}

Vector3 ToFx(const fxdesc::Vector3 &v) { return {v.x, v.y, v.z}; }

// A direction through the transform's rotation, then its scale (the original's order of operands: the scale first).
void RotateScale(const fx::EffectsTransform &t, Vector3 *v)
{
    t.Rotate(v);
    v->x = Mul(t.scale, v->x);
    v->y = Mul(t.scale, v->y);
    v->z = Mul(t.scale, v->z);
}

// The move of a path point since the last one, rotated (the z row sums its last term first here), times the scale, onto the
// position.
void FollowPoint(cTSParticle *p, const fx::EffectsTransform &t, const cTSParticlePathPoint &key)
{
    float x = Sub(key.mPosition.x, p->mPathPosition.x);
    float y = Sub(key.mPosition.y, p->mPathPosition.y);
    float z = Sub(key.mPosition.z, p->mPathPosition.z);
    if (t.flags & fx::EffectsTransform::kRotated) {
        const float(&m)[3][3] = t.m;
        const float nx = fx::RowFromY(x, y, z, m[0][0], m[0][1], m[0][2]);
        const float ny = fx::RowFromY(x, y, z, m[1][0], m[1][1], m[1][2]);
        const float nz = Add(Mul(z, m[2][2]), Add(Mul(y, m[2][1]), Mul(x, m[2][0])));
        x = nx;
        y = ny;
        z = nz;
    }
    p->mPosition.x = Add(Mul(t.scale, x), p->mPosition.x);
    p->mPosition.y = Add(Mul(t.scale, y), p->mPosition.y);
    p->mPosition.z = Add(Mul(t.scale, z), p->mPosition.z);
    p->mPathPosition = ToFx(key.mPosition);
}

} // namespace

// 0xbba1b0.
bool cTSParticlePath::Evaluate(float t, cTSParticlePathPoint *out) const
{
    const cTSParticlePathPoint *const begin = mPoints.data();
    const cTSParticlePathPoint *const end = begin + mPoints.size();
    const float first = begin == end ? 0.0f : begin->mTime;
    const float last = begin == end ? 0.0f : end[-1].mTime;
    const cTSParticlePathPoint *extend = nullptr;
    if (first > t) {
        if (!(mFlags & 1)) return false;
        extend = begin;
    } else if (t > last) {
        if (!(mFlags & 2)) return false;
        extend = end - 1;
    }
    if (extend) { // before the first point or after the last: carried on at the velocity there
        out->mTime = t;
        out->mVelocity = extend->mVelocity;
        const float dt = Sub(t, extend->mTime);
        out->mPosition = {Add(Mul(out->mVelocity.x, dt), extend->mPosition.x), Add(Mul(out->mVelocity.y, dt), extend->mPosition.y),
                          Add(Mul(out->mVelocity.z, dt), extend->mPosition.z)};
        return true;
    }
    const cTSParticlePathPoint *const lastKey = end - 1;
    const cTSParticlePathPoint *next = begin;
    while (next < lastKey && t >= next->mTime) ++next;
    if (next == begin || begin >= lastKey) { // at or before the first point of two or more, or the only point
        *out = *begin;
        return true;
    }
    const cTSParticlePathPoint &p = next[-1], &n = *next;
    const float dur = Sub(n.mTime, p.mTime);
    if (1.0e-6f >= dur) { // (a NaN interpolates)
        *out = p;
        return true;
    }
    const Basis w(Div(Sub(t, p.mTime), dur));
    const float gd = Mul(w.g, dur), bd = Mul(w.b, dur), fd = Div(w.f, dur), nfd = Div(Sub(0.0f, w.f), dur);
    const float *const pa = &p.mPosition.x, *const pb = &p.mVelocity.x, *const na = &n.mPosition.x, *const nb = &n.mVelocity.x;
    float *const oa = &out->mPosition.x, *const ob = &out->mVelocity.x;
    for (int i = 0; i < 3; ++i)
        oa[i] = Add(Add(Add(Mul(pa[i], w.e), Mul(na[i], w.c)), Mul(pb[i], gd)), Mul(nb[i], bd));
    for (int i = 0; i < 3; ++i)
        ob[i] = Add(Add(Add(Mul(na[i], fd), Mul(pa[i], nfd)), Mul(pb[i], w.d)), Mul(nb[i], w.a));
    out->mTime = t;
    return true;
}

// 0xbb8c00.
void cTSParticlesEffect::SteerAlongPath(cTSParticle *particle, float step)
{
    const cTSParticlePath &path = mDescription->mPath;
    const cTSParticlePathPoint *const lastKey = path.mPoints.data() + path.mPoints.size() - 1;
    const cTSParticlePathPoint *key = path.mPoints.data();
    const float age = particle->mAge;
    while (key < lastKey && age >= key->mTime) ++key;
    Vector3 &pos = particle->mPosition;
    Vector3 &vel = particle->mVelocity;
    float x;
    if (key->mTime != 0.0f) {
        if (age > key->mTime) { // past the last point: straight on
            pos.x = Add(Mul(vel.x, step), pos.x);
            pos.y = Add(Mul(vel.y, step), pos.y);
            pos.z = Add(Mul(vel.z, step), pos.z);
            return;
        }
        x = key->mTime;
    } else {
        x = particle->mLife;
    }
    const float h = Add(Sub(x, age), step);
    Vector3 a = ToFx(key->mPosition), b = ToFx(key->mVelocity);
    mLocal.Rotate(&a);
    mLocal.Rotate(&b);
    const float scale = mLocal.scale;
    a = {Add(Mul(a.x, scale), mLocal.translation.x), Add(Mul(a.y, scale), mLocal.translation.y),
         Add(Mul(a.z, scale), mLocal.translation.z)};
    b = {Mul(b.x, scale), Mul(b.y, scale), Mul(b.z, scale)};
    const Basis w(Div(step, h));
    const float bh = Mul(w.b, h), gh = Mul(w.g, h), fh = Div(w.f, h), nfh = Div(Sub(0.0f, w.f), h);
    const Vector3 p0 = pos, v0 = vel;
    pos = {Add(Add(Add(Mul(a.x, w.c), Mul(p0.x, w.e)), Mul(v0.x, gh)), Mul(b.x, bh)),
           Add(Add(Add(Mul(a.y, w.c), Mul(p0.y, w.e)), Mul(v0.y, gh)), Mul(b.y, bh)),
           Add(Add(Add(Mul(a.z, w.c), Mul(p0.z, w.e)), Mul(v0.z, gh)), Mul(b.z, bh))};
    vel = {Add(Add(Add(Mul(a.x, fh), Mul(p0.x, nfh)), Mul(v0.x, w.d)), Mul(b.x, w.a)),
           Add(Add(Add(Mul(p0.y, nfh), Mul(a.y, fh)), Mul(v0.y, w.d)), Mul(b.y, w.a)),
           Add(Add(Add(Mul(p0.z, nfh), Mul(a.z, fh)), Mul(v0.z, w.d)), Mul(b.z, w.a))};
}

// 0xbc12a0.
bool cTSParticlesEffect::IntegrateSimple(cTSParticle *particle, float step)
{
    if (!Age(particle, step)) return false;
    const Vector3 old = particle->mVelocity;
    const float keep = 1.0f - Mul(step, mDescription->mDrag);
    const float half = Mul(step, 0.5f);
    Vector3 &vel = particle->mVelocity;
    vel.x = Mul(Add(Mul(mGravity.x, step), old.x), keep);
    vel.y = Mul(Add(Mul(mGravity.y, step), old.y), keep);
    vel.z = Mul(Add(Mul(mGravity.z, step), old.z), keep);
    Vector3 &pos = particle->mPosition;
    pos.x = Add(Mul(Add(vel.x, old.x), half), pos.x);
    pos.y = Add(Mul(Add(vel.y, old.y), half), pos.y);
    pos.z = Add(Mul(Add(vel.z, old.z), half), pos.z);
    return true;
}

// 0xbc13c0.
bool cTSParticlesEffect::IntegrateSubsteps(cTSParticle *particle, float step)
{
    if (!Age(particle, step)) return false;
    float remaining = step;
    do {
        const float dt = kSubstep < remaining ? kSubstep : remaining;
        const Vector3 old = particle->mPosition;
        Vector3 &pos = particle->mPosition;
        Vector3 &vel = particle->mVelocity;
        const Vector3 v = vel;
        pos.x = Add(Mul(v.x, dt), pos.x);
        pos.y = Add(Mul(v.y, dt), pos.y);
        pos.z = Add(Mul(v.z, dt), pos.z);
        vel.x = Add(Mul(mGravity.x, dt), vel.x);
        vel.y = Add(Mul(mGravity.y, dt), vel.y);
        vel.z = Add(Mul(mGravity.z, dt), vel.z);
        if (!ApplyForces(particle, &old, dt)) return false;
        remaining = Sub(remaining, dt);
    } while (remaining > 0.0f);
    return particle->mAlive != 0;
}

// 0xbc19f0.
bool cTSParticlesEffect::IntegrateSteeredSubsteps(cTSParticle *particle, float step)
{
    if (!Age(particle, step)) return false;
    float remaining = step;
    do {
        const float dt = kSubstep < remaining ? kSubstep : remaining;
        const Vector3 old = particle->mPosition;
        SteerAlongPath(particle, dt);
        Vector3 &vel = particle->mVelocity;
        vel.x = Add(Mul(mGravity.x, dt), vel.x);
        vel.y = Add(Mul(mGravity.y, dt), vel.y);
        vel.z = Add(Mul(mGravity.z, dt), vel.z);
        if (!ApplyForces(particle, &old, dt)) return false;
        remaining = Sub(remaining, dt);
    } while (remaining > 0.0f);
    return particle->mAlive != 0;
}

// 0xbc1530.
bool cTSParticlesEffect::IntegrateSteered(cTSParticle *particle, float step)
{
    if (!Age(particle, step)) return false;
    Vector3 &vel = particle->mVelocity;
    {
        // The path's last velocity is taken back out (it comes in again at the end of the step).
        Vector3 last = particle->mPathVelocity;
        mLocal.Rotate(&last);
        vel.x = Sub(vel.x, Mul(last.x, mLocal.scale));
        vel.y = Sub(vel.y, Mul(last.y, mLocal.scale));
        vel.z = Sub(vel.z, Mul(last.z, mLocal.scale));
    }
    cTSParticlePathPoint key = {};
    if (mDescription->mPath.Evaluate(particle->mAge, &key)) FollowPoint(particle, mLocal, key);
    particle->mPathVelocity = ToFx(key.mVelocity);
    Vector3 add = ToFx(key.mVelocity);
    mLocal.Rotate(&add);
    add = {Mul(add.x, mLocal.scale), Mul(add.y, mLocal.scale), Mul(add.z, mLocal.scale)};
    float remaining = step;
    do {
        const float dt = kSubstep < remaining ? kSubstep : remaining;
        const Vector3 old = particle->mPosition;
        Vector3 &pos = particle->mPosition;
        const Vector3 v = vel;
        pos.x = Add(Mul(v.x, dt), pos.x);
        pos.y = Add(Mul(v.y, dt), pos.y);
        pos.z = Add(Mul(v.z, dt), pos.z);
        vel.x = Add(Mul(mGravity.x, dt), vel.x);
        vel.y = Add(Mul(mGravity.y, dt), vel.y);
        vel.z = Add(Mul(mGravity.z, dt), vel.z);
        if (!ApplyForces(particle, &old, dt)) return false;
        remaining = Sub(remaining, dt);
    } while (remaining > 0.0f);
    const bool alive = particle->mAlive != 0;
    vel.x = Add(add.x, vel.x);
    vel.y = Add(add.y, vel.y);
    vel.z = Add(add.z, vel.z);
    return alive;
}

// 0xbbcb70.
bool cTSParticlesEffect::IntegrateOnPath(cTSParticle *particle, float step)
{
    if (!Age(particle, step)) return false;
    cTSParticlePathPoint key = {};
    if (mDescription->mPath.Evaluate(particle->mAge, &key)) FollowPoint(particle, mLocal, key);
    particle->mVelocity = ToFx(key.mVelocity);
    particle->mPathVelocity = ToFx(key.mVelocity);
    RotateScale(mLocal, &particle->mVelocity);
    return true;
}

// 0xbc08b0.
bool cTSParticlesEffect::IntegrateOnPathPlaced(cTSParticle *particle, float step)
{
    if (!Age(particle, step)) return false;
    cTSParticlePathPoint key = {};
    if (mDescription->mPath.Evaluate(particle->mAge, &key)) {
        particle->mPosition = ToFx(key.mPosition);
        particle->mVelocity = ToFx(key.mVelocity);
        mLocal.Apply(&particle->mPosition);
    }
    RotateScale(mLocal, &particle->mVelocity);
    return true;
}

// 0xbc1190.
bool cTSParticlesEffect::IntegrateParticle(cTSParticle *particle, float step)
{
    const cTSParticlesDescription *const d = mDescription;
    const uint64_t flags = d->mFlags;
    const uint32_t high = static_cast<uint32_t>(flags >> 32);
    if (high & 4) return high & 8 ? IntegrateOnPathPlaced(particle, step) : IntegrateOnPath(particle, step);
    if (high & 2) return high & 8 ? IntegrateSteeredSubsteps(particle, step) : IntegrateSteered(particle, step);
    if (flags & kSubstepFlags) return IntegrateSubsteps(particle, step);
    if (Mul(step, d->mDrag) > kSimpleLimit) return IntegrateSubsteps(particle, step);
    return IntegrateSimple(particle, step);
}

// 0xbb9ea0.
void __cdecl fx::ParticlePosition(Vector3 *out, const cTSParticle *particle, const cTSParticlesDescription *d, float t)
{
    using nGZCompGeom::convex::Mul87;
    using nGZCompGeom::convex::Stored;
    float radius = Stored(Mul87(fx::EvaluateCurve(d->mScrewRadius, t), particle->mScrewRadius));
    if (!(radius == 0.0f)) {
        const float vx = particle->mVelocity.x, vy = particle->mVelocity.y, vz = particle->mVelocity.z;
        const float speed = fx::SqrtF(Add(Add(Mul(vx, vx), Mul(vy, vy)), Mul(vz, vz)));
        if (!(1.0e-6f > speed)) {
            float angle = Stored(Mul87(Mul87(fx::EvaluateCurve(d->mScrewAngle, t), particle->mScrewAngle), static_cast<double>(6.2831855f)));
            if (d->mScrewAngleToSpeed == 1) angle = Mul(angle, speed);
            else if (d->mScrewAngleToSpeed == 2) angle = Div(angle, speed);
            if (d->mScrewRadiusToSpeed == 1) radius = Mul(speed, radius);
            else if (d->mScrewRadiusToSpeed == 2) radius = Div(radius, speed);
            const float limit = d->mScrewRadiusMax;
            if (radius > limit) radius = limit;
            else radius = -limit > radius ? -limit : radius;

            Vector3 side{1.0f, 0.0f, 0.0f}; // straight up or down the velocity's x-y plane has no side: the x axis
            if (!(vx == 0.0f && vy == 0.0f)) {
                const float nx = -vx;
                const float length = fx::SqrtF(Add(Add(Mul(nx, nx), Mul(vy, vy)), 0.0f));
                side = {Div(vy, length), Div(nx, length), Div(0.0f, length)};
            }
            float offset[3] = {Mul(side.x, radius), Mul(side.y, radius), Mul(side.z, radius)};
            const float inverse = Div(1.0f, speed);
            const float half = Mul(angle, 0.5f);
            const float s = fx::SinF(half);
            const float q[4] = {Mul(Mul(vx, inverse), s), Mul(Mul(vy, inverse), s), Mul(Mul(vz, inverse), s), fx::CosF(half)};
            nGZCompGeom::RotateByQuaternionOrdered(offset, q);
            const Vector3 &p = particle->mPosition;
            *out = {Add(p.x, offset[0]), Add(p.y, offset[1]), Add(p.z, offset[2])};
            return;
        }
    }
    *out = particle->mPosition;
}
