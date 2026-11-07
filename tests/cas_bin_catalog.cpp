// Differential test for nTSCASCore::cTSBinCatalog (lane JG; src/cas/cTSBinCatalog.h): the class in the standalone shape.
//
// BinCatalog_Run: a catalog built by the original constructor (0xb86c50, game vtables 0xe8ae0c / 0xe8ae44 / 0xe8ae60) or
// the original creator (0xb8b270) against one built by ours (the constructor hook's adapter, or Create), each filled with
// the same state (written through our layout on both sides: the maps and vectors are MSVC 14.40 containers like the
// game's) and driven through its own vtables by the same random sequence of operations, then destroyed by the last
// Release (cRZUnknownMT's Release through the +4 face's slot 3: the thunk 0xb87771, the deleting destructor 0xb87790, the
// destructor 0xb87060). Operations (every slot of every face): QueryInterface (0xb8dd00; through the +4 and +0x1c faces
// the thunks 0xb8dd31 / 0xb8dd39) / AddRef / Release, Init (0xb8c090; the factory's last Release seen at the game's
// operator delete), Shutdown (folded 0xb82190), OnTick (0xb8d510, with its queue worker 0xb8dc50), and the
// cITSBinCatalog slots 3..46: LoadSources 0xb8d5a0, AddSource 0xb88600, RemoveSource 0xb903e0, HasSource 0xb8c1f0,
// GetSourceNames 0xb8bd00, AddBin 0xb877c0, SaveBins 0xb906c0, ClearAll 0xb8fdc0, RemoveBin 0xb89630, BinCount 0xb8d4e0,
// GetBinsOfType 0xb8be30, GetBins 0xb8beb0, SortBins 0xb90940, MoveBin 0xb8c600 (IndexOfBin 0xb8ad20), MoveBinAt
// 0xb8c240, RenumberBins 0xb89a50, FindBinType 0xb8c050, AddFragment 0xb87bd0, SaveFragment 0xb90cd0, MoveFragment
// 0xb8c640 (0xb8aaf0), MoveFragmentBy 0xb8c7f0, MoveFragmentBefore 0xb8c960, MoveFragmentAfter 0xb8caa0, RemoveFragment
// 0xb89820 (FindBin 0xb8a850), RemoveFragmentFor 0xb897e0, FragmentCount 0xb8b540, GetFragments 0xb8b640,
// GetAllFragments 0xb8c010, GetFragmentsOfType 0xb8bfc0, GetBinFragments 0xb8c030, HasFragmentFor 0xb90a50,
// BinHasFragment 0xb90ad0, AnyBinHasFragment 0xb90a70, SetIndexed 0xb89a20 (the index cleared: 0xb89320), SetSortMode
// 0xb907b0, RenumberLayer 0xb8aee0, RenumberFragments 0xb8b0c0, NextFragmentIndex 0xb8b8d0, ClearPending 0xb89350,
// TakePending 0xb89380, TakeFinished 0xb8bbe0, IsPendingDone 0xb8d530, QueuePending 0xb88860, RemoveOutfit 0xb89910.
//
// Every object the catalog reaches is a recording fake (tests/ts_services_fixture.h): sources, bins, fragments, bin
// types, pending objects, filters, the persist service (cells 0x10fc684 and 0x10faf6c), its manager (slot 3) and the
// framework. The bins' sort index (slot 22) and the fragments' layer (slot 27) answer without logging: the sorts and
// searches (library code: std::sort, std::equal_range, std::lower_bound) may read them in another order. Stood in for on
// both sides (the image's functions our code still calls through OriginalFunction): EnsureLoaded 0xb88e10, LoadSource
// 0xb8d6b0, the index's keepers (0xb8f3d0, 0xb87a40, 0xb90150, 0xb87eb0, 0xb90210, 0xb8f420, 0xb87f70, 0xb903a0), the
// gatherers (0xb8ba90, 0xb8b4a0, 0xb8b660, 0xb8b6c0, 0xb8bf50, 0xb8aba0), the neighbour finders 0xb8b340 / 0xb8b770, the
// fragment finder 0xb8a8d0, the renumbering 0xb8adc0, the positions 0xb90b80, the pending worker 0xb8cbe0 and the global
// 0xb8feb0; each logs its arguments (the catalog as an offset into it) and returns a drawn value. The real cTSSimFragment
// constructor 0xb86ea0 (RenumberLayer's probe) and cTSBinTypeFilter constructor 0xb644a0 run on both sides. The game's
// operator new / delete (0x824632 / 0x824624) are ours, so nodes either side allocates may be freed by the other's code.
//
// Compared after every operation: the result (al where the slot returns a bool), the event log, every field (maps in
// order with their keys and values, vectors with their capacity), the out-parameters, and every fake's count and words;
// at the end the counts after the destruction.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "cas/cTSBinCatalog.h"
#include "game/game.h"
#include "rz/cRZString.h"

#include <cstring>
#include <iterator>
#include <new>
#include <random>
#include <vector>

namespace bincat_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{4, "d"}, {10, "ddddd"}, {11, "ddddd"}, {12, "ddddd"}, {13, "ddddd"}, {14, "ddd-dd"}, {15, "ddd-dd"}, {16, "ddd-dd"}, {17, "ddd-dd"}};

using namespace nTSCASCore;
using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSBinCatalog;
using KeyList = std::vector<BinKey, GZServiceAllocator<BinKey>>;

constexpr uint32_t kTag = 0x7d000000;    // the catalog
constexpr uint32_t kKeyTag = 0x6b000000; // the key pool

