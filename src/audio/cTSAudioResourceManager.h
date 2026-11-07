#pragma once
// cTSAudioResourceManager (lane IY): the audio director's resource service (class 0xcce5d633, service id 0x6d92870d,
// priority -0x2dc6c0). 0x1ac bytes, /O2. It keeps three key maps (sound definitions, hit lists, data sources: the 64-bit id
// instance | resource << 32 -> the resource key), filled from the segments' key lists through three type / group filters,
// three caches of loaded resources by id, and an idle preloader that loads every listed key, 4000 ms of stopwatch time per
// update, sound definitions first, then hit lists, then the data sources of the allowed groups.
//
// Faces (RTTI order): +0 cITSAudioResourceManager (vtable 0xdaab54, 13 slots, iid 0xcce5d62f), +4 cIGZSndEventHandler
// (0xdaab8c), +8 cIGZSndUpdateTarget (0xdaaba0), +0xc cIGZMessageTarget2 (0xdaabb4, iid 0x452294aa), +0x10 cRZSystemService /
// cIGZSystemService (0xdaabc8), +0x14 cRZUnknownMT (0xdaac00).
//
// Standalone shape (docs/standalone-shape.md): the factory 0x22c83b (the audio director registers it for class 0xcce5d633)
// is hooked to Create; the constructor 0x22b1d9 is called only by it, and the vtables are referenced only by the
// constructor and the destructor 0x22b528, so every manager carries ours and the slots have no adapters. AddSegmentKeys
// (0x22b97b) is also hooked: the original BuildResourceMaps 0x22cf55 still calls it.
#include "audio/AudioInterfaces.h"
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZString.h"
#include "ts/cTSCatalog.h"                 // cIGZPersistResourceKeyFilter
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

class cIGZString;

using AudioRef = cRZAutoRefCount<cIGZUnknown>;

// The id maps' hash: the id's low word (the instance; the original's finds 0x2182f0 take it as the hash, equality 0x218ec8
// compares both words).
struct AudioIdHash {
    size_t operator()(uint64_t id) const { return static_cast<uint32_t>(id); }
};
// MSVC 14.40 std::unordered_map as in the game (0x20 bytes). Key maps: nodes 0x20, the key at +0x10; caches: nodes 0x18,
// the counted resource at +0x10 (erasing releases it).
using AudioKeyMap = std::unordered_map<uint64_t, cGZPersistResourceKey, AudioIdHash>;
using AudioCache = std::unordered_map<uint64_t, AudioRef, AudioIdHash>;
static_assert(sizeof(AudioKeyMap) == 0x20 && sizeof(AudioCache) == 0x20, "MSVC 14.40 unordered_map");
using AudioKeyList = std::list<cGZPersistResourceKey>; // nodes 0x18
static_assert(sizeof(AudioKeyList) == 8 && sizeof(AudioKeyList::iterator) == 4, "MSVC 14.40 list");

// cTSAudioKeyFilterByTypesAndGroups (vtable 0xdaab3c, 0x18 bytes): accepts the keys whose type and group are both in its
// sets. Built only as the manager's members (constructor 0x22b17a), so with the manager ours it is only ever ours.
class cTSAudioKeyFilterByTypesAndGroups : public cIGZPersistResourceKeyFilter {
public:
    static constexpr uint32_t kInterfaceId = 0x8865cd89;

    cTSAudioKeyFilterByTypesAndGroups() = default; // 0x22b17a
    ~cTSAudioKeyFilterByTypesAndGroups() override; // 0x22b50b; slot 4, the scalar deleting destructor 0x22b76c (0x18)
    cTSAudioKeyFilterByTypesAndGroups(const cTSAudioKeyFilterByTypesAndGroups &) = delete;
    cTSAudioKeyFilterByTypesAndGroups &operator=(const cTSAudioKeyFilterByTypesAndGroups &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, folded 0x99b760 (cGZPersistResourceKeyFilter's)
    uint32_t AddRef() override;                              // slot 1, folded 0x148fb0
    uint32_t Release() override;                             // slot 2, folded 0x723ae0
    bool IsKeyIncluded(const uint32_t *key) override;        // slot 3, 0x22df58

    uint32_t mRefCount = 0;    // +4
    std::set<uint32_t> mTypes;  // +8
    std::set<uint32_t> mGroups; // +0x10
};
static_assert(sizeof(cTSAudioKeyFilterByTypesAndGroups) == 0x18, "cTSAudioKeyFilterByTypesAndGroups layout");

// A cRZString member (built and destroyed by hand, as the original's inlined code does).
struct AudioResourceString {
    union {
        cRZString s;
    };
    AudioResourceString() { s.Construct(); }
    AudioResourceString(const AudioResourceString &) = delete;
    AudioResourceString &operator=(const AudioResourceString &) = delete;
    ~AudioResourceString() { s.Destruct(); }
};
static_assert(sizeof(AudioResourceString) == 0x20, "cRZString");

// cITSAudioResourceManager (iid 0xcce5d62f): slots 3-12. Slots 3 and 4 are also cIGZSystemService's Init / Shutdown (the
// +0x10 face's slots 8 / 9 are the thunks 0x22ce7e / 0x22e784).
class cITSAudioResourceManager : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                    // 3 (0x22cd5a)
    virtual bool Shutdown() = 0;                                                // 4 (0x22e6e2)
    virtual bool StartPreloading() = 0;                                         // 5 (0x22e78c)
    virtual bool ClearInitialized() = 0;                                        // 6 (0x22e7c4)
    virtual void PostSegmentAdded(cIGZUnknown *segment) = 0;                    // 7 (0x22ba3a)
    virtual bool GetSoundDefinition(uint64_t id, uint32_t iid, void **out) = 0; // 8 (0x22ca32)
    virtual bool GetHitList(uint64_t id, uint32_t iid, void **out) = 0;         // 9 (0x22c8c4)
    virtual bool OpenDecoder(uint64_t id, void **out) = 0;                      // 10 (0x22bd57)
    virtual bool CreateDecoder(cIGZUnknown *stream, void **out) = 0;            // 11 (0x22c05a)
    virtual cIGZUnknown *GetPropertySet() = 0;                                  // 12 (0x22bb69)
};

class cTSAudioResourceManager : public cITSAudioResourceManager,
                                public cIGZSndEventHandler,
                                public cIGZSndUpdateTarget,
                                public cIGZMessageTarget2,
                                public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 0x1ac;
    static constexpr uint32_t kServiceId = 0x6d92870d;
    static constexpr int32_t kPriority = int32_t(0xffd23940);
    static constexpr uint32_t kIid = 0xcce5d62f;
    static constexpr uint32_t kMessageTargetIid = 0x452294aa;

