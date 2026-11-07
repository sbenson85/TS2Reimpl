// Differential test for nTSCASCore::cTSSkinCatalog (lanes IU, JF; src/cas/cTSSkinCatalog.h): the class in the standalone
// shape.
//
// SkinCatalog_Run: a catalog built by the original constructor (0xb72310, game vtables 0xe8a4dc / 0xe8a514 / 0xe8a530) or
// the original creator (0xb7a0e0) against one built by ours (the constructor hook's adapter, or Create), each filled with
// the same state (written through our layout on both sides: the maps are MSVC 14.40 std::maps like the game's) and driven
// through its own vtables by the same random sequence of operations, then destroyed by the last Release (cRZUnknownMT's
// Release through the +4 face's slot 3: the thunk 0xb72f37, the deleting destructor 0xb730f0, the destructor 0xb72b40).
// Operations (every slot of every face): QueryInterface (0xb7bc70; through the +4 and +0x1c faces the thunks 0xb7bca1 /
// 0xb7bca9) / AddRef / Release, Init (0xb7aaf0; the factory's last Release seen at the game's operator delete), Shutdown
// (folded 0xb82190), and the cITSSkinCatalog slots 3..47: CountOfType 0xb7a620, TotalCount 0xb7a6a0, MappedId1 / 2 0xb7a7c0 /
// 0xb7a5a0, AddResource 0xb731d0, RemoveResource 0xb76bb0, Reload 0xb7aff0, ClearAll 0xb81530, FindType1 0xb7d700,
// FindType1Matching 0xb7da90, FindPropsMatching 0xb7dbf0, FindType2 0xb7d510, FindType2Matching 0xb7d650, HasType2
// 0xb7ada0, FindType3 0xb7b780, FindType3Matching 0xb7b8c0, HasType3 0xb7ad30, FindOverlayProps 0xb7c2c0,
// FindOverlayPropsMatching 0xb7c720, FindType5 0xb7bff0, FindType5Any 0xb7be10, FindType5Matching 0xb7c210, FindType4
// 0xb7e090, FindType4Matching 0xb7e2b0, FindType6 0xb7ce10, FindType6Matching 0xb7cf20, FindType7 0xb7cfd0,
// FindType7Matching 0xb7d110, FindType8 0xb7c840, FindType8Matching 0xb7c980, FindType9 0xb7cc20, FindType9Matching
// 0xb7cd60, FindType10 0xb7ca30, FindType10Matching 0xb7cb70, ApplyEntry 0xb7d1c0, PickRandomType1 0xb7a8e0,
// PickRandomProps 0xb7a810, AddSource 0xb740e0, RemoveSource 0xb81700, HasSource 0xb7ace0, GetSourceNames 0xb7a9b0,
// GetCurrentSourceName 0xb7a600, SelectSource 0xb81920, IsSourceOpen 0xb7ae60, ClearCurrentSource 0xb75dc0.
//
// Every object the catalog reaches is a recording fake (tests/ts_services_fixture.h): entries, their property objects,
// predicates, sources, resources, the persist manager (cell 0x10faf6c) and the framework's services. Stood in for on both
// sides (the image's functions, which our code calls through OriginalFunction): EnsureLoaded 0xb74910, LoadSource
// 0xb7b2d0, the ten insert helpers, IndexOfSourceNamed 0xb79f50, SelectSourceAt 0xb81820, IsSourceOpen(index) 0xb7ae10,
// the global 0xb8feb0; each logs its arguments (the catalog as an offset into it) and returns a drawn value. The game's
// operator new / delete (0x824632 / 0x824624) are ours, so map nodes either side allocates may be freed by the other's
// code. The zero GUID (0x4f8e50) is the real one: its static (0x10d9f54) is set per seed, its guard to "built".
//
// Left out of the comparison: the seed the constructor hands to cRZRandom::Construct. It is the C runtime's clock()
// (0xb72612: `call 0x1ba00b`, the image's, which the harness never initialises: it stands in with a constant), and each side
// has its own runtime; Construct (0x1b8bc8, ours) stands in on both sides, logs its object and writes the seed's state.
//
// Compared after every operation: the result (al where the slot returns a bool), the event log, every field (maps in order
// with their keys and values, vectors with their capacity, the never-written +0xbc..+0xc7), the out-parameters, and every
// fake's count; every third seed then puts a null entry under a key and calls ApplyEntry and HasType2 / 3 on it (the
// slots that skip null entries); at the end the counts after the destruction.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "cas/cTSSkinCatalog.h"
#include "game/game.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"

#include <cstring>
#include <iterator>
#include <new>
#include <random>
#include <vector>

namespace skincat_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{4, "dddd"}, {7, "dd"}, {16, "d"}, {17, "d"}, {18, "d"}, {21, "dd"}};

