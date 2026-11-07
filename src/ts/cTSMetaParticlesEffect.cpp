// The meta-particles effect's object, slots and small helpers (lane WZ, rendering row E7). Declarations and what is still the
// game's: src/ts/cTSMetaParticlesEffect.h. All scalar SSE single with the first-operand NaN rule of fx::Add / fx::Mul.
#include "ts/cTSMetaParticlesEffect.h"

#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/RZMemory.h"
#include "rz/cRZRandom.h"
#include "ts/ParticleCurve.h"
#include "ts/ParticleMath.h"

#include <bit>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <new>
#include <string>

using fx::Add;
using fx::CosF;
using fx::Div;
using fx::EffectsTransform;
using fx::Mul;
using fx::RandomIn;
using fx::RowFromX;
using fx::RowFromY;
using fx::SinF;
using fx::SqrtF;
using fx::Sub;
using fx::Vector3;
using nGZCompGeom::convex::Mul87;
using nGZCompGeom::convex::Stored;

namespace {

// The description's flags (the two words of +0x158).
constexpr uint32_t kFlagSurfaceEffects = 0x40;  // the terrain and water spawn names
constexpr uint32_t kFlagBurst = 0x1;           // emit: 0xc01310
constexpr uint32_t kFlagOnce = 0x2;            // emit: 0xc01590
constexpr uint32_t kFlagRestartable = 0x4;     // Start with an argument still runs when the rate mode is set
constexpr uint32_t kFlagOnSurface = 0x1000;
constexpr uint32_t kFlagSurfaceQuadrant = 0x2000;
constexpr uint32_t kFlagKeepHeight = 0xc000;   // either bit: the source box stays at the description's height
constexpr uint32_t kFlagWalks = 0x800000;
constexpr uint32_t kFlagWalkRandomStart = 0x2000000;
constexpr uint32_t kHighFlagInitOnHandOver = 0x1; // of the high word

constexpr uint32_t kNoDirectionCell = 0x10fd35c; // three floats the prefer direction is compared with
constexpr uint32_t kPoolElementCell = 0x107eef0, kPoolBlockCell = 0x107eef4, kPoolBlockListCell = 0x107eef8, kPoolFreeCell = 0x107eefc;

// Formats and suffixes of the names the owner is asked about (game text, by address).
constexpr uint32_t kSurfaceFormat0 = 0xe912d4, kSurfaceFormat1 = 0xe912e0, kDeathFormat = 0xe912ec;
constexpr uint32_t kDeathSuffix = 0xe91304, kHandOverSuffix = 0xe9130c;

constexpr float kTwoPi = 6.2831855f;
constexpr float kHalfPi = 1.5707964f;

uint32_t LowFlags(const cTSMetaParticlesDescription *d) { return static_cast<uint32_t>(d->mFlags); }
uint32_t HighFlags(const cTSMetaParticlesDescription *d) { return static_cast<uint32_t>(d->mFlags >> 32); }

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
bool KnowsEffect(cIGZUnknown *owner, const char *name)
{
    return VirtualCall<bool(__thiscall *)(void *, const char *), 12>(owner, name);
}
bool MakeEffect(cIGZUnknown *owner, const char *name, void *effect)
{
    return VirtualCall<bool(__thiscall *)(void *, const char *, void *), 14>(owner, name, effect);
}

// A spawned effect or a particle's, by the visual-effect face (its own slots, not cITSComponentEffect's).
void EffectStart(void *effect, uint32_t argument) { VirtualCall<void(__thiscall *)(void *, uint32_t), 3>(effect, argument); }
void EffectStop(void *effect, uint32_t mode) { VirtualCall<void(__thiscall *)(void *, uint32_t), 4>(effect, mode); }
bool EffectRunning(void *effect) { return VirtualCall<bool(__thiscall *)(void *), 5>(effect); }
void EffectHide(void *effect, bool hide) { VirtualCall<void(__thiscall *)(void *, bool), 8>(effect, hide); }
void EffectSetParent(void *effect, const EffectsTransform *t)
{
    VirtualCall<void(__thiscall *)(void *, const EffectsTransform *), 9>(effect, t);
}
void EffectSetLocal(void *effect, const EffectsTransform *t)
{
    VirtualCall<void(__thiscall *)(void *, const EffectsTransform *), 10>(effect, t);
}
void EffectQuery(void *effect, uint32_t key, void *value) { VirtualCall<void(__thiscall *)(void *, uint32_t, void *), 14>(effect, key, value); }

// A float's bits as a number line through zero; two floats closer than 32 steps count as the same.
int32_t OnLine(uint32_t bits)
{
    const uint32_t sign = static_cast<uint32_t>(static_cast<int32_t>(bits) >> 31);
    return static_cast<int32_t>(((sign >> 1) ^ bits) - sign);
}
bool Close(uint32_t a, uint32_t b)
{
    const int32_t difference = OnLine(a) - OnLine(b);
    return (difference >> 5) == (difference >> 31);
}

// fmul dword [mem] on a value the x87 holds: a NaN in the register beats a signalling memory operand.
double Mul87Memory(double held, float memory)
{
    if (held != held && memory != memory && !(std::bit_cast<uint32_t>(memory) & 0x00400000u)) return x87::Rounded(held);
    return Mul87(held, static_cast<double>(memory));
}

// The strengths a walk draws, by the mix between the plane and the pitch.
void DrawStrengths(cTSMetaWalk *state, float value, float mix)
{
    if (0.5f > mix) {
        state->mHorizontal = value;
        const float scaled = Mul(value, mix);
        state->mVertical = Add(scaled, scaled);
    } else {
        const float scaled = Mul(Sub(1.0f, mix), value);
        state->mHorizontal = Add(scaled, scaled);
        state->mVertical = value;
    }
}

} // namespace

