#include "rz/RZMessageServer.h"
#include "rz/PathFileOps.h"
#include "ts/cTSObjectFolder.h"
#include "ts/ObjectSaveTypeTable.h"
#include "app/SpecialFolders.h"

#include "app/AppInit.h"
#include "app/ReportFinish.h"
#include "game/game.h"
#include "game/original.h"
#include "rz/cRZStringSub.h"
#include "rz/cRZMessage2Standard.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cIGZPropertySet.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/RZMemory.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/DirectoryScan.h"
#include "rz/cRZFileName.h"
#include "rz/PathAttributes.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"

#include <cstring>
#include <list>
#include <new>
#include <utility>

// cTSObjectFolder (lane IK). Every call the original makes through an object's, a service's or a message's vtable is made the
// same way and in the same order; the containers are the real ones (the game's STL is the same MSVC 14.40 library).
//
// The objects are game objects (cTSObject's primary face). The slots used here, as the original uses them:
//   4 data object (its slot 3 GetData(index), 6 SetData(index, value), 8 IsDeleted, 9, 11 tile at (x, y)),
//   5 SetResource, 7 Reload, 8 name text (also Unload(1) on the behaviour and the two framework parts), 11 contains(object),
//   14 / 15 Get / SetName text, 18 behaviour, 19 SetBehavior, 20, 23, 28 the id to file under, 31, 37, 38, 40 / 41, 42 / 43,
//   45 instance, 55, 57 / 58 save index, 62 / 63, 66 group, 67 / 70 framework parts, 73 portal index, 74, 75 resource, 76.
// Message ids and the folder's ids are named below.

namespace {

// A call through slot kSlot of the vtable at `object` (thiscall), with the result type of the original's use.
template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}
void AddRefObject(void *object) { Call<1>(object); }
void ReleaseObject(void *object) { Call<2>(object); }
void *Ptr(uint32_t value) { return reinterpret_cast<void *>(static_cast<uintptr_t>(value)); }

constexpr uint32_t kTypeObjd = 0x4f424a44;  // 'OBJD' resource type
constexpr uint32_t kTypeObjf = 0x4f424a66;  // 'OBJf' resource type
constexpr uint32_t kTypeGlob = 0x474c4f42;  // 'GLOB' resource type
constexpr uint32_t kTypeTree = 0x54524545;  // 'TREE' resource type
constexpr uint32_t kTypeFern = 0x4e524546;  // 'FERN' (skipped by the import)
constexpr uint32_t kResourceIid = 0x3b4b7a05;    // an object resource (catalogue slot 5 / QI)
constexpr uint32_t kPersistIid = 0x456b8f1d;     // a persistent resource (key slot 4)
constexpr uint32_t kPackageType = 0x3b2271bb;    // the lot package segment
constexpr uint32_t kStringIid = 0x089b7dc8;      // cIGZString
constexpr uint32_t kMessage2Iid = 0x65297976;    // cIGZMessage2

constexpr uint32_t kMsgObjectsChanged = 0x8e;
constexpr uint32_t kMsgObjectRemoved = 0xa8;
constexpr uint32_t kMsgResourcesUnloaded = 0xab;
constexpr uint32_t kMsgResourcesReloaded = 0xac;

constexpr uint32_t kGlobalText = 0xda6340;          // "global"
constexpr uint32_t kPreloadSettingText = 0xdcd090;  // the Init setting name
constexpr uint32_t kCustomSettingText = 0xdcd0a4;   // the LoadGlobals setting name ("disableCustomObjects")
constexpr uint32_t kScanSettingText = 0xdcd0dc;     // the neighbourhood-select setting name
constexpr uint32_t kPackagePatternText = 0xdcbc50;  // the package file pattern
constexpr uint32_t kSuffixSeparatorText = 0xd9f880; // what the import looks for in a package file name
constexpr uint32_t kCopyNumberFormat = 0xdcd0f4;    // the import's copy number format
constexpr uint32_t kTileFormat = 0xdcd0f8;          // the import's multi-tile suffix format
constexpr uint32_t kLeadText = 0xdcd104;            // " - Lead" (7 characters)

constexpr uint32_t kGuidTranslationVTable = 0xdcc87c; // the GUID translation table (0x14 bytes)
constexpr uint32_t kTypeFilterVTable = 0xda5cf0;      // cGZPersistResourceKeyFilterByType (12 bytes)
constexpr uint32_t kTypeGroupFilterVTable = 0xda5d08; // the type-and-group key filter (16 bytes)

struct ResourceKey {
    uint32_t type, group, instance, resource;
};
using KeyVector = std::vector<ResourceKey, GZServiceAllocator<ResourceKey>>;
static_assert(sizeof(KeyVector) == 12);

// The message server and a standard message made the way the game does: the server first (0x9c5a89), then 0x2c bytes
// (RZMalloc) and the constructor 0x1c033d; counted when the allocation worked, used untested (a failed allocation faults on the
// first call, as in the original), released when the scope ends.
struct StandardMessage {
    void *mServer;
    void *mMessage = nullptr;

    StandardMessage() : mServer(GetCachedMessageServer())
    {
        if (void *memory = RZMemory::Malloc(0x2c)) mMessage = static_cast<cRZMessage2Standard *>(memory)->Construct();
        if (mMessage) AddRefObject(mMessage);
    }
    StandardMessage(const StandardMessage &) = delete;
    StandardMessage &operator=(const StandardMessage &) = delete;
    ~StandardMessage() noexcept(false)
    {
        if (mMessage) ReleaseObject(mMessage);
    }
    // SetId (slot 5), then the server's slot 3.
    void Post(uint32_t id)
    {
        Call<5>(mMessage, id);
        Call<3>(mServer, mMessage);
    }
    // SetId, SetValue0 (slot 11), then the server's slot 3.
    void Post(uint32_t id, uint32_t value)
    {
        Call<5>(mMessage, id);
        Call<11>(mMessage, value);
        Call<3>(mServer, mMessage);
    }
};

// A local counted pointer as the original's frames hold one: released (when set) at the end of the scope.
struct LocalRef {
    void *p = nullptr;
    LocalRef() = default;
    explicit LocalRef(void *object) : p(object) {}
    LocalRef(const LocalRef &) = delete;
    LocalRef &operator=(const LocalRef &) = delete;
    ~LocalRef() noexcept(false)
    {
        if (p) ReleaseObject(p);
    }
    // The original's reset before a getter fills the pointer: null first, then the old one released.
    void **Reset()
    {
        if (void *old = p) {
            p = nullptr;
            ReleaseObject(old);
        }
        return &p;
    }
};

// A cRZString local (built empty or from text, destroyed at the end of the scope).
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    explicit LocalString(const char *text) { s.Construct(text); }
    explicit LocalString(const cIGZString &text) { s.Construct(text); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { s.Destruct(); }
    // A string with the given bytes (the original's inline basic_string construction from a pointer and a length).
    LocalString(const char *text, uint32_t length)
    {
        s.Construct();
        s.mString.assign(text, length);
    }
};

// The calls that clear an object's links when it leaves the folder: slots 19, 20, 5, 55, 23 with 0.
void DetachLinks(void *object)
{
    Call<19>(object, 0u);
    Call<20>(object, 0u);
    Call<5>(object, 0u);
    Call<55>(object, 0u);
    Call<23>(object, 0u);
}

// The id map's value for `id`, or null.
void *FindIn(FolderIdMap &map, uint32_t id)
{
    auto it = map.find(id);
    return it == map.end() ? nullptr : it->second.Get();
}

// 0x4d76a0 (__thiscall on the folder, ret 0x14, original): loads the object (group, instance): fills *out (counted) when out
// is not null, `load` 1 for a load that keeps the object; `name` the neighbourhood text or null. True when it loaded one.
bool LoadObject(cTSObjectFolder *folder, uint32_t group, uint16_t instance, void **out, uint32_t load, const char *name)
{
    using Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, const char *);
    return OriginalFunction<Fn, 0x4d76a0>()(static_cast<cITSBehaviorFinder *>(folder), group, instance, out, load, name) != 0;
}

// The data value `index` of an object (its data object's slot 3).
uint32_t DataOf(void *object, uint32_t index) { return Call<3>(Call<4, void *>(object), index); }

