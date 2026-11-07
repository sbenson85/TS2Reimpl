#pragma once
// cGZSndMgr (lane KJ): the engine's sound manager service (class 0x7af85b98, service id 0x1af85bfb, iid 0x3af85bb7,
// priority 0x7a11e). 0xbc bytes, /O1 (the module is built for size).
//
// It keeps two registries of sound objects by 64-bit id. The first: objects registered per id (slot 12) and creators asked
// in turn for an id nobody registered (slot 15, interface 0x3af9866f); Create (slot 18) hands out an interface of the
// registered object or of the first creator that answers, Play (slot 20) creates the id's sound (interface 0x206077d2),
// queues it on the playing list and starts it; aliases map a 32-bit name id to a sound id (slots 6 / 7, Play by alias slot
// 22). The second: objects per id (slot 29) and creators by a 32-bit id (slot 25, interface 0x9b75709e), Create2 (slot 24).
// The update target (+0x20, registered with the sound renderer every 250 ms) polls the playing list's first sound and
// drops it once it says it is done. Every operation runs under the lock (+0x78, a heap cRZCriticalSection).
//
// Faces (RTTI order): +0 cRZSystemService (cIGZSystemService, vtable 0xe81114; cRZUnknownMT at +4, 0xe8114c), +0x1c
// cIGZSndMgr (0xe81168, 32 slots), +0x20 cIGZSndUpdateTarget (0xe811ec).
//
// Standalone shape (docs/standalone-shape.md): the factory 0xb22333 (the sound COM director registers it for class
// 0x7af85b98) is hooked to Create; the constructor 0xb256f5 is reached only from it, and the vtables are referenced only by
// the constructor and the destructor 0xb2582c (reached only through the deleting destructor 0xb25906 and its +4 thunk
// 0xb258d8), so every manager carries ours and the slots need no adapters.
#include "audio/AudioInterfaces.h"          // cIGZSndUpdateTarget
#include "audio/cTSAudioResourceManager.h"  // AudioRef, AudioIdHash
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <unordered_map>

class cIGZString;

// cIGZSndMgr (iid 0x3af85bb7): slots 3-31. Ids are 64 bits (two words on the stack); "iid, out" pairs are QueryInterface
// arguments handed to the object found.
class cIGZSndMgr : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x3af85bb7;
    virtual bool OpenThroughResources(uint32_t a, uint32_t b, uint32_t c) = 0; // 3 (0xb25980): the resource manager's slot 3
    virtual void SetDefaultFlags(uint32_t flags) = 0;                    // 4 (folded 0x20daf0): +0x30
    virtual uint32_t GetDefaultFlags() = 0;                              // 5 (folded 0x149d00)
    virtual uint32_t GetAlias(uint32_t name) = 0;                        // 6 (0xb262e2): the id's low word, 0 when none
    virtual void SetAlias(uint32_t name, uint64_t id) = 0;               // 7 (0xb2606d)
    virtual bool SetWordB4(uint32_t value) = 0;                          // 8 (0xb2604f)
    virtual bool GetWordB4(uint32_t *out) = 0;                           // 9 (0xb2595e)
    virtual bool SetWordB8(uint32_t value) = 0;                          // 10 (0xb2605e)
    virtual bool GetWordB8(uint32_t *out) = 0;                           // 11 (0xb2596f)
    virtual void RegisterObject(cIGZUnknown *object, uint64_t id) = 0;   // 12 (0xb25cce): unless the id has one
    virtual void UnregisterObject(uint64_t id) = 0;                      // 13 (0xb26322)
    virtual void UnregisterObjectByPointer(cIGZUnknown *object) = 0;     // 14 (0xb2637c): the first entry holding it
    virtual bool AddCreator(cIGZUnknown *creator) = 0;                   // 15 (0xb25d66): at the front
    virtual bool RemoveCreator(cIGZUnknown *creator) = 0;                // 16 (0xb25f43)
    virtual bool CreateObjectByName(cIGZString *name, uint32_t a, uint32_t b, uint32_t iid, void **out) = 0; // 17 (0xb25ad1)
    virtual bool CreateObject(uint64_t id, uint32_t a, uint32_t b, uint32_t iid, void **out) = 0; // 18 (0xb25b22)
    virtual bool Play(uint64_t id) = 0;                                  // 19 (0xb2617f): with the default flags
    virtual bool PlayWithFlags(uint64_t id, uint32_t flags) = 0;         // 20 (0xb26192)
    virtual bool PlayAlias(uint32_t name, uint32_t unused) = 0;          // 21 (0xb26257): with the default flags
    virtual bool PlayAliasWithFlags(uint32_t name, uint32_t unused, uint32_t flags) = 0; // 22 (0xb2626a)
    virtual bool AddPlaying(cIGZUnknown *sound) = 0;                     // 23 (0xb25928): at the back
    virtual bool CreateObject2(uint64_t id, uint32_t a, uint32_t b, uint32_t iid, void **out) = 0; // 24 (0xb259a3)
    virtual bool AddCreator2(cIGZUnknown *creator, uint32_t key) = 0;    // 25 (0xb25e77)
    virtual bool RemoveCreator2(uint32_t key) = 0;                       // 26 (0xb25f95)
    virtual bool RemoveCreator2ByPointer(cIGZUnknown *creator) = 0;      // 27 (0xb25fe1)
    virtual bool QueryCreator2(uint32_t key, uint32_t iid, void **out) = 0; // 28 (0xb25a73)
    virtual void RegisterObject2(cIGZUnknown *object, uint64_t id) = 0;  // 29 (0xb25dee): unless the id has one
    virtual void UnregisterObject2(uint64_t id) = 0;                     // 30 (0xb263bf)
    virtual void UnregisterObject2ByPointer(cIGZUnknown *object) = 0;    // 31 (0xb26410)
};

