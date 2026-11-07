#include "rz/RZMessageServer.h"
#include "ts/cTSWantTreeManager.h"

#include "app/AppInit.h"
#include "app/AppServices2.h"
#include "game/game.h"
#include "game/original.h"
#include "rz/cRZMessage2Standard.h"
#include "game/vtable.h"
#include "gz/cGZAny.h" // GZString
#include "gz/cGZPropertySetResource.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "rz/RZMemory.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "ts/cTSWantDefinition.h"
#include "ts/cTSWantTreeLoader.h"

#include <new>

namespace {

constexpr uint32_t kIStreamIid = 0x051b11bd;      // the record's cIGZIStream (Read)
constexpr uint32_t kOStreamIid = 0xc51b11cb;      // the record's cIGZOStream (Write)
constexpr uint32_t kSerializerIid = 0xebe701f3;   // the property set resource's serializer dispatcher
constexpr uint32_t kPackageClass = 0x086d4a89;    // the COM framework's packed file (DBPF) segment
constexpr uint32_t kSegmentIid = 0x656b8f0c;      // cIGZPersistDBSegment
constexpr uint32_t kDirectoryClass = 0xa079ce1b;  // the COM framework's directory segment
constexpr uint32_t kDirectoryIid = 0x856c8cf7;
constexpr uint32_t kWatcherClass = 0xe953da68;    // cGZPersistDirectoryWatcher
constexpr uint32_t kWatcherIid = 0xe953da64;
constexpr uint32_t kWatchFilter = 0x18;           // the change filter the watchers get

// The cached resource manager (0x20deb5, an original callee) and message server (0x9c5a89, an original callee).
void *ResourceManager() { return OriginalFunction<void *(__cdecl *)(), 0x20deb5>()(); }
void *MessageServer() { return GetCachedMessageServer(); }

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// A cRZString local (the hook-shaped string is built and destroyed by hand, as the original's inlined code does).
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    explicit LocalString(const char *text) { s.Construct(text); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { s.Destruct(); }
};

// The directory list cTSAppCommonImpl slot 5 fills (nodes and texts in the allocator service's memory).
using DirectoryList = std::list<GZString, GZServiceAllocator<GZString>>;

// The resource manager's key list (slot 19): 16-byte keys in a block from the allocator service, freed through it.
struct KeyList {
    cGZPersistResourceKey *first = nullptr;
    cGZPersistResourceKey *last = nullptr;
    cGZPersistResourceKey *end = nullptr;
    KeyList() = default;
    KeyList(const KeyList &) = delete;
    KeyList &operator=(const KeyList &) = delete;
    ~KeyList()
    {
        if (first) FreeThroughAllocatorService(first, 1);
    }
};

// GetClassObject (the COM framework's slot 3) into a counted pointer, released first.
bool GetClassObject(uint32_t classId, uint32_t iid, WantTreeRef &out)
{
    void *const framework = GetDirectorFrameworkObject();
    return VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(framework, classId, iid, out.AsPPVoid());
}

// The segments in `found` added to the resource manager (slot 27 AddSegment) and appended to `segments`.
template <class It> void AddSegments(void *resources, It begin, It end, WantTreeRefList *segments)
{
    for (It it = begin; it != end; ++it) {
        cIGZUnknown *const segment = it->Get();
        VirtualCall<bool(__thiscall *)(void *, void *), 27>(resources, segment);
        const WantTreeRef held(segment);
        segments->push_back(held);
    }
}

} // namespace

cTSWantTreeManager::cTSWantTreeManager() = default;

cTSWantTreeManager::~cTSWantTreeManager() = default;

// ---- cIGZUnknown ----