// The original's setup of a loaded object's ownership data (Import / AddLotObjectCopy): 9 is 0x10 -> 2; 2 and 0x11 stay;
// anything else gets 0xd2 = 0xb39360() (a constant getter, original).
void FixObjectKind(void *object)
{
    int32_t kind = static_cast<int32_t>(Call<3>(object, 9u));
    if (kind == 0x10) {
        Call<6>(object, 9u, 2u);
    } else if (kind != 2 && kind != 0x11) {
        uint32_t value = OriginalFunction<uint32_t(__cdecl *)(), 0xb39360>()();
        Call<6>(object, 0xd2u, value);
    }
}

// The framework's object (GetDirectorFrameworkObject) slot 7: fills the default owner value.
void DefaultOwner(uint32_t *out) { Call<7>(GetDirectorFrameworkObject(), out); }

// The neighbourhood-text maps of LoadGlobals: id -> name, std::hash (FNV-1a) as the original's.
using NameMap = std::unordered_map<uint32_t, FolderName>;
static_assert(sizeof(NameMap) == 0x20);

// A new GUID translation table (0x14 bytes, vtable 0xdcc87c, zeroed), counted once; null when the allocation failed.
void *NewGuidTranslation()
{
    auto *table = static_cast<uint32_t *>(::operator new(0x14));
    if (!table) return nullptr;
    table[1] = 0;
    table[2] = 0;
    table[3] = 0;
    table[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress(kGuidTranslationVTable)));
    table[4] = 0;
    AddRefObject(table);
    return table;
}

// A directory scan on the caller's stack, as the neighbourhood code uses it: our ::DirectoryScan (src/rz/DirectoryScan.h;
// lane LY switched it from the original callees), built and destroyed through its adapters, the addressable entry points
// tests stand in for.
struct FolderScan {
    alignas(::DirectoryScan) uint8_t bytes[sizeof(::DirectoryScan)];
    ::DirectoryScan *scan;
    explicit FolderScan(const cRZString *directory)
        : scan(DirectoryScanAdapters::Construct(bytes, nullptr, directory, false, false, false))
    {
    }
    FolderScan(const FolderScan &) = delete;
    FolderScan &operator=(const FolderScan &) = delete;
    ~FolderScan() { DirectoryScanAdapters::Destruct(scan, nullptr); }
    void Find(cRZString *pattern) { scan->SetPattern(pattern, true); }
    uint32_t Count() const { return scan->mCount; }
    void EntryPath(uint32_t index, cRZString *out) { scan->EntryAt(index)->FullPath(out); }
};

// 0x1c8674 (45 bytes, as cTSNeighborhoodManagerLoad has it inline): the name part of a file name, split off the full path first
// when it is still empty.
cRZString *NamePart(cRZFileName &fileName)
{
    if (fileName.mName.mString.empty() && !fileName.mFull.mString.empty())
        cRZFileName::SplitInto(&fileName.mFull, nullptr, nullptr, &fileName.mName, nullptr);
    return &fileName.mName;
}

} // namespace

// ---- FolderRef / FolderName ----

FolderRef::FolderRef(void *object) : mObject(object)
{
    if (object) AddRefObject(object);
}

FolderRef::~FolderRef()
{
    if (mObject) ReleaseObject(mObject);
}

void FolderRef::Assign(void *object)
{
    if (object == mObject) return;
    if (object) AddRefObject(object);
    void *old = mObject;
    mObject = object;
    if (old) ReleaseObject(old);
}

void **FolderRef::Reset()
{
    if (void *old = mObject) {
        mObject = nullptr;
        ReleaseObject(old);
    }
    return &mObject;
}

bool FolderNameLess::operator()(const FolderName &a, const FolderName &b) const
{
    return _stricmp(a.s.Data(), b.s.Data()) < 0;
}

// ---- construction ----

cITSObjectFolder *__cdecl cTSObjectFolder::Create()
{
    auto *folder = new (std::nothrow) cTSObjectFolder;
    return folder ? static_cast<cITSObjectFolder *>(folder) : nullptr;
}

cTSObjectFolder::cTSObjectFolder()
{
    for (uint32_t id : {0x7c26430fu, 0xa2u, 0xc3u, 0xc4u, 0xc5u, 0xb5u, 0x8ecc1b6du}) mMessageIds.insert(id);
    mRegistered = true;
}

cTSObjectFolder::~cTSObjectFolder() = default;

bool cTSObjectFolder::QueryInterface(uint32_t riid, void **out)
{
    if (riid == 1 || riid == kFolderIid) {
        *out = static_cast<cITSObjectFolder *>(this);
    } else if (riid == kFinderIid) {
        *out = static_cast<cITSBehaviorFinder *>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

uint32_t cTSObjectFolder::Release()
{
    if (--mRefCount == 0) {
        delete this;
        return 0;
    }
    return mRefCount;
}

// ---- cITSBehaviorFinder ----

// Writes an object's save index (its behaviour's slot 10 object's slot 57, as 16 bits), or -1 for none.
void cTSObjectFolder::WriteObjectRef(void *stream, void **object)
{
    uint32_t value = 0xffffffff;
    if (*object) {
        if (void *part = Call<37, void *>(*object)) value = Call<57, uint16_t>(Call<10, void *>(part));
    }
    Call<6>(stream, value);
}

// Reads an object reference: before version 0x2f an object id (FindObject), from 0x2f on a 16-bit save index.
void cTSObjectFolder::ReadObjectRef(void *stream, void **out)
{
    const uint32_t *version = Call<78, const uint32_t *>(Call<20, void *>(stream));
    if (*version < 0x2f) {
        uint32_t id;
        Call<9>(stream, &id);
        void *object = static_cast<cITSObjectFolder *>(this)->FindObject(id);
        *out = object ? Call<18, void *>(object) : nullptr;
        return;
    }
    int16_t index;
    Call<6>(stream, &index);
    *out = nullptr;
    if (index <= 0 || static_cast<uint32_t>(index) >= mSaveIndex.size()) return;
    uint32_t id = mSaveIndex[index];
    void *object;
    auto it = mObjectsById.find(id);
    if (it != mObjectsById.end()) {
        object = it->second.Get();
    } else {
        if (!mGlobalIds) return;
        auto global = mGlobalIds->find(id);
        if (global == mGlobalIds->end()) return;
        object = global->second.Get();
    }
    if (!object) return;
    if (!Call<63, uint8_t>(object)) Call<62>(object, 1u);
    *out = Call<18, void *>(object);
}

void cTSObjectFolder::DetachObject(void *object)
{
    Call<19>(object, 0u);
    Call<5>(object, 0u);
    Call<55>(object, 0u);
    Call<23>(object, 0u);
}

// Gives an object its OBJD resource and a fresh behaviour (framework class 0x1b269450) built from its framework parts.
void cTSObjectFolder::AttachObjectResource(void *object)
{
    void *catalog = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    int16_t instance = Call<45, int16_t>(object);
    uint32_t group = Call<66>(object);
    ResourceKey key{kTypeObjd, group, static_cast<uint32_t>(static_cast<int32_t>(instance)), 0};
    LocalRef resource;
    Call<5>(catalog, &key, kResourceIid, &resource.p, 0u, 0u);
    if (!resource.p) return;
    Call<5>(object, resource.p);
    LocalRef behavior;
    Call<3>(GetDirectorFrameworkObject(), 0x1b269450u, 0x7b269499u, &behavior.p);
    void *partB = Call<70, void *>(object);
    void *partA = Call<67, void *>(object);
    uint32_t built = Call<21>(object, mFramework.Get(), partA, partB);
    uint32_t id = Call<28>(object);
    Call<3>(behavior.p, id, built);
    Call<19>(object, behavior.p);
    Call<23>(object, 0u);
    if (Call<10>(Call<67, void *>(object)) == 1) LinkToContainer(object);
}

void cTSObjectFolder::ReloadObjectResource(void *object)
{
    void *catalog = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    int16_t instance = Call<45, int16_t>(object);
    uint32_t group = Call<66>(object);
    ResourceKey key{kTypeObjd, group, static_cast<uint32_t>(static_cast<int32_t>(instance)), 0};
    LocalRef resource;
    Call<5>(catalog, &key, kResourceIid, &resource.p, 0u, 0u);
    if (resource.p) Call<5>(object, resource.p);
}

// Links an object in a container (data 9 is 2) to the container (data 0xcb's object, whose data 9 is 0x10).
void cTSObjectFolder::LinkToContainer(void *object)
{
    void *data = Call<4, void *>(object);
    if (!data || Call<3>(data, 9u) != 2) return;
    void *container = static_cast<cITSObjectFolder *>(this)->FindObject(Call<3>(data, 0xcbu));
    if (!container) return;
    void *containerData = Call<4, void *>(container);
    if (!containerData || Call<3>(containerData, 9u) != 0x10) return;
    void *containerBehavior = Call<18, void *>(container);
    void *behavior = Call<18, void *>(object);
    Call<8>(behavior, Call<11>(containerBehavior, Call<16>(containerBehavior)));
    uint32_t x = Call<40>(container);
    uint32_t y = Call<42>(container);
    Call<41>(object, x);
    Call<43>(object, y);
}

// ---- cITSObjectFolder: start and stop ----

// Builds the lot's framework object (class 0x0b54e37d) in the "global" group, loads the global objects, makes the lot object
// (class 0x5b12bf90) when there is none, sets the game options from the settings and the memory, registers for the messages.
void cTSObjectFolder::Init()
{
    LocalString global(GameAddress<const char>(kGlobalText));
    uint32_t group = Call<11>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(), &global.s);
    void *framework = GetDirectorFrameworkObject();
    Call<3>(framework, 0x0b54e37du, 0xeb54e473u, mFramework.Reset());
    Call<5>(mFramework.Get(), group, 4u);
    Call<15>(mFramework.Get(), &global.s);
    {
        LocalString text;
        appinit::CommonImplSlot4(0x2bd047c9, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&text.s)), 1, 0, 0xffffffff);
        Call<58>(GetCachedSingletonC(), &text.s);
        static_cast<cITSObjectFolder *>(this)->LoadGlobals();
        if (!mLotObject.Get()) {
            void *director = GetDirectorFrameworkObject();
            Call<3>(director, 0x5b12bf90u, 0x5b12bf88u, mLotObject.Reset());
            uint32_t value = Call<11>(mFramework.Get());
            Call<68>(mLotObject.Get(), &value, mFramework.Get());
            Call<77>(mLotObject.Get(), value);
        }
        ScopedRef settings{nullptr};
        if (void *service = GetServiceObject()) Call<5>(service, 0x0ac1887eu, 0x4bb0d269u, &settings.mObject);
        gz::AnyBoolean preload(0);
        {
            GZServiceString name(GameAddress<const char>(kPreloadSettingText));
            Call<9>(settings.mObject, &name, static_cast<cIGZAny *>(&preload));
        }
        appbase::cRZVersion version;
        version.Construct();
        uint32_t total = 0, available = 0;
        version.GetMemory(&total, &available);
        if (!preload.mValue && total < 0x200) {
            Call<73>(GetCachedSingletonC(), 0x54544142u, 0u);
        } else {
            Call<73>(GetCachedSingletonC(), 0x54544142u, 0u);
            Call<73>(GetCachedSingletonC(), 0x42484156u, 0u);
        }
        Call<73>(GetCachedSingletonC(), 0x42434f4eu, 0u);
        mLoaded = true;
        reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)
            ->CallSlot5ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))),
                                  nullptr);
        {
            StandardMessage message;
            message.Post(kMsgObjectsChanged);
        }
        version.Destruct();
        settings.ReleaseObject();
    }
}

