#pragma once
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// cTSObjectFolder: the objects of the loaded lot and of the neighbourhood (the global objects), by object id, with the
// lot's save/load and the lot-import work around them. RTTI cTSObjectFolder (type descriptor 0x10a62e8), bases
// cITSBehaviorFinder at +0, cITSObjectFolder at +4, cIGZMessageTarget2 at +8. Vtables 0xdccf50 (10 slots), 0xdccf7c (58
// slots), 0xdcd068 (4 slots). 0xa4 bytes. Lane IK. RVAs are 1.18.0.187.
//
// Standalone shape (docs/standalone-shape.md): a real C++ class; the compiler emits its three vtables. The game creates the
// folder only through the factory 0x4d3280 (operator new 0xa4 + the constructor 0x4cef20; registered as class 0x9b14358f
// by the COM director table, src/rz/COMDirectorData.cpp), which is hooked to Create, so every folder carries our vtables. The
// game's vtables are referenced only by the constructor and the destructor 0x4cf710 (reached only from the deleting
// destructor, slot 5 of the primary face).
//
// The objects it holds are game objects (cTSObjectSelector, vtable 0xdcd1a4, made by 0x4d9870, as lane MB found from the
// save table: its slots are ifaces::cITSObjectSelector; "cTSObject" below is the older name): slot 4 their data (slot 3 GetData(index),
// slot 8 IsDeleted, slot 9, slot 11 Set...), slot 8 name, slot 18 the object id, slot 28 the id to file under, slot 38,
// slot 57 the save index, slot 58 SetSaveIndex, slot 63 / 62 and so on; the .cpp names each use.

// A counted object pointer (the game's cRZAutoRefCount<cIGZUnknown>, inlined everywhere): AddRef (slot 1) when a pointer is
// taken in, Release (slot 2) when it is dropped. No move constructor, like the game's: a vector copies (AddRef + Release)
// when it grows.
class FolderRef {
public:
    FolderRef() noexcept = default;
    explicit FolderRef(void *object);
    FolderRef(const FolderRef &other) : FolderRef(other.mObject) {}
    FolderRef &operator=(const FolderRef &other)
    {
        Assign(other.mObject);
        return *this;
    }
    ~FolderRef();
    // AddRef the new object, store it, then Release the old one; nothing for the same pointer.
    void Assign(void *object);
    // Releases what is held (the slot nulled first) and gives the slot, as the out argument of a getter.
    void **Reset();
    void *Get() const { return mObject; }

private:
    void *mObject = nullptr;
};
static_assert(sizeof(FolderRef) == 4);

using FolderObjects = std::vector<FolderRef>; // the game's vector of counted pointers (operator new)
static_assert(sizeof(FolderObjects) == 12);

// An ordered set of counted pointers (by address; erasing a node releases it, 0x16f520).
struct FolderRefLess {
    bool operator()(const FolderRef &a, const FolderRef &b) const { return a.Get() < b.Get(); }
};
using FolderRefSet = std::set<FolderRef, FolderRefLess>;
static_assert(sizeof(FolderRefSet) == 8);

// The id map's hash (inline in 0x4cebd0 and the other finders): a 32-bit integer mix of the object id.
struct FolderIdHash {
    size_t operator()(uint32_t id) const noexcept
    {
        uint32_t h = id * 0x1001;
        h = ((h >> 22) ^ h) * 0x11;
        h = ((h >> 9) ^ h) * 0x401;
        h = ((h >> 2) ^ h) * 0x81;
        return (h >> 12) ^ h;
    }
};
// object id -> object (node 0x10: links, id at +8, the counted pointer at +0xc; erasing a node releases it, 0x4ceaf0).
using FolderIdMap = std::unordered_map<uint32_t, FolderRef, FolderIdHash>;
static_assert(sizeof(FolderIdMap) == 0x20);

