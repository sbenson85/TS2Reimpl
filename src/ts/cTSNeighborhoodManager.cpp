#include "rz/RZMessageServer.h"
#include "ts/cTSNeighborhoodManager.h"
#include "app/SpecialFolders.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "app/AppServices2.h"
#include "rz/RZMemory.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"

#include <cstring>
#include <new>
#include <string.h>

namespace {
// Calls slot `slot` of `object` (__thiscall) with the given dword arguments.
template <typename R = uint32_t, typename... A> R Call(const void *object, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return VirtualSlot<Fn>(object, slot)(object, args...);
}

constexpr uint32_t kCStringVTable = 0xd9b470; // cRZString
constexpr int kAddRef = 1;
constexpr int kReleaseSlot = 2;

// Slots of the neighbourhood objects the manager uses.
constexpr int kHoodHidden = 14;     // (slot 14, one argument) reports whether the neighbourhood is left out
constexpr int kHoodKindA = 18;      // slot 18: compared with the first filter value
constexpr int kHoodKindB = 22;      // slot 22: compared with the second filter value
constexpr int kHoodShutdown = 26;   // slot 26: called when the manager lets go of the neighbourhoods
constexpr int kHoodGroup = 4;       // slot 4: the object FindAccepting asks
constexpr int kGroupAccepts = 10;   // its slot 10 (value, 0)
constexpr int kHoodSetId = 11;      // slot 11: sets the id
constexpr int kHoodId = 12;         // slot 12: the id (-1 or 0 when it has none)
constexpr int kHoodName = 8;        // slot 8: the name (cIGZString)
constexpr int kHoodPersist = 24;    // slot 24: the persist resource face (slot 6 the group, slot 4 sets the key)
constexpr int kHoodSave = 27;       // slot 27: saves it
constexpr int kStringData = 6;      // cIGZString slots
constexpr int kStringLength = 7;
constexpr int kStringEquals = 9;    // (text, length, case insensitive)
constexpr int kStringSprintf = 31;  // __cdecl (this, format, ...)
constexpr int kStringText = 5;
constexpr int kMessageSetId = 5;    // cRZMessage2Standard slots
constexpr int kMessageValue0 = 11;
constexpr int kMessageObject = 30;
constexpr int kMainNeighborhood = 1; // slot 18 of a main neighbourhood (not a sub-neighbourhood)
constexpr uint32_t kMessageAdded = 0xc3;      // message ids posted by Add / Remove
constexpr uint32_t kMessageMainRemoved = 0xc4;
constexpr uint32_t kMessageRemoved = 0xc6;
constexpr int kResourceGroup = 6;   // persist resource faces: slot 6 the group, slot 4 sets the key
constexpr int kResourceSetKey = 4;
constexpr int kPersistSave = 16;      // persist system slots Save uses: 16 saves a resource (face, 0)
constexpr int kPersistCollect = 48;   // (group, list) adds the group's records to the list
constexpr int kPersistCloseList = 49; // (list)
constexpr int kPersistCommit = 50;    // (list, 1)
constexpr int kFrameworkCreate = 3;   // framework: (class, interface, out)
constexpr uint32_t kSaveListenerClass = 0xcdcbe0e3; // cTSSaveGameInfo (src/ts/cTSSaveGameInfo.h)
constexpr uint32_t kSaveListenerInterface = 0xcdcbe0dd; // cITSSaveGameInfo
constexpr int kListenerBegin = 3;   // cITSSaveGameInfo::Fill (no arguments), then
constexpr int kListenerSaved = 10;  // cITSSaveGameInfo::Save(group, instance 1) (returns bool, ignored here)
constexpr uint32_t kHoodRecordType = 0xac8a7a2e; // resource type of a neighbourhood's record
constexpr uint32_t kNameFormat = 0xdcbc6c;    // "N%03d"
constexpr uint32_t kTutorialName = 0xdcbc44;  // the tutorial neighbourhood's name
constexpr uint32_t kOptionQuery = 0x4c4324c5;     // the common implementation query Open makes
constexpr uint32_t kPersistInterface = 0x656b8f0c; // what Open asks the persist system for (slot 21)

// Tree successor of an MSVC 14.40 map node (_Tree_unchecked_const_iterator::operator++).
NeighborhoodTreeNode *NextNode(NeighborhoodTreeNode *node)
{
    if (!node->mRight->mIsNil) {
        node = node->mRight;
        while (!node->mLeft->mIsNil) node = node->mLeft;
        return node;
    }
    NeighborhoodTreeNode *parent;
    while (!(parent = node->mParent)->mIsNil && node == parent->mRight) node = parent;
    return parent;
}

void AddRefObject(void *object) { Call(object, kAddRef); }
void ReleaseObject(void *object) { Call(object, kReleaseSlot); }

// The destructor of the lists (0x1e6a40, an original callee).
void DestroyList(ServiceRefVector *list) { OriginalFunction<void(__thiscall *)(void *), 0x1e6a40>()(list); }

// vector assign (0x41d6e0, ServiceRefVector::AssignCounted since lane FU): out = [first, first + count).
void AssignList(ServiceRefVector *out, void **first, uint32_t count)
{
    out->AssignCounted(first, count);
}

// push_back of a counted pointer, as the filters write it: held while stored.
void AppendCounted(ServiceRefVector *out, void *item)
{
    if (item) AddRefObject(item);
    out->PushBack(item);
    if (item) ReleaseObject(item);
}

// The record of `name`: the first entry not below it (case insensitive), when the name is not below that entry either.
HoodNameMap::iterator FindName(HoodNameMap &names, const NeighborhoodName &key)
{
    auto it = names.lower_bound(key);
    if (it == names.end() || _stricmp(key.mString.c_str(), it->first.mString.c_str()) < 0) return names.end();
    return it;
}

NeighborhoodNameEntry *FindNamed(HoodNameMap &names, const char *text)
{
    auto it = FindName(names, NeighborhoodName(text));
    return it == names.end() ? nullptr : &it->second;
}

// The name of a neighbourhood (its slot 8, a cIGZString) as a map key.
NeighborhoodName NameOf(void *hood)
{
    void *name = Call<void *>(hood, kHoodName);
    const uint32_t length = Call(name, kStringLength);
    const char *data = Call<const char *>(name, kStringData);
    return NeighborhoodName(data, length);
}

// Allocates and builds a framework message (cRZMessage2Standard, as 0x1c033d does) holding one reference, with the id set.
void *NewMessage(uint32_t id)
{
    auto *message = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
    if (message) {
        message->mVTable = GameAddress(cRZMessage2Standard::kVTable);
        message->mRefCount = 0;
        message->mFlags = 0;
        message->mId = 0;
        for (uint32_t &value : message->mValue) value = 0;
        message->mPtr20 = message->mPtr24 = message->mPtr28 = nullptr;
        AddRefObject(message);
    }
    Call(message, kMessageSetId, id);
    return message;
}

// Posts a message to the cached message server (0x9c5a89, an original callee) and lets go of it.
void PostAndRelease(void *message)
{
    void *server = GetCachedMessageServer();
    Call(server, 3, message);
    if (message) ReleaseObject(message);
}

// The neighbourhood path helpers (__cdecl (name, out path, 1, -1), original callees): 0xc0b362 takes the name's text, 0xc0b232
// and 0xc0b150 the name object.
using PathFromText = uint8_t(__cdecl *)(const char *, void *, uint32_t, uint32_t);
using PathFromName = uint8_t(__cdecl *)(void *, void *, uint32_t, uint32_t);

// The neighbourhood an id map node holds.
void *HoodAt(NeighborhoodTreeNode *node) { return *reinterpret_cast<void **>(reinterpret_cast<char *>(node) + 0x14); }

bool Matches(void *hood, int32_t a, int32_t b)
{
    return (a == 0 || Call<int32_t>(hood, kHoodKindA) == a) && (b == -1 || Call<int32_t>(hood, kHoodKindB) == b);
}
} // namespace

