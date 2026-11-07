#pragma once
// cTSAudioTSObjectUpdateDemon (lane LD): the service that keeps the sounds objects play placed in the world (class
// 0x8d5812a4 in the audio COM director's table, interface 0x4d7fd97e, service id 0x4d58135d, priority -0x2dc6c0; on the
// framework's tick list, not its service list). 0x68 bytes, /O1 (the module is built for size). RVAs 1.18.0.187.
//
// It listens to the audio events (DoMessage): a sound event (0xcc773dd7 / 0xaedbce8b) names an object (key 0xac5b5b4f, the
// object id word), a sound (0x6bd63081) and a volume (0xec488a56). For each object that plays sounds it keeps a monitor
// (tMonitorEntry: the object, its model, its place and the interval it is updated at) and for each sound a playing-sound
// record (tPlayingSound) tied to its object's monitor. Every tick it updates every monitor's placement, then steps the
// monitors round-robin for 500 units of its stopwatch, then updates every playing sound (the event it sends the sound
// renderer when the sound's place changed). Messages 0xb2 / 0xb5 drop everything.
//
// Faces (RTTI order): +0 cITSAudioTSObjectUpdateDemon (vtable 0xdaa970, 5 slots), +4 cIGZMessageTarget2 (0xdaa988), +8
// cRZSystemService / cIGZSystemService (0xdaa99c), +0xc cRZUnknownMT (0xdaa9d4: slot 3 the deleting destructor's thunk
// 0x2293d1).
//
// Standalone shape (docs/standalone-shape.md): the factory 0x229c42 (the audio director's class table) is hooked to Create;
// the constructor 0x22910e is called only by it, and the vtables are referenced only by the constructor and the destructor
// 0x2292a0 (reached only through the deleting destructor 0x2293d9), so every demon carries ours and the slots need no
// adapters. The monitors and playing sounds are made only by AddMonitor / AddSound, and every body of the three classes is
// ours (lane LE did the monitor's placement update 0x22a5ad, its step 0x2299b7 with the view ray 0x2296d2, the playing
// sound's update 0x22a119 and their small helpers), so nothing original reaches any member and only the factory is hooked.
#include "audio/cTSAudioResourceManager.h" // AudioRef
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/RZUnknownBase.h"
#include "rz/cRZStopwatch.h"
#include "ts/cTSCatalog.h" // ProductRef
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>

class cTSAudioTSObjectUpdateDemon;
class cITSAudioEvent;

// The 12-byte points the monitor and the sound keep.
struct AudioPoint {
    float x, y, z;
};
static_assert(sizeof(AudioPoint) == 12);

// A scene node's transform as its slot 54 (and the view's slots 53 / 140) write it: 0x1c bytes, the translation, then the
// rotation quaternion (x, y, z, w; 0x1a6c40 rotates by it).
struct AudioTransform {
    AudioPoint position;
    float rotation[4];
};
static_assert(sizeof(AudioTransform) == 0x1c);

// nGZCompGeom::cRay3 (0x20 bytes; vtable 0xda139c): an origin and a direction. Built here only by its constructor 0x18e720
// (origin, direction; +0x1c zero) and assigned by 0x18e880 (origin and direction only), destroyed by 0x18e860 (the vtable
// back to 0xda139c, then the base's 0xda1304): another class's code, called as the original.
struct AudioRay {
    const void *mVTable;  // +0x00
    AudioPoint mOrigin;    // +0x04
    AudioPoint mDirection; // +0x10
    uint32_t mWord1c;      // +0x1c
};
static_assert(sizeof(AudioRay) == 0x20);

// tMonitorEntry (0x70 bytes; cRZUnknown at +0, vtable 0xdaa9f0: every slot cRZUnknown's but the deleting destructor
// 0x2293f8): one object that plays sounds. Its model (+0x14) and object (+0x10) are filled by the placement update.
class tMonitorEntry : public rz::cRZUnknown {
public:
    static constexpr uint32_t kSize = 0x70;

    tMonitorEntry(cTSAudioTSObjectUpdateDemon *demon, uint16_t objectId); // 0x2291ac (ret 8)
    ~tMonitorEntry() noexcept override;                                         // 0x2292e9; deleting 0x2293f8
    tMonitorEntry(const tMonitorEntry &) = delete;
    tMonitorEntry &operator=(const tMonitorEntry &) = delete;

