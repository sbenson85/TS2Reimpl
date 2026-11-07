// Differential test for cTSPatternCatalog (lane JW; src/cas/cTSPatternCatalog.h): the class in the standalone shape.
//
// PatternCatalog_Run: a catalog built by the original constructor (0xc9f9e0, game vtables 0xeb51b0 / 0xeb51e8 / 0xeb5204)
// or the original creator (0xca27f0) against one built by ours (the constructor hook's adapter, or Create), each filled
// with the same state (written through our layout on both sides: the maps are MSVC 14.40 containers like the game's) and
// driven through its own vtables by the same random sequence of operations, then destroyed by the last Release
// (cRZUnknownMT's Release through the +4 face's slot 3: the thunk 0xca051a, the deleting destructor 0xca05e0, the
// destructor 0xca0010). Operations (every slot of every face the class defines): QueryInterface (0xca3060; through the +4
// and +0x1c faces the thunks 0xca3091 / 0xca3099) / AddRef / Release, Init (0xca2880; the factory's last Release seen at
// the game's operator delete), Shutdown (0xca4970), and the cITSPatternCatalog slots 3..11: AddPattern 0xca07b0,
// RemovePattern 0xca1280, Load 0xca2a40 (its key loader 0xca2ca0, the insert helpers 0xca0a60 / 0xca0b00 / 0xca0940 /
// 0xca0690 / 0xca0c20, and cPatternFilter: IsKeyIncluded 0xca29f0, the deleting destructor 0xca0530, the folded
// QueryInterface 0xb7b970 / Release 0x72c480), ClearAll 0xca4630, GetPatterns 0xca3170, GetKind3 0xca3240, GetKind4
// 0xca2f10, GetKind5 0xca2e70, GetKind6 0xca32e0 (all through EnsureLoaded 0xca0d40).
//
// Every object the catalog reaches is a recording fake (tests/ts_services_fixture.h): the framework (cell 0x10c9378), the
// persist system's resource-manager face and its own face, the COM object (the director's slot 12), segments, pattern
// resources and their typed faces. Stood in for on both sides: the file checks 0x1c2182 / 0x1c2e25 (RemovePattern), each
// logging the path it is handed and answering a drawn value. The game's operator new / delete (0x824632 / 0x824624) are
// ours, so nodes either side allocates may be freed by the other's code.
//
// Compared after every operation: the result (al where the slot returns a bool), the event log, every field (maps in
// order with their keys and values), the out-parameter, and every fake's count and words; at the end the counts after the
// destruction.
#include "code_patch.h"
#include "difftest.h"
#include "rz/PathFileOps.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "cas/cTSPatternCatalog.h"
#include "game/game.h"
#include "rz/cRZString.h"

#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace patcat_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{8, "ddddd"}, {9, "ddddd"}, {10, "ddddd"}, {11, "ddddd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSPatternCatalog;
using KeyList = std::vector<PatternKey, GZServiceAllocator<PatternKey>>;

constexpr uint32_t kTag = 0x7d000000;    // the catalog
constexpr uint32_t kKeyTag = 0x6b000000; // the key pool

enum : unsigned {
    kFw = 0, kMgr, kSys, kSysMgr, kCom,
    kSeg0 = 5,   // 5..7
    kRes0 = 8,   // 8..11
    kTyped0 = 12, // 12..15
};
constexpr unsigned kSegs = 3, kRes = 4;

PatternKey gKeys[8];
std::mt19937 gDraw;
uint32_t Draw(uint32_t n) { return uint32_t(gDraw() % n); }
bool gNoService; // the framework answers nothing (Init's failure)