// ---- the name map ----

NeighborhoodName::NeighborhoodName(const char *text)
    : mVTable(GameAddress(kCStringVTable)), mString(text, std::strlen(text)), mRefCount(0)
{
}

NeighborhoodName::NeighborhoodName(const char *data, uint32_t length)
    : mVTable(GameAddress(kCStringVTable)), mString(data, length), mRefCount(0)
{
}

bool NeighborhoodNameLess::operator()(const NeighborhoodName &a, const NeighborhoodName &b) const
{
    return _stricmp(a.mString.c_str(), b.mString.c_str()) < 0;
}

NeighborhoodNameEntry::~NeighborhoodNameEntry()
{
    DestroyList(&mHoods);
    if (mObject) ReleaseObject(mObject);
}

// ---- construction ----

cTSNeighborhoodManager *cTSNeighborhoodManager::Create() { return new (std::nothrow) cTSNeighborhoodManager; }

// The members after the body: the name map, then the id map (the list is a plain struct: the original's list destructor here).
cTSNeighborhoodManager::~cTSNeighborhoodManager() { DestroyList(&mHoods); }

// ---- cIGZUnknown / cIGZSerializable ----

bool cTSNeighborhoodManager::QueryInterface(uint32_t id, void **out)
{
    switch (id) {
    case 0xcc8a6a62:
        *out = static_cast<cITSNeighborhoodManager *>(this);
        AddRef();
        return true;
    case 1:
    case 0xe4fda3d4:
        *out = static_cast<cIGZSerializable *>(this);
        AddRef();
        return true;
    case 0x456b8f1d:
        *out = static_cast<cIGZPersistResource *>(this);
        AddRef();
        return true;
    }
    return false;
}