    bool Init();     // 0x229dc8: the lot from the state service; with it and a sound renderer, the times and enabled
    bool Detach();   // 0x22a00f: disabled, every reference dropped
    void Update();   // 0x22a091: the placement update (0x22a5ad), then the update interval from the distance
    // 0x22a5ad (1420 B): the object and its model taken (or dropped: disabled), then the "root_rot" node's (or the model's)
    // place and facing, the bounding box centre when they moved, the object's room and floor, the distance to the view.
    void UpdatePlacement();
    // 0x2299b7 (328 B): with the view and the lot scene, the audibility from the floor against the demon's (a ray from the
    // view to the centre through the lot scene when below it), sent to the sound renderer when it changed; the step time.
    void Step();
    bool AimRay(AudioRay *ray); // 0x2296d2 (221 B, ret 4): with a model, the ray from the view's eye to the centre
    AudioPoint Position() const { return mPosition; } // 0x229e85 (ret 4: the out point)
    AudioPoint Facing() const { return mFacing; }     // 0x2297af (ret 4: the out point)
    bool Hidden() const { return mHidden != 0; }      // 0x229d15
    static void *View();     // 0x22ab53: the state service's slot 33, its slot 18 (a nGZSceneGraph::cViewerRefNode)
    static void *LotScene(); // 0x229f1d: the state service's slot 33, its slot 17 (its slot 92 casts a ray)

    cRZAutoRefCount<cTSAudioTSObjectUpdateDemon> mDemon; // +0x08
    AudioRef mLot;                    // +0x0c the state service's slot 46, its slot 4
    AudioRef mObject;                 // +0x10 the object (the app's slot 26, its slot 29 by id): placement update
    ProductRef mModel;                // +0x14 the object's model (its slot 12; a virtual-base reference): placement update
    AudioRef mRef18;                  // +0x18 never set here
    uint32_t mRegistered;             // +0x1c set to 1 by AddMonitor
    uint32_t mUpdatedAt;              // +0x20 the demon's seconds at Init and at each step
    uint32_t mStepAt;                 // +0x24 the demon's seconds at Init, then at each update while sounds play
    uint32_t mInterval;               // +0x28 the update interval (Update)
    uint16_t mObjectId;               // +0x2c
    uint8_t mUnknown2e[2];            // +0x2e never written
    int32_t mRoom;                    // +0x30 -1; the object's slot 131 (16 bits, sign-extended): placement update
    int32_t mFloor;                   // +0x34 -1; the object's slot 36 (16 bits) less one: placement update
    float mAudibility;                // +0x38 1 or 0 (Step); 0 when disabled (placement update)
    uint32_t mSoundCount;             // +0x3c the playing sounds tied to it (tPlayingSound::SetMonitor)
    float mDistance;                  // +0x40 to the view's eye: placement update
    uint8_t mEnabled;                 // +0x44
    uint8_t mUnknown45[3];            // +0x45 never written
    AudioPoint mCentre;               // +0x48 the bounding box centre: placement update (not set by the constructor)
    AudioPoint mPosition;             // +0x54 placement update (not set by the constructor)
    AudioPoint mFacing;               // +0x60 placement update (not set by the constructor)
    uint8_t mHidden;                  // +0x6c the object's slot 13 (0x22) bit 1: placement update
    uint8_t mUnknown6d[3];            // +0x6d never written
};
static_assert(sizeof(tMonitorEntry) == tMonitorEntry::kSize, "new 0x70 in AddMonitor");
static_assert(offsetof(tMonitorEntry, mDemon) == 8 && offsetof(tMonitorEntry, mModel) == 0x14 &&
                  offsetof(tMonitorEntry, mRegistered) == 0x1c && offsetof(tMonitorEntry, mObjectId) == 0x2c &&
                  offsetof(tMonitorEntry, mSoundCount) == 0x3c && offsetof(tMonitorEntry, mEnabled) == 0x44 &&
                  offsetof(tMonitorEntry, mCentre) == 0x48 && offsetof(tMonitorEntry, mFacing) == 0x60 &&
                  offsetof(tMonitorEntry, mRoom) == 0x30 && offsetof(tMonitorEntry, mAudibility) == 0x38 &&
                  offsetof(tMonitorEntry, mHidden) == 0x6c,
              "tMonitorEntry layout");

