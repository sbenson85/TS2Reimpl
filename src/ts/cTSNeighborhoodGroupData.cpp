#include "ts/cTSNeighborhoodGroupData.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cITSGlobals.h"
#include "ifaces/cITSNeighborhood.h"
#include "ifaces/cITSNeighborhoodInfo.h"
#include "ifaces/cITSNeighborhoodManager.h"
#include "ifaces/cITSPersistSystem.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/ServiceRefVector.h"
#include "rz/ServiceGetters.h"

#include <new>

namespace {

template <typename R, typename... A> R Call(const void *object, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(const void *, A...)>(object, slot)(object, args...);
}

void AddRefObject(void *object) { As<cIGZUnknown>(object)->AddRef(); }
void ReleaseObject(void *object) { As<cIGZUnknown>(object)->Release(); }

void *PersistService()
{
    return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
}

// Slots of the neighbourhoods the group holds (their own interface; names unknown for most).
namespace nb {
constexpr int kKeyObject = 16;   // the object whose slot 6 gives the neighbourhood's id
constexpr int kCommit = 18;      // answers whether it went through
constexpr int kBeforeSave = 19;
constexpr int kAfterSave = 7;
constexpr int kSave = 8;
constexpr int kFinish = 20;
constexpr int kResourceKey = 16; // as kKeyObject: the object whose slot 3 writes the persist key
constexpr int kSetGroup = 14;
constexpr int kSetSource = 6;
} // namespace nb

uint8_t RefreshFromSource(cTSNeighborhoodGroupData *group, void *source) { return group->LoadNeighborhoods(source); }

constexpr uint32_t kNeighborhoodType = 0x4e474248; // 'NGBH', the neighbourhood resource
constexpr uint32_t kNeighborhoodInstance = 1;      // every neighbourhood of a group is instance 1 of its own group
constexpr uint32_t kNeighborhoodIid = 0x7aa51c46;  // what GetResource is asked for (cITSNeighborhood)

// A counted pointer field takes `object` (counted first, the old one released after), unless it holds it already.
void AssignCounted(void *&field, void *object)
{
    void *old = field;
    if (object == old) return;
    if (object) AddRefObject(object);
    field = object;
    if (old) ReleaseObject(old);
}

} // namespace

NeighborhoodGroupEntry::~NeighborhoodGroupEntry()
{
    if (mNeighborhood) ReleaseObject(mNeighborhood);
}

NeighborhoodGroupHeld::~NeighborhoodGroupHeld()
{
    if (mObject) ReleaseObject(mObject);
}

// ---- construction and destruction ----

cTSNeighborhoodGroupData::cTSNeighborhoodGroupData() : mById(), mByKey(), mCurrent(nullptr), mFirst(nullptr)
{
    new (&mOrdered) NeighborhoodGroupOrdered();
    mCursor = NeighborhoodGroupOrdered::iterator();
}

// Out of line, not defaulted: the compiler's destructor then stores both vtables first, as the original's (docs/standalone-shape.md,
// cTSFamilyHistory). The members left (the tables) are destroyed after the body, +0x2c before +0xc, then cRZUnknown's.
cTSNeighborhoodGroupData::~cTSNeighborhoodGroupData()
{
    mOrdered.~NeighborhoodGroupOrdered();
    if (mFirst) ReleaseObject(mFirst);
    if (mCurrent) ReleaseObject(mCurrent);
}

cTSNeighborhoodGroupData *cTSNeighborhoodGroupData::Create() { return new (std::nothrow) cTSNeighborhoodGroupData; }

bool cTSNeighborhoodGroupData::QueryInterface(uint32_t id, void **out)
{
    if (id != kInterfaceId) return rz::cRZUnknown::QueryInterface(id, out);
    *out = static_cast<cITSNeighborhoodGroupData *>(this);
    AddRef();
    return true;
}

uint32_t cTSNeighborhoodGroupData::AddRef() { return rz::cRZUnknown::AddRef(); }

uint32_t cTSNeighborhoodGroupData::Release() { return rz::cRZUnknown::Release(); }

// ---- the members ----

