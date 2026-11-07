#include "cas/cTSBinCatalog.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"

#include <algorithm>
#include <bit>
#include <new>

namespace nTSCASCore {

namespace {

// nTSCASCore::cTSBinInfoFactory as Init builds it inline (12 bytes, not ours): the cIGZPersistResourceFactory vtable (a
// dead store), the cRZUnknown part at +4 (0x1b9052: vtable, zero count), then the class's two game vtables.
struct BinInfoFactoryLayout {
    const void *mVTable;      // +0x0 0xe8af20
    RZUnknownLayout mUnknown; // +0x4 0xe8af40, count +8
};
static_assert(sizeof(BinInfoFactoryLayout) == 0xc, "BinInfoFactoryLayout layout");
constexpr uint32_t kPersistResourceFactoryVTable = 0xda6718;
constexpr uint32_t kBinInfoFactoryVTable = 0xe8af20;
constexpr uint32_t kBinInfoFactoryVTable4 = 0xe8af40;
constexpr uint32_t kFactoryKind = 0x8c4f2caa;
constexpr uint32_t kFactoryTypes[] = {0xac598eac, 0x6c4f359d, 0x0c560f39}; // outfit data, bin info, sim fragment

constexpr uint32_t kBinType = 0x6c4f359d;      // cTSBinInfo's resource type
constexpr uint32_t kBinIid = 0x6c4f3599;       // cTSBinInfo's interface
constexpr uint32_t kFragmentType = 0x0c560f39; // cTSSimFragment's resource type
constexpr uint32_t kOutfitType = 0xac598eac;   // the outfit data a collection lists (slot 46)
constexpr uint32_t kDefaultGroup = 0x4f184aa9;
constexpr uint32_t kGroupIid = 0x3b2271bb;     // what a source answers with its group (slot 4) and its state (slot 6)
constexpr uint32_t kFragmentSize = 0x74;
constexpr uint32_t kTypeFilterVTable = 0xda5cf0; // cGZPersistResourceKeyFilterByType (the persist code's)

// cGZPersistResourceKeyFilterByType as slot 5 builds it (12 bytes, not ours: the game's vtable, its count, the type).
struct KeyFilterByType {
    const void *mVTable;
    uint32_t mRefCount;
    uint32_t mType;
};
static_assert(sizeof(KeyFilterByType) == 0xc, "KeyFilterByType layout");

// cTSBinTypeFilter as slot 31 builds it on its stack (12 bytes; its constructor 0xb644a0 is still game code).
struct BinTypeFilter {
    const void *mVTable;
    uint32_t mRefCount;
    int32_t mType;
};
static_assert(sizeof(BinTypeFilter) == 0xc, "BinTypeFilter layout");

// A source's resource keys (slot 19's out-parameter), from the allocator service.
using KeyList = std::vector<BinKey, GZServiceAllocator<BinKey>>;

// ---- the image's code this class still calls (`this` the catalog) ----

void EnsureLoaded(cTSBinCatalog *self)
{
    OriginalFunction<void(__thiscall *)(cTSBinCatalog *), 0xb88e10>()(self);
}
bool LoadSource(cTSBinCatalog *self, void *source, uint32_t flag)
{
    return OriginalFunction<bool(__thiscall *)(cTSBinCatalog *, void *, uint32_t), 0xb8d6b0>()(self, source, flag);
}
void RebuildIndex(cTSBinCatalog *self) { OriginalFunction<void(__thiscall *)(cTSBinCatalog *), 0xb8f3d0>()(self); }
void UpdatePositions(cTSBinCatalog *self) { OriginalFunction<void(__thiscall *)(cTSBinCatalog *), 0xb90b80>()(self); }
// 0xb8cbe0: one step of a pending object's work (its state's low byte the step; -1 when done).
void WorkPending(cTSBinCatalog *self, PendingEntry *entry)
{
    OriginalFunction<void(__thiscall *)(cTSBinCatalog *, PendingEntry *), 0xb8cbe0>()(self, entry);
}
// 0xb8a8d0: the fragment filing `outfit` in the bin `bin` (null when none).
void *FindFragment(cTSBinCatalog *self, const BinKey *outfit, const BinKey *bin)
{
    return OriginalFunction<void *(__thiscall *)(cTSBinCatalog *, const BinKey *, const BinKey *), 0xb8a8d0>()(self, outfit,
                                                                                                                 bin);
}
// 0xb8adc0: renumbers the fragments in [first, last) one apart; whether every save went through.
bool RenumberRange(cTSBinCatalog *self, BinRef *first, BinRef *last)
{
    return OriginalFunction<bool(__thiscall *)(cTSBinCatalog *, BinRef *, BinRef *), 0xb8adc0>()(self, first, last);
}
// 0xb8feb0 (cdecl): tells a source a key left it.
void SourceDropped(void *source, const BinKey *key)
{
    OriginalFunction<uint8_t(__cdecl *)(void *, const BinKey *), 0xb8feb0>()(source, key);
}
using GatherFn = void(__thiscall *)(cTSBinCatalog *, BinList *, void *, void *, bool);

void *PersistService() { return CachedServiceGetter<0x10fc684, 0x056b906e, 0x9af5e924>(); } // 0xb8bcb0
void *PersistServiceDirect() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); } // 0xb390c0

// ---- the slots of the objects the catalog holds ----

void *Call3(void *object) { return VirtualCall<void *(__thiscall *)(void *), 3>(object); }
// A resource's key (slot 3: written into `buffer`, which is answered).
const BinKey *KeyOf(void *object, BinKey *buffer)
{
    return VirtualCall<const BinKey *(__thiscall *)(void *, BinKey *), 3>(object, buffer);
}
void SetKey(void *object, const BinKey *key) { VirtualCall<void(__thiscall *)(void *, const BinKey *), 4>(object, key); }
template <int Slot> uint32_t WordAt(void *object) { return VirtualCall<uint32_t(__thiscall *)(void *), Slot>(object); }
template <int Slot> uint8_t ByteAt(void *object) { return VirtualCall<uint8_t(__thiscall *)(void *), Slot>(object); }
// A bin's sort index (slots 22 / 23).
int32_t BinIndex(void *bin) { return VirtualCall<int32_t(__thiscall *)(void *), 22>(bin); }
void SetBinIndex(void *bin, int32_t index) { VirtualCall<void(__thiscall *)(void *, int32_t), 23>(bin, index); }
// A fragment's bin key (19), outfit key (21), sort index (24 / 25) and layer (26 / 27).
const BinKey *BinKeyOf(void *fragment) { return VirtualCall<const BinKey *(__thiscall *)(void *), 19>(fragment); }
const BinKey *OutfitKeyOf(void *fragment) { return VirtualCall<const BinKey *(__thiscall *)(void *), 21>(fragment); }
void SetFragmentIndex(void *fragment, int32_t index)
{
    VirtualCall<void(__thiscall *)(void *, int32_t), 24>(fragment, index);
}
int32_t FragmentIndexOf(void *fragment) { return VirtualCall<int32_t(__thiscall *)(void *), 25>(fragment); }
uint16_t LayerOf(void *fragment) { return VirtualCall<uint16_t(__thiscall *)(void *), 27>(fragment); }
uint32_t SourceId(void *source) { return WordAt<15>(source); }
bool Query(void *object, uint32_t iid, void **out)
{
    return VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(object, iid, out);
}
void Grab(void *object) { VirtualCall<uint32_t(__thiscall *)(void *), 1>(object); }
void Drop(void *object) { VirtualCall<uint32_t(__thiscall *)(void *), 2>(object); }