// ---- the walks ----

// 0xc016f0.
void __cdecl fx::MetaDirectionAngles(const Vector3 *direction, float *heading, float *pitch)
{
    const float x = direction->x, y = direction->y;
    const float flat = Add(Mul(y, y), Mul(x, x));
    if (1e-6f > flat) {
        *heading = 0.0f;
        *pitch = 0.0f > direction->z ? -kHalfPi : kHalfPi;
        return;
    }
    *heading = static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)));
    const float z = direction->z;
    const float length = SqrtF(Add(Mul(z, z), flat));
    *pitch = static_cast<float>(std::asin(static_cast<double>(Div(z, length))));
}

// 0xbfdb00.
void cTSMetaWalk::Apply(Vector3 *force) const
{
    if (mVertical == 0.0f) {
        if (mHorizontal == 0.0f) return;
        force->x = Add(Mul(CosF(mHeading), mHorizontal), force->x);
        force->y = Add(Mul(SinF(mHeading), mHorizontal), force->y);
        return;
    }
    if (mHorizontal != 0.0f) {
        const float flat = CosF(mPitch);
        force->x = Add(Mul(Mul(CosF(mHeading), mHorizontal), flat), force->x);
        force->y = Add(Mul(Mul(SinF(mHeading), mHorizontal), flat), force->y);
    }
    force->z = Add(Mul(SinF(mPitch), mVertical), force->z);
}

// 0xbfffb0.
void cTSMetaWalk::Init(const cTSParticleWalk *walk, const Vector3 *direction, bool randomStart)
{
    fx::MetaDirectionAngles(direction, &mHeading, &mPitch);
    if (!randomStart) {
        mClock = 0.0f;
        Next(walk);
        return;
    }
    mClock = RandomIn(walk->mDelay.x, walk->mDelay.y);
    DrawStrengths(this, RandomIn(walk->mStrength.x, walk->mStrength.y), walk->mMix);
}

// 0xc03cb0.
void cTSMetaWalk::Next(const cTSParticleWalk *walk)
{
    mClock = Add(RandomIn(walk->mDelay.x, walk->mDelay.y), mClock);
    DrawStrengths(this, RandomIn(walk->mStrength.x, walk->mStrength.y), walk->mMix);
    const float range = Add(walk->mTurn[0], 1e-5f);
    if (1.0f > walk->mMix) {
        const float turn = RandomIn(-range, range);
        const float angle = Add(Mul(turn, kTwoPi), mHeading);
        const float extra = Mul(walk->mTurn[1], kTwoPi);
        mHeading = 0.0f > turn ? Sub(angle, extra) : Add(extra, angle);
    }
    if (walk->mMix > 0.0f) {
        const float turn = RandomIn(-range, range);
        const float angle = Add(Mul(turn, kTwoPi), mPitch);
        const float extra = Mul(walk->mTurn[1], kTwoPi);
        mPitch = 0.0f > turn ? Sub(angle, extra) : Add(extra, angle);
    }
}

// ---- the particle and its pool ----

// 0xbfd4a0.
cTSMetaParticle::cTSMetaParticle()
{
    std::memset(static_cast<void *>(this), 0, sizeof(*this));
}

// 0xbfda30.
void *__cdecl cTSMetaParticle::Allocate(size_t)
{
    void **const freeList = GameAddress<void *>(kPoolFreeCell);
    if (!*freeList) {
        void **const blocks = GameAddress<void *>(kPoolBlockListCell);
        const uint32_t element = *GameAddress<uint32_t>(kPoolElementCell);
        const uint32_t bytes = *GameAddress<uint32_t>(kPoolBlockCell);
        void **const block = static_cast<void **>(RZMemory::Malloc(bytes + 8));
        *block = *blocks;
        *blocks = block;
        char *const first = reinterpret_cast<char *>(block + 1);
        *freeList = first;
        char *const last = first + (bytes / element - 1) * element;
        for (char *cell = first; cell < last; cell += element) *reinterpret_cast<char **>(cell) = cell + element;
        *reinterpret_cast<char **>(last) = nullptr;
    }
    void **const cell = static_cast<void **>(*freeList);
    *freeList = *cell;
    return cell;
}