void cTSObjectFolder::Shutdown()
{
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)
        ->CallSlot6ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))), nullptr);
    mRegistered = false;
    ReleaseLotScripts();
    Call<59>(GetCachedSingletonC());
    ClearObjects();
    mFramework.Reset();
    mLoaded = false;
    StandardMessage message;
    message.Post(kMsgObjectsChanged);
}

void cTSObjectFolder::ReleaseLotScripts()
{
    if (mLoaded) mLotScripts.clear();
}

void cTSObjectFolder::DetachObjectFully(void *object)
{
    DetachLinks(object);
    Call<31>(object, 0u);
    if (static_cast<int32_t>(Call<73>(object)) != -1) DetachLinks(mPortals[Call<73>(object)].Get());
}

void cTSObjectFolder::ClearObjects()
{
    for (uint32_t i = 0; i < mObjects.size(); ++i) DetachObjectFully(mObjects[i].Get());
    mObjects.clear();
    mObjectsById.clear();
    for (uint32_t i = 0; i < mPortals.size(); ++i) DetachLinks(mPortals[i].Get());
    mPortals.clear();
    DetachLinks(mLotObject.Get());
    mLotObject.Reset();
    mGlobals = nullptr;
    mGlobalIds = nullptr;
    for (auto &entry : mGroupObjects) {
        for (uint32_t i = 0; i < entry.second.size(); ++i) DetachLinks(entry.second[i].Get());
        entry.second.clear();
    }
    mGroupObjects.clear();
    mGroupIds.clear();
}

// ---- cITSObjectFolder: lookups ----

uint32_t cTSObjectFolder::GetSelectedGlobalValue()
{
    if (!mLoaded) return 0;
    for (auto &object : *mGlobals)
        if (Call<38, uint8_t>(object.Get())) return Call<15>(Call<18, void *>(object.Get()));
    return 0;
}

int32_t cTSObjectFolder::GetObjectCount() { return mLoaded ? static_cast<int32_t>(mObjects.size()) : 0; }

int32_t cTSObjectFolder::GetAllObjectCount()
{
    if (!mLoaded) return 0;
    uint32_t globals = mGlobals ? static_cast<uint32_t>(mGlobals->size()) : 0;
    return static_cast<int32_t>(mObjects.size() + globals);
}

int32_t cTSObjectFolder::GetPortalCount() { return static_cast<int32_t>(mPortals.size()); }

void *cTSObjectFolder::GetObjectAt(int32_t index)
{
    if (!mLoaded || index < 0) return nullptr;
    uint32_t i = static_cast<uint32_t>(index);
    uint32_t count = static_cast<uint32_t>(mObjects.size());
    if (i < count) return mObjects[i].Get();
    if (!mGlobals || i >= count + mGlobals->size()) return nullptr;
    return (*mGlobals)[i - count].Get();
}

void *cTSObjectFolder::FindObject(uint32_t id)
{
    if (!mLoaded && !mImporting && !mLoadingGlobals) return nullptr;
    auto it = mObjectsById.find(id);
    if (it != mObjectsById.end()) return it->second.Get();
    if (!mGlobalIds) return nullptr;
    return FindIn(*mGlobalIds, id);
}

void *cTSObjectFolder::FindObjectByObjectId(int32_t id)
{
    if (!mLoaded) return nullptr;
    for (uint32_t i = 0; i < mObjects.size(); ++i)
        if (Call<18>(mObjects[i].Get()) == static_cast<uint32_t>(id)) return mObjects[i].Get();
    if (!mGlobals) return nullptr;
    for (uint32_t i = 0; i < mGlobals->size(); ++i)
        if (Call<18>((*mGlobals)[i].Get()) == static_cast<uint32_t>(id)) return (*mGlobals)[i].Get();
    return nullptr;
}

void *cTSObjectFolder::FindObjectByName(const char *name)
{
    if (!mLoaded) return nullptr;
    for (uint32_t i = 0; i < mObjects.size(); ++i) {
        void *object = mObjects[i].Get();
        if (_stricmp(Call<8, const char *>(object), name) == 0) return object;
    }
    if (!mGlobals) return nullptr;
    for (uint32_t i = 0; i < mGlobals->size(); ++i) {
        void *object = (*mGlobals)[i].Get();
        if (_stricmp(Call<8, const char *>(object), name) == 0) return object;
    }
    return nullptr;
}

void *cTSObjectFolder::FindObjectBySaveIndex(int32_t index)
{
    if (!mLoaded || index <= 0 || static_cast<uint32_t>(index) >= mSaveIndex.size()) return nullptr;
    uint32_t id = mSaveIndex[index];
    auto it = mObjectsById.find(id);
    if (it != mObjectsById.end()) return it->second.Get();
    if (!mGlobalIds) return nullptr;
    return FindIn(*mGlobalIds, id);
}

void *cTSObjectFolder::GetLotObject() { return mLoaded ? mLotObject.Get() : nullptr; }