// The persist manager (the service's slot 3): 15 whether a key is taken, 34 the source holding a key (raw), 13 files a
// key in a source, 10 forgets a key, 8 takes a resource.
bool KeyTaken(void *manager, const BinKey *key)
{
    return VirtualCall<bool(__thiscall *)(void *, const BinKey *), 15>(manager, key);
}
void SourceOf(void *manager, const BinKey *key, void **out)
{
    VirtualCall<void(__thiscall *)(void *, const BinKey *, void **), 34>(manager, key, out);
}
void ForgetKey(void *manager, const BinKey *key) { VirtualCall<void(__thiscall *)(void *, const BinKey *), 10>(manager, key); }
// The service's slot 16: saves a resource (into `source`, or where it lives).
uint8_t Save(void *service, void *resource, void *source)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, void *, void *), 16>(service, resource, source);
}
// A source's slot 26 (drops a key), 10 (marks itself changed).
uint8_t SourceRemove(void *source, const BinKey *key)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, const BinKey *), 26>(source, key);
}
uint8_t SourceTouch(void *source) { return VirtualCall<uint8_t(__thiscall *)(void *), 10>(source); }

bool SameKey(const BinKey *a, const BinKey *b)
{
    return a->type == b->type && a->group == b->group && a->instance == b->instance && a->resource == b->resource;
}

// The display order: by sort index (slot 22, signed); a null bin sorts last (0xb86470's and 0xb8ad20's predicate; the
// left operand's index is read first).
bool BinBefore(void *a, void *b)
{
    if (!a) return false;
    if (!b) return true;
    const int32_t left = BinIndex(a);
    return left < BinIndex(b);
}
struct BinLess {
    bool operator()(const BinRef &a, const BinRef &b) const { return BinBefore(a.Get(), b.Get()); }
    bool operator()(const BinRef &a, void *b) const { return BinBefore(a.Get(), b); }
};

// The fragments' order by layer (slot 27, 16 bits): 0 first, 0xffff last, the others from the highest down.
int32_t CompareLayers(uint16_t a, uint16_t b)
{
    if (a == b) return 0;
    if (a == 0) return -1;
    if (a == 0xffff) return 1;
    if (b == 0) return 1;
    if (b == 0xffff) return -1;
    return b < a ? -1 : 1;
}
bool FragmentBefore(void *a, void *b)
{
    const uint16_t left = LayerOf(a);
    return CompareLayers(left, LayerOf(b)) < 0;
}
struct FragmentLess {
    bool operator()(const BinRef &a, const BinRef &b) const { return FragmentBefore(a.Get(), b.Get()); }
    bool operator()(const BinRef &a, void *b) const { return FragmentBefore(a.Get(), b); }
    bool operator()(void *a, const BinRef &b) const { return FragmentBefore(a, b.Get()); }
};

// The fragments' full order (0x72079e, MoveFragment's sort): by layer, then by sort index (slot 25, the right operand's
// read first): descending in layer 0, ascending in the others.
bool FragmentSortBefore(void *a, void *b)
{
    const uint16_t left = LayerOf(a);
    const uint16_t right = LayerOf(b);
    if (left != right) return CompareLayers(left, right) < 0;
    const int32_t theirs = FragmentIndexOf(b);
    const int32_t own = FragmentIndexOf(a);
    return left == 0 ? own > theirs : own < theirs;
}
struct FragmentSortLess {
    bool operator()(const BinRef &a, const BinRef &b) const { return FragmentSortBefore(a.Get(), b.Get()); }
};

// 0xb8aaf0: in the run of `list` with `fragment`'s layer, the entry with its key (each entry's key read before the
// fragment's), else the list's end.
BinRef *FindSameFragment(BinList *list, void *fragment)
{
    auto [first, last] = std::equal_range(list->data(), list->data() + list->size(), fragment, FragmentLess{});
    for (BinRef *it = first; it != last; ++it) {
        BinKey a;
        BinKey b;
        const BinKey *entry = KeyOf(it->Get(), &a);
        const BinKey *own = KeyOf(fragment, &b);
        if (SameKey(own, entry)) return it;
    }
    return list->data() + list->size();
}

// The bins' list a slot works on: the caller's, or the display order.
BinList &ListOr(BinList *list, BinList &order) { return list ? *list : order; }

} // namespace

// ---- construction ----

// 0xb86c50: the service (id 0x0c4f252f), no sources, empty maps and lists, sort mode 1, then ClearAll (called directly by
// the original, as a constructor's virtual call is). mLoaded is first written by ClearAll.
cTSBinCatalog::cTSBinCatalog()
    : rz::cRZSystemService(kServiceId, kPriority), mSourcesLoaded(false), mNextInstance(0), mIndexed(false), mSortMode(1)
{
    ClearAll();
}

// 0xb87060: ClearAll, then the members in reverse order (mCustomSource released among them), then the service.
cTSBinCatalog::~cTSBinCatalog() noexcept { ClearAll(); }

cTSBinCatalog *__cdecl cTSBinCatalog::Create() { return new cTSBinCatalog; }

cTSBinCatalog *__fastcall cTSBinCatalogAdapters::Construct(void *memory, void *) { return ::new (memory) cTSBinCatalog; }

// ---- cIGZUnknown / cIGZSystemService ----

// 0xb8dd00: cITSBinCatalog -> the +0x1c face (counted through the primary face's AddRef), else the service's.
bool cTSBinCatalog::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cITSBinCatalog *>(this);
    AddRef();
    return true;
}

uint32_t cTSBinCatalog::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSBinCatalog::Release() { return rz::cRZSystemService::Release(); }