// 0xbfdab0.
void __cdecl cTSMetaParticle::Free(void *particle)
{
    if (!particle) return;
    void **const freeList = GameAddress<void *>(kPoolFreeCell);
    *static_cast<void **>(particle) = *freeList;
    *freeList = particle;
}

// ---- life and death ----

// 0xbfd4f0. The words the constructor leaves unset stay out of the list (+0x29, +0x61, +0x98, +0xa0, +0xb0, +0xc0, the walks,
// +0x11c.., +0x144, +0x148, +0x174, +0x188, +0x1ec).
cTSMetaParticlesEffect::cTSMetaParticlesEffect(cTSMetaParticlesDescription *description)
    : mAttached(false), mRunning(false), mStopping(false), mVisible(true), mCount(0), mDescription(description), mSettings(nullptr),
      mEmitClock(0.0), mEmitPeriod(-1.0), mForce{0.0f, 0.0f, 0.0f}, mHasPreferDir(false), mPreferHeading(0.0f),
      mPreferPitch(0.0f), mStartArgument(1), mStopMode(1), mDelay(0.0f), mEvent101{1.0f, 1.0f}, mSpeedScale(1.0f), mEvent3(1.0f),
      mEvent4{1.0f, 1.0f, 1.0f}, mSurfaceRange{0.0f, 0.0f}, mSurfaceBase(0.0f),
      mSource{{FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX}}, mHasEffect(false), mHasDeathEffect(false),
      mHasSurfaceEffect{false, false}, mHasSpawnArgument(false), mFirstFrame(-1.0f), mWord1c4(1.0f), mWord1d8(0)
{
    mLocal.Reset();
    mWorld.Reset();
}

// 0xbfd840: the members, last first.
cTSMetaParticlesEffect::~cTSMetaParticlesEffect() = default;

// 0xc03910.
cITSComponentEffect *__cdecl cTSMetaParticlesEffect::Create(cTSMetaParticlesDescription *description)
{
    return new (std::nothrow) cTSMetaParticlesEffect(description);
}

// ---- the slots ----

// 0xbffe20.
void cTSMetaParticlesEffect::Attach(cIGZUnknown *owner)
{
    if (mAttached) return;
    mAttached = true;
    mOwner.Assign(owner);
    mSettings = SettingsOf(owner);
    mSpeedScale = mEvent3 = mRateScale = mLifeScale = 1.0f;
    mEvent4 = {1.0f, 1.0f, 1.0f};
    mEmitterPosition = {0.0f, 0.0f, 0.0f};
    mFollower.Reset();
    mFollowerSet = false;
    mHasSpawnArgument = false;
    mSpawnArgument.Reset();
    mSurface.Assign(SurfaceOf(owner));
    if (mSurface.Get()) {
        float low, high;
        VirtualCall<void(__thiscall *)(void *, float *, float *, float *, float *), 3>(mSurface.Get(), &low, &high, &mSurfaceRange[0],
                                                                                      &mSurfaceRange[1]);
        mSurfaceRange[0] = Sub(mSurfaceRange[0], low);
        mSurfaceRange[1] = Sub(mSurfaceRange[1], high);
        mSurfaceBase = VirtualCall<float(__thiscall *)(void *), 7>(mSurface.Get());
    }
    Prepare();
}

// 0xc02ea0.
void cTSMetaParticlesEffect::Detach()
{
    if (!mAttached) return;
    mAttached = false;
    if (mRunning) Stop(1);
    if (mNode.Get()) {
        if (VirtualCall<void *(__thiscall *)(void *), 34>(mNode.Get())) {
            void *const parent = VirtualCall<void *(__thiscall *)(void *), 34>(mNode.Get());
            VirtualCall<void(__thiscall *)(void *, void *), 46>(parent, mNode.Get());
        }
    }
    mNode.Reset();
    mSurface.Reset();
    mDescription = nullptr;
}