uint32_t cTSNeighborhoodManager::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

uint32_t cTSNeighborhoodManager::GetGZCLSID() { return kClassId; }

bool cTSNeighborhoodManager::Write(ifaces::cIGZOStream *stream)
{
    Call<uint8_t>(stream, 9, uint32_t(2));
    Call<uint8_t>(stream, 9, kStreamVersion);
    return Call<uint8_t>(stream, 18) == 0;
}

bool cTSNeighborhoodManager::Read(ifaces::cIGZIStream *stream)
{
    // ORIGINAL-BUG: the locals are never initialised; on a failed read they keep what their stack slots held on entry,
    // the stream pointer and the saved ecx (the serial face). Reproduced.
    int32_t version = int32_t(uintptr_t(stream));
    uint32_t second = uint32_t(uintptr_t(static_cast<cIGZSerializable *>(this)));
    Call<uint8_t>(stream, 8, &version);
    if (version >= 2) {
        Call<uint8_t>(stream, 9, &second);
        if (second > kStreamVersion) return false;
    }
    return Call<uint8_t>(stream, 18) == 0;
}

// ---- walking and lookups ----

void *cTSNeighborhoodManager::First()
{
    mCursor = IdHead()->mLeft;
    return Next();
}

void *cTSNeighborhoodManager::Next()
{
    while (mCursor != IdHead()) {
        void *hood = *reinterpret_cast<void **>(reinterpret_cast<char *>(mCursor) + 0x14);
        const bool hidden = Call<uint8_t>(hood, kHoodHidden, uint32_t(1)) != 0;
        mCursor = NextNode(mCursor);
        if (!hidden) return hood;
    }
    return nullptr;
}

void *cTSNeighborhoodManager::GetById(uint32_t id)
{
    auto it = mHoodsById.find(id);
    return it == mHoodsById.end() ? nullptr : it->second.Get();
}

void *cTSNeighborhoodManager::GetByName(const char *name)
{
    NeighborhoodNameEntry *entry = FindNamed(mHoodsByName, name);
    return entry ? entry->mObject : nullptr;
}

void *cTSNeighborhoodManager::FindAccepting(uint32_t value)
{
    for (auto &pair : mHoodsById) {
        void *hood = pair.second.Get();
        void *group = Call<void *>(hood, kHoodGroup);
        if (Call<uint8_t>(group, kGroupAccepts, value, uint32_t(0)) != 0) return hood;
    }
    return nullptr;
}

bool cTSNeighborhoodManager::GetNamedList(const char *name, ServiceRefVector *out)
{
    NeighborhoodNameEntry *entry = FindNamed(mHoodsByName, name);
    if (!entry) return false;
    if (out != &entry->mHoods) AssignList(out, entry->mHoods.mFirst, entry->mHoods.Size());
    return out->mFirst != out->mLast;
}

