// cTSParticlesEffect (lane WR, rendering row E4). Layout, shape and what is still the game's: src/ts/cTSParticlesEffect.h.
#include "ts/cTSParticlesEffect.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZRandom.h"
#include "sg/SGNodeInterfaces.h"
#include "ts/ParticleCurve.h"
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSParticlesRenderable.h"

#include <bit>
#include <cfloat>
#include <cmath>
#include <new>

using fx::Add;
using fx::Div;
using fx::EffectsTransform;
using fx::Mul;
using fx::RowFromX;
using fx::RowFromY;
using fx::Sub;
using fx::Vector3;

namespace {

// The game's random generator, the one cRZRandom's static initialiser (slot 703) makes.
cRZRandom &Random() { return *GameAddress<cRZRandom>(0x10fd138); }
float RandomIn(float low, float high)
{
    return static_cast<float>(Random().RandomDoubleRangeUniform(static_cast<double>(low), static_cast<double>(high)));
}

constexpr uint32_t kFlagRespawnFirst = 0x4; // Start's restart check: a limited-rate effect only restarts with this flag
constexpr uint32_t kFlagAtBoxCentre = 0x200;
constexpr uint32_t kFlagWind = 0x100000;
constexpr uint32_t kFlagFollowerInits = 0x20000000;
constexpr uint32_t kFlagAlwaysHandOn = 0x40000000;

uint32_t LowFlags(const cTSParticlesDescription *d) { return static_cast<uint32_t>(d->mFlags); }

// The owner: the effects world's slots this class uses.
const cTSParticlesSettings *SettingsOf(cIGZUnknown *owner)
{
    return VirtualCall<const cTSParticlesSettings *(__thiscall *)(void *), 21>(owner);
}
cIGZUnknown *SurfaceOf(cIGZUnknown *owner)
{
    void *const source = VirtualCall<void *(__thiscall *)(void *), 24>(owner);
    return VirtualCall<cIGZUnknown *(__thiscall *)(void *, uint32_t), 7>(source, 0);
}
cIGZUnknown *FindCollider(cIGZUnknown *owner, const char *name)
{
    return VirtualCall<cIGZUnknown *(__thiscall *)(void *, const char *), 28>(owner, name);
}
cIGZUnknown *FindConstraint(cIGZUnknown *owner, const char *name)
{
    return VirtualCall<cIGZUnknown *(__thiscall *)(void *, const char *), 30>(owner, name);
}
bool MakeEffect(cIGZUnknown *owner, const char *name, cIGZUnknown **effect)
{
    return VirtualCall<bool(__thiscall *)(void *, const char *, cIGZUnknown **), 14>(owner, name, effect);
}

} // namespace

// ---- the attractors ----

// 0xbde5a0.
void cTSParticleAttractorState::Set(const cTSParticleAttractor *description, const EffectsTransform *transform)
{
    mDescription = description;
    if (!description->mHasCentre) return;
    mCentre = {description->mCentre.x, description->mCentre.y, description->mCentre.z};
    if (transform) transform->Apply(&mCentre);
    mStrength = 1.0f;
}

// 0xbde5e0.
void cTSParticleAttractorState::SetCentre(const Vector3 *centre) { mCentre = *centre; }

// 0xbde650.
void cTSParticleAttractorState::Follow(const EffectsTransform *transform)
{
    if (mDescription->mHasCentre != 1) return;
    mCentre = {mDescription->mCentre.x, mDescription->mCentre.y, mDescription->mCentre.z};
    transform->Apply(&mCentre);
}

// 0xbddf10.
void __fastcall cTSParticleAttractorState::Assign(std::vector<cTSParticleAttractorState> *states, void *,
                                                  const std::vector<cTSParticleAttractor> *attractors)
{
    const size_t count = attractors->size();
    states->resize(count);
    for (size_t i = 0; i < count; ++i) (*states)[i].Set(&(*attractors)[i], nullptr);
}

// ---- the box ----