// 0xc035f0.
void cTSMetaParticlesEffect::Start(uint32_t a)
{
    const cTSMetaParticlesDescription *const d = mDescription;
    if (a != 0 && d->mRateMode != 0 && !(LowFlags(d) & kFlagRestartable)) return;
    if (!mHasEffect) return;
    mRunning = true;
    mStopping = false;
    mEmitClock = 0.0;
    mEmitted = 0;
    mTime = 0.0;
    mEmitRemainder = 0.0f;
    mForce = {d->mForce.x, d->mForce.y, d->mForce.z};
    const uint32_t flags = LowFlags(d);
    if (flags & kFlagWalks) {
        Vector3 direction;
        fx::RandomInBox(&direction, reinterpret_cast<const float *>(&d->m38));
        for (int i = 0; i < 2; ++i) mWalks[i].Init(&mDescription->mWalks[i], &direction, (flags & kFlagWalkRandomStart) != 0);
    }
    float delay = 0.0f;
    if (d->m28.x >= 0.0f) { // a NaN takes neither
        delay = RandomIn(d->m28.x, d->m28.y);
    } else if (d->m30.x >= 0.0f) {
        delay = RandomIn(d->m30.x, d->m30.y);
    }
    mDelay = delay;
    mStartArgument = a;
    mStopMode = 0;
    mPrerollPending = a != 0;
}

// 0xc03770.
void cTSMetaParticlesEffect::Stop(uint32_t a)
{
    if (!mRunning) return;
    if (mFollower.Get() && mCount == 0) mFollower->Stop(a);
    mStopMode = a;
    mStopping = a == 0;
    if (a == 0) return;
    mRunning = false;
    for (cTSMetaParticle *const particle : mParticles) {
        RetireParticle(particle);
        --mCount;
    }
    mParticles.clear();
    for (size_t i = 0; i < mSpawned.size(); ++i) {
        if (EffectRunning(mSpawned[i].Get())) EffectStop(mSpawned[i].Get(), a);
    }
    mSpawned.clear();
}

// 0xc02bb0.
void cTSMetaParticlesEffect::Update(const EffectsTransform *local, const EffectsTransform *parent)
{
    mLocal = *local;
    mWorld = *parent;
    mCollidersPlaced = false;
    if (!mFollowerSet && (LowFlags(mDescription) & kFlagOnSurface)) {
        mLocal.flags &= ~EffectsTransform::kTranslated;
        ++mLocal.Version();
        mWorld.flags &= ~EffectsTransform::kTranslated;
        ++mWorld.Version();
        mLocal.translation = {0.0f, 0.0f, 0.0f};
        mWorld.translation = {0.0f, 0.0f, 0.0f};
    }
    Vector3 p = mLocal.translation;
    if (mWorld.flags & EffectsTransform::kRotated) {
        const float (&m)[3][3] = mWorld.m;
        p = {RowFromY(p.x, p.y, p.z, m[0][0], m[0][1], m[0][2]), RowFromY(p.x, p.y, p.z, m[1][0], m[1][1], m[1][2]),
             RowFromY(p.x, p.y, p.z, m[2][0], m[2][1], m[2][2])};
    }
    const float scale = mWorld.scale;
    mEmitterPosition = {Add(Mul(scale, p.x), mWorld.translation.x), Add(Mul(scale, p.y), mWorld.translation.y),
                        Add(Mul(scale, p.z), mWorld.translation.z)};
    if (mWorld.scale != 1.0f || (mWorld.flags & (EffectsTransform::kRotated | EffectsTransform::kTranslated))) {
        for (size_t i = 0; i < mSpawned.size(); ++i) EffectSetParent(mSpawned[i].Get(), &mWorld);
    }
}