int32_t cTSNeighborhoodManager::CountNamed(const char *name, int32_t a, int32_t b)
{
    int32_t count = 0;
    NeighborhoodNameEntry *entry = FindNamed(mHoodsByName, name);
    if (!entry) return 0;
    for (void **p = entry->mHoods.mFirst; p != entry->mHoods.mLast; ++p)
        if (Matches(*p, a, b)) ++count;
    return count;
}

bool cTSNeighborhoodManager::Filter(ServiceRefVector *out, int32_t a, int32_t b)
{
    if (a == 0 && b == -1) {
        if (out != &mHoods) AssignList(out, mHoods.mFirst, mHoods.Size());
    } else {
        for (void **p = mHoods.mFirst; p != mHoods.mLast; ++p)
            if (Matches(*p, a, b)) AppendCounted(out, *p);
    }
    return out->mFirst != out->mLast;
}

int32_t cTSNeighborhoodManager::FilterWith(ServiceRefVector *out, void *predicate)
{
    const int32_t before = int32_t(out->Size());
    if (!predicate) {
        if (out != &mHoods) AssignList(out, mHoods.mFirst, mHoods.Size());
    } else {
        for (void **p = mHoods.mFirst; p != mHoods.mLast; ++p) {
            void *hood = *p;
            if (Call<uint8_t>(predicate, 0, hood) != 0) AppendCounted(out, hood);
        }
    }
    return int32_t(out->Size()) - before;
}

void cTSNeighborhoodManager::Open(const uint32_t *key, uint32_t extra)
{
    // An empty cRZString the option query fills.
    NeighborhoodName option("");
    appinit::CommonImplSlot4(kOptionQuery, uint32_t(uintptr_t(&option)), 1, 0, 0xffffffffu);
    void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    void *holder = nullptr;
    using Open21 = uint32_t(__thiscall *)(void *, uint32_t, void **, uint32_t, const char *, uint32_t, uint32_t, uint32_t);
    VirtualCall<Open21, 21>(persist, kPersistInterface, &holder, 1, option.mString.c_str(), extra, 1, 1);
    SetKey(*reinterpret_cast<const cGZPersistResourceKey *>(key));
    Load();
    if (holder) ReleaseObject(holder);
}

void cTSNeighborhoodManager::Clear()
{
    for (NeighborhoodTreeNode *node = IdHead()->mLeft; node != IdHead(); node = NextNode(node))
        Call(*reinterpret_cast<void **>(reinterpret_cast<char *>(node) + 0x14), kHoodShutdown);
    mHoodsById.clear();
    mHoodsByName.clear();
    mNextId = 1;
    mCursor = IdHead();
    for (void **p = mHoods.mFirst; p != mHoods.mLast; ++p) Call(*p, kHoodShutdown);
    void **end = mHoods.mLast;
    if (mHoods.mFirst != end) {
        for (void **p = mHoods.mFirst; p != end; ++p)
            if (*p) ReleaseObject(*p);
        mHoods.mLast = mHoods.mFirst;
    }
}

// ---- saving, naming, adding and removing ----

uint8_t cTSNeighborhoodManager::Save()
{
    void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    ServiceRefVector records{};
    Call(persist, kPersistCollect, GetGroup(), &records);
    for (NeighborhoodTreeNode *node = IdHead()->mLeft; node != IdHead(); node = NextNode(node)) {
        void *hood = HoodAt(node);
        if (hood && !Call<uint8_t>(hood, kHoodHidden, 1u))
            Call(persist, kPersistCollect, Call(Call<void *>(hood, kHoodPersist), kResourceGroup), &records);
    }
    Call(persist, kPersistCloseList, &records);
    uint8_t ok = Call<uint8_t>(persist, kPersistSave, static_cast<cIGZPersistResource *>(this), 0u);

    void *holder = nullptr;
    void *framework = GetDirectorFrameworkObject();
    Call(framework, kFrameworkCreate, kSaveListenerClass, kSaveListenerInterface, &holder);
    if (holder) {
        Call(holder, kListenerBegin);
        const uint32_t group = GetGroup();
        Call(holder, kListenerSaved, group, 1u);
    }
    uint8_t result = ok;
    if (ok) {
        for (NeighborhoodTreeNode *node = IdHead()->mLeft; node != IdHead(); node = NextNode(node)) {
            void *hood = HoodAt(node);
            if (!hood || Call<uint8_t>(hood, kHoodHidden, 1u)) continue;
            const uint32_t group = Call(Call<void *>(hood, kHoodPersist), kResourceGroup);
            const uint32_t key[4] = {kHoodRecordType, group, 1, 0};
            Call(Call<void *>(hood, kHoodPersist), kResourceSetKey, key);
            ok &= Call<uint8_t>(hood, kHoodSave);
            if (holder) Call(holder, kListenerSaved, group, 1u);
        }
        result = ok & Call<uint8_t>(persist, kPersistCommit, &records, 1u);
    }
    if (holder) ReleaseObject(holder);
    records.ReleaseAndFree();
    return result;
}