enum : unsigned {
    kFw = 0, kSvc, kMgr, kGrouped, kFilter, kCustom,
    kSrc0 = 6, kSrc1, kSrc2, kSrcNew,  // 6..9
    kBin0 = 10,                        // 10..13
    kFrag0 = 14,                       // 14..17
    kType0 = 18, kType1,               // 18..19
    kPend0 = 20, kPend1,               // 20..21
};
constexpr unsigned kBins = 4, kFrags = 4;

BinKey gKeys[8];
std::mt19937 gDraw;
uint32_t Draw(uint32_t n) { return uint32_t(gDraw() % n); }
cIGZUnknown *U(unsigned i) { return reinterpret_cast<cIGZUnknown *>(&F(i)); }
Sys *gCatalog;

void LogKeyAt(uint32_t at)
{
    const auto *k = reinterpret_cast<const BinKey *>(uintptr_t(at));
    Ev(k->type);
    Ev(k->group);
    Ev(k->instance);
    Ev(k->resource);
}
uint32_t Self(const void *p) { return svcfx::NormOut(P(p)); }

// ---- fake slots ----

uint32_t DrawBool(Obj *, uint32_t *) { return 0x100u | Draw(2); }
uint32_t LogKeyBool(Obj *, uint32_t *a)
{
    LogKeyAt(a[0]);
    return 0x100u | Draw(2);
}
uint32_t LogKeyVoid(Obj *, uint32_t *a)
{
    LogKeyAt(a[0]);
    return 0;
}
uint32_t InstanceTaken(Obj *, uint32_t *a) // the manager's slot 15
{
    LogKeyAt(a[0]);
    return Draw(3) == 0;
}
uint32_t SourceOfKey(Obj *, uint32_t *a) // the manager's slot 34: (key, &source)
{
    LogKeyAt(a[0]);
    static const unsigned kFound[] = {~0u, kSrc1, kSrcNew, kSrc0};
    const unsigned which = kFound[Draw(4)];
    if (which != ~0u) *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(which);
    return 0;
}
uint32_t ServiceFindBin(Obj *, uint32_t *a) // the service's slot 5: (key, iid, &bin, 0, 0)
{
    LogKeyAt(a[0]);
    if (Draw(4) == 0) return 0x200;
    const unsigned bin = kBin0 + Draw(kBins);
    ++F(bin).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(bin);
    return 1;
}
uint32_t KeyInto(Obj *self, uint32_t *a) // slot 3: the key into *a[0]
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), self->value, sizeof(BinKey));
    return a[0];
}
uint32_t KeyFrom(Obj *self, uint32_t *a) // slot 4: the key taken
{
    LogKeyAt(a[0]);
    std::memcpy(self->value, reinterpret_cast<const void *>(uintptr_t(a[0])), sizeof(BinKey));
    return 0;
}
uint32_t SetValue4(Obj *self, uint32_t *a)
{
    self->value[4] = a[0];
    return 0;
}
uint32_t GetValue4(Obj *self, uint32_t *) { return self->value[4]; }
uint32_t SetValue5(Obj *self, uint32_t *a)
{
    self->value[5] = a[0];
    return 0;
}
uint32_t __fastcall QuietIndex(Obj *self, void *) { return self->value[4]; }
uint32_t __fastcall QuietLayer(Obj *self, void *) { return self->value[5] & 0xffff; }
uint32_t __fastcall QuietAddRef(Obj *self, void *) { return uint32_t(++self->refs); }
uint32_t __fastcall QuietRelease(Obj *self, void *) { return uint32_t(--self->refs); }
uint32_t SourceQuery(Obj *, uint32_t *a)
{
    if (Draw(3) == 0) return 0x200;
    ++F(kGrouped).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(kGrouped);
    return 1;
}
uint32_t SourceName(Obj *self, uint32_t *a)
{
    char text[16];
    std::snprintf(text, sizeof(text), "src%u", self->id);
    reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString.assign(text);
    return 1;
}
// A source's slot 19: (keys, filter): its keys of bins (groups the catalog has, mostly), the count answered. Logged
// like a generic slot, but the filter (a heap block) by its contents.
uint32_t __fastcall SourceKeys(Obj *self, void *, KeyList *keys, const uint32_t *filter)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(19);
    Ev(2);
    Ev(Self(keys));
    Ev(filter ? filter[0] - P(GameAddress<void>(0)) : 0);
    Ev(filter ? filter[1] : 0);
    Ev(filter ? filter[2] : 0);
    static const uint32_t kGroups[] = {1, 2, 0x4f184aa9, 7};
    const unsigned n = Draw(4);
    for (unsigned i = 0; i < n; ++i) keys->push_back({0x6c4f359d, kGroups[Draw(4)], 1 + Draw(3), 0});
    if (n && Draw(3) == 0) return n - 1;
    return Draw(5) == 0 ? 0 : n;
}
uint32_t FilterTakes(Obj *self, uint32_t *a) { return 0x200u | (((svcfx::Norm(a[0]) * 2654435761u + self->value[0]) >> 9) & 1); }

// The manager's slot 20, (kind, type, factory): logged like a generic slot, but the factory (a heap block) by its
// contents and whether it is the one the previous call got.
const void *gLastFactory;
const void *gFactory;
void __cdecl GameDelete(void *p, size_t size)
{
    if (p && p == gFactory) {
        Ev(0xdead0000u | uint32_t(size));
        gFactory = nullptr;
    }
    ::operator delete(p);
}
uint32_t __fastcall RegisterFactory(Obj *self, void *, uint32_t kind, uint32_t type, const uint32_t *factory)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(20);
    Ev(3);
    Ev(kind);
    Ev(type);
    Ev(factory == gLastFactory);
    gLastFactory = factory;
    gFactory = factory;
    Ev(factory[0] - P(GameAddress<void>(0)));
    Ev(factory[1] - P(GameAddress<void>(0)));
    Ev(factory[2]);
    return 1;
}