void *cTSObjectFolder::GetPortalAt(uint32_t index)
{
    if (!mLoaded || index >= mPortals.size()) return nullptr;
    return mPortals[index].Get();
}

// The object contained in `object` (one that answers its slot 11) whose data matches: data 10 the container's group, 0xb the
// container's tile at (x, y), 0x38 `slot`.
void *cTSObjectFolder::FindContainedAt(void *object, int32_t x, int32_t y, int32_t slot)
{
    if (!mLoaded || x < 0 || y < 0) return nullptr;
    void *data = Call<4, void *>(object);
    if (Call<8, uint8_t>(data)) return nullptr;
    uint16_t group = Call<3, uint16_t>(data, 10u);
    uint16_t tile = Call<11, uint16_t>(data, x, y);
    cITSObjectFolder *face = this;
    for (int32_t i = 0; i < face->GetAllObjectCount(); ++i) {
        void *other = face->GetObjectAt(static_cast<int16_t>(i));
        if (!Call<11, uint8_t>(other, object)) continue;
        void *otherData = Call<4, void *>(other);
        if (static_cast<int32_t>(Call<3>(otherData, 10u)) != static_cast<int16_t>(group)) continue;
        if (static_cast<int32_t>(Call<3>(otherData, 0xbu)) != static_cast<int16_t>(tile)) continue;
        if (static_cast<int32_t>(Call<3>(otherData, 0x38u)) == slot) return other;
    }
    return nullptr;
}

// The lead member of a multi-tile object (data 0x3f set), else FindContainedAt(object, 0, 0, 0).
void *cTSObjectFolder::FindLeadContained(void *object)
{
    if (!mLoaded) return nullptr;
    void *data = Call<4, void *>(object);
    if (Call<8, uint8_t>(data)) return nullptr;
    uint16_t group = Call<3, uint16_t>(data, 10u);
    cITSObjectFolder *face = this;
    for (int32_t i = 0; i < face->GetAllObjectCount(); ++i) {
        void *other = face->GetObjectAt(static_cast<int16_t>(i));
        if (!Call<11, uint8_t>(other, object)) continue;
        void *otherData = Call<4, void *>(other);
        if (static_cast<int32_t>(Call<3>(otherData, 10u)) != static_cast<int16_t>(group)) continue;
        if (Call<3>(otherData, 0x3fu) != 0) return other;
    }
    return face->FindContainedAt(object, 0, 0, 0);
}

void *cTSObjectFolder::FindFirstMultitileMember(void *object)
{
    if (!mLoaded) return nullptr;
    void *data = Call<4, void *>(object);
    if (Call<8, uint8_t>(data)) return object;
    uint16_t group = Call<3, uint16_t>(data, 10u);
    cITSObjectFolder *face = this;
    for (int32_t i = 0; i < face->GetAllObjectCount(); ++i) {
        void *other = face->GetObjectAt(i);
        if (!Call<11, uint8_t>(other, object)) continue;
        void *otherData = Call<4, void *>(other);
        Call<9>(otherData);
        Call<3>(otherData, 10u);
        if (static_cast<int32_t>(Call<3>(otherData, 10u)) != static_cast<int16_t>(group)) continue;
        if (Call<9, uint8_t>(otherData)) return other;
    }
    return nullptr;
}

void *cTSObjectFolder::FindNthMultitileMember(void *object, int32_t n)
{
    if (!mLoaded) return nullptr;
    void *data = Call<4, void *>(object);
    if (Call<8, uint8_t>(data)) return nullptr;
    uint16_t group = Call<3, uint16_t>(data, 10u);
    cITSObjectFolder *face = this;
    for (int32_t i = 0; i < face->GetAllObjectCount(); ++i) {
        void *other = face->GetObjectAt(static_cast<int16_t>(i));
        if (!Call<11, uint8_t>(other, object)) continue;
        void *otherData = Call<4, void *>(other);
        if (static_cast<int32_t>(Call<3>(otherData, 10u)) != static_cast<int16_t>(group)) continue;
        if (Call<9, uint8_t>(otherData)) continue;
        if (n == 0) return other;
        --n;
    }
    return nullptr;
}

// ---- cITSObjectFolder: loading ----

// Drops the cached resources, then loads every global object: the GLOB records name the neighbourhoods (0x4d8850), and each
// OBJD record is loaded (0x4d76a0) under its neighbourhood's name; the custom objects (0x4cfc20) and the import (0x4d5d00) too.
void cTSObjectFolder::LoadGlobals()
{
    if (!mCachedResources.empty()) {
        void *persist = Call<3, void *>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
        for (const FolderRef &resource : mCachedResources)
            if (resource.Get()) Call<30>(persist, resource.Get());
        mCachedResources.clear();
    }
    void *catalog = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    void *persist = Call<3, void *>(catalog);
    NameMap namesByGroup;  // the GLOB record's group -> neighbourhood name
    NameMap namesByNameId; // the name's group id -> neighbourhood name
    {
        KeyVector globs;
        Call<19>(persist, &globs, kTypeGlob);
        LocalString name;
        for (const ResourceKey &key : globs) {
            uint32_t group = key.group;
            using ReadName = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, cRZString *);
            if (!OriginalFunction<ReadName, 0x4d8850>()(static_cast<cITSBehaviorFinder *>(this), group, key.instance, &name.s))
                continue;
            uint32_t nameId = Call<11>(catalog, &name.s);
            FolderName &byId = namesByNameId[nameId];
            if (&byId.s.mString != &name.s.mString) byId.s.mString.assign(name.s.mString.data(), name.s.mString.size());
            FolderName &byGroup = namesByGroup[group];
            if (&byGroup.s.mString != &name.s.mString) byGroup.s.mString.swap(name.s.mString);
        }
    }
    KeyVector objects;
    uint32_t count = Call<19>(persist, &objects, kTypeObjd);
    uint32_t skipEvery = OriginalFunction<uint32_t(__cdecl *)(), 0x4d3980>()();
    bool any = false;
    uint32_t counter = 0;
    ScopedRef settings{nullptr};
    if (void *service = GetServiceObject()) Call<5>(service, 0x0ac1887eu, 0x4bb0d269u, &settings.mObject);
    gz::AnyBoolean disableCustom(0);
    {
        GZServiceString name(GameAddress<const char>(kCustomSettingText));
        Call<9>(settings.mObject, &name, static_cast<cIGZAny *>(&disableCustom));
    }
    using LoadCustom = void(__thiscall *)(void *, uint32_t, NameMap *);
    OriginalFunction<LoadCustom, 0x4cfc20>()(static_cast<cITSBehaviorFinder *>(this), disableCustom.mValue, &namesByNameId);
    for (uint32_t i = 0; static_cast<int32_t>(count) > 0 && i < count; ++i) {
        uint32_t group = objects.data()[i].group;
        auto it = namesByGroup.find(group);
        const char *name = it != namesByGroup.end() ? it->second.s.Data() : nullptr;
        if (skipEvery) {
            if (++counter == skipEvery) {
                counter = 0;
                continue;
            }
        }
        any |= LoadObject(this, objects.data()[i].group, static_cast<uint16_t>(objects.data()[i].instance), nullptr, 1, name);
    }
    any |= OriginalFunction<uint32_t(__thiscall *)(void *), 0x4d5d00>()(static_cast<cITSBehaviorFinder *>(this)) != 0;
    if (any) {
        StandardMessage message;
        message.Post(kMsgObjectsChanged);
    }
    settings.ReleaseObject();
}

// Loads the OBJD records of one group (the catalogue's slot 36); the loaded objects join `loaded` when it is given.
bool cTSObjectFolder::LoadGroupObjects(uint32_t group, FolderObjectSet *loaded, bool notify)
{
    KeyVector keys;
    uint32_t count = Call<36>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(), &keys, group, kTypeObjd);
    bool any = false;
    for (uint32_t i = 0; i < count; ++i) {
        LocalRef object;
        any |= LoadObject(this, keys.data()[i].group, static_cast<uint16_t>(keys.data()[i].instance), &object.p, 0, nullptr);
        if (object.p && loaded) loaded->insert(object.p);
    }
    if (any && notify) {
        StandardMessage message;
        message.Post(kMsgObjectsChanged);
    }
    return true;
}