    cTSAudioResourceManager();                    // 0x22b1d9
    ~cTSAudioResourceManager() noexcept override; // 0x22b528; deleting 0x22b78b (+0x14 thunk 0x22b6fb)
    cTSAudioResourceManager(const cTSAudioResourceManager &) = delete;
    cTSAudioResourceManager &operator=(const cTSAudioResourceManager &) = delete;

    static cTSAudioResourceManager *__cdecl Create(); // 0x22c83b: new 0x1ac, the constructor

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x22e529; thunks 0x22e56b / 0x22e573 / 0x22e58b / 0x22e57b / 0x22e583
    uint32_t AddRef() override;                              // 0x22b7cc; thunks 0x22b7d4 / 0x22b7dc / 0x22b7f4 / 0x22b7e4 / 0x22b7ec
    uint32_t Release() override;                             // 0x22e5ca; thunks 0x22e5d2 / 0x22e5da / 0x22e5f2 / 0x22e5e2 / 0x22e5ea

    // ---- cITSAudioResourceManager / cIGZSystemService ----
    bool Init() override;
    bool Shutdown() override;
    bool StartPreloading() override;
    bool ClearInitialized() override;
    void PostSegmentAdded(cIGZUnknown *segment) override;
    bool GetSoundDefinition(uint64_t id, uint32_t iid, void **out) override;
    bool GetHitList(uint64_t id, uint32_t iid, void **out) override;
    bool OpenDecoder(uint64_t id, void **out) override;
    bool CreateDecoder(cIGZUnknown *stream, void **out) override;
    cIGZUnknown *GetPropertySet() override { return mPropertySet.Get(); }

    // ---- cIGZSndEventHandler (+4), cIGZSndUpdateTarget (+8), cIGZMessageTarget2 (+0xc) ----
    bool HandleEvent(cIGZUnknown *event) override; // 0x22c64e
    void Update(uint32_t now) override;            // 0x22e8a3
    bool DoMessage(void *message) override;        // 0x22c741

    // ---- members the original calls (non-virtual, __thiscall on the primary face) ----
    void AddSegmentKeys(cIGZUnknown *segment); // 0x22b97b (hooked: BuildResourceMaps 0x22cf55 calls it)
    // 0x22b7fc: the keys `filter` takes go into `keys` (by id) and onto `preload`; an id already there logs the two names
    // when they differ.
    void RegisterKeys(const cGZPersistResourceKey *first, const cGZPersistResourceKey *last, cIGZPersistResourceKeyFilter *filter,
                      AudioKeyMap *keys, AudioKeyList *preload);
    bool GetKeyName(const cGZPersistResourceKey *key, cIGZString *out); // 0x22e7d4
    bool PreloadSoundDefinition(const cGZPersistResourceKey *key);      // 0x22e38c
    bool PreloadHitList(const cGZPersistResourceKey *key);              // 0x22e1ef
    bool PreloadDataSource(const cGZPersistResourceKey *key);           // 0x22df9d
    bool IsPreloadGroup(const cGZPersistResourceKey *key) const;        // 0x22e6ad

    cIGZMessageTarget2 *MessageTarget() { return this; }