bool cTSWantTreeManager::QueryInterface(uint32_t iid, void **out)
{
    if (iid == kFactoryIid) {
        *out = static_cast<cIGZPersistResourceFactory *>(this);
    } else if (iid == 1 || iid == kIid) {
        *out = static_cast<cITSWantTreeManager *>(this);
    } else if (iid == kMessageTargetIid) {
        *out = static_cast<cIGZMessageTarget2 *>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

uint32_t cTSWantTreeManager::AddRef() { return static_cast<uint32_t>(++mRefCount); }

uint32_t cTSWantTreeManager::Release()
{
    if (mRefCount > 1) return static_cast<uint32_t>(--mRefCount);
    delete this;
    return 0;
}

// ---- cITSWantTreeManager ----

bool cTSWantTreeManager::Init()
{
    if (mInitialized) return true;
    mInitialized = true;
    void *resources = ResourceManager();
    if (!resources) return false;
    VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void *), 20>(resources, kFactoryId, kDefinitionType,
                                                                             static_cast<cIGZPersistResourceFactory *>(this));
    {
        LocalString name("Wants.package");
        AddPackage(kPackageFolder, &name.s, &mWantPackages);
    }
    bool found;
    {
        LocalString extension("xml");
        found = AddDirectory(kWantsXmlFolder, kWantsXmlGroup, &extension.s, kDefinitionType, &mWantPackages);
    }
    if (found) WatchDirectories(kWantsXmlFolder, static_cast<cIGZMessageTarget2 *>(this), &mWantWatchers);
    {
        LocalString name("WantTrees.package");
        AddPackage(kPackageFolder, &name.s, &mTreePackages);
    }
    {
        LocalString extension("xml");
        found = AddDirectory(kTreesXmlFolder, kTreesXmlGroup, &extension.s, kTreeType, &mTreePackages);
    }
    if (found) WatchDirectories(kTreesXmlFolder, static_cast<cIGZMessageTarget2 *>(this), &mTreeWatchers);
    {
        resources = ResourceManager();
        KeyList keys;
        VirtualCall<uint32_t(__thiscall *)(void *, KeyList *, uint32_t), 19>(resources, &keys, kDefinitionType);
        for (cGZPersistResourceKey *key = keys.first; key != keys.last; ++key) LoadDefinition(key, this);
    }
    {
        resources = ResourceManager();
        KeyList keys;
        VirtualCall<uint32_t(__thiscall *)(void *, KeyList *, uint32_t), 19>(resources, &keys, kTreeType);
        for (cGZPersistResourceKey *key = keys.first; key != keys.last; ++key) cTSWantTreeLoader::LoadTree(key, this);
    }
    VirtualCall<bool(__thiscall *)(void *, void *, uint32_t), 5>(MessageServer(), static_cast<cIGZMessageTarget2 *>(this),
                                                                 kReloadMessage);
    return true;
}

// The segments are taken out of the resource manager (slot 30 RemoveSegment) and closed (their slot 4), each held for the
// duration, then dropped.
void cTSWantTreeManager::ShutdownSegments(WantTreeRefList &segments, void *resources)
{
    if (segments.empty()) return;
    for (WantTreeRef &entry : segments) {
        const WantTreeRef segment(entry.Get());
        VirtualCall<bool(__thiscall *)(void *, void *), 30>(resources, segment.Get());
        VirtualCall<void(__thiscall *)(void *), 4>(segment.Get());
    }
    segments.clear();
}

bool cTSWantTreeManager::Shutdown()
{
    if (!mInitialized) return true;
    mInitialized = false;
    VirtualCall<bool(__thiscall *)(void *, void *, uint32_t), 6>(MessageServer(), static_cast<cIGZMessageTarget2 *>(this),
                                                                 kReloadMessage);
    void *const resources = ResourceManager();
    if (!resources) return false;
    VirtualCall<bool(__thiscall *)(void *, uint32_t), 21>(resources, kFactoryId);
    ShutdownSegments(mWantPackages, resources);
    ShutdownSegments(mTreePackages, resources);
    for (auto &tree : mTrees) VirtualCall<void(__thiscall *)(void *), 3>(tree.second.Get());
    mTrees.clear();
    mDefinitions.clear();
    for (WantTreeRefList *watchers : {&mWantWatchers, &mTreeWatchers}) {
        if (watchers->empty()) continue;
        for (WantTreeRef &watcher : *watchers) VirtualCall<bool(__thiscall *)(void *), 5>(watcher.Get());
        watchers->clear();
    }
    return true;
}

cIGZUnknown *cTSWantTreeManager::GetDefinition(uint32_t guid)
{
    const auto it = mDefinitions.find(guid);
    return it == mDefinitions.end() ? nullptr : it->second.Get();
}

bool cTSWantTreeManager::LoadTreesFromFile(uint32_t unused)
{
    // Still the game's body (it parses <data>\..._tree.xml from disk with a game-built cTSWantTreeLoader); listed in the
    // constructor entry's original_calls.
    return OriginalFunction<bool(__thiscall *)(cITSWantTreeManager *, uint32_t), 0x554a50>()(this, unused);
}

bool cTSWantTreeManager::Reload()
{
    void *const simulator = VirtualCall<void *(__thiscall *)(void *), 4>(GetCachedSingleton()); // GetWantFearService
    VirtualCall<void(__thiscall *)(void *), 55>(simulator);                                       // FlushUpdates
    Shutdown();
    return Init();
}

// ---- cIGZPersistResourceFactory ----

bool cTSWantTreeManager::CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4, uint32_t a5)
{
    const uint32_t type = *static_cast<const uint32_t *>(record->GetKey());
    WantTreeRef resource;
    bool result = false;
    if (CreateInstanceByType(type, riid, resource.AsPPVoid(), a4, a5) &&
        Read(resource.Get(), reinterpret_cast<cIGZUnknown *>(record)))
        result = resource->QueryInterface(riid, out);
    return result;
}

