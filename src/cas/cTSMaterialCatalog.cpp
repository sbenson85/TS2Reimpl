#include "cas/cTSMaterialCatalog.h"

#include "app/AppInit.h"     // CommonImplSlot5 (0x1d0c60): the content directories
#include "app/AppServices2.h" // DirectoryExists
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cIGZCOM.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/PersistResourceManager.h"
#include "misc/UtilitiesC.h" // ServiceHolder (the message server)
#include "rz/MiscHelpersB.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StringKeyParser.h" // cRZStringHelpers: LowerCase, EndsWith
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"
#include "rz/cRZString.h"

#include <cstring>
#include <new>
#include <vector>

namespace {

// The persist system (service 0x056b906e, cITSPersistSystem 0x9af5e924), from the cache 0x10faf6c or the framework.
constexpr uint32_t kPersistService = 0x056b906e;
constexpr uint32_t kPersistSystemIid = 0x9af5e924;
void *PersistSystem() { return CachedServiceGetter<0x10faf6c, kPersistService, kPersistSystemIid>(); }
ifaces::cIGZPersistResourceManager *ResourceManager()
{
    return reinterpret_cast<ifaces::cIGZPersistResourceManager *>(As<ifaces::cITSPersistSystem>(PersistSystem())->GetResourceManager());
}

constexpr uint32_t kMaterialType = 0x4c697e5a;     // the material entries' resource type (the filter's, the factory's)
constexpr uint32_t kMaterialResourceIid = 0x2c696643; // what LoadKey asks the resource manager for
constexpr uint32_t kMaterialEntryIid = 0x4c697e55;  // the entry face AddEntry takes
constexpr uint32_t kDefaultGroup = 0x8c6965ac;      // AddMaterial's group for a key without one
constexpr uint32_t kMessageIid = 0x65297976;        // what DoMessage asks the message for (and does not use)
constexpr uint32_t kSegmentIid = 0x656b8f0c;        // the segment face slots 11 / 12 hand out
constexpr uint32_t kContentDirectories = 0x6c69ac4a; // the app's option for the "Materials/" directories
constexpr const char *kNameSuffix = "_cres";        // the global 0x108a418 points at it
constexpr const char *kPackageName = "materials.package";
constexpr const char *kPackageLabel = "matle";      // CreateSegment's label for the material type (0xebac44)

// The material entry factory as Init builds it inline (12 bytes, not ours): the cIGZPersistResourceFactory vtable (a dead
// store), the cRZUnknown part at +4 (0x1b9052: vtable, zero count), then cTSModelMaterialEntryFactory's two game vtables.
struct EntryFactoryLayout {
    const void *mVTable;      // +0x0 0xebabf4
    RZUnknownLayout mUnknown; // +0x4 0xebac14, count +8
};
static_assert(sizeof(EntryFactoryLayout) == 0xc, "EntryFactoryLayout layout");
constexpr uint32_t kPersistResourceFactoryVTable = 0xda6718;
constexpr uint32_t kEntryFactoryVTable = 0xebabf4;
constexpr uint32_t kEntryFactoryVTable4 = 0xebac14;
constexpr uint32_t kFactoryKind = 0xac9124d3;

// CreateSegment's segment (COM class 0xa079ce1b, interface 0x856c8cf7); its slots are not named yet.
constexpr uint32_t kSegmentClass = 0xa079ce1b;
constexpr uint32_t kSegmentClassIid = 0x856c8cf7;

// The resource manager's keys (Load's list), from the allocator service.
using KeyList = std::vector<MaterialKey, GZServiceAllocator<MaterialKey>>;

const ::cGZPersistResourceKey *AsResourceKey(const MaterialKey *key)
{
    return reinterpret_cast<const ::cGZPersistResourceKey *>(key);
}

void AddRefObject(void *object) { static_cast<cIGZUnknown *>(object)->AddRef(); }
void ReleaseObject(void *object) { static_cast<cIGZUnknown *>(object)->Release(); }

// An entry's parts (its 0x4c697e55 face): the resource (slot 3), the group (4), the name (5) and second name (6), the names
// as allocator-service strings by value.
void *EntryResource(void *entry) { return RawSlot<3, void *>(entry); }
uint32_t EntryGroup(void *entry) { return RawSlot<4>(entry); }
GZString *EntryName(void *entry, GZString *out) { return RawSlot<5, GZString *>(entry, out); }
GZString *EntrySecondName(void *entry, GZString *out) { return RawSlot<6, GZString *>(entry, out); }
// A resource's kind (slot 12), its file name (slot 13, a C string) and its 16-byte key (slot 14).
uint32_t ResourceKind(void *resource) { return RawSlot<12>(resource); }
const char *ResourceText(void *resource) { return RawSlot<13, const char *>(resource); }
const uint32_t *ResourceKeyWords(void *resource) { return RawSlot<14, const uint32_t *>(resource); }

// The catalog's key for a name: lowercased, "_cres" appended unless it already ends so (the original builds it in a
// cRZString and copies its text, up to the first NUL, into a std::string). The callers hand the name's whole std::string
// (GetByName, GetByGroup) or its text up to the first NUL (AddEntry, RemoveEntry).
std::string MaterialName(const char *text, size_t length)
{
    cRZString name;
    name.Construct();
    name.mString.assign(text, length);
    auto *helpers = reinterpret_cast<cRZStringHelpers *>(&name);
    helpers->LowerCase();
    if (!helpers->EndsWith(kNameSuffix, 5, 1)) name.mString.append(kNameSuffix, std::strlen(kNameSuffix));
    std::string key(name.ToChar());
    name.Destruct();
    return key;
}

// The list's push_back as the callers write it out: the entry held (counted) around the push, which counts it again.
void PushBackHeld(MaterialList *list, void *entry)
{
    if (entry) AddRefObject(entry);
    list->PushBack(entry);
    if (entry) ReleaseObject(entry);
}

// The message server (ServiceHolder, ids 0x04fa845b / 0x652294c7), as the key walkers 0x1c0458 / 0x1c081e get it: queried
// and released again at once.
void *MessageServer()
{
    ServiceHolder holder{nullptr};
    holder.QueryFromService();
    void *const server = holder.mObject;
    ReleaseHeldObject(holder.mObject);
    return server;
}

} // namespace