using namespace nTSCASCore;
using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSSkinCatalog;

constexpr uint32_t kTag = 0x7d000000;    // the catalog
constexpr uint32_t kKeyTag = 0x6b000000; // the key pool

enum : unsigned {
    kFw = 0, kPersist, kSub, kHolder, kResource, kGrouped, kTyped, kPred, kSrc0, kSrc1, kSrc2, kEntry0, // 11..15 entries
    kProps0 = 16, // 16..18
    kTarget = 19, kSrcNew, kPred1,
};
constexpr unsigned kEntries = 5, kSetA = 3; // entries 0..2: masks at slot 7, ids at 10; 3..4: GUIDs at 7..9, masks at 10, 13
constexpr unsigned kProps = 3;

constexpr uint32_t kResType[10] = {0xebcf3e27, 0x4c158081, 0x8c1580b5, 0x2c1fd8a1, 0x0c1fe246,
                                   0x6c93b566, 0x8c93bf6c, 0x8c93e35c, 0x0c93e3de, 0xd1954460};

SkinKey gKeys[8];
std::mt19937 gDraw;
uint32_t Draw(uint32_t n) { return uint32_t(gDraw() % n); }
cIGZUnknown *U(unsigned i) { return reinterpret_cast<cIGZUnknown *>(&F(i)); }

void LogKeyAt(uint32_t at)
{
    const auto *k = reinterpret_cast<const SkinKey *>(uintptr_t(at));
    Ev(k->type);
    Ev(k->group);
    Ev(k->instance);
    Ev(k->resource);
}

// ---- fake slots ----

uint32_t DrawBool(Obj *, uint32_t *) { return 0x100u | Draw(2); }
uint32_t LogKeyBool(Obj *, uint32_t *a)
{
    LogKeyAt(a[0]);
    return 0x100u | Draw(2);
}
uint32_t LogKeyResult(Obj *, uint32_t *a) // the persist manager's slot 17: a byte the caller keeps raw
{
    LogKeyAt(a[0]);
    static const uint32_t kResults[] = {0, 1, 2, 0x301};
    return kResults[Draw(4)];
}
uint32_t InstanceTaken(Obj *, uint32_t *a)
{
    LogKeyAt(a[0]);
    return Draw(3) == 0;
}
uint32_t SubFind(Obj *, uint32_t *a) // slot 34: (key, &source)
{
    LogKeyAt(a[0]);
    static const unsigned kFound[] = {~0u, kSrc1, kSrcNew};
    const unsigned which = kFound[Draw(3)];
    if (which != ~0u) *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(which);
    return 0;
}
uint32_t FrameworkSlot5(Obj *, uint32_t *a)
{
    ++F(kHolder).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kHolder);
    return 1;
}
uint32_t HolderValue(Obj *, uint32_t *a) // slot 13: (key, &value)
{
    LogKeyAt(a[0]);
    if (Draw(2)) *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(kTarget);
    return 0x100u | Draw(2);
}
uint32_t ResourceQuery(Obj *, uint32_t *a)
{
    if (Draw(4) == 0) return 0;
    ++F(kTyped).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(kTyped);
    return 1;
}
uint32_t SourceQuery(Obj *, uint32_t *a)
{
    if (Draw(3) == 0) return 0x200;
    ++F(kGrouped).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(kGrouped);
    return 1;
}
uint32_t ResourceKey(Obj *self, uint32_t *a) // slot 3: the key into *a[0]
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), self->value, sizeof(SkinKey));
    return a[0];
}
uint32_t SourceName(Obj *self, uint32_t *a)
{
    char text[16];
    std::snprintf(text, sizeof(text), "src%u", self->id);
    reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString.assign(text);
    return 1;
}
uint32_t PredTakes(Obj *self, uint32_t *a) { return 0x200u | (((svcfx::Norm(a[0]) * 2654435761u + self->value[0]) >> 9) & 1); }
uint32_t PredType(Obj *self, uint32_t *a) { return 0x400u | ((self->value[1] >> (a[0] & 31)) & 1); }
uint32_t PropsKey(Obj *self, uint32_t *a) // slot 3 of a property object: its key, by value
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), &gKeys[self->value[0] % 8], sizeof(SkinKey));
    return a[0];
}
// The manager's slot 20, (kind, id, factory): logged like a generic slot, but the factory (a heap block) by its contents
// and whether it is the one the previous call got.
const void *gLastFactory;
const void *gFactory; // the last factory registered, until freed
// The game's operator delete: a factory freed (its last Release, game code) is logged.
void __cdecl GameDelete(void *p, size_t size)
{
    if (p && p == gFactory) {
        Ev(0xdead0000u | uint32_t(size));
        gFactory = nullptr;
    }
    ::operator delete(p);
}
uint32_t __fastcall RegisterFactory(Obj *self, void *, uint32_t kind, uint32_t id, const uint32_t *factory)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(20);
    Ev(3);
    Ev(kind);
    Ev(id);
    Ev(factory == gLastFactory);
    gLastFactory = factory;
    gFactory = factory;
    Ev(factory[0] - P(GameAddress<void>(0)));
    Ev(factory[1] - P(GameAddress<void>(0)));
    Ev(factory[2]);
    Ev(factory[3] & 0xff);
    return 1;
}

