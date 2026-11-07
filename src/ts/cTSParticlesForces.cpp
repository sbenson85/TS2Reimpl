// What pushes a particle (lane WV, rendering row E5): the attractors' forces and the effect's collision and force steps.
// Declarations in src/ts/cTSParticlesEffect.h. All scalar SSE single with the first-operand NaN rule of fx::Add / fx::Mul.
#include "ts/ParticleCurve.h"
#include "ts/ParticleMath.h"
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSParticlesEffect.h"

#include "compgeom/Rigid.h"
#include "game/vtable.h"

#include <cmath>

using fx::Add;
using fx::Mul;
using fx::Vector3;

// 0xbe01c0.
void __cdecl fx::SpawnEffect(cIGZUnknown *owner, const char *name, const Vector3 *position, uint32_t argument,
                             std::vector<CountedRef<cIGZUnknown>> *keep, EffectsTransform *parent)
{
    cIGZUnknown *effect = nullptr;
    if (VirtualCall<bool(__thiscall *)(void *, const char *, cIGZUnknown **), 14>(owner, name, &effect)) {
        EffectsTransform place;
        place.Reset();
        place.translation = *position;
        place.flags |= EffectsTransform::kTranslated;
        ++place.Version();
        VirtualCall<void(__thiscall *)(void *, EffectsTransform *), 10>(effect, &place);
        if (parent) VirtualCall<void(__thiscall *)(void *, EffectsTransform *), 9>(effect, parent);
        VirtualCall<void(__thiscall *)(void *, uint32_t), 8>(effect, argument);
        VirtualCall<void(__thiscall *)(void *, uint32_t), 3>(effect, 0);
        if (keep) keep->emplace_back(effect);
    }
    if (effect) effect->Release();
}

// 0xbdf6a0.
bool __cdecl fx::SweepColliders(cIGZUnknown *owner, std::vector<cTSParticleColliderState> *colliders, std::vector<char> *scratch,
                                float distance, const Vector3 *vector, Vector3 *position, Vector3 *velocity, uint32_t argument,
                                std::vector<CountedRef<cIGZUnknown>> *keep, EffectsTransform *parent)
{
    if (VirtualCall<bool(__thiscall *)(void *, uint32_t), 8>(owner, 2)) return true;
    for (const cTSParticleColliderState *it = colliders->data(); it != colliders->data() + colliders->size(); ++it) {
        const cTSParticleCollider *const d = it->mDescription;
        Vector3 normal;
        if (!VirtualCall<bool(__thiscall *)(void *, const cTSParticleCollider *, char *, float, const Vector3 *, Vector3 *, Vector3 *), 5>(
                it->mObject, d, scratch->data() + it->mStateOffset, distance, vector, position, &normal))
            continue;
        const float chance = d->m50;
        if (chance != 0.0f &&
            (chance == 1.0f || chance >= static_cast<float>(GameRandom().RandomDoubleUniform()))) {
            if (!d->m6C.empty()) SpawnEffect(owner, d->m6C.c_str(), position, argument, keep, parent);
            return false;
        }
        if (!d->m54.empty()) SpawnEffect(owner, d->m54.c_str(), position, argument, keep, parent);
        float dot = static_cast<float>(nGZCompGeom::Dot87(&velocity->x, &normal.x));
        if (!(0.0f > dot)) continue;
        const float drag = d->m48;
        if (drag != 0.0f) {
            const float x = velocity->x, y = velocity->y, z = velocity->z;
            const float lengthSquared = Add(Add(Mul(x, x), Mul(y, y)), Mul(z, z));
            if (!(drag >= lengthSquared)) {
                const float s = SqrtF(1.0f - drag / lengthSquared);
                velocity->x = Mul(x, s);
                velocity->y = Mul(y, s);
                velocity->z = Mul(z, s);
                dot = Mul(s, dot);
            } else {
                // A velocity inside the drag is replaced by a global's three floats (0x10fd170, nothing writes it: a zero
                // vector, if it stays as the image's loader leaves it) and this collider does no bounce.
                *velocity = *GameAddress<Vector3>(0x10fd170);
                continue;
            }
        }
        const Vector3 v = *velocity;
        const float a = 1.0f - d->m44, b = d->m40 + 1.0f;
        const float tx = Mul(normal.x, dot), ty = Mul(normal.y, dot), tz = Mul(normal.z, dot);
        const float rx = v.x - tx, ry = v.y - ty, rz = v.z - tz;
        velocity->x = v.x - Add(Mul(rx, a), Mul(b, tx));
        velocity->y = v.y - Add(Mul(ry, a), Mul(ty, b));
        velocity->z = v.z - Add(Mul(a, rz), Mul(tz, b));
    }
    return true;
}