bool cTSWantTreeManager::CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t, uint32_t)
{
    if (type != kDefinitionType) return false;
    cIGZPersistResource *const definition = new cTSWantDefinition;
    definition->AddRef();
    definition->SetType(kDefinitionType);
    const bool result = definition->QueryInterface(riid, out);
    definition->Release();
    return result;
}

bool cTSWantTreeManager::Read(cIGZUnknown *resource, cIGZUnknown *record)
{
    WantTreeRef stream;
    bool result;
    if (!record->QueryInterface(kIStreamIid, stream.AsPPVoid())) {
        result = false;
    } else {
        cGZPropertySetString *const set = NewPropertySet();
        set->AddRef();
        {
            WantTreeRef serializer;
            if (!set->QueryInterface(kSerializerIid, serializer.AsPPVoid()) ||
                !VirtualCall<bool(__thiscall *)(void *, void *, void *), 3>(serializer.Get(), stream.Get(), set) ||
                *static_cast<const uint32_t *>(VirtualCall<const void *(__thiscall *)(void *), 3>(record)) != kDefinitionType) {
                result = false;
            } else {
                WantTreeRef definition;
                result = resource->QueryInterface(kDefinitionIid, definition.AsPPVoid());
                if (result) VirtualCall<void(__thiscall *)(void *, void *), 21>(definition.Get(), set); // ReadProperties
            }
        }
        set->Release();
    }
    return result;
}

bool cTSWantTreeManager::Write(cIGZUnknown *resource, cIGZUnknown *record)
{
    WantTreeRef stream;
    bool result;
    if (!record->QueryInterface(kOStreamIid, stream.AsPPVoid())) {
        result = false;
    } else {
        cGZPropertySetString *const set = NewPropertySet();
        set->AddRef();
        {
            WantTreeRef serializer;
            if (!set->QueryInterface(kSerializerIid, serializer.AsPPVoid())) {
                result = false;
            } else if (*static_cast<const uint32_t *>(VirtualCall<const void *(__thiscall *)(void *), 3>(record)) != kDefinitionType) {
                result = false;
            } else {
                WantTreeRef definition;
                if (!resource->QueryInterface(kDefinitionIid, definition.AsPPVoid())) {
                    result = false;
                } else {
                    VirtualCall<void(__thiscall *)(void *, void *), 20>(definition.Get(), set); // WriteProperties
                    result = VirtualCall<bool(__thiscall *)(void *, void *, void *, uint32_t), 4>(serializer.Get(), stream.Get(),
                                                                                                set, 0);
                }
            }
        }
        set->Release();
    }
    return result;
}

// ---- cIGZMessageTarget2 ----

bool cTSWantTreeManager::DoMessage(void *message)
{
    const uint32_t type = VirtualCall<uint32_t(__thiscall *)(void *), 4>(message);
    if (type == kReloadMessage) {
        mReloadPending = false;
        void *const globals = GetCachedSingleton();
        if (!globals) return false;
        void *const objects = VirtualCall<void *(__thiscall *)(void *), 26>(globals);   // GetObjectTable
        void *const simulator = VirtualCall<void *(__thiscall *)(void *), 4>(globals);  // GetWantFearService
        if (!objects || !simulator) return false;
        VirtualCall<void(__thiscall *)(void *), 55>(simulator); // FlushUpdates
        Shutdown();
        Init();
        void **const *persons = VirtualCall<void **const *(__thiscall *)(void *), 46>(objects); // {begin, end}
        for (void **it = persons[0]; it != persons[1]; ++it) {
            if (!*it) continue;
            const uint16_t sim = VirtualCall<uint16_t(__thiscall *)(void *), 57>(*it); // the person's sim id
            VirtualCall<void(__thiscall *)(void *, uint32_t, int32_t), 35>(simulator, sim, 0); // QueueSimUpdate(sim, 0)
        }
        return false;
    }
    if (type != kChangedMessage) return false;
    if (!mReloadPending) {
        mReloadPending = true;
        void *const memory = RZMemory::Malloc(0x2c);
        void *const posted = memory ? static_cast<cRZMessage2Standard *>(memory)->Construct() : nullptr;
        const WantTreeRef held(static_cast<cIGZUnknown *>(posted));
        if (posted) {
            VirtualCall<void(__thiscall *)(void *, uint32_t), 5>(posted, kReloadMessage); // SetId
            VirtualCall<bool(__thiscall *)(void *, void *, uint32_t), 4>(MessageServer(), posted, 0);
        }
    }
    return true;
}