// ---- cTSMaterialFilter ----

// Folded 0xb7b970: 1 or 0x8865cd89 -> this, counted through slot 1.
bool cTSMaterialFilter::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kInterfaceId) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cTSMaterialFilter::AddRef() { return ++mRefCount; }

// Folded 0x72c480: the last reference deletes (slot 4) without counting down, and answers 0.
uint32_t cTSMaterialFilter::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

// 0xd06b70: the material type.
bool cTSMaterialFilter::IsKeyIncluded(const uint32_t *key) { return key[0] == kMaterialType; }

// ---- construction ----

// 0xd04bd0: the service (id 0xac6965bc), empty trees, no default segment, then the reload message put in the set. mCount is
// not written.
cTSMaterialCatalog::cTSMaterialCatalog() : rz::cRZSystemService(kServiceId, kPriority)
{
    mMessages.insert(kReloadMessage);
}

// 0xd04e10: the default segment released, then the trees. ORIGINAL-BUG: the name maps and lists are freed only by ClearAll
// (Shutdown); a catalog destroyed without it leaks them and the entries they hold.
cTSMaterialCatalog::~cTSMaterialCatalog() noexcept {}

cTSMaterialCatalog *__cdecl cTSMaterialCatalog::Create() { return new cTSMaterialCatalog; }

cTSMaterialCatalog *__fastcall cTSMaterialCatalogAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSMaterialCatalog;
}

// ---- cIGZUnknown / cIGZSystemService ----

// 0xd06f80: cITSMaterialCatalog -> the +0x1c face (counted through the primary face's AddRef), else the service's (which
// does not answer the message target's interface).
bool cTSMaterialCatalog::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cITSMaterialCatalog *>(this);
    AddRef();
    return true;
}

uint32_t cTSMaterialCatalog::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSMaterialCatalog::Release() { return rz::cRZSystemService::Release(); }