// 0xb8c090: registers the bin info factory with the persist resource manager for the three CAS resource types, then
// hands the catalog to the framework (its slot 9).
bool cTSBinCatalog::Init()
{
    // The base's Init (shared 0x144910) answers true without side effects: the original's early return is dead.
    void *manager = Call3(PersistService());
    auto *factory = static_cast<BinInfoFactoryLayout *>(::operator new(sizeof(BinInfoFactoryLayout)));
    factory->mVTable = GameAddress<void>(kPersistResourceFactoryVTable);
    factory->mUnknown.Construct();
    factory->mVTable = GameAddress<void>(kBinInfoFactoryVTable);
    factory->mUnknown.mVTable = GameAddress<void>(kBinInfoFactoryVTable4);
    Grab(factory);
    for (uint32_t type : kFactoryTypes) {
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void *), 20>(manager, kFactoryKind, type, factory);
    }
    // The framework, cached in a global the first time (as the original does); its slot 9 takes the system.
    void *&framework = *GameAddress<void *>(0x10f5194);
    if (!framework) framework = GetServiceObject();
    VirtualCall<uint32_t(__thiscall *)(void *, void *), 9>(framework, this);
    Drop(factory);
    return true;
}

// folded 0xb82190 (shared with cTSSkinCatalog): ClearAll through the interface's vtable, then the base's Shutdown (true).
bool cTSBinCatalog::Shutdown()
{
    ClearAll();
    return rz::cRZSystemService::Shutdown();
}

// 0xb8d510: works the pending queue (0xb8dc50), then the base's OnTick (true). From the front: an entry already done
// leaves the queue and ends the tick; otherwise one step of it runs (0xb8cbe0), and the tick ends when that finished it
// (it leaves) or moved it to another step; an entry whose step did not change lets the next one run.
bool cTSBinCatalog::OnTick(uint32_t counter)
{
    for (size_t i = 0; i < mPending.size(); ++i) {
        PendingEntry &entry = mPending[i];
        if (entry.state != 0xffffffffu) {
            const uint8_t step = static_cast<uint8_t>(entry.state);
            WorkPending(this, &entry);
            if (entry.state != 0xffffffffu) {
                if (static_cast<uint8_t>(entry.state) != step) return rz::cRZSystemService::OnTick(counter);
                continue;
            }
        }
        mPending.erase(mPending.begin() + static_cast<ptrdiff_t>(i));
        break;
    }
    return rz::cRZSystemService::OnTick(counter);
}

// ---- cITSBinCatalog: sources ----

// 0xb8d5a0: loads every source not loaded yet. Before the first EnsureLoaded that is EnsureLoaded itself. Otherwise each
// source is loaded again (LoadSource 0xb8d6b0, counting its bins into mBinCount); one that fails leaves the catalog when
// it has no 0x3b2271bb interface or that interface's slot 6 answers 1. Then a count marks the order dirty, the index is
// rebuilt (0xb8f3d0) when kept, and the sources count as loaded.
uint32_t cTSBinCatalog::LoadSources()
{
    if (!mLoaded) {
        EnsureLoaded(this);
        return mBinCount;
    }
    if (mSourcesLoaded) return mBinCount;
    mBinCount = 0;
    BinRef held;
    for (auto it = mSources.begin(); it != mSources.end();) {
        void *source = it->Get();
        // ORIGINAL-BUG: a null source is neither loaded nor skipped: the loop never ends.
        if (!source) continue;
        if (!LoadSource(this, source, 0)) {
            if (!Query(source, kGroupIid, held.AsPPVoid()) || WordAt<6>(held.Get()) == 1) {
                it = mSources.erase(it);
                continue;
            }
        }
        ++it;
    }
    if (mBinCount) mDirty = true;
    if (mIndexed) RebuildIndex(this);
    mSourcesLoaded = true;
    return mBinCount;
}

// 0xb88600: adds `source` (by id, once) and loads it now (LoadSource with 1) or marks the sources not loaded.
bool cTSBinCatalog::AddSource(void *source, bool load)
{
    if (!source) return false;
    if (!HasSource(source)) {
        BinRef held(static_cast<cIGZUnknown *>(source));
        mSources.push_back(held);
        held = BinRef();
        if (load) {
            LoadSource(this, source, 1);
        } else {
            mSourcesLoaded = false;
        }
    }
    return true;
}

// 0xb903e0: the first source with `source`'s id (slot 15) leaves (its fragments out of the index first, when kept:
// 0xb87f70 lists them, 0xb903a0 drops them), then every bin filed under a key `source` lists (slot 19, through a type
// filter for bins) that `source` holds (slot 20 of the bin's key) leaves the catalog; groups left empty go.
bool cTSBinCatalog::RemoveSource(void *source)
{
    if (!source) return false;
    bool removed = false;
    const uint32_t id = SourceId(source);
    for (auto it = mSources.begin(); it != mSources.end(); ++it) {
        if (SourceId(it->Get()) != id) continue;
        if (mIndexed) {
            BinList fragments;
            OriginalFunction<void(__cdecl *)(void *, BinList *, uint32_t, uint32_t), 0xb87f70>()(it->Get(), &fragments, 0, 0);
            OriginalFunction<void(__thiscall *)(cTSBinCatalog *, BinList *), 0xb903a0>()(this, &fragments);
        }
        mSources.erase(it);
        removed = true;
        break;
    }
    auto *filter = static_cast<KeyFilterByType *>(::operator new(sizeof(KeyFilterByType)));
    if (filter) {
        filter->mRefCount = 0;
        filter->mVTable = GameAddress<void>(kTypeFilterVTable);
        filter->mType = kBinType;
        Grab(filter);
    }
    KeyList keys;
    const uint32_t count = VirtualCall<uint32_t(__thiscall *)(void *, KeyList *, void *), 19>(source, &keys, filter);
    for (uint32_t i = 0; i < count; ++i) {
        auto group = mBins.find(keys.data()[i].group);
        if (group == mBins.end()) continue;
        BinInstanceMap &bins = group->second;
        for (auto it = bins.begin(); it != bins.end();) {
            BinRef bin = it->second;
            auto current = it++;
            if (bin) {
                BinKey key;
                if (VirtualCall<bool(__thiscall *)(void *, const BinKey *), 20>(source, KeyOf(bin.Get(), &key))) {
                    bins.erase(current);
                    mDirty = true;
                }
            }
        }
        if (bins.empty()) mBins.erase(group);
    }
    keys = KeyList();
    if (filter) Drop(filter);
    return removed;
}

// 0xb8c1f0: whether a source with `source`'s id (slot 15) is in the catalog.
bool cTSBinCatalog::HasSource(void *source)
{
    if (!source) return false;
    const uint32_t id = SourceId(source);
    for (auto &held : mSources) {
        if (SourceId(held.Get()) == id) return true;
    }
    return false;
}

// 0xb8bd00: each source's name (slot 11) appended to *out.
void cTSBinCatalog::GetSourceNames(ContentServiceStrings *out)
{
    for (auto it = mSources.begin(); it != mSources.end(); ++it) {
        ContentString name;
        VirtualCall<void(__thiscall *)(void *, cIGZString *), 11>(it->Get(), name.s.AsInterface());
        out->push_back(name);
    }
}

// ---- bins ----

