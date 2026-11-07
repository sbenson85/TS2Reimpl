#include "cas/cTSSkinCatalog.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/RZAutoRef.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/ServiceGetters.h"

#include <bit>
#include <ctime>
#include <new>

namespace nTSCASCore {

namespace {

// nTSCASCore::cTSSkinEntryFactory as Init builds it inline (16 bytes, not ours): the cIGZPersistResourceFactory vtable
// (a dead store), the cRZUnknown part at +4 (0x1b9052: vtable, zero count), then the class's two game vtables and a
// cleared byte at +0xc.
struct SkinEntryFactory {
    const void *mVTable;      // +0x0 0xe8a5f4
    RZUnknownLayout mUnknown; // +0x4 0xe8a614, count +8
    uint8_t mFlag;            // +0xc
    uint8_t mPad[3];
};
static_assert(sizeof(SkinEntryFactory) == 0x10, "SkinEntryFactory layout");
constexpr uint32_t kPersistResourceFactoryVTable = 0xda6718;
constexpr uint32_t kSkinEntryFactoryVTable = 0xe8a5f4;
constexpr uint32_t kSkinEntryFactoryVTable4 = 0xe8a614;

// The persist resource manager's slot 20 takes (a, b, factory); Init registers the factory under the first id with each
// of these second ids.
constexpr uint32_t kSkinEntryKind = 0x0c4c6ca4;
constexpr uint32_t kSkinEntryIds[] = {0x4c158081, 0x8c1580b5, 0x2c1fd8a1, 0x0c1fe246, 0xebcf3e27,
                                      0x6c93b566, 0x8c93bf6c, 0x8c93e35c, 0x0c93e3de, 0xd1954460};

// The resource type each catalog type (1..10) stands for, and the interface slot 7 asks the resource for before handing
// it to that type's insert helper (a non-slot member, still game code; `this` the catalog, the interface pointer or null).
struct TypeInfo {
    uint32_t resourceType;
    uint32_t iid;
    uint32_t insert;
};
constexpr uint32_t kDefaultGroup = 0x2c17b74a;
constexpr uint32_t kGroupIid = 0x3b2271bb; // what a source answers with its group (slot 4)

// ---- the image's code this class still calls ----

// 0xb74910 (this = the catalog): loads every source's resources once (mLoaded).
void EnsureLoaded(cTSSkinCatalog *self)
{
    OriginalFunction<void(__thiscall *)(cTSSkinCatalog *), 0xb74910>()(self);
}
// 0x4f8e50: the shared zero GUID (a function-local static copied from 0x10d9f44 on first use).
const SkinKey *ZeroGuid() { return OriginalFunction<const SkinKey *(__cdecl *)(), 0x4f8e50>()(); }

void *PersistManager() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); }

// ---- the slots of the objects the catalog holds ----

bool Query(void *object, uint32_t iid, void **out)
{
    return VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(object, iid, out);
}
void Drop(void *object) { VirtualCall<uint32_t(__thiscall *)(void *), 2>(object); }
// An entry's property object.
void *Props(void *entry) { return VirtualCall<void *(__thiscall *)(void *), 3>(entry); }
template <int Slot> const SkinKey *KeyAt(void *object)
{
    return VirtualCall<const SkinKey *(__thiscall *)(void *), Slot>(object);
}
template <int Slot> uint32_t WordAt(void *object) { return VirtualCall<uint32_t(__thiscall *)(void *), Slot>(object); }
template <int Slot> uint16_t HalfAt(void *object) { return VirtualCall<uint16_t(__thiscall *)(void *), Slot>(object); }
template <int Slot> uint8_t ByteAt(void *object) { return VirtualCall<uint8_t(__thiscall *)(void *), Slot>(object); }
// A predicate's slot 3: whether it takes `object`.
bool Takes(void *predicate, void *object)
{
    return VirtualCall<bool(__thiscall *)(void *, void *), 3>(predicate, object);
}

// 16 bytes compared whole (the original's dword loop).
bool SameKey(const SkinKey *a, const SkinKey *b)
{
    return a->type == b->type && a->group == b->group && a->instance == b->instance && a->resource == b->resource;
}

// ---- the filters' tests (each re-reads the entry's property object, as the original does) ----

