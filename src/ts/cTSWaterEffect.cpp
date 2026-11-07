#include "ts/cTSWaterEffect.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "rz/RZMemory.h"
#include "rz/RZMessageServer.h"
#include "rz/StaticGetters.h"
#include "rz/cRZMessage2Standard.h"
#include "ts/EffectsPosition.h"
#include "ts/cTSCompDescription.h"

#include <bit>
#include <new>

using nGZSceneGraph::cICompositionTreeNode;
using nGZSceneGraph::cITransformNode;

namespace {

// The water body's slots (named from their uses here; the object is the director's, class 0x4c181525).
using BodyVoidFn = void(__thiscall *)(void *);
using BodyAddressFn = void(__thiscall *)(void *, const void *);
using BodyIntFn = void(__thiscall *)(void *, int32_t);
using BodyTwoFn = void(__thiscall *)(void *, uint32_t, uint32_t);
using BodyFloatFn = void(__thiscall *)(void *, float);
using BodyNodeFn = cITransformNode *(__thiscall *)(void *);

// A float in hundredths rounded to the nearest whole number the way the original does it: adding 1.5 * 2^23 in single
// precision and taking the low bits.
int32_t Hundredths(float value)
{
    const float shifted = value * 100.0f + 12582912.0f;
    return std::bit_cast<int32_t>(shifted) - 0x4b400000;
}

} // namespace

cTSWaterEffect::cTSWaterEffect(const cTSWaterDescription *description)
    : cTSEffectBase(description), mElapsed(0)
{
}

cTSWaterEffect::~cTSWaterEffect()
{
    if (mBody.Get()) TakeBodyDown();
}

cITSComponentEffect *__cdecl cTSWaterEffect::Create(const cTSWaterDescription *description)
{
    return new (std::nothrow) cTSWaterEffect(description);
}

void cTSWaterEffect::Attach(cIGZUnknown *owner) { mOwner.Assign(owner); }

void cTSWaterEffect::Start(uint32_t)
{
    if (mRunning) return;
    mRunning = true;
    mElapsed = 0;
    if (static_cast<const cTSWaterDescription *>(mDescription)->mFlags & 1) MakeBody();
}

void cTSWaterEffect::Stop(uint32_t)
{
    if (!mRunning) return;
    mRunning = false;
    if (mBody.Get()) TakeBodyDown();
}

void cTSWaterEffect::MakeBody()
{
    // ORIGINAL-BUG: an effect with the body flag started before Attach has no owner, and this call crashes on null.
    auto *tree = VirtualCall<cICompositionTreeNode *(__thiscall *)(void *), 42>(mOwner.Get());
    if (!tree || mBody.Get()) return;
    auto *com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    mBody.Reset();
    com->GetClassObject(kBodyClass, kBodyIid, reinterpret_cast<void **>(mBody.Address()));
    void *body = mBody.Get();
    if (!body) return;
    const auto *d = static_cast<const cTSWaterDescription *>(mDescription);
    VirtualCall<BodyVoidFn, 3>(body);
    VirtualCall<BodyAddressFn, 7>(body, &d->mFloat14);
    VirtualCall<BodyIntFn, 6>(body, d->mShortA);
    VirtualCall<BodyTwoFn, 5>(body, d->mByte8, d->mByte9);
    VirtualCall<BodyVoidFn, 8>(body);
    VirtualCall<BodyNodeFn, 11>(body)->SetTranslation(mPosition);
    tree->AddChild(VirtualCall<BodyNodeFn, 11>(body));
}

void cTSWaterEffect::TakeBodyDown()
{
    void *body = mBody.Get();
    if (!body) return;
    if (VirtualCall<BodyNodeFn, 11>(body)->GetParent()) {
        void *again = mBody.Get();
        cICompositionTreeNode *parent = VirtualCall<BodyNodeFn, 11>(again)->GetParent();
        parent->RemoveChild(VirtualCall<BodyNodeFn, 11>(again));
    }
    VirtualCall<BodyVoidFn, 4>(mBody.Get());
    mBody.Reset();
}

void cTSWaterEffect::Splash(float a, float b)
{
    auto *message = static_cast<uint8_t *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
    // ORIGINAL-BUG: a failed allocation is not checked, so the calls below go through a null message and crash.
    // cRZMessage2Standard's constructor (0x1c033d), inline: the game's vtable, a zero count and flags, zero fields.
    *reinterpret_cast<const void **>(message) = GameAddress(cRZMessage2Standard::kVTable);
    *reinterpret_cast<uint32_t *>(message + 4) = 0;
    message[8] = 0;
    for (uint32_t offset = 0x0c; offset <= 0x28; offset += 4) *reinterpret_cast<uint32_t *>(message + offset) = 0;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(message); // AddRef
    VirtualCall<void(__thiscall *)(void *, uint32_t), 5>(message, kSplashMessage);
    VirtualCall<BodyIntFn, 11>(message, Hundredths(mPosition[0]));
    VirtualCall<BodyIntFn, 16>(message, Hundredths(mPosition[1]));
    VirtualCall<BodyIntFn, 21>(message, Hundredths(a));
    VirtualCall<BodyIntFn, 26>(message, Hundredths(b));
    void *server = GetCachedMessageServer();
    VirtualCall<void(__thiscall *)(void *, void *, uint32_t), 4>(server, message, 0);
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(message); // Release
}

void cTSWaterEffect::AddToScene(void *step, void *)
{
    if (!mRunning) return;
    const auto *d = static_cast<const cTSWaterDescription *>(mDescription);
    if (d->mFlags & 1) {
        if (mBody.Get()) VirtualCall<BodyFloatFn, 9>(mBody.Get(), std::bit_cast<float>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(step))));
        return;
    }
    if (d->mFloat10 == 0.0f) {
        Splash(d->mFloatC, d->mFloat8);
        mRunning = false;
        return;
    }
    const float dt = std::bit_cast<float>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(step)));
    mElapsed = fx::Add(dt, mElapsed);
    // ORIGINAL-BUG: a negative splash interval never brings mElapsed below it, so this loop splashes forever.
    if (mElapsed >= d->mFloat10) {
        do {
            Splash(d->mFloatC, d->mFloat8);
            d = static_cast<const cTSWaterDescription *>(mDescription);
            mElapsed = mElapsed - d->mFloat10;
        } while (mElapsed >= d->mFloat10);
    }
}

void cTSWaterEffect::Update(const fx::EffectsTransform *local, const fx::EffectsTransform *parent)
{
    fx::EffectPosition(local, parent, mPosition);
}