void LogKeyAt(uint32_t at)
{
    const auto *k = reinterpret_cast<const PatternKey *>(uintptr_t(at));
    Ev(k->type);
    Ev(k->group);
    Ev(k->instance);
    Ev(k->resource);
}
uint32_t Self(const void *p) { return svcfx::NormOut(P(p)); }

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}

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
uint32_t GetService(Obj *, uint32_t *a) // the framework's slot 5: (id, iid, &out)
{
    if (gNoService) return 0;
    const unsigned which = a[1] == 0x656b8efc ? kMgr : a[1] == 0x9af5e924 ? kSys : ~0u;
    if (which == ~0u) return 0;
    ++F(which).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(which);
    return 1;
}
uint32_t GetResource(Obj *, uint32_t *a) // the manager's slot 3: (key, iid, &out, 0, 0)
{
    LogKeyAt(a[0]);
    const unsigned draw = Draw(5);
    if (draw == 0) return 0x200;
    const unsigned which = kRes0 + Draw(kRes);
    ++F(which).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(which);
    return draw == 1 ? 0x100 : 1; // now and then a refusal that still hands out a counted resource
}
uint32_t FindSegment(Obj *, uint32_t *a) // the manager's slot 34: (key, &segment), counted
{
    LogKeyAt(a[0]);
    if (Draw(4) == 0) return 0x200;
    const unsigned which = kSeg0 + Draw(kSegs);
    ++F(which).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(which);
    return 1;
}
uint32_t KeyInto(Obj *self, uint32_t *a) // slot 3: the key into *a[0]
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), self->value, sizeof(PatternKey));
    return a[0];
}
uint32_t KeyFrom(Obj *self, uint32_t *a) // slot 4: the key taken
{
    LogKeyAt(a[0]);
    std::memcpy(self->value, reinterpret_cast<const void *>(uintptr_t(a[0])), sizeof(PatternKey));
    return 1;
}
uint32_t Kind(Obj *self, uint32_t *) // a resource's slot 13: its kind, now and then another
{
    return Draw(4) == 0 ? Draw(8) : self->value[4];
}
uint32_t TypedFace(Obj *self, uint32_t *a) // a resource's slot 0: (iid, &out): its typed face, counted
{
    const unsigned which = kTyped0 + (self->id - 200 - kRes0);
    ++F(which).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(which);
    return Draw(2);
}
uint32_t NewGuid(Obj *, uint32_t *a) // the COM object's slot 7
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = 0x1000 + Draw(3);
    return 1;
}
uint32_t SegmentPath(Obj *self, uint32_t *a) // a segment's slot 11
{
    char text[16];
    std::snprintf(text, sizeof(text), "seg%u", self->id);
    reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString.assign(text);
    return 0;
}

// The manager's slot 18, (keys, filter): the filter asked about each pattern type and two others, counted and released
// through its own slots; then keys from the pool appended, the count answered.
// The key filter handed to slot 18 is held (one more reference) until the operation returns: then its count is logged
// (Load must have released its own) and the hold released, which deletes it on both sides.
void *gHeldFilter;
uint32_t __fastcall KeyListOf(Obj *self, void *, KeyList *keys, void *filter)
{
    if (!gHeldFilter) {
        gHeldFilter = filter;
        Slot<uint32_t>(filter, 1);
    }
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(18);
    Ev(uint32_t(keys->size()));
    Ev(reinterpret_cast<const uint32_t *>(filter)[1]); // its count
    static const uint32_t kTypes[] = {0xcca8e925, 0xaca8ea06, 0x2cb230b8, 0x6d619378, 0x4dcadb7e, 0, 0xac8ce8fa};
    for (uint32_t type : kTypes) {
        const uint32_t key[4] = {type, 1, 2, 3};
        Ev(Slot<uint32_t>(filter, 3, static_cast<const uint32_t *>(key)) & 0xff);
    }
    void *q = nullptr;
    static const uint32_t kIids[] = {1, 0x8865cd89, 0x12345678};
    const uint32_t ok = Slot<uint32_t>(filter, 0, kIids[Draw(3)], &q) & 0xff;
    Ev(ok);
    Ev(q == filter);
    if (ok) Ev(Slot<uint32_t>(filter, 2));
    const unsigned n = Draw(5);
    for (unsigned i = 0; i < n; ++i) keys->push_back(gKeys[Draw(8)]);
    return n;
}

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
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
uint32_t __fastcall RegisterFactory(Obj *self, void *, uint32_t kind, uint32_t type, const uint32_t *factory)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(20);
    Ev(kind);
    Ev(type);
    Ev(factory == gLastFactory);
    gLastFactory = factory;
    gFactory = factory;
    Ev(factory[0] - P(GameAddress<void>(0)));
    Ev(factory[1] - P(GameAddress<void>(0)));
    Ev(factory[2]);
    return Draw(6) == 0 ? 0x300u : 1u;
}

