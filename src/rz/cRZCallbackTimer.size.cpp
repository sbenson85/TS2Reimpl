// cRZCallbackTimer (lane LO, foundations F1; src/rz/cRZCallbackTimer.h). Size profile, as the original's.
#include "rz/cRZCallbackTimer.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZCriticalSection.h"

#include <new>
#include <set>

namespace {

// cIGZUnknown slots, and the callback service's (cRZCallbackThread's) and the framework's.
constexpr int kSlotAddRef = 1, kSlotRelease = 2;
constexpr int kServiceSlotAdd = 3, kServiceSlotRemove = 4;
constexpr int kFrameworkSlotGetService = 5, kSourceSlotGetCallbackService = 3;
// The timer's own slots, called through its vtable as the original does.
constexpr int kSlotStartRepeating = 5, kSlotStartOnce = 6, kSlotStop = 7;

using LiveSet = std::set<uint32_t>; // the game's std::set<uint32_t> (MSVC 14.40's layout; one process heap)

LiveSet &Live() { return *GameAddress<LiveSet>(cRZCallbackTimer::kLiveSetRva); }
cRZCriticalSection &LiveLock() { return *GameAddress<cRZCriticalSection>(cRZCallbackTimer::kLiveLockRva); }
void *DefaultService() { return GameAddress<void>(cRZCallbackTimer::kDefaultServiceRva); }

uint32_t Key(const cRZCallbackTimer *timer) { return uint32_t(uintptr_t(timer)); }
void AddRef(void *object) { VirtualCall<uint32_t(__thiscall *)(void *), kSlotAddRef>(object); }
void Release(void *object) { VirtualCall<uint32_t(__thiscall *)(void *), kSlotRelease>(object); }

uint32_t ServiceAdd(cRZCallbackTimer *self, uint32_t delay, bool once)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, cRZCallbackTimer::Callback, void *, uint32_t, uint32_t, uint32_t),
                       kServiceSlotAdd>(self->mService, &cRZCallbackTimer::OnTimer, self, delay, once, 0);
}

// Slots 1 and 3: set the value; an active timer is stopped and started again the same way, keeping its holder.
template <uint32_t cRZCallbackTimer::*Field> bool SetAndRestart(cRZCallbackTimer *self, uint32_t value)
{
    void *holder = self->mHolder;
    if (holder) AddRef(holder); // the restart's Stop releases the timer's reference
    self->*Field = value;
    if (self->mActive) {
        VirtualCall<uint32_t(__thiscall *)(void *), kSlotStop>(self);
        if (self->mOnce)
            VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, void *), kSlotStartOnce>(self, 0, nullptr);
        else
            VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, void *), kSlotStartRepeating>(self, 0, nullptr);
    }
    if (holder) {
        if (self->mHolder) Release(holder);
        self->mHolder = holder;
    }
    return true;
}

} // namespace

cRZCallbackTimer *cRZCallbackTimer::Construct()
{
    mVTable = GameAddress(kVTable);
    mUseService = 1;
    mCallback = nullptr;
    mField10 = 100;
    mPeriod = 1000;
    mActive = 0;
    mOnce = 0;
    mId = 0;
    mService = nullptr;
    mHolder = nullptr;
    LiveLock().Lock();
    Live().insert(Key(this));
    LiveLock().Unlock();
    mService = DefaultService();
    mUseService = 0;
    SetUseService(true);
    return this;
}

void cRZCallbackTimer::Destruct()
{
    mVTable = GameAddress(kVTable);
    LiveLock().Lock();
    auto it = Live().find(Key(this));
    if (it != Live().end()) Live().erase(it);
    LiveLock().Unlock();
    if (mActive) Stop();
    if (mHolder) {
        Release(mHolder);
        mHolder = nullptr;
    }
    mCallback = nullptr;
    mData = nullptr;
    if (mService) {
        if (mUseService) Release(mService);
        mService = nullptr;
    }
}

cRZCallbackTimer *cRZCallbackTimer::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

bool cRZCallbackTimer::SetField10(uint32_t value) { return SetAndRestart<&cRZCallbackTimer::mField10>(this, value); }
bool cRZCallbackTimer::SetPeriod(uint32_t period) { return SetAndRestart<&cRZCallbackTimer::mPeriod>(this, period); }

bool cRZCallbackTimer::StartRepeating(uint32_t period, void *holder)
{
    if (mActive || !mCallback) return false;
    if (holder) {
        AddRef(holder);
        mHolder = holder;
    }
    if (period) mPeriod = period;
    mActive = 1;
    mId = ServiceAdd(this, mPeriod, false);
    if (mId) return true;
    mActive = 0;
    if (holder) Release(holder);
    mHolder = nullptr;
    return false;
}

bool cRZCallbackTimer::StartOnce(uint32_t delay, void *holder)
{
    if (mActive || !mCallback) return false;
    if (!delay) delay = mPeriod;
    if (holder) {
        AddRef(holder);
        mHolder = holder;
    }
    mActive = 1;
    mOnce = 1;
    mId = ServiceAdd(this, delay, true);
    if (mId) return true;
    mActive = 0;
    mOnce = 0;
    if (holder) Release(holder);
    mHolder = nullptr;
    return false;
}

bool cRZCallbackTimer::Stop()
{
    if (!mId) return true;
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), kServiceSlotRemove>(mService, mId);
    if (mHolder) {
        Release(mHolder);
        mHolder = nullptr;
    }
    mId = 0;
    mActive = 0;
    mOnce = 0;
    return true;
}

bool cRZCallbackTimer::SetCallback(Callback callback, void *data)
{
    mCallback = callback;
    mData = data;
    return true;
}

bool cRZCallbackTimer::SetUseService(bool use)
{
    if (mUseService == uint8_t(use)) return true;
    mUseService = use;
    if (!use) {
        // ORIGINAL-BUG: the framework's service is dropped without releasing the reference taken for it.
        mService = DefaultService();
        return true;
    }
    void *framework = GetServiceObject();
    void *source = nullptr;
    if (framework && (VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **), kFrameworkSlotGetService>(
                          framework, kServiceIid, kServiceClsid, &source) & 0xff)) {
        void *service = VirtualCall<void *(__thiscall *)(void *), kSourceSlotGetCallbackService>(source);
        if (mService != service) {
            if (mService) Release(mService);
            mService = service;
            if (service) AddRef(service);
        }
        Release(source);
        return true;
    }
    mUseService = 0;
    if (mService != DefaultService()) {
        if (mService) Release(mService);
        mService = DefaultService();
    }
    return false;
}

void __cdecl cRZCallbackTimer::OnTimer(void *timer)
{
    auto *self = static_cast<cRZCallbackTimer *>(timer);
    LiveLock().Lock();
    if (Live().find(Key(self)) == Live().end()) {
        LiveLock().Unlock();
        return;
    }
    const bool once = self->mOnce != 0;
    void *holder = self->mHolder;
    const Callback callback = self->mCallback;
    void *data = self->mData;
    if (holder) AddRef(holder); // kept alive across the call
    if (once) {
        if (self->mHolder) {
            Release(self->mHolder);
            self->mHolder = nullptr;
        }
        self->mId = 0;
        self->mActive = 0;
        self->mOnce = 0;
    }
    LiveLock().Unlock();
    if (!callback) return; // ORIGINAL-BUG: the reference taken on the holder above is never released
    callback(data);
    if (holder) Release(holder);
}