uint8_t cTSNeighborhoodGroupData::Add(void *source, void *neighborhood)
{
    void *old = mCurrent;
    if (neighborhood != old) {
        if (neighborhood) AddRefObject(neighborhood);
        mCurrent = neighborhood;
        if (old) ReleaseObject(old);
    }
    Call<void>(mCurrent, nb::kSetGroup, this);
    Call<void>(mCurrent, nb::kSetSource, source);
    RefreshFromSource(this, source);

    void *holder = nullptr; // the original's local counted copy of the neighbourhood
    if (neighborhood) {
        AddRefObject(neighborhood);
        holder = neighborhood;
    }
    void *sourceKeyObject = Call<void *>(source, 24);
    const uint32_t id = Call<uint32_t>(sourceKeyObject, 6);
    NeighborhoodGroupEntry &entry = mById[id];
    void *previous = entry.mNeighborhood;
    if (holder != previous) {
        if (holder) AddRefObject(holder);
        entry.mNeighborhood = holder;
        if (previous) ReleaseObject(previous);
    }
    entry.mDirty = 0;

    if (mFirst == nullptr) {
        void *current = mCurrent;
        if (current != mFirst) {
            if (current) AddRefObject(current);
            mFirst = current;
        }
    } else {
        Call<void>(mCurrent, 12, mFirst);
    }

    uint8_t all = 1;
    for (auto &item : mById)
        if (item.second.mNeighborhood != neighborhood) all &= Call<uint8_t>(item.second.mNeighborhood, nb::kCommit);
    if (holder) ReleaseObject(holder);
    return all;
}

uint8_t cTSNeighborhoodGroupData::Refresh(void *source)
{
    uint8_t all = RefreshFromSource(this, source);
    for (auto &item : mById) all &= Call<uint8_t>(item.second.mNeighborhood, nb::kCommit);
    Reset();
    return all;
}

// Kept out of line like the original (Add and Refresh call it), which also lets their tests replace it at its entry.
__declspec(noinline) uint8_t cTSNeighborhoodGroupData::LoadNeighborhoods(void *source)
{
    uint8_t initialized = 0;
    ifaces::cITSNeighborhoodManager *manager = As<ifaces::cITSGlobals>(GetCachedSingleton())->GetNeighborhoodManager();
    ifaces::cITSPersistSystem *persist = As<ifaces::cITSPersistSystem>(PersistService());
    if (!manager) return 0;
    // ORIGINAL-BUG: only the first table is emptied; the second (+0x2c) keeps pointers into the entries freed here until the
    // group rebuilds it (Refresh does, through slot 9; Add does not).
    mById.clear();

    // The main neighbourhood: a local entry (counted pointer, clean) the resource is fetched into.
    NeighborhoodGroupEntry main;
    char *prefix = const_cast<char *>(As<cIGZString>(As<ifaces::cITSNeighborhoodInfo>(source)->GetFilePrefix())->ToChar());
    ifaces::cITSNeighborhoodInfo *info = manager->GetByName(prefix); // never null for a loaded hood (used unchecked)
    uint32_t key[4] = {kNeighborhoodType, info->AsPersistResource()->GetGroup(), kNeighborhoodInstance, 0};
    if (void *old = main.mNeighborhood) { // the holder's "address of" drops what it held (nothing here)
        main.mNeighborhood = nullptr;
        ReleaseObject(old);
    }
    if (persist->GetResource(reinterpret_cast<Key16 *>(key), kNeighborhoodIid, &main.mNeighborhood, 0, 0)) {
        auto *hood = As<ifaces::cITSNeighborhood>(main.mNeighborhood);
        hood->SetGroupData(As<ifaces::cITSNeighborhoodGroupData>(this));
        initialized = hood->Initialize(info) & 1;
        if (info == source) AssignCounted(mCurrent, main.mNeighborhood);
        AssignCounted(mFirst, main.mNeighborhood);
        NeighborhoodGroupEntry &stored = mById[info->AsPersistResource()->GetGroup()];
        AssignCounted(stored.mNeighborhood, main.mNeighborhood);
        stored.mDirty = main.mDirty;

        // The sub-neighbourhoods, each in its own group (the instance stays 1), delegating to the main one.
        ServiceRefVector children{};
        manager->GetNamedList(prefix, &children);
        for (void **it = children.mFirst; it != children.mLast; ++it) {
            NeighborhoodGroupEntry child;
            auto *childInfo = As<ifaces::cITSNeighborhoodInfo>(*it);
            key[1] = childInfo->AsPersistResource()->GetGroup();
            if (void *old = child.mNeighborhood) {
                child.mNeighborhood = nullptr;
                ReleaseObject(old);
            }
            if (persist->GetResource(reinterpret_cast<Key16 *>(key), kNeighborhoodIid, &child.mNeighborhood, 0, 0)) {
                auto *childHood = As<ifaces::cITSNeighborhood>(child.mNeighborhood);
                childHood->SetGroupData(As<ifaces::cITSNeighborhoodGroupData>(this));
                initialized &= childHood->Initialize(childInfo);
                childHood->SetDelegate(As<ifaces::cITSNeighborhood>(main.mNeighborhood));
                NeighborhoodGroupEntry &childStored = mById[childInfo->AsPersistResource()->GetGroup()];
                AssignCounted(childStored.mNeighborhood, child.mNeighborhood);
                childStored.mDirty = child.mDirty;
                if (childInfo == source) AssignCounted(mCurrent, child.mNeighborhood);
            }
        }
        // The original frees the list with 0x1f1f70, a copy of this with an EH frame (same work).
        children.ReleaseAndFree();
    }
    return initialized;
}

