#pragma once
// cTSAudioListener (lane KH): the audio listener service (class 0x6b8d3d09, interface 0x4b8d3d02, service id 0x2ba80390,
// priority 0). 0x120 bytes, /O1 (the module is built for size).
//
// Every tick it places the listener: the active object's cell (the app's slot 26, its slot 39) as a world position, kept
// inside the screen margins (the 0x10cc414..0x10cc420 percentages of the view's size, else the primary point), and moves
// two smoothed points towards their targets (the primary, +0x60, and the secondary, +0x8c; the step 0x98ba40). Then it
// sends the listener event (0x0b8d81d5) to the sound renderer and posts a message of that id. The tuning comes from a
// property-set resource (+0x110). The world update before that (0x211dce, with 0x211ece / 0x21253b, about 4 KB) is the
// original's.
//
// Faces (RTTI order): +0 cITSAudioListener (vtable 0xda8454, 14 slots), +4 cIGZSystemService (0xda8490), +8
// cIGZMessageTarget2 (0xda84c8), +0xc cITSCheatSystem::cCheatCommand (0xda84dc, 12 slots: the "lerp" / "slerp" cheat).
//
// Standalone shape (docs/standalone-shape.md): the factory 0x211065 (the audio COM director registers it for class
// 0x6b8d3d09) is hooked to Create; the constructor 0x210b4d is called only by it, and the vtables are referenced only by the
// constructor and the destructor 0x210d39 (reached only through the deleting destructor 0x210e12 and its +0xc thunk
// 0x210e0a), so every listener carries ours and the slots need no adapters.
#include "audio/cTSAudioAmbienceSystem.h"  // cICheatCommand
#include "audio/cTSAudioResourceManager.h" // AudioRef
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZSystemService.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZStopwatch.h"

#include <cstddef>
#include <cstdint>
#include <set>

// cITSAudioListener (iid 0x4b8d3d02). Slots 3 and 4 are also cIGZSystemService's Init / Shutdown (the +4 face's slots 8 / 9
// are the thunks 0x2113ca / 0x211d9b). The getters copy under the lock; the points are 3 floats.
class cITSAudioListener : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x4b8d3d02;
    virtual bool Init() = 0;                                  // 3 (0x2112ee)
    virtual bool Shutdown() = 0;                              // 4 (0x211d4d)
    virtual void GetVector54(float *out) = 0;                 // 5 (0x2111dc): +0x54
    virtual bool GetPrimary(float *out) = 0;                  // 6 (0x211132): the primary point
    virtual bool GetPrimaryAnd3c(float *point, float *v3c) = 0; // 7 (0x211183): the primary point, +0x3c
    virtual bool GetSecondary(float *out) = 0;                // 8 (0x211284): the secondary point
    virtual bool GetPoints(float *primary, float *secondary) = 0; // 9 (0x21121f)
    virtual bool GetVector30(float *out) = 0;                 // 10 (0x2110a0): +0x30
    virtual bool GetVector30And3c(float *v30, float *v3c) = 0; // 11 (0x2110e5)
    virtual int32_t GetWordD8() = 0;                          // 12 (0x211425)
    virtual uint32_t GetWordDC() = 0;                         // 13 (0x2113e7)
};