// props slot 19 against the age byte, slot 20 against the gender byte: both must share a bit.
bool AgesAndGenders(void *entry, uint8_t ages, uint8_t genders)
{
    if ((ages & WordAt<19>(Props(entry))) == 0) return false;
    return (genders & ByteAt<20>(Props(entry))) != 0;
}
// props slot 21: no flags asked, or none set, or a shared bit (the test of zero is on the whole word, the match on its byte).
bool FlagsMatch(void *entry, uint8_t flags)
{
    if (flags == 0) return true;
    if (WordAt<21>(Props(entry)) == 0) return true;
    return (flags & ByteAt<21>(Props(entry))) != 0;
}
// props slot 22 (a 16-bit mask asked) and slot 25 (a 32-bit one), the same way.
bool Mask22Matches(void *entry, uint16_t mask)
{
    if (mask == 0) return true;
    if (WordAt<22>(Props(entry)) == 0) return true;
    return (mask & WordAt<22>(Props(entry))) != 0;
}
bool Mask25Matches(void *entry, uint32_t mask)
{
    if (mask == 0) return true;
    if (WordAt<25>(Props(entry)) == 0) return true;
    return (mask & WordAt<25>(Props(entry))) != 0;
}
// An entry's 16-bit mask at Slot: none asked, none set, or a shared bit.
template <int Slot> bool EntryMaskMatches(void *entry, uint16_t mask)
{
    if (mask == 0) return true;
    if (HalfAt<Slot>(entry) == 0) return true;
    return (mask & HalfAt<Slot>(entry)) != 0;
}
// An entry's GUID at Slot: none asked, the entry's is the zero GUID, or the same.
template <int Slot> bool GuidMatches(void *entry, const SkinKey *want)
{
    if (!want) return true;
    const SkinKey *zero = ZeroGuid();
    if (SameKey(KeyAt<Slot>(entry), zero)) return true;
    return SameKey(want, KeyAt<Slot>(entry));
}
// An entry's id at Slot: none asked, or the same.
template <int Slot> bool IdMatches(void *entry, const SkinKey *want) { return !want || SameKey(want, KeyAt<Slot>(entry)); }
// The property object's key (slot 16): none asked, or the same.
bool PropsKeyMatches(void *entry, const SkinKey *want) { return !want || SameKey(want, KeyAt<16>(Props(entry))); }

// The "Matching" slots: every entry of `map` the predicate takes (handed its property object), up to `max` (the count is
// tested before each entry; -1 takes all). Nothing when the map is empty.
void TakeMatching(SkinEntryMap &map, SkinPtrVector *out, void *predicate, int32_t max)
{
    if (map.size() == 0) return;
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (max == 0) return;
        void *entry = it->second.Get();
        if (Takes(predicate, Props(entry))) {
            out->push_back(entry);
            --max;
        }
    }
}

// The filters on slots 29..35's maps: ages, genders, flags, the props' 16-bit mask, and an id at the entry's IdSlot.
template <int IdSlot>
void FindWithId(cTSSkinCatalog *self, SkinEntryMap &map, SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags,
                uint16_t mask22, const SkinKey *id)
{
    out->clear();
    EnsureLoaded(self);
    if (map.size() == 0) return;
    for (auto &[key, ref] : map) {
        void *entry = ref.Get();
        if (!AgesAndGenders(entry, ages, genders) || !FlagsMatch(entry, flags) || !Mask22Matches(entry, mask22)) continue;
        if (!IdMatches<IdSlot>(entry, id)) continue;
        out->push_back(entry);
    }
}

// Slots 14 and 17: ages, genders, flags, the props' 32-bit mask (slot 25) and the props' key.
void FindWithPropsKey(cTSSkinCatalog *self, SkinEntryMap &map, SkinPtrVector *out, uint8_t ages, uint8_t genders,
                      uint8_t flags, uint32_t mask25, const SkinKey *key)
{
    out->clear();
    EnsureLoaded(self);
    if (map.size() == 0) return;
    for (auto &[k, ref] : map) {
        void *entry = ref.Get();
        if (!AgesAndGenders(entry, ages, genders) || !FlagsMatch(entry, flags) || !Mask25Matches(entry, mask25)) continue;
        if (!PropsKeyMatches(entry, key)) continue;
        out->push_back(entry);
    }
}

// Slots 16 and 19: whether an entry of `map` has the key (its slot 4); null entries are skipped.
bool HasKey(cTSSkinCatalog *self, SkinEntryMap &map, const SkinKey *key)
{
    EnsureLoaded(self);
    for (auto &[k, ref] : map) {
        void *entry = ref.Get();
        if (entry && SameKey(KeyAt<4>(entry), key)) return true;
    }
    return false;
}

// The overlay maps' filters (slots 20, 22, 23, 25): ages, genders, flags, two GUIDs (GuidA / GuidB slots, zero GUID
// matches anything), the id (slot 5), the props' 16-bit mask when Mask22, and the entry's mask (MaskSlot).
template <int GuidA, int GuidB, int MaskSlot>
bool OverlayMatches(void *entry, uint8_t ages, uint8_t genders, uint8_t flags, const SkinKey *guidA, const SkinKey *guidB,
                    const SkinKey *id, bool withMask22, uint16_t mask22, uint16_t mask)
{
    if (!AgesAndGenders(entry, ages, genders) || !FlagsMatch(entry, flags)) return false;
    if (!GuidMatches<GuidA>(entry, guidA) || !GuidMatches<GuidB>(entry, guidB) || !IdMatches<5>(entry, id)) return false;
    if (withMask22 && !Mask22Matches(entry, mask22)) return false;
    return EntryMaskMatches<MaskSlot>(entry, mask);
}

} // namespace

// ---- construction ----

// 0xb72310: the service (id 0x4bcf1bb0), no source current, empty maps, the random seeded from clock(), then ClearAll
// (called directly by the original, as a constructor's virtual call is).
cTSSkinCatalog::cTSSkinCatalog()
    : rz::cRZSystemService(kServiceId, kPriority), mLoaded(false), mNextInstance(0), mCurrent(-1)
{
    mRandom.Construct(ClockSeedAt(0xb72623)); // clock(), as the original (seed control keys it by that site)
    ClearAll();
}