// 0xd069b0: the material entry factory registered with the resource manager (its answer not checked), the default segments
// opened, and the message target subscribed to every message of the set (0x1c0458's walk); false without a resource manager.
bool cTSMaterialCatalog::Init()
{
    if (!rz::cRZSystemService::Init()) return false;
    auto *const manager = ResourceManager();
    if (!manager) return false;
    auto *factory = static_cast<EntryFactoryLayout *>(::operator new(sizeof(EntryFactoryLayout)));
    factory->mVTable = GameAddress<void>(kPersistResourceFactoryVTable);
    factory->mUnknown.Construct();
    factory->mVTable = GameAddress<void>(kEntryFactoryVTable);
    factory->mUnknown.mVTable = GameAddress<void>(kEntryFactoryVTable4);
    auto *counted = reinterpret_cast<cIGZUnknown *>(factory);
    counted->AddRef();
    RawSlot<20, bool>(manager, kFactoryKind, kMaterialType, static_cast<void *>(factory));
    OpenDefaultSegments();
    // ORIGINAL-BUG (0x1c0458): no check that the message server exists; with none, the walk calls through null.
    void *const server = MessageServer();
    for (uint32_t id : mMessages) RawSlot<5, void>(server, static_cast<cIGZMessageTarget2 *>(this), id);
    counted->Release();
    return true;
}

// 0xd07eb0: ClearAll through the interface's vtable, the message target unsubscribed from the set's messages (0x1c081e's
// walk: nothing without a message server), then the base's Shutdown (true).
bool cTSMaterialCatalog::Shutdown()
{
    static_cast<cITSMaterialCatalog *>(this)->ClearAll();
    if (void *const server = MessageServer()) {
        for (uint32_t id : mMessages) RawSlot<6, void>(server, static_cast<cIGZMessageTarget2 *>(this), id);
    }
    return rz::cRZSystemService::Shutdown();
}

// 0xd06250: a message of type kReloadMessage reloads the catalog (slot 5 through the vtable); true for that type. The
// message is asked for interface 0x65297976 first, the answer held and released unused.
bool cTSMaterialCatalog::DoMessage(void *message)
{
    if (!message) return false;
    MaterialRef held;
    As<ifaces::cIGZMessage2Standard>(message)->QueryInterface(kMessageIid, held.AsPPVoid());
    const uint32_t type = As<ifaces::cIGZMessage2Standard>(message)->GetType();
    if (type == kReloadMessage) static_cast<cITSMaterialCatalog *>(this)->Load();
    return type == kReloadMessage;
}

// ---- cITSMaterialCatalog ----

// 0xd05140: the resource's key completed: a new instance from the COM object (its slot 7) when it has none, the default
// group when it has none, the material type when its kind is 1 (any other kind keeps its type and fails when that is 0).
// The segment is `segment`, else the one the resource manager finds holding the key (counted by it and never released: a
// leak the original has), else the default one; none fails. A key the manager has cached is forgotten first; the key is
// set on the resource, the manager caches it and files it in the segment (its slot 13, the answer).
bool cTSMaterialCatalog::AddMaterial(void *resource, void *segment)
{
    auto *const manager = ResourceManager();
    MaterialKey key;
    As<ifaces::cIGZPersistResource>(resource)->GetKey(&key);
    if (!key.instance) {
        uint32_t instance;
        As<ifaces::cIGZCOM>(GetDirectorFrameworkObject())->CreateGuid(&instance);
        key.instance = instance;
    }
    if (!key.group) key.group = kDefaultGroup;
    if (ResourceKind(resource) == 1) key.type = kMaterialType;
    else if (!key.type) return false;
    void *target = segment;
    if (!target) {
        // ORIGINAL-BUG: the segment FindSegmentWithKey counts for the caller is never released.
        manager->FindSegmentWithKey(AsResourceKey(&key), &target);
        if (!target) target = mDefaultSegment.Get();
    }
    if (!target) return false;
    if (manager->IsResourceCached(AsResourceKey(&key))) manager->ForgetResource(AsResourceKey(&key));
    As<ifaces::cIGZPersistResource>(resource)->SetKey(&key);
    manager->CacheResource(static_cast<ifaces::cIGZPersistResource *>(resource));
    return RawSlot<13, bool>(manager, &key, target);
}