// 0xc020c0. `a` the values; `b` a count, for event 0xe.
bool cTSMetaParticlesEffect::HandleEvent(uint32_t kind, void *a, void *b)
{
    const float *const v = static_cast<const float *>(a);
    switch (kind) {
    case 0: mSpeedScale = *v; return true;
    case 1: mLifeScale = *v; return true;
    case 2: mRateScale = *v; return true;
    case 3: mEvent3 = *v; return true;
    case 4: mEvent4 = *static_cast<const Vector3 *>(a); return true;
    case 5: Aim(static_cast<const Vector3 *>(a)); return true;
    case 7:
        if (!mAttractors.empty()) {
            Vector3 point = *static_cast<const Vector3 *>(a);
            mWorld.ToLocal(&point);
            mAttractors[0].SetCentre(&point);
        }
        return true;
    case 8:
        if (!mAttractors.empty()) mAttractors[0].mStrength = *v;
        return true;
    case 9: // the point of the line (v0 v1 v2) + t (v3 v4 v5) at the emitter's height
        if (!mAttractors.empty()) {
            const float along = Sub(mEmitterPosition.y, v[1]);
            const float slope = v[4];
            Vector3 point = {Add(Div(Mul(v[3], along), slope), v[0]), Add(Div(Mul(v[4], along), slope), v[1]),
                             Add(Div(Mul(v[5], along), slope), v[2])};
            mWorld.ToLocal(&point);
            mAttractors[0].SetCentre(&point);
            mAttractors[0].mStrength = 1.0f;
        }
        return true;
    case 0xd:
        mFirstFrame = static_cast<float>(static_cast<int32_t>(std::bit_cast<uint32_t>(Add(*v, 12582912.0f)) + 0xb4c00000u));
        return true;
    case 0xe: { // `b` numbers, three to a point: a path at the description's speed
        const int32_t count = static_cast<int32_t>(reinterpret_cast<intptr_t>(b)) / 3;
        const float speed = mDescription->mEmitVolume.x;
        const Vector3 *const points = static_cast<const Vector3 *>(a);
        mPath.clear();
        float time = 0.0f;
        for (int32_t i = 0; i < count; ++i) {
            const Vector3 &before = points[i - 1 < 0 ? 0 : i - 1];
            const Vector3 &after = points[i + 1 >= count ? count - 1 : i + 1];
            const Vector3 along = {Sub(after.x, before.x), Sub(after.y, before.y), Sub(after.z, before.z)};
            const float length = SqrtF(Add(Add(Mul(along.x, along.x), Mul(along.y, along.y)), Mul(along.z, along.z)));
            cTSParticlePathPoint point;
            point.mPosition = {points[i].x, points[i].y, points[i].z};
            if (length > 1e-6f) {
                const float scale = Div(speed, length);
                point.mVelocity = {Mul(along.x, scale), Mul(along.y, scale), Mul(along.z, scale)};
            } else {
                point.mVelocity = {0.0f, 0.0f, 0.0f};
            }
            if (i != 0) {
                const Vector3 step = {Sub(points[i].x, points[i - 1].x), Sub(points[i].y, points[i - 1].y), Sub(points[i].z, points[i - 1].z)};
                const float piece = SqrtF(Add(Add(Mul(step.x, step.x), Mul(step.y, step.y)), Mul(step.z, step.z)));
                time = Add(Div(piece, speed), time);
            }
            point.mTime = time;
            mPath.push_back(point);
        }
        return true;
    }
    case 0x101: SetEvent101(v[0], v[1]); return true;
    default: return false;
    }
}

// 0xc02dd0.
bool cTSMetaParticlesEffect::Query(void *a, void *b)
{
    if (a == nullptr) {
        cIGZString *name = nullptr;
        static_cast<cIGZUnknown *>(b)->QueryInterface(cRZString::kGZIID_cIGZString, reinterpret_cast<void **>(&name));
        if (name) {
            *mNameOverride.mString.AsInterface() = *name;
            name->Release();
        }
        return true;
    }
    if (reinterpret_cast<uintptr_t>(a) == 1) {
        mHasSpawnArgument = true;
        mSpawnArgument.Assign(static_cast<cIGZUnknown *>(b));
        return true;
    }
    return false;
}

// 0xc02600.
void cTSMetaParticlesEffect::SetFlagged(bool value)
{
    mVisible = value;
    for (cTSMetaParticle *const particle : mParticles) {
        if (particle->mEffect.Get()) EffectHide(particle->mEffect.Get(), !value);
    }
    for (size_t i = 0; i < mSpawned.size(); ++i) EffectHide(mSpawned[i].Get(), !value);
}

// 0xc02080.
void cTSMetaParticlesEffect::Slot10(uint32_t follower)
{
    mFollower.Assign(reinterpret_cast<cITSComponentEffect *>(static_cast<uintptr_t>(follower)));
    static_cast<cTSMetaParticlesEffect *>(mFollower.Get())->MarkFollowerSet(); // a null follower: as in the game, no check
}

// 0xc038f0.
void cTSMetaParticlesEffect::Slot11(uint32_t description)
{
    mDescription = VirtualCall<cTSMetaParticlesDescription *(__thiscall *)(void *), 3>(reinterpret_cast<void *>(static_cast<uintptr_t>(description)));
    Prepare();
}

// ---- the helpers ----