// Loads the OBJD records of a package (its slot 19 through a type filter).
bool cTSObjectFolder::LoadPackageObjects(void *package, FolderObjectSet *loaded)
{
    if (!package) return false;
    bool any = false;
    KeyVector keys;
    void *filter = ::operator new(0xc);
    if (filter) {
        auto *words = static_cast<uint32_t *>(filter);
        words[1] = 0;
        words[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress(kTypeFilterVTable)));
        words[2] = kTypeObjd;
        AddRefObject(filter);
    }
    LocalRef filterRef(filter);
    Call<19>(package, &keys, filter);
    int32_t count = static_cast<int32_t>(keys.size());
    if (count > 0) {
        for (int32_t i = 0; i < count; ++i) {
            LocalRef object;
            bool loadedOne =
                LoadObject(this, keys.data()[i].group, static_cast<uint16_t>(keys.data()[i].instance), &object.p, 0, nullptr);
            if (loadedOne && object.p) {
                any = true;
                if (loaded) loaded->insert(object.p);
            }
        }
        if (any) {
            StandardMessage message;
            message.Post(kMsgObjectsChanged);
        }
    }
    return true;
}

// Takes an object out of the folder (the lot's or the global list and the id maps), tells the server, drops its resource.
bool cTSObjectFolder::RemoveObject(void *object)
{
    if (!mLoaded && !mImporting && !mLoadingGlobals) return false;
    void *catalog = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    FolderRef held;
    uint32_t id;
    auto it = mObjects.begin();
    for (; it != mObjects.end(); ++it)
        if (it->Get() == object) break;
    if (it != mObjects.end()) {
        id = Call<28>(object);
        held = *it;
        mObjects.erase(it);
    } else {
        auto global = mGlobals->begin();
        for (;; ++global) {
            if (global == mGlobals->end()) return false;
            if (global->Get() == object) break;
        }
        id = Call<28>(object);
        held = *global;
        mGlobals->erase(global);
    }
    auto byId = mObjectsById.find(id);
    if (byId != mObjectsById.end()) mObjectsById.erase(byId);
    if (mGlobalIds) {
        auto globalId = mGlobalIds->find(id);
        if (globalId != mGlobalIds->end()) mGlobalIds->erase(globalId);
    }
    if (!mImporting && !mLoadingGlobals) {
        StandardMessage message;
        message.Post(kMsgObjectRemoved, id);
        message.Post(kMsgObjectsChanged, 0);
    }
    void *resource = Call<75, void *>(object);
    void *persist = Call<3, void *>(catalog);
    uint8_t keyBuffer[16];
    Call<10>(persist, Call<3>(resource, keyBuffer));
    DetachObjectFully(held.Get());
    return true;
}

// Unloads an object's resources (behaviour slot 28, framework parts Unload(1)) and the lot framework's.
void cTSObjectFolder::UnloadObjectResources(void *object)
{
    if (!object) return;
    StandardMessage message;
    message.Post(kMsgResourcesUnloaded, 0);
    Call<59>(GetCachedSingletonC());
    if (mFramework.Get()) {
        if (mLotObject.Get() && Call<18, void *>(mLotObject.Get())) Call<28>(Call<18, void *>(mLotObject.Get()));
        Call<8>(mFramework.Get(), 1u);
    }
    if (void *behavior = Call<18, void *>(object)) {
        Call<28>(behavior);
        if (void *part = Call<67, void *>(object)) Call<8>(part, 1u);
        if (void *part = Call<70, void *>(object)) Call<8>(part, 1u);
    }
    void *catalog = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    Call<39>(catalog);
    Call<32>(Call<3, void *>(catalog));
}

void cTSObjectFolder::ReloadObjectResources(void *object)
{
    if (!object) return;
    LocalString text;
    appinit::CommonImplSlot4(0x2bd047c9, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&text.s)), 1, 0, 0xffffffff);
    Call<58>(GetCachedSingletonC(), &text.s);
    if (mFramework.Get()) {
        Call<7>(mFramework.Get());
        if (mLotObject.Get() && Call<18, void *>(mLotObject.Get())) Call<5>(Call<18, void *>(mLotObject.Get()));
    }
    Call<74>(object);
    if (void *part = Call<67, void *>(object)) Call<7>(part);
    if (void *part = Call<70, void *>(object)) Call<7>(part);
    if (void *behavior = Call<18, void *>(object)) Call<5>(behavior);
    ReloadObjectResource(object);
    StandardMessage message;
    message.Post(kMsgResourcesReloaded, 0);
}

void cTSObjectFolder::UnloadAllResources()
{
    if (!mLoaded) return;
    StandardMessage message;
    message.Post(kMsgResourcesUnloaded, 0);
    Call<59>(GetCachedSingletonC());
    if (mFramework.Get()) {
        if (mLotObject.Get() && Call<18, void *>(mLotObject.Get())) Call<28>(Call<18, void *>(mLotObject.Get()));
        Call<8>(mFramework.Get(), 1u);
    }
    for (auto &entry : mObjects) {
        if (void *behavior = Call<18, void *>(entry.Get())) Call<28>(behavior);
        if (void *part = Call<67, void *>(entry.Get())) Call<8>(part, 1u);
        if (void *part = Call<70, void *>(entry.Get())) Call<8>(part, 1u);
    }
    for (auto &entry : mPortals) {
        if (void *behavior = Call<18, void *>(entry.Get())) Call<28>(behavior);
        if (void *part = Call<67, void *>(entry.Get())) Call<8>(part, 1u);
    }
    void *catalog = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    Call<39>(catalog);
    Call<32>(Call<3, void *>(catalog));
}

void cTSObjectFolder::ReloadAllResources()
{
    if (!mLoaded) return;
    Call<43>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(), 0xfu);
    LocalString text;
    appinit::CommonImplSlot4(0x2bd047c9, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&text.s)), 1, 0, 0xffffffff);
    Call<58>(GetCachedSingletonC(), &text.s);
    if (mFramework.Get()) {
        Call<7>(mFramework.Get());
        if (mLotObject.Get() && Call<18, void *>(mLotObject.Get())) Call<5>(Call<18, void *>(mLotObject.Get()));
    }
    for (auto &entry : mObjects) {
        void *object = entry.Get();
        if (!object) continue;
        Call<74>(object);
        if (void *part = Call<67, void *>(object)) Call<7>(part);
        if (void *part = Call<70, void *>(object)) Call<7>(part);
        if (void *behavior = Call<18, void *>(object)) Call<5>(behavior);
        ReloadObjectResource(object);
    }
    for (auto &entry : mPortals) {
        void *object = entry.Get();
        if (!object) continue;
        Call<74>(object);
        if (void *part = Call<67, void *>(object)) Call<7>(part);
        if (void *behavior = Call<18, void *>(object)) Call<5>(behavior);
    }
    StandardMessage message;
    message.Post(kMsgResourcesReloaded, 0);
}

// ---- cITSObjectFolder: the save table ----

// Reads the lot's save table: the objects' save indices cleared, the lot scripts dropped, the package's GUID translation read
// (0xc0b669), the table read (catalogue slot 73), then the save index rebuilt from the objects' indices.
void cTSObjectFolder::ReadSaveTable(void *package)
{
    if (!mLoaded) return;
    for (auto &entry : mObjects) Call<58>(entry.Get(), 0u);
    for (auto &entry : *mGlobals) Call<58>(entry.Get(), 0u);
    ReleaseLotScripts();
    LocalRef translation(NewGuidTranslation());
    OriginalFunction<void(__thiscall *)(void *, void *), 0xc0b669>()(translation.p, package);
    {
        ObjectSaveTypeTable table(this, &mLotScripts, static_cast<GUIDTranslation *>(translation.p));
        Call<73>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(), &table, package, 0u, 0u);
        mSaveIndex.clear();
        mSaveIndex.push_back(0);
        auto record = [this](void *object) {
            int16_t index = static_cast<int16_t>(Call<57, uint16_t>(object));
            if (index <= 0) return;
            while (static_cast<uint32_t>(index) >= mSaveIndex.size()) mSaveIndex.push_back(0);
            uint32_t *slot = &mSaveIndex[index];
            *slot = Call<28>(object);
        };
        for (auto &entry : mObjects) record(entry.Get());
        for (auto &entry : *mGlobals) record(entry.Get());
    }
}