// 0xd05ec0: the entry taken out of the list filed under its group and name (the first element that is the entry; the ones
// after it move down a place, counted), the name pointed at that list again; then the record of the entry's resource
// deleted from the segment holding it (counted by the manager and never released: a leak the original has), a cached
// resource forgotten first. True when a segment held the key and deleted it. Looking the group and the name up files empty
// ones (operator[]).
bool cTSMaterialCatalog::RemoveEntry(void *entry)
{
    if (MaterialNameMap *const names = mGroups[EntryGroup(entry)]) {
        GZString text;
        EntryName(entry, &text);
        const std::string name = MaterialName(text.c_str(), std::strlen(text.c_str()));
        text = GZString();
        if (MaterialList *const list = (*names)[name]) {
            void **at = list->mFirst;
            while (at != list->mLast && *at != entry) ++at;
            if (at != list->mLast) {
                for (; at + 1 != list->mLast; ++at) {
                    void *const next = at[1];
                    void *const old = at[0];
                    if (next == old) continue;
                    if (next) AddRefObject(next);
                    at[0] = next;
                    if (old) ReleaseObject(old);
                }
                if (void *const last = list->mLast[-1]) ReleaseObject(last);
                --list->mLast;
            }
            mNames[name] = list;
        }
    }
    MaterialKey key;
    As<ifaces::cIGZPersistResource>(EntryResource(entry))->GetKey(&key);
    if (!key.instance || !key.group) return false;
    auto *const manager = ResourceManager();
    if (manager->IsResourceCached(AsResourceKey(&key))) manager->ForgetResource(AsResourceKey(&key));
    void *segment = nullptr;
    manager->FindSegmentWithKey(AsResourceKey(&key), &segment);
    if (!segment) return false;
    return RawSlot<26, bool>(segment, &key);
}

// 0xd06ba0: every entry the resource manager has a key for (type 0x4c697e5a, through a cTSMaterialFilter) filed: mCount
// zeroed, each key loaded (0xd06d00); answers mCount.
uint32_t cTSMaterialCatalog::Load()
{
    auto *const manager = ResourceManager();
    mCount = 0;
    auto *filter = new cTSMaterialFilter;
    filter->AddRef();
    KeyList keys;
    manager->GetResourceKeyList(&keys, reinterpret_cast<ifaces::cIGZPersistResourceKeyFilter *>(filter));
    for (const MaterialKey &key : keys) LoadKey(&key);
    const uint32_t count = mCount;
    keys = KeyList();
    filter->Release();
    return count;
}

// 0xd06dc0: as Load over one segment's keys (its GetResourceKeyList); the segment held (counted) meanwhile. The resource
// manager is asked for and not used. Without a segment only mCount is zeroed.
uint32_t cTSMaterialCatalog::LoadSegment(void *segment)
{
    MaterialRef held(static_cast<cIGZUnknown *>(segment));
    As<ifaces::cITSPersistSystem>(PersistSystem())->GetResourceManager();
    mCount = 0;
    if (segment) {
        auto *filter = new cTSMaterialFilter;
        filter->AddRef();
        KeyList keys;
        As<ifaces::cIGZPersistDBSegment>(segment)->GetResourceKeyList(&keys, reinterpret_cast<ifaces::cIGZPersistResourceKeyFilter *>(filter));
        for (const MaterialKey &key : keys) LoadKey(&key);
        keys = KeyList();
        filter->Release();
    }
    return mCount;
}

// 0xd07c50: every group's name map freed with its lists (their entries released), then both trees emptied. mCount stays.
void cTSMaterialCatalog::ClearAll()
{
    for (auto &[group, names] : mGroups) {
        if (!names) continue;
        for (auto &[name, list] : *names) {
            if (!list) continue;
            list->ReleaseAndFree();
            delete list;
        }
        delete names;
    }
    mGroups.clear();
    mNames.clear();
}

// 0xd071f0: `out` emptied (released), then, when any name is filed, the list filed under `name` (looked up through
// operator[], which files a missing name), filtered.
void cTSMaterialCatalog::GetByName(MaterialList *out, const std::string &name, const std::string *nameFilter, const uint32_t *keyFilter)
{
    out->Clear();
    if (mNames.empty()) return;
    AppendMatching(out, mNames[MaterialName(name.data(), name.size())], nameFilter, keyFilter);
}