// 0xbb6730.
bool cTSParticlesEffect::CollideColliders(cTSParticle *particle, Vector3 vector)
{
    const cTSParticlesDescription *const d = mDescription;
    const float ratio = particle->mAge / particle->mLife;
    float f = fx::EvaluateCurve(d->mSize, ratio) * Mul(mSizeScale, particle->mSize);
    const float alpha = fx::EvaluateCurve(d->mAlpha, ratio);
    if (alpha > 1.0f) f = Mul(alpha, f);
    if (d->mAlphaVary != 0.0f) f = Mul(f, Add(std::fabs(d->mAlphaVary), 1.0f));
    f = Mul(f, mWorld.scale);
    return fx::SweepColliders(mOwner.Get(), &mColliders, &mColliderState, f, &vector, &particle->mPosition, &particle->mVelocity,
                              mVisible == 0, nullptr, nullptr);
}

// 0xbb9840.
Vector3 *cTSParticleAttractorState::Force(Vector3 *out, const Vector3 *position) const
{
    const cTSParticleAttractor *const d = mDescription;
    if (d) {
        const float px = position->x, py = position->y, pz = position->z;
        switch (d->mHasCentre) {
        case 2: {
            const float x = px - mCentre.x, y = py - mCentre.y, z = pz - mCentre.z;
            out->x = Mul(Mul(x, d->m10), d->m10) - x;
            out->y = Mul(Mul(d->m14, y), d->m14) - y;
            out->z = Mul(Mul(d->m18, z), d->m18) - z;
            return out;
        }
        case 3: {
            const float x = mCentre.x - px, y = mCentre.y - py, z = mCentre.z - pz;
            out->x = Mul(Mul(x, d->m10), d->m10);
            out->y = Mul(Mul(y, d->m14), d->m14);
            out->z = Mul(Mul(z, d->m18), d->m18);
            return out;
        }
        case 4: {
            float x = px - mCentre.x, y = py - mCentre.y, z = pz - mCentre.z;
            const float length = fx::SqrtF(Add(Add(Mul(y, y), Mul(x, x)), Mul(z, z)));
            if (length == 0.0f) {
                // A point at the centre: a random direction (a unit ball by rejection, the sphere's by the divide).
                float sum;
                do {
                    x = fx::RandomIn(-1.0f, 1.0f);
                    y = fx::RandomIn(-1.0f, 1.0f);
                    z = fx::RandomIn(-1.0f, 1.0f);
                    sum = Add(Add(Mul(y, y), Mul(x, x)), Mul(z, z));
                } while (sum > 1.0f || 0.0001f > sum);
                const float unit = fx::SqrtF(sum);
                out->x = x / unit;
                out->y = y / unit;
                out->z = z / unit;
                return out;
            }
            const float pull = (d->m10 - length) / length;
            out->x = Mul(x, pull);
            out->y = Mul(y, pull);
            out->z = Mul(z, pull);
            return out;
        }
        default: break;
        }
    }
    const float cx = mCentre.x, cy = mCentre.y, cz = mCentre.z;
    out->x = cx - position->x;
    out->y = cy - position->y;
    out->z = cz - position->z;
    return out;
}