// 0xc00160.
void cTSMetaParticlesEffect::Prepare()
{
    const cTSMetaParticlesDescription *const d = mDescription;
    float rate = d->mRateVary;
    if (!(rate > 1e-6f)) rate = 1e-6f; // a NaN too
    mEmitPeriod = static_cast<double>(1.0f / rate);
    mSource.mMax = {d->mSourceMax.x, d->mSourceMax.y, d->mSourceMax.z};
    mSource.mMin = {d->mSourceMin.x, d->mSourceMin.y, d->mSourceMin.z};
    const uint32_t flags = LowFlags(d);
    if (flags & kFlagOnSurface) {
        if (!(flags & kFlagKeepHeight)) {
            mSource.mMin.z = Add(mSurfaceBase, mSource.mMin.z);
            mSource.mMax.z = Add(mSurfaceBase, mSource.mMax.z);
        }
        if (flags & kFlagSurfaceQuadrant) {
            ChooseSourceQuadrant(0);
        } else {
            mSource.mMin.x = 0.0f;
            mSource.mMax.x = mSurfaceRange[0];
            mSource.mMin.y = 0.0f;
            mSource.mMax.y = mSurfaceRange[1];
        }
    }

    const uint32_t *const prefer = reinterpret_cast<const uint32_t *>(&d->mPreferDir);
    const uint32_t *const zero = GameAddress<uint32_t>(kNoDirectionCell);
    if (Close(prefer[0], zero[0]) && Close(prefer[1], zero[1]) && Close(prefer[2], zero[2])) {
        mHasPreferDir = false;
    } else {
        mHasPreferDir = true;
        fx::MetaDirectionAngles(reinterpret_cast<const Vector3 *>(&d->mPreferDir), &mPreferHeading, &mPreferPitch);
    }

    if (flags & kFlagSurfaceEffects) {
        {
            cTSParticlesEffect::Text name;
            mHasSurfaceEffect[0] = KnowsEffect(mOwner.Get(), name.mString.Sprintf(GameAddress<const char>(kSurfaceFormat0), d->mEffect.c_str())->ToChar());
        }
        cTSParticlesEffect::Text name;
        mHasSurfaceEffect[1] = KnowsEffect(mOwner.Get(), name.mString.Sprintf(GameAddress<const char>(kSurfaceFormat1), mDescription->mEffect.c_str())->ToChar());
    }

    cTSParticleAttractorState::Assign(&mAttractors, nullptr, &mDescription->mAttractors);

    if (!mDescription->mColliders.empty()) {
        uint32_t state = 0;
        mColliders.clear();
        mColliderState.clear();
        for (const cTSParticleCollider &collider : mDescription->mColliders) {
            cIGZUnknown *const object = VirtualCall<cIGZUnknown *(__thiscall *)(void *, const char *), 28>(mOwner.Get(), collider.mName.c_str());
            if (!object) continue;
            const uint32_t wanted = VirtualCall<uint32_t(__thiscall *)(void *), 3>(object);
            mColliders.push_back({object, &collider, state});
            state += wanted;
        }
        mCollidersPlaced = false;
        mColliderState.resize(state);
    }

    mHasEffect = KnowsEffect(mOwner.Get(), mDescription->mEffect.c_str());
    cTSParticlesEffect::Text name;
    mHasDeathEffect = KnowsEffect(mOwner.Get(), name.mString.Sprintf(GameAddress<const char>(kDeathFormat), mDescription->mEffect.c_str())->ToChar());
}

// 0xc02a80.
void cTSMetaParticlesEffect::ChooseSourceQuadrant(uint32_t quadrant)
{
    int32_t chosen = static_cast<int32_t>(quadrant);
    if (LowFlags(mDescription) & kFlagSurfaceQuadrant) {
        const float *const wind = reinterpret_cast<const float *>(mSettings->mBefore); // +0x10, unnamed in the settings so far
        const float x = wind[0], y = wind[1];
        const int32_t turn = x > y ? (x > 0.0f ? 3 : 2) : (y > 0.0f ? 0 : 1);
        chosen = (chosen + turn) % 4; // a negative sum stays negative: no case
    }
    const float width = mSurfaceRange[0], depth = mSurfaceRange[1];
    switch (static_cast<uint32_t>(chosen)) {
    case 0:
        mSource.mMin.x = 0.0f;
        mSource.mMax.x = width;
        mSource.mMin.y = 0.0f;
        mSource.mMax.y = 0.0f;
        break;
    case 1:
        mSource.mMin.x = width;
        mSource.mMax.x = width;
        mSource.mMin.y = 0.0f;
        mSource.mMax.y = depth;
        break;
    case 2:
        mSource.mMin.x = 0.0f;
        mSource.mMax.x = width;
        mSource.mMin.y = depth;
        mSource.mMax.y = depth;
        break;
    case 3:
        mSource.mMin.x = 0.0f;
        mSource.mMax.x = 0.0f;
        mSource.mMin.y = 0.0f;
        mSource.mMax.y = depth;
        break;
    default: break;
    }
}