class cGZSndMgr : public rz::cRZSystemService, public cIGZSndMgr, public cIGZSndUpdateTarget {
public:
    static constexpr uint32_t kSize = 0xbc;
    static constexpr uint32_t kServiceId = 0x1af85bfb;
    static constexpr int32_t kPriority = 0x7a11e;

    // MSVC 14.40 containers as in the game. The id maps hash the id's low word (the finds 0x2182f0); nodes: objects 0x18
    // (the id at +8, the object at +0x10), aliases 0x18 (the name at +8, the id at +0x10). The maps: creators2 nodes 0x18
    // (key +0x10, creator +0x14), objects2 nodes 0x20 (id +0x10, object +0x18). The lists hold counted objects (+8).
    using ObjectMap = std::unordered_map<uint64_t, AudioRef, AudioIdHash>;
    using AliasMap = std::unordered_map<uint32_t, uint64_t>;
    using RefList = std::list<AudioRef>;

    cGZSndMgr();                    // 0xb256f5
    ~cGZSndMgr() noexcept override; // 0xb2582c; deleting 0xb25906 (+4 thunk 0xb258d8)
    cGZSndMgr(const cGZSndMgr &) = delete;
    cGZSndMgr &operator=(const cGZSndMgr &) = delete;

    static cGZSndMgr *__cdecl Create(); // 0xb22333: new 0xbc, the constructor

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xb25c7f; thunks 0xb25cb6 / 0xb25cbe / 0xb25cc6
    // The service's (folded 0x1cc320 = 0x1b9640; the faces' thunks 0x1cc325 / 0x1cc32d / 0x1cc335).
    uint32_t AddRef() override { return rz::cRZSystemService::AddRef(); }
    uint32_t Release() override;                             // 0xb25f0c; thunks 0xb25f2b / 0xb25f33 / 0xb25f3b

    // ---- cIGZSystemService ----
    bool Init() override;     // 0xb25c00
    bool Shutdown() override; // 0xb260d0