// ---- stand-ins ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
long __cdecl ImageClock() { return 12345; }

cRZRandom gRandomState;
const void *gRandomAt;
cRZRandom *__fastcall StandRandom(cRZRandom *self, void *, uint32_t)
{
    Ev(0x1b8bc8);
    gRandomAt = self; // logged by RunSide as an offset into the catalog (its range is not known before the creator returns)
    *self = gRandomState;
    return self;
}
void __fastcall StandEnsureLoaded(void *self, void *)
{
    Ev(0xb74910);
    Ev(svcfx::Norm(P(self)));
}
uint32_t __fastcall StandLoadSource(void *self, void *, void *source)
{
    Ev(0xb7b2d0);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(P(source)));
    ++*reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 0x24);
    return 0x100u | Draw(2);
}
template <uint32_t Rva> void __fastcall StandInsert(void *self, void *, void *typed)
{
    Ev(Rva);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(P(typed)));
}
int32_t __fastcall StandIndexOf(void *self, void *, const char *name)
{
    Ev(0xb79f50);
    Ev(svcfx::Norm(P(self)));
    Ev(P(name));
    static const int32_t kIndexes[] = {-1, 0, 1, 2};
    return kIndexes[Draw(4)];
}
uint32_t __fastcall StandSelectAt(void *self, void *, int32_t index, uint32_t check)
{
    Ev(0xb81820);
    Ev(svcfx::Norm(P(self)));
    Ev(uint32_t(index));
    Ev(check & 0xff);
    return 0x300u | Draw(2);
}
uint32_t __fastcall StandOpenAt(void *self, void *, int32_t index)
{
    Ev(0xb7ae10);
    Ev(svcfx::Norm(P(self)));
    Ev(uint32_t(index));
    return 0x500u | Draw(2);
}
uint32_t __cdecl StandForget(void *record, const SkinKey *key)
{
    Ev(0xb8feb0);
    Ev(svcfx::Norm(P(record)));
    LogKeyAt(P(key));
    return 0x700u | Draw(2);
}

// The type-1 predicate's group bits (slot 4): a subset of the groups the catalog has (FindType1Matching searches a bit
// without a group forever, an original bug the replacement keeps), with bits above the 14 it uses now and then.
Sys *gCatalog;
uint32_t Pred1Bits(Obj *self, uint32_t *)
{
    uint32_t bits = 0;
    unsigned i = 0;
    for (const auto &[bit, inner] : gCatalog->mType1Groups)
        if ((self->value[1] >> (i++ % 32)) & 1) bits |= bit;
    return bits | (self->value[0] & 1 ? 0xc000u : 0u) | (self->value[0] & 2 ? 0xffff0000u : 0u);
}

// ---- the world for a seed ----

SkinKey RandomKey(std::mt19937 &r, uint32_t type) { return {type, 1 + uint32_t(r() % 2), 1 + uint32_t(r() % 2), uint32_t(r() % 2)}; }
uint32_t Bits(std::mt19937 &r) // small masks, often zero, with busy upper bits now and then
{
    static const uint32_t kBits[] = {0, 0, 1, 2, 3, 4, 6, 0x8001, 0xff00, 0xffff, 0x10000, 0x12345601};
    return kBits[r() % 12];
}