// 0xb877c0: files `bin`: its key gets type 0x6c4f359d, a group (0 -> 0x4f184aa9) and, when it has none, the next
// instance the persist manager does not know. The source is `source`, else the one the manager names, else the first
// (none: false); a key the source already holds is forgotten by the manager first. The bin is keyed, handed to the
// manager and filed in the source (false when that fails); `source` joins the catalog; the bin goes into its group
// (made when new) under its instance, and the source is marked changed.
bool cTSBinCatalog::AddBin(void *bin, void *source)
{
    EnsureLoaded(this);
    void *manager = Call3(PersistService());
    BinKey key;
    KeyOf(bin, &key);
    if (key.group == 0) key.group = kDefaultGroup;
    key.type = kBinType;
    if (key.instance == 0) {
        do {
            key.instance = ++mNextInstance;
        } while (KeyTaken(manager, &key));
    }
    void *target = source;
    if (!target) {
        SourceOf(manager, &key, &target);
        if (!target) {
            if (mSources.empty()) return false;
            target = mSources.front().Get();
        }
    }
    if (VirtualCall<bool(__thiscall *)(void *, const BinKey *), 20>(target, &key)) ForgetKey(manager, &key);
    SetKey(bin, &key);
    VirtualCall<void(__thiscall *)(void *, void *), 8>(manager, bin);
    if (!VirtualCall<bool(__thiscall *)(void *, const BinKey *, void *), 13>(manager, &key, target)) return false;
    if (source) AddSource(source, true);
    BinKey scratch;
    auto group = mBins.find(KeyOf(bin, &scratch)->group);
    if (group == mBins.end()) {
        mBins.emplace(KeyOf(bin, &scratch)->group, BinInstanceMap{});
        group = mBins.find(KeyOf(bin, &scratch)->group);
    }
    group->second[KeyOf(bin, &scratch)->instance].Assign(static_cast<cIGZUnknown *>(bin));
    mDirty = true;
    SourceTouch(target);
    if (mIndexed) OriginalFunction<void(__thiscall *)(cTSBinCatalog *, void *), 0xb87a40>()(this, bin);
    return true;
}

// 0xb906c0: every bin filed again in the source holding its key (the manager's slots 34 and 13); false when one has no
// source or a filing fails (the rest are still filed).
bool cTSBinCatalog::SaveBins(uint32_t)
{
    EnsureLoaded(this);
    bool ok = true;
    void *manager = Call3(PersistService());
    for (auto &[group, bins] : mBins) {
        for (auto &[instance, bin] : bins) {
            BinKey key;
            KeyOf(bin.Get(), &key);
            void *source = nullptr;
            SourceOf(manager, &key, &source);
            if (!source) {
                ok = false;
            } else {
                ok &= VirtualCall<bool(__thiscall *)(void *, const BinKey *, void *), 13>(manager, &key, source);
            }
        }
    }
    return ok;
}

// 0xb8fdc0: everything dropped: the pending queue (slot 41), the index, the count, the sources, the bins, the display
// order, the positions and the bin types; not loaded, the order dirty.
void cTSBinCatalog::ClearAll()
{
    ClearPending();
    ClearIndex();
    mBinCount = 0;
    mSources.clear();
    mBins.clear();
    mOrder.clear();
    mPositions.clear();
    mBinTypes.clear();
    mLoaded = 0;
    mDirty = true;
}

void cTSBinCatalog::ClearIndex() { mIndex.clear(); }

// 0xb89630: takes `bin` out of its group (the group goes when left empty), then out of the source holding its key (its
// slot 26; 0xb8feb0 told), and the manager forgets the key. False without a source; else the source's answer.
bool cTSBinCatalog::RemoveBin(void *bin)
{
    EnsureLoaded(this);
    void *manager = Call3(PersistService());
    BinKey key;
    KeyOf(bin, &key);
    BinKey scratch;
    auto group = mBins.find(KeyOf(bin, &scratch)->group);
    if (group != mBins.end()) {
        BinInstanceMap &bins = group->second;
        auto it = bins.find(KeyOf(bin, &scratch)->instance);
        if (it != bins.end()) {
            bins.erase(it);
            mDirty = true;
        }
        if (bins.empty()) mBins.erase(group);
    }
    void *source = nullptr;
    SourceOf(manager, &key, &source);
    if (!source) return false;
    const uint8_t removed = SourceRemove(source, &key);
    SourceDropped(source, &key);
    ForgetKey(manager, &key);
    if (mIndexed && removed) OriginalFunction<void(__thiscall *)(cTSBinCatalog *, void *), 0xb90150>()(this, bin);
    return std::bit_cast<bool>(removed);
}

// 0xb8d4e0: the number of bins in the display order (sorted first when dirty).
uint32_t cTSBinCatalog::BinCount()
{
    EnsureLoaded(this);
    if (mDirty) SortBins();
    return static_cast<uint32_t>(mOrder.size());
}

// 0xb8be30: the bins (display order) whose type mask (slot 12) shares a bit with `mask`.
void cTSBinCatalog::GetBinsOfType(BinList *out, uint32_t mask)
{
    EnsureLoaded(this);
    out->clear();
    if (mDirty) SortBins();
    for (auto &bin : mOrder) {
        if (mask & WordAt<12>(bin.Get())) out->push_back(bin);
    }
}

// 0xb8beb0: the bins (display order) `filter` takes (its slot 3); without a filter, all of them.
void cTSBinCatalog::GetBins(BinList *out, void *filter)
{
    EnsureLoaded(this);
    out->clear();
    if (mDirty) SortBins();
    if (filter) {
        for (auto &bin : mOrder) {
            if (VirtualCall<bool(__thiscall *)(void *, void *), 3>(filter, bin.Get())) out->push_back(bin);
        }
    } else if (out != &mOrder) {
        *out = mOrder;
    }
}

// 0xb90940: the display order rebuilt from the bins (null ones left out) and sorted by sort index; then the positions
// (0xb90b80, which keeps them in sort mode 1).
void cTSBinCatalog::SortBins()
{
    EnsureLoaded(this);
    mOrder.clear();
    for (auto &[group, bins] : mBins) {
        for (auto &[instance, ref] : bins) {
            if (!ref) continue;
            BinRef held = ref;
            mOrder.push_back(held);
        }
    }
    std::sort(mOrder.begin(), mOrder.end(), BinLess{});
    mDirty = false;
    UpdatePositions(this);
}

// 0xb8ad20: where `bin` goes in the display order by its sort index (the first entry not before it), -1 past the end.
int32_t cTSBinCatalog::IndexOfBin(void *bin)
{
    auto it = std::lower_bound(mOrder.begin(), mOrder.end(), bin, BinLess{});
    if (it == mOrder.end()) return -1;
    return static_cast<int32_t>(it - mOrder.begin());
}