// 0xb72b40: ClearAll, then the members in reverse order (the resources vector, the two id maps, the type maps, the sources).
cTSSkinCatalog::~cTSSkinCatalog() { ClearAll(); }

cTSSkinCatalog *__cdecl cTSSkinCatalog::Create() { return new cTSSkinCatalog; }

cTSSkinCatalog *__fastcall cTSSkinCatalogAdapters::Construct(void *memory, void *) { return ::new (memory) cTSSkinCatalog; }

// ---- cIGZUnknown / cIGZSystemService ----

// 0xb7bc70: cITSSkinCatalog -> the +0x1c face (counted through the primary face's AddRef), else the service's.
bool cTSSkinCatalog::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cITSSkinCatalog *>(this);
    AddRef();
    return true;
}

uint32_t cTSSkinCatalog::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSSkinCatalog::Release() { return rz::cRZSystemService::Release(); }

// 0xb7aaf0: registers the skin entry factory with the persist resource manager for the ten resource types.
bool cTSSkinCatalog::Init()
{
    // The base's Init (shared 0x144910) answers true without side effects: the original's early return is dead.
    void *service = PersistManager();
    void *manager = VirtualCall<void *(__thiscall *)(void *), 3>(service);
    auto *factory = static_cast<SkinEntryFactory *>(::operator new(sizeof(SkinEntryFactory)));
    factory->mVTable = GameAddress<void>(kPersistResourceFactoryVTable);
    factory->mUnknown.Construct();
    factory->mVTable = GameAddress<void>(kSkinEntryFactoryVTable);
    factory->mUnknown.mVTable = GameAddress<void>(kSkinEntryFactoryVTable4);
    factory->mFlag = 0;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(factory); // AddRef
    for (uint32_t id : kSkinEntryIds) {
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void *), 20>(manager, kSkinEntryKind, id, factory);
    }
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(factory); // Release
    return true;
}

// folded 0xb82190: ClearAll through the interface's vtable, then the base's Shutdown (true).
bool cTSSkinCatalog::Shutdown()
{
    ClearAll();
    return rz::cRZSystemService::Shutdown();
}

// ---- cITSSkinCatalog: counts and lookups ----

SkinEntryMap *cTSSkinCatalog::MapOfType(uint32_t type)
{
    switch (type) {
    case 1: return &mType1;
    case 2: return &mType2;
    case 3: return &mType3;
    case 4: return &mType4;
    case 5: return &mType5;
    case 6: return &mType6;
    case 7: return &mType7;
    case 8: return &mType8;
    case 9: return &mType9;
    case 10: return &mType10;
    default: return nullptr;
    }
}

// 0xb7a620: the size of type `type`'s map; 0 outside 1..10.
uint32_t cTSSkinCatalog::CountOfType(uint32_t type)
{
    switch (type) {
    case 1: return static_cast<uint32_t>(mType1.size());
    case 2: return static_cast<uint32_t>(mType2.size());
    case 3: return static_cast<uint32_t>(mType3.size());
    case 4: return static_cast<uint32_t>(mType4.size());
    case 5: return static_cast<uint32_t>(mType5.size());
    case 6: return static_cast<uint32_t>(mType6.size());
    case 7: return static_cast<uint32_t>(mType7.size());
    case 8: return static_cast<uint32_t>(mType8.size());
    case 9: return static_cast<uint32_t>(mType9.size());
    case 10: return static_cast<uint32_t>(mType10.size());
    default: return 0;
    }
}

// 0xb7a6a0: slot 3 (through the vtable) for 0..10, summed.
uint32_t cTSSkinCatalog::TotalCount()
{
    uint32_t total = 0;
    for (int32_t type = 0; type < 11; ++type) total += CountOfType(static_cast<uint32_t>(type));
    return total;
}

// 0xb7a7c0 / 0xb7a5a0: the id the map holds for `id`, 0 when it has none.
uint32_t cTSSkinCatalog::MappedId1(uint32_t id)
{
    EnsureLoaded(this);
    auto it = mIds1.find(id);
    return it != mIds1.end() ? it->second : 0;
}

uint32_t cTSSkinCatalog::MappedId2(uint32_t id)
{
    EnsureLoaded(this);
    auto it = mIds2.find(id);
    return it != mIds2.end() ? it->second : 0;
}

// ---- adding and removing resources ----

