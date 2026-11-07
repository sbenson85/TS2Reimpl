#pragma once
// cGZSndSys (lane KK): the engine's sound system service, the object the sound probe (0x9c5b7e) hands out (class
// 0xe41ce9fd, service id 0x23cecba0, iid 0x106077d2, priority 500000). 0x150 bytes, /O1 (the module is built for size).
//
// It routes sound events (messages of type 0xdb53383b whose event interface 0x5b523ba4 carries a cIGZMessage2Standard):
// slots 5-7 build one and dispatch it at once to the event handlers (slots 15 / 16), slots 9-12 queue it on the message
// queue the update drains. It drives two lists of update targets (slot 30: period 20 ms, or anything else for the 250 ms
// list, timed by the stopwatch at +0x48), keeps the renderers (slots 35 / 36) and broadcasts the master level and the 3D
// listener (the +0xc and +0x10 faces) to them, and holds named values (slots 23-27, names lowercased and trimmed, a
// leading '.' dropped). A callback timer (from Init) calls the timer tick every 20 ms, which updates with the time since
// the last tick. Walks raise a busy count; what is added or removed during a walk waits in a pending list until the
// walk ends.
//
// Faces (RTTI order): +0 cIGZSndSys (vtable 0xe80f60, 55 slots), +4 cIGZSystemService (0xe81040), +8 cIGZMessageTarget2
// (0xe81078), +0xc cIGZSnd3DListener (0xe8108c), +0x10 cIGZSnd3DListenerEx (0xe810bc).
//
// Standalone shape (docs/standalone-shape.md): the factory 0xb22378 (the sound COM director registers it for class
// 0xe41ce9fd) is hooked to Create; the constructor 0xb22fd5 is reached only from it, and the vtables are referenced only by
// the constructor and the destructor 0xb231cd (reached only through the deleting destructor 0xb232e1, primary slot 53),
// so every system carries ours and the slots need no adapters.
#include "audio/AudioInterfaces.h"          // cIGZSndEventHandler, cIGZSndUpdateTarget
#include "audio/cTSAudioResourceManager.h"  // AudioRef, AudioResourceString
#include "gz/cGZSndEaRenderer.h"            // cIGZSndRenderer, cIGZSnd3DListener, cIGZSnd3DListenerEx
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZSystemService.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>

class cIGZString;