// 0xb8c600: moves `bin` (found by IndexOfBin) one way (slot 17); whether it moved.
bool cTSBinCatalog::MoveBin(void *bin, uint32_t direction)
{
    const int32_t index = IndexOfBin(bin);
    if (index < 0) return false;
    return MoveBinAt(static_cast<uint32_t>(index), direction, nullptr) != static_cast<uint32_t>(index);
}

// 0xb8c240: moves the bin at `index` of `list` (null: the display order): 0 before the previous one, 1 after the next
// (the two swap sort indexes), 2 to the front (one below the first's index), 3 to the back (one above the last's). The
// new index is saved (the persist service's slot 16) and, for a swap, the other bin's; a failed save restores both
// (re-saving the bin) and leaves `index`. Then the order is dirty and sorted again, and a caller's list follows the
// move. Answers the new position, `index` when nothing moved.
uint32_t cTSBinCatalog::MoveBinAt(uint32_t index, uint32_t direction, BinList *list)
{
    BinList &bins = ListOr(list, mOrder);
    const uint32_t count = static_cast<uint32_t>(bins.size());
    if (count < 2) return index;
    BinRef bin = bins.at(index);
    if (!bin) return index;
    const int32_t oldIndex = BinIndex(bin.Get());
    uint32_t target = index;
    BinRef other;
    int32_t newIndex = 0;
    switch (direction) {
    case 0:
        if (index == 0) return index;
        target = index - 1;
        other = bins.at(target);
        if (!other) return index;
        newIndex = BinIndex(other.Get());
        if (target == index) return index;
        break;
    case 1:
        target = index + 1;
        if (target >= count) return index;
        other = bins.at(target);
        if (!other) return index;
        newIndex = BinIndex(other.Get());
        if (target == index) return index;
        break;
    case 2: {
        if (index == 0) return index;
        void *first = bins.at(0).Get();
        if (!first) return index;
        newIndex = BinIndex(first) - 1;
        target = 0;
        break;
    }
    case 3: {
        if (index + 1 >= count) return index;
        const uint32_t last = count - 1;
        void *tail = bins.at(last).Get();
        if (!tail) return index;
        newIndex = BinIndex(tail) + 1;
        target = last;
        if (target == index) return index;
        break;
    }
    default:
        return index;
    }
    void *service = PersistServiceDirect();
    SetBinIndex(bin.Get(), newIndex);
    if (!Save(service, bin.Get(), nullptr)) {
        SetBinIndex(bin.Get(), oldIndex);
        return index;
    }
    if (other) {
        SetBinIndex(other.Get(), oldIndex);
        if (!Save(service, other.Get(), nullptr)) {
            SetBinIndex(bin.Get(), oldIndex);
            Save(service, bin.Get(), nullptr);
            SetBinIndex(other.Get(), newIndex);
            return index;
        }
    }
    mDirty = true;
    SortBins();
    if (list) {
        if (other) {
            list->at(target).Assign(bin.Get());
            list->at(index).Assign(other.Get());
        } else {
            list->erase(list->begin() + static_cast<ptrdiff_t>(index));
            if (direction == 2) {
                list->insert(list->begin(), bin);
            } else {
                list->push_back(bin);
            }
        }
    }
    return target;
}

// 0xb89a50: makes the sort indexes of `list` (null: the display order, sorted first when dirty; a caller's list is
// sorted here) strictly increasing, saving each bin it bumps (the persist service's slot 16); a failed save restores
// that bin's index and answers false, as a null bin does.
bool cTSBinCatalog::RenumberBins(BinList *list)
{
    BinList &bins = ListOr(list, mOrder);
    const uint32_t count = static_cast<uint32_t>(bins.size());
    if (count < 2) return true;
    if (list) {
        std::sort(list->begin(), list->end(), BinLess{});
    } else if (mDirty) {
        SortBins();
    }
    void *service = PersistServiceDirect();
    // The count was taken before the sort: a display order the sort shortened throws out_of_range, as the original's.
    void *first = bins.at(0).Get();
    if (!first) return false;
    int32_t previous = BinIndex(first);
    for (uint32_t i = 1; i < count; ++i) {
        void *bin = bins.at(i).Get();
        if (!bin) return false;
        int32_t current = BinIndex(bin);
        if (previous >= current) {
            SetBinIndex(bin, previous + 1);
            if (!Save(service, bin, nullptr)) {
                SetBinIndex(bin, current);
                return false;
            }
            current = previous + 1;
        }
        previous = current;
    }
    return true;
}

// 0xb8c050: the bin type whose id (slot 12) is `id` (not counted), null when none.
void *cTSBinCatalog::FindBinType(uint32_t id)
{
    for (auto &type : mBinTypes) {
        if (WordAt<12>(type.Get()) == id) return type.Get();
    }
    return nullptr;
}

// 0xb8a850: the bin filed under the key's group and instance (not counted), null when none.
void *cTSBinCatalog::FindBin(const BinKey *key)
{
    auto group = mBins.find(key->group);
    if (group == mBins.end()) return nullptr;
    auto it = group->second.find(key->instance);
    if (it == group->second.end()) return nullptr;
    return it->second.Get();
}

// ---- fragments ----

// 0xb87bd0: files `fragment`. A fragment key with a group and an instance is kept (whether the manager knows it is
// "existing"); otherwise the fragment already filing the same outfit in the same bin (0xb8a8d0) lends its key and sort
// index ("existing"), or the key gets type 0x0c560f39, the bin's group (or 0x4f184aa9) and a fresh instance, and the
// fragment is keyed. An existing fragment fails unless allowed. It is saved (the service's slot 16) into `source` (which
// then joins), or into the source holding its key (existing) or its bin's key; failing that, a new fragment is saved
// once more after taking the custom source's group (its 0x3b2271bb interface's slot 4) as its slot 9. Then the index
// learns it (0xb8f420 for the bin of an existing one, 0xb87eb0 for a new one) and it moves to its place by layer (slot
// 25 for layer 0, else 24).
bool cTSBinCatalog::AddFragment(void *fragment, void *source, bool allowExisting)
{
    EnsureLoaded(this);
    void *service = PersistService();
    BinKey key;
    KeyOf(fragment, &key);
    bool existing;
    if (key.type == kFragmentType && key.group != 0 && key.instance != 0) {
        existing = KeyTaken(Call3(service), &key);
    } else {
        const BinKey *bin = BinKeyOf(fragment);
        void *found = FindFragment(this, OutfitKeyOf(fragment), bin);
        existing = found != nullptr;
        if (found) {
            BinKey scratch;
            key = *KeyOf(found, &scratch);
            SetFragmentIndex(fragment, FragmentIndexOf(found));
        } else {
            key.type = kFragmentType;
            if (key.group == 0) {
                const uint32_t group = BinKeyOf(fragment)->group;
                key.group = group ? group : kDefaultGroup;
            }
            if (key.instance == 0) {
                do {
                    key.instance = ++mNextInstance;
                } while (KeyTaken(Call3(service), &key));
            }
        }
        SetKey(fragment, &key);
    }
    if (existing && !allowExisting) return false;
    uint8_t ok;
    if (source) {
        ok = Save(service, fragment, source);
        if (!ok) return false;
        AddSource(source, true);
    } else {
        BinRef holder;
        BinKey where;
        if (existing) {
            BinKey scratch;
            where = *KeyOf(fragment, &scratch);
        } else {
            where = *BinKeyOf(fragment);
        }
        void *manager = Call3(service);
        SourceOf(manager, &where, holder.AsPPVoid());
        ok = Save(service, fragment, holder.Get());
        if (!ok && !existing && mCustomSource) {
            BinRef grouped;
            if (Query(mCustomSource.Get(), kGroupIid, grouped.AsPPVoid())) {
                VirtualCall<void(__thiscall *)(void *, uint32_t), 9>(fragment, WordAt<4>(grouped.Get()));
                ok = Save(service, fragment, nullptr);
            }
        }
        if (!ok) return false;
    }
    if (mIndexed) {
        if (existing) {
            OriginalFunction<void(__thiscall *)(cTSBinCatalog *, const BinKey *), 0xb8f420>()(this, BinKeyOf(fragment));
        } else {
            OriginalFunction<void(__thiscall *)(cTSBinCatalog *, void *), 0xb87eb0>()(this, fragment);
        }
    }
    if (LayerOf(fragment) == 0) {
        MoveFragmentAfter(fragment);
    } else {
        MoveFragmentBefore(fragment);
    }
    return std::bit_cast<bool>(ok);
}