// ---- stand-ins ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }

void LogList(const BinList *list)
{
    if (!list) {
        Ev(0xeeee);
        return;
    }
    Ev(uint32_t(list->size()));
    for (const BinRef &ref : *list) Ev(Self(ref.Get()));
}
void Fill(BinList *out)
{
    const unsigned n = Draw(4);
    for (unsigned i = 0; i < n; ++i) out->push_back(BinRef(U(kFrag0 + Draw(kFrags))));
}

void __fastcall StandEnsureLoaded(void *self, void *)
{
    Ev(0xb88e10);
    Ev(Self(self));
}
uint32_t __fastcall StandLoadSource(void *self, void *, void *source, uint32_t flag)
{
    Ev(0xb8d6b0);
    Ev(Self(self));
    Ev(Self(source));
    Ev(flag);
    ++*reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 0x24);
    return 0x100u | Draw(2);
}
template <uint32_t Rva> void __fastcall StandThis(void *self, void *)
{
    Ev(Rva);
    Ev(Self(self));
}
template <uint32_t Rva> void __fastcall StandThisObject(void *self, void *, void *object)
{
    Ev(Rva);
    Ev(Self(self));
    Ev(Self(object));
}
void __fastcall StandIndexBin(void *self, void *, const BinKey *key) // 0xb8f420
{
    Ev(0xb8f420);
    Ev(Self(self));
    LogKeyAt(P(key));
}
// 0xb8cbe0: one step of the entry's work: done now and then (always by the fourth step), else the step moves on or not.
uint32_t gWorkCalls;
void __fastcall StandWork(void *self, void *, PendingEntry *entry)
{
    Ev(0xb8cbe0);
    Ev(Self(self));
    Ev(entry->id);
    Ev(entry->state);
    Ev(*GameAddress<uint32_t>(0x107c3e0));
    if (++gWorkCalls % 4 == 0 || Draw(3) == 0) {
        entry->state = 0xffffffffu;
    } else if (Draw(2)) {
        entry->state += 1;
    }
}
void *__fastcall StandFindFragment(void *self, void *, const BinKey *outfit, const BinKey *bin) // 0xb8a8d0
{
    Ev(0xb8a8d0);
    Ev(Self(self));
    LogKeyAt(P(outfit));
    LogKeyAt(P(bin));
    return Draw(3) == 0 ? nullptr : &F(kFrag0 + Draw(kFrags));
}
uint32_t __fastcall StandRenumber(void *self, void *, BinRef *first, BinRef *last) // 0xb8adc0
{
    Ev(0xb8adc0);
    Ev(Self(self));
    Ev(uint32_t(last - first));
    for (BinRef *it = first; it != last; ++it) Ev(Self(it->Get()));
    return 0x100u | Draw(2);
}
uint32_t __cdecl StandForget(void *source, const BinKey *key) // 0xb8feb0
{
    Ev(0xb8feb0);
    Ev(Self(source));
    LogKeyAt(P(key));
    return 0x700u | Draw(2);
}
void __cdecl StandSourceFragments(void *source, BinList *out, uint32_t a, uint32_t b) // 0xb87f70
{
    Ev(0xb87f70);
    Ev(Self(source));
    LogList(out);
    Ev(a);
    Ev(b);
    Fill(out);
}
void __fastcall StandDropFragments(void *self, void *, BinList *list) // 0xb903a0
{
    Ev(0xb903a0);
    Ev(Self(self));
    LogList(list);
}
template <uint32_t Rva> void __fastcall StandGather(void *self, void *, BinList *out, void *bin, void *filter, uint32_t append)
{
    Ev(Rva);
    Ev(Self(self));
    LogList(out);
    Ev(Self(bin));
    Ev(Self(filter));
    Ev(append);
    Fill(out);
}
template <uint32_t Rva> void __fastcall StandGatherAll(void *self, void *, BinList *out, const uint32_t *filter, uint32_t a3)
{
    Ev(Rva);
    Ev(Self(self));
    LogList(out);
    // A stack cTSBinTypeFilter (slot 31): its vtable and type; a fake by its id.
    const uint32_t f = Self(filter);
    Ev(f);
    if (f == 0x5ac00000) {
        Ev(filter[0] - P(GameAddress<void>(0)));
        Ev(filter[2]);
    }
    Ev(a3);
    Fill(out);
}
void __fastcall StandOutfitFragments(void *self, void *, const BinKey *key, BinList *out) // 0xb8aba0
{
    Ev(0xb8aba0);
    Ev(Self(self));
    LogKeyAt(P(key));
    LogList(out);
    Fill(out);
}
template <uint32_t Rva> uint32_t __fastcall StandNeighbour(void *self, void *, void *fragment, void **out)
{
    Ev(Rva);
    Ev(Self(self));
    Ev(Self(fragment));
    Ev(Self(*out));
    if (Draw(4) == 0) return 0x200;
    const unsigned n = kFrag0 + Draw(kFrags);
    ++F(n).refs;
    *out = &F(n);
    return 1;
}

// ---- the world for a seed ----