// A neighbourhood name as the game's maps key it: a cRZString (vtable 0xd9b470, std::string, count), compared without case
// (0x4a62e0 / 0x471480: _stricmp). Built and destroyed through the hook-shaped cRZString members.
struct FolderName {
    union {
        cRZString s;
    };
    FolderName() { s.Construct(); }
    explicit FolderName(const cIGZString &text) { s.Construct(text); }
    FolderName(const FolderName &other) { s.Construct(reinterpret_cast<const cIGZString &>(other.s)); }
    FolderName &operator=(const FolderName &) = delete;
    ~FolderName() { s.Destruct(); }
};
static_assert(sizeof(FolderName) == 0x20);
struct FolderNameLess {
    bool operator()(const FolderName &a, const FolderName &b) const;
};
// name -> the neighbourhood's global objects (node 0x3c) / their id map (node 0x50).
using FolderGroupObjects = std::map<FolderName, FolderObjects, FolderNameLess>;
using FolderGroupIds = std::map<FolderName, FolderIdMap, FolderNameLess>;
static_assert(sizeof(FolderGroupObjects) == 8 && sizeof(FolderGroupIds) == 8);

// The lot package descriptions slot 57 copies in (vectors over the allocator service; 0x50- and 0x58-byte elements, copied
// and destroyed by the library code the compiler instantiates for these types).
using FolderString = std::basic_string<char, std::char_traits<char>, GZServiceAllocator<char>>;
struct FolderPackageEntry {
    uint32_t mA;
    uint32_t mB;
    FolderString mText[3];
};
static_assert(sizeof(FolderPackageEntry) == 0x50);
struct FolderPackageInfo {
    FolderString mName;
    bool mFlag;
    std::vector<FolderString, GZServiceAllocator<FolderString>> mLists[4];
    std::vector<FolderPackageEntry, GZServiceAllocator<FolderPackageEntry>> mEntries;
};
static_assert(sizeof(FolderPackageInfo) == 0x58);
using FolderPackageInfos = std::vector<FolderPackageInfo, GZServiceAllocator<FolderPackageInfo>>;

// The object sets slots 25, 26 and 55 fill (the caller's): ordered pointers, nodes from the allocator service (0x204140).
using FolderObjectSet = std::set<void *, std::less<void *>, GZServiceAllocator<void *>>;

// The primary face (10 slots). Slot 5 is the virtual destructor (the deleting destructor 0x4cfbf0).
class cITSBehaviorFinder : public cIGZUnknown {
public:
    virtual void WriteObjectRef(void *stream, void **object) = 0; // slot 3
    virtual void ReadObjectRef(void *stream, void **out) = 0;     // slot 4
    virtual ~cITSBehaviorFinder() = default;                      // slot 5
    virtual void DetachObject(void *object) = 0;                  // slot 6
    virtual void AttachObjectResource(void *object) = 0;          // slot 7
    virtual void ReloadObjectResource(void *object) = 0;          // slot 8
    virtual void LinkToContainer(void *object) = 0;               // slot 9
};

