#include "rz/PathFileOps.h"
#include "cas/cTSPatternCatalog.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cIGZCOM.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/PersistResourceManager.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"

#include <new>
#include <vector>

namespace {

// The framework's services this class asks for (service 0x056b906e, the persist system): its resource-manager face and its
// own interface.
constexpr uint32_t kPersistService = 0x056b906e;
constexpr uint32_t kResourceManagerIid = 0x656b8efc;
constexpr uint32_t kPersistSystemIid = 0x9af5e924;

// The pattern types by kind, and the interface the loader asks each kind's resource for.
constexpr uint32_t kFabricType = 0xcca8e925; // kinds 0..2
constexpr uint32_t kKind3Type = 0xaca8ea06;
constexpr uint32_t kKind4Type = 0x2cb230b8;
constexpr uint32_t kKind5Type = 0x6d619378;
constexpr uint32_t kKind6Type = 0x4dcadb7e;
constexpr uint32_t kFabricIid = 0xcca8e918;
constexpr uint32_t kKind3Iid = 0xaca8ea02;
constexpr uint32_t kKind4Iid = 0x2cb230aa;
constexpr uint32_t kKind5Iid = 0x6d619371;
constexpr uint32_t kKind6Iid = 0x4dcadb79;
constexpr uint32_t kPatternResourceIid = 0xac8ce8f0; // what the loader asks the resource manager for
constexpr uint32_t kDefaultGroup = 0x4c8cc5c0;       // AddPattern's group for a key without one

// cTSProductPatternFactory as Init builds it inline (12 bytes, not ours): the cIGZPersistResourceFactory vtable (a dead
// store), the cRZUnknown part at +4 (0x1b9052: vtable, zero count), then the class's two game vtables.
struct PatternFactoryLayout {
    const void *mVTable;      // +0x0 0xe91d0c
    RZUnknownLayout mUnknown; // +0x4 0xe91d2c, count +8
};
static_assert(sizeof(PatternFactoryLayout) == 0xc, "PatternFactoryLayout layout");
constexpr uint32_t kPersistResourceFactoryVTable = 0xda6718;
constexpr uint32_t kPatternFactoryVTable = 0xe91d0c;
constexpr uint32_t kPatternFactoryVTable4 = 0xe91d2c;
constexpr uint32_t kFactoryKind = 0xaca74f79;
constexpr uint32_t kFactoryTypes[] = {kFabricType, kKind3Type, kKind4Type, kKind5Type, kKind6Type};

// The resource manager's keys (Load's list), from the allocator service.
using KeyList = std::vector<PatternKey, GZServiceAllocator<PatternKey>>;

const ::cGZPersistResourceKey *AsResourceKey(const PatternKey *key)
{
    return reinterpret_cast<const ::cGZPersistResourceKey *>(key);
}

// The framework's service `iid` (service 0x056b906e) into `held` (left empty without a framework).
void GetPersistService(uint32_t iid, PatternRef &held)
{
    void *const framework = GetServiceObject();
    if (framework) As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kPersistService, iid, held.AsPPVoid());
}

// A pattern's kind (its slot 13: 0..6), and the resource behind a pattern's typed face (that face's slot 3).
uint32_t KindOf(void *pattern) { return RawSlot<13>(pattern); }
void *ResourceOf(void *typed) { return RawSlot<3, void *>(typed); }
const PatternKey *KeyOf(void *resource, PatternKey *buffer)
{
    return static_cast<const PatternKey *>(As<ifaces::cIGZPersistResource>(resource)->GetKey(buffer));
}

// The file operations RemovePattern makes on a segment's path (cdecl, a cIGZString): whether the file can be written
// (0x1c2182), and its deletion (0x1c2e25).
bool CanWriteFile(cRZString *path) { return OriginalFunction<bool(__cdecl *)(cRZString *), 0x1c2182>()(path); }
bool DeleteFileAt(cRZString *path) { return PathDelete(path->AsInterface()); }