// Writes the lot's save table: every object to be saved (slot 63) gets the next save index, the others 0.
void cTSObjectFolder::WriteSaveTable(void *package)
{
    if (!mLoaded) return;
    ReleaseLotScripts();
    uint32_t next = 1;
    mSaveIndex.push_back(0);
    auto number = [this, &next](void *object) {
        if (Call<63, uint8_t>(object)) {
            Call<58>(object, next);
            ++next;
            mSaveIndex.push_back(Call<28>(object));
        } else {
            Call<58>(object, 0u);
        }
    };
    for (auto &entry : mObjects) number(entry.Get());
    for (auto &entry : *mGlobals) number(entry.Get());
    LocalRef translation(NewGuidTranslation());
    ObjectSaveTypeTable table(this, &mLotScripts, static_cast<GUIDTranslation *>(translation.p));
    Call<72>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(), &table, package, 0u);
}

void cTSObjectFolder::ClearSaveIndices()
{
    mSaveIndex.clear();
    for (auto &entry : mObjects) Call<58>(entry.Get(), 0u);
    for (auto &entry : *mGlobals) Call<58>(entry.Get(), 0u);
}

bool cTSObjectFolder::AnyLotScriptBusy()
{
    if (!mLoaded) return false;
    for (auto &script : mLotScripts)
        if (Call<6>(script.Get()) != 2) return true;
    return false;
}

void *cTSObjectFolder::FindLotScript(int32_t id)
{
    if (!mLoaded) return nullptr;
    for (auto &script : mLotScripts)
        if (script.Get() && static_cast<int32_t>(Call<7, int16_t>(script.Get())) == id) return script.Get();
    return nullptr;
}

// ---- cITSObjectFolder: the special objects ----

void *cTSObjectFolder::FindObject37() { return static_cast<cITSObjectFolder *>(this)->FindObject(0x3c08965f); }
void *cTSObjectFolder::FindObject38() { return static_cast<cITSObjectFolder *>(this)->FindObject(0x7fd96b54); }
void *cTSObjectFolder::FindObject39() { return static_cast<cITSObjectFolder *>(this)->FindObject(0x4c859106); }
void *cTSObjectFolder::FindObject40() { return static_cast<cITSObjectFolder *>(this)->FindObject(0xacc53dff); }
void *cTSObjectFolder::FindObject41() { return static_cast<cITSObjectFolder *>(this)->FindObject(0xec85f0f3); }
void *cTSObjectFolder::FindObject42() { return static_cast<cITSObjectFolder *>(this)->FindObject(0x7fd96b54); }
void *cTSObjectFolder::FindObject44() { return static_cast<cITSObjectFolder *>(this)->FindObject(0x7c4); }
void *cTSObjectFolder::FindObject45() { return static_cast<cITSObjectFolder *>(this)->FindObject(0xecfedb37); }
void *cTSObjectFolder::FindObject46() { return static_cast<cITSObjectFolder *>(this)->FindObject(0xb21ada68); }
void *cTSObjectFolder::FindObject47() { return static_cast<cITSObjectFolder *>(this)->FindObject(0x532fb5a1); }
void *cTSObjectFolder::FindObject48() { return static_cast<cITSObjectFolder *>(this)->FindObject(0x6cd4a8bc); }

bool cTSObjectFolder::IsObject39To41(void *object)
{
    cITSObjectFolder *face = this;
    if (object == face->FindObject39()) return true;
    if (object == face->FindObject40()) return true;
    return object == face->FindObject41();
}

bool cTSObjectFolder::IsObject44Or45(void *object)
{
    cITSObjectFolder *face = this;
    return object == face->FindObject44() || object == face->FindObject45();
}

bool cTSObjectFolder::IsObject46(void *object) { return object == static_cast<cITSObjectFolder *>(this)->FindObject46(); }
bool cTSObjectFolder::IsObject48(void *object) { return object == static_cast<cITSObjectFolder *>(this)->FindObject48(); }

// ---- cITSObjectFolder: import and copies ----

// Imports a lot's objects into new packages under `directory`: for every package of the lot object's group (catalogue slot 48)
// that is not a lot script (0x856c8cf7) a package is opened (catalogue slot 22) named `name` plus the source's suffix (or a copy
// number), and every record of the group is copied over with group -1: TREE and OBJD records are rewritten (OBJD: its name,
// owner and container data), the rest copied by the catalogue (slot 37). Returns whether *out was filled (the last package
// without a suffix answers `riid`); false when a package cannot be made or a record cannot be read.
bool cTSObjectFolder::ImportLotObjects(uint32_t riid, void **out, uint32_t packageArg, void *lotObject, cIGZString *directory,
                                       cIGZString *name, cIGZString *displayName, bool touchPackage, bool keepContainer,
                                       uint32_t owner)
{
    void *catalog = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    uint32_t directoryLength = Call<7>(directory); // Strlen first, then the text (slot 6)
    LocalString path(Call<6, const char *>(directory), directoryLength);
    cRZFileName::NormaliseSeparators(&path.s, '\\');
    Call<11>(catalog, name);
    uint32_t group = Call<66>(lotObject);
    uint32_t treeOwner = owner;
    if (!owner) {
        DefaultOwner(&owner);
        treeOwner = owner;
    }
    folders::EnsureDirectory(&path.s); // 0x1c5031, ours (lane LY)
    ServiceRefVector sources{};
    struct Freed {
        ServiceRefVector &v;
        ~Freed() { v.ReleaseAndFree(); }
    } freed{sources};
    Call<48>(catalog, group, &sources);
    uint32_t copyNumber = 0;
    void **end = sources.mLast;
    for (void **source = sources.mFirst; source != end; ++source) {
        void *sourcePackage = *source;
        LocalRef script;
        if (Call<0, uint8_t>(sourcePackage, 0x856c8cf7u, &script.p)) continue;
        LocalString suffix;
        LocalString fileName;
        Call<11>(sourcePackage, &fileName.s);
        cRZFileName parts;
        parts.Construct(&fileName.s);
        struct PartsGone {
            cRZFileName &f;
            ~PartsGone() { f.Destruct(); }
        } partsGone{parts};
        int32_t at = static_cast<int32_t>(Call<28>(NamePart(parts), GameAddress<const char>(kSuffixSeparatorText), 0u, 1u));
        if (at != -1) {
            alignas(cRZString) uint8_t tail[sizeof(cRZString)];
            cRZString *rest = Sub(*NamePart(parts)).Mid(reinterpret_cast<cRZString *>(tail), static_cast<uint32_t>(at), 0xffffffffu);
            if (&suffix.s.mString != &rest->mString) suffix.s.mString.assign(rest->mString.data(), rest->mString.size());
            rest->Destruct();
        } else if (copyNumber) {
            suffix.s.Sprintf(GameAddress<const char>(kCopyNumberFormat), copyNumber);
            ++copyNumber;
        } else {
            copyNumber = 1;
        }
        LocalRef package;
        uint32_t nameLength = Call<7>(name);
        LocalString packageName(Call<6, const char *>(name), nameLength);
        packageName.s.mString.append(suffix.s.mString.data(), suffix.s.mString.size());
        package.Reset();
        if (!Call<22, uint8_t>(catalog, kPackageType, &package.p, packageArg, &path.s, &packageName.s, 1u, 1u)) return false;
        if (touchPackage) {
            LocalRef edit;
            if (Call<0, uint8_t>(package.p, 0x8ceec300u, &edit.p)) {
                Call<4>(edit.p);
                Call<9>(edit.p, 1u);
            }
        }
        KeyVector keys;
        Call<19>(sourcePackage, &keys, 0u);
        LocalRef sourceDb;
        Call<0>(sourcePackage, kPackageType, &sourceDb.p);
        LocalRef record;
        int32_t count = static_cast<int32_t>(keys.size());
        for (int32_t i = 0; i < count; ++i) {
            ResourceKey *key = &keys.data()[i];
            if (key->group != group || key->type == kTypeFern) continue;
            ResourceKey target{};
            ResourceKey renamed = *key;
            renamed.group = 0xffffffff;
            Call<11>(package.p, &renamed, &target);
            if (key->type == kTypeTree) {
                void *persist = Call<3, void *>(catalog);
                if (Call<5, uint8_t>(persist, key, kPersistIid, record.Reset(), 0u, 0u)) {
                    Call<4>(record.p, &target);
                    LocalRef tree;
                    if (!Call<0, uint8_t>(record.p, 0xeda4decdu, &tree.p)) return false;
                    Call<3>(tree.p, treeOwner);
                    Call<4>(record.p, &target);
                    void *segment = Call<3, void *>(package.p);
                    Call<16>(catalog, record.p, segment);
                    continue;
                }
            } else if (key->type == kTypeObjd) {
                void *persist = Call<3, void *>(catalog);
                if (!Call<5, uint8_t>(persist, key, kPersistIid, record.Reset(), 0u, 0u)) return false;
                Call<4>(record.p, &target);
                LocalRef object;
                if (!Call<0, uint8_t>(record.p, kResourceIid, &object.p)) return false;
                LocalString oldName;
                Call<14>(object.p, &oldName.s);
                LocalString newName;
                newName.s.Assign(displayName ? *displayName : *name);
                if (Call<3>(object.p, 10u) != 0) {
                    LocalString extra;
                    int32_t tile = static_cast<int32_t>(Call<3>(object.p, 0xbu));
                    if (tile != -1) {
                        LocalString tileText;
                        tileText.s.Sprintf(GameAddress<const char>(kTileFormat), tile / 256, tile & 0xff);
                        if (newName.s.Find(*tileText.s.AsInterface(), 0, true) == -1)
                            extra.s.mString.append(tileText.s.mString.data(), tileText.s.mString.size());
                    }
                    if (Call<3>(object.p, 0x3fu) != 0) {
                        if (newName.s.Find(GameAddress<const char>(kLeadText), 0, false) == -1)
                            extra.s.mString.append(GameAddress<const char>(kLeadText), 7);
                    }
                    newName.s.mString.append(extra.s.mString.data(), extra.s.mString.size());
                }
                Call<15>(object.p, &newName.s);
                if (keepContainer) Call<6>(object.p, 0xcbu, Call<3>(object.p, 0xcau));
                if (!owner) DefaultOwner(&owner);
                Call<6>(object.p, 0xcau, owner);
                Call<6>(object.p, 0xd0u, owner);
                owner = 0;
                FixObjectKind(object.p);
                Call<4>(record.p, &target);
                void *segment = Call<3, void *>(package.p);
                Call<16>(catalog, record.p, segment);
                continue;
            }
            void *segment = Call<3, void *>(package.p);
            Call<37>(catalog, &keys.data()[i], sourcePackage, &target, segment);
        }
        Call<10>(Call<3, void *>(package.p));
        if (suffix.s.mString.empty()) Call<0>(package.p, riid, out);
    }
    return *out != nullptr;
}