void ConfigureFakes(uint32_t seed)
{
    std::mt19937 r(seed * 7 + 3);
    svcfx::ResetAll();
    svcfx::AddRange(gKeys, sizeof(gKeys), kKeyTag);
    F(kFw).Set(5, "ddp", 0, &FrameworkSlot5);
    F(kPersist).Set(3, 0, &F(kSub));
    F(kPersist).Set(16, "pp", 0, &DrawBool);
    F(kPersist).Set(17, "p", 0, &LogKeyResult);
    F(kSub).Set(15, "p", 0, &InstanceTaken);
    F(kSub).Set(34, "pp", 0, &SubFind);
    F(kSub).Set(20, 3, 0u);
    F(kSub).table[20] = reinterpret_cast<void *>(&RegisterFactory);
    gLastFactory = nullptr;
    gFactory = nullptr;
    F(kHolder).Set(13, "pp", 0, &HolderValue);
    F(kHolder).Set(5, "ppd--", 0, &DrawBool);
    F(kResource).Set(0, "dp", 0, &ResourceQuery);
    F(kResource).Set(3, "p", 0, &ResourceKey);
    F(kResource).Set(4, "p", 0, &LogKeyBool);
    F(kResource).Set(12, 0, 0u);
    F(kResource).Set(15, 0, 0u);
    F(kResource).Set(27, "p", 0u);
    F(kGrouped).Set(4, 0, r() % 2 ? 0x2c17b74a : r());
    F(kGrouped).Set(6, 0, 0, [](Obj *, uint32_t *) -> uint32_t {
        static const uint32_t kAnswers[] = {1, 0, 2, 0x101};
        return kAnswers[Draw(4)];
    });
    for (unsigned p : {kPred, kPred1}) {
        F(p).value[0] = uint32_t(r());
        F(p).value[1] = uint32_t(r());
        F(p).Set(3, "p", 0, &PredTakes);
    }
    F(kPred).Set(4, "d", 0, &PredType);
    F(kPred1).Set(4, 0, 0, &Pred1Bits);
    for (unsigned s : {kSrc0, kSrc1, kSrc2, kSrcNew}) {
        F(s).Set(0, "dp", 0, &SourceQuery);
        F(s).Set(10, 0, 0u);
        F(s).Set(11, "p", 0, &SourceName);
        F(s).Set(15, 0, r() % 3);
        F(s).Set(20, "p", 0, &LogKeyBool);
    }
    auto key = [&] { return &gKeys[r() % 8]; };
    for (unsigned e = 0; e < kEntries; ++e) {
        Obj &o = F(kEntry0 + e);
        o.Set(0, "pd", 0, &DrawBool);
        o.Set(3, 0, &F(kProps0 + e % kProps));
        o.Set(4, 0, key());
        o.Set(5, 0, key());
        if (e < kSetA) {
            o.Set(6, 0, key());
            o.Set(7, 0, Bits(r));
            o.Set(10, 0, key());
        } else {
            o.Set(7, 0, key());
            o.Set(8, 0, key());
            o.Set(9, 0, key());
            o.Set(10, 0, Bits(r));
            o.Set(13, 0, Bits(r));
        }
    }
    for (unsigned p = 0; p < kProps; ++p) {
        Obj &o = F(kProps0 + p);
        o.value[0] = uint32_t(r());
        o.Set(3, "p", 0, &PropsKey);
        o.Set(16, 0, key());
        for (unsigned slot : {19u, 20u}) o.Set(slot, 0, r() % 4 ? Bits(r) | 0x10u : Bits(r)); // ages / genders mostly set
        for (unsigned slot : {21u, 22u, 25u}) o.Set(slot, 0, Bits(r));
    }
}

// The key pool (some equal to the zero GUID by content) and the zero GUID's static.
void MakeKeys(uint32_t seed, svcfx::Globals &globals)
{
    std::mt19937 r(seed * 5 + 11);
    const SkinKey zero = r() % 3 ? SkinKey{0, 0, 0, 0} : SkinKey{1, 2, 1, 0};
    for (SkinKey &k : gKeys) k = r() % 3 == 0 ? zero : RandomKey(r, r() % 2 ? 0 : 1);
    globals.SetWord(0x10d9f54, zero.type);
    globals.SetWord(0x10d9f58, zero.group);
    globals.SetWord(0x10d9f5c, zero.instance);
    globals.SetWord(0x10d9f60, zero.resource);
    gRandomState.Construct(seed * 2654435761u + 1);
}

SkinEntryMap &MapOf(Sys *s, unsigned type)
{
    SkinEntryMap *maps[] = {&s->mType1, &s->mType2, &s->mType3, &s->mType4, &s->mType5,
                            &s->mType6, &s->mType7, &s->mType8, &s->mType9, &s->mType10};
    return *maps[type - 1];
}
unsigned EntryFor(std::mt19937 &r, unsigned type)
{
    return type == 4 || type == 5 ? kEntry0 + kSetA + unsigned(r() % (kEntries - kSetA)) : kEntry0 + unsigned(r() % kSetA);
}