// 0xb731d0: files `resource` in the catalog. Its key gets a group (0 -> 0x2c17b74a; -1 -> the source's (its
// 0x3b2271bb interface's slot 4), or 0x2c17b74a without a source), the resource type of its catalog type (slot 12; any
// other type keeps the key's, and fails when that is 0) and, when it has none, the next instance number the persist
// manager does not know (slot 3's slot 15). The resource goes to `source`, else the current source, else the source the
// manager names (slot 3's slot 34), else the first; the manager registers it there (slot 16, which may refuse). A resource
// whose slot 15 answers -1 gets a value from the framework's service 0x0dc7057a (slot 13; its slot 27). Last, the type's
// insert helper is handed the resource's interface of that type.
bool cTSSkinCatalog::AddResource(void *resource, void *source)
{
    static constexpr TypeInfo kTypes[] = {
        {0xebcf3e27, 0xebcf3e20, 0xb742b0}, {0x4c158081, 0x4c15807c, 0xb74690}, {0x8c1580b5, 0x8c1580ae, 0xb73ba0},
        {0x2c1fd8a1, 0x2c1fd89d, 0xb747c0}, {0x0c1fe246, 0x0c1fe23e, 0xb73cd0}, {0x6c93b566, 0x6c93b561, 0xb73e00},
        {0x8c93bf6c, 0x6c93bf64, 0xb73a70}, {0x8c93e35c, 0x8c93e358, 0xb736e0}, {0x0c93e3de, 0x0c93e3da, 0xb73940},
        {0xd1954460, 0xd1954459, 0xb73810},
    };
    EnsureLoaded(this);
    void *persist = PersistManager();
    SkinKey key;
    VirtualCall<void(__thiscall *)(void *, SkinKey *), 3>(resource, &key);
    if (key.group == 0) {
        key.group = kDefaultGroup;
    } else if (key.group == 0xffffffffu) {
        if (!source) {
            key.group = kDefaultGroup;
        } else {
            void *grouped = nullptr;
            if (Query(source, kGroupIid, &grouped)) key.group = WordAt<4>(grouped);
            if (grouped) Drop(grouped);
        }
    }
    const uint32_t type = WordAt<12>(resource);
    if (type - 1 < kTypeCount) {
        key.type = kTypes[type - 1].resourceType;
    } else if (key.type == 0) {
        return false;
    }
    if (key.instance == 0) {
        do {
            key.instance = ++mNextInstance;
        } while (VirtualCall<bool(__thiscall *)(void *, const SkinKey *), 15>(
            VirtualCall<void *(__thiscall *)(void *), 3>(persist), &key));
    }
    void *target = nullptr;
    if (source) {
        target = source;
    } else if (mCurrent >= 0) {
        target = mSources.data()[mCurrent].Get();
    } else {
        VirtualCall<void(__thiscall *)(void *, const SkinKey *, void **), 34>(
            VirtualCall<void *(__thiscall *)(void *), 3>(persist), &key, &target);
    }
    if (!target) {
        if (mSources.empty()) return false;
        target = mSources.front().Get();
    }
    if (VirtualCall<bool(__thiscall *)(void *, const SkinKey *), 20>(target, &key)) {
        VirtualCall<void(__thiscall *)(void *, const SkinKey *), 17>(persist, &key);
    }
    VirtualCall<void(__thiscall *)(void *, const SkinKey *), 4>(resource, &key);
    if (!VirtualCall<bool(__thiscall *)(void *, void *, void *), 16>(persist, resource, target)) return false;
    VirtualCall<void(__thiscall *)(void *), 10>(target);
    if (HalfAt<15>(resource) == 0xffff) {
        RZAutoRef values{nullptr};
        if (void *framework = GetServiceObject()) {
            VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(framework, 0x0dc7057a, 0xcdb467ae,
                                                                                   &values.mObject);
        }
        // The out-parameter is the variable that held the persist manager: left as it was when slot 13 does not write it.
        void *value = persist;
        if (VirtualCall<bool(__thiscall *)(void *, const SkinKey *, void **), 13>(values.mObject, &key, &value)) {
            VirtualCall<void(__thiscall *)(void *, void *), 27>(resource, value);
        }
        values.ReleaseObject();
    }
    const uint32_t kind = WordAt<12>(resource);
    if (kind - 1 < kTypeCount) {
        const TypeInfo &info = kTypes[kind - 1];
        void *typed = nullptr;
        Query(resource, info.iid, &typed);
        using Insert = void(__thiscall *)(cTSSkinCatalog *, void *);
        switch (kind) {
        case 1: OriginalFunction<Insert, 0xb742b0>()(this, typed); break;
        case 2: OriginalFunction<Insert, 0xb74690>()(this, typed); break;
        case 3: OriginalFunction<Insert, 0xb73ba0>()(this, typed); break;
        case 4: OriginalFunction<Insert, 0xb747c0>()(this, typed); break;
        case 5: OriginalFunction<Insert, 0xb73cd0>()(this, typed); break;
        case 6: OriginalFunction<Insert, 0xb73e00>()(this, typed); break;
        case 7: OriginalFunction<Insert, 0xb73a70>()(this, typed); break;
        case 8: OriginalFunction<Insert, 0xb736e0>()(this, typed); break;
        case 9: OriginalFunction<Insert, 0xb73940>()(this, typed); break;
        default: OriginalFunction<Insert, 0xb73810>()(this, typed); break;
        }
        if (typed) Drop(typed);
    }
    return true;
}