// 0xb90cd0: saves a fragment the manager knows (the service's slot 16); the index learns its bin again (0xb8f420).
bool cTSBinCatalog::SaveFragment(void *fragment)
{
    EnsureLoaded(this);
    void *manager = Call3(PersistService());
    BinKey key;
    if (!KeyTaken(manager, KeyOf(fragment, &key))) return false;
    const uint8_t saved = Save(PersistService(), fragment, nullptr);
    if (saved && mIndexed) {
        OriginalFunction<void(__thiscall *)(cTSBinCatalog *, const BinKey *), 0xb8f420>()(this, BinKeyOf(fragment));
    }
    return std::bit_cast<bool>(saved);
}

// 0xb8c640: moves `fragment` `delta` places within its layer's run of `list` (sorted by layer): past the run's start
// (delta < 0) or end it goes to the run's edge (slots 25 / 24). Otherwise it takes the sort index of the entry `delta`
// away, and every entry from there to its old place shifts one index the other way, each saved (the service's slot
// 16) while the saves succeed. The run is sorted again (by layer and sort index) after.
bool cTSBinCatalog::MoveFragment(BinList *list, void *fragment, int32_t delta)
{
    if (delta == 0) return true;
    BinRef *begin = list->data();
    auto [first, last] = std::equal_range(begin, begin + list->size(), fragment, FragmentLess{});
    BinRef *it = FindSameFragment(list, fragment);
    uint8_t ok;
    if (delta < 0) {
        if (delta < first - it) return MoveFragmentAfter(fragment);
        void *service = PersistService();
        BinRef *p = it + delta;
        SetFragmentIndex(fragment, FragmentIndexOf(p->Get()));
        ok = Save(service, fragment, nullptr);
        for (; p != it && ok; ++p) {
            void *entry = p->Get();
            SetFragmentIndex(entry, FragmentIndexOf(entry) + 1);
            ok = Save(service, entry, nullptr);
        }
    } else {
        if (delta > (last - it) - 1) return MoveFragmentBefore(fragment);
        void *service = PersistService();
        BinRef *p = it + delta;
        SetFragmentIndex(fragment, FragmentIndexOf(p->Get()));
        ok = Save(service, fragment, nullptr);
        for (; p != it && ok; --p) {
            void *entry = p->Get();
            SetFragmentIndex(entry, FragmentIndexOf(entry) - 1);
            ok = Save(service, entry, nullptr);
        }
    }
    std::sort(first, last, FragmentSortLess{});
    return std::bit_cast<bool>(ok);
}

// 0xb8c7f0: MoveFragment within the fragments of `fragment`'s bin: the index's list when kept, else the bin's fragments
// (the bin got from the service's slot 5, its fragments from slot 32). False when the bin is not found.
bool cTSBinCatalog::MoveFragmentBy(void *fragment, int32_t delta)
{
    if (delta == 0) return true;
    if (mIndexed) return MoveFragment(&mIndex[*BinKeyOf(fragment)], fragment, delta);
    BinRef bin;
    const BinKey *key = BinKeyOf(fragment);
    if (!VirtualCall<bool(__thiscall *)(void *, const BinKey *, uint32_t, void **, uint32_t, uint32_t), 5>(
            PersistService(), key, kBinIid, bin.AsPPVoid(), 0, 0)) {
        return false;
    }
    BinList fragments;
    GetBinFragments(bin.Get(), &fragments, nullptr);
    return MoveFragment(&fragments, fragment, delta);
}

namespace {

// Slots 24 / 25: `fragment` placed one index past (`step` +1) or before (-1) its neighbour (0xb8b340 / 0xb8b770 find
// it), the direction flipped for layer 0; nothing when it is already there or is that neighbour.
bool StepFragment(cTSBinCatalog *self, void *fragment, int32_t step, uint32_t finder)
{
    BinRef neighbour;
    using Finder = bool(__thiscall *)(cTSBinCatalog *, void *, void **);
    const bool found = finder == 0xb8b340
        ? OriginalFunction<Finder, 0xb8b340>()(self, fragment, neighbour.AsPPVoid())
        : OriginalFunction<Finder, 0xb8b770>()(self, fragment, neighbour.AsPPVoid());
    if (!found) return false;
    const int32_t own = FragmentIndexOf(fragment);
    const int32_t other = FragmentIndexOf(neighbour.Get());
    const int32_t direction = LayerOf(fragment) != 0 ? step : -step;
    const bool clear = direction > 0 ? own < other : own > other;
    if (!clear) {
        BinKey a;
        BinKey b;
        const BinKey *theirs = KeyOf(neighbour.Get(), &a);
        if (SameKey(KeyOf(fragment, &b), theirs)) return true;
    }
    const int32_t target = other + direction;
    if (own == target) return true;
    SetFragmentIndex(fragment, target);
    return self->SaveFragment(fragment);
}

} // namespace

// 0xb8c960
bool cTSBinCatalog::MoveFragmentBefore(void *fragment) { return StepFragment(this, fragment, 1, 0xb8b340); }

// 0xb8caa0
bool cTSBinCatalog::MoveFragmentAfter(void *fragment) { return StepFragment(this, fragment, -1, 0xb8b770); }