// The catalog's state (the same on both sides: written through our layout).
void Populate(Sys *s, uint32_t seed)
{
    std::mt19937 r(seed * 13 + 1);
    s->mLoaded = r() % 2;
    s->mLoadedCount = r() % 5;
    s->mNextInstance = r() % 3;
    const unsigned sources = r() % 4;
    for (unsigned i = 0; i < sources; ++i) s->mSources.push_back(SkinRef(U(kSrc0 + r() % 3)));
    s->mCurrent = sources && r() % 2 ? int32_t(r() % sources) : -1;
    for (unsigned type = 1; type <= 10; ++type) {
        const unsigned n = r() % 4;
        for (unsigned i = 0; i < n; ++i) MapOf(s, type).emplace(RandomKey(r, kResType[type - 1]), SkinRef(U(EntryFor(r, type))));
    }
    // 1 and 2 twice as often: FindType1 visits several bits and filters later maps by the bits not yet visited (coordinator).
    static const uint32_t kGroupBits[] = {1, 2, 4, 8, 0x2000, 0x4000, 1, 2};
    const unsigned groups = r() % 4;
    for (unsigned g = 0; g < groups; ++g) {
        SkinEntryMap &inner = s->mType1Groups[kGroupBits[r() % 8]];
        const unsigned n = r() % 3;
        for (unsigned i = 0; i < n; ++i) {
            SkinKey key = RandomKey(r, kResType[0]);
            if (!s->mType1.empty() && r() % 3) {
                auto it = s->mType1.begin();
                std::advance(it, r() % s->mType1.size());
                key = it->first;
            }
            inner.emplace(key, SkinRef(U(EntryFor(r, 1))));
        }
    }
    for (SkinIdMap *ids : {&s->mIds1, &s->mIds2}) {
        const unsigned n = r() % 3;
        for (unsigned i = 0; i < n; ++i) (*ids)[r() % 4] = uint32_t(r());
    }
    const unsigned held = r() % 3;
    for (unsigned i = 0; i < held; ++i) s->mResources.push_back(SkinRef(U(kEntry0 + r() % kEntries)));
}

// ---- dumping ----

uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

void DumpMap(Blob &b, const SkinEntryMap &map)
{
    b.push_back(uint32_t(map.size()));
    for (const auto &[k, v] : map) {
        b.insert(b.end(), {k.type, k.group, k.instance, k.resource, svcfx::Norm(P(v.Get()))});
    }
}
void DumpRefs(Blob &b, const SkinRefVector &v)
{
    b.push_back(uint32_t(v.size()));
    b.push_back(uint32_t(v.capacity()));
    for (const SkinRef &ref : v) b.push_back(svcfx::Norm(P(ref.Get())));
}