// 0xb76bb0: takes `resource` out (held counted meanwhile): with a group and an instance, the persist manager drops its key
// (slot 17; its answer is the result) after the source holding it was looked up (slot 3's slot 34) and handed to 0xb8feb0;
// on exactly 1 the source is told (slot 10) and the key leaves its type's map (type 1: from every group too).
bool cTSSkinCatalog::RemoveResource(void *resource)
{
    EnsureLoaded(this);
    SkinRef held(static_cast<cIGZUnknown *>(resource));
    SkinKey key;
    VirtualCall<void(__thiscall *)(void *, SkinKey *), 3>(resource, &key);
    uint8_t result = 0;
    if (key.instance != 0 && key.group != 0) {
        void *persist = PersistManager();
        void *holder = nullptr;
        VirtualCall<void(__thiscall *)(void *, const SkinKey *, void **), 34>(
            VirtualCall<void *(__thiscall *)(void *), 3>(persist), &key, &holder);
        result = VirtualCall<uint8_t(__thiscall *)(void *, const SkinKey *), 17>(persist, &key);
        OriginalFunction<uint8_t(__cdecl *)(void *, const SkinKey *), 0xb8feb0>()(holder, &key);
        if (result == 1) {
            if (holder) VirtualCall<void(__thiscall *)(void *), 10>(holder);
            const uint32_t type = WordAt<12>(resource);
            if (SkinEntryMap *map = MapOfType(type)) {
                auto it = map->find(key);
                if (it != map->end()) {
                    map->erase(it);
                    if (type == 1) {
                        for (auto &[bit, group] : mType1Groups) {
                            auto in = group.find(key);
                            if (in != group.end()) group.erase(in);
                        }
                    }
                }
            }
        }
    }
    return std::bit_cast<bool>(result);
}

// 0xb81530: every map emptied (in this order), the sources and resources released and emptied, not loaded. The id maps
// stay.
void cTSSkinCatalog::ClearAll()
{
    mType2.clear();
    mType3.clear();
    mType1.clear();
    mType1Groups.clear();
    mType5.clear();
    mType4.clear();
    mType6.clear();
    mType7.clear();
    mType8.clear();
    mType9.clear();
    mType10.clear();
    mSources.clear();
    mResources.clear();
    mLoaded = false;
}

// 0xb7aff0: not loaded yet: load, and the count. Loaded: the resources and maps emptied (ClearAll's order), the count
// reset, every source (last to first) loaded again (0xb7b2d0); one that does not load and whose 0x3b2271bb interface's slot
// 6 answers 1 is dropped (its place nulled, then removed with the other null places). The count the loads left.
uint32_t cTSSkinCatalog::Reload()
{
    if (!mLoaded) {
        EnsureLoaded(this);
        return mLoadedCount;
    }
    mResources.clear();
    mType2.clear();
    mType3.clear();
    mType1.clear();
    mType1Groups.clear();
    mType5.clear();
    mType4.clear();
    mType6.clear();
    mType7.clear();
    mType8.clear();
    mType9.clear();
    mType10.clear();
    mLoadedCount = 0;
    // (The load may grow the vector: the original keeps its element pointer and re-reads only the begin.)
    SkinRef *at = mSources.data() + mSources.size();
    while (at != mSources.data()) {
        --at;
        void *source = at->Get();
        if (!OriginalFunction<bool(__thiscall *)(cTSSkinCatalog *, void *), 0xb7b2d0>()(this, source)) {
            void *grouped = nullptr;
            if (Query(source, kGroupIid, &grouped) && WordAt<6>(grouped) == 1 && at->Get()) at->Assign(nullptr);
            if (grouped) Drop(grouped);
        }
    }
    for (auto it = mSources.begin(); it != mSources.end();) {
        if (!it->Get()) {
            it = mSources.erase(it);
        } else {
            ++it;
        }
    }
    return mLoadedCount;
}

// ---- type 1 (property sets) ----

// 0xb7d700: the type-1 entries passing the filters, taken from the group map of each bit of `groups` in turn (its low 16
// bits; none: the whole type-1 map). Filters: ages / genders, the entry's GUIDs at slots 5 and 6, flags, the props' 16-bit
// mask against the bits not yet visited, the entry's mask (slot 7) and the props' key.
void cTSSkinCatalog::FindType1(SkinPtrVector *out, uint8_t ages, uint8_t genders, const SkinKey *guid5,
                               const SkinKey *guid6, uint8_t flags, uint32_t groups, uint16_t mask7, const SkinKey *key)
{
    out->clear();
    const uint16_t asked = static_cast<uint16_t>(groups);
    EnsureLoaded(this);
    if (mType1.size() == 0) return;
    uint32_t remaining = groups;
    uint32_t low = (0u - remaining) & remaining;
    for (;;) {
        const uint32_t bit = low & 0xffff;
        SkinEntryMap *map;
        if (static_cast<uint16_t>(low) == 0) {
            if (asked != 0) return;
            map = &mType1;
        } else if (asked == 0) {
            map = &mType1;
        } else {
            auto it = mType1Groups.find(bit);
            map = it != mType1Groups.end() ? &it->second : nullptr;
        }
        if (map) {
            for (auto &[k, ref] : *map) {
                void *entry = ref.Get();
                if (!AgesAndGenders(entry, ages, genders)) continue;
                if (!GuidMatches<5>(entry, guid5) || !GuidMatches<6>(entry, guid6) || !FlagsMatch(entry, flags)) continue;
                if (!Mask22Matches(entry, static_cast<uint16_t>(remaining))) continue;
                if (!EntryMaskMatches<7>(entry, mask7) || !PropsKeyMatches(entry, key)) continue;
                out->push_back(entry);
            }
        }
        if (asked == 0) return;
        remaining &= ~bit;
        low = (0u - (remaining & 0xffff)) & remaining & 0xffff;
    }
}