// The folder face (+4, 58 slots).
class cITSObjectFolder : public cIGZUnknown {
public:
    virtual void *GetBehaviorFinder() = 0;                                      // slot 3
    virtual void Init() = 0;                                                    // slot 4
    virtual void Shutdown() = 0;                                                // slot 5
    virtual void *GetFrameworkObject() = 0;                                     // slot 6
    virtual uint32_t GetSelectedGlobalValue() = 0;                              // slot 7
    virtual void WriteObjectRef(void *stream, void **object) = 0;               // slot 8
    virtual void ReadObjectRef(void *stream, void **out) = 0;                   // slot 9
    virtual int32_t GetObjectCount() = 0;                                       // slot 10
    virtual int32_t GetAllObjectCount() = 0;                                    // slot 11
    virtual int32_t GetPortalCount() = 0;                                       // slot 12
    virtual void *GetObjectAt(int32_t index) = 0;                               // slot 13
    virtual void *FindObject(uint32_t id) = 0;                                  // slot 14
    virtual void *FindObjectByObjectId(int32_t id) = 0;                         // slot 15
    virtual void *FindObjectByName(const char *name) = 0;                       // slot 16
    virtual void *FindObjectBySaveIndex(int32_t index) = 0;                     // slot 17
    virtual void *GetLotObject() = 0;                                           // slot 18
    virtual void *GetPortalAt(uint32_t index) = 0;                              // slot 19
    virtual void *FindContainedAt(void *object, int32_t x, int32_t y, int32_t slot) = 0; // slot 20
    virtual void *FindLeadContained(void *object) = 0;                          // slot 21
    virtual void *FindFirstMultitileMember(void *object) = 0;                   // slot 22
    virtual void *FindNthMultitileMember(void *object, int32_t n) = 0;          // slot 23
    virtual void LoadGlobals() = 0;                                             // slot 24
    virtual bool LoadGroupObjects(uint32_t group, FolderObjectSet *loaded, bool notify) = 0; // slot 25
    virtual bool LoadPackageObjects(void *package, FolderObjectSet *loaded) = 0; // slot 26
    virtual bool RemoveObject(void *object) = 0;                                // slot 27
    virtual void UnloadObjectResources(void *object) = 0;                       // slot 28
    virtual void ReloadObjectResources(void *object) = 0;                       // slot 29
    virtual void UnloadAllResources() = 0;                                      // slot 30
    virtual void ReloadAllResources() = 0;                                      // slot 31
    virtual void ReadSaveTable(void *package) = 0;                              // slot 32
    virtual void WriteSaveTable(void *package) = 0;                             // slot 33
    virtual void ClearSaveIndices() = 0;                                        // slot 34
    virtual bool AnyLotScriptBusy() = 0;                                        // slot 35
    virtual void *FindLotScript(int32_t id) = 0;                                // slot 36
    virtual void *FindObject37() = 0;                                           // slot 37
    virtual void *FindObject38() = 0;                                           // slot 38 (and 42)
    virtual void *FindObject39() = 0;                                           // slot 39
    virtual void *FindObject40() = 0;                                           // slot 40
    virtual void *FindObject41() = 0;                                           // slot 41
    virtual void *FindObject42() = 0;                                           // slot 42
    virtual bool IsObject39To41(void *object) = 0;                              // slot 43
    virtual void *FindObject44() = 0;                                           // slot 44
    virtual void *FindObject45() = 0;                                           // slot 45
    virtual void *FindObject46() = 0;                                           // slot 46
    virtual void *FindObject47() = 0;                                           // slot 47
    virtual void *FindObject48() = 0;                                           // slot 48
    virtual bool IsObject44Or45(void *object) = 0;                              // slot 49
    virtual bool IsObject46(void *object) = 0;                                  // slot 50
    virtual bool IsObject48(void *object) = 0;                                  // slot 51
    virtual bool ImportLotObjects(uint32_t riid, void **out, uint32_t packageArg, void *lotObject, cIGZString *directory,
                                  cIGZString *name, cIGZString *displayName, bool touchPackage, bool keepContainer,
                                  uint32_t owner) = 0; // slot 52
    virtual bool AddLotObjectCopy(void *object, cIGZString *name) = 0;          // slot 53
    virtual void CollectTagged(void *list) = 0;                                 // slot 54
    virtual void CollectByAttribute(int32_t value, FolderObjectSet *out) = 0;  // slot 55
    virtual uint32_t GetMessageCount() = 0;                                     // slot 56
    virtual void GetPackageInfos(FolderPackageInfos *out) = 0;                 // slot 57
};