class cTSAudioListener : public cITSAudioListener,
                         public cIGZSystemService,
                         public cIGZMessageTarget2,
                         public cICheatCommand {
public:
    static constexpr uint32_t kSize = 0x120;
    static constexpr uint32_t kServiceId = 0x2ba80390;
    static constexpr uint32_t kMessageTargetIid = 0x452294aa;

    struct Vector3 {
        float x, y, z;
    };
    static_assert(sizeof(Vector3) == 12);

    // A point moved towards its target a step at a time (0x98ba40, `ret 4`: the milliseconds since the last tick).
    struct SmoothedPoint {
        uint32_t unknown0;  // +0x00 never written
        int32_t steps;      // +0x04 (tuning "...listenersteps", the cheat's steps)
        float minChangeSq;  // +0x08
        float minChange;    // +0x0c
        Vector3 current;    // +0x10
        Vector3 target;     // +0x1c
        uint8_t placed;     // +0x28 set once the point has had a target
        uint8_t unknown29[3]; // never written
    };
    static_assert(sizeof(SmoothedPoint) == 0x2c);

    // The critical section as a member: built (0x1af64e) in member order, destroyed (0x1af68a) in reverse member order.
    struct OwnedLock : cRZCriticalSection {
        OwnedLock() { Construct(); }
        ~OwnedLock() { Destruct(); }
        OwnedLock(const OwnedLock &) = delete;
        OwnedLock &operator=(const OwnedLock &) = delete;
    };

    cTSAudioListener();                    // 0x210b4d
    ~cTSAudioListener() noexcept override; // 0x210d39; deleting 0x210e12 (+0xc thunk 0x210e0a)
    cTSAudioListener(const cTSAudioListener &) = delete;
    cTSAudioListener &operator=(const cTSAudioListener &) = delete;

    static cTSAudioListener *__cdecl Create(); // 0x211065: new 0x120, the constructor

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x211865; thunks 0x2118a5 / 0x2118ad / 0x2118b5
    uint32_t AddRef() override;                              // 0x210e34; thunks 0x210e42 / 0x210e4a / 0x210e52
    uint32_t Release() override;                             // 0x211c9d; thunks 0x211cc1 / 0x211cc9 / 0x211cd1

    // ---- cITSAudioListener / cIGZSystemService ----
    bool Init() override;
    bool Shutdown() override;
    void GetVector54(float *out) override;
    bool GetPrimary(float *out) override;
    bool GetPrimaryAnd3c(float *point, float *v3c) override;
    bool GetSecondary(float *out) override;
    bool GetPoints(float *primary, float *secondary) override;
    bool GetVector30(float *out) override;
    bool GetVector30And3c(float *v30, float *v3c) override;
    int32_t GetWordD8() override;
    uint32_t GetWordDC() override;

    // cIGZSystemService (+4): the accessors are folded bodies (0x2112e0, 0x211d30, 0x2112e7, 0x2113e0, 0x211d40; OnIdle
    // the shared 0x148260)
    uint32_t GetServiceID() override { return mServiceId; }
    void SetServiceID(uint32_t id) override { mServiceId = id; }
    int32_t GetServicePriority() override { return mPriority; }
    bool IsServiceRunning() override { return *reinterpret_cast<const bool *>(&mRunning); } // the byte as is
    void SetServiceRunning(bool running) override { mRunning = running; }
    bool OnTick(uint32_t counter) override; // 0x211469
    bool OnIdle(uint32_t) override { return true; }
    int32_t GetServiceTickPriority() override { return mPriority; } // 0x2112e7 again

    // ---- cIGZMessageTarget2 (+8) ----
    bool DoMessage(void *message) override; // 0x210e60

    // ---- cICheatCommand (+0xc) ----
    void Slot3(uint32_t word, uint32_t unused) override;
    void Slot4(uint32_t parser) override;  // 0x210e9f: the "lerp" / "slerp" cheat (a nGZCommandParser)
    const char *GetName() override;        // 0x211463
    const char *GetHelp(uint32_t unused) override; // 0x1d0a53 (cCheatCommand's)
    bool Slot7() override;
    uint32_t Slot8() override;
    const char *GetDescription() override; // 0x210e5a
    uint32_t Slot11() override;

    // ---- members the original calls (non-virtual, __thiscall on the primary face) ----
    void LoadTuning();  // 0x2118bd: the tuning property set (the old one forgotten first) and the values from it
    void RequestSnap(); // 0x211cd9: the next tick puts both points on their targets

    cIGZSystemService *Service() { return this; }
    cIGZMessageTarget2 *MessageTarget() { return this; }

    // +0x00 .. +0x0f: the four faces
    uint32_t mCheatWord = 0;          // 0x10 the cheat command's word (slot 3 / slot 11)
    uint32_t mCheatRefs = 0;          // 0x14 the cheat command's own count (unused: our AddRef / Release override it)
    volatile int32_t mRefs = 0;       // 0x18
    AudioRef mRenderer;               // 0x1c the sound renderer (service 0x23cecba0, iid 0x106077d2), Init to Shutdown
    OwnedLock mLock;                  // 0x20 its slots 1 / 2 lock and unlock
    std::set<uint32_t> mMessageIds;   // 0x28
    Vector3 mVector30;                // 0x30 never written here (the event's 0x6d9e30f4)
    Vector3 mVector3c;                // 0x3c from 0xff394c (the event's 0x6bd87da2)
    Vector3 mVector48;                // 0x48 from 0xff3958 (the event's 0xabd87da5)
    Vector3 mVector54;                // 0x54 never written here
    SmoothedPoint mPrimary;           // 0x60 (the event's 0x8bd87d9f)
    SmoothedPoint mSecondary;         // 0x8c
    uint8_t mSecondaryPlaced;         // 0xb8 the secondary point started at the first world position
    uint8_t mSnap;                    // 0xb9 RequestSnap
    uint8_t mUnknownBA[6];            // 0xba never written
    cRZStopwatch mTickWatch;          // 0xc0 mode 4
    int32_t mWordD8 = -1;             // 0xd8
    uint32_t mWordDC = 0;             // 0xdc
    float mHeightScale;               // 0xe0 tuning "listenerheightscale" (0.25)
    float mRolloff;                   // 0xe4 tuning "rolloff" (1.8): the renderer's 0x5b30e7b3 face, slot 10
    float mMinHeight;                 // 0xe8 tuning "listenerminheight" (2)
    float mCameraOffset = 0;          // 0xec tuning "listenertocameraoffset" (0)
    uint8_t mDirty = 1;               // 0xf0 set by the load / lot messages
    uint8_t mUnknownF1[3];            // 0xf1 never written
    Vector3 mVectorF4;                // 0xf4 from 0x10cc430
    float mRotation[4] = {0, 0, 0, 1}; // 0x100
    AudioRef mTuning;                 // 0x110 the tuning property set (iid 0x4bb0d269)
    uint32_t mServiceId = kServiceId; // 0x114
    int32_t mPriority = 0;            // 0x118 (also the tick priority)
    uint8_t mRunning = 0;             // 0x11c
    uint8_t mUnknown11d[3];           // 0x11d never written
};