void Dump(Blob &b, Sys *s, bool ours)
{
    static const uint32_t kFaces[3][2] = {{0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kCatalogVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(s, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 8; off < 0x1c; off += 4) b.push_back(Word(s, off));
    b.push_back(Word(s, 0x20));
    b.push_back(Word(s, 0x24));
    b.push_back(Word(s, 0x28));
    DumpRefs(b, s->mSources);
    b.push_back(Word(s, 0x38));
    for (unsigned type = 1; type <= 10; ++type) DumpMap(b, MapOf(s, type));
    b.push_back(uint32_t(s->mType1Groups.size()));
    for (const auto &[bit, inner] : s->mType1Groups) {
        b.push_back(bit);
        DumpMap(b, inner);
    }
    for (const SkinIdMap *ids : {&s->mIds1, &s->mIds2}) {
        b.push_back(uint32_t(ids->size()));
        for (const auto &[k, v] : *ids) b.insert(b.end(), {k, v});
    }
    DumpRefs(b, s->mResources);
    for (uint32_t off = 0xb0; off < 0xbc; off += 4) b.push_back(Word(s, off));
    for (uint32_t off = 0xbc; off < 0xc8; off += 4) b.push_back(Word(s, off));
}
void DumpOut(Blob &b, const SkinPtrVector &out)
{
    b.push_back(uint32_t(out.size()));
    for (void *p : out) b.push_back(svcfx::Norm(P(p)));
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
        void *made = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0xb7a0e0)();
        b.push_back(made != nullptr);
        return static_cast<Sys *>(made);
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(cTSSkinCatalogAdapters::Construct(memory, nullptr))
                   : ctx.Original<void *(__thiscall *)(void *)>(0xb72310)(memory);
    b.push_back(r == memory);
    return static_cast<Sys *>(memory);
}

const char *const kNames[] = {"Base", "", "Expansion 1"};
const uint32_t kIids[] = {1, Sys::kIid, 0x287fb697 /* cIGZSystemService */, 0x12345678};

// Draws 0, 1, 2 or -1 (no cap), the boundaries of every loop cap.
int32_t Cap(std::mt19937 &r)
{
    static const int32_t kCaps[] = {0, 1, 2, -1, 3};
    return kCaps[r() % 5];
}
// Filter arguments: often none (no key, all bits), so that entries pass the other filters and each one is seen deciding.
const SkinKey *KeyArg(std::mt19937 &r) { return r() % 2 == 0 ? nullptr : &gKeys[r() % 8]; }
uint32_t ByteArg(std::mt19937 &r) { return (r() % 2 ? 0xabcd0000u : 0u) | (r() % 2 ? 0xffffu : Bits(r)); }

void RunOp(Blob &b, Sys *s, unsigned slot, std::mt19937 &r)
{
    void *const primary = s;
    void *const face = reinterpret_cast<uint8_t *>(s) + 0x1c;
    SkinPtrVector out;
    if (r() % 3 == 0) out.push_back(&F(kTarget)); // a vector the call appends to or clears
    const uint32_t ages = ByteArg(r), genders = ByteArg(r), flags = ByteArg(r);
    auto o = &out;
    switch (slot) {
    case 0: { // QueryInterface on a face (+0, the +4 thunk 0xb7bca1, the +0x1c thunk 0xb7bca9), the result released
        static const uint32_t kFaceAt[] = {0, 4, 0x1c};
        void *at = reinterpret_cast<uint8_t *>(s) + kFaceAt[r() % 3];
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(at, 0, kIids[r() % 4], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 1: // Init
        gLastFactory = nullptr;
        b.push_back(Slot<uint32_t>(primary, 8) & 0xff);
        break;
    case 2: b.push_back(Slot<uint32_t>(primary, 9) & 0xff); break; // Shutdown
    case 3: {
        static const uint32_t kTypes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0xffffffff};
        b.push_back(Slot<uint32_t>(face, 3, kTypes[r() % 13]));
        break;
    }
    case 4: b.push_back(Slot<uint32_t>(face, 4)); break;
    case 5:
    case 6: b.push_back(Slot<uint32_t>(face, slot, uint32_t(r() % 5))); break;
    case 7: { // AddResource
        Obj &res = F(kResource);
        static const uint32_t kGroups[] = {0, 0xffffffff, 7};
        static const uint32_t kTypes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0xffffffff};
        const SkinKey key = {r() % 3 ? 0u : 0x1234u, kGroups[r() % 3], r() % 2 ? 0u : 5u, uint32_t(r() % 2)};
        std::memcpy(res.value, &key, sizeof(key));
        res.ret[12] = kTypes[r() % 13];
        res.ret[15] = r() % 2 ? 0x1ffffu : 3u;
        static const unsigned kSources[] = {~0u, ~0u, kSrcNew, kSrc2};
        const unsigned which = kSources[r() % 4];
        void *source = which == ~0u ? nullptr : static_cast<void *>(&F(which));
        b.push_back(Slot<uint32_t>(face, 7, static_cast<void *>(&res), source) & 0xff);
        break;
    }
    case 8: { // RemoveResource: a key of the maps, now and then
        Obj &res = F(kResource);
        const unsigned type = 1 + unsigned(r() % 10);
        SkinKey key = RandomKey(r, kResType[type - 1]);
        SkinEntryMap &map = MapOf(s, type);
        if (!map.empty() && r() % 2) {
            auto it = map.begin();
            std::advance(it, r() % map.size());
            key = it->first;
        }
        if (r() % 4 == 0) key.instance = 0;
        if (r() % 5 == 0) key.group = 0;
        std::memcpy(res.value, &key, sizeof(key));
        res.ret[12] = r() % 6 ? type : uint32_t(r() % 2 ? 11 : 0);
        b.push_back(Slot<uint32_t>(face, 8, static_cast<void *>(&res)) & 0xff);
        break;
    }
    case 9: b.push_back(Slot<uint32_t>(face, 9)); break;
    case 10: Slot<void>(face, 10); break;
    case 11: {
        static const uint32_t kGroups[] = {0, 1, 3, 5, 0x2001, 0x4000, 0x10000, 0x10001, 0xffff};
        Slot<void>(face, 11, o, ages, genders, KeyArg(r), KeyArg(r), flags, kGroups[r() % 9], r() % 2 ? Bits(r) : ByteArg(r), KeyArg(r));
        break;
    }
    case 12: Slot<void>(face, 12, o, static_cast<void *>(&F(kPred1)), uint32_t(r() % 2), Cap(r)); break;
    case 14:
    case 17: Slot<void>(face, slot, o, ages, genders, flags, Bits(r), KeyArg(r)); break;
    case 16:
    case 19: b.push_back(Slot<uint32_t>(face, slot, &gKeys[r() % 8]) & 0xff); break;
    case 20: Slot<void>(face, 20, o, ages, genders, flags, KeyArg(r), KeyArg(r), KeyArg(r), ByteArg(r)); break;
    case 22:
    case 25: Slot<void>(face, slot, o, ages, genders, flags, KeyArg(r), KeyArg(r), KeyArg(r), ByteArg(r), ByteArg(r)); break;
    case 23: Slot<void>(face, 23, o, ages, genders, flags, KeyArg(r), KeyArg(r), KeyArg(r), ByteArg(r)); break;
    case 27: Slot<void>(face, 27, o, ages, genders, flags, ByteArg(r)); break;
    case 29:
    case 31:
    case 33:
    case 35: Slot<void>(face, slot, o, ages, genders, flags, ByteArg(r), KeyArg(r)); break;
    case 13: case 15: case 18: case 21: case 24: case 26: case 28: case 30: case 32: case 34: case 36:
        Slot<void>(face, slot, o, static_cast<void *>(&F(kPred)), uint32_t(r() % 2) | (r() % 2 ? 0xab00u : 0u), Cap(r));
        break;
    case 37: { // ApplyEntry: a key of the maps, now and then
        SkinKey key = RandomKey(r, r() % 5 ? kResType[r() % 10] : 0x12345678u);
        b.push_back(Slot<uint32_t>(face, 37, &key, static_cast<void *>(&F(kTarget)), uint32_t(r())) & 0xff);
        break;
    }
    case 38:
    case 39: {
        SkinKey key = {0xeeeeeeee, 0xeeeeeeee, 0xeeeeeeee, 0xeeeeeeee};
        b.push_back(Slot<uint32_t>(face, slot, &key, static_cast<void *>(&F(slot == 38 ? kPred1 : kPred))) & 0xff);
        b.insert(b.end(), {key.type, key.group, key.instance, key.resource});
        break;
    }
    case 40: {
        static const unsigned kSources[] = {~0u, kSrc0, kSrc1, kSrcNew};
        const unsigned which = kSources[r() % 4];
        b.push_back(Slot<uint32_t>(face, 40, which == ~0u ? nullptr : static_cast<void *>(&F(which)),
                                   uint32_t(r() % 2) | 0xcd00u) & 0xff);
        break;
    }
    case 41:
    case 42: {
        static const unsigned kSources[] = {~0u, kSrc0, kSrc2, kSrcNew};
        const unsigned which = kSources[r() % 4];
        b.push_back(Slot<uint32_t>(face, slot, which == ~0u ? nullptr : static_cast<void *>(&F(which))) & 0xff);
        break;
    }
    case 43: {
        ContentServiceStrings names;
        if (r() % 2) names.emplace_back("old");
        Slot<void>(face, 43, &names);
        b.push_back(uint32_t(names.size()));
        for (const ContentString &n : names) {
            b.push_back(uint32_t(n.s.mString.size()));
            for (char c : n.s.mString) b.push_back(uint8_t(c));
        }
        break;
    }
    case 44: {
        ContentString name("before");
        b.push_back(Slot<uint32_t>(face, 44, name.s.AsInterface()) & 0xff);
        for (char c : name.s.mString) b.push_back(uint8_t(c));
        break;
    }
    case 45:
    case 46: b.push_back(Slot<uint32_t>(face, slot, kNames[r() % 3]) & 0xff); break;
    case 47: Slot<void>(face, 47); break;
    default: break;
    }
    DumpOut(b, out);
}

Blob RunSide(difftest::Context &ctx, uint32_t seed, bool ours, svcfx::Globals &globals)
{
    ConfigureFakes(seed);
    MakeKeys(seed, globals);
    gDraw.seed(seed * 17 + 5);
    Blob b;
    gRandomAt = nullptr;
    Sys *s = Build(ctx, b, ours);
    gCatalog = s;
    svcfx::AddRange(s, Sys::kSize, kTag);
    b.push_back(svcfx::Norm(P(gRandomAt)));
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    if (gCreator) { // the bytes the creator leaves as the heap had them (padding, +0x24, +0xbc..), made alike
        auto *m = reinterpret_cast<uint8_t *>(s);
        std::memset(m + 0x19, 0xc5, 3);
        std::memset(m + 0x21, 0xc5, 3);
        s->mLoadedCount = 0xc5c5c5c5;
        std::memset(s->mUnusedBC, 0xc5, sizeof(s->mUnusedBC));
    }
    Dump(b, s, ours);
    Populate(s, seed);
    Slot<uint32_t>(s, 1); // counted twice: QueryInterface's references come and go without destroying it
    Slot<uint32_t>(s, 1);
    std::mt19937 r(seed * 31 + 7);
    const unsigned ops = 3 + unsigned(r() % 14);
    for (unsigned i = 0; i < ops; ++i) {
        // Every slot alike, with Init / Shutdown / ClearAll (which empty everything) rarer.
        unsigned slot = unsigned(r() % 48);
        if ((slot == 2 || slot == 10) && r() % 3) slot = 47 - unsigned(r() % 10);
        // AddResource and GetCurrentSourceName read the current source unchecked: not after removals left the index past
        // the end (both sides would read a stale or foreign word).
        if ((slot == 7 || slot == 44) && s->mCurrent >= int32_t(s->mSources.size())) slot = 47;
        if (svcfx::gTrace) fprintf(stderr, "seed %u %s op %u slot %u\n", seed, ours ? "ours" : "orig", i, slot);
        b.push_back(0xa0000000u | slot);
        RunOp(b, s, slot, r);
        b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
        svcfx::gEv.clear();
        Dump(b, s, ours);
        svcfx::CaptureFakes(b, kValueKinds);
    }
    // A null entry under a key (the queries that iterate would call it; ApplyEntry and HasType2 / 3 skip it): those
    // three, then the entry taken out again.
    if (seed % 3 == 1) {
        void *const face = reinterpret_cast<uint8_t *>(s) + 0x1c;
        const unsigned type = 1 + unsigned(r() % 3);
        SkinKey key = RandomKey(r, kResType[type - 1]);
        MapOf(s, type)[key].Assign(nullptr);
        b.push_back(Slot<uint32_t>(face, 37, &key, static_cast<void *>(&F(kTarget)), 7u) & 0xff);
        b.push_back(Slot<uint32_t>(face, 16, &key) & 0xff);
        b.push_back(Slot<uint32_t>(face, 19, &key) & 0xff);
        b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
        svcfx::gEv.clear();
        MapOf(s, type).erase(key);
    }
    // The last Release destroys it (cRZUnknownMT's Release deletes at a count of 1 or below).
    while (Word(s, 8) > 1) b.push_back(Slot<uint32_t>(s, 2));
    b.push_back(Slot<uint32_t>(s, 2));
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b, kValueKinds);
    return b;
}

} // namespace skincat_test