// The values of one kind's map appended to `out` (in instance order).
void AppendValues(PatternList *out, const PatternMap &map)
{
    if (map.empty()) return;
    for (const auto &[instance, pattern] : map) out->push_back(pattern.Get());
}

} // namespace

// ---- cPatternFilter ----

// Folded 0xb7b970: 1 or 0x8865cd89 -> this, counted through slot 1.
bool cPatternFilter::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kInterfaceId) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cPatternFilter::AddRef() { return ++mRefCount; }

// Folded 0x72c480: the last reference deletes (slot 4) without counting down, and answers 0.
uint32_t cPatternFilter::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

// 0xca29f0: the five pattern types.
bool cPatternFilter::IsKeyIncluded(const uint32_t *key)
{
    switch (key[0]) {
    case kFabricType:
    case kKind3Type:
    case kKind4Type:
    case kKind5Type:
    case kKind6Type:
        return true;
    default:
        return false;
    }
}

// ---- construction ----

// 0xc9f9e0: the service (id 0x8c8cc5ed), not loaded, no default segment, empty maps, then ClearAll (called directly by the
// original, as a constructor's virtual call is). mCount is not written.
cTSPatternCatalog::cTSPatternCatalog() : rz::cRZSystemService(kServiceId, kPriority), mLoaded(false)
{
    ClearAll();
}

// 0xca0010: ClearAll, then the maps (empty by then), the default segment released, then the service.
cTSPatternCatalog::~cTSPatternCatalog() noexcept { ClearAll(); }

cTSPatternCatalog *__cdecl cTSPatternCatalog::Create() { return new cTSPatternCatalog; }

cTSPatternCatalog *__fastcall cTSPatternCatalogAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSPatternCatalog;
}

// ---- cIGZUnknown / cIGZSystemService ----

// 0xca3060: cITSPatternCatalog -> the +0x1c face (counted through the primary face's AddRef), else the service's.
bool cTSPatternCatalog::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cITSPatternCatalog *>(this);
    AddRef();
    return true;
}

uint32_t cTSPatternCatalog::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSPatternCatalog::Release() { return rz::cRZSystemService::Release(); }

// 0xca2880: registers the product pattern factory with the resource manager for the five pattern types (kind 0xaca74f79),
// stopping at the first refusal; false without a resource manager or when one was refused.
bool cTSPatternCatalog::Init()
{
    if (!rz::cRZSystemService::Init()) return false;
    PatternRef manager;
    GetPersistService(kResourceManagerIid, manager);
    if (!manager.Get()) return false;
    auto *factory = static_cast<PatternFactoryLayout *>(::operator new(sizeof(PatternFactoryLayout)));
    factory->mVTable = GameAddress<void>(kPersistResourceFactoryVTable);
    factory->mUnknown.Construct();
    factory->mVTable = GameAddress<void>(kPatternFactoryVTable);
    factory->mUnknown.mVTable = GameAddress<void>(kPatternFactoryVTable4);
    auto *counted = reinterpret_cast<cIGZUnknown *>(factory);
    counted->AddRef();
    bool registered = true;
    for (uint32_t type : kFactoryTypes) {
        if (!RawSlot<20, bool>(manager.Get(), kFactoryKind, type, static_cast<void *>(factory))) {
            registered = false;
            break;
        }
    }
    counted->Release();
    return registered;
}

// 0xca4970: ClearAll through the interface's vtable, then the base's Shutdown (true).
bool cTSPatternCatalog::Shutdown()
{
    static_cast<cITSPatternCatalog *>(this)->ClearAll();
    return rz::cRZSystemService::Shutdown();
}

// ---- cITSPatternCatalog ----