// cIGZSndSys (iid 0x106077d2): slots 3-54. Event "data" words go to the event's cIGZMessage2Standard data slots 1-4.
class cIGZSndSys : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x106077d2;
    using UpdateTargetFn = bool(__cdecl *)(cIGZUnknown *target, uint32_t a, uint32_t b);

    virtual cIGZSnd3DListener *AsListener() = 0;                         // 3 (folded 0x6e70ed: +0xc, null kept)
    virtual cIGZSnd3DListenerEx *AsListenerEx() = 0;                     // 4 (folded 0x6f5105: +0x10, null kept)
    virtual bool SendEventText(uint32_t type, const cIGZString *text, uint32_t unused1, uint32_t unused2, uint32_t unused3,
                               uint32_t unused4) = 0;                    // 5 (0xb234b4)
    virtual bool SendEventObject(uint32_t type, cIGZUnknown *object, uint32_t d1, uint32_t d2, uint32_t d3,
                                 uint32_t d4) = 0;                       // 6 (0xb23525)
    virtual bool SendEvent(uint32_t type, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) = 0; // 7 (0xb2340d)
    virtual bool DispatchEvent(cIGZUnknown *event) = 0;                  // 8 (0xb235e1): to the handlers in turn
    virtual bool PostEventText(uint32_t type, const cIGZString *text, uint32_t unused1, uint32_t unused2, uint32_t unused3,
                               uint32_t unused4) = 0;                    // 9 (0xb240a9)
    virtual bool PostEventObject(uint32_t type, cIGZUnknown *object, uint32_t d1, uint32_t d2, uint32_t d3,
                                 uint32_t d4) = 0;                       // 10 (0xb24107)
    virtual bool PostEvent(uint32_t type, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) = 0; // 11 (0xb24015)
    virtual bool QueueEvent(cIGZUnknown *event) = 0;                     // 12 (0xb241b0): its data on the queue
    virtual bool CreateEvent(uint32_t type, uint32_t iid, void **out, uint32_t d1, uint32_t d2, uint32_t d3,
                             uint32_t d4) = 0;                           // 13 (0xb23846)
    virtual bool CreateEmptyEvent(uint32_t iid, void **out) = 0;         // 14 (0xb2389f): the event's +0x2c face asked
    virtual bool AddEventHandler(cIGZSndEventHandler *handler) = 0;      // 15 (0xb2427d): at the front
    virtual bool RemoveEventHandler(cIGZSndEventHandler *handler) = 0;   // 16 (0xb24ed7)
    virtual bool Update(uint32_t elapsed) = 0;                           // 17 (0xb2506e)
    virtual bool IsRunning() = 0;                                        // 18 (folded 0x954fb5: +0x45)
    virtual void SetName(const cIGZString &name) = 0;                    // 19 (0xb24678)
    virtual cIGZString *GetName() = 0;                                   // 20 (folded 0x158f90: +0x18)
    virtual bool IsEnabled() = 0;                                        // 21 (folded 0x2296b0: +0x44)
    virtual void SetEnabled(bool enabled) = 0;                           // 22 (folded 0x229f40)
    virtual bool SetNamedValue(const cIGZString *name, uint32_t value) = 0;   // 23 (0xb247b5)
    virtual bool GetNamedValue(const cIGZString *name, uint32_t *out) = 0;    // 24 (0xb23a10)
    virtual uint32_t GetNamedValueCount() = 0;                           // 25 (folded 0x249080: +0xec)
    virtual bool GetNamedValueAt(uint32_t index, uint32_t *out) = 0;     // 26 (0xb23ab4)
    virtual bool GetNameAfter(uint32_t skip, cIGZString *name) = 0;      // 27 (0xb23af8)
    virtual int32_t GetLevel() = 0;                                      // 28 (folded 0x230133: +0x10c)
    virtual bool ApplyLevel(uint32_t unused) = 0;                        // 29 (0xb24865): to every renderer
    virtual bool AddUpdateTarget(cIGZSndUpdateTarget *target, uint32_t period) = 0; // 30 (0xb24379): at the front
    virtual bool RemoveUpdateTarget(cIGZSndUpdateTarget *target) = 0;    // 31 (0xb24fc0)
    virtual bool ForEachUpdateTarget(UpdateTargetFn fn, uint32_t a, uint32_t b) = 0; // 32 (0xb237b5)
    virtual bool EnableUpdates() = 0;                                    // 33 (0xb23766)
    virtual bool DisableUpdates() = 0;                                   // 34 (0xb23406)
    virtual bool AddRenderer(cIGZSndRenderer *renderer) = 0;             // 35 (0xb24307): at the back, once
    virtual bool RemoveRenderer(cIGZSndRenderer *renderer) = 0;          // 36 (0xb24f5d)
    virtual bool HasRenderer(cIGZSndRenderer *renderer) = 0;             // 37 (0xb23e70)
    virtual uint32_t GetRendererCount() = 0;                             // 38 (folded 0x24ad70: +0xb0)
    virtual bool QuerySound(uint32_t id, uint32_t iid, void **out) = 0;  // 39 (0xb238ee)
    virtual bool HasSound(uint32_t id) = 0;                              // 40 (0xb23eb5)
    virtual cRZCriticalSection *GetLock() = 0;                           // 41 (folded 0x6e7c71: +0x68)
    virtual uint32_t GetTime() = 0;                                      // 42 (0xb23a08): the uptime's Elapsed (unchecked)
    virtual cIGZUnknown *GetObject104() = 0;                             // 43 (folded 0x418aa0)
    virtual bool GetObject104Ref(cIGZUnknown **out) = 0;                    // 44 (0xb23c31): counted
    virtual cIGZUnknown *GetObject108() = 0;                             // 45 (folded 0x418b60)
    virtual bool GetObject108Ref(cIGZUnknown **out) = 0;                    // 46 (0xb23c05): counted
    virtual bool HoldUntilDispatched(cIGZUnknown *object) = 0;           // 47 (0xb2331b)
    virtual void StopAllSounds() = 0;                                    // 48 (0xb23edc)
    virtual void SetFlag14d(bool value) = 0;                             // 49 (0xb24b35)
    virtual bool GetFlag14d() = 0;                                       // 50 (0xb23c73)
    virtual uint32_t GetTimerThreadSlot7() = 0;                          // 51 (0xb23c5d)
    virtual uint32_t TimerSlot3(uint32_t value) = 0;                     // 52 (0xb24b42)
    virtual ~cIGZSndSys() = default;                                     // 53 (deleting 0xb232e1)
    virtual void SetRunning(bool running) = 0;                           // 54 (0xb2485b)
};