// 0xc026a0. Back through the world and local placements to the emitter's space (the local scale is not undone), the force's
// fall over the mean life taken off; the rest is the direction and the speed that arrive in one mean life.
void cTSMetaParticlesEffect::Aim(const Vector3 *target)
{
    Vector3 p = {Sub(target->x, mWorld.translation.x), Sub(target->y, mWorld.translation.y), Sub(target->z, mWorld.translation.z)};
    if (mWorld.scale != 1.0f) p = {Div(p.x, mWorld.scale), Div(p.y, mWorld.scale), Div(p.z, mWorld.scale)};
    if (mWorld.flags & EffectsTransform::kRotated) {
        const float (&m)[3][3] = mWorld.m;
        p = {RowFromX(p.x, p.y, p.z, m[0][0], m[1][0], m[2][0]), RowFromY(p.x, p.y, p.z, m[0][1], m[1][1], m[2][1]),
             RowFromY(p.x, p.y, p.z, m[0][2], m[1][2], m[2][2])};
    }
    p = {Sub(p.x, mLocal.translation.x), Sub(p.y, mLocal.translation.y), Sub(p.z, mLocal.translation.z)};
    if (mLocal.flags & EffectsTransform::kRotated) {
        const float (&m)[3][3] = mLocal.m;
        p = {RowFromX(p.x, p.y, p.z, m[0][0], m[1][0], m[2][0]), RowFromY(p.x, p.y, p.z, m[0][1], m[1][1], m[2][1]),
             RowFromY(p.x, p.y, p.z, m[0][2], m[1][2], m[2][2])};
    }
    const float life = Mul(Add(mDescription->mLife.y, mDescription->mLife.x), 0.5f);
    const float fall = Mul(Mul(life, life), 0.5f);
    p = {Sub(p.x, Mul(mForce.x, fall)), Sub(p.y, Mul(mForce.y, fall)), Sub(p.z, Mul(mForce.z, fall))};
    const float distance = SqrtF(Add(Add(Mul(p.y, p.y), Mul(p.x, p.x)), Mul(p.z, p.z)));
    if (0.0f < distance) p = {Div(p.x, distance), Div(p.y, distance), Div(p.z, distance)};

    const float negZ = -p.z;
    const float length = SqrtF(Add(Add(Mul(negZ, negZ), 0.0f), Mul(p.x, p.x)));
    Vector3 second, third;
    if (0.0f < length) {
        second = {-p.y, p.x, -0.0f};
        third = {Div(negZ, length), Div(0.0f, length), Div(p.x, length)};
    } else { // a NaN length too
        second = {-1.0f, 0.0f, 0.0f};
        third = {0.0f, 0.0f, 1.0f};
    }
    mLocal.flags |= EffectsTransform::kRotated;
    ++mLocal.Version();
    mLocal.m[0][0] = p.x;
    mLocal.m[0][1] = p.y;
    mLocal.m[0][2] = p.z;
    mLocal.m[1][0] = second.x;
    mLocal.m[1][1] = second.y;
    mLocal.m[1][2] = second.z;
    mLocal.m[2][0] = third.x;
    mLocal.m[2][1] = third.y;
    mLocal.m[2][2] = third.z;
    mSpeedScale = Div(Div(distance, Mul(Add(mDescription->mEmitVolume.y, mDescription->mEmitVolume.x), 0.5f)), life);
}

// 0xc02680.
void cTSMetaParticlesEffect::SetEvent101(float a, float b)
{
    mEvent101[0] = a;
    mEvent101[1] = b;
}

// 0xbff510.
void cTSMetaParticlesEffect::PruneSpawned()
{
    size_t kept = 0;
    for (size_t i = 0; i < mSpawned.size(); ++i) {
        if (!EffectRunning(mSpawned[i].Get())) continue;
        if (kept < i) mSpawned[kept].Assign(mSpawned[i].Get());
        ++kept;
    }
    mSpawned.resize(kept);
}

// 0xbff610.
void cTSMetaParticlesEffect::RetireParticle(cTSMetaParticle *particle)
{
    if (mFollower.Get() && particle->mLife <= particle->mAge) {
        HandParticleOn(particle);
    } else {
        if (particle->mEffect.Get()) {
            EffectStop(particle->mEffect.Get(), mStopMode);
            if (mStopMode == 0 && EffectRunning(particle->mEffect.Get())) mSpawned.push_back(particle->mEffect);
            particle->mEffect.Reset();
        }
        fx::CountedRef<cIGZUnknown> effect;
        if (mHasDeathEffect && mStopMode == 0 && mVisible) {
            cIGZUnknown *const owner = mOwner.Get();
            const std::string name = mDescription->mEffect + GameAddress<const char>(kDeathSuffix);
            if (MakeEffect(owner, name.c_str(), effect.Address())) {
                EffectsTransform place;
                place.Reset();
                place.translation = particle->mPosition;
                place.flags |= EffectsTransform::kTranslated;
                ++place.Version();
                EffectSetParent(effect.Get(), &mWorld);
                EffectSetLocal(effect.Get(), &place);
                if (mHasSpawnArgument) EffectQuery(effect.Get(), 1, mSpawnArgument.Get());
                EffectHide(effect.Get(), !mVisible);
                EffectStart(effect.Get(), 0);
                mSpawned.push_back(effect);
            }
        }
        cTSMetaParticle::Free(particle);
    }
    if (mFollower.Get() && mCount == 0 && mStopping) mFollower->Stop(0);
}