DIFF_TEST(SkinCatalog_Run, 0xb72310)
{
    using namespace skincat_test;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.Set(svcfx::kPersistCell, &F(kPersist));
    globals.Set(0x10c9378, &F(kFw)); // the framework (GetServiceObject, src/rz/Singleton_20ddfb.h)
    globals.SetWord(0x10d9f64, uint32_t(INT32_MIN)); // the zero GUID's guard: built
    shape::CodePatch gameNew(0x824632, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch gameDelete(0x824624, reinterpret_cast<const void *>(&GameDelete));
    shape::CodePatch clock(0x1ba00b, reinterpret_cast<const void *>(&ImageClock));
    shape::StandIn random(0x1b8bc8, &cRZRandom::Construct, &StandRandom);
    shape::CodePatch ensure(0xb74910, reinterpret_cast<const void *>(&StandEnsureLoaded));
    shape::CodePatch load(0xb7b2d0, reinterpret_cast<const void *>(&StandLoadSource));
    shape::CodePatch indexOf(0xb79f50, reinterpret_cast<const void *>(&StandIndexOf));
    shape::CodePatch selectAt(0xb81820, reinterpret_cast<const void *>(&StandSelectAt));
    shape::CodePatch openAt(0xb7ae10, reinterpret_cast<const void *>(&StandOpenAt));
    shape::CodePatch forget(0xb8feb0, reinterpret_cast<const void *>(&StandForget));
    shape::CodePatch insert1(0xb742b0, reinterpret_cast<const void *>(&StandInsert<0xb742b0>));
    shape::CodePatch insert2(0xb74690, reinterpret_cast<const void *>(&StandInsert<0xb74690>));
    shape::CodePatch insert3(0xb73ba0, reinterpret_cast<const void *>(&StandInsert<0xb73ba0>));
    shape::CodePatch insert4(0xb747c0, reinterpret_cast<const void *>(&StandInsert<0xb747c0>));
    shape::CodePatch insert5(0xb73cd0, reinterpret_cast<const void *>(&StandInsert<0xb73cd0>));
    shape::CodePatch insert6(0xb73e00, reinterpret_cast<const void *>(&StandInsert<0xb73e00>));
    shape::CodePatch insert7(0xb73a70, reinterpret_cast<const void *>(&StandInsert<0xb73a70>));
    shape::CodePatch insert8(0xb736e0, reinterpret_cast<const void *>(&StandInsert<0xb736e0>));
    shape::CodePatch insert9(0xb73940, reinterpret_cast<const void *>(&StandInsert<0xb73940>));
    shape::CodePatch insert10(0xb73810, reinterpret_cast<const void *>(&StandInsert<0xb73810>));
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        gCreator = seed % 5 == 4;
        const Blob original = RunSide(ctx, seed, false, globals);
        const Blob ours = RunSide(ctx, seed, true, globals);
        if (!svcfx::Same(ctx, original, ours, "run", seed) && svcfx::gTrace) {
            size_t i = 0;
            while (i < original.size() && i < ours.size() && original[i] == ours[i]) ++i;
            for (size_t k = i > 40 ? i - 40 : 0; k < i + 8 && k < original.size() && k < ours.size(); ++k)
                fprintf(stderr, "%5zu %08x %08x\n", k, original[k], ours[k]);
        }
    }
}