// 0xca07b0: the pattern's key completed: a new instance from the COM object (its slot 7) when it has none, the default
// group when it has none, the type of its kind (a kind past 6 keeps its type, and fails when that is 0). The segment is
// `segment`, else the one the resource manager finds holding the key (counted by it and never released: a leak the
// original has), else the default one. A key the segment already holds is forgotten by the manager first; the key is set
// on the pattern, the manager caches it and files it in the segment (its slot 13), and a filed pattern flushes the segment.
bool cTSPatternCatalog::AddPattern(void *pattern, void *segment)
{
    PatternRef manager;
    GetPersistService(kResourceManagerIid, manager);
    auto *resources = As<ifaces::cIGZPersistResourceManager>(manager.Get());
    PatternKey key;
    As<ifaces::cIGZPersistResource>(pattern)->GetKey(&key);
    uint32_t instance = key.instance;
    if (!instance) As<ifaces::cIGZCOM>(GetDirectorFrameworkObject())->CreateGuid(&instance);
    key.instance = instance;
    if (!key.group) key.group = kDefaultGroup;
    switch (KindOf(pattern)) {
    case 0:
    case 1:
    case 2: key.type = kFabricType; break;
    case 3: key.type = kKind3Type; break;
    case 4: key.type = kKind4Type; break;
    case 5: key.type = kKind5Type; break;
    case 6: key.type = kKind6Type; break;
    default:
        if (!key.type) return false;
        break;
    }
    void *target = segment;
    if (!target) resources->FindSegmentWithKey(AsResourceKey(&key), &target);
    if (!target) target = mDefaultSegment.Get();
    if (As<ifaces::cIGZPersistDBSegment>(target)->TestForRecord(AsResourceKey(&key))) {
        resources->ForgetResource(AsResourceKey(&key));
    }
    As<ifaces::cIGZPersistResource>(pattern)->SetKey(&key);
    resources->CacheResource(static_cast<ifaces::cIGZPersistResource *>(pattern));
    if (!RawSlot<13, bool>(manager.Get(), &key, target)) return false;
    As<ifaces::cIGZPersistDBSegment>(target)->Flush();
    return true;
}

// 0xca1280: a pattern with an instance and a group: the segment the resource manager finds holding its key (counted by it
// and never released: a leak the original has) is closed (an open one is reopened read-only first when its file can be
// written; when it cannot, nothing more happens and the answer is false), the resource deleted, the segment handed to the
// persist system's slot 45 and removed from its resource manager, closed again, and its file deleted (the answer).
bool cTSPatternCatalog::RemovePattern(void *pattern)
{
    PatternRef system;
    GetPersistService(kPersistSystemIid, system);
    PatternKey key;
    As<ifaces::cIGZPersistResource>(pattern)->GetKey(&key);
    if (!key.instance || !key.group) return false;
    PatternRef manager;
    GetPersistService(kResourceManagerIid, manager);
    void *segment = nullptr;
    As<ifaces::cIGZPersistResourceManager>(manager.Get())->FindSegmentWithKey(AsResourceKey(&key), &segment);
    if (!segment) return false;
    auto *file = As<ifaces::cIGZPersistDBSegment>(segment);
    cRZString path;
    file->GetPath(&path);
    if (file->IsOpen()) {
        file->Close();
        if (!CanWriteFile(&path)) return false;
        file->Open(1, 0);
    }
    auto *persist = As<ifaces::cITSPersistSystem>(system.Get());
    persist->DeleteResource(&key);
    persist->RegisterSegment(static_cast<ifaces::cIGZPersistDBSegment *>(segment));
    persist->GetResourceManager()->RemoveSegment(segment);
    file->Close();
    return DeleteFileAt(&path);
}

// 0xca2a40: every pattern the resource manager has a key for (the keys of the five types, through a cPatternFilter) filed
// afresh: mCount zeroed, each key loaded (0xca2ca0), then the catalog marked loaded; answers mCount. (The original also
// times the load with a stopwatch and formats the time into a local string it destroys unused: no effect, left out.)
uint32_t cTSPatternCatalog::Load()
{
    PatternRef manager;
    GetPersistService(kResourceManagerIid, manager);
    mCount = 0;
    auto *filter = new cPatternFilter;
    filter->AddRef();
    KeyList keys;
    As<ifaces::cIGZPersistResourceManager>(manager.Get())->GetResourceKeyList(&keys, reinterpret_cast<ifaces::cIGZPersistResourceKeyFilter *>(filter));
    for (const PatternKey &key : keys) LoadKey(&key);
    const uint32_t count = mCount;
    mLoaded = true;
    keys = KeyList();
    filter->Release();
    return count;
}

