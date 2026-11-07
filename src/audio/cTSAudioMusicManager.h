#pragma once
// cTSAudioMusicManager (lane KI): the audio director's music service (factory 0x214c96, service id 0x4bb90be0, priority
// -0x2dc6c0, interface 0xabb90bdb). 0xe4 bytes, /O1 (the module is built for size).
//
// It owns the music states (cTSAudioMusicManagerState, 0x1c bytes: five built by Init, 0xec313961 / 0xec313964 /
// 0xec313967 / 0xec31396a / 0x0d8a50ad), the player objects at +0x5c / +0x60, the stations (+0x98, by a 64-bit id with
// two names) and the custom playlist manager (+0xe0). Slots 5-11 are thread proxies as in the ambience manager: on the
// sound renderer's thread (its slot 51 is its thread id) they run the worker at once, else they build a cTSAudioEvent and
// hand it over (the renderer's slot 8, the sound probe's slots 8 / 12, or the sound manager's PostEvent); the renderer
// gives the event back to HandleEvent (the +4 face) on its thread. The workers and the state / player members are still
// the original's (original_calls in db/status/cTSAudioMusicManager.jsonl).
//
// Faces (RTTI order): +0 cITSAudioMusicManager (vtable 0xda8978, 22 slots), +4 cIGZSndEventHandler (0xda89d4), +8
// cIGZSndUpdateTarget (0xda89e8), +0xc cIGZMessageTarget2 (0xda89fc), +0x10 cRZSystemService / cIGZSystemService
// (0xda8a10), +0x14 cRZUnknownMT (0xda8a48; its slot 3 is the deleting destructor thunk 0x214505).
//
// Standalone shape (docs/standalone-shape.md): the factory 0x214c96 (the audio COM director registers it) is hooked to
// Create; the constructor 0x214020 is called only by it, and the vtables are referenced only by the constructor and the
// destructor 0x21436e (reached only through the deleting destructor 0x2145ae and its +0x14 thunk 0x214505), so every
// manager carries ours: the slots and their adjustor thunks are ours in effect through those callers.
#include "audio/AudioInterfaces.h"          // cIGZSndEventHandler, cIGZSndUpdateTarget
#include "audio/cTSAudioResourceManager.h" // AudioRef, AudioResourceString
#include "gz/cGZDBSegmentPackedFile.h"     // GZServiceAllocator
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <set>

class cIGZString;

// cITSAudioMusicManager (iid 0xabb90bdb). Slots 3 and 4 are also cIGZSystemService's Init / Shutdown (the +0x10 face's
// slots 8 / 9 are the thunks 0x2151eb / 0x215640). Argument words documented as "a bool" are passed on raw to the worker
// and as their low byte in the event. A station is named by a 64-bit id (two words).
class cITSAudioMusicManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xabb90bdb;
    virtual bool Init() = 0;                                                        // 3 (0x214f1f)
    virtual bool Shutdown() = 0;                                                    // 4 (0x215576)
    virtual void Slot5() = 0;                                                       // 5 (0x215856): player +0x60, 0x23a2f4
    virtual void Slot6(uint32_t a, uint32_t flag, uint32_t viaManager) = 0;         // 6 (0x215492): 0x2175cb; flags bools
    virtual void Slot7(uint32_t a, uint32_t b, int32_t c, const cIGZString *d, const cIGZString *e) = 0; // 7 (0x215648)
    virtual void Slot8(uint32_t a) = 0;                                             // 8 (0x21578a): 0x217be2
    virtual void Slot9() = 0;                                                       // 9 (0x2158f8): 0x217c55
    virtual void Slot10(uint32_t a, uint32_t flag) = 0;                             // 10 (0x21536b): 0x217490; flag a bool
    virtual void Slot11(uint32_t a, const char *name) = 0;                          // 11 (0x215294): 0x21745e
    virtual void *GetList88() = 0;                                                  // 12 (0x214d6e): the list at +0x88
    virtual uint64_t FindStationId(const cIGZString *name) = 0;                     // 13 (0x214d75): by the second name
    virtual void *Slot14(uint32_t idLow, uint32_t idHigh) = 0;                      // 14 (0x214e5e)
    virtual uint64_t Slot15(uint32_t idLow, uint32_t idHigh, uint32_t a) = 0;       // 15 (0x214ded)
    virtual bool Slot16(uint32_t a, uint32_t b, uint32_t c) = 0;                    // 16 (0x214cd1)
    virtual void Slot17(uint32_t idLow, uint32_t idHigh, uint32_t a, uint32_t b, uint32_t c) = 0; // 17 (0x214c3b)
    virtual bool Slot18(uint32_t idLow, uint32_t idHigh, uint32_t a, uint32_t b) = 0; // 18 (0x214ebd)
    virtual void Slot19(float value) = 0;                                           // 19 (0x2151f3): player +0x60, 0x23a25e
    virtual void Slot20(float value) = 0;                                           // 20 (0x215477): player +0x60, 0x23a457
    virtual cIGZUnknown *GetPlaylistManager() = 0;                                  // 21 (folded 0x142870): +0xe0, uncounted
};