// A cRZString member or local (the game's cRZString, built and destroyed by hand as the original's inlined code does).
struct SndSysString {
    union {
        cRZString s;
    };
    SndSysString() { s.Construct(); }
    explicit SndSysString(const char *text) { s.Construct(text); }
    explicit SndSysString(const cIGZString &other) { s.Construct(other); }
    // The map's key copies: cRZString(const cRZString &, 0, npos) (0x141eb3), as the node constructor 0xb22eef.
    SndSysString(const SndSysString &other) { s.Construct(other.s, 0, 0xffffffffu); }
    SndSysString &operator=(const SndSysString &) = delete;
    ~SndSysString() { s.Destruct(); }
};
static_assert(sizeof(SndSysString) == 0x20, "cRZString");

// The named values' order: the std::strings' operator< (0x144a19), as the finds 0x37eeb0 / 0x97dd14.
struct SndSysNameLess {
    using is_transparent = void;
    bool operator()(const SndSysString &a, const SndSysString &b) const { return a.s.mString < b.s.mString; }
    bool operator()(const SndSysString &a, const cRZString &b) const { return a.s.mString < b.mString; }
    bool operator()(const cRZString &a, const SndSysString &b) const { return a.mString < b.s.mString; }
};

class cGZSndSys : public cIGZSndSys,
                  public cIGZSystemService,
                  public cIGZMessageTarget2,
                  public cIGZSnd3DListener,
                  public cIGZSnd3DListenerEx {
public:
    static constexpr uint32_t kSize = 0x150;
    static constexpr uint32_t kServiceId = 0x23cecba0;
    static constexpr int32_t kPriority = 500000;

    // MSVC 14.40 containers as in the game: the lists hold counted objects (nodes 0x0c), the named values are a map from a
    // cRZString to a word (nodes 0x34: the key at +0x10, the value at +0x30).
    using RefList = std::list<AudioRef>;
    using NameMap = std::map<SndSysString, uint32_t, SndSysNameLess>;

    struct OwnedLock : cRZCriticalSection {
        OwnedLock() { Construct(); }
        ~OwnedLock() { Destruct(); }
        OwnedLock(const OwnedLock &) = delete;
        OwnedLock &operator=(const OwnedLock &) = delete;
    };
    struct OwnedStopwatch : cRZStopwatch {
        OwnedStopwatch() { Construct(4); } // mode 4 (milliseconds); no destructor work (0x140fe0)
        OwnedStopwatch(const OwnedStopwatch &) = delete;
        OwnedStopwatch &operator=(const OwnedStopwatch &) = delete;
    };

    cGZSndSys();                    // 0xb22fd5
    ~cGZSndSys() noexcept override; // 0xb231cd; deleting 0xb232e1 (primary slot 53)
    cGZSndSys(const cGZSndSys &) = delete;
    cGZSndSys &operator=(const cGZSndSys &) = delete;

    static cGZSndSys *__cdecl Create(); // 0xb22378: new 0x150, the constructor

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xb241e1; thunks 0xb2425d / 0xb24265 / 0xb24275 / 0xb2426d
    uint32_t AddRef() override;                              // folded 0x2296a0; thunks 0x333def / 0xb23303 / 0xb23313 / 0xb2330b
    uint32_t Release() override;                             // 0xb24435; thunks 0xb2444f / 0xb24457 / 0xb24467 / 0xb2445f

    // ---- cIGZSndSys (+0) ----
    cIGZSnd3DListener *AsListener() override;
    cIGZSnd3DListenerEx *AsListenerEx() override; // also the listener's slot 10 (thunk 0xb23388)
    bool SendEventText(uint32_t type, const cIGZString *text, uint32_t, uint32_t, uint32_t, uint32_t) override;
    bool SendEventObject(uint32_t type, cIGZUnknown *object, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) override;
    bool SendEvent(uint32_t type, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) override;
    bool DispatchEvent(cIGZUnknown *event) override;
    bool PostEventText(uint32_t type, const cIGZString *text, uint32_t, uint32_t, uint32_t, uint32_t) override;
    bool PostEventObject(uint32_t type, cIGZUnknown *object, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) override;
    bool PostEvent(uint32_t type, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) override;
    bool QueueEvent(cIGZUnknown *event) override;
    bool CreateEvent(uint32_t type, uint32_t iid, void **out, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) override;
    bool CreateEmptyEvent(uint32_t iid, void **out) override;
    bool AddEventHandler(cIGZSndEventHandler *handler) override;
    bool RemoveEventHandler(cIGZSndEventHandler *handler) override;
    bool Update(uint32_t elapsed) override;
    bool IsRunning() override { return mRunning; }
    void SetName(const cIGZString &name) override;
    cIGZString *GetName() override { return mName.s.AsInterface(); }
    bool IsEnabled() override { return mEnabled; }
    void SetEnabled(bool enabled) override { mEnabled = enabled; }
    bool SetNamedValue(const cIGZString *name, uint32_t value) override;
    bool GetNamedValue(const cIGZString *name, uint32_t *out) override;
    uint32_t GetNamedValueCount() override { return static_cast<uint32_t>(mNamedValues.size()); }
    bool GetNamedValueAt(uint32_t index, uint32_t *out) override;
    bool GetNameAfter(uint32_t skip, cIGZString *name) override;
    int32_t GetLevel() override { return mLevel; }
    bool ApplyLevel(uint32_t unused) override;
    bool AddUpdateTarget(cIGZSndUpdateTarget *target, uint32_t period) override;
    bool RemoveUpdateTarget(cIGZSndUpdateTarget *target) override;
    bool ForEachUpdateTarget(UpdateTargetFn fn, uint32_t a, uint32_t b) override;
    bool EnableUpdates() override;
    bool DisableUpdates() override;
    bool AddRenderer(cIGZSndRenderer *renderer) override;
    bool RemoveRenderer(cIGZSndRenderer *renderer) override;
    bool HasRenderer(cIGZSndRenderer *renderer) override;
    uint32_t GetRendererCount() override { return static_cast<uint32_t>(mRenderers.size()); }
    bool QuerySound(uint32_t id, uint32_t iid, void **out) override;
    bool HasSound(uint32_t id) override;
    cRZCriticalSection *GetLock() override { return &mLock; }
    uint32_t GetTime() override;
    cIGZUnknown *GetObject104() override { return mObject104.Get(); }
    bool GetObject104Ref(cIGZUnknown **out) override;
    cIGZUnknown *GetObject108() override { return mObject108.Get(); }
    bool GetObject108Ref(cIGZUnknown **out) override;
    bool HoldUntilDispatched(cIGZUnknown *object) override;
    void StopAllSounds() override;
    void SetFlag14d(bool value) override;
    bool GetFlag14d() override;
    uint32_t GetTimerThreadSlot7() override;
    uint32_t TimerSlot3(uint32_t value) override;
    void SetRunning(bool running) override;

    // ---- cIGZSystemService (+4) ----
    uint32_t GetServiceID() override { return mServiceId; }               // folded 0x1674b0
    void SetServiceID(uint32_t id) override { mServiceId = id; }          // folded 0x20c6f0
    int32_t GetServicePriority() override;                                 // 0xb23bff
    bool IsServiceRunning() override { return true; }                      // folded 0x144910
    void SetServiceRunning(bool) override {}                               // folded 0x300a0
    bool Init() override;                                                  // 0xb23ca1 (listener slot 3: thunk 0xb23e68)
    bool Shutdown() override;                                              // 0xb24c1a (listener slot 4: thunk 0xb24ecf)
    bool OnTick(uint32_t) override { return false; }                       // folded 0x18a7a0
    bool OnIdle(uint32_t) override { return true; }                        // folded 0x148260
    int32_t GetServiceTickPriority() override;                             // 0xb23bff (the same body as slot 5)

    // ---- cIGZMessageTarget2 (+8) ----
    bool DoMessage(void *message) override; // 0xb236f3

    // ---- cIGZSnd3DListener (+0xc) ----
    void ListenerSlot5() override;                                                        // 0xb23390
    void GetPosition(float *x, float *y, float *z) override;                              // 0xb23bc9
    bool SetPosition(float x, float y, float z) override;                                 // 0xb249d9
    void GetOrientation(float *topX, float *topY, float *topZ, float *frontX, float *frontY,
                        float *frontZ) override;                                          // 0xb23b7c
    bool SetOrientation(float topX, float topY, float topZ, float frontX, float frontY,
                        float frontZ) override;                                           // 0xb248ba

    // ---- cIGZSnd3DListenerEx (+0x10) ----
    void GetVector70(float *x, float *y, float *z) override;                              // 0xb23c7a
    bool SetVector70(float x, float y, float z) override;                                 // 0xb24b51
    void GetAll(float *px, float *py, float *pz, float *vx, float *vy, float *vz, float *tx, float *ty, float *tz,
                float *fx, float *fy, float *fz) override;                                // 0xb2395b
    bool SetAll(float px, float py, float pz, float vx, float vy, float vz, float tx, float ty, float tz, float fx,
                float fy, float fz) override;                                             // 0xb2446f
    void GetValueAc(float *out) override;                                                 // 0xb239f9
    bool SetValueAc(float value) override;                                                // 0xb24722
    void GetFactorLength(float *out) override;                                            // 0xb23bf0
    bool SetAllFactors(float value) override;                                             // 0xb24aa2
    void GetValueB0(float *out) override;                                                 // 0xb239ea
    bool SetValueB0(float value) override;                                                // 0xb2468f

    // ---- non-virtual ----
    // 0xb250f0 (thiscall, ret 0xc): unless updates are off, fn on the 20 ms targets, and on the 250 ms ones when the clock
    // passed 250 ms (then restarted); then the pending removals and additions. Always true.
    bool CallUpdateTargets(UpdateTargetFn fn, uint32_t a, uint32_t b);
    // 0xb250e0 (cdecl): the update's callback, target->Update(elapsed); always true.
    static bool __cdecl UpdateOne(cIGZUnknown *target, uint32_t elapsed, uint32_t unused);
    // 0xb2376d (cdecl): QuerySound's callback; stops (false) at the target whose sound (0x206077d2) has the id, writing it.
    static bool __cdecl FindSound(cIGZUnknown *target, uint32_t id, uint32_t out);
    // 0xb23923 (cdecl): the callback timer's tick: Update with the time since the last tick (game global 0x10fad88).
    static void __cdecl TimerTick(cGZSndSys *self);

    // +0x00 .. +0x13: the five faces
    bool mNameSet = false;               // 0x14 SetName ran
    uint8_t mUnused15[3];                // 0x15 never written
    SndSysString mName;                  // 0x18 slots 19 / 20
    AudioRef mQueue;                     // 0x38 the message queue (0xc66b0d, 0x20 bytes), from Init
    uint32_t mRefCount = 0;              // 0x3c
    uint32_t mServiceId = kServiceId;    // 0x40
    bool mEnabled = true;                // 0x44 slots 21 / 22: Init builds the queue and timer only when set
    bool mRunning = false;               // 0x45 slots 18 / 54: the timer started; Update does nothing until then
    uint8_t mUnused46[2];                // 0x46 never written
    OwnedStopwatch mClock;               // 0x48 the 250 ms list's clock (started by Init)
    cRZStopwatch *mUptime = nullptr;     // 0x60 a heap stopwatch Init starts and Shutdown frees (GetTime's clock)
    void *mTimer = nullptr;              // 0x64 the cRZCallbackTimer (0x9ccf32, 0x28 bytes), from Init
    OwnedLock mLock;                     // 0x68 slot 41
    bool mUpdatesEnabled = true;         // 0x70 slots 33 / 34
    uint8_t mUnused71[3];                // 0x71 never written
    RefList mFastTargets;                // 0x74 update targets every 20 ms
    RefList mSlowTargets;                // 0x7c update targets every 250 ms
    RefList mPendingFast;                // 0x84 added during a walk
    RefList mPendingSlow;                // 0x8c
    RefList mPendingTargetRemovals;      // 0x94 removed during a walk
    volatile long mTargetsBusy = 0;      // 0x9c walks of the targets
    volatile long mTargetsFlushing = 0;  // 0xa0 raised while the pending lists are applied
    OwnedLock mTargetsLock;              // 0xa4
    RefList mRenderers;                  // 0xac
    volatile long mRenderersBusy = 0;    // 0xb4
    OwnedLock mRenderersLock;            // 0xb8
    RefList mHandlers;                   // 0xc0 event handlers
    RefList mPendingHandlers;            // 0xc8
    RefList mPendingHandlerRemovals;     // 0xd0
    volatile long mHandlersBusy = 0;     // 0xd8
    volatile long mHandlersFlushing = 0; // 0xdc
    OwnedLock mHandlersLock;             // 0xe0
    NameMap mNamedValues;                // 0xe8
    RefList mHeld;                       // 0xf0 released after each dispatch
    volatile long mHeldBusy = 0;         // 0xf8
    OwnedLock mHeldLock;                 // 0xfc
    AudioRef mObject104;                 // 0x104 the director's class 0x9d4c6b95 (iid 0x5d4c6b90), from Init
    AudioRef mObject108;                 // 0x108 the director's class 0xbdf613b4 (iid 0x3df613a1), from Init
    int32_t mLevel = 0x400;              // 0x10c slot 28; ApplyLevel hands it to the renderers
    float mPosition[3] = {};             // 0x110 the listener (+0xc face)
    float mOrientation[6] = {0, 1, 0, 0, 0, 0}; // 0x11c top x, y, z, front x, y, z
    float mVector70[3] = {};             // 0x134 the listener-ex (+0x10 face)
    float mValueAc = 0;                  // 0x140
    float mFactor = 1;                   // 0x144
    float mValueB0 = 0;                  // 0x148
    bool mShuttingDown = false;          // 0x14c set by Shutdown; RemoveUpdateTarget then does nothing
    bool mFlag14d = false;               // 0x14d slots 49 / 50
    uint8_t mUnused14e[2];               // 0x14e never written
};