class cTSObjectFolder : public cITSBehaviorFinder, public cITSObjectFolder, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kClassId = 0x9b14358f;
    static constexpr uint32_t kFolderIid = 0x9b14358e;
    static constexpr uint32_t kFinderIid = 0x1b26949e;

    // 0x4d3280 (__cdecl): operator new 0xa4 and the constructor; the folder face (+4) of the new folder, null when the
    // allocation failed.
    static cITSObjectFolder *__cdecl Create();
    cTSObjectFolder();           // 0x4cef20
    ~cTSObjectFolder() override; // 0x4cf710; slot 5 (0x4cfbf0) is the compiler's scalar deleting destructor
    cTSObjectFolder(const cTSObjectFolder &) = delete;
    cTSObjectFolder &operator=(const cTSObjectFolder &) = delete;

    // ---- cIGZUnknown (the +4 / +8 faces' QI and Release are the compiler's thunks 0x4d4c19 / 0x4d4c21, 0x4d55f5 / 0x4d55fd;
    // their AddRef thunks the folded 0x1f4daf / 0x1f4db7) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x4d4bd0
    uint32_t AddRef() override { return ++mRefCount; }      // folded 0x15e520
    uint32_t Release() override;                            // 0x4d55e0

    // ---- cITSBehaviorFinder (slots 3 and 4 also fill the folder face's slots 8 and 9: thunks 0x4d716c, 0x4d5356) ----
    void WriteObjectRef(void *stream, void **object) override; // 0x4d7130
    void ReadObjectRef(void *stream, void **out) override;     // 0x4d5240
    void DetachObject(void *object) override;                  // 0x4d21e0
    void AttachObjectResource(void *object) override;          // 0x4d5430
    void ReloadObjectResource(void *object) override;          // 0x4d5360
    void LinkToContainer(void *object) override;               // 0x4d6c70

    // ---- cITSObjectFolder ----
    void *GetBehaviorFinder() override { return static_cast<cITSBehaviorFinder *>(this); } // folded 0x142700
    void Init() override;                                    // 0x4d3cb0
    void Shutdown() override;                                // 0x4d1ea0
    void *GetFrameworkObject() override { return mFramework.Get(); } // folded 0x149cf0
    uint32_t GetSelectedGlobalValue() override;              // 0x4d3690
    int32_t GetObjectCount() override;                       // 0x4d09c0
    int32_t GetAllObjectCount() override;                    // 0x4d09e0
    int32_t GetPortalCount() override;                       // 0x4d0a10
    void *GetObjectAt(int32_t index) override;               // 0x4d3570
    void *FindObject(uint32_t id) override;                  // 0x4d37c0
    void *FindObjectByObjectId(int32_t id) override;         // 0x4d3710
    void *FindObjectByName(const char *name) override;       // 0x4d3870
    void *FindObjectBySaveIndex(int32_t index) override;     // 0x4d3910
    void *GetLotObject() override;                           // 0x4d3360
    void *GetPortalAt(uint32_t index) override;              // 0x4d3b30
    void *FindContainedAt(void *object, int32_t x, int32_t y, int32_t slot) override; // 0x4d3b90
    void *FindLeadContained(void *object) override;          // 0x4d3380
    void *FindFirstMultitileMember(void *object) override;   // 0x4d3450
    void *FindNthMultitileMember(void *object, int32_t n) override; // 0x4d35c0
    void LoadGlobals() override;                             // 0x4d64f0
    bool LoadGroupObjects(uint32_t group, FolderObjectSet *loaded, bool notify) override; // 0x4d41a0
    bool LoadPackageObjects(void *package, FolderObjectSet *loaded) override; // 0x4d43a0
    bool RemoveObject(void *object) override;                // 0x4d5680
    void UnloadObjectResources(void *object) override;       // 0x4d6ee0
    void ReloadObjectResources(void *object) override;       // 0x4d5b70
    void UnloadAllResources() override;                      // 0x4d6d40
    void ReloadAllResources() override;                      // 0x4d5930
    void ReadSaveTable(void *package) override;              // 0x4d46d0
    void WriteSaveTable(void *package) override;             // 0x4d49a0
    void ClearSaveIndices() override;                        // 0x4d4670
    bool AnyLotScriptBusy() override;                        // 0x4d3c80
    void *FindLotScript(int32_t id) override;                // 0x4d3530
    void *FindObject37() override;                           // 0x4d3b80
    void *FindObject38() override;                           // 0x4d36e0
    void *FindObject39() override;                           // 0x4d3b60
    void *FindObject40() override;                           // 0x4d3340
    void *FindObject41() override;                           // 0x4d3330
    void *FindObject42() override;                           // 0x4d36e0 (the body of slot 38)
    bool IsObject39To41(void *object) override;              // 0x4d40f0
    void *FindObject44() override;                           // 0x4d3b70
    void *FindObject45() override;                           // 0x4d3700
    void *FindObject46() override;                           // 0x4d36f0
    void *FindObject47() override;                           // 0x4d32f0
    void *FindObject48() override;                           // 0x4d3370
    bool IsObject44Or45(void *object) override;              // 0x4d4130
    bool IsObject46(void *object) override;                  // 0x4d4180
    bool IsObject48(void *object) override;                  // 0x4d4160
    bool ImportLotObjects(uint32_t riid, void **out, uint32_t packageArg, void *lotObject, cIGZString *directory,
                          cIGZString *name, cIGZString *displayName, bool touchPackage, bool keepContainer,
                          uint32_t owner) override; // 0x4d0a20
    bool AddLotObjectCopy(void *object, cIGZString *name) override; // 0x4d18d0
    void CollectTagged(void *list) override;                 // 0x4d4560
    void CollectByAttribute(int32_t value, FolderObjectSet *out) override; // 0x4d3a00
    uint32_t GetMessageCount() override { return mMessageCount; } // folded 0x24b400
    void GetPackageInfos(FolderPackageInfos *out) override; // 0x4d3300

    // ---- cIGZMessageTarget2 (+8) ----
    bool DoMessage(void *message) override; // 0x4d22b0

    // Non-virtual members (called by the slots).
    void ReleaseLotScripts();       // 0x4d5610: (loaded only) every lot script released, the list emptied
    void ClearObjects();            // 0x4d1f90: every object, portal, the lot object and the global objects detached and dropped
    void DetachObjectFully(void *object); // 0x4d2210: slots 19, 20, 5, 55, 23 (0) and 31 (0), then the object's portal's five
    // Part of DoMessage (message 0xc5, a neighbourhood selected whose id map is still empty): scans the neighbourhood's
    // directory for packages, opens each (catalogue slot 21), loads the OBJD records of their groups (slot 25), and closes
    // and deletes the files of the groups that gave none.
    void SelectNeighborhoodFirstTime(const cRZString &name);

    uint32_t mRefCount = 0;                 // +0x0c
    bool mLoaded = false;                   // +0x10 Init .. Shutdown
    bool mRegistered = false;               // +0x11 set at the end of the constructor; Shutdown clears it
    bool mImporting = false;                // +0x12 (0x4d5d00)
    bool mLoadingGlobals = false;           // +0x13 (0x4d76a0)
    FolderRef mFramework;                   // +0x14 the lot's framework object (Init: class 0x0b54e37d)
    FolderObjects mLotScripts;              // +0x18
    FolderGroupObjects mGroupObjects;       // +0x24 the global objects of each neighbourhood
    FolderIdMap mObjectsById;               // +0x2c
    FolderGroupIds mGroupIds;               // +0x4c their id maps
    FolderObjects mObjects;                 // +0x54 the lot's objects
    FolderObjects mPortals;                 // +0x60
    FolderRef mLotObject;                   // +0x6c (Init: class 0x5b12bf90)
    FolderObjects *mGlobals = nullptr;      // +0x70 the current neighbourhood's entry of mGroupObjects
    FolderIdMap *mGlobalIds = nullptr;      // +0x74 ... and of mGroupIds
    FolderPackageInfos mPackageInfos;       // +0x78
    FolderRefSet mCachedResources;          // +0x84 resources LoadGlobals drops (persist slot 30) and releases
    std::vector<uint32_t> mSaveIndex;       // +0x8c save index -> object id
    std::set<uint32_t> mMessageIds;         // +0x98
    uint32_t mMessageCount = 0;             // +0xa0 messages 0x8ecc1b6d seen
};