// 0xbb9b90.
Vector3 *__fastcall cTSParticleAttractorState::Sum(std::vector<cTSParticleAttractorState> *states, void *, Vector3 *out,
                                                  const Vector3 *position, char *stop)
{
    *out = {0.0f, 0.0f, 0.0f};
    if (stop) *stop = 0;
    for (size_t i = 0; i != states->size(); ++i) {
        if (stop && *stop) break;
        const cTSParticleAttractorState &state = (*states)[i];
        const cTSParticleAttractor *const d = state.mDescription;
        Vector3 f;
        state.Force(&f, position);
        const float lengthSquared = Add(Add(Mul(f.x, f.x), Mul(f.y, f.y)), Mul(f.z, f.z));
        if (lengthSquared > 0.0f) {
            const float strength = state.mStrength;
            const float length = fx::SqrtF(lengthSquared);
            if (stop && d && d->m30 > length) {
                *stop = 1; // inside the minimum distance: the force goes in as it is
            } else {
                float scaled = strength;
                if (d) {
                    const float ratio = length / d->mStrength;
                    scaled = fx::EvaluateCurve(d->mCurve, 1.0f < ratio ? 1.0f : ratio) * strength;
                }
                const float scale = scaled / length;
                f = {Mul(f.x, scale), Mul(f.y, scale), Mul(f.z, scale)};
            }
        } else if (stop) {
            *stop = d && d->m30 > 0.0f;
        }
        out->x = Add(f.x, out->x);
        out->y = Add(f.y, out->y);
        out->z = Add(f.z, out->z);
    }
    return out;
}

namespace {

// The description's flag bits this step reads (ParticlesDescription, src/gz/ParticleCommands.h).
constexpr uint32_t kCollideFlags = 0x18;         // bit 3 (collide) or bit 4
constexpr uint32_t kTerrainCollideFlag = 0x8000; // bit 15
constexpr uint32_t kStickyFlag = 0x10000;        // bit 16
constexpr uint32_t kTerrainRepelFlag = 0x20000;  // bit 17
constexpr uint32_t kKillOutsideFlag = 0x40000;   // bit 18

// The surface object (a height field): slot 5 the height at (x, y), returned on the x87 stack; slot 6 the normal there,
// written to the first argument, which it returns.
using SurfaceHeight = float(__thiscall *)(void *, float, float);
using SurfaceNormal = Vector3 *(__thiscall *)(void *, Vector3 *, float, float);

} // namespace

// 0xbb85b0.
bool cTSParticlesEffect::CollideSurface(cTSParticle *particle, const Vector3 *where, float step)
{
    void *const surface = mSurface.Get();
    if (!surface) return true;
    const cTSParticlesDescription *const d = mDescription;
    const uint32_t flags = static_cast<uint32_t>(d->mFlags);
    Vector3 *const velocity = &particle->mVelocity;
    if (flags & kTerrainRepelFlag) {
        // A field pushing the particle up and off the surface, stronger the nearer it is: the point a tenth of the
        // velocity ahead (at least a tenth of a unit of it: the divisor is 10 or the speed) is looked up.
        const float k = d->mTerrainRepel[2];
        const Vector3 v = *velocity;
        const float sx = Mul(v.x, k), sy = Mul(v.y, k), sz = Mul(v.z, k);
        const float length = fx::SqrtF(Add(Add(Mul(v.x, v.x), Mul(v.y, v.y)), Mul(v.z, v.z)));
        const float divisor = length > 10.0f ? length : 10.0f;
        Vector3 p = {Add(sx / divisor, particle->mPosition.x), Add(sy / divisor, particle->mPosition.y),
                     Add(sz / divisor, particle->mPosition.z)};
        mWorld.Apply(&p);
        if (!(p.x > 0.0f) || !(p.y > 0.0f) || !(mSurfaceRange[0] > p.x) || !(mSurfaceRange[1] > p.y)) return true;
        const float over = p.z - VirtualCall<SurfaceHeight, 5>(surface, p.x, p.y);
        if (d->mTerrainRepel[4] > over) return false;   // under the surface by more than the kill depth
        if (!(d->mTerrainRepel[0] > over)) return true; // beyond the field's reach
        Vector3 normal;
        VirtualCall<SurfaceNormal, 6>(surface, &normal, p.x, p.y);
        const float time = Mul(step, d->mTerrainRepel[1]);
        const float t = d->mTerrainRepel[0] - over;
        const float f = Mul(t, t);
        velocity->x = Add(Mul(Mul(normal.x, f), time), velocity->x);
        velocity->y = Add(Mul(Mul(normal.y, f), time), velocity->y);
        velocity->z = Add(Mul(Mul(Mul(normal.z, f), d->mTerrainRepel[3]), time), velocity->z);
        return true;
    }
    if (d->mBounce == 0.0f) {
        // No bounce: a particle under the surface is put on it and loses the speed into it.
        const float height = VirtualCall<SurfaceHeight, 5>(surface, where->x, where->y);
        if (!(flags & kStickyFlag) && !(height > where->z)) return true;
        particle->mPosition = {where->x, where->y, height};
        mWorld.ToLocal(&particle->mPosition);
        Vector3 buffer;
        const Vector3 n = *VirtualCall<SurfaceNormal, 6>(surface, &buffer, where->x, where->y);
        const float into = -static_cast<float>(nGZCompGeom::Dot87(&velocity->x, &n.x));
        if (!(into > 0.0f)) return true;
        velocity->x = Add(Mul(n.x, into), velocity->x);
        velocity->y = Add(Mul(n.y, into), velocity->y);
        velocity->z = Add(Mul(n.z, into), velocity->z);
        return true;
    }
    // A bounce: only a particle under the surface, and then the surface's height is asked again (twice in all).
    if (!(VirtualCall<SurfaceHeight, 5>(surface, where->x, where->y) > where->z)) return true;
    const float height = VirtualCall<SurfaceHeight, 5>(surface, where->x, where->y);
    const float death = d->mDeath;
    if (death != 0.0f) {
        if (death == 1.0f) return false;
        if (death >= static_cast<float>(fx::GameRandom().RandomDoubleUniform())) return false;
    }
    particle->mPosition = {where->x, where->y, height};
    mWorld.ToLocal(&particle->mPosition);
    Vector3 buffer;
    Vector3 n = *VirtualCall<SurfaceNormal, 6>(surface, &buffer, where->x, where->y);
    if (mWorld.flags & fx::EffectsTransform::kRotated) { // the normal back into the effect's space
        const float(&m)[3][3] = mWorld.m;
        n = {fx::RowFromY(n.x, n.y, n.z, m[0][0], m[1][0], m[2][0]), fx::RowFromY(n.x, n.y, n.z, m[0][1], m[1][1], m[2][1]),
             fx::RowFromY(n.x, n.y, n.z, m[0][2], m[1][2], m[2][2])};
    }
    const double dot = nGZCompGeom::Dot87(&n.x, &velocity->x);
    const float k = static_cast<float>(dot * static_cast<double>(Add(d->mBounce, 1.0f))); // an x87 product kept to 24 bits
    velocity->x = velocity->x - Mul(n.x, k);
    velocity->y = velocity->y - Mul(n.y, k);
    velocity->z = velocity->z - Mul(n.z, k);
    return true;
}