// 0xb89820: takes `fragment` out of the source holding its key (its slots 26 and 10; 0xb8feb0 told), the manager
// forgetting the key; refused without a source or when its bin is locked (the bin's slot 27 has bit 0x20). The index
// drops it (0xb90210) when that worked.
bool cTSBinCatalog::RemoveFragment(void *fragment)
{
    EnsureLoaded(this);
    void *manager = Call3(PersistService());
    BinKey key;
    KeyOf(fragment, &key);
    void *source = nullptr;
    SourceOf(manager, &key, &source);
    if (!source) return false;
    if (void *bin = FindBin(BinKeyOf(fragment))) {
        if (ByteAt<27>(bin) & 0x20) return false;
    }
    const bool removed = SourceRemove(source, &key) && SourceTouch(source);
    SourceDropped(source, &key);
    ForgetKey(manager, &key);
    if (mIndexed && removed) OriginalFunction<void(__thiscall *)(cTSBinCatalog *, void *), 0xb90210>()(this, fragment);
    return removed;
}

// 0xb897e0: removes the fragment filing `outfit` in `bin`; false when there is none.
bool cTSBinCatalog::RemoveFragmentFor(const BinKey *outfit, const BinKey *bin)
{
    void *fragment = FindFragment(this, outfit, bin);
    return fragment && RemoveFragment(fragment);
}

// 0xb8b540: the number of fragments in `bin`: the index's list when kept (the order sorted first when dirty), else
// gathered (0xb8ba90).
uint32_t cTSBinCatalog::FragmentCount(void *bin)
{
    if (!mIndexed) {
        BinList fragments;
        OriginalFunction<GatherFn, 0xb8ba90>()(this, &fragments, bin, nullptr, false);
        return static_cast<uint32_t>(fragments.size());
    }
    EnsureLoaded(this);
    if (mDirty) SortBins();
    BinKey key;
    return static_cast<uint32_t>(mIndex[*KeyOf(bin, &key)].size());
}

// 0xb8b640: `bin`'s fragments `filter` takes: from the index when kept (0xb8b4a0), else gathered (0xb8ba90).
void cTSBinCatalog::GetFragments(BinList *out, void *bin, void *filter, bool append)
{
    if (mIndexed) {
        OriginalFunction<GatherFn, 0xb8b4a0>()(this, out, bin, filter, append);
    } else {
        OriginalFunction<GatherFn, 0xb8ba90>()(this, out, bin, filter, append);
    }
}

// 0xb8c010: every fragment `filter` takes, in display order (sort mode 1: 0xb8b6c0; else 0xb8b660).
void cTSBinCatalog::GetAllFragments(BinList *out, void *filter, uint32_t a3)
{
    using Fn = void(__thiscall *)(cTSBinCatalog *, BinList *, void *, uint32_t);
    if (mSortMode == 1) {
        OriginalFunction<Fn, 0xb8b6c0>()(this, out, filter, a3);
    } else {
        OriginalFunction<Fn, 0xb8b660>()(this, out, filter, a3);
    }
}

// 0xb8bfc0: GetAllFragments through a bin type filter (cTSBinTypeFilter, made by 0xb644a0); type -1 takes all.
void cTSBinCatalog::GetFragmentsOfType(BinList *out, int32_t type, uint32_t a3)
{
    if (type == -1) {
        GetAllFragments(out, nullptr, a3);
        return;
    }
    BinTypeFilter filter;
    OriginalFunction<BinTypeFilter *(__thiscall *)(BinTypeFilter *, int32_t), 0xb644a0>()(&filter, type);
    GetAllFragments(out, &filter, a3);
}

// 0xb8c030: `bin`'s fragments `filter` takes (0xb8bf50).
void cTSBinCatalog::GetBinFragments(void *bin, BinList *out, void *filter)
{
    OriginalFunction<void(__thiscall *)(cTSBinCatalog *, BinList *, void *, void *, uint32_t), 0xb8bf50>()(this, out, bin,
                                                                                                           filter, 0);
}

// 0xb90a50: whether a fragment files `outfit` in `bin`.
bool cTSBinCatalog::HasFragmentFor(const BinKey *outfit, const BinKey *bin) { return FindFragment(this, outfit, bin) != nullptr; }

// 0xb90ad0: whether `bin` has a fragment `filter` takes.
bool cTSBinCatalog::BinHasFragment(void *filter, void *bin)
{
    BinList fragments;
    GetFragments(&fragments, bin, filter, false);
    return !fragments.empty();
}

// 0xb90a70: whether a bin of the display order with a type in `mask` (-1: any) has a fragment `filter` takes.
bool cTSBinCatalog::AnyBinHasFragment(void *filter, uint32_t mask)
{
    for (auto &bin : mOrder) {
        if (mask != 0xffffffffu && (mask & WordAt<12>(bin.Get())) == 0) continue;
        if (BinHasFragment(filter, bin.Get())) return true;
    }
    return false;
}

// 0xb89a20: keeps the fragment index or drops it; turned on after loading, it is built (0xb8f3d0).
void cTSBinCatalog::SetIndexed(bool indexed)
{
    if (mIndexed == indexed) return;
    mIndexed = indexed;
    if (!indexed) {
        ClearIndex();
    } else if (mLoaded) {
        RebuildIndex(this);
    }
}

// 0xb907b0: sort mode 0 or 1; the positions are brought up to date (0xb90b80) unless the order is dirty.
void cTSBinCatalog::SetSortMode(uint32_t mode)
{
    if (mSortMode == mode || mode >= 2) return;
    mSortMode = mode;
    if (!mDirty) UpdatePositions(this);
}

// 0xb8aee0: renumbers (0xb8adc0) the fragments of layer `layer` (found with a probe fragment): in `bin` (from the index,
// or gathered by slot 32), or in every indexed bin (false when not indexed).
bool cTSBinCatalog::RenumberLayer(uint32_t layer, void *bin)
{
    void *memory = ::operator new(kFragmentSize);
    void *probe = memory ? OriginalFunction<void *(__thiscall *)(void *), 0xb86ea0>()(memory) : nullptr;
    BinRef held(static_cast<cIGZUnknown *>(probe));
    VirtualCall<void(__thiscall *)(void *, uint32_t), 26>(probe, layer);
    bool ok = true;
    if (bin) {
        if (mIndexed) {
            BinKey key;
            BinList &fragments = mIndex[*KeyOf(bin, &key)];
            auto [first, last] = std::equal_range(fragments.data(), fragments.data() + fragments.size(), probe,
                                                  FragmentLess{});
            ok = RenumberRange(this, first, last);
        } else {
            BinList fragments;
            GetBinFragments(bin, &fragments, nullptr);
            auto [first, last] = std::equal_range(fragments.data(), fragments.data() + fragments.size(), probe,
                                                  FragmentLess{});
            ok = RenumberRange(this, first, last);
        }
    } else if (mIndexed) {
        for (auto &[key, fragments] : mIndex) {
            auto [first, last] = std::equal_range(fragments.data(), fragments.data() + fragments.size(), probe,
                                                  FragmentLess{});
            ok &= RenumberRange(this, first, last);
        }
    } else {
        ok = false;
    }
    return ok;
}