// 0xb7da90: the type-1 entries the predicate takes (handed the entry), from the group map of each bit of its slot 4
// (masked to 14 bits; none: the whole type-1 map), up to `max`.
void cTSSkinCatalog::FindType1Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max)
{
    if (!append) out->clear();
    EnsureLoaded(this);
    if (mType1.size() == 0) return;
    const uint32_t asked = WordAt<4>(predicate) & 0x3fff;
    uint32_t remaining = asked;
    uint32_t low = (0u - asked) & asked;
    for (;;) {
        if (static_cast<uint16_t>(low) == 0 && static_cast<uint16_t>(asked) != 0) return;
        if (max == 0) return;
        SkinEntryMap *map;
        if (static_cast<uint16_t>(asked) == 0) {
            map = &mType1;
        } else {
            auto it = mType1Groups.find(low);
            // ORIGINAL-BUG: a bit with no group map is searched again without being cleared (0xb7db3b..0xb7db4d jump back
            // to the loop head): the call never returns unless `max` is 0.
            if (it == mType1Groups.end()) continue;
            map = &it->second;
        }
        for (auto it = map->begin(); it != map->end() && max != 0; ++it) {
            void *entry = it->second.Get();
            if (Takes(predicate, entry)) {
                out->push_back(entry);
                --max;
            }
        }
        if (static_cast<uint16_t>(asked) == 0) return;
        remaining &= ~low;
        low = (0u - remaining) & remaining;
    }
}

// 0xb7dbf0: the property objects of every type's entries the predicate takes, for the types (1..10, in that order) its
// slot 4 accepts, up to `max`.
void cTSSkinCatalog::FindPropsMatching(SkinPtrVector *out, void *predicate, bool append, int32_t max)
{
    if (!append) out->clear();
    EnsureLoaded(this);
    SkinEntryMap *maps[] = {&mType1, &mType2, &mType3, &mType4, &mType5, &mType6, &mType7, &mType8, &mType9, &mType10};
    for (uint32_t type = 1; type <= kTypeCount; ++type) {
        if (!VirtualCall<bool(__thiscall *)(void *, uint32_t), 4>(predicate, type)) continue;
        SkinEntryMap &map = *maps[type - 1];
        for (auto it = map.begin(); it != map.end(); ++it) {
            if (max == 0) break;
            void *props = Props(it->second.Get());
            if (Takes(predicate, props)) {
                out->push_back(props);
                --max;
            }
        }
    }
}

// ---- types 2 and 3 ----

// 0xb7d510 / 0xb7b780.
void cTSSkinCatalog::FindType2(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint32_t mask25,
                               const SkinKey *key)
{
    FindWithPropsKey(this, mType2, out, ages, genders, flags, mask25, key);
}

void cTSSkinCatalog::FindType3(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint32_t mask25,
                               const SkinKey *key)
{
    FindWithPropsKey(this, mType3, out, ages, genders, flags, mask25, key);
}

// 0xb7d650 / 0xb7b8c0 (and the other "Matching" slots of one map).
void cTSSkinCatalog::FindType2Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max)
{
    if (!append) out->clear();
    EnsureLoaded(this);
    TakeMatching(mType2, out, predicate, max);
}

void cTSSkinCatalog::FindType3Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max)
{
    if (!append) out->clear();
    EnsureLoaded(this);
    TakeMatching(mType3, out, predicate, max);
}

// 0xb7ada0 / 0xb7ad30.
bool cTSSkinCatalog::HasType2(const SkinKey *key) { return HasKey(this, mType2, key); }
bool cTSSkinCatalog::HasType3(const SkinKey *key) { return HasKey(this, mType3, key); }

// ---- the overlays (types 5 and 4) ----

// 0xb7c2c0: the property objects of the type-5 entries, then the type-4 ones, passing the filters (type 5: GUIDs at slots
// 7 / 8, mask at slot 10; type 4: GUIDs at slots 8 / 9, mask at slot 13).
void cTSSkinCatalog::FindOverlayProps(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags,
                                      const SkinKey *guidA, const SkinKey *guidB, const SkinKey *id, uint16_t mask)
{
    out->clear();
    EnsureLoaded(this);
    for (auto &[k, ref] : mType5) {
        void *entry = ref.Get();
        if (OverlayMatches<7, 8, 10>(entry, ages, genders, flags, guidA, guidB, id, false, 0, mask)) {
            out->push_back(Props(entry));
        }
    }
    for (auto &[k, ref] : mType4) {
        void *entry = ref.Get();
        if (OverlayMatches<8, 9, 13>(entry, ages, genders, flags, guidA, guidB, id, false, 0, mask)) {
            out->push_back(Props(entry));
        }
    }
}

// 0xb7c720: the property objects of the type-5 then type-4 entries the predicate takes (slot 3 asked again for the push).
void cTSSkinCatalog::FindOverlayPropsMatching(SkinPtrVector *out, void *predicate, bool append, int32_t max)
{
    if (!append) out->clear();
    EnsureLoaded(this);
    if (mType5.size() == 0 && mType4.size() == 0) return;
    for (SkinEntryMap *map : {&mType5, &mType4}) {
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (max == 0) return;
            void *entry = it->second.Get();
            if (Takes(predicate, Props(entry))) {
                out->push_back(Props(entry));
                --max;
            }
        }
    }
}

