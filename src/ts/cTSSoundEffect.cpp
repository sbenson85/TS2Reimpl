#include "ts/cTSSoundEffect.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "rz/StaticGetters.h"
#include "ts/EffectsPosition.h"
#include "ts/cTSCompDescription.h"

#include <bit>
#include <new>

using fx::Add;
using fx::EffectsTransform;

namespace {

constexpr uint32_t kMessageClass = 0xcbc79579; // the director's message to the sound renderer
constexpr uint32_t kMessageIid = 0x0bc7957d;

// The message object's slots (named from their uses here).
using SetTypeFn = void(__thiscall *)(void *, uint32_t);
using SetWordFn = void(__thiscall *)(void *, uint32_t, uint32_t);
using SetFloatFn = void(__thiscall *)(void *, uint32_t, float);
using SetVectorFn = void(__thiscall *)(void *, uint32_t, const float *);
using SendFn = void(__thiscall *)(void *, void *);

void SetType(void *m, uint32_t type) { VirtualCall<SetTypeFn, 5>(m, type); }
void SetWord(void *m, uint32_t key, uint32_t value) { VirtualCall<SetWordFn, 12>(m, key, value); }
void SetFloat(void *m, uint32_t key, float value) { VirtualCall<SetFloatFn, 9>(m, key, value); }
void SetVector(void *m, uint32_t key, const float *value) { VirtualCall<SetVectorFn, 18>(m, key, value); }

// A message made by the director framework, null when it can't (the holder drops its reference when it goes).
struct Message {
    fx::CountedRef<cIGZUnknown> mRef;
    bool Make()
    {
        auto *com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
        mRef.Reset();
        return uint8_t(com->GetClassObject(kMessageClass, kMessageIid, reinterpret_cast<void **>(mRef.Address()))) != 0;
    }
    void *Get() const { return mRef.Get(); }
};

// Keys of the message's words.
constexpr uint32_t kKeySound = 0x0bd63077;
constexpr uint32_t kKeyHandle = 0x6bd63081;
constexpr uint32_t kKeyOwner = 0xac5b5b4f;
constexpr uint32_t kKeyMode = 0x8bd63087;
constexpr uint32_t kKeyFlag = 0x8bd63096;
constexpr uint32_t kKeyVolume = 0x0bd63084;
constexpr uint32_t kKeyPosition = 0x6bd63530;

} // namespace

cTSSoundEffect::cTSSoundEffect(const cTSSoundDescription *description)
    : mDescription(description), mRunning(false), mHandle(0), mElapsed(0), mNextRepeat(0)
{
}

cITSComponentEffect *__cdecl cTSSoundEffect::Create(const cTSSoundDescription *description)
{
    return new (std::nothrow) cTSSoundEffect(description);
}

void cTSSoundEffect::Start(uint32_t)
{
    if (mRunning) return;
    mService.Assign(static_cast<cIGZUnknown *>(OriginalFunction<void *(__cdecl *)(), 0x9c5b7e>()()));
    mElapsed = 0;
    mNextRepeat = 0;
    mHandle = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    if (mService.Get()) {
        Message message;
        if (message.Make()) {
            SetType(message.Get(), 0xea451903);
            SetWord(message.Get(), kKeySound, mDescription->mSound);
            SetWord(message.Get(), kKeyHandle, mHandle);
            SetWord(message.Get(), kKeyOwner, 0);
            SetWord(message.Get(), kKeyMode, 0);
            SetWord(message.Get(), kKeyFlag, mDescription->mFlags & 1);
            if ((mDescription->mFlags >> 1) & 1) SetFloat(message.Get(), kKeyVolume, mDescription->mFloat10);
            VirtualCall<SendFn, 12>(mService.Get(), message.Get());
        }
    }
    mRunning = true;
}

void cTSSoundEffect::Stop(uint32_t how)
{
    if (!mRunning) return;
    mRunning = false;
    if (!mService.Get()) return;
    Message message;
    if (message.Make()) {
        SetType(message.Get(), 0x6a451908);
        SetWord(message.Get(), kKeyHandle, mHandle);
        SetWord(message.Get(), kKeyFlag, how == 1);
        VirtualCall<SendFn, 12>(mService.Get(), message.Get());
    }
    mService.Reset();
}

void cTSSoundEffect::AddToScene(void *step, void *)
{
    const float elapsed = Add(std::bit_cast<float>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(step))), mElapsed);
    mElapsed = elapsed;
    const float period = mDescription->mFloat8;
    if (period > 0.0f && elapsed > mNextRepeat) {
        mNextRepeat = Add(period, elapsed);
        if (mService.Get()) {
            Message message;
            if (message.Make()) {
                SetType(message.Get(), 0xca45190f);
                SetWord(message.Get(), kKeyHandle, mHandle);
                SetVector(message.Get(), kKeyPosition, mPosition);
                VirtualCall<SendFn, 12>(mService.Get(), message.Get());
            }
        }
    }
    const float duration = mDescription->mFloatC;
    if (duration > 0.0f && duration < mElapsed) Stop(1);
}

void cTSSoundEffect::Update(const EffectsTransform *local, const EffectsTransform *parent)
{
    fx::EffectPosition(local, parent, mPosition);
}