// ---- stand-ins ----

void LogPath(const cRZString *path)
{
    Ev(uint32_t(path->mString.size()));
    for (char c : path->mString) Ev(uint8_t(c));
}
template <uint32_t Rva> uint32_t __cdecl StandFile(const cRZString *path)
{
    Ev(Rva);
    LogPath(path);
    return 0x700u | Draw(2);
}

// ---- the world for a seed ----

void ConfigureFakes(uint32_t seed)
{
    std::mt19937 r(seed * 7 + 3);
    svcfx::ResetAll();
    svcfx::AddRange(gKeys, sizeof(gKeys), kKeyTag);
    svcfx::gFrameworkIndex = kCom;
    F(kFw).Set(5, "pdp", 0, &GetService);
    F(kMgr).Set(3, "pdp--", 0, &GetResource);
    F(kMgr).Set(8, "p", 0u);
    F(kMgr).Set(10, "p", 0, &LogKeyVoid);
    F(kMgr).Set(13, "pp", 0, &LogKeyBool);
    F(kMgr).Set(18, 2, 0u);
    F(kMgr).table[18] = reinterpret_cast<void *>(&KeyListOf);
    F(kMgr).Set(20, 3, 0u);
    F(kMgr).table[20] = reinterpret_cast<void *>(&RegisterFactory);
    F(kMgr).Set(34, "pp", 0, &FindSegment);
    gLastFactory = nullptr;
    gFactory = nullptr;
    F(kSys).Set(3, 0, &F(kSysMgr));
    F(kSys).Set(17, "p", 0, &LogKeyBool);
    F(kSys).Set(45, "p", 0, &DrawBool);
    F(kSysMgr).Set(30, "p", 1u);
    F(kCom).Set(7, "p", 0, &NewGuid);
    for (unsigned i = 0; i < kSegs; ++i) {
        Obj &o = F(kSeg0 + i);
        o.Set(5, "d-", 1u);
        o.Set(7, 0, 0, &DrawBool);
        o.Set(9, 0, 1u);
        o.Set(10, 0, 0, &DrawBool);
        o.Set(11, "p", 0, &SegmentPath);
        o.Set(20, "p", 0, &LogKeyBool);
    }
    static const uint32_t kTypes[] = {0, 0xcca8e925, 0x4dcadb7e, 0x1234};
    for (unsigned i = 0; i < kRes; ++i) {
        Obj &o = F(kRes0 + i);
        const PatternKey key = {kTypes[r() % 4], r() % 3 ? 0u : 7u, r() % 3 ? uint32_t(r() % 6) : 0u, uint32_t(r() % 2)};
        std::memcpy(o.value, &key, sizeof(key));
        o.value[4] = uint32_t(r() % 8); // kind
        o.Set(0, "dp", 0, &TypedFace);
        o.Set(3, "p", 0, &KeyInto);
        o.Set(4, "p", 0, &KeyFrom);
        o.Set(13, 0, 0, &Kind);
        F(kTyped0 + i).Set(3, 0, &o);
    }
}

// The key pool: the pattern types, others, instances 0..5.
void MakeKeys(uint32_t seed)
{
    std::mt19937 r(seed * 5 + 11);
    static const uint32_t kTypes[] = {0xcca8e925, 0xaca8ea06, 0x2cb230b8, 0x6d619378, 0x4dcadb7e, 0x12345678};
    for (PatternKey &k : gKeys) k = {kTypes[r() % 6], uint32_t(r() % 3), uint32_t(r() % 6), uint32_t(r() % 2)};
}

bool gNullDefault; // this seed has no default segment (AddPattern then always names one)

