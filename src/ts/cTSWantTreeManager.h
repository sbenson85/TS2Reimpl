#pragma once
// cTSWantTreeManager (lane IV): the want simulator's definitions and trees (cTSWantSimulator +0x24, slot 23 asks it for a
// definition by guid). 0x44 bytes, /O2. Init loads Wants.package and WantTrees.package from the game's data folders (plus
// the xml folders, with directory watchers that post a reload), registers itself as the persist factory of want definitions
// (cTSWantDefinition, type 0xed7d7b4d), reads every definition record into the definition map and every tree record into
// the tree map (cTSWantTreeLoader::LoadTree). A watcher's change message posts 0xdaba8b9, which reloads everything.
//
// Faces (RTTI order): +0 cITSWantTreeManager (vtable 0xdd4210, 10 slots), +4 cIGZPersistResourceFactory (0xdd423c, 7 slots;
// slot 3 the factories' shared CreateInstance 0x5522d0), +8 cIGZMessageTarget2 (0xdd425c). No destructor slot: Release
// destroys inline (sized delete 0x44).
//
// Standalone shape (docs/standalone-shape.md): the constructor 0x551c40 (hooked: placement new) is the only creation site
// (cTSWantSimulator::Init 0x4905e0 news it); the vtables are referenced only by the constructor and Release, so every
// manager carries ours.
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZPersistResourceFactory.h"
#include "gz/cIGZUnknown.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>

class cRZString;
class cGZPropertySetString;

// A counted interface pointer as the manager's containers hold them (list nodes 0xc: next, prev, pointer; map nodes 0x18: the
// key at +0x10, the pointer at +0x14). Erasing releases (0x1d98d0 / 0x46f6d0 for the lists, 0x1fc790 for the maps).
using WantTreeRef = cRZAutoRefCount<cIGZUnknown>;
using WantTreeRefList = std::list<WantTreeRef>;
using WantTreeRefMap = std::map<uint32_t, WantTreeRef>;
static_assert(sizeof(WantTreeRefList) == 8 && sizeof(WantTreeRefMap) == 8, "MSVC 14.40 list / map headers");

// cITSWantTreeManager (iid 0xad7dc916): slots 3-9.
class cITSWantTreeManager : public cIGZUnknown {
public:
    virtual bool Init() = 0;                          // slot 3
    virtual bool Shutdown() = 0;                      // slot 4
    virtual cIGZUnknown *GetDefinition(uint32_t guid) = 0; // slot 5 (ret 4): not counted
    virtual WantTreeRefMap *GetTrees() = 0;           // slot 6 (folded 0x49d5c0: lea eax, [ecx + 0x34])
    virtual WantTreeRefMap *GetDefinitions() = 0;     // slot 7 (folded 0x44c520: lea eax, [ecx + 0x3c])
    virtual bool LoadTreesFromFile(uint32_t unused) = 0; // slot 8 (ret 4)
    virtual bool Reload() = 0;                        // slot 9
};

namespace wanttreemgr {
constexpr uint32_t kFactoryFace = 4;
constexpr uint32_t kMessageFace = 8;
} // namespace wanttreemgr