void cTSNeighborhoodManager::NextFreeName(void *name)
{
    using Sprintf = void(__cdecl *)(void *, const char *, ...);
    const char *format = GameAddress<const char>(kNameFormat);
    int32_t number = 1;
    do {
        VirtualSlot<Sprintf>(name, kStringSprintf)(name, format, number);
        ++number;
    } while (FindNamed(mHoodsByName, Call<const char *>(name, kStringText)));

    NeighborhoodName path("");
    auto *pathString = reinterpret_cast<cRZString *>(&path);
    OriginalFunction<PathFromText, 0xc0b362>()(Call<const char *>(name, kStringText), &path, 1, 0xffffffffu);
    if (as2::DirectoryExists(pathString)) {
        do {
            VirtualSlot<Sprintf>(name, kStringSprintf)(name, format, number);
            ++number;
            OriginalFunction<PathFromText, 0xc0b362>()(Call<const char *>(name, kStringText), &path, 1, 0xffffffffu);
        } while (as2::DirectoryExists(pathString));
    }
}

bool cTSNeighborhoodManager::Add(void *hood)
{
    const auto named = FindName(mHoodsByName, NameOf(hood));
    if (named != mHoodsByName.end()) {
        const NeighborhoodNameEntry &entry = named->second;
        if (hood == entry.mObject) return false;
        for (void **p = entry.mHoods.mFirst; p != entry.mHoods.mLast; ++p)
            if (*p == hood) return false;
    }

    // A fresh id when it has none (the tutorial keeps 0) or when its id is taken.
    bool fresh = Call<int32_t>(hood, kHoodId) == -1;
    if (!fresh && Call(hood, kHoodId) == 0) {
        void *text = Call<void *>(hood, kHoodName);
        fresh = !Call<uint8_t>(text, kStringEquals, GameAddress<const char>(kTutorialName), 0xffffffffu, 1u);
    }
    if (fresh) Call(hood, kHoodSetId, mNextId++);
    while (mHoodsById.find(Call(hood, kHoodId)) != mHoodsById.end()) Call(hood, kHoodSetId, mNextId++);

    {
        const uint32_t id = Call(hood, kHoodId);
        AddRefObject(hood); // the original holds it while it inserts
        // The loop above leaves the id unmapped, so this always inserts; the original still checks.
        auto inserted = mHoodsById.try_emplace(id);
        if (inserted.second) inserted.first->second.Assign(hood);
        ReleaseObject(hood);
    }

    if (named == mHoodsByName.end()) {
        // The original builds the record on the stack (one reference), copies it into a pair (another) and the pair into the
        // map (the one that stays), then lets go of the two copies.
        AddRefObject(hood);
        NeighborhoodName key = NameOf(hood);
        AddRefObject(hood);
        NeighborhoodNameEntry &entry = mHoodsByName.try_emplace(key).first->second;
        AddRefObject(hood);
        entry.mObject = hood;
        ReleaseObject(hood);
        ReleaseObject(hood);
    } else {
        NeighborhoodNameEntry &entry = named->second;
        if (Call<int32_t>(hood, kHoodKindA) == kMainNeighborhood) {
            // A main neighbourhood takes the record's place; the one it replaces joins the list.
            entry.mHoods.PushBack(entry.mObject);
            void *old = entry.mObject;
            if (hood != old) {
                AddRefObject(hood);
                entry.mObject = hood;
                if (old) ReleaseObject(old);
            }
        } else {
            AddRefObject(hood);
            entry.mHoods.PushBack(hood);
            ReleaseObject(hood);
        }
    }

    CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); // fetched and never used
    NeighborhoodName savePath("");
    OriginalFunction<PathFromName, 0xc0b232>()(Call<void *>(hood, kHoodName), &savePath, 1, 0xffffffffu);
    // 0x1c5031, ours (lane LY); the name has cRZString's layout.
    folders::EnsureDirectory(reinterpret_cast<cRZString *>(&savePath));
    NeighborhoodName storePath("");
    OriginalFunction<PathFromName, 0xc0b150>()(Call<void *>(hood, kHoodName), &storePath, 1, 0xffffffffu);
    folders::EnsureDirectory(reinterpret_cast<cRZString *>(&storePath));

    void *message = NewMessage(kMessageAdded);
    void *text = ::operator new(sizeof(NeighborhoodName));
    if (text) text = new (text) NeighborhoodName(NameOf(hood));
    Call(message, kMessageObject, text);
    PostAndRelease(message);
    return 1;
}