// Adds a copy of an object's OBJD record (and its OBJf record) to the object's package under the next free instance, named
// `name`. False when the object has no record or package, or a record of the group already has that name.
bool cTSObjectFolder::AddLotObjectCopy(void *object, cIGZString *name)
{
    if (!object) return false;
    uint32_t nameLength = Call<7>(name);
    LocalString text(Call<6, const char *>(name), nameLength);
    uint32_t group = Call<66>(object);
    int16_t instance = Call<45, int16_t>(object);
    ResourceKey key{kTypeObjd, group, static_cast<uint32_t>(static_cast<int32_t>(instance)), 0};
    LocalRef resource;
    void *catalog = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    void *persist = Call<3, void *>(catalog);
    Call<5>(persist, &key, kResourceIid, resource.Reset(), 0u, 0u);
    if (!resource.p) return false;
    void *filter = ::operator new(0x10);
    if (filter) {
        auto *words = static_cast<uint32_t *>(filter);
        words[1] = 0;
        words[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress(kTypeGroupFilterVTable)));
        words[2] = kTypeObjd;
        words[3] = group;
        AddRefObject(filter);
    }
    LocalRef filterRef(filter);
    LocalRef package;
    void *persist2 = Call<3, void *>(catalog);
    Call<34>(persist2, &key, package.Reset());
    if (!package.p) return false;
    LocalRef other;
    LocalString otherName;
    KeyVector keys;
    Call<19>(package.p, &keys, filter);
    uint32_t count = static_cast<uint32_t>(keys.size());
    uint32_t highest = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (keys.data()[i].instance > highest) highest = keys.data()[i].instance;
        other.Reset();
        Call<5>(catalog, &key, kResourceIid, &other.p, 0u, 0u);
        if (!other.p) continue;
        Call<14>(other.p, &otherName.s);
        if (otherName.s.mString.size() == text.s.mString.size() &&
            std::memcmp(text.s.mString.data(), otherName.s.mString.data(), text.s.mString.size()) == 0)
            return false;
    }
    uint32_t newInstance = highest + 1;
    uint32_t owner = 0;
    DefaultOwner(&owner);
    Call<6>(resource.p, 0xcbu, Call<3>(resource.p, 0xcau));
    Call<6>(resource.p, 0xcau, owner);
    FixObjectKind(resource.p);
    Call<15>(resource.p, &text.s);
    LocalRef record;
    if (!Call<0, uint8_t>(resource.p, kPersistIid, &record.p)) return false;
    ResourceKey newKey{kTypeObjd, group, newInstance, 0};
    Call<4>(record.p, &newKey);
    if (!Call<16, uint8_t>(catalog, record.p, package.p)) return false;
    key.type = kTypeObjf;
    LocalRef functions;
    void *persist3 = Call<3, void *>(catalog);
    if (!Call<5, uint8_t>(persist3, &key, kPersistIid, functions.Reset(), 0u, 0u)) return true;
    newKey.type = kTypeObjf;
    Call<4>(functions.p, &newKey);
    return Call<16, uint8_t>(catalog, functions.p, package.p) != 0;
}

// Asks every object's data 0x5a; for each that has one, a tag object (class 0x5a69d325) for (data 0xca, value) joins `list`.
void cTSObjectFolder::CollectTagged(void *list)
{
    cITSObjectFolder *face = this;
    int32_t count = face->GetAllObjectCount();
    for (int32_t i = 0; i < count; ++i) {
        void *object = face->GetObjectAt(i);
        if (!object) continue;
        void *data = Call<4, void *>(object);
        if (!data) continue;
        uint32_t value = Call<3>(data, 0x5au);
        if (!value) continue;
        LocalRef tag;
        void *framework = GetDirectorFrameworkObject();
        Call<3>(framework, 0x5a69d325u, 0x5a69d324u, tag.Reset());
        uint32_t owner = Call<3>(data, 0xcau);
        Call<3>(tag.p, owner, value);
        Call<8>(list, tag.p);
    }
}

void cTSObjectFolder::CollectByAttribute(int32_t value, FolderObjectSet *out)
{
    out->clear();
    cITSObjectFolder *face = this;
    int32_t count = face->GetAllObjectCount();
    for (int32_t i = 0; i < count; ++i) {
        void *object = face->GetObjectAt(i);
        if (object && static_cast<int32_t>(Call<76>(object)) == value) out->insert(object);
    }
}

void cTSObjectFolder::GetPackageInfos(FolderPackageInfos *out)
{
    if (out != &mPackageInfos) out->assign(mPackageInfos.begin(), mPackageInfos.end());
}

// ---- cIGZMessageTarget2 ----

