#pragma once
// cTSMaterialCatalog: Create-a-Sim's catalog of model materials (RTTI name; service id 0xac6965bc, priority 0xffd23940;
// cITSMaterialCatalog iid 0x6c69658c). 0x44 bytes, /O2. Lane JX (the whole class, standalone shape).
//
// What it holds: the material entries (the 0x4c697e55 face of the resources of type 0x4c697e5a, cTSModelMaterialEntry) by
// group and name: mGroups maps a group (the entry's slot 4) to a heap MaterialNameMap, which maps the entry's name (its slot 5,
// lowercased and ending in "_cres") to a heap MaterialList of counted entries. mNames maps the same names to the list last
// filed under them, across groups. Each list is owned by its group's name map (ClearAll frees both); mNames only points.
// Entries arrive through AddEntry (slot 10), from Load (slot 5: every key of type 0x4c697e5a the resource manager has) and
// LoadSegment (slot 6: one segment's keys), counted in mCount.
//
// Init registers the material entry factory, opens every content directory's "materials.package" as the default segment
// (OpenDefaultSegments 0xd08060, through slots 11 / 12) and subscribes the message target face to message 0xcde2cc2d, on
// which DoMessage reloads (slot 5).
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (vtable 0xebab54, 13 slots), +4 cRZUnknownMT (0xebab8c, 6),
// +0x1c cITSMaterialCatalog (0xebaba8, 13), +0x20 cIGZMessageTarget2 (0xebabe0, 4). The original's interface slot bodies
// take `this` as their face; ours are members (the compiler adjusts).
//
// Standalone shape (docs/standalone-shape.md): the creator 0xd06930 (registered by address by cTSCASCOMDirector) is the
// only caller of the constructor 0xd04bd0; the vtables are referenced only by the constructor and the destructor 0xd04e10.
// Both hooks on: every catalog carries our vtables.
#include "cas/cTSSkinCatalog.h" // SkinKey (the resource key), SkinRef
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSCatalog.h" // cIGZPersistResourceKeyFilter, NameLessIgnoringCase

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>

using MaterialKey = nTSCASCore::SkinKey;
// A counted object (the default segment, a held resource): AddRef / Release through slots 1 / 2.
using MaterialRef = nTSCASCore::SkinRef;
// The entries filed under one name (counted; storage from the allocator service: growth 0xcb6f00, destruction 0x1f1f70).
using MaterialList = ServiceRefVector;
// name -> list (node 0x2c: the name at +0x10, the list at +0x28; operator[] 0xd04f60 orders by _stricmp).
using MaterialNameMap = std::map<std::string, MaterialList *, NameLessIgnoringCase>;
// group -> its name map (node 0x18; operator[] 0x1d2180).
using MaterialGroupMap = std::map<uint32_t, MaterialNameMap *>;
static_assert(sizeof(MaterialNameMap) == 8 && sizeof(MaterialGroupMap) == 8 && sizeof(std::set<uint32_t>) == 8, "MSVC 14.40 trees");

// cTSMaterialFilter (type descriptor 0x10c8eb4, vtable 0xebac30, 8 bytes): the key filter Load and LoadSegment hand out: it
// takes the keys of type 0x4c697e5a. Built only by those two (operator new 8, the vtable, count 0, AddRef), so with them ours
// it is only ever ours.
class cTSMaterialFilter : public cIGZPersistResourceKeyFilter {
public:
    static constexpr uint32_t kInterfaceId = 0x8865cd89;

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, folded 0xb7b970
    uint32_t AddRef() override;                              // slot 1, folded 0x148fb0
    uint32_t Release() override;                             // slot 2, folded 0x72c480
    bool IsKeyIncluded(const uint32_t *key) override;        // slot 3, 0xd06b70
    ~cTSMaterialFilter() override = default;                 // slot 4, 0xd050e0 (the scalar deleting destructor)

    uint32_t mRefCount = 0; // +4
};
static_assert(sizeof(cTSMaterialFilter) == 8, "cTSMaterialFilter layout");

// cITSMaterialCatalog (iid 0x6c69658c): slots 3-12. An "entry" is a material entry's 0x4c697e55 face (slot 3 its resource,
// 4 its group, 5 its name and 6 its second name as allocator-service strings); a "resource" a persist resource (slot 3 its
// key, 4 sets it, 12 its kind).
class cITSMaterialCatalog : public cIGZUnknown {
public:
    // 3 (0xd05140): files a resource: its key completed (a new instance from the COM object when it has none, group
    // 0x8c6965ac when it has none, type 0x4c697e5a when its kind is 1; a key still without a type fails), then saved
    // through the resource manager into `segment`, the segment holding its key, or the default one.
    virtual bool AddMaterial(void *resource, void *segment) = 0;
    // 4 (0xd05ec0): takes an entry out of its list, then deletes its resource's record from the segment holding it.
    virtual bool RemoveEntry(void *entry) = 0;
    virtual uint32_t Load() = 0;                       // 5 (0xd06ba0): the entries filed (mCount)
    virtual uint32_t LoadSegment(void *segment) = 0;   // 6 (0xd06dc0): the entries filed from one segment (mCount)
    virtual void ClearAll() = 0;                       // 7 (0xd07c50)
    // 8 (0xd071f0): `out` emptied, then the list filed under `name` (mNames), filtered (AppendMatching).
    virtual void GetByName(MaterialList *out, const std::string &name, const std::string *nameFilter, const uint32_t *keyFilter) = 0;
    // 9 (0xd07340): `out` emptied, then every group's lists (group 0), a group's lists, or one of its names' list, filtered.
    virtual void GetByGroup(MaterialList *out, uint32_t group, const std::string *name, const std::string *nameFilter,
                            const uint32_t *keyFilter) = 0;
    virtual void AddEntry(void *entry) = 0;                            // 10 (0xd05250)
    virtual bool OpenSegment(const char *path, void **out) = 0;        // 11 (0xd05790): an existing package file
    virtual bool CreateSegment(const char *directory, void **out) = 0; // 12 (0xd05900): a new package in a directory
};