bool cTSNeighborhoodManager::Remove(void *hood)
{
    if (!hood) return 0;
    const uint32_t id = Call(hood, kHoodId);
    const int32_t kind = Call<int32_t>(hood, kHoodKindA);
    void *message;
    if (kind == kMainNeighborhood) {
        message = NewMessage(kMessageMainRemoved);
        void *text = ::operator new(sizeof(NeighborhoodName));
        if (text) text = static_cast<cRZString *>(text)->Construct(*static_cast<const cIGZString *>(Call<void *>(hood, kHoodName)));
        Call(message, kMessageObject, text);
    } else {
        message = NewMessage(kMessageRemoved);
        Call(message, kMessageValue0, Call(hood, kHoodId));
    }
    PostAndRelease(message);

    DeleteFiles(hood);
    mHoodsById.erase(id);
    const auto named = FindName(mHoodsByName, NameOf(hood));
    if (named == mHoodsByName.end()) return 1;
    ServiceRefVector &list = named->second.mHoods;
    if (kind == kMainNeighborhood) {
        void **end = list.mLast;
        // ORIGINAL-BUG: a null entry in the list is asked for its id (slot 12 through a null pointer) and crashes.
        for (void **p = list.mFirst; p != end; ++p) mHoodsById.erase(Call(*p, kHoodId));
        mHoodsByName.erase(named);
        return 1;
    }
    void **end = list.mLast;
    void **at = list.mFirst;
    while (at != end && *at != hood) ++at;
    if (at == end) return 1;
    // vector::erase: each later entry moves down one (counted assignment), the last is released.
    for (void **next = at + 1; next != end; ++next) {
        void *moved = *next;
        void *old = next[-1];
        if (moved == old) continue;
        if (moved) AddRefObject(moved);
        next[-1] = moved;
        if (old) ReleaseObject(old);
    }
    if (void *last = list.mLast[-1]) ReleaseObject(last);
    --list.mLast;
    return 1;
}

// Slot 3 (Load, 0x4a4160) and HasPackage (0x4a6000) are in cTSNeighborhoodManagerLoad.cpp.

// ---- the RVA hooks' adapters (docs/standalone-shape.md, step 6) ----

using Adapters = cTSNeighborhoodManagerAdapters;

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t id, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(id, out);
}

uint32_t __fastcall Adapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}


bool __fastcall Adapters::HasPackage(Self *self, void *, void *hood)
{
    [[msvc::forceinline_calls]] return self->Self::HasPackage(hood);
}

void __fastcall Adapters::AddTutorial(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::AddTutorial();
}

void __fastcall Adapters::DeleteFiles(Self *self, void *, void *hood)
{
    [[msvc::forceinline_calls]] return self->Self::DeleteFiles(hood);
}



void *__fastcall Adapters::First(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::First();
}













// 0x4a1fb0: the scalar deleting destructor (the destructor is inline in the original too).



uint32_t __fastcall Adapters::GetGZCLSID(cIGZSerializable *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetGZCLSID();
}