// 0xb8b0c0: renumbers (0xb8adc0) all the fragments of `bin` (the index's list, or slot 32's), or of every bin (each
// indexed list, or slot 30's list of all).
bool cTSBinCatalog::RenumberFragments(void *bin)
{
    if (bin) {
        if (mIndexed) {
            BinKey key;
            BinList &fragments = mIndex[*KeyOf(bin, &key)];
            return RenumberRange(this, fragments.data(), fragments.data() + fragments.size());
        }
        BinList fragments;
        GetBinFragments(bin, &fragments, nullptr);
        return RenumberRange(this, fragments.data(), fragments.data() + fragments.size());
    }
    if (mIndexed) {
        bool ok = true;
        for (auto &[key, fragments] : mIndex) ok &= RenumberRange(this, fragments.data(), fragments.data() + fragments.size());
        return ok;
    }
    BinList fragments;
    GetAllFragments(&fragments, nullptr, 0);
    return RenumberRange(this, fragments.data(), fragments.data() + fragments.size());
}

namespace {

// Slot 40's answer from a bin's fragments: one past the larger sort index of the first and the last (a null one counts
// 0); 0 for none.
int32_t NextIndexOf(const BinList &fragments)
{
    if (fragments.empty()) return 0;
    int32_t top = 0;
    if (void *front = fragments.front().Get()) top = FragmentIndexOf(front);
    if (void *back = fragments.back().Get()) top = std::max(top, FragmentIndexOf(back));
    return top + 1;
}

} // namespace

// 0xb8b8d0: the next sort index for a fragment of the bin keyed `bin` (from the index when kept; else the bin from the
// service's slot 5 and its fragments from slot 32); 0 when the bin is unknown or has none.
int32_t cTSBinCatalog::NextFragmentIndex(const BinKey *bin)
{
    if (mIndexed) {
        auto it = mIndex.find(*bin);
        if (it == mIndex.end()) return 0;
        return NextIndexOf(it->second);
    }
    BinRef held;
    if (!VirtualCall<bool(__thiscall *)(void *, const BinKey *, uint32_t, void **, uint32_t, uint32_t), 5>(
            PersistService(), bin, kBinIid, held.AsPPVoid(), 0, 0)) {
        return 0;
    }
    BinList fragments;
    GetBinFragments(held.Get(), &fragments, nullptr);
    return NextIndexOf(fragments);
}

// ---- pending objects ----

// 0xb89350: the queue and the objects emptied.
void cTSBinCatalog::ClearPending()
{
    mPending.clear();
    mPendingObjects.clear();
}

// 0xb89380: hands out the object pending under `id` (counted; null and false when none) and forgets it. A queue entry
// still working is finished first when `wait` (the global 0x107c3e0 cleared meanwhile, then set to 9) and leaves the
// queue; without `wait` it leaves unfinished and the answer is false. A finished entry stays queued.
bool cTSBinCatalog::TakePending(uint32_t id, void **out, bool wait)
{
    *out = nullptr;
    auto object = mPendingObjects.find(id);
    if (object == mPendingObjects.end()) return false;
    *out = object->second.Get();
    Grab(*out);
    auto entry = std::find_if(mPending.begin(), mPending.end(), [id](const PendingEntry &e) { return e.id == id; });
    if (entry == mPending.end() || entry->state == 0xffffffffu) {
        mPendingObjects.erase(object);
        return true;
    }
    bool done = false;
    if (wait) {
        uint32_t &busy = *GameAddress<uint32_t>(0x107c3e0);
        busy = 0;
        do {
            WorkPending(this, &*entry);
        } while (entry->state != 0xffffffffu);
        busy = 9;
        done = true;
    }
    mPending.erase(entry);
    mPendingObjects.erase(object);
    return done;
}

// 0xb8bbe0: hands out the object pending under `id` (counted; false when none) once its queue entry is finished
// (worked to the end when `wait`; else false, the object still handed out); the entry leaves the queue, the object stays.
bool cTSBinCatalog::TakeFinished(uint32_t id, void **out, bool wait)
{
    *out = nullptr;
    auto object = mPendingObjects.find(id);
    if (object == mPendingObjects.end()) return false;
    *out = object->second.Get();
    Grab(*out);
    auto entry = std::find_if(mPending.begin(), mPending.end(), [id](const PendingEntry &e) { return e.id == id; });
    if (entry == mPending.end()) return true;
    if (entry->state != 0xffffffffu) {
        if (!wait) return false;
        do {
            WorkPending(this, &*entry);
        } while (entry->state != 0xffffffffu);
    }
    mPending.erase(entry);
    return true;
}

// 0xb8d530: whether `id`'s queue entry is finished; without one, whether an object is pending under it.
bool cTSBinCatalog::IsPendingDone(uint32_t id)
{
    for (auto &entry : mPending) {
        if (entry.id == id) return entry.state == 0xffffffffu;
    }
    return mPendingObjects.find(id) != mPendingObjects.end();
}

// 0xb88860: queues `object` under `id` at step `step` (the state's second byte). The same object already pending
// changes nothing; another one is replaced, its first queue entry dropped.
void cTSBinCatalog::QueuePending(uint32_t id, void *object, uint32_t step)
{
    auto it = mPendingObjects.find(id);
    if (it != mPendingObjects.end()) {
        if (it->second.Get() == object) return;
        auto entry = std::find_if(mPending.begin(), mPending.end(), [id](const PendingEntry &e) { return e.id == id; });
        if (entry != mPending.end()) mPending.erase(entry);
    }
    mPendingObjects[id].Assign(static_cast<cIGZUnknown *>(object));
    mPending.push_back({id, step << 8});
}

// 0xb89910: removes an outfit (type 0xac598eac): first every fragment filing it (listed by 0xb8aba0; all must go),
// then its key from the source holding it (its slots 26 and 10; 0xb8feb0 told), the manager forgetting the key.
bool cTSBinCatalog::RemoveOutfit(const BinKey *key)
{
    if (key->type != kOutfitType) return false;
    BinList fragments;
    OriginalFunction<void(__thiscall *)(cTSBinCatalog *, const BinKey *, BinList *), 0xb8aba0>()(this, key, &fragments);
    for (auto &fragment : fragments) {
        if (!RemoveFragment(fragment.Get())) return false;
    }
    void *manager = Call3(PersistService());
    void *source = nullptr;
    SourceOf(manager, key, &source);
    if (!source || !SourceRemove(source, key) || !SourceTouch(source)) return false;
    SourceDropped(source, key);
    ForgetKey(manager, key);
    return true;
}

} // namespace nTSCASCore