// The catalog's state (the same on both sides: written through our layout).
void Populate(Sys *s, uint32_t seed)
{
    std::mt19937 r(seed * 13 + 1);
    s->mCount = r() % 5;
    s->mLoaded = r() % 2;
    if (!gNullDefault) s->mDefaultSegment.Assign(reinterpret_cast<cIGZUnknown *>(&F(kSeg0 + r() % kSegs)));
    PatternMap *maps[] = {&s->mKind1, &s->mKind2, &s->mKind3, &s->mKind4, &s->mKind5, &s->mKind6};
    for (PatternMap *map : maps) {
        const unsigned n = r() % 4;
        for (unsigned i = 0; i < n; ++i) (*map)[uint32_t(r() % 6)].Assign(reinterpret_cast<cIGZUnknown *>(&F(kTyped0 + r() % kRes)));
    }
}

// ---- dumping ----

uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

void DumpList(Blob &b, const PatternList &v)
{
    b.push_back(uint32_t(v.size()));
    b.push_back(uint32_t(v.capacity()));
    for (void *p : v) b.push_back(Self(p));
}

bool gCreator;
void Dump(Blob &b, Sys *s, bool ours, bool fresh)
{
    static const uint32_t kFaces[3][2] = {{0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kCatalogVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(s, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 8; off < 0x18; off += 4) b.push_back(Word(s, off));
    b.push_back(Word(s, 0x18) & 0xff); // mRunning (its padding is the heap's under the creator)
    // mCount and the padding after mLoaded are never written by the constructor: 0xc5 fill, or the heap's under the creator.
    b.push_back(fresh && gCreator ? 0 : Word(s, 0x20));
    b.push_back(gCreator ? Word(s, 0x24) & 0xff : Word(s, 0x24));
    b.push_back(Self(s->mDefaultSegment.Get()));
    const PatternMap *maps[] = {&s->mKind1, &s->mKind2, &s->mKind3, &s->mKind4, &s->mKind5, &s->mKind6};
    for (const PatternMap *map : maps) {
        b.push_back(uint32_t(map->size()));
        for (const auto &[instance, pattern] : *map) b.insert(b.end(), {instance, Self(pattern.Get())});
    }
}

// ---- driving a catalog ----

Sys *Build(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gCreator) {
        void *made = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0xca27f0)();
        b.push_back(made != nullptr);
        return static_cast<Sys *>(made);
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(cTSPatternCatalogAdapters::Construct(memory, nullptr))
                   : ctx.Original<void *(__thiscall *)(void *)>(0xc9f9e0)(memory);
    b.push_back(r == memory);
    return static_cast<Sys *>(memory);
}

const uint32_t kIids[] = {1, Sys::kIid, 0x287fb697 /* cIGZSystemService */, 0x12345678};

void RunOp(Blob &b, Sys *s, unsigned op, std::mt19937 &r)
{
    void *const primary = s;
    void *const face = reinterpret_cast<uint8_t *>(s) + 0x1c;
    PatternList out;
    if (r() % 3 == 0) out.push_back(&F(kTyped0 + r() % kRes)); // a list the getters empty first
    switch (op) {
    case 0: { // QueryInterface on a face (+0, the +4 thunk 0xca3091, the +0x1c thunk 0xca3099), the result released
        static const uint32_t kFaceAt[] = {0, 4, 0x1c};
        void *at = reinterpret_cast<uint8_t *>(s) + kFaceAt[r() % 3];
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(at, 0, kIids[r() % 4], &q) & 0xff;
        b.push_back(ok);
        b.push_back(Self(q));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 1: { // Init; now and then without a framework, or with one that answers nothing
        gLastFactory = nullptr;
        const unsigned mode = r() % 6;
        void *&framework = *GameAddress<void *>(0x10c9378);
        if (mode == 0) framework = nullptr;
        gNoService = mode == 1;
        b.push_back(Slot<uint32_t>(primary, 8) & 0xff);
        framework = &F(kFw);
        gNoService = false;
        break;
    }
    case 2: b.push_back(Slot<uint32_t>(primary, 9) & 0xff); break; // Shutdown
    case 3: { // AddPattern: a pattern with or without a group, an instance, a type; a segment named or not
        Obj &pattern = F(kRes0 + r() % kRes);
        if (r() % 3 == 0) pattern.value[1] = 0;
        if (r() % 3 == 0) pattern.value[2] = 0;
        if (r() % 3 == 0) pattern.value[0] = 0;
        void *segment = gNullDefault || r() % 2 ? static_cast<void *>(&F(kSeg0 + r() % kSegs)) : nullptr;
        b.push_back(Slot<uint32_t>(face, 3, static_cast<void *>(&pattern), segment) & 0xff);
        break;
    }
    case 4: { // RemovePattern
        Obj &pattern = F(kRes0 + r() % kRes);
        if (r() % 4 == 0) pattern.value[1] = r() % 2 ? 0 : 5;
        if (r() % 4 == 0) pattern.value[2] = r() % 2 ? 0 : 4;
        b.push_back(Slot<uint32_t>(face, 4, static_cast<void *>(&pattern)) & 0xff);
        break;
    }
    case 5: b.push_back(Slot<uint32_t>(face, 5)); break; // Load
    case 6: Slot<void>(face, 6); break;                  // ClearAll
    case 7: {
        static const uint32_t kWhich[] = {0, 1, 2, 0xffffffff};
        Slot<void>(face, 7, &out, kWhich[r() % 4]);
        break;
    }
    case 8:
    case 9:
    case 10:
    case 11: Slot<void>(face, op, &out); break;
    default: break;
    }
    DumpList(b, out);
}

Blob RunSide(difftest::Context &ctx, uint32_t seed, bool ours)
{
    ConfigureFakes(seed);
    MakeKeys(seed);
    gDraw.seed(seed * 17 + 5);
    Blob b;
    Sys *s = Build(ctx, b, ours);
    svcfx::AddRange(s, Sys::kSize, kTag);
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    Dump(b, s, ours, true);
    Populate(s, seed);
    Slot<uint32_t>(s, 1); // counted twice: QueryInterface's references come and go without destroying it
    Slot<uint32_t>(s, 1);
    std::mt19937 r(seed * 31 + 7);
    const unsigned ops = 2 + unsigned(r() % 12);
    for (unsigned i = 0; i < ops; ++i) {
        // Every slot alike, with Shutdown / ClearAll (which empty everything) rarer.
        unsigned op = unsigned(r() % 12);
        if ((op == 2 || op == 6) && r() % 2) op = 7 + unsigned(r() % 5);
        char label[64];
        std::snprintf(label, sizeof(label), "seed %u %s op %u slot %u", seed, ours ? "ours" : "orig", i, op);
        ctx.Mark(label);
        if (svcfx::gTrace) fprintf(stderr, "%s\n", label);
        b.push_back(0xa0000000u | op);
        RunOp(b, s, op, r);
        if (gHeldFilter) {
            b.push_back(0xf1170000u | reinterpret_cast<const uint32_t *>(gHeldFilter)[1]);
            b.push_back(Slot<uint32_t>(gHeldFilter, 2));
            gHeldFilter = nullptr;
        }
        b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
        svcfx::gEv.clear();
        Dump(b, s, ours, false);
        svcfx::CaptureFakes(b, kValueKinds);
    }
    // The last Release destroys it (cRZUnknownMT's Release deletes at a count of 1 or below).
    while (Word(s, 8) > 1) b.push_back(Slot<uint32_t>(s, 2));
    b.push_back(Slot<uint32_t>(s, 2));
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b, kValueKinds);
    return b;
}

} // namespace patcat_test

DIFF_TEST(PatternCatalog_Run, 0xc9f9e0)
{
    using namespace patcat_test;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.Set(0x10c9378, &F(kFw)); // the framework (GetServiceObject, src/rz/Singleton_20ddfb.h)
    shape::CodePatch gameNew(0x824632, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch gameDelete(0x824624, reinterpret_cast<const void *>(&GameDelete));
    shape::CodePatch canWrite(0x1c2182, reinterpret_cast<const void *>(&StandFile<0x1c2182>));
    shape::StandIn deleteFile(0x1c2e25, &PathDelete, &StandFile<0x1c2e25>);
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        gCreator = seed % 5 == 4;
        gNullDefault = seed % 4 == 1;
        const Blob original = RunSide(ctx, seed, false);
        const Blob ours = RunSide(ctx, seed, true);
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
