#pragma once
#include <cstdint>

// cRZCallbackTimer (0x28 bytes, vtable 0xe61bb8, 13 slots; RVAs 1.18.0.187): calls a cdecl callback with its data once
// after a delay, or repeatedly at a period, through a callback service (a cRZCallbackThread: its slot 3 AddCallback,
// slot 4 RemoveCallback). The service is the framework's (asked for through slot 5 of GetServiceObject()) or, without
// one, the static callback thread at 0x10f59e8. Every live timer's address is in a game-global std::set<uint32_t>
// (0x10f59d8) under the lock at 0x10f59e0, so a callback that arrives after the timer died is dropped (OnTimer).
// Other groups embed the timer as a value member (cGZWinBtn, cGZWinScrollbar2, cTSEffectsManager) or allocate it
// (cGZSndSys), so it keeps the game's vtable and its slots are hooked at their RVAs (the hook shape).
//
// Slots not ours: 2 and 4 (getters of +0x10 and +0x14, folded with unrelated classes: 0x95ad0f, 0x95acf3), 8 (shared
// 0x156110) and 12 (shared 0x15e550). Slots 9 and 10 are both SetCallback.
class cRZCallbackTimer {
public:
    static constexpr uint32_t kVTable = 0xe61bb8;
    static constexpr uint32_t kSize = 0x28;
    static constexpr uint32_t kLiveSetRva = 0x10f59d8;     // std::set<uint32_t>: the live timers
    static constexpr uint32_t kLiveLockRva = 0x10f59e0;    // cRZCriticalSection guarding it
    static constexpr uint32_t kDefaultServiceRva = 0x10f59e8; // the static cRZCallbackThread
    static constexpr uint32_t kServiceIid = 0xa76c78e7, kServiceClsid = 0x276c784b; // the framework's slot 5 query
    using Callback = void(__cdecl *)(void *data);

    cRZCallbackTimer *Construct();                       // 0x9ccf32
    void Destruct();                                     // 0x9ccfa9
    cRZCallbackTimer *DeletingDestructor(uint32_t flags); // 0x9cd02c, slot 0
    bool SetField10(uint32_t value);                     // 0x9cd1f3, slot 1: restarts an active timer
    bool SetPeriod(uint32_t period);                     // 0x9cd19d, slot 3: restarts an active timer
    bool StartRepeating(uint32_t period, void *holder);  // 0x9cd2b7, slot 5: period 0 keeps the current one
    bool StartOnce(uint32_t delay, void *holder);        // 0x9cd249, slot 6: delay 0 uses the period (not stored)
    bool Stop();                                         // 0x9cd32b, slot 7
    bool SetCallback(Callback callback, void *data);     // 0x9cd18a, slots 9 and 10
    bool SetUseService(bool use);                        // 0x9cd04b, slot 11
    static void __cdecl OnTimer(void *timer);            // 0x9cd0ec: the callback the service calls

    const void *mVTable;  // 0x00
    uint8_t mUseService;  // 0x04 the service is the framework's (a reference held)
    uint8_t mPad05[3];
    Callback mCallback;   // 0x08
    void *mData;          // 0x0c
    uint32_t mField10;    // 0x10 100 at construction; only stored (slot 1) and read back (slot 2)
    uint32_t mPeriod;     // 0x14 1000 at construction: the repeat period and the default delay
    uint8_t mActive;      // 0x18
    uint8_t mOnce;        // 0x19
    uint8_t mPad1a[2];
    uint32_t mId;         // 0x1c the service's callback id, 0 when none
    void *mService;       // 0x20 cIGZUnknown-based callback service
    void *mHolder;        // 0x24 a cIGZUnknown kept alive (a reference) while the timer runs
};
static_assert(sizeof(cRZCallbackTimer) == cRZCallbackTimer::kSize, "cRZCallbackTimer size");