// tPlayingSound (0x44 bytes; cRZUnknown at +0, vtable 0xdaaa0c: every slot cRZUnknown's but the deleting destructor
// 0x229417): one sound an object plays.
class tPlayingSound : public rz::cRZUnknown {
public:
    static constexpr uint32_t kSize = 0x44;

    explicit tPlayingSound(cTSAudioTSObjectUpdateDemon *demon); // 0x229228 (ret 4)
    ~tPlayingSound() noexcept override;                               // 0x229374; deleting 0x229417
    tPlayingSound(const tPlayingSound &) = delete;
    tPlayingSound &operator=(const tPlayingSound &) = delete;

    void SetMonitor(tMonitorEntry *monitor); // 0x229f6e (ret 4): only from none to one or back, counting its sounds
    bool Detach();                           // 0x22a044: the demon and the monitor dropped
    // 0x22a119 (1146 B): with an enabled monitor, the monitor's place, room, floor and audibility copied and sent to the
    // sound renderer (event 0xec773209) when any changed (the points within 32 ulps count as unchanged), then the
    // visibility (event 0x4a451915) when the monitor's hidden flag changed. A disabled monitor is dropped.
    void Update();

    cRZAutoRefCount<cTSAudioTSObjectUpdateDemon> mDemon; // +0x08
    cRZAutoRefCount<tMonitorEntry> mMonitor;             // +0x0c
    uint16_t mObjectId;                                  // +0x10
    uint8_t mUnknown12[2];                               // +0x12 never written
    uint32_t mSoundId;                                   // +0x14
    float mVolume;                                       // +0x18
    AudioPoint mPosition;                                // +0x1c from 0x10cc5a0; the monitor's (the update)
    AudioPoint mFacing;                                  // +0x28 from 0x10cc5a0; the monitor's (the update)
    int32_t mRoom;                                       // +0x34 the monitor's (the update)
    int32_t mFloor;                                      // +0x38 the monitor's (the update)
    float mAudibility;                                   // +0x3c the monitor's (the update)
    uint8_t mHidden;                                     // +0x40 the monitor's (the update)
    uint8_t mUnknown41[3];                               // +0x41 never written
};
static_assert(sizeof(tPlayingSound) == tPlayingSound::kSize, "new 0x44 in AddSound");
static_assert(offsetof(tPlayingSound, mMonitor) == 0xc && offsetof(tPlayingSound, mSoundId) == 0x14 &&
                  offsetof(tPlayingSound, mPosition) == 0x1c && offsetof(tPlayingSound, mFacing) == 0x28 &&
                  offsetof(tPlayingSound, mRoom) == 0x34 && offsetof(tPlayingSound, mAudibility) == 0x3c &&
                  offsetof(tPlayingSound, mHidden) == 0x40,
              "tPlayingSound layout");

using PlayingSoundList = std::list<cRZAutoRefCount<tPlayingSound>>;
using MonitorList = std::list<cRZAutoRefCount<tMonitorEntry>>;
static_assert(sizeof(MonitorList) == 8, "MSVC 14.40 list");

// cITSAudioTSObjectUpdateDemon (iid 0x4d7fd97e): slots 3 and 4 are also cIGZSystemService's Init / Shutdown (the +8 face's
// slots 8 / 9 are the thunks 0x229dc0 / 0x22a007).
class cITSAudioTSObjectUpdateDemon : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x4d7fd97e;
    virtual bool Init() = 0;     // 3 (0x229d1e)
    virtual bool Shutdown() = 0; // 4 (0x229fbc)
};