class cTSMaterialCatalog : public rz::cRZSystemService, public cITSMaterialCatalog, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kVTable = 0xebab54;
    static constexpr uint32_t kVTable4 = 0xebab8c;
    static constexpr uint32_t kCatalogVTable = 0xebaba8;
    static constexpr uint32_t kTargetVTable = 0xebabe0;
    static constexpr uint32_t kSize = 0x44;
    static constexpr uint32_t kServiceId = 0xac6965bc;
    static constexpr int32_t kPriority = int32_t(0xffd23940);
    static constexpr uint32_t kIid = 0x6c69658c;     // cITSMaterialCatalog
    static constexpr uint32_t kReloadMessage = 0xcde2cc2d;

    cTSMaterialCatalog(); // 0xd04bd0
    // 0xd04e10; deleting 0xd050c0 (+4 thunk 0xd0508f). noexcept as the base's (mDefaultSegment's destructor is not).
    ~cTSMaterialCatalog() noexcept override;
    cTSMaterialCatalog(const cTSMaterialCatalog &) = delete;
    cTSMaterialCatalog &operator=(const cTSMaterialCatalog &) = delete;

    // 0xd06930 (__cdecl): the COM director's creator: a new catalog (its +0 face; null when new failed).
    static cTSMaterialCatalog *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xd06f80; thunks 0xd06fb1 (+4), 0xd06fb9 (+0x1c), 0xd06fc1 (+0x20)
    uint32_t AddRef() override;                              // folded 0x1cc320 (the service's)
    uint32_t Release() override;                             // folded 0x1cd7a0 (the service's)

    // ---- cIGZSystemService ----
    bool Init() override;     // 0xd069b0
    bool Shutdown() override; // 0xd07eb0

    // ---- cITSMaterialCatalog (+0x1c) ----
    bool AddMaterial(void *resource, void *segment) override;
    bool RemoveEntry(void *entry) override;
    uint32_t Load() override;
    uint32_t LoadSegment(void *segment) override;
    void ClearAll() override;
    void GetByName(MaterialList *out, const std::string &name, const std::string *nameFilter, const uint32_t *keyFilter) override;
    void GetByGroup(MaterialList *out, uint32_t group, const std::string *name, const std::string *nameFilter,
                    const uint32_t *keyFilter) override;
    void AddEntry(void *entry) override;
    bool OpenSegment(const char *path, void **out) override;
    bool CreateSegment(const char *directory, void **out) override;

    // ---- cIGZMessageTarget2 (+0x20) ----
    bool DoMessage(void *message) override; // 0xd06250

    // 0xd06d00 (__cdecl (key, catalog)): the resource under `key` (interface 0x2c696643), when the manager has it and its kind
    // is 1, filed through AddEntry (by its 0x4c697e55 face).
    void LoadKey(const MaterialKey *key);
    // 0xd08060: every content directory's "materials.package" opened (slot 11) or created (slot 12) in turn, last first; each
    // segment that opens becomes the default one. True when any did.
    bool OpenDefaultSegments();
    // 0xd07000 (__cdecl): appends to `out` (counted) each entry of `list` whose second name equals `nameFilter` (ignoring
    // case) and whose resource's slot 14 key equals the 16 bytes at `keyFilter`; a null filter takes everything.
    static void AppendMatching(MaterialList *out, const MaterialList *list, const std::string *nameFilter, const uint32_t *keyFilter);
    // 0xd07170 (__cdecl): AppendMatching over every list of `names` (in name order).
    static void AppendNameMap(MaterialList *out, const MaterialNameMap *names, const std::string *nameFilter, const uint32_t *keyFilter);

    // +0x00 .. +0x1b: cRZSystemService; +0x1c: the cITSMaterialCatalog vtable; +0x20: the cIGZMessageTarget2 vtable
    uint32_t mCount;               // 0x24 the entries the loads filed (zeroed by Load / LoadSegment; not set by the constructor)
    MaterialGroupMap mGroups;      // 0x28 group -> name map (the name maps and their lists are freed by ClearAll only)
    MaterialNameMap mNames;        // 0x30 name -> the list last filed under it
    std::set<uint32_t> mMessages;  // 0x38 the messages Init subscribes to (the constructor puts in kReloadMessage)
    MaterialRef mDefaultSegment;   // 0x40 the segment AddMaterial files into when none other is found
};

static_assert(sizeof(cTSMaterialCatalog) == cTSMaterialCatalog::kSize, "cTSMaterialCatalog size");
static_assert(offsetof(cTSMaterialCatalog, mCount) == 0x24, "mCount");
static_assert(offsetof(cTSMaterialCatalog, mGroups) == 0x28, "mGroups");
static_assert(offsetof(cTSMaterialCatalog, mNames) == 0x30, "mNames");
static_assert(offsetof(cTSMaterialCatalog, mMessages) == 0x38, "mMessages");
static_assert(offsetof(cTSMaterialCatalog, mDefaultSegment) == 0x40, "mDefaultSegment");

// The hooks' entry points: the constructor (placement new of ours) for the creator's call; the creator itself is hooked
// directly (Create).
struct cTSMaterialCatalogAdapters {
    static cTSMaterialCatalog *__fastcall Construct(void *memory, void *edx); // 0xd04bd0
};