void ConfigureFakes(uint32_t seed)
{
    std::mt19937 r(seed * 7 + 3);
    svcfx::ResetAll();
    svcfx::AddRange(gKeys, sizeof(gKeys), kKeyTag);
    F(kFw).Set(9, "p", 1u);
    F(kSvc).Set(3, 0, &F(kMgr));
    F(kSvc).Set(5, "pdp--", 0, &ServiceFindBin);
    F(kSvc).Set(16, "pp", 0, &DrawBool);
    F(kMgr).Set(8, "p", 0u);
    F(kMgr).Set(10, "p", 0, &LogKeyVoid);
    F(kMgr).Set(13, "pp", 0, &LogKeyBool);
    F(kMgr).Set(15, "p", 0, &InstanceTaken);
    F(kMgr).Set(34, "pp", 0, &SourceOfKey);
    F(kMgr).Set(20, 3, 0u);
    F(kMgr).table[20] = reinterpret_cast<void *>(&RegisterFactory);
    gLastFactory = nullptr;
    gFactory = nullptr;
    F(kGrouped).Set(4, 0, r() % 2 ? 0x4f184aa9 : r());
    F(kGrouped).Set(6, 0, 0, [](Obj *, uint32_t *) -> uint32_t {
        static const uint32_t kAnswers[] = {1, 0, 2, 0x101};
        return kAnswers[Draw(4)];
    });
    F(kFilter).value[0] = uint32_t(r());
    F(kFilter).Set(3, "p", 0, &FilterTakes);
    F(kCustom).Set(0, "dp", 0, &SourceQuery);
    for (unsigned s : {kSrc0, kSrc1, kSrc2, kSrcNew}) {
        F(s).Set(0, "dp", 0, &SourceQuery);
        F(s).Set(10, 0, 0, &DrawBool);
        F(s).Set(11, "p", 0, &SourceName);
        F(s).Set(15, 0, r() % 3);
        F(s).Set(19, 2, 0u);
        F(s).table[19] = reinterpret_cast<void *>(&SourceKeys);
        F(s).Set(20, "p", 0, &LogKeyBool);
        F(s).Set(26, "p", 0, &LogKeyBool);
    }
    static const uint32_t kGroups[] = {0, 1, 2, 0x4f184aa9};
    for (unsigned i = 0; i < kBins; ++i) {
        Obj &o = F(kBin0 + i);
        const BinKey key = {r() % 4 ? 0x6c4f359du : 0x1234u, kGroups[r() % 4], r() % 4, uint32_t(r() % 2)};
        std::memcpy(o.value, &key, sizeof(key));
        o.value[4] = uint32_t(int32_t(r() % 9) - 2); // sort index
        o.Set(3, "p", 0, &KeyInto);
        o.Set(4, "p", 0, &KeyFrom);
        o.Set(12, 0, uint32_t(1u << (r() % 3)) | (r() % 4 ? 0u : 8u));
        o.Set(22, 0, 0u);
        o.table[22] = reinterpret_cast<void *>(&QuietIndex);
        o.Set(23, "d", 0, &SetValue4);
        o.Set(27, 0, r() % 3 ? 0x1fd0u : 0x120u); // bit 0x20: locked (RemoveFragment)
    }
    static const uint32_t kLayers[] = {0, 1, 2, 3, 0xffff, 0x10000};
    for (unsigned i = 0; i < kFrags; ++i) {
        Obj &o = F(kFrag0 + i);
        const BinKey key = {r() % 3 ? 0x0c560f39u : 0x1234u, r() % 3 ? 1u + uint32_t(r() % 2) : 0u, r() % 3 ? 1u + uint32_t(r() % 3) : 0u, 0};
        std::memcpy(o.value, &key, sizeof(key));
        o.value[4] = uint32_t(int32_t(r() % 7) - 1);
        o.value[5] = kLayers[r() % 6];
        o.Set(3, "p", 0, &KeyInto);
        o.Set(4, "p", 0, &KeyFrom);
        o.Set(9, "d", 0u);
        o.Set(19, 0, &gKeys[r() % 8]);
        o.Set(21, 0, &gKeys[r() % 8]);
        o.Set(24, "d", 0, &SetValue4);
        o.Set(25, 0, 0, &GetValue4);
        o.Set(26, 1, 0, &SetValue5);
        o.Set(27, 0, 0u);
        o.table[27] = reinterpret_cast<void *>(&QuietLayer);
    }
    F(kType0).Set(12, 0, 1u);
    F(kType1).Set(12, 0, r() % 2 ? 2u : 1u);
}

bool gNullBins; // this seed files an empty bin slot (every seventh)

// The key pool: bins' and fragments' keys alike (types the catalog checks for, groups it has).
void MakeKeys(uint32_t seed)
{
    std::mt19937 r(seed * 5 + 11);
    static const uint32_t kTypes[] = {0x6c4f359d, 0x0c560f39, 0xac598eac, 0x12345678};
    static const uint32_t kGroups[] = {0, 1, 2, 0x4f184aa9};
    for (BinKey &k : gKeys) k = {kTypes[r() % 4], kGroups[r() % 4], r() % 4, uint32_t(r() % 2)};
}