// 0xbb82f0.
bool cTSParticlesEffect::ApplyForces(cTSParticle *particle, const Vector3 *vector, float step)
{
    Vector3 *const velocity = &particle->mVelocity;
    if (!mAttractors.empty()) {
        char stop;
        Vector3 pull;
        const Vector3 *const f = cTSParticleAttractorState::Sum(&mAttractors, nullptr, &pull, &particle->mPosition, &stop);
        velocity->x = Add(Mul(f->x, step), velocity->x);
        velocity->y = Add(Mul(f->y, step), velocity->y);
        velocity->z = Add(Mul(f->z, step), velocity->z);
        if (stop) { // inside an attractor's minimum distance
            particle->mAlive = 0;
            return false;
        }
    }
    const cTSParticlesDescription *const d = mDescription;
    const float keep = 1.0f - Mul(step, d->mDrag);
    velocity->x = Mul(keep, velocity->x);
    velocity->y = Mul(keep, velocity->y);
    velocity->z = Mul(keep, velocity->z);
    const uint32_t flags = static_cast<uint32_t>(d->mFlags);
    if ((flags & kCollideFlags) && !CollideColliders(particle, *vector)) {
        particle->mAlive = 0;
        return false;
    }
    // Where the particle is now (the sweep may have moved it), in the surface's space.
    Vector3 where = particle->mPosition;
    mWorld.Rotate(&where);
    const float scale = mWorld.scale;
    where = {Add(Mul(where.x, scale), mWorld.translation.x), Add(Mul(where.y, scale), mWorld.translation.y),
             Add(Mul(where.z, scale), mWorld.translation.z)};
    if (where.x > 0.0f && where.y > 0.0f && mSurfaceRange[0] > where.x && mSurfaceRange[1] > where.y) {
        if ((flags & (kTerrainCollideFlag | kTerrainRepelFlag)) && !CollideSurface(particle, &where, step)) {
            particle->mAlive = 0;
            return false;
        }
        return true;
    }
    if (flags & kKillOutsideFlag) { // off the surface's edge
        particle->mAlive = 0;
        return false;
    }
    return true;
}