// 0xbb9350.
Vector3 cTSParticleBox::Center() const
{
    return {Mul(Add(mMin.x, mMax.x), 0.5f), Mul(Add(mMin.y, mMax.y), 0.5f), Mul(Add(mMin.z, mMax.z), 0.5f)};
}

// 0xbb5bd0.
void cTSParticleBox::Extend(const Vector3 *point)
{
    if (mMin.x > mMax.x) {
        mMin = *point;
        mMax = *point;
        return;
    }
    const Vector3 p = *point;
    if (mMin.x > p.x) mMin.x = p.x;
    else if (p.x > mMax.x) mMax.x = p.x;
    if (mMin.y > p.y) mMin.y = p.y;
    else if (p.y > mMax.y) mMax.y = p.y;
    if (mMin.z > p.z) mMin.z = p.z;
    else if (p.z > mMax.z) mMax.z = p.z;
}

// 0xbc0cb0. Rotated: the centre and the half size (each times scale / 2) go through the rotation and the matrix of absolute values
// (0x1a6250: nine sign-bit clears), the result is centre -/+ extent. The operand orders are the original's (NaN payloads).
void cTSParticleBox::Transform(const EffectsTransform *t)
{
    if (!(t->flags & EffectsTransform::kRotated)) {
        const float scale = t->scale;
        mMin = {Add(Mul(scale, mMin.x), t->translation.x), Add(Mul(scale, mMin.y), t->translation.y),
                Add(Mul(scale, mMin.z), t->translation.z)};
        mMax = {Add(Mul(scale, mMax.x), t->translation.x), Add(Mul(scale, mMax.y), t->translation.y),
                Add(Mul(scale, mMax.z), t->translation.z)};
        return;
    }
    const cTSParticleBox box = *this;
    const float half = Mul(t->scale, 0.5f);
    float a[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) a[i][j] = std::bit_cast<float>(std::bit_cast<uint32_t>(t->m[i][j]) & 0x7fffffffu);
    const float hx = Mul(Sub(box.mMax.x, box.mMin.x), half);
    const float hy = Mul(Sub(box.mMax.y, box.mMin.y), half);
    const float hz = Mul(Sub(box.mMax.z, box.mMin.z), half);
    const float ex = RowFromX(hx, hy, hz, a[0][0], a[0][1], a[0][2]);
    const float ey = RowFromX(hx, hy, hz, a[1][0], a[1][1], a[1][2]);
    const float ez = RowFromX(hx, hy, hz, a[2][0], a[2][1], a[2][2]);
    const float sx = Mul(Add(box.mMax.x, box.mMin.x), half);
    const float sy = Mul(Add(box.mMax.y, box.mMin.y), half);
    const float sz = Mul(Add(box.mMax.z, box.mMin.z), half);
    const float cx = Add(RowFromY(sx, sy, sz, t->m[0][0], t->m[0][1], t->m[0][2]), t->translation.x);
    const float cy = Add(RowFromY(sx, sy, sz, t->m[1][0], t->m[1][1], t->m[1][2]), t->translation.y);
    const float cz = Add(RowFromY(sx, sy, sz, t->m[2][0], t->m[2][1], t->m[2][2]), t->translation.z);
    mMin = {Sub(cx, ex), Sub(cy, ey), Sub(cz, ez)};
    mMax = {Add(cx, ex), Add(cy, ey), Add(ez, cz)};
}

// ---- life and death ----