uint8_t cTSNeighborhoodGroupData::SaveAll()
{
    uint8_t ok = 1;
    void *persist = PersistService();
    for (auto &item : mById) {
        NeighborhoodGroupEntry &entry = item.second;
        void *neighborhood = entry.mNeighborhood;
        if (!entry.mDirty && neighborhood != mCurrent && neighborhood != mFirst) continue;
        Call<void>(neighborhood, nb::kBeforeSave);
        void *keyObject = Call<void *>(neighborhood, nb::kResourceKey);
        void *records = Call<void *>(persist, 3);
        // The record table's slot is read before the key is fetched.
        auto store = VirtualSlot<uint8_t(__thiscall *)(void *, void *)>(records, 10);
        uint32_t key[4];
        void *keyPointer = Call<void *>(keyObject, 3, key);
        ok &= store(records, keyPointer);
        ok &= Call<uint8_t>(neighborhood, nb::kAfterSave);
    }
    mById.clear();
    if (mCurrent) {
        void *current = mCurrent;
        mCurrent = nullptr;
        ReleaseObject(current);
    }
    return ok;
}

uint8_t cTSNeighborhoodGroupData::Flush()
{
    uint8_t ok = 1;
    PersistService();
    for (auto &item : mById) {
        NeighborhoodGroupEntry &entry = item.second;
        void *neighborhood = entry.mNeighborhood;
        if (!entry.mDirty && neighborhood != mCurrent && neighborhood != mFirst) continue;
        ok &= Call<uint8_t>(neighborhood, nb::kSave);
        ok &= Call<uint8_t>(neighborhood, nb::kFinish);
        entry.mDirty = 0;
    }
    return ok;
}

void cTSNeighborhoodGroupData::Reset()
{
    Rebuild(-1);
}

void cTSNeighborhoodGroupData::Rebuild(int32_t skipId)
{
    mOrdered.clear();
    mByKey.clear();
    for (auto &item : mById) {
        void *member = Call<void *>(item.second.mNeighborhood, 54);
        while (member) {
            // ORIGINAL-BUG: a member whose slot 18 equals skipId is never stepped past; the loop asks it again, for ever.
            if (skipId == Call<int32_t>(member, 18)) continue;
            const int32_t id = Call<int32_t>(member, 18);
            NeighborhoodGroupHeld &held = mOrdered[id];
            if (held.mObject != member) {
                AddRefObject(member);
                void *old = held.mObject;
                held.mObject = member;
                if (old) ReleaseObject(old);
            }
            const uint32_t key = uint32_t(Call<int32_t>(member, 18));
            mByKey[key] = &item.second;
            member = Call<void *>(item.second.mNeighborhood, 55);
        }
    }
}

void cTSNeighborhoodGroupData::TrackCurrent(int32_t id)
{
    if (!mCurrent) return;
    void *object = Call<void *>(mCurrent, 57, id);
    NeighborhoodGroupHeld &held = mOrdered[id];
    if (held.mObject != object) {
        if (object) AddRefObject(object);
        void *old = held.mObject;
        held.mObject = object;
        if (old) ReleaseObject(old);
    }
    void *keyObject = Call<void *>(mCurrent, nb::kKeyObject);
    const uint32_t key = Call<uint32_t>(keyObject, 6);
    auto it = mById.find(key);
    if (it != mById.end()) mByKey[uint32_t(id)] = &it->second;
}

void cTSNeighborhoodGroupData::Remove(uint32_t id)
{
    auto it = mById.find(id);
    if (it == mById.end()) return;
    mById.erase(it);
    Reset();
}

void cTSNeighborhoodGroupData::MarkDirtyById(uint32_t id)
{
    for (auto &item : mById) {
        void *keyObject = Call<void *>(item.second.mNeighborhood, nb::kKeyObject);
        if (Call<uint32_t>(keyObject, 6) == id) {
            item.second.mDirty = 1;
            return;
        }
    }
}

void cTSNeighborhoodGroupData::MarkDirtyByKey(int32_t key)
{
    auto it = mByKey.find(key);
    if (it != mByKey.end()) it->second->mDirty = 1;
}

void *cTSNeighborhoodGroupData::GetByKey(int32_t key)
{
    auto it = mByKey.find(key);
    return it != mByKey.end() ? it->second->mNeighborhood : nullptr;
}

void *cTSNeighborhoodGroupData::GetById(uint32_t id)
{
    auto it = mById.find(id);
    return it != mById.end() ? it->second.mNeighborhood : nullptr;
}