// 0xca4630: every map emptied (their patterns released): kind 2's first, then kinds 1, 3, 4, 5, 6; then not loaded.
void cTSPatternCatalog::ClearAll()
{
    mKind2.clear();
    mKind1.clear();
    mKind3.clear();
    mKind4.clear();
    mKind5.clear();
    mKind6.clear();
    mLoaded = false;
}

// 0xca3170: `out` emptied, the catalog loaded, then kind 1's patterns (which 0) or kind 2's (which 1).
void cTSPatternCatalog::GetPatterns(PatternList *out, uint32_t which)
{
    out->clear();
    EnsureLoaded();
    if (which == 0) AppendValues(out, mKind1);
    else if (which == 1) AppendValues(out, mKind2);
}

// 0xca3240 / 0xca2f10 / 0xca2e70 / 0xca32e0: `out` emptied, the catalog loaded, then the kind's patterns.
void cTSPatternCatalog::GetKind3(PatternList *out)
{
    out->clear();
    EnsureLoaded();
    AppendValues(out, mKind3);
}
void cTSPatternCatalog::GetKind4(PatternList *out)
{
    out->clear();
    EnsureLoaded();
    AppendValues(out, mKind4);
}
void cTSPatternCatalog::GetKind5(PatternList *out)
{
    out->clear();
    EnsureLoaded();
    AppendValues(out, mKind5);
}
void cTSPatternCatalog::GetKind6(PatternList *out)
{
    out->clear();
    EnsureLoaded();
    AppendValues(out, mKind6);
}

// ---- loading ----

void cTSPatternCatalog::EnsureLoaded()
{
    if (!mLoaded) static_cast<cITSPatternCatalog *>(this)->Load();
}

// 0xca2ca0: the resource under `key` (interface 0xac8ce8f0), when the manager has it, filed by its kind through the
// interface of that kind (asked through its slot 0; the answer is not checked).
void cTSPatternCatalog::LoadKey(const PatternKey *key)
{
    PatternRef manager;
    GetPersistService(kResourceManagerIid, manager);
    PatternRef resource;
    if (!As<ifaces::cIGZPersistResourceManager>(manager.Get())->GetResource(key, kPatternResourceIid, resource.AsPPVoid(), 0, 0)) {
        return;
    }
    uint32_t iid;
    const uint32_t kind = KindOf(resource.Get());
    switch (kind) {
    case 0:
    case 1:
    case 2: iid = kFabricIid; break;
    case 3: iid = kKind3Iid; break;
    case 4: iid = kKind4Iid; break;
    case 5: iid = kKind5Iid; break;
    case 6: iid = kKind6Iid; break;
    default: return;
    }
    PatternRef typed;
    resource->QueryInterface(iid, typed.AsPPVoid());
    switch (kind) {
    case 3: InsertInto(mKind3, typed.Get()); break;
    case 4: InsertInto(mKind4, typed.Get()); break;
    case 5: InsertInto(mKind5, typed.Get()); break;
    case 6: InsertInto(mKind6, typed.Get()); break;
    default: InsertKind12(typed.Get()); break;
    }
}

// 0xca0a60: the resource's slot 13 picks the map (1 or 2; any other files nothing); the count goes up either way.
void cTSPatternCatalog::InsertKind12(void *typed)
{
    const uint32_t kind = KindOf(ResourceOf(typed));
    if (kind == 1 || kind == 2) {
        PatternKey buffer;
        const uint32_t instance = KeyOf(ResourceOf(typed), &buffer)->instance;
        (kind == 1 ? mKind1 : mKind2)[instance].Assign(static_cast<cIGZUnknown *>(typed));
    }
    ++mCount;
}

void cTSPatternCatalog::InsertInto(PatternMap &map, void *typed)
{
    PatternKey buffer;
    const uint32_t instance = KeyOf(ResourceOf(typed), &buffer)->instance;
    map[instance].Assign(static_cast<cIGZUnknown *>(typed));
    ++mCount;
}
