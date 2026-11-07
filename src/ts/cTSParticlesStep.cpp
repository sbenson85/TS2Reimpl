// cTSParticlesEffect's step, vtable slot 5 0xbb6830 (lane WX, rendering row E6): the preroll and the world's counters, the
// renderable under its parent, the effect's scales from the event values, the gravity, the colliders placed once, then every
// particle integrated (by the description's flags) with its drawn position grown into the box, the emitter, the box scaled for
// the sprites and moved to the world, the wind, the renderable's refresh. The layout and the rest of the class:
// src/ts/cTSParticlesEffect.h. Operand orders follow the assembler (the loops differ in them, NaN payloads only).
#include "ts/cTSParticlesEffect.h"

#include "game/game.h"
#include "game/vtable.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/SGTreeCode.h"
#include "ts/ParticleCurve.h"
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSParticlesRenderable.h"

#include <bit>
#include <cfloat>
#include <cstdint>

using fx::Add;
using fx::Div;
using fx::Mul;
using fx::Sub;
using fx::Vector3;

namespace {

constexpr uint32_t kFlagBursts = 0x1, kFlagOnce = 0x2;
constexpr uint32_t kFlagScaleSize = 0x4000;       // the spawn size goes by the transform's scale
constexpr uint32_t kFlagWind = 0x100000;
constexpr uint32_t kFlagLateCounters = 0x400000;  // the counters of the second kind
constexpr uint32_t kFlagSizeByLife = 0x10000000;  // the life scale goes to the size, not the spawn size
constexpr uint32_t kSubstepFlags = 0x04028018;    // IntegrateParticle's: these always take the substeps
constexpr float kSimpleLimit = 0.05f;             // 0xa0daf068
constexpr float kTiny = 1e-06f;                   // 0xa0db13d0
constexpr float kRateLimit = 20.0f;               // 0xa0dad394
constexpr float kHalf = 0.5f;                     // 0xa0d9c694

float Abs(float v) { return std::bit_cast<float>(std::bit_cast<uint32_t>(v) & 0x7fffffffu); }

// The effect's value between a range's two ends, scaled: (range[1] - range[0]) * t + range[0], times the scale.
float Ranged(const float *range, float t, float scale)
{
    return Mul(Add(Mul(Sub(range[1], range[0]), t), range[0]), scale);
}

// The ways a stretched particle's two ends are made (operand orders only).
enum class Style { kA, kB, kC };

} // namespace