    // +0x00 .. +0x0f: the four interface faces; +0x10 .. +0x2b: cRZSystemService
    AudioRef mPersist;                         // 0x2c the persist system (framework slot 5, class 0x56b906e); its slot 3 is the resource manager
    uint8_t mInitialized;                      // 0x30
    uint8_t mPreloadStarted;                   // 0x31
    uint8_t mUnknown32[2];                     // 0x32 never written
    std::set<uint32_t> mMessageIds;            // 0x34 {0x2df90c05}
    AudioResourceString mString3C;             // 0x3c never used past construction
    uint8_t mByte5C;                           // 0x5c 0
    uint8_t mUnknown5D[3];                     // 0x5d never written
    AudioKeyMap mSoundDefinitionKeys;          // 0x60
    AudioKeyMap mHitListKeys;                  // 0x80
    AudioKeyMap mDataSourceKeys;               // 0xa0
    AudioKeyList mSoundDefinitionPreload;      // 0xc0
    AudioKeyList mHitListPreload;              // 0xc8
    AudioKeyList mDataSourcePreload;           // 0xd0
    uint32_t mPreloadState;                    // 0xd8 0 idle, 1 begin, 3..8 the three passes, 2 end
    AudioKeyList::iterator mPreloadCursor;     // 0xdc
    std::vector<uint32_t> mPreloadGroups;      // 0xe0 the groups whose data sources are preloaded
    AudioCache mSoundDefinitions;              // 0xec
    AudioCache mHitLists;                      // 0x10c
    AudioCache mDataSources;                   // 0x12c
    std::map<uint32_t, AudioRef> mSegments;    // 0x14c (BuildResourceMaps; cleared by Shutdown)
    cTSAudioKeyFilterByTypesAndGroups mDataSourceFilter;    // 0x154
    cTSAudioKeyFilterByTypesAndGroups mSoundDefinitionFilter; // 0x16c
    cTSAudioKeyFilterByTypesAndGroups mHitListFilter;       // 0x184
    uint32_t mSoundDefinitionsLoaded;          // 0x19c
    uint32_t mHitListsLoaded;                  // 0x1a0
    uint32_t mDataSourcesLoaded;               // 0x1a4
    AudioRef mPropertySet;                     // 0x1a8 a cGZPropertySetString (slot 12)

private:
    bool GetCached(AudioCache &cache, AudioKeyMap &keys, uint64_t id, uint32_t iid, void **out, uint32_t missingLine,
                   uint32_t missingFormat, uint32_t failedLine, uint32_t failedFormat);
    bool Preload(AudioCache &cache, uint32_t &loaded, const cGZPersistResourceKey *key, uint32_t cachedLine,
                 uint32_t failedLine);
    void PreloadPass(AudioKeyList &list, uint32_t nextState, int kind);
};

static_assert(sizeof(cTSAudioResourceManager) == cTSAudioResourceManager::kSize, "cTSAudioResourceManager size");
static_assert(offsetof(cTSAudioResourceManager, mPersist) == 0x2c, "mPersist");
static_assert(offsetof(cTSAudioResourceManager, mInitialized) == 0x30, "mInitialized");
static_assert(offsetof(cTSAudioResourceManager, mMessageIds) == 0x34, "mMessageIds");
static_assert(offsetof(cTSAudioResourceManager, mString3C) == 0x3c, "mString3C");
static_assert(offsetof(cTSAudioResourceManager, mByte5C) == 0x5c, "mByte5C");
static_assert(offsetof(cTSAudioResourceManager, mSoundDefinitionKeys) == 0x60, "mSoundDefinitionKeys");
static_assert(offsetof(cTSAudioResourceManager, mDataSourceKeys) == 0xa0, "mDataSourceKeys");
static_assert(offsetof(cTSAudioResourceManager, mSoundDefinitionPreload) == 0xc0, "mSoundDefinitionPreload");
static_assert(offsetof(cTSAudioResourceManager, mPreloadState) == 0xd8, "mPreloadState");
static_assert(offsetof(cTSAudioResourceManager, mPreloadCursor) == 0xdc, "mPreloadCursor");
static_assert(offsetof(cTSAudioResourceManager, mPreloadGroups) == 0xe0, "mPreloadGroups");
static_assert(offsetof(cTSAudioResourceManager, mSoundDefinitions) == 0xec, "mSoundDefinitions");
static_assert(offsetof(cTSAudioResourceManager, mDataSources) == 0x12c, "mDataSources");
static_assert(offsetof(cTSAudioResourceManager, mSegments) == 0x14c, "mSegments");
static_assert(offsetof(cTSAudioResourceManager, mDataSourceFilter) == 0x154, "mDataSourceFilter");
static_assert(offsetof(cTSAudioResourceManager, mHitListFilter) == 0x184, "mHitListFilter");
static_assert(offsetof(cTSAudioResourceManager, mSoundDefinitionsLoaded) == 0x19c, "mSoundDefinitionsLoaded");
static_assert(offsetof(cTSAudioResourceManager, mPropertySet) == 0x1a8, "mPropertySet");

// The hooks' entry points.
struct cTSAudioResourceManagerAdapters {
    static cTSAudioResourceManager *__fastcall Construct(void *memory, void *edx); // 0x22b1d9 (tests; not hooked)
};