// The catalog's state (the same on both sides: written through our layout).
void Populate(Sys *s, uint32_t seed)
{
    std::mt19937 r(seed * 13 + 1);
    s->mLoaded = r() % 2;
    s->mBinCount = r() % 5;
    s->mNextInstance = r() % 3;
    s->mDirty = r() % 2;
    s->mSourcesLoaded = r() % 2;
    s->mIndexed = r() % 2;
    s->mSortMode = r() % 3;
    const unsigned sources = r() % 4;
    for (unsigned i = 0; i < sources; ++i) s->mSources.push_back(BinRef(U(kSrc0 + r() % 3)));
    if (r() % 2) s->mCustomSource.Assign(U(kCustom));
    // Bins, mostly filed under their own key's group and instance.
    const unsigned bins = r() % 5;
    for (unsigned i = 0; i < bins; ++i) {
        const unsigned b = kBin0 + unsigned(r() % kBins);
        const auto *key = reinterpret_cast<const BinKey *>(F(b).value);
        const uint32_t group = r() % 4 ? key->group : 2;
        const uint32_t instance = r() % 4 ? key->instance : 3;
        s->mBins[group][instance].Assign(U(b));
    }
    // An empty slot now and then (SortBins and RemoveSource skip it; SaveBins would not, so that seed never saves).
    if (gNullBins) {
        const uint32_t group = r() % 2 ? 1u : 0x4f184aa9u;
        s->mBins[group][uint32_t(r() % 4)];
    }
    const unsigned order = r() % 5;
    for (unsigned i = 0; i < order; ++i) s->mOrder.push_back(BinRef(U(kBin0 + r() % kBins)));
    const unsigned types = r() % 3;
    for (unsigned i = 0; i < types; ++i) s->mBinTypes.push_back(BinRef(U(kType0 + r() % 2)));
    static const uint32_t kStates[] = {0xffffffff, 0x100, 0x201, 0};
    const unsigned pending = r() % 4;
    for (unsigned i = 0; i < pending; ++i) s->mPending.push_back({1 + uint32_t(r() % 4), kStates[r() % 4]});
    const unsigned objects = r() % 4;
    for (unsigned i = 0; i < objects; ++i) s->mPendingObjects[1 + uint32_t(r() % 4)].Assign(U(kPend0 + r() % 2));
    const unsigned words = r() % 3;
    for (unsigned i = 0; i < words; ++i) s->mIdWords[uint32_t(r() % 4)].assign(r() % 3, uint32_t(r()));
    const unsigned index = r() % 4;
    for (unsigned i = 0; i < index; ++i) {
        BinKey key = r() % 2 ? *reinterpret_cast<const BinKey *>(F(kBin0 + r() % kBins).value) : gKeys[r() % 8];
        BinList &list = s->mIndex[key];
        const unsigned n = r() % 4;
        for (unsigned k = 0; k < n; ++k) list.push_back(BinRef(U(kFrag0 + r() % kFrags)));
    }
    const unsigned positions = r() % 3;
    for (unsigned i = 0; i < positions; ++i) s->mPositions[gKeys[r() % 8]] = int32_t(r() % 5);
}

// ---- dumping ----

uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

template <class V> void DumpRefs(Blob &b, const V &v)
{
    b.push_back(uint32_t(v.size()));
    b.push_back(uint32_t(v.capacity()));
    for (const BinRef &ref : v) b.push_back(Self(ref.Get()));
}
void DumpKey(Blob &b, const BinKey &k) { b.insert(b.end(), {k.type, k.group, k.instance, k.resource}); }