// A cRZString member (built and destroyed by hand, as the original's inlined code does); the copy is the std::string's
// copy with the count 0 (0x141eb3 with the whole string).
struct MusicString {
    union {
        cRZString s;
    };
    MusicString() { s.Construct(); }
    MusicString(const MusicString &other) { s.Construct(other.s, 0, ~0u); }
    MusicString &operator=(const MusicString &) = delete;
    ~MusicString() { s.Destruct(); }
};
static_assert(sizeof(MusicString) == 0x20, "cRZString");

// A station (the +0x98 list's value, 0x48 bytes; copied by 0x2141ea, destroyed by 0x2144b9): its 64-bit id and two names.
struct MusicStation {
    uint32_t idLow = 0;  // +0x00
    uint32_t idHigh = 0; // +0x04
    MusicString name;    // +0x08 the name 0x215c55 looks the station's state up by
    MusicString name2;   // +0x28 the name slot 13 compares
    MusicStation() = default;
    MusicStation(const MusicStation &) = default; // 0x2141ea: the ids, then both strings
    MusicStation &operator=(const MusicStation &) = delete;
};
static_assert(sizeof(MusicStation) == 0x48, "MusicStation");

// MSVC 14.40 containers as in the game: the id lists' nodes 0xc and the station list's 0x50 come from the allocator
// service (0x217dd1 / 0x217e49, freed through 0x1d0903), the others from operator new.
using MusicIdList = std::list<uint32_t, GZServiceAllocator<uint32_t>>;
using MusicStationList = std::list<MusicStation, GZServiceAllocator<MusicStation>>;
using MusicObjectList = std::list<AudioRef>; // counted (the node copy 0x213b96 AddRefs)
// A word to a counted object, nodes 0x18 (only the original's code fills them; a node's pair is destroyed by 0x21455d,
// which releases the second word).
using MusicRefMap = std::map<uint32_t, AudioRef>;
static_assert(sizeof(MusicIdList) == 8 && sizeof(MusicStationList) == 8 && sizeof(MusicObjectList) == 8, "MSVC 14.40 list");
static_assert(sizeof(MusicRefMap) == 8, "MSVC 14.40 map");