// 0xd07340: `out` emptied (released), then, when any group is filed: for group 0 every group's lists in order; else the
// group's (operator[]: a missing group is filed empty) lists, or with `name` the one list filed under it (operator[]).
void cTSMaterialCatalog::GetByGroup(MaterialList *out, uint32_t group, const std::string *name, const std::string *nameFilter,
                                    const uint32_t *keyFilter)
{
    out->Clear();
    if (mGroups.empty()) return;
    if (!group) {
        for (const auto &[id, names] : mGroups) AppendNameMap(out, names, nameFilter, keyFilter);
        return;
    }
    MaterialNameMap *const names = mGroups[group];
    if (!names) return;
    if (name) AppendMatching(out, (*names)[MaterialName(name->data(), name->size())], nameFilter, keyFilter);
    else AppendNameMap(out, names, nameFilter, keyFilter);
}

// 0xd05250: files an entry under its group (a new name map when the group has none: the group asked again to store it) and
// name (a new list when the name has none), unless the list already has it: the same pointer, or one with an equal second
// name (ignoring case) whose resource's slot 13 text is equal too (ignoring case). A new entry is appended (counted) and
// counted in mCount. Either way the name is pointed at the list (mNames).
void cTSMaterialCatalog::AddEntry(void *entry)
{
    if (!entry) return;
    MaterialNameMap *names = mGroups[EntryGroup(entry)];
    if (!names) {
        names = new MaterialNameMap;
        mGroups[EntryGroup(entry)] = names;
    }
    GZString text;
    EntryName(entry, &text);
    const std::string name = MaterialName(text.c_str(), std::strlen(text.c_str()));
    text = GZString();
    MaterialList *list = (*names)[name];
    if (!list) {
        list = new MaterialList{};
        (*names)[name] = list;
    }
    bool found = false;
    for (void **at = list->mFirst; at != list->mLast; ++at) {
        void *const other = *at;
        if (other == entry) {
            found = true;
            break;
        }
        GZString mine, theirs;
        EntrySecondName(entry, &mine);
        EntrySecondName(other, &theirs);
        if (_stricmp(theirs.c_str(), mine.c_str()) == 0) {
            void *const myResource = EntryResource(entry);
            void *const theirResource = EntryResource(other);
            const char *const myText = ResourceText(myResource);
            if (_stricmp(ResourceText(theirResource), myText) == 0) found = true;
        }
        if (found) break;
    }
    if (!found) {
        PushBackHeld(list, entry);
        ++mCount;
    }
    mNames[name] = list;
}

// 0xd05790: an existing file at `path` opened as a packed-file segment by the persist system (its OpenSegment: interface
// 0x656b8f0c into `out`, mode 2, the drive-and-directory and the file name of the path, 1, 1); false when the path does
// not exist. The persist system comes from the framework, unchecked.
bool cTSMaterialCatalog::OpenSegment(const char *path, void **out)
{
    cRZString file;
    file.Construct(path);
    bool opened = false;
    if (cRZFile::PathExists(*file.AsInterface())) {
        MaterialRef persist;
        if (void *const framework = GetServiceObject()) {
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kPersistService, kPersistSystemIid, persist.AsPPVoid());
        }
        cRZFileName parts;
        parts.Construct(&file);
        cRZString directory;
        parts.GetDrivePath(&directory);
        // 0x1c8674 (cRZFileName, not ours yet): the name part, split from the full path when it is still empty.
        cRZString *const name = OriginalFunction<cRZString *(__thiscall *)(cRZFileName *), 0x1c8674>()(&parts);
        opened = uint8_t(As<ifaces::cITSPersistSystem>(persist.Get())->OpenSegment(kSegmentIid, out, 2, &directory, name, 1, 1)) != 0;
        directory.Destruct();
        parts.Destruct();
    }
    file.Destruct();
    return opened;
}