void Dump(Blob &b, Sys *s, bool ours)
{
    static const uint32_t kFaces[3][2] = {{0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kCatalogVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(s, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 8; off < 0x18; off += 4) b.push_back(Word(s, off));
    b.push_back(Word(s, 0x18) & 0xff); // mRunning (its padding is the heap's under the creator)
    b.push_back(Word(s, 0x20));
    b.push_back(Word(s, 0x24));
    DumpRefs(b, s->mSources);
    b.push_back(Self(s->mCustomSource.Get()));
    b.push_back(Word(s, 0x38) & 0xffff);
    b.push_back(Word(s, 0x3c));
    DumpRefs(b, s->mOrder);
    b.push_back(uint32_t(s->mBins.size()));
    for (const auto &[group, bins] : s->mBins) {
        b.push_back(group);
        b.push_back(uint32_t(bins.size()));
        for (const auto &[instance, bin] : bins) b.insert(b.end(), {instance, Self(bin.Get())});
    }
    DumpRefs(b, s->mBinTypes);
    b.push_back(uint32_t(s->mPending.size()));
    b.push_back(uint32_t(s->mPending.capacity()));
    for (const PendingEntry &e : s->mPending) b.insert(b.end(), {e.id, e.state});
    b.push_back(uint32_t(s->mPendingObjects.size()));
    for (const auto &[id, object] : s->mPendingObjects) b.insert(b.end(), {id, Self(object.Get())});
    b.push_back(uint32_t(s->mIdWords.size()));
    for (const auto &[id, words] : s->mIdWords) {
        b.insert(b.end(), {id, uint32_t(words.size())});
        b.insert(b.end(), words.begin(), words.end());
    }
    b.push_back(uint32_t(s->mIndex.size()));
    for (const auto &[key, list] : s->mIndex) {
        DumpKey(b, key);
        DumpRefs(b, list);
    }
    b.push_back(Word(s, 0x84) & 0xff);
    b.push_back(Word(s, 0x88));
    b.push_back(uint32_t(s->mPositions.size()));
    for (const auto &[key, at] : s->mPositions) {
        DumpKey(b, key);
        b.push_back(uint32_t(at));
    }
}

// ---- driving a catalog ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}

bool gCreator;
Sys *Build(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gCreator) {
        void *made = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0xb8b270)();
        b.push_back(made != nullptr);
        return static_cast<Sys *>(made);
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(cTSBinCatalogAdapters::Construct(memory, nullptr))
                   : ctx.Original<void *(__thiscall *)(void *)>(0xb86c50)(memory);
    b.push_back(r == memory);
    return static_cast<Sys *>(memory);
}

const uint32_t kIids[] = {1, Sys::kIid, 0x287fb697 /* cIGZSystemService */, 0x12345678};

void *Source(std::mt19937 &r, bool allowNull = true)
{
    static const unsigned kSources[] = {~0u, kSrc0, kSrc1, kSrc2, kSrcNew};
    const unsigned which = kSources[allowNull ? r() % 5 : 1 + r() % 4];
    return which == ~0u ? nullptr : static_cast<void *>(&F(which));
}
void *Bin(std::mt19937 &r) { return &F(kBin0 + unsigned(r() % kBins)); }
void *Fragment(std::mt19937 &r) { return &F(kFrag0 + unsigned(r() % kFrags)); }
void *FilterArg(std::mt19937 &r) { return r() % 2 ? static_cast<void *>(&F(kFilter)) : nullptr; }
uint32_t Flag(std::mt19937 &r) { return uint32_t(r() % 2) | (r() % 3 ? 0u : 0xab00u); }

// A caller's list of bins (MoveBinAt, RenumberBins) or of fragments.
void FillList(BinList &list, std::mt19937 &r, unsigned base, unsigned kinds)
{
    const unsigned n = r() % 5;
    for (unsigned i = 0; i < n; ++i) list.push_back(BinRef(U(base + unsigned(r() % kinds))));
}

void RunOp(Blob &b, Sys *s, unsigned slot, std::mt19937 &r)
{
    void *const primary = s;
    void *const face = reinterpret_cast<uint8_t *>(s) + 0x1c;
    BinList out;
    if (r() % 3 == 0) out.push_back(BinRef(U(kFrag0 + r() % kFrags))); // a list the call appends to or clears
    switch (slot) {
    case 0: { // QueryInterface on a face (+0, the +4 thunk 0xb8dd31, the +0x1c thunk 0xb8dd39), the result released
        static const uint32_t kFaceAt[] = {0, 4, 0x1c};
        void *at = reinterpret_cast<uint8_t *>(s) + kFaceAt[r() % 3];
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(at, 0, kIids[r() % 4], &q) & 0xff;
        b.push_back(ok);
        b.push_back(Self(q));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 1: // Init
        gLastFactory = nullptr;
        b.push_back(Slot<uint32_t>(primary, 8) & 0xff);
        break;
    case 2: b.push_back(Slot<uint32_t>(primary, 9) & 0xff); break; // Shutdown
    case 47: b.push_back(Slot<uint32_t>(primary, 10, uint32_t(r())) & 0xff); break; // OnTick
    case 3: b.push_back(Slot<uint32_t>(face, 3)); break;
    case 4: b.push_back(Slot<uint32_t>(face, 4, Source(r), Flag(r)) & 0xff); break;
    case 5:
    case 6: b.push_back(Slot<uint32_t>(face, slot, Source(r)) & 0xff); break;
    case 7: {
        ContentServiceStrings names;
        if (r() % 2) names.emplace_back("old");
        Slot<void>(face, 7, &names);
        b.push_back(uint32_t(names.size()));
        for (const ContentString &n : names) {
            b.push_back(uint32_t(n.s.mString.size()));
            for (char c : n.s.mString) b.push_back(uint8_t(c));
        }
        break;
    }
    case 8: { // AddBin: a key with or without a group and an instance
        Obj &bin = *static_cast<Obj *>(Bin(r));
        if (r() % 3 == 0) bin.value[1] = 0;
        if (r() % 3 == 0) bin.value[2] = 0;
        b.push_back(Slot<uint32_t>(face, 8, static_cast<void *>(&bin), Source(r)) & 0xff);
        break;
    }
    case 9: b.push_back(Slot<uint32_t>(face, 9, uint32_t(r())) & 0xff); break;
    case 10: Slot<void>(face, 10); break;
    case 11: b.push_back(Slot<uint32_t>(face, 11, Bin(r)) & 0xff); break;
    case 28: b.push_back(Slot<uint32_t>(face, 28, Bin(r))); break;
    case 12: b.push_back(Slot<uint32_t>(face, 12)); break;
    case 13: {
        static const uint32_t kMasks[] = {0, 1, 2, 3, 8, 0xffffffff};
        Slot<void>(face, 13, &out, kMasks[r() % 6]);
        break;
    }
    case 14:
        if (r() % 8 == 0) {
            Slot<void>(face, 14, &s->mOrder, FilterArg(r));
        } else {
            Slot<void>(face, 14, &out, FilterArg(r));
        }
        break;
    case 15: Slot<void>(face, 15); break;
    case 16: { // MoveBin; a null bin sorts after every bin (not found)
        void *bin = r() % 8 ? Bin(r) : nullptr;
        b.push_back(Slot<uint32_t>(face, 16, bin, uint32_t(r() % 5)) & 0xff);
        break;
    }
    case 17: { // MoveBinAt: an index inside the list (past it the original throws)
        const bool own = r() % 2;
        BinList list;
        if (own) FillList(list, r, kBin0, kBins);
        const size_t n = own ? list.size() : s->mOrder.size();
        const uint32_t index = n < 2 ? uint32_t(r() % 3) : uint32_t(r() % n);
        b.push_back(Slot<uint32_t>(face, 17, index, uint32_t(r() % 5), own ? &list : nullptr));
        DumpRefs(b, list);
        break;
    }
    case 18: {
        // The display order: not when sorting it first would shorten it (both sides then throw out_of_range).
        size_t filed = 0;
        for (const auto &[group, bins] : s->mBins)
            for (const auto &[instance, bin] : bins) filed += bin.Get() != nullptr;
        const bool own = r() % 2 || (s->mDirty && filed < s->mOrder.size());
        BinList list;
        if (own) FillList(list, r, kBin0, kBins);
        b.push_back(Slot<uint32_t>(face, 18, own ? &list : nullptr) & 0xff);
        DumpRefs(b, list);
        break;
    }
    case 19: b.push_back(Self(Slot<void *>(face, 19, uint32_t(r() % 4)))); break;
    case 20: { // AddFragment: a key of the right type with a group and an instance, or not
        Obj &frag = *static_cast<Obj *>(Fragment(r));
        if (r() % 3 == 0) frag.value[0] = 0x0c560f39;
        if (r() % 4 == 0) frag.value[1] = 0;
        if (r() % 4 == 0) frag.value[2] = 0;
        b.push_back(Slot<uint32_t>(face, 20, static_cast<void *>(&frag), Source(r), Flag(r)) & 0xff);
        break;
    }
    case 21:
    case 24:
    case 25:
    case 26: b.push_back(Slot<uint32_t>(face, slot, Fragment(r)) & 0xff); break;
    case 22: { // MoveFragment within a list of fragments sorted by layer (or not), the fragment in it (or not)
        BinList list;
        FillList(list, r, kFrag0, kFrags);
        if (r() % 2) {
            std::sort(list.begin(), list.end(), [](const BinRef &a, const BinRef &c) {
                const uint16_t x = uint16_t(static_cast<Obj *>(static_cast<void *>(a.Get()))->value[5]);
                const uint16_t y = uint16_t(static_cast<Obj *>(static_cast<void *>(c.Get()))->value[5]);
                auto rank = [](uint16_t v) { return v == 0 ? -1 : v == 0xffff ? 0x10000 : 0xffff - v; };
                return rank(x) < rank(y);
            });
        }
        void *fragment = r() % 4 || list.empty() ? Fragment(r) : list[r() % list.size()].Get();
        b.push_back(Slot<uint32_t>(face, 22, &list, fragment, int32_t(r() % 7) - 3) & 0xff);
        DumpRefs(b, list);
        break;
    }
    case 23: b.push_back(Slot<uint32_t>(face, 23, Fragment(r), int32_t(r() % 7) - 3) & 0xff); break;
    case 27:
    case 33: b.push_back(Slot<uint32_t>(face, slot, &gKeys[r() % 8], &gKeys[r() % 8]) & 0xff); break;
    case 29: Slot<void>(face, 29, &out, Bin(r), FilterArg(r), Flag(r)); break;
    case 30: Slot<void>(face, 30, &out, FilterArg(r), uint32_t(r() % 3)); break;
    case 31: {
        static const int32_t kTypes[] = {-1, 0, 5, 0x1234};
        Slot<void>(face, 31, &out, kTypes[r() % 4], uint32_t(r() % 3));
        break;
    }
    case 32: Slot<void>(face, 32, Bin(r), &out, FilterArg(r)); break;
    case 34: b.push_back(Slot<uint32_t>(face, 34, FilterArg(r), Bin(r)) & 0xff); break;
    case 35: {
        static const uint32_t kMasks[] = {0xffffffff, 0, 1, 3, 8};
        b.push_back(Slot<uint32_t>(face, 35, FilterArg(r), kMasks[r() % 5]) & 0xff);
        break;
    }
    case 36: Slot<void>(face, 36, Flag(r)); break;
    case 37: Slot<void>(face, 37, uint32_t(r() % 4)); break;
    case 38: {
        static const uint32_t kLayers[] = {0, 1, 2, 3, 0xffff};
        b.push_back(Slot<uint32_t>(face, 38, kLayers[r() % 5], r() % 3 ? Bin(r) : nullptr) & 0xff);
        break;
    }
    case 39: b.push_back(Slot<uint32_t>(face, 39, r() % 3 ? Bin(r) : nullptr) & 0xff); break;
    case 40: {
        const BinKey *key = r() % 2 ? &gKeys[r() % 8] : reinterpret_cast<const BinKey *>(F(kBin0 + r() % kBins).value);
        b.push_back(Slot<uint32_t>(face, 40, key));
        break;
    }
    case 41: Slot<void>(face, 41); break;
    case 42:
    case 43: {
        void *got = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        b.push_back(Slot<uint32_t>(face, slot, uint32_t(r() % 5), &got, Flag(r)) & 0xff);
        b.push_back(Self(got));
        if (got) Slot<uint32_t>(got, 2);
        break;
    }
    case 44: b.push_back(Slot<uint32_t>(face, 44, uint32_t(r() % 5)) & 0xff); break;
    case 45: {
        // Never a null object: TakePending / TakeFinished count what they hand out unchecked.
        Slot<void>(face, 45, uint32_t(r() % 5), static_cast<void *>(&F(kPend0 + r() % 2)), uint32_t(r() % 3));
        break;
    }
    case 46: {
        BinKey key = gKeys[r() % 8];
        if (r() % 2) key.type = 0xac598eac;
        b.push_back(Slot<uint32_t>(face, 46, &key) & 0xff);
        break;
    }
    default: break;
    }
    DumpRefs(b, out);
}

Blob RunSide(difftest::Context &ctx, uint32_t seed, bool ours, svcfx::Globals &globals)
{
    ConfigureFakes(seed);
    MakeKeys(seed);
    gDraw.seed(seed * 17 + 5);
    gWorkCalls = 0;
    globals.Set(0x10f5194, seed % 2 ? &F(kFw) : nullptr); // the framework's cache, empty now and then
    globals.SetWord(0x107c3e0, 5);
    Blob b;
    Sys *s = Build(ctx, b, ours);
    gCatalog = s;
    svcfx::AddRange(s, Sys::kSize, kTag);
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    Dump(b, s, ours);
    Populate(s, seed);
    Slot<uint32_t>(s, 1); // counted twice: QueryInterface's references come and go without destroying it
    Slot<uint32_t>(s, 1);
    std::mt19937 r(seed * 31 + 7);
    const unsigned ops = 3 + unsigned(r() % 14);
    for (unsigned i = 0; i < ops; ++i) {
        // Every slot alike, with Init / Shutdown / ClearAll (which empty everything) rarer.
        unsigned slot = unsigned(r() % 48);
        if ((slot == 2 || slot == 10) && r() % 3) slot = 46 - unsigned(r() % 10);
        if (slot == 9 && gNullBins) slot = 15; // SaveBins reads every filed bin, an empty slot too (both sides)
        char label[64];
        std::snprintf(label, sizeof(label), "seed %u %s op %u slot %u", seed, ours ? "ours" : "orig", i, slot);
        ctx.Mark(label);
        if (svcfx::gTrace) fprintf(stderr, "%s\n", label);
        b.push_back(0xa0000000u | slot);
        RunOp(b, s, slot, r);
        b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
        svcfx::gEv.clear();
        Dump(b, s, ours);
        svcfx::CaptureFakes(b, kValueKinds);
        b.push_back(*GameAddress<uint32_t>(0x107c3e0));
    }
    // The last Release destroys it (cRZUnknownMT's Release deletes at a count of 1 or below).
    while (Word(s, 8) > 1) b.push_back(Slot<uint32_t>(s, 2));
    b.push_back(Slot<uint32_t>(s, 2));
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b, kValueKinds);
    return b;
}

} // namespace bincat_test