static_assert(sizeof(cGZSndSys) == cGZSndSys::kSize, "cGZSndSys size");
static_assert(offsetof(cGZSndSys, mNameSet) == 0x14, "mNameSet");
static_assert(offsetof(cGZSndSys, mName) == 0x18, "mName");
static_assert(offsetof(cGZSndSys, mQueue) == 0x38, "mQueue");
static_assert(offsetof(cGZSndSys, mRefCount) == 0x3c, "mRefCount");
static_assert(offsetof(cGZSndSys, mEnabled) == 0x44, "mEnabled");
static_assert(offsetof(cGZSndSys, mClock) == 0x48, "mClock");
static_assert(offsetof(cGZSndSys, mUptime) == 0x60, "mUptime");
static_assert(offsetof(cGZSndSys, mLock) == 0x68, "mLock");
static_assert(offsetof(cGZSndSys, mUpdatesEnabled) == 0x70, "mUpdatesEnabled");
static_assert(offsetof(cGZSndSys, mFastTargets) == 0x74, "mFastTargets");
static_assert(offsetof(cGZSndSys, mPendingTargetRemovals) == 0x94, "mPendingTargetRemovals");
static_assert(offsetof(cGZSndSys, mTargetsBusy) == 0x9c, "mTargetsBusy");
static_assert(offsetof(cGZSndSys, mTargetsLock) == 0xa4, "mTargetsLock");
static_assert(offsetof(cGZSndSys, mRenderers) == 0xac, "mRenderers");
static_assert(offsetof(cGZSndSys, mRenderersLock) == 0xb8, "mRenderersLock");
static_assert(offsetof(cGZSndSys, mHandlers) == 0xc0, "mHandlers");
static_assert(offsetof(cGZSndSys, mHandlersBusy) == 0xd8, "mHandlersBusy");
static_assert(offsetof(cGZSndSys, mHandlersLock) == 0xe0, "mHandlersLock");
static_assert(offsetof(cGZSndSys, mNamedValues) == 0xe8, "mNamedValues");
static_assert(offsetof(cGZSndSys, mHeld) == 0xf0, "mHeld");
static_assert(offsetof(cGZSndSys, mHeldLock) == 0xfc, "mHeldLock");
static_assert(offsetof(cGZSndSys, mObject104) == 0x104, "mObject104");
static_assert(offsetof(cGZSndSys, mLevel) == 0x10c, "mLevel");
static_assert(offsetof(cGZSndSys, mPosition) == 0x110, "mPosition");
static_assert(offsetof(cGZSndSys, mOrientation) == 0x11c, "mOrientation");
static_assert(offsetof(cGZSndSys, mVector70) == 0x134, "mVector70");
static_assert(offsetof(cGZSndSys, mValueB0) == 0x148, "mValueB0");
static_assert(offsetof(cGZSndSys, mShuttingDown) == 0x14c, "mShuttingDown");

// The hooks' entry points.
struct cGZSndSysAdapters {
    static cGZSndSys *__fastcall Construct(void *memory, void *edx); // 0xb22fd5 (tests; not hooked)
};