// 0xb7bff0 / 0xb7be10 / 0xb7e090: one overlay map's entries passing the filters (with the props' 16-bit mask but in 23).
void cTSSkinCatalog::FindType5(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, const SkinKey *guidA,
                               const SkinKey *guidB, const SkinKey *id, uint16_t mask22, uint16_t mask)
{
    out->clear();
    EnsureLoaded(this);
    if (mType5.size() == 0) return;
    for (auto &[k, ref] : mType5) {
        void *entry = ref.Get();
        if (OverlayMatches<7, 8, 10>(entry, ages, genders, flags, guidA, guidB, id, true, mask22, mask)) out->push_back(entry);
    }
}

void cTSSkinCatalog::FindType5Any(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, const SkinKey *guidA,
                                  const SkinKey *guidB, const SkinKey *id, uint16_t mask)
{
    out->clear();
    EnsureLoaded(this);
    if (mType5.size() == 0) return;
    for (auto &[k, ref] : mType5) {
        void *entry = ref.Get();
        if (OverlayMatches<7, 8, 10>(entry, ages, genders, flags, guidA, guidB, id, false, 0, mask)) out->push_back(entry);
    }
}

void cTSSkinCatalog::FindType4(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, const SkinKey *guidA,
                               const SkinKey *guidB, const SkinKey *id, uint16_t mask22, uint16_t mask)
{
    out->clear();
    EnsureLoaded(this);
    if (mType4.size() == 0) return;
    for (auto &[k, ref] : mType4) {
        void *entry = ref.Get();
        if (OverlayMatches<8, 9, 13>(entry, ages, genders, flags, guidA, guidB, id, true, mask22, mask)) out->push_back(entry);
    }
}

void cTSSkinCatalog::FindType5Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max)
{
    if (!append) out->clear();
    EnsureLoaded(this);
    TakeMatching(mType5, out, predicate, max);
}

void cTSSkinCatalog::FindType4Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max)
{
    if (!append) out->clear();
    EnsureLoaded(this);
    TakeMatching(mType4, out, predicate, max);
}

// ---- types 6..10 ----

// 0xb7ce10: ages, genders, flags and the props' 16-bit mask.
void cTSSkinCatalog::FindType6(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22)
{
    out->clear();
    EnsureLoaded(this);
    if (mType6.size() == 0) return;
    for (auto &[k, ref] : mType6) {
        void *entry = ref.Get();
        if (AgesAndGenders(entry, ages, genders) && FlagsMatch(entry, flags) && Mask22Matches(entry, mask22)) {
            out->push_back(entry);
        }
    }
}

// 0xb7cfd0 / 0xb7c840 / 0xb7cc20 / 0xb7ca30: ... and an id at the entry's slot 5, 6, 10 and 6.
void cTSSkinCatalog::FindType7(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22,
                               const SkinKey *id)
{
    FindWithId<5>(this, mType7, out, ages, genders, flags, mask22, id);
}

void cTSSkinCatalog::FindType8(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22,
                               const SkinKey *id)
{
    FindWithId<6>(this, mType8, out, ages, genders, flags, mask22, id);
}

void cTSSkinCatalog::FindType9(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22,
                               const SkinKey *id)
{
    FindWithId<10>(this, mType9, out, ages, genders, flags, mask22, id);
}

void cTSSkinCatalog::FindType10(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22,
                                const SkinKey *id)
{
    FindWithId<6>(this, mType10, out, ages, genders, flags, mask22, id);
}

// 0xb7cf20 / 0xb7d110 / 0xb7c980 / 0xb7cd60 / 0xb7cb70.
void cTSSkinCatalog::FindType6Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max)
{
    if (!append) out->clear();
    EnsureLoaded(this);
    TakeMatching(mType6, out, predicate, max);
}

void cTSSkinCatalog::FindType7Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max)
{
    if (!append) out->clear();
    EnsureLoaded(this);
    TakeMatching(mType7, out, predicate, max);
}

void cTSSkinCatalog::FindType8Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max)
{
    if (!append) out->clear();
    EnsureLoaded(this);
    TakeMatching(mType8, out, predicate, max);
}

void cTSSkinCatalog::FindType9Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max)
{
    if (!append) out->clear();
    EnsureLoaded(this);
    TakeMatching(mType9, out, predicate, max);
}

void cTSSkinCatalog::FindType10Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max)
{
    if (!append) out->clear();
    EnsureLoaded(this);
    TakeMatching(mType10, out, predicate, max);
}

// ---- applying, picking ----

// 0xb7d1c0: an entry of the key's type (by its resource type) under the key applies itself (slot 0: target, arg); without
// one, or when it answers false, the persist resource manager does (slot 5: key, target, arg, 0, 0). Other types: false.
bool cTSSkinCatalog::ApplyEntry(const SkinKey *key, void *target, uint32_t arg)
{
    EnsureLoaded(this);
    SkinEntryMap *map;
    switch (key->type) {
    case 0xebcf3e27: map = &mType1; break;
    case 0x4c158081: map = &mType2; break;
    case 0x8c1580b5: map = &mType3; break;
    case 0x2c1fd8a1: map = &mType4; break;
    case 0x0c1fe246: map = &mType5; break;
    case 0x6c93b566: map = &mType6; break;
    case 0x8c93bf6c: map = &mType7; break;
    case 0x8c93e35c: map = &mType8; break;
    case 0x0c93e3de: map = &mType9; break;
    case 0xd1954460: map = &mType10; break;
    default: return false;
    }
    auto it = map->find(*key);
    if (it != map->end() && it->second.Get() &&
        VirtualCall<bool(__thiscall *)(void *, void *, uint32_t), 0>(it->second.Get(), target, arg)) {
        return true;
    }
    RZAutoRef manager{nullptr};
    if (void *framework = GetServiceObject()) {
        VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(framework, 0x056b906e, 0x9af5e924,
                                                                               &manager.mObject);
    }
    const bool result = VirtualCall<bool(__thiscall *)(void *, const SkinKey *, void *, uint32_t, uint32_t, uint32_t), 5>(
        manager.mObject, key, target, arg, 0, 0);
    manager.ReleaseObject();
    return result;
}