// 0xbb4a80. The words the constructor leaves unset stay out of the list (+0x34, +0x41, +0xc5, +0xfd, +0x1f1, +0x20c).
cTSParticlesEffect::cTSParticlesEffect(cTSParticlesDescription *description)
    : mAttached(false), mRunning(false), mStopping(false), mVisible(true), mStopMode(1), mCount(0), mDescription(description),
      mSettings(nullptr), mTime(0.0), mPrerollPending(false), mEmitClock(0.0), mEmitted(0), mEmitPeriod(-1.0),
      mEmitRemainder(0.0f), mGravity{0.0f, 0.0f, 0.0f}, mWindClock(0.0f), mWindAngle{0.0f, 0.0f}, mWind{0.0f, 0.0f, 0.0f},
      mDelay(0.0f), mBound{}, mMaxSize(0.0f), mMaxAlpha(1.0f), mPreviousPosition{0.0f, 0.0f, 0.0f}, mFramesDrawn(0), mWord142(0),
      mSpeedScale(1.0f), mRange{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, mRateScale(1.0f), mLifeScale(1.0f), mEvent3(0.0f),
      mEvent4{1.0f, 1.0f, 1.0f}, mFirstFrame(0), mScale17c(1.0f), mSpawnSize(1.0f), mSizeScale(1.0f), mAlphaScale(1.0f),
      mColorScale{1.0f, 1.0f, 1.0f}, mSurfaceRange{0.0f, 0.0f}, mWord1a4(0.0f), mSource{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
      mCollidersPlaced(false), mFollowerSet(false)
{
    mLocal.Reset();
    mWorld.Reset();
}

// 0xbb5290: the members, last first.
cTSParticlesEffect::~cTSParticlesEffect() = default;

// 0xbc0c50.
cITSComponentEffect *__cdecl cTSParticlesEffect::Create(cTSParticlesDescription *description)
{
    return new (std::nothrow) cTSParticlesEffect(description);
}

// ---- the slots ----

// 0xbba860.
void cTSParticlesEffect::Attach(cIGZUnknown *owner)
{
    if (mAttached) return;
    mOwner.Assign(owner);
    mSettings = SettingsOf(owner);
    mSpeedScale = mRateScale = mLifeScale = mEvent3 = 1.0f;
    mEvent4 = {1.0f, 1.0f, 1.0f};
    mLocal.Reset();
    mWorld.Reset();
    mTextureOverride.mString.mString.clear();
    mFollowerSet = false;
    Box().mMin = {FLT_MAX, FLT_MAX, FLT_MAX};
    Box().mMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    mMaxSize = Mul(Add(std::fabs(mDescription->mSizeVary), 1.0f), mDescription->mSize.GetMax());
    const float maxAlpha = mDescription->mAlpha.GetMax();
    mEmitterPosition = {0.0f, 0.0f, 0.0f};
    mMaxAlpha = Mul(Add(std::fabs(mDescription->mAlphaVary), 1.0f), maxAlpha);
    mFollower.Reset();
    mSurface.Assign(SurfaceOf(owner));
    if (mSurface.Get()) {
        float low, high;
        VirtualCall<void(__thiscall *)(void *, float *, float *, float *, float *), 3>(mSurface.Get(), &low, &high, &mSurfaceRange[0],
                                                                                      &mSurfaceRange[1]);
        mSurfaceRange[0] = Sub(mSurfaceRange[0], low);
        mSurfaceRange[1] = Sub(mSurfaceRange[1], high);
    }
    Prepare();
    mRenderable.Assign(new (std::nothrow) nGZSceneGraph::cTSParticlesRenderable(this));
    mRenderable->Setup(mDescription);
    mAttached = true;
}

void cTSParticlesEffect::FromTheParent()
{
    if (mRenderable->GetParent()) mRenderable->GetParent()->RemoveChild(mRenderable.Get());
}

// 0xbc07e0.
void cTSParticlesEffect::Detach()
{
    if (!mAttached) return;
    mAttached = false;
    if (mRunning) Stop(1);
    if (mRenderable.Get()) FromTheParent();
    mRenderable.Reset();
    mSurface.Reset();
    mDescription = nullptr;
    mParent.Reset();
    mScene.Reset();
    mOwner.Reset();
}

// 0xbc0a10.
void cTSParticlesEffect::Start(uint32_t a)
{
    const cTSParticlesDescription *const d = mDescription;
    if (a != 0 && d->mRateMode != 0 && !(LowFlags(d) & kFlagRespawnFirst)) return;
    if (!mRunning || mStopping) {
        mRunning = true;
        mStopping = false;
    }
    mEmitClock = 0.0;
    mTime = 0.0;
    mPreviousPosition = mLocal.translation;
    mEmitted = 0;
    mEmitRemainder = 0.0f;
    mGravity = {d->mForce.x, d->mForce.y, d->mForce.z};
    mWind = {0.0f, 0.0f, 0.0f};
    if (LowFlags(d) & kFlagWind) {
        mWindClock = 0.0f;
        mWindAngle[0] = static_cast<float>(Random().RandomDoubleRangeUniform(-0.5, 0.5));
        mWindAngle[1] = static_cast<float>(Random().RandomDoubleRangeUniform(-0.5, 0.5));
        StepWind(0.0f);
    }
    float delay = 0.0f;
    if (d->m28.x >= 0.0f) {
        delay = RandomIn(d->m28.x, d->m28.y);
    } else if (d->m30.x >= 0.0f) {
        delay = RandomIn(d->m30.x, d->m30.y);
    }
    mDelay = delay;
    mStopMode = 0;
    mPrerollPending = a != 0;
}

// 0xbc0b90.
void cTSParticlesEffect::Stop(uint32_t a)
{
    if (!mRunning) return;
    if (mFollower.Get() && mCount == 0) mFollower->Stop(a);
    mStopMode = a;
    mStopping = a == 0;
    if (a == 0) return;
    mRunning = false;
    ClearParticles();
    if (mRenderable.Get()) FromTheParent();
}

// 0xbbd8f0.
void cTSParticlesEffect::Update(const EffectsTransform *local, const EffectsTransform *parent)
{
    mLocal = *local;
    mWorld = *parent;
    mCollidersPlaced = false;
    if (!mFollowerSet && (LowFlags(mDescription) & kFlagAtBoxCentre)) {
        mLocal.flags &= ~EffectsTransform::kTranslated;
        ++mLocal.Version();
        mWorld.flags &= ~EffectsTransform::kTranslated;
        ++mWorld.Version();
        mLocal.translation = {0.0f, 0.0f, 0.0f};
        mWorld.translation = {0.0f, 0.0f, 0.0f};
        Box() = mSource;
        mEmitterPosition = Box().Center();
    }
    const Vector3 previous = mEmitterPosition;
    mEmitterPosition = mLocal.translation;
    mWorld.Apply(&mEmitterPosition);
    cTSParticleBox &box = Box();
    if (!(box.mMin.x > box.mMax.x)) {
        const Vector3 moved = {Sub(mEmitterPosition.x, previous.x), Sub(mEmitterPosition.y, previous.y),
                               Sub(mEmitterPosition.z, previous.z)};
        box.mMin = {Add(moved.x, box.mMin.x), Add(moved.y, box.mMin.y), Add(moved.z, box.mMin.z)};
        box.mMax = {Add(moved.x, box.mMax.x), Add(moved.y, box.mMax.y), Add(moved.z, box.mMax.z)};
    }
}

// 0xbbd0e0.
bool cTSParticlesEffect::HandleEvent(uint32_t kind, void *a, void *)
{
    const float *const values = static_cast<const float *>(a);
    switch (kind) {
    case 0: mSpeedScale = *values; return true;
    case 1: mLifeScale = *values; return true;
    case 2: mRateScale = *values; return true;
    case 3: mEvent3 = *values; return true;
    case 4: mEvent4 = *static_cast<const Vector3 *>(a); return true;
    case 5: Aim(static_cast<const Vector3 *>(a)); return true;
    case 7:
        if (!mAttractors.empty()) mAttractors[0].SetCentre(static_cast<const Vector3 *>(a));
        return true;
    case 8:
        if (!mAttractors.empty()) mAttractors[0].mStrength = *values;
        return true;
    case 9:
        if (!mAttractors.empty()) { // the line (a point, a direction) where it crosses the height of the box's centre
            const Vector3 centre = Box().Center();
            const float along = Sub(centre.y, values[1]);
            const Vector3 point = {Add(Div(Mul(values[3], along), values[4]), values[0]),
                                   Add(Div(Mul(values[4], along), values[4]), values[1]),
                                   Add(Div(Mul(values[5], along), values[4]), values[2])};
            mAttractors[0].SetCentre(&point);
            mAttractors[0].mStrength = 1.0f;
        }
        return true;
    case 0xd: // a float's rounding to an integer: the sum's low mantissa bits
        mFirstFrame = std::bit_cast<uint32_t>(Add(*values, 12582912.0f)) + 0xb4c00000u;
        return true;
    case 0x101:
        mRange[0] = values[0];
        mRange[2] = values[1];
        mRange[4] = values[2];
        mRange[1] = values[3];
        mRange[3] = values[4];
        mRange[5] = values[5];
        return true;
    default: return false;
    }
}

// 0xbbdc90.
bool cTSParticlesEffect::Query(void *a, void *b)
{
    if (a != nullptr) return false;
    cIGZString *name = nullptr;
    static_cast<cIGZUnknown *>(b)->QueryInterface(cRZString::kGZIID_cIGZString, reinterpret_cast<void **>(&name));
    if (name) {
        cIGZString &text = *mTextureOverride.mString.AsInterface();
        if (!name->IsEqual(text, true)) {
            text = *name;
            if (mDescription && mRenderable.Get()) mRenderable->BuildMaterial(mDescription);
        }
        name->Release();
    }
    return true;
}

// 0xbbd380.
void cTSParticlesEffect::SetFlagged(bool value)
{
    mVisible = value;
    if (!value) {
        FromTheParent();
    } else if (mParent.Get() && !mRenderable->GetParent()) {
        mParent->AddChild(mRenderable.Get());
    }
    if (mVisible) mPreviousPosition = mLocal.translation;
}

// 0xbbd0a0.
void cTSParticlesEffect::Slot10(uint32_t follower)
{
    mFollower.Assign(reinterpret_cast<cITSComponentEffect *>(static_cast<uintptr_t>(follower)));
    static_cast<cTSParticlesEffect *>(mFollower.Get())->MarkFollowerSet(); // the follower's flag (a null follower: as in the game, no check)
}

// 0xbc0c20.
void cTSParticlesEffect::Slot11(uint32_t description)
{
    mDescription = VirtualCall<cTSParticlesDescription *(__thiscall *)(void *), 3>(reinterpret_cast<void *>(static_cast<uintptr_t>(description)));
    Prepare();
    mRenderable->Setup(mDescription);
    ResetLives();
}

// ---- the helpers ----

// 0xbbab40.
void cTSParticlesEffect::Prepare()
{
    const cTSParticlesDescription *const d = mDescription;
    float rate = d->mRateVary;
    if (!(rate > 1e-6f)) rate = 1e-6f; // a NaN too (maxss)
    mEmitPeriod = static_cast<double>(1.0f / rate);
    mFirstFrame = d->mFrameStart;
    cTSParticleAttractorState::Assign(&mAttractors, nullptr, &d->mAttractors);
    mSource.mMax = {d->mSourceMax.x, d->mSourceMax.y, d->mSourceMax.z};
    mSource.mMin = {d->mSourceMin.x, d->mSourceMin.y, d->mSourceMin.z};
    if (!d->mConstrain.empty()) mConstrain.Assign(FindConstraint(mOwner.Get(), d->mConstrain.c_str()));
    if (d->mColliders.empty()) return;
    uint32_t state = 0;
    mColliders.clear();
    mColliderState.clear();
    for (const cTSParticleCollider &collider : d->mColliders) {
        cIGZUnknown *const object = FindCollider(mOwner.Get(), collider.mName.c_str());
        if (!object) continue;
        const uint32_t wanted = VirtualCall<uint32_t(__thiscall *)(void *), 3>(object);
        mColliders.push_back({object, &collider, state});
        state += wanted;
    }
    mCollidersPlaced = false;
    mColliderState.resize(state);
}

// 0xbbd410. Back through the world and local placements to the emitter's space, the gravity's fall over the mean life taken
// off, and the rest is the direction (a rotation that takes +x there) and the speed that arrive in one mean life.
void cTSParticlesEffect::Aim(const Vector3 *target)
{
    Vector3 p = {Sub(target->x, mWorld.translation.x), Sub(target->y, mWorld.translation.y), Sub(target->z, mWorld.translation.z)};
    if (mWorld.scale != 1.0f) p = {Div(p.x, mWorld.scale), Div(p.y, mWorld.scale), Div(p.z, mWorld.scale)};
    if (mWorld.flags & EffectsTransform::kRotated) {
        const float (&m)[3][3] = mWorld.m;
        p = {RowFromX(p.x, p.y, p.z, m[0][0], m[1][0], m[2][0]), RowFromY(p.x, p.y, p.z, m[0][1], m[1][1], m[2][1]),
             RowFromY(p.x, p.y, p.z, m[0][2], m[1][2], m[2][2])};
    }
    p = {Sub(p.x, mLocal.translation.x), Sub(p.y, mLocal.translation.y), Sub(p.z, mLocal.translation.z)};
    if (mLocal.scale != 1.0f) p = {Div(p.x, mLocal.scale), Div(p.y, mLocal.scale), Div(p.z, mLocal.scale)};
    if (mLocal.flags & EffectsTransform::kRotated) {
        const float (&m)[3][3] = mLocal.m;
        p = {RowFromX(p.x, p.y, p.z, m[0][0], m[1][0], m[2][0]), RowFromY(p.x, p.y, p.z, m[0][1], m[1][1], m[2][1]),
             RowFromY(p.x, p.y, p.z, m[0][2], m[1][2], m[2][2])};
    }
    const float life = Mul(Add(mDescription->mLife.y, mDescription->mLife.x), 0.5f);
    const float fall = Mul(Mul(life, life), 0.5f);
    p = {Sub(p.x, Mul(mGravity.x, fall)), Sub(p.y, Mul(mGravity.y, fall)), Sub(p.z, Mul(mGravity.z, fall))};
    const float distance = fx::SqrtF(Add(Add(Mul(p.y, p.y), Mul(p.x, p.x)), Mul(p.z, p.z)));
    if (0.0f < distance) p = {Div(p.x, distance), Div(p.y, distance), Div(p.z, distance)};

    Vector3 side = {-p.y, p.x, -0.0f};
    Vector3 up;
    const float length = fx::SqrtF(Add(Add(Mul(side.x, side.x), Mul(side.y, side.y)), 0.0f));
    if (!(0.0f < length)) { // a NaN length too
        side = {0.0f, 1.0f, 0.0f};
        up = {-1.0f, -0.0f, -0.0f};
    } else {
        side = {Div(side.x, length), Div(side.y, length), Div(side.z, length)};
        up = {Sub(Mul(p.z, side.y), Mul(side.z, p.y)), Sub(Mul(p.x, side.z), Mul(side.x, p.z)), Sub(Mul(side.x, p.y), Mul(p.x, side.y))};
    }
    mLocal.flags |= EffectsTransform::kRotated;
    ++mLocal.Version();
    mLocal.m[0][0] = p.x;
    mLocal.m[0][1] = side.x;
    mLocal.m[0][2] = up.x;
    mLocal.m[1][0] = p.y;
    mLocal.m[1][1] = side.y;
    mLocal.m[1][2] = up.y;
    mLocal.m[2][0] = p.z;
    mLocal.m[2][1] = side.z;
    mLocal.m[2][2] = up.z;
    mSpeedScale = Div(Div(distance, Mul(Add(mDescription->mEmitVolume.y, mDescription->mEmitVolume.x), 0.5f)), life);
}

// 0xbbce70.
void cTSParticlesEffect::ClearParticles()
{
    for (cTSParticle *const particle : mParticles) {
        --mCount;
        RetireParticle(particle);
        if (mRenderable.Get() && mRenderable->mShape.mObject) mRenderable->RemoveParticleChild();
    }
    mParticles.clear();
}

// 0xbb94d0.
void cTSParticlesEffect::RetireParticle(cTSParticle *particle)
{
    if (mFollower.Get() && (particle->mAge >= particle->mLife || (LowFlags(mDescription) & kFlagAlwaysHandOn))) {
        HandParticleOn(particle);
        if (mFollower.Get() && mCount == 0 && mStopping) mFollower->Stop(0);
        return;
    }
    if (mStopMode == 0 && !mDescription->mDeathEffect.empty()) {
        cIGZUnknown *effect = nullptr;
        if (MakeEffect(mOwner.Get(), mDescription->mDeathEffect.c_str(), &effect)) {
            EffectsTransform place;
            place.Reset();
            place.translation = particle->mPosition;
            place.flags |= EffectsTransform::kTranslated;
            ++place.Version();
            VirtualCall<void(__thiscall *)(void *, EffectsTransform *), 10>(effect, &place);
            VirtualCall<void(__thiscall *)(void *, EffectsTransform *), 9>(effect, &mWorld);
            VirtualCall<void(__thiscall *)(void *, uint32_t), 3>(effect, 0);
        }
        if (effect) effect->Release();
    }
    if (particle) { // back to the pool
        void **const freeList = GameAddress<void *>(kPoolFreeCell);
        *reinterpret_cast<void **>(particle) = *freeList;
        *freeList = particle;
    }
}

// 0xbba740. `this` is the effect the particle leaves.
void cTSParticlesEffect::HandParticleOn(cTSParticle *particle)
{
    cTSParticlesEffect *const follower = static_cast<cTSParticlesEffect *>(mFollower.Get());
    const cTSParticlesDescription *const d = follower->mDescription;
    particle->mAge = 0.0f;
    particle->mLife = RandomIn(d->mLife.x, d->mLife.y);
    particle->mAlive = 1;
    if (!follower->IsRunning()) follower->Start(0);
    if (LowFlags(d) & kFlagFollowerInits) follower->InitParticle(particle);
    follower->AddParticle(particle);
}

// 0xbb5b30.
void cTSParticlesEffect::AddParticle(cTSParticle *particle)
{
    mParticles.push_back(particle);
    ++mCount;
    if (mRenderable.Get() && mRenderable->mShape.mObject) mRenderable->AddParticleChild();
}

// 0xbc1120.
void cTSParticlesEffect::ResetLife(cTSParticle *particle)
{
    particle->mLife = RandomIn(mDescription->mLife.x, mDescription->mLife.y);
    particle->mAge = 0.0f;
}

// 0xbc1160.
void cTSParticlesEffect::ResetLives()
{
    for (cTSParticle *const particle : mParticles) ResetLife(particle);
}

// 0xbc1b10.
void cTSParticlesEffect::StepWind(float step)
{
    const cTSParticlesDescription *const d = mDescription;
    mWindClock = Sub(mWindClock, step);
    while (mWindClock <= 0.0f) {
        mWindClock = Add(RandomIn(d->mWalkDelay.x, d->mWalkDelay.y), mWindClock);
        for (float &angle : mWindAngle) {
            const float turn = RandomIn(-d->mWalkTurn.x, d->mWalkTurn.x);
            angle = Add(turn, angle);
            angle = 0.0f > turn ? Sub(angle, d->mWalkTurn.y) : Add(angle, d->mWalkTurn.y);
        }
        const float strength = RandomIn(d->mWalkStrength.x, d->mWalkStrength.y);
        const float tilt = Mul(mWindAngle[1], 6.2831855f);
        const float turn = Mul(mWindAngle[0], 6.2831855f);
        const float cosTilt = fx::CosF(tilt), cosTurn = fx::CosF(turn), sinTurn = fx::SinF(turn), sinTilt = fx::SinF(tilt);
        mWind = {Mul(Mul(cosTurn, strength), cosTilt), Mul(Mul(sinTurn, strength), cosTilt), Mul(sinTilt, strength)};
    }
}