// ---- the loading helpers ----

bool cTSWantTreeManager::AddPackage(uint32_t folder, cRZString *name, WantTreeRefList *segments)
{
    void *const resources = ResourceManager();
    if (!resources) return false;
    WantTreeRefList found;
    LocalString directory;
    appinit::CommonImplSlot4(folder, Word(&directory.s), 1, 0, 0xffffffffu);
    LocalString path;
    path.s.mString.assign(directory.s.mString.data(), directory.s.mString.size());
    path.s.mString.append(name->mString.data(), name->mString.size());
    if (cRZFile::PathExists(*path.s.AsInterface())) {
        WantTreeRef segment;
        if (GetClassObject(kPackageClass, kSegmentIid, segment)) {
            VirtualCall<bool(__thiscall *)(void *, cIGZString *), 12>(segment.Get(), path.s.AsInterface()); // the path
            VirtualCall<bool(__thiscall *)(void *), 3>(segment.Get());                                       // Init
            found.push_back(segment);
        }
    }
    AddSegments(resources, found.begin(), found.end(), segments);
    return !found.empty();
}

bool cTSWantTreeManager::AddDirectory(uint32_t folder, uint32_t group, cRZString *extension, uint32_t type,
                                      WantTreeRefList *segments)
{
    void *const resources = ResourceManager();
    if (!resources) return false;
    WantTreeRefList found;
    LocalString directory;
    appinit::CommonImplSlot4(folder, Word(&directory.s), 1, 0, 0xffffffffu);
    if (as2::DirectoryExists(&directory.s)) {
        WantTreeRef segment;
        if (!GetClassObject(kDirectoryClass, kDirectoryIid, segment)) return false;
        void *const s = segment.Get();
        VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 15>(s, 1, 1);
        VirtualCall<void(__thiscall *)(void *, uint32_t), 16>(s, 1);
        VirtualCall<void(__thiscall *)(void *, uint32_t), 10>(s, group);
        void *const types = VirtualCall<void *(__thiscall *)(void *), 12>(s);
        VirtualCall<void(__thiscall *)(void *, uint32_t, cRZString *), 3>(types, type, extension);
        VirtualCall<bool(__thiscall *)(void *), 4>(s);
        VirtualCall<bool(__thiscall *)(void *, cRZString *), 9>(s, &directory.s);
        VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t), 6>(s, 1, 1);
        const WantTreeRef opened(VirtualCall<cIGZUnknown *(__thiscall *)(void *), 3>(s));
        found.push_back(opened);
    }
    AddSegments(resources, found.rbegin(), found.rend(), segments);
    return !found.empty();
}

void cTSWantTreeManager::WatchDirectories(uint32_t folder, cIGZMessageTarget2 *target, WantTreeRefList *watchers)
{
    DirectoryList directories;
    appinit::CommonImplSlot5(folder, Word(&directories), 1, 0, 0xffffffffu);
    for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
        LocalString path(it->c_str());
        WantTreeRef watcher;
        GetClassObject(kWatcherClass, kWatcherIid, watcher);
        if (void *const w = watcher.Get()) {
            VirtualCall<bool(__thiscall *)(void *), 4>(w); // Init
            VirtualCall<void(__thiscall *)(void *, cIGZMessageTarget2 *), 8>(w, target);
            VirtualCall<void(__thiscall *)(void *, cIGZString *, uint32_t, uint32_t), 10>(w, path.s.AsInterface(), kWatchFilter, 0);
            VirtualCall<bool(__thiscall *)(void *), 6>(w); // Start
        }
        if (watchers) watchers->push_front(watcher); // inserted before head->next (0x497368): the list ends up in folder order
    }
}

void cTSWantTreeManager::LoadDefinition(const void *key, cTSWantTreeManager *manager)
{
    void *const resources = ResourceManager();
    WantTreeRef definition;
    if (VirtualCall<bool(__thiscall *)(void *, const void *, uint32_t, void **, uint32_t, uint32_t), 3>(
            resources, key, kDefinitionIid, definition.AsPPVoid(), 0, 0)) {
        const uint32_t guid = VirtualCall<uint32_t(__thiscall *)(void *), 3>(definition.Get()); // GetId
        manager->mDefinitions[guid].Assign(definition.Get());
    }
}

__declspec(noinline) cGZPropertySetString *cTSWantTreeManager::NewPropertySet()
{
    return (new cGZPropertySetResourceString)->SetFace();
}

__declspec(noinline) cTSWantTreeManager *cTSWantTreeManager::Create() { return new cTSWantTreeManager; }

// ---- adapters ----

cTSWantTreeManager *__fastcall cTSWantTreeManagerAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSWantTreeManager;
}