DIFF_TEST(BinCatalog_Run, 0xb86c50)
{
    using namespace bincat_test;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.Set(0x10fc684, &F(kSvc));
    globals.Set(svcfx::kPersistCell, &F(kSvc));
    globals.Set(0x10c9378, &F(kFw)); // the framework (GetServiceObject, src/rz/Singleton_20ddfb.h)
    shape::CodePatch gameNew(0x824632, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch gameDelete(0x824624, reinterpret_cast<const void *>(&GameDelete));
    shape::CodePatch ensure(0xb88e10, reinterpret_cast<const void *>(&StandEnsureLoaded));
    shape::CodePatch load(0xb8d6b0, reinterpret_cast<const void *>(&StandLoadSource));
    shape::CodePatch rebuild(0xb8f3d0, reinterpret_cast<const void *>(&StandThis<0xb8f3d0>));
    shape::CodePatch positions(0xb90b80, reinterpret_cast<const void *>(&StandThis<0xb90b80>));
    shape::CodePatch indexBin(0xb87a40, reinterpret_cast<const void *>(&StandThisObject<0xb87a40>));
    shape::CodePatch unindexBin(0xb90150, reinterpret_cast<const void *>(&StandThisObject<0xb90150>));
    shape::CodePatch indexFragment(0xb87eb0, reinterpret_cast<const void *>(&StandThisObject<0xb87eb0>));
    shape::CodePatch unindexFragment(0xb90210, reinterpret_cast<const void *>(&StandThisObject<0xb90210>));
    shape::CodePatch indexKey(0xb8f420, reinterpret_cast<const void *>(&StandIndexBin));
    shape::CodePatch work(0xb8cbe0, reinterpret_cast<const void *>(&StandWork));
    shape::CodePatch find(0xb8a8d0, reinterpret_cast<const void *>(&StandFindFragment));
    shape::CodePatch renumber(0xb8adc0, reinterpret_cast<const void *>(&StandRenumber));
    shape::CodePatch forget(0xb8feb0, reinterpret_cast<const void *>(&StandForget));
    shape::CodePatch sourceFragments(0xb87f70, reinterpret_cast<const void *>(&StandSourceFragments));
    shape::CodePatch dropFragments(0xb903a0, reinterpret_cast<const void *>(&StandDropFragments));
    shape::CodePatch gather1(0xb8ba90, reinterpret_cast<const void *>(&StandGather<0xb8ba90>));
    shape::CodePatch gather2(0xb8b4a0, reinterpret_cast<const void *>(&StandGather<0xb8b4a0>));
    shape::CodePatch gather3(0xb8bf50, reinterpret_cast<const void *>(&StandGather<0xb8bf50>));
    shape::CodePatch gatherAll1(0xb8b660, reinterpret_cast<const void *>(&StandGatherAll<0xb8b660>));
    shape::CodePatch gatherAll2(0xb8b6c0, reinterpret_cast<const void *>(&StandGatherAll<0xb8b6c0>));
    shape::CodePatch outfit(0xb8aba0, reinterpret_cast<const void *>(&StandOutfitFragments));
    shape::CodePatch before(0xb8b340, reinterpret_cast<const void *>(&StandNeighbour<0xb8b340>));
    shape::CodePatch after(0xb8b770, reinterpret_cast<const void *>(&StandNeighbour<0xb8b770>));
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        gCreator = seed % 5 == 4;
        gNullBins = seed % 7 == 3;
        const Blob original = RunSide(ctx, seed, false, globals);
        const Blob ours = RunSide(ctx, seed, true, globals);
        if (!svcfx::Same(ctx, original, ours, "run", seed) && ctx.failures.size() <= 3) {
            size_t i = 0;
            while (i < original.size() && i < ours.size() && original[i] == ours[i]) ++i;
            size_t op = 0; // the operation the difference is in
            for (size_t k = 0; k < i; ++k)
                if ((original[k] & 0xffffff00u) == 0xa0000000u) op = k;
            fprintf(stderr, "seed %u: op at %zu slot %u\n", seed, op, original[op] & 0xff);
            for (size_t k = i > 120 && op < i - 120 ? i - 120 : op; k < i + 8 && k < original.size() && k < ours.size(); ++k)
                fprintf(stderr, "%5zu %08x %08x\n", k, original[k], ours[k]);
        }
    }
}