// 0xb7a8e0: a random one (mRandom) of the type-1 entries the predicate takes (slot 12, through the vtable): its property
// object's key (slot 3, returned by value) into *out.
bool cTSSkinCatalog::PickRandomType1(SkinKey *out, void *predicate)
{
    SkinPtrVector found;
    FindType1Matching(&found, predicate, false, -1);
    if (!found.empty()) {
        void *entry = found[mRandom.RandomUint32Uniform(static_cast<uint32_t>(found.size()))];
        if (entry) {
            SkinKey key;
            *out = *VirtualCall<const SkinKey *(__thiscall *)(void *, SkinKey *), 3>(Props(entry), &key);
            return true;
        }
    }
    return false;
}

// 0xb7a810: the same over the property objects slot 13 finds.
bool cTSSkinCatalog::PickRandomProps(SkinKey *out, void *predicate)
{
    SkinPtrVector found;
    FindPropsMatching(&found, predicate, false, -1);
    if (!found.empty()) {
        void *props = found[mRandom.RandomUint32Uniform(static_cast<uint32_t>(found.size()))];
        if (props) {
            SkinKey key;
            *out = *VirtualCall<const SkinKey *(__thiscall *)(void *, SkinKey *), 3>(props, &key);
            return true;
        }
    }
    return false;
}

// ---- sources ----

// 0xb740e0: a source not yet held (slot 42, through the vtable) is added (counted), and loaded (0xb7b2d0) when asked. False
// only for null.
bool cTSSkinCatalog::AddSource(void *source, bool load)
{
    if (!source) return false;
    if (!HasSource(source)) {
        mSources.push_back(SkinRef(static_cast<cIGZUnknown *>(source)));
        if (load) OriginalFunction<bool(__thiscall *)(cTSSkinCatalog *, void *), 0xb7b2d0>()(this, source);
    }
    return true;
}

// 0xb81700: the first held source with `source`'s id (slot 15) is removed.
bool cTSSkinCatalog::RemoveSource(void *source)
{
    if (source) {
        const uint32_t id = WordAt<15>(source);
        for (auto it = mSources.begin(); it != mSources.end(); ++it) {
            if (WordAt<15>(it->Get()) == id) {
                mSources.erase(it);
                return true;
            }
        }
    }
    return false;
}

// 0xb7ace0: whether a held source has `source`'s id (slot 15).
bool cTSSkinCatalog::HasSource(void *source)
{
    if (source) {
        const uint32_t id = WordAt<15>(source);
        for (auto it = mSources.begin(); it != mSources.end(); ++it) {
            if (WordAt<15>(it->Get()) == id) return true;
        }
    }
    return false;
}

// 0xb7a9b0: every source's name (slot 11) appended to *out.
void cTSSkinCatalog::GetSourceNames(ContentServiceStrings *out)
{
    for (auto it = mSources.begin(); it != mSources.end(); ++it) {
        ContentString name;
        VirtualCall<void(__thiscall *)(void *, cIGZString *), 11>(it->Get(), name.s.AsInterface());
        out->push_back(name);
    }
}

// 0xb7a600: the current source's name (slot 11) into *out; false without one.
bool cTSSkinCatalog::GetCurrentSourceName(cIGZString *out)
{
    if (mCurrent < 0) return false;
    VirtualCall<void(__thiscall *)(void *, cIGZString *), 11>(mSources.data()[mCurrent].Get(), out);
    return true;
}

// 0xb81920: the source named `name` (0xb79f50, -1 when none) made current, checked (0xb81820 with 1).
bool cTSSkinCatalog::SelectSource(const char *name)
{
    const int32_t index = OriginalFunction<int32_t(__thiscall *)(cTSSkinCatalog *, const char *), 0xb79f50>()(this, name);
    return OriginalFunction<bool(__thiscall *)(cTSSkinCatalog *, int32_t, bool), 0xb81820>()(this, index, true);
}

// 0xb7ae60: whether the source named `name` is open (0xb7ae10).
bool cTSSkinCatalog::IsSourceOpen(const char *name)
{
    const int32_t index = OriginalFunction<int32_t(__thiscall *)(cTSSkinCatalog *, const char *), 0xb79f50>()(this, name);
    return OriginalFunction<bool(__thiscall *)(cTSSkinCatalog *, int32_t), 0xb7ae10>()(this, index);
}

// 0xb75dc0.
void cTSSkinCatalog::ClearCurrentSource() { mCurrent = -1; }

} // namespace nTSCASCore