// 0xd05900: a new packed-file segment (COM class 0xa079ce1b) set up in the existing directory `directory` (its numbered
// slots with the original's arguments; the material type labelled "matle"), added to the resource manager, loaded
// (LoadSegment through the vtable) and handed out as interface 0x656b8f0c (the answer); false when the directory does not
// exist or the class cannot be made.
bool cTSMaterialCatalog::CreateSegment(const char *directory, void **out)
{
    cRZString path;
    path.Construct(directory);
    bool created = false;
    if (as2::DirectoryExists(&path)) {
        MaterialRef segment;
        auto *const com = As<ifaces::cIGZCOM>(GetDirectorFrameworkObject());
        if (uint8_t(com->GetClassObject(kSegmentClass, kSegmentClassIid, segment.AsPPVoid()))) {
            void *const s = segment.Get();
            RawSlot<15>(s, 1u, 1u);
            RawSlot<16>(s, 1u);
            void *const labels = RawSlot<12, void *>(s);
            cRZString label;
            label.Construct(kPackageLabel);
            RawSlot<3>(labels, kMaterialType, &label);
            label.Destruct();
            RawSlot<4>(s);
            RawSlot<9>(s, &path);
            RawSlot<6>(s, 1u, 1u);
            RawSlot<18>(RawSlot<3, void *>(s), 0u);
            RawSlot<29>(s, 1u);
            RawSlot<28>(s, 1u);
            auto *const manager = ResourceManager();
            manager->AddSegment(RawSlot<3, ifaces::cIGZPersistDBSegment *>(s));
            static_cast<cITSMaterialCatalog *>(this)->LoadSegment(RawSlot<3, void *>(s));
            created = static_cast<cIGZUnknown *>(s)->QueryInterface(kSegmentIid, out);
        }
    }
    path.Destruct();
    return created;
}

// ---- loading ----

void cTSMaterialCatalog::LoadKey(const MaterialKey *key)
{
    auto *const manager = ResourceManager();
    MaterialRef resource;
    if (!manager->GetResource(key, kMaterialResourceIid, resource.AsPPVoid(), 0, 0)) return;
    if (ResourceKind(resource.Get()) != 1) return;
    MaterialRef entry;
    resource->QueryInterface(kMaterialEntryIid, entry.AsPPVoid());
    static_cast<cITSMaterialCatalog *>(this)->AddEntry(entry.Get());
}

bool cTSMaterialCatalog::OpenDefaultSegments()
{
    MaterialRef persist; // asked for and not used
    if (void *const framework = GetServiceObject()) {
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kPersistService, kPersistSystemIid, persist.AsPPVoid());
    }
    bool any = false;
    GZStringList directories;
    appinit::CommonImplSlot5(kContentDirectories, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&directories)), 1, 0, 0xffffffffu);
    for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
        const std::string folder(it->c_str());
        std::string package(it->c_str());
        package.append(kPackageName, 0x11);
        MaterialRef segment;
        auto *const catalog = static_cast<cITSMaterialCatalog *>(this);
        bool opened = catalog->OpenSegment(package.c_str(), segment.AsPPVoid());
        if (!opened) opened = catalog->CreateSegment(folder.c_str(), segment.AsPPVoid());
        any |= opened;
        if (segment.Get()) mDefaultSegment.Assign(segment.Get());
    }
    return any;
}

void cTSMaterialCatalog::AppendMatching(MaterialList *out, const MaterialList *list, const std::string *nameFilter, const uint32_t *keyFilter)
{
    if (!list) return;
    for (void **at = list->mFirst; at != list->mLast; ++at) {
        void *const entry = *at;
        bool match = true;
        if (nameFilter) {
            GZString second;
            EntrySecondName(entry, &second);
            match = _stricmp(nameFilter->c_str(), second.c_str()) == 0;
        }
        if (match && keyFilter) match = std::memcmp(keyFilter, ResourceKeyWords(EntryResource(entry)), 16) == 0;
        if (match) PushBackHeld(out, entry);
    }
}

void cTSMaterialCatalog::AppendNameMap(MaterialList *out, const MaterialNameMap *names, const std::string *nameFilter, const uint32_t *keyFilter)
{
    if (!names) return;
    for (const auto &[name, list] : *names) AppendMatching(out, list, nameFilter, keyFilter);
}