class cTSWantTreeManager : public cITSWantTreeManager, public cIGZPersistResourceFactory, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kVTable = 0xdd4210;
    static constexpr uint32_t kFactoryVTable = 0xdd423c;
    static constexpr uint32_t kMessageVTable = 0xdd425c;
    static constexpr uint32_t kSize = 0x44;
    static constexpr uint32_t kIid = 0xad7dc916;            // cITSWantTreeManager
    static constexpr uint32_t kFactoryIid = 0xa56b8f17;     // cIGZPersistResourceFactory
    static constexpr uint32_t kMessageTargetIid = 0x452294aa; // cIGZMessageTarget2
    static constexpr uint32_t kFactoryId = 0xcd7dc919;      // the resource manager's factory id (slot 20 / 21)
    static constexpr uint32_t kDefinitionType = 0xed7d7b4d; // cTSWantDefinition's class id and record type
    static constexpr uint32_t kDefinitionIid = 0xed7d7b4e;  // cITSWantDefinition
    static constexpr uint32_t kTreeType = 0x6d814afe;       // want tree records (XML)
    static constexpr uint32_t kReloadMessage = 0xdaba8b9;   // posted on a watcher's change; reloads everything
    static constexpr uint32_t kChangedMessage = 0x6ae76406; // a directory watcher's message: files changed
    // Data folders (cTSAppCommonImpl slot 4 / slot 5 option ids) and the directory segments' group ids.
    static constexpr uint32_t kPackageFolder = 0x4dd1e035;  // the folder Wants.package and WantTrees.package are in
    static constexpr uint32_t kWantsXmlFolder = 0x0d7eb18b;
    static constexpr uint32_t kTreesXmlFolder = 0x0d7ebaf1;
    static constexpr uint32_t kWantsXmlGroup = 0x4dd1cdc1;
    static constexpr uint32_t kTreesXmlGroup = 0x4dd1cdc4;

    cTSWantTreeManager(); // 0x551c40
    cTSWantTreeManager(const cTSWantTreeManager &) = delete;
    cTSWantTreeManager &operator=(const cTSWantTreeManager &) = delete;

    // ---- cIGZUnknown (all faces) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0x555490; thunks 0x5554fd (+4), 0x555505 (+8)
    uint32_t AddRef() override;                             // folded 0x15e520; thunks folded 0x1f4daf / 0x1f4db7
    uint32_t Release() override;                            // 0x556160; thunks 0x556223 (+4), 0x55622b (+8)

    // ---- cITSWantTreeManager ----
    bool Init() override;                                   // 0x5545f0
    bool Shutdown() override;                               // 0x5562a0
    cIGZUnknown *GetDefinition(uint32_t guid) override;     // 0x5525a0
    WantTreeRefMap *GetTrees() override { return &mTrees; }             // folded 0x49d5c0
    WantTreeRefMap *GetDefinitions() override { return &mDefinitions; } // folded 0x44c520
    bool LoadTreesFromFile(uint32_t unused) override;       // 0x554a50 (still the game's body: forwarded)
    bool Reload() override;                                 // 0x556510

    // ---- cIGZPersistResourceFactory (+4) ----
    bool CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4, uint32_t a5) override; // folded 0x5522d0
    bool CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5) override; // 0x552370
    bool Read(cIGZUnknown *resource, cIGZUnknown *record) override;  // 0x555f30
    bool Write(cIGZUnknown *resource, cIGZUnknown *record) override; // 0x557240

    // ---- cIGZMessageTarget2 (+8) ----
    bool DoMessage(void *message) override;                 // 0x552430

    // ---- the loading helpers Init uses (__cdecl free functions in the original) ----
    // 0x496ef0: `name` in the folder of option `folder` opened as a package (when the file exists), added to the resource
    // manager and appended (counted) to `segments`. True when one was added.
    static bool AddPackage(uint32_t folder, cRZString *name, WantTreeRefList *segments);
    // 0x496c00: the folder of option `folder`, when it exists, opened as a directory segment of group `group` holding the
    // files of extension `extension` as records of `type`; added and appended as AddPackage does.
    static bool AddDirectory(uint32_t folder, uint32_t group, cRZString *extension, uint32_t type, WantTreeRefList *segments);
    // 0x497230: a directory watcher on each folder of option `folder` (cTSAppCommonImpl slot 5, last first), its messages to
    // `target`, appended (counted, null too) to `watchers` when given.
    static void WatchDirectories(uint32_t folder, cIGZMessageTarget2 *target, WantTreeRefList *watchers);
    // 0x555280: the definition resource of `key` (resource manager slot 3 as cITSWantDefinition) stored under its id.
    static void LoadDefinition(const void *key, cTSWantTreeManager *manager);
    // Read / Write's temporary property set: a new cGZPropertySetResourceString (the original: operator new 0x50 and its
    // constructor 0x5519c0), handed out by its set face, not counted. Out of line so the tests can stand in for it.
    static cGZPropertySetString *NewPropertySet();
    // cTSWantSimulator::Init's `new cTSWantTreeManager` (the original: operator new 0x44 and the constructor 0x551c40).
    // Out of line so the simulator's tests can stand in for it.
    static cTSWantTreeManager *Create();

    // +0x00, +0x04, +0x08: the three faces' vtables
    int32_t mRefCount = 0;            // 0x0c
    bool mInitialized = false;        // 0x10 Init sets, Shutdown clears
    bool mReloadPending = false;      // 0x11 a reload message is posted and not handled yet
    WantTreeRefList mWantPackages;    // 0x14 segments: Wants.package and the wants xml folder
    WantTreeRefList mTreePackages;    // 0x1c segments: WantTrees.package and the trees xml folder
    WantTreeRefList mWantWatchers;    // 0x24 directory watchers on the wants xml folders
    WantTreeRefList mTreeWatchers;    // 0x2c directory watchers on the trees xml folders
    WantTreeRefMap mTrees;            // 0x34 tree id -> root cTSWantNode (filled by cTSWantTreeLoader)
    WantTreeRefMap mDefinitions;      // 0x3c guid -> cTSWantDefinition (its cITSWantDefinition face)

private:
    ~cTSWantTreeManager(); // inline in Release (not virtual: no destructor slot)
    void ShutdownSegments(WantTreeRefList &segments, void *resources);
};

static_assert(sizeof(cTSWantTreeManager) == cTSWantTreeManager::kSize, "cTSWantTreeManager size");
static_assert(offsetof(cTSWantTreeManager, mRefCount) == 0x0c, "mRefCount");
static_assert(offsetof(cTSWantTreeManager, mInitialized) == 0x10, "mInitialized");
static_assert(offsetof(cTSWantTreeManager, mReloadPending) == 0x11, "mReloadPending");
static_assert(offsetof(cTSWantTreeManager, mWantPackages) == 0x14, "mWantPackages");
static_assert(offsetof(cTSWantTreeManager, mTreeWatchers) == 0x2c, "mTreeWatchers");
static_assert(offsetof(cTSWantTreeManager, mTrees) == 0x34, "mTrees");
static_assert(offsetof(cTSWantTreeManager, mDefinitions) == 0x3c, "mDefinitions");

// The old entry points (RVA hooks): the constructor (the class's switch) and the free helpers. The slot bodies are reached
// only through the vtables (ours on every manager while the constructor hook is on).
struct cTSWantTreeManagerAdapters {
    using Self = cTSWantTreeManager;
    static Self *__fastcall Construct(void *memory, void *); // 0x551c40
};