class cTSAudioMusicManager : public cITSAudioMusicManager,
                             public cIGZSndEventHandler,
                             public cIGZSndUpdateTarget,
                             public cIGZMessageTarget2,
                             public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 0xe4;
    static constexpr uint32_t kServiceId = 0x4bb90be0;
    static constexpr int32_t kPriority = int32_t(0xffd23940);
    static constexpr uint32_t kHandlerIid = 0xbb523bb2; // cIGZSndEventHandler

    // The critical section as a member: built (0x1af64e) in member order, destroyed (0x1af68a) in reverse member order.
    struct OwnedLock : cRZCriticalSection {
        OwnedLock() { Construct(); }
        ~OwnedLock() { Destruct(); }
        OwnedLock(const OwnedLock &) = delete;
        OwnedLock &operator=(const OwnedLock &) = delete;
    };

    cTSAudioMusicManager();                    // 0x214020
    ~cTSAudioMusicManager() noexcept override; // 0x21436e; deleting 0x2145ae (+0x14 thunk 0x214505)
    cTSAudioMusicManager(const cTSAudioMusicManager &) = delete;
    cTSAudioMusicManager &operator=(const cTSAudioMusicManager &) = delete;

    static cTSAudioMusicManager *__cdecl Create(); // 0x214c96: new 0xe4, the constructor

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x21520e; thunks 0x21526c / 0x215274 / 0x21528c / 0x21527c / 0x215284
    uint32_t AddRef() override;                              // folded 0x18fd70; thunks 0x2145d0 / 0x2145d8 / 0x2145f0 / 0x2145e0 / 0x2145e8
    uint32_t Release() override;                             // 0x215437; thunks 0x21544f / 0x215457 / 0x21546f / 0x21545f / 0x215467

    // ---- cITSAudioMusicManager / cIGZSystemService ----
    bool Init() override;
    bool Shutdown() override;
    void Slot5() override;
    void Slot6(uint32_t a, uint32_t flag, uint32_t viaManager) override;
    void Slot7(uint32_t a, uint32_t b, int32_t c, const cIGZString *d, const cIGZString *e) override;
    void Slot8(uint32_t a) override;
    void Slot9() override;
    void Slot10(uint32_t a, uint32_t flag) override;
    void Slot11(uint32_t a, const char *name) override;
    void *GetList88() override { return &mList88; }
    uint64_t FindStationId(const cIGZString *name) override;
    void *Slot14(uint32_t idLow, uint32_t idHigh) override;
    uint64_t Slot15(uint32_t idLow, uint32_t idHigh, uint32_t a) override;
    bool Slot16(uint32_t a, uint32_t b, uint32_t c) override;
    void Slot17(uint32_t idLow, uint32_t idHigh, uint32_t a, uint32_t b, uint32_t c) override;
    bool Slot18(uint32_t idLow, uint32_t idHigh, uint32_t a, uint32_t b) override;
    void Slot19(float value) override;
    void Slot20(float value) override;
    cIGZUnknown *GetPlaylistManager() override { return mPlaylistManager.Get(); }

    // ---- cIGZSndEventHandler (+4), cIGZSndUpdateTarget (+8), cIGZMessageTarget2 (+0xc) ----
    bool HandleEvent(cIGZUnknown *event) override; // 0x2145f8
    void Update(uint32_t now) override;            // 0x215998
    bool DoMessage(void *message) override;        // 0x214b85

    bool OnRendererThread(); // the renderer's slot 51 against the current thread

    cIGZSndEventHandler *Handler() { return this; }
    cIGZSndUpdateTarget *UpdateTarget() { return this; }
    cIGZMessageTarget2 *MessageTarget() { return this; }
    cIGZSystemService *Service() { return this; }

    // +0x00 .. +0x0f: the four interface faces; +0x10 .. +0x2b: cRZSystemService
    uint32_t mRefs = 0;               // 0x2c (AddRef / Release, not interlocked)
    uint8_t mInitialized = 0;         // 0x30
    uint8_t mUnknown31[3];            // 0x31 never written
    AudioRef mRenderer;               // 0x34 the sound renderer (service 0x23cecba0, iid 0x106077d2), Init to Shutdown
    AudioRef mService38;              // 0x38 service 0x1af85bfb (iid 0x3af85bb7, the getter 0x212e8c), Init to Shutdown
    std::set<uint32_t> mMessageIds;   // 0x3c
    MusicObjectList mList44;          // 0x44 (slot 16 asks each 0x237cb9)
    OwnedLock mLock;                  // 0x4c
    MusicObjectList mStates;          // 0x54 the music states (0x2159c2 adds them)
    AudioRef mPlayer5C;               // 0x5c (only the original's workers use it)
    AudioRef mPlayer60;               // 0x60 (slots 5, 19, 20, Update, the 0x6bbf8a5a / 0x0bb6649b events)
    uint32_t mWord64 = 0;             // 0x64
    uint32_t mWord68 = 0;             // 0x68
    uint8_t mUnknown6C[0x10];         // 0x6c never written here
    AudioRef mInterface7C;            // 0x7c the 0x206077d2 face of the 0x2bc250e7 event's object
    MusicIdList mList80;              // 0x80 (slot 14's default)
    MusicIdList mList88;              // 0x88 (slot 12)
    MusicIdList mList90;              // 0x90
    MusicStationList mStations;       // 0x98
    uint8_t mByteA0 = 0;              // 0xa0
    uint8_t mUnknownA1[3];            // 0xa1 never written
    uint32_t mWordA4 = 0;             // 0xa4
    uint32_t mWordA8 = 0;             // 0xa8
    MusicString mStringAC;            // 0xac (the 0x0bb6649b event restarts only while it and +0xa4 are empty)
    uint8_t mByteCC = 0;              // 0xcc
    uint8_t mUpdatePending = 0;       // 0xcd Update runs 0x21738b once when set (the 0x0bb6649b event sets it)
    uint8_t mUnknownCE[2];            // 0xce never written
    MusicRefMap mMapD0;              // 0xd0
    MusicRefMap mMapD8;              // 0xd8
    AudioRef mPlaylistManager;        // 0xe0 a cTSAudioCustomPlaylistManager (0x18 bytes, 0x23a84c), Init to Shutdown
};