static_assert(sizeof(cTSAudioListener) == cTSAudioListener::kSize, "cTSAudioListener size");
static_assert(offsetof(cTSAudioListener, mCheatWord) == 0x10, "mCheatWord");
static_assert(offsetof(cTSAudioListener, mRefs) == 0x18, "mRefs");
static_assert(offsetof(cTSAudioListener, mRenderer) == 0x1c, "mRenderer");
static_assert(offsetof(cTSAudioListener, mLock) == 0x20, "mLock");
static_assert(offsetof(cTSAudioListener, mMessageIds) == 0x28, "mMessageIds");
static_assert(offsetof(cTSAudioListener, mVector30) == 0x30, "mVector30");
static_assert(offsetof(cTSAudioListener, mVector54) == 0x54, "mVector54");
static_assert(offsetof(cTSAudioListener, mPrimary) == 0x60, "mPrimary");
static_assert(offsetof(cTSAudioListener, mSecondary) == 0x8c, "mSecondary");
static_assert(offsetof(cTSAudioListener, mSecondaryPlaced) == 0xb8, "mSecondaryPlaced");
static_assert(offsetof(cTSAudioListener, mTickWatch) == 0xc0, "mTickWatch");
static_assert(offsetof(cTSAudioListener, mWordD8) == 0xd8, "mWordD8");
static_assert(offsetof(cTSAudioListener, mHeightScale) == 0xe0, "mHeightScale");
static_assert(offsetof(cTSAudioListener, mDirty) == 0xf0, "mDirty");
static_assert(offsetof(cTSAudioListener, mVectorF4) == 0xf4, "mVectorF4");
static_assert(offsetof(cTSAudioListener, mRotation) == 0x100, "mRotation");
static_assert(offsetof(cTSAudioListener, mTuning) == 0x110, "mTuning");
static_assert(offsetof(cTSAudioListener, mServiceId) == 0x114, "mServiceId");
static_assert(offsetof(cTSAudioListener, mRunning) == 0x11c, "mRunning");

// The hooks' entry points.
struct cTSAudioListenerAdapters {
    static cTSAudioListener *__fastcall Construct(void *memory, void *edx); // 0x210b4d (tests; not hooked)
};