// 0xbb6830.
void cTSParticlesEffect::AddToScene(void *stepArg, void *statsArg)
{
    const float step = std::bit_cast<float>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stepArg)));
    int32_t *const stats = static_cast<int32_t *>(statsArg);
    cTSParticlesDescription *const d = mDescription;
    const uint32_t low = static_cast<uint32_t>(d->mFlags);
    const uint32_t high = static_cast<uint32_t>(d->mFlags >> 32);

    float local;
    if (!mPrerollPending) {
        local = step;
        if (mWord142 < mFramesDrawn) {
            mWord142 = mFramesDrawn;
            if (!(low & kFlagLateCounters))
                stats[3] += mCount;
            else
                stats[5] += mCount;
        }
    } else {
        local = Add(step, d->mPreroll);
        mPrerollPending = false;
    }

    if (mVisible && mRenderable->GetParent() == nullptr) {
        if (!mParent.Get()) {
            mParent.Assign(VirtualCall<nGZSceneGraph::cICompositionTreeNode *(__thiscall *)(void *), 42>(mOwner.Get()));
            mScene.Assign(VirtualCall<cIGZUnknown *(__thiscall *)(void *), 43>(mOwner.Get()));
        }
        if (mParent.Get()) mParent->AddChild(mRenderable.Get());
    }

    if (!(0.0f > local)) {
        const Vector3 position = mLocal.translation;
        {
            const float dy = Sub(position.y, mPreviousPosition.y);
            const float dx = Sub(position.x, mPreviousPosition.x);
            const float dz = Sub(position.z, mPreviousPosition.z);
            const float sum = Add(Add(Mul(dy, dy), Mul(dx, dx)), Mul(dz, dz));
            if (sum > 1.0f) mPreviousPosition = position;
        }
        const float t = mSettings->mDetail;
        const float v1 = Ranged(&mRange[0], t, mRateScale);
        mScale17c = v1;
        const float v2 = Ranged(&mRange[2], t, mLifeScale);
        mColorScale = mEvent4;
        mAlphaScale = Ranged(&mRange[4], t, mEvent3);
        float spawn;
        if (!(low & kFlagSizeByLife)) {
            spawn = v2;
            mSizeScale = 1.0f;
        } else {
            spawn = 1.0f;
            mSizeScale = v2;
        }
        mSpawnSize = spawn;
        if (low & kFlagScaleSize) {
            spawn = Mul(spawn, mLocal.scale);
            mSpawnSize = spawn;
        }
        if (low & kFlagBursts) {
            const float rate = fx::EvaluateCurve(d->mRate, 0.0f);
            if (rate > kRateLimit) {
                mSpawnSize = Mul(spawn, mSettings->mSizeFactor);
                mScale17c = Mul(v1, mSettings->mRateFactor);
            } else {
                mScale17c = 1.0f;
            }
        } else if (!(low & kFlagOnce)) {
            mSpawnSize = Mul(spawn, mSettings->mSizeFactor);
            mScale17c = Mul(v1, mSettings->mRateFactor);
        }

        for (cTSParticleAttractorState &attractor : mAttractors) attractor.Follow(&mLocal);
        mTime = static_cast<double>(local) + mTime; // (a double add: fx::Add is the float one)

        {
            const float k = d->mGlobalWind;
            const Vector3 &s3 = *reinterpret_cast<const Vector3 *>(mSettings->mBefore);
            const auto &force = d->mForce;
            mGravity.x = Add(Mul(k, s3.x), Add(force.x, mWind.x));
            mGravity.y = Add(Add(force.y, mWind.y), Mul(k, s3.y));
            mGravity.z = Add(Add(force.z, mWind.z), Mul(k, s3.z));
        }

        if (!mColliderState.empty() && !mCollidersPlaced) {
            for (const cTSParticleColliderState &c : mColliders)
                VirtualCall<void(__thiscall *)(void *, const void *, void *, void *, void *), 4>(
                    c.mObject, c.mDescription, &mLocal, &mWorld, mColliderState.data() + c.mStateOffset);
            mCollidersPlaced = true;
        }

        cTSParticleBox &box = Box();
        mBound[0] = mBound[1] = mBound[2] = FLT_MAX;
        mBound[3] = mBound[4] = mBound[5] = -FLT_MAX;

        Style style;
        if (high & 4)
            style = Style::kA;
        else if (high & 2)
            style = Style::kB;
        else if ((low & kSubstepFlags) || kSimpleLimit < Mul(local, d->mDrag))
            style = Style::kC;
        else
            style = Style::kA;
        const cTSEffectCurve &screw = d->mScrewRadius;
        for (auto it = mParticles.begin(); it != mParticles.end();) {
            cTSParticle *const p = *it;
            bool alive = p->mAlive != 0;
            if (alive) {
                if (high & 4)
                    alive = (high & 8) ? IntegrateOnPathPlaced(p, local) : IntegrateOnPath(p, local);
                else if (high & 2)
                    alive = (high & 8) ? IntegrateSteeredSubsteps(p, local) : IntegrateSteered(p, local);
                else if (style == Style::kC)
                    alive = IntegrateSubsteps(p, local);
                else
                    alive = IntegrateSimple(p, local);
            }
            if (!alive) {
                it = mParticles.erase(it);
                --mCount;
                RetireParticle(p);
                if (mRenderable.Get() && mRenderable->mShape.mObject) mRenderable->RemoveParticleChild();
                continue;
            }
            ++it;
            Vector3 where;
            if (screw.mKind == 0 && screw.mValues.size() == 1 && screw.mValues[0] == 0.0f)
                where = p->mPosition;
            else
                fx::ParticlePosition(&where, p, d, Div(p->mAge, p->mLife));
            const float ax = Abs(p->mVelocity.x), ay = Abs(p->mVelocity.y), az = Abs(p->mVelocity.z);
            float longest = az > ay ? az : ay;
            longest = longest > ax ? longest : ax;
            if (kTiny > longest || d->mStretch == 0.0f) {
                box.Extend(&where);
                continue;
            }
            const float f = Div(Mul(Mul(mMaxSize, kHalf), mSizeScale), d->mStretch);
            const Vector3 &v = p->mVelocity;
            Vector3 off, front;
            if (style == Style::kC)
                off = {Mul(f, v.x), Mul(f, v.y), Mul(f, v.z)};
            else
                off = {Mul(v.x, f), Mul(v.y, f), Mul(v.z, f)};
            if (high & 4) {
                front = {Add(where.x, off.x), Add(where.y, off.y), Add(where.z, off.z)};
            } else if (high & 2) {
                front = {Add(off.x, where.x), Add(off.y, where.y), Add(off.z, where.z)};
            } else if (style == Style::kC) {
                front = {Add(where.x, off.x), Add(off.y, where.y), Add(off.z, where.z)};
            } else {
                front = {Add(where.x, off.x), Add(where.y, off.y), Add(where.z, off.z)};
            }
            box.Extend(&front);
            const Vector3 back = {Sub(where.x, off.x), Sub(where.y, off.y), Sub(where.z, off.z)};
            box.Extend(&back);
        }

        if (!mFollowerSet) Emit(local);

        if (!(mBound[0] > mBound[3])) {
            const float f15 = Mul(Mul(Mul(mSizeScale, mMaxSize), mSpawnSize), kHalf);
            if (mMaxAlpha == 1.0f) {
                mBound[0] = Sub(mBound[0], f15);
                mBound[1] = Sub(mBound[1], f15);
                mBound[2] = Sub(mBound[2], f15);
                mBound[3] = Add(f15, mBound[3]);
                mBound[4] = Add(f15, mBound[4]);
                mBound[5] = Add(f15, mBound[5]);
            } else {
                const float f14 = Mul(f15, mMaxAlpha);
                mBound[0] = Sub(mBound[0], f14);
                mBound[1] = Sub(mBound[1], f14);
                mBound[2] = Sub(mBound[2], f15);
                mBound[3] = Add(f14, mBound[3]);
                mBound[4] = Add(f14, mBound[4]);
                mBound[5] = Add(f15, mBound[5]);
            }
            box.Transform(&mWorld);
        }

        if (mCount == 0 && mStopping) {
            mRunning = false;
            return;
        }
        if (low & kFlagWind) StepWind(local);
        if (mVisible && mRenderable.Get() && mRenderable->mShape.mObject)
            mRenderable->UpdateParticleChildren(d);
        nGZSceneGraph::NodeTransformChanged(mRenderable.Get());
        mPreviousPosition = position;
    }

    if (!(low & kFlagLateCounters))
        stats[2] += mCount;
    else
        stats[4] += mCount;
}