class cTSAudioTSObjectUpdateDemon : public cITSAudioTSObjectUpdateDemon,
                                    public cIGZMessageTarget2,
                                    public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 0x68;
    static constexpr uint32_t kServiceId = 0x4d58135d;
    static constexpr int32_t kPriority = int32_t(0xffd23940);
    static constexpr uint32_t kIid = cITSAudioTSObjectUpdateDemon::kIid;

    cTSAudioTSObjectUpdateDemon();                    // 0x22910e
    ~cTSAudioTSObjectUpdateDemon() noexcept override; // 0x2292a0; deleting 0x2293d9 (+0xc thunk 0x2293d1)
    cTSAudioTSObjectUpdateDemon(const cTSAudioTSObjectUpdateDemon &) = delete;
    cTSAudioTSObjectUpdateDemon &operator=(const cTSAudioTSObjectUpdateDemon &) = delete;

    // 0x229c42 (__cdecl): the COM director's factory: a new demon as its +8 face (null when new failed).
    static cIGZSystemService *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x229e98; thunks 0x229eba / 0x229ec2 / 0x229eca
    uint32_t AddRef() override;  // folded 0x2239ab (the base's); thunks 0x2239b3 / 0x2239bb / 0x2239cb
    uint32_t Release() override; // folded 0x2252e3 (the base's); thunks 0x2252eb / 0x2252f3 / 0x225303

    // ---- cITSAudioTSObjectUpdateDemon / cIGZSystemService ----
    bool Init() override;
    bool Shutdown() override;
    bool OnTick(uint32_t counter) override; // +8 slot 10, 0x229e45

    // ---- cIGZMessageTarget2 (+4) ----
    bool DoMessage(void *message) override; // 0x2297c2

    // ---- members the original calls (non-virtual, __thiscall on the primary face) ----
    bool AddMonitor(uint32_t objectId);                               // 0x229436 (ret 4): the low word is the id
    bool AddSound(uint32_t objectId, uint32_t soundId, float volume); // 0x22955b (ret 0xc)
    bool RemoveSound(uint32_t soundId);                               // 0x229ed2 (ret 4)
    bool OnSoundEvent(cITSAudioEvent *event);                         // 0x229c85 (ret 4)
    tMonitorEntry *FindMonitor(uint32_t objectId);                    // 0x229aff (ret 4): by the low word
    tPlayingSound *FindSound(uint32_t soundId);                       // 0x229b2c (ret 4)
    void ClearMonitors();                                             // 0x229b64
    void ClearSounds();                                               // 0x229bd3
    bool ClearAll();                                                  // 0x229d02
    void UpdateMonitors();                                            // 0x22a593
    void StepNextMonitor();                                           // 0x22995a
    void RotateMonitors();                                            // 0x22a05e: the first monitor to the back
    void UpdateSounds();                                              // 0x22ab39
    uint32_t Seconds();                                               // 0x22a07f: the stopwatch's count / 1000

    cIGZMessageTarget2 *MessageTarget() { return this; }
    cIGZSystemService *Service() { return this; }

    // +0x00 .. +0x07: the two interface faces; +0x08 .. +0x23: cRZSystemService
    std::set<uint32_t> mMessageIds; // +0x24
    PlayingSoundList mSounds;       // +0x2c
    MonitorList mMonitors;          // +0x34
    uint32_t mWord3c = 0;           // +0x3c never read here
    cRZStopwatch mWatch;            // +0x40 mode 3
    uint32_t mMonitorsAdded = 0;    // +0x58
    int32_t mModeWord5c = -1;       // +0x5c message 0xece20be8: its 0x65297976 face's slot 9; the floor Step compares with
    int32_t mModeWord60 = -1;       // +0x60 message 0xece20be8: its 0x65297976 face's slot 19
    uint32_t mUnknown64;            // +0x64 never written
};

static_assert(sizeof(cTSAudioTSObjectUpdateDemon) == cTSAudioTSObjectUpdateDemon::kSize, "new 0x68 in the factory");
static_assert(offsetof(cTSAudioTSObjectUpdateDemon, mMessageIds) == 0x24 &&
                  offsetof(cTSAudioTSObjectUpdateDemon, mSounds) == 0x2c &&
                  offsetof(cTSAudioTSObjectUpdateDemon, mMonitors) == 0x34 &&
                  offsetof(cTSAudioTSObjectUpdateDemon, mWord3c) == 0x3c &&
                  offsetof(cTSAudioTSObjectUpdateDemon, mWatch) == 0x40 &&
                  offsetof(cTSAudioTSObjectUpdateDemon, mMonitorsAdded) == 0x58 &&
                  offsetof(cTSAudioTSObjectUpdateDemon, mModeWord5c) == 0x5c &&
                  offsetof(cTSAudioTSObjectUpdateDemon, mUnknown64) == 0x64,
              "cTSAudioTSObjectUpdateDemon layout");

// The hooks' entry points.
struct cTSAudioTSObjectUpdateDemonAdapters {
    static cTSAudioTSObjectUpdateDemon *__fastcall Construct(void *memory, void *edx); // 0x22910e (tests; not hooked)
};