// 0xc03970. A one-off effect (the description's name and the suffix) started where a particle hit the surface, in the effect's
// space, left to run out.
void cTSMetaParticlesEffect::SpawnSurfaceEffect(const char *suffix, const Vector3 *place)
{
    const std::string name = mDescription->mEffect + suffix;
    if (!mVisible) return;
    fx::CountedRef<cIGZUnknown> effect;
    if (!MakeEffect(mOwner.Get(), name.c_str(), effect.Address())) return;
    Vector3 point = *place;
    mWorld.ToLocal(&point);
    EffectSetParent(effect.Get(), &mWorld);
    mSpawned.push_back(effect);
    EffectsTransform local;
    local.Reset();
    local.translation = point;
    local.flags |= EffectsTransform::kTranslated;
    ++local.Version();
    EffectSetLocal(effect.Get(), &local);
    EffectHide(effect.Get(), !mVisible);
    EffectStart(effect.Get(), 0);
}

// 0xbffbd0.
void cTSMetaParticlesEffect::HandParticleOn(cTSMetaParticle *particle)
{
    cTSMetaParticlesEffect *const follower = static_cast<cTSMetaParticlesEffect *>(mFollower.Get());
    const cTSMetaParticlesDescription *const d = follower->mDescription;
    particle->mAge = 0.0f;
    particle->mLife = RandomIn(d->mLife.x, d->mLife.y);
    particle->mAlive = 1;
    if (!d->mEffect.empty() && d->mEffect != mDescription->mEffect) {
        EffectsTransform parent, local;
        parent.Reset();
        local.Reset();
        if (particle->mEffect.Get()) {
            VirtualCall<void(__thiscall *)(void *, EffectsTransform *), 18>(particle->mEffect.Get(), &parent);
            VirtualCall<void(__thiscall *)(void *, EffectsTransform *), 19>(particle->mEffect.Get(), &local);
            EffectStop(particle->mEffect.Get(), mStopMode);
            particle->mEffect.Reset();
        }
        cIGZUnknown *const owner = mOwner.Get();
        const std::string name = d->mEffect + GameAddress<const char>(kHandOverSuffix);
        particle->mEffect.Reset();
        if (MakeEffect(owner, name.c_str(), particle->mEffect.Address())) {
            EffectSetParent(particle->mEffect.Get(), &parent);
            EffectSetLocal(particle->mEffect.Get(), &parent); // ORIGINAL-BUG: the parent place again; `local`, read just above, is not used
            if (mHasSpawnArgument) EffectQuery(particle->mEffect.Get(), 1, mSpawnArgument.Get());
            EffectHide(particle->mEffect.Get(), !mVisible);
            EffectStart(particle->mEffect.Get(), mStartArgument);
        }
    }
    if (!follower->IsRunning()) follower->Start(0);
    if (HighFlags(d) & kHighFlagInitOnHandOver) {
        follower->InitParticle(particle);
    }
    follower->AddParticle(particle);
}

// 0xbff4a0.
cTSMetaParticle *cTSMetaParticlesEffect::NewParticle()
{
    cTSMetaParticle *const particle = new (cTSMetaParticle::Allocate(sizeof(cTSMetaParticle))) cTSMetaParticle();
    InitParticle(particle);
    return particle;
}

// 0xbfdc30.
void cTSMetaParticlesEffect::AddParticle(cTSMetaParticle *particle)
{
    mParticles.push_back(particle);
    ++mCount;
}

// 0xbffb30. E8's emitters.
void cTSMetaParticlesEffect::Emit(float step)
{
    if (mStopping) return;
    if (mDelay > 0.0f) {
        const float left = Sub(mDelay, step);
        mDelay = left;
        if (left > 0.0f) return;
        step = -left;
    }
    float use = step;
    const float preroll = mDescription->mPreroll;
    if (preroll > 0.0f) {
        const float limit = Sub(preroll, 0.01f);
        use = limit < step ? limit : step;
    }
    const uint32_t flags = LowFlags(mDescription);
    if (flags & kFlagOnce) {
        EmitOnce(use);
    } else if (flags & kFlagBurst) {
        EmitBurst(use);
    } else {
        EmitContinuous(use);
    }
}

// 0xc047e0.
void cTSMetaParticlesEffect::TickWalks(float step)
{
    for (int i = 0; i < 2; ++i) {
        mWalks[i].mClock = Sub(mWalks[i].mClock, step);
        if (0.0f > mWalks[i].mClock) {
            do {
                mWalks[i].Next(&mDescription->mWalks[i]);
            } while (0.0f > mWalks[i].mClock);
        }
    }
}

// 0xbfe190.
bool cTSMetaParticlesEffect::CollideColliders(cTSMetaParticle *particle, const Vector3 *vector)
{
    const float curve = fx::EvaluateCurve(mDescription->mSize, Div(particle->mAge, particle->mLife));
    const float distance = Stored(Mul87Memory(Mul87Memory(static_cast<double>(curve), particle->mSizeVary), mWorld.scale));
    return fx::SweepColliders(mOwner.Get(), &mColliders, &mColliderState, distance, vector, &particle->mPosition, &particle->mVelocity,
                       !mVisible, &mSpawned, &mWorld);
}