static_assert(sizeof(cTSObjectFolder) == 0xa4);
static_assert(offsetof(cTSObjectFolder, mRefCount) == 0x0c && offsetof(cTSObjectFolder, mLoaded) == 0x10);
static_assert(offsetof(cTSObjectFolder, mImporting) == 0x12 && offsetof(cTSObjectFolder, mFramework) == 0x14);
static_assert(offsetof(cTSObjectFolder, mLotScripts) == 0x18 && offsetof(cTSObjectFolder, mGroupObjects) == 0x24);
static_assert(offsetof(cTSObjectFolder, mObjectsById) == 0x2c && offsetof(cTSObjectFolder, mGroupIds) == 0x4c);
static_assert(offsetof(cTSObjectFolder, mObjects) == 0x54 && offsetof(cTSObjectFolder, mPortals) == 0x60);
static_assert(offsetof(cTSObjectFolder, mLotObject) == 0x6c && offsetof(cTSObjectFolder, mGlobals) == 0x70);
static_assert(offsetof(cTSObjectFolder, mGlobalIds) == 0x74 && offsetof(cTSObjectFolder, mPackageInfos) == 0x78);
static_assert(offsetof(cTSObjectFolder, mCachedResources) == 0x84 && offsetof(cTSObjectFolder, mSaveIndex) == 0x8c);
static_assert(offsetof(cTSObjectFolder, mMessageIds) == 0x98 && offsetof(cTSObjectFolder, mMessageCount) == 0xa0);