static_assert(sizeof(cTSAudioMusicManager) == cTSAudioMusicManager::kSize, "cTSAudioMusicManager size");
static_assert(offsetof(cTSAudioMusicManager, mRefs) == 0x2c, "mRefs");
static_assert(offsetof(cTSAudioMusicManager, mInitialized) == 0x30, "mInitialized");
static_assert(offsetof(cTSAudioMusicManager, mRenderer) == 0x34, "mRenderer");
static_assert(offsetof(cTSAudioMusicManager, mMessageIds) == 0x3c, "mMessageIds");
static_assert(offsetof(cTSAudioMusicManager, mList44) == 0x44, "mList44");
static_assert(offsetof(cTSAudioMusicManager, mLock) == 0x4c, "mLock");
static_assert(offsetof(cTSAudioMusicManager, mStates) == 0x54, "mStates");
static_assert(offsetof(cTSAudioMusicManager, mPlayer5C) == 0x5c, "mPlayer5C");
static_assert(offsetof(cTSAudioMusicManager, mWord64) == 0x64, "mWord64");
static_assert(offsetof(cTSAudioMusicManager, mInterface7C) == 0x7c, "mInterface7C");
static_assert(offsetof(cTSAudioMusicManager, mList80) == 0x80, "mList80");
static_assert(offsetof(cTSAudioMusicManager, mList88) == 0x88, "mList88");
static_assert(offsetof(cTSAudioMusicManager, mStations) == 0x98, "mStations");
static_assert(offsetof(cTSAudioMusicManager, mByteA0) == 0xa0, "mByteA0");
static_assert(offsetof(cTSAudioMusicManager, mWordA4) == 0xa4, "mWordA4");
static_assert(offsetof(cTSAudioMusicManager, mStringAC) == 0xac, "mStringAC");
static_assert(offsetof(cTSAudioMusicManager, mByteCC) == 0xcc, "mByteCC");
static_assert(offsetof(cTSAudioMusicManager, mMapD0) == 0xd0, "mMapD0");
static_assert(offsetof(cTSAudioMusicManager, mPlaylistManager) == 0xe0, "mPlaylistManager");

// The hooks' entry points.
struct cTSAudioMusicManagerAdapters {
    static cTSAudioMusicManager *__fastcall Construct(void *memory, void *edx); // 0x214020 (tests; not hooked)
};