void cTSObjectFolder::SelectNeighborhoodFirstTime(const cRZString &name)
{
    void *catalog = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    std::set<uint32_t> emptyGroups;
    std::set<uint32_t> groups;
    void *options = Call<21, void *>(GetServiceObject());
    gz::AnyBoolean keepOpen(0);
    {
        GZServiceString settingName(GameAddress<const char>(kScanSettingText));
        ScopedRef settings{nullptr};
        if (void *service = GetServiceObject()) Call<5>(service, 0x0ac1887eu, 0x4bb0d269u, &settings.mObject);
        bool found = Call<9, uint8_t>(settings.mObject, &settingName, static_cast<cIGZAny *>(&keepOpen)) != 0;
        settings.ReleaseObject();
        if (!found) keepOpen.mValue = 0;
    }
    if (Call<31, uint8_t>(GetCachedSingleton())) {
        LocalString option(GameAddress<const char>(kScanSettingText), 0x14);
        if (Call<9, uint8_t>(options, &option.s)) keepOpen.mValue = 1;
    }
    LocalString directory;
    using DirectoryOf = void(__cdecl *)(const cRZString *, cRZString *, uint32_t, uint32_t);
    OriginalFunction<DirectoryOf, 0xc0b150>()(&name, &directory.s, 1, 0xffffffff);
    FolderScan scan(&directory.s); // destroyed at the end, after the groups are loaded (0x4d318c)
    {
        {
            LocalString pattern(GameAddress<const char>(kPackagePatternText), 9);
            scan.Find(&pattern.s);
        }
        uint32_t count = scan.Count();
        for (uint32_t i = 0; i < count; ++i) {
            LocalString entry;
            scan.EntryPath(i, &entry.s);
            cRZFileName file;
            file.Construct(&entry.s);
            struct FileGone {
                cRZFileName &f;
                ~FileGone() { f.Destruct(); }
            } fileGone{file};
            LocalString fileName;
            fileName.s.mString.assign(file.GetName()->mString);
            alignas(cRZString) uint8_t pathStorage[sizeof(cRZString)];
            cRZString *path = file.GetDrivePath(reinterpret_cast<cRZString *>(pathStorage));
            struct PathGone {
                cRZString *p;
                ~PathGone() { p->Destruct(); }
            } pathGone{path};
            cRZFileName::NormaliseSeparators(path, '/');
            LocalRef package;
            Call<21>(catalog, kPackageType, &package.p, 1u, path->Data(), fileName.s.Data(), 1u, 1u);
            if (!keepOpen.mValue) Call<26>(catalog, Call<3>(package.p));
            uint32_t group = Call<11>(catalog, &fileName.s);
            groups.insert(group);
        }
    }
    while (!groups.empty()) {
        uint32_t group = *groups.begin();
        groups.erase(groups.begin());
        FolderObjectSet loaded;
        static_cast<cITSObjectFolder *>(this)->LoadGroupObjects(group, &loaded, false);
        if (loaded.empty()) emptyGroups.insert(group);
    }
    while (!emptyGroups.empty()) {
        uint32_t group = *emptyGroups.begin();
        emptyGroups.erase(emptyGroups.begin());
        ServiceRefVector packages{};
        struct Freed {
            ServiceRefVector &v;
            ~Freed() { v.ReleaseAndFree(); }
        } freed{packages};
        Call<48>(catalog, group, &packages);
        for (void **it = packages.mFirst; it != packages.mLast; ++it) {
            void *package = *it;
            LocalString path;
            Call<11>(package, &path.s);
            Call<9>(package);
            Call<4>(package);
            Call<30>(Call<3, void *>(catalog), package);
            if (PathAttributesMatch(path.s.AsInterface(), pathattr::kDirectory))
                OriginalFunction<uint8_t(__cdecl *)(cRZString *, uint32_t), 0x1c5fa0>()(&path.s, 1);
            else
                PathDelete(path.s.AsInterface());
        }
    }
}

bool cTSObjectFolder::DoMessage(void *message)
{
    if (!message) return false;
    LocalRef standard;
    if (!Call<0, uint8_t>(message, kMessage2Iid, &standard.p)) return false;
    uint32_t type = Call<4>(standard.p);
    switch (type) {
    case 0xc3: { // a neighbourhood was added: its (empty) entries
        LocalRef text;
        if (Call<29, uint8_t>(standard.p, kStringIid, text.Reset())) {
            FolderName key(*static_cast<cIGZString *>(text.p));
            if (mGroupObjects.find(key) == mGroupObjects.end()) mGroupObjects.emplace(key, FolderObjects());
            if (mGroupIds.find(key) == mGroupIds.end()) mGroupIds.emplace(key, FolderIdMap());
        }
        return true;
    }
    case 0xb5: { // the catalogue changed: every global object gets its resource again, or leaves
        if (!Call<8, uint8_t>(standard.p) || Call<9>(standard.p) != 0 || !mGlobals) return true;
        void *catalog = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
        std::list<void *> missing;
        for (auto &entry : *mGlobals) {
            void *object = entry.Get();
            if (!object) continue;
            LocalRef resource;
            int16_t instance = Call<45, int16_t>(object);
            uint32_t group = Call<66>(object);
            ResourceKey key{kTypeObjd, group, static_cast<uint32_t>(static_cast<int32_t>(instance)), 0};
            resource.Reset();
            if (Call<5, uint8_t>(catalog, &key, kResourceIid, &resource.p, 0u, 0u))
                Call<5>(object, resource.p);
            else
                missing.push_back(object);
        }
        while (!missing.empty()) {
            void *object = missing.front();
            missing.pop_front();
            static_cast<cITSObjectFolder *>(this)->RemoveObject(object);
        }
        return true;
    }
    case 0xa2: { // a behaviour's object changed: objects that use it (slot 16, or slot 15 when set) get slot 31(0)
        LocalRef changed;
        void *source = Call<32, void *>(standard.p);
        if (Call<0, uint8_t>(source, 0x7b269499u, changed.Reset())) {
            // The two loops ask the slot 16 values in opposite orders (0x4d26aa: the changed one first; 0x4d2739: the
            // object's behaviour first); slot 15 is asked the behaviour's first in both.
            auto sameSlot15 = [&changed](void *behavior) {
                if (Call<15>(behavior) == 0) return false;
                uint32_t mine = Call<15>(behavior);
                return mine == Call<15>(changed.p);
            };
            for (auto &entry : mObjects) {
                void *behavior = Call<18, void *>(entry.Get());
                uint32_t theirs = Call<16>(changed.p);
                if (Call<16>(behavior) == theirs || sameSlot15(behavior)) Call<31>(entry.Get(), 0u);
            }
            if (mGlobals) {
                for (size_t i = 0; i < mGlobals->size(); ++i) {
                    void *object = (*mGlobals)[i].Get();
                    void *behavior = Call<18, void *>(object);
                    uint32_t mine = Call<16>(behavior);
                    if (mine == Call<16>(changed.p) || sameSlot15(behavior)) Call<31>(object, 0u);
                }
            }
        }
        return true;
    }
    case 0xc4: { // a neighbourhood was removed: its global objects' resources dropped, its entries erased
        LocalRef text;
        if (Call<29, uint8_t>(standard.p, kStringIid, text.Reset())) {
            void *persist = Call<3, void *>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
            FolderName key(*static_cast<cIGZString *>(text.p));
            auto it = mGroupObjects.find(key);
            if (it != mGroupObjects.end()) {
                for (auto &entry : it->second) {
                    void *object = entry.Get();
                    if (!object) continue;
                    void *resource = Call<75, void *>(object);
                    uint8_t keyBuffer[16];
                    Call<10>(persist, Call<3>(resource, keyBuffer));
                }
                mGroupObjects.erase(it);
            }
            auto ids = mGroupIds.find(key);
            if (ids != mGroupIds.end()) mGroupIds.erase(ids);
        }
        return true;
    }
    case 0xc5: { // a neighbourhood was selected: its globals become current (loaded on first use)
        LocalRef text;
        if (!Call<29, uint8_t>(standard.p, kStringIid, &text.p)) return true;
        FolderName key(*static_cast<cIGZString *>(text.p));
        mGlobals = &mGroupObjects.find(key)->second;
        mGlobalIds = &mGroupIds.find(key)->second;
        if (mGlobalIds->size() == 0) SelectNeighborhoodFirstTime(key.s);
        StandardMessage notice;
        notice.Post(kMsgObjectsChanged);
        return true;
    }
    case 0x7c26430f: { // a framework object went away: objects that reference it lose their links
        LocalRef gone;
        void *source = Call<32, void *>(standard.p);
        if (Call<0, uint8_t>(source, 0xeb54e473u, gone.Reset())) {
            auto uses = [&gone](void *object) {
                void *behavior = Call<18, void *>(object);
                return Call<16, void *>(behavior) == gone.p || Call<15, void *>(behavior) == gone.p ||
                       Call<14, void *>(behavior) == gone.p;
            };
            for (auto &entry : mObjects) {
                if (!uses(entry.Get())) continue;
                Call<41>(entry.Get(), 0xffffffffu);
                Call<43>(entry.Get(), 0xffffffffu);
            }
            if (mGlobals)
                for (size_t i = 0; i < mGlobals->size(); ++i)
                    if (uses((*mGlobals)[i].Get())) LinkToContainer((*mGlobals)[i].Get());
        }
        return true;
    }
    case 0x8ecc1b6d:
        ++mMessageCount;
        return false;
    default:
        return false;
    }
}