uint32_t cTSNeighborhoodGroupData::MarkAndQuery(int32_t key)
{
    auto it = mByKey.find(key);
    if (it == mByKey.end()) return 0;
    NeighborhoodGroupEntry *entry = it->second;
    void *neighborhood = entry->mNeighborhood;
    entry->mDirty = 1;
    return Call<uint32_t>(neighborhood, 37, key);
}

uint32_t cTSNeighborhoodGroupData::QueryByKey(int32_t key)
{
    auto it = mByKey.find(key);
    if (it != mByKey.end()) return Call<uint32_t>(it->second->mNeighborhood, 57, key);
    return 0;
}

uint32_t cTSNeighborhoodGroupData::BeginIteration()
{
    mCursor = mOrdered.begin();
    return Next();
}

uint32_t cTSNeighborhoodGroupData::Next()
{
    if (mCursor == mOrdered.end()) return 0;
    const uint32_t value = uint32_t(uintptr_t(mCursor->second.mObject));
    ++mCursor;
    return value;
}

uint32_t cTSNeighborhoodGroupData::NextKey(int32_t key)
{
    if (key == 0) return mOrdered.empty() ? 0 : uint32_t(mOrdered.begin()->first);
    auto it = mOrdered.find(key);
    if (it == mOrdered.end()) return 0;
    ++it;
    return it == mOrdered.end() ? 0 : uint32_t(it->first);
}

// ---- the RVA hooks' adapters (one statement each: matchcheck v7 ties them to the member in our vtable) ----

using Adapters = cTSNeighborhoodGroupDataAdapters;

// 0x4729f0: the deleting destructor (game callers: none but the +4 face's thunk 0x4728af).
Adapters::Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
// 0x4728af: the +4 face's slot 3 (sub ecx, 4; jmp 0x4729f0).
Adapters::Self *__fastcall Adapters::UnknownDeletingDestructor(rz::cRZUnknown *face, void *, uint32_t flags)
{
    Self *const self = &static_cast<Self &>(*face);
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t id, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(id, out);
}
bool __fastcall Adapters::UnknownQueryInterface(rz::cRZUnknown *face, void *, uint32_t id, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::QueryInterface(id, out);
}
uint8_t __fastcall Adapters::Add(Self *self, void *, void *source, void *neighborhood)
{
    [[msvc::forceinline_calls]] return self->Self::Add(source, neighborhood);
}
uint8_t __fastcall Adapters::Refresh(Self *self, void *, void *source)
{
    [[msvc::forceinline_calls]] return self->Self::Refresh(source);
}
uint8_t __fastcall Adapters::SaveAll(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::SaveAll();
}
uint8_t __fastcall Adapters::Flush(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Flush();
}
void __fastcall Adapters::Reset(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Reset();
}
void __fastcall Adapters::Rebuild(Self *self, void *, int32_t skipId)
{
    [[msvc::forceinline_calls]] return self->Self::Rebuild(skipId);
}
void __fastcall Adapters::TrackCurrent(Self *self, void *, int32_t id)
{
    [[msvc::forceinline_calls]] return self->Self::TrackCurrent(id);
}
void __fastcall Adapters::Remove(Self *self, void *, uint32_t id)
{
    [[msvc::forceinline_calls]] return self->Self::Remove(id);
}
void __fastcall Adapters::MarkDirtyById(Self *self, void *, uint32_t id)
{
    [[msvc::forceinline_calls]] return self->Self::MarkDirtyById(id);
}
void __fastcall Adapters::MarkDirtyByKey(Self *self, void *, int32_t key)
{
    [[msvc::forceinline_calls]] return self->Self::MarkDirtyByKey(key);
}
void *__fastcall Adapters::GetByKey(Self *self, void *, int32_t key)
{
    [[msvc::forceinline_calls]] return self->Self::GetByKey(key);
}
void *__fastcall Adapters::GetById(Self *self, void *, uint32_t id)
{
    [[msvc::forceinline_calls]] return self->Self::GetById(id);
}
uint32_t __fastcall Adapters::MarkAndQuery(Self *self, void *, int32_t key)
{
    [[msvc::forceinline_calls]] return self->Self::MarkAndQuery(key);
}
uint32_t __fastcall Adapters::QueryByKey(Self *self, void *, int32_t key)
{
    [[msvc::forceinline_calls]] return self->Self::QueryByKey(key);
}
uint32_t __fastcall Adapters::BeginIteration(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::BeginIteration();
}
uint32_t __fastcall Adapters::Next(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Next();
}
uint32_t __fastcall Adapters::NextKey(Self *self, void *, int32_t key)
{
    [[msvc::forceinline_calls]] return self->Self::NextKey(key);
}
uint8_t __fastcall Adapters::LoadNeighborhoods(Self *self, void *, void *source)
{
    [[msvc::forceinline_calls]] return self->Self::LoadNeighborhoods(source);
}