    // ---- cIGZSndMgr (+0x1c) ----
    bool OpenThroughResources(uint32_t a, uint32_t b, uint32_t c) override;
    void SetDefaultFlags(uint32_t flags) override { mDefaultFlags = flags; }
    uint32_t GetDefaultFlags() override { return mDefaultFlags; }
    uint32_t GetAlias(uint32_t name) override;
    void SetAlias(uint32_t name, uint64_t id) override;
    bool SetWordB4(uint32_t value) override;
    bool GetWordB4(uint32_t *out) override;
    bool SetWordB8(uint32_t value) override;
    bool GetWordB8(uint32_t *out) override;
    void RegisterObject(cIGZUnknown *object, uint64_t id) override;
    void UnregisterObject(uint64_t id) override;
    void UnregisterObjectByPointer(cIGZUnknown *object) override;
    bool AddCreator(cIGZUnknown *creator) override;
    bool RemoveCreator(cIGZUnknown *creator) override;
    bool CreateObjectByName(cIGZString *name, uint32_t a, uint32_t b, uint32_t iid, void **out) override;
    bool CreateObject(uint64_t id, uint32_t a, uint32_t b, uint32_t iid, void **out) override;
    bool Play(uint64_t id) override;
    bool PlayWithFlags(uint64_t id, uint32_t flags) override;
    bool PlayAlias(uint32_t name, uint32_t unused) override;
    bool PlayAliasWithFlags(uint32_t name, uint32_t unused, uint32_t flags) override;
    bool AddPlaying(cIGZUnknown *sound) override;
    bool CreateObject2(uint64_t id, uint32_t a, uint32_t b, uint32_t iid, void **out) override;
    bool AddCreator2(cIGZUnknown *creator, uint32_t key) override;
    bool RemoveCreator2(uint32_t key) override;
    bool RemoveCreator2ByPointer(cIGZUnknown *creator) override;
    bool QueryCreator2(uint32_t key, uint32_t iid, void **out) override;
    void RegisterObject2(cIGZUnknown *object, uint64_t id) override;
    void UnregisterObject2(uint64_t id) override;
    void UnregisterObject2ByPointer(cIGZUnknown *object) override;

    // ---- cIGZSndUpdateTarget (+0x20) ----
    void Update(uint32_t now) override; // 0xb26474

    // +0x00 .. +0x1b: cRZSystemService; +0x1c, +0x20: the two faces
    uint8_t mInitialized = 0;          // 0x24
    uint8_t mUnknown25[3];             // 0x25 never written
    AudioRef mRenderer;                // 0x28 the sound renderer (the probe 0x9c5b7e), from Init
    AudioRef mResources;               // 0x2c the persist resource manager (0x56b906e / 0x656b8efc), from Init
    uint32_t mDefaultFlags = 0x400;    // 0x30 slots 4 / 5; Play / PlayAlias pass it
    ObjectMap mObjects;                // 0x34
    uint32_t mObjectsBusy = 0;         // 0x54 raised around walks and some finds
    AliasMap mAliases;                 // 0x58
    cRZCriticalSection *mLock = nullptr; // 0x78
    RefList mCreators;                 // 0x7c
    uint32_t mCreatorsBusy = 0;        // 0x84
    RefList mPlaying;                  // 0x88
    uint32_t mPlayingBusy = 0;         // 0x90
    std::map<uint32_t, AudioRef> mCreators2; // 0x94
    uint32_t mCreators2Busy = 0;       // 0x9c
    std::map<uint64_t, AudioRef> mObjects2;  // 0xa0
    uint32_t mObjects2Busy = 0;        // 0xa8
    std::map<uint32_t, uint32_t> mMapAC; // 0xac never filled; Shutdown walks and clears it
    uint32_t mWordB4 = 0;              // 0xb4 slots 8 / 9
    uint32_t mWordB8 = 0;              // 0xb8 slots 10 / 11
};

static_assert(sizeof(cGZSndMgr) == cGZSndMgr::kSize, "cGZSndMgr size");
static_assert(offsetof(cGZSndMgr, mInitialized) == 0x24, "mInitialized");
static_assert(offsetof(cGZSndMgr, mRenderer) == 0x28, "mRenderer");
static_assert(offsetof(cGZSndMgr, mDefaultFlags) == 0x30, "mDefaultFlags");
static_assert(offsetof(cGZSndMgr, mObjects) == 0x34, "mObjects");
static_assert(offsetof(cGZSndMgr, mObjectsBusy) == 0x54, "mObjectsBusy");
static_assert(offsetof(cGZSndMgr, mAliases) == 0x58, "mAliases");
static_assert(offsetof(cGZSndMgr, mLock) == 0x78, "mLock");
static_assert(offsetof(cGZSndMgr, mCreators) == 0x7c, "mCreators");
static_assert(offsetof(cGZSndMgr, mPlaying) == 0x88, "mPlaying");
static_assert(offsetof(cGZSndMgr, mCreators2) == 0x94, "mCreators2");
static_assert(offsetof(cGZSndMgr, mObjects2) == 0xa0, "mObjects2");
static_assert(offsetof(cGZSndMgr, mMapAC) == 0xac, "mMapAC");
static_assert(offsetof(cGZSndMgr, mWordB8) == 0xb8, "mWordB8");

// The hooks' entry points.
struct cGZSndMgrAdapters {
    static cGZSndMgr *__fastcall Construct(void *memory, void *edx); // 0xb256f5 (tests; not hooked)
};
