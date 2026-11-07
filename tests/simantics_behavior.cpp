// cTSBehavior (lane GU): differential tests in the standalone shape (docs/standalone-shape.md, "Tests").
//
// Two routes per case, each on its own behaviour object seeded alike in a fake world rebuilt from the case's seed:
//   route 0: the object built by the original factory 0x469e70 (game vtable), the original function called at its RVA;
//   route 1: the object built by our factory (cTSBehavior::Create), the function called through our vtable, in the slot where
//            the game's vtable holds that RVA (shape::SlotOf).
// Calls the bodies make on `this` go through the object's own vtable, so route 0 runs original code throughout and route 1
// ours. The fake world: tree sources, the primitive set, trees and their nodes, the resource cache (GetCachedSingletonC's
// cache 0x10cc290), the resource service (cache 0x10faf6c) and its database, the framework (through the COM director's vtable
// at 0x10c9390), records, string sets, constants and the checker. Each fake logs every call (its id, slot, arguments with
// pointers mapped to ids, the key words where a slot takes a key) and answers from the seed; out-parameters are written
// whole. Compared: the result (masked to the slot's return width), the log, the object's fields, every fake's reference
// count, the strings handed in, and the static breakpoint list (the game's vector at 0x10da9f4: text, ids, nodes, size,
// capacity). Route 1 also checks AdaptersRoute (a third route) for the hooks kept as adapters.
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cIGZString.h"
#include "shape.h"
#include "simantics/cTSBehavior.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <utility>
#include <vector>

namespace {
namespace gu {
using B = cTSBehavior;
using Blob = std::vector<uint32_t>;
using shape::P;

constexpr uint32_t kFactory = 0x469e70;
constexpr uint32_t kCacheC = 0x10cc290;      // GetCachedSingletonC's cache
constexpr uint32_t kServiceCache = 0x10faf6c; // CachedServiceGetter<0x10faf6c, ...>'s cache
constexpr uint32_t kDirector = 0x10c9390, kDirectorGuard = 0x10c93f4; // the COM director singleton (vtable word) and its guard
constexpr uint32_t kSizeGuardA = 0x10daa0c, kSizeGuardB = 0x10daa1c;  // slot 30's thread-safe static guards
constexpr uint32_t kSizeVecA = 0x10daa00, kSizeVecB = 0x10daa10;      // slot 30's static vectors (begin, end, capacity)

// ---- fakes ----

enum Kind { kSource, kPrims, kTree, kNode, kCache, kService, kDb, kRecord, kPersist, kStrings, kConstants, kChecker, kFramework,
            kTarget, kKinds };
struct SlotSpec {
    unsigned slot, words;
    int keyArg; // the argument that points at a 16-byte key (logged), -1 none
};
const std::vector<SlotSpec> kSpecs[kKinds] = {
    /* kSource */ {{1, 0, -1}, {2, 0, -1}, {11, 0, -1}, {14, 1, -1}, {16, 0, -1}, {19, 3, -1}, {22, 3, -1}, {23, 2, -1}},
    /* kPrims */ {{1, 0, -1}, {2, 0, -1}, {5, 4, -1}, {6, 4, -1}, {7, 2, -1}},
    /* kTree */ {{0, 2, -1}, {1, 0, -1}, {2, 0, -1}, {8, 0, -1}, {20, 0, -1}, {22, 1, -1}, {30, 1, -1}, {32, 0, -1}, {33, 1, -1}},
    /* kNode */ {{1, 0, -1}, {2, 0, -1}, {3, 0, -1}, {15, 1, -1}, {19, 1, -1}},
    /* kCache */ {{72, 3, 0}},
    /* kService */ {{3, 0, -1}, {5, 5, 0}, {16, 2, -1}, {28, 1, -1}, {34, 3, 0}},
    /* kDb */ {{7, 5, 0}, {10, 1, 0}, {34, 2, 0}},
    /* kRecord */ {{1, 0, -1}, {2, 0, -1}, {26, 1, 0}},
    /* kPersist */ {{1, 0, -1}, {2, 0, -1}, {4, 1, 0}},
    /* kStrings */ {{1, 0, -1}, {2, 0, -1}, {5, 1, -1}},
    /* kConstants */ {{1, 0, -1}, {2, 0, -1}, {13, 1, -1}},
    /* kChecker */ {{1, 0, -1}, {2, 0, -1}, {5, 2, -1}, {9, 2, -1}},
    /* kFramework */ {{3, 3, -1}},
    /* kTarget */ {{3, 0, -1}},
};
constexpr unsigned kSlots = 80;

struct Fake {
    void **vt;
    uint32_t id;
    Kind kind;
    uint32_t refs;
    uint32_t ret[kSlots];
    bool writes[kSlots];
    char name[12];
};

constexpr unsigned kSources = 4, kTrees = 6, kNodes = 10;
struct World {
    Fake sources[kSources], prims, trees[kTrees], nodes[kNodes], cache, service, db, record, persist, strings, constants, checker,
        framework, target;
    uint32_t treeNodes[kTrees]; // node count per tree (slot 20)
    uint32_t nodeOperands[kNodes][6];
};
World gW;
void *gVTables[kKinds][kSlots];
union TreeName { cRZString s; TreeName() {} ~TreeName() {} };
TreeName gTreeNames[kTrees];
Blob gLog;
uintptr_t gStackTop;
const void *gThis;           // the behaviour object under test
cRZString *gStrings[4];      // strings the case hands in
bool gFailedSlot;

Fake *AllFakes(unsigned &count)
{
    count = sizeof(World) / sizeof(Fake);
    return reinterpret_cast<Fake *>(&gW);
}
static_assert(offsetof(World, treeNodes) == (kSources + 1 + kTrees + kNodes + 10) * sizeof(Fake), "fakes first");

uint32_t Map(uint32_t v)
{
    unsigned count;
    Fake *all = AllFakes(count);
    for (unsigned i = 0; i < count; ++i)
        if (v == P(&all[i])) return 0xF0000000u | all[i].id;
    if (v && v == P(gThis)) return 0xB0B0B0B0u;
    for (unsigned i = 0; i < 4; ++i)
        if (gStrings[i] && v == P(gStrings[i])) return 0x57000000u | i;
    for (unsigned i = 0; i < kTrees; ++i)
        if (v == P(&gTreeNames[i].s)) return 0x5A000000u | i;
    uint32_t here = 0;
    if (v >= P(&here) && v < gStackTop) return 0x5AC0u;
    return v;
}

void Text(Fake *self, void *out, unsigned salt)
{
    char text[24];
    std::snprintf(text, sizeof text, "%s-%u", self->name, salt);
    static_cast<cIGZString *>(out)->FromChar(text);
}

template <class T> T *AddRefd(T *p)
{
    if (p) ++p->refs;
    return p;
}

uint32_t Handle(Fake *self, unsigned slot, const uint32_t *a, unsigned n)
{
    gLog.insert(gLog.end(), {0xCA11u, self->id, slot});
    int keyArg = -1;
    for (const SlotSpec &s : kSpecs[self->kind])
        if (s.slot == slot) keyArg = s.keyArg;
    for (unsigned i = 0; i < n; ++i) {
        gLog.push_back(Map(a[i]));
        if (int(i) == keyArg) {
            const uint32_t *key = reinterpret_cast<const uint32_t *>(a[i]);
            gLog.insert(gLog.end(), {key[0], key[1], key[2], key[3]});
        }
    }
    const uint32_t r = self->ret[slot];
    const bool w = self->writes[slot];
    if (slot == 1 && self->kind != kCache && self->kind != kService && self->kind != kDb) return ++self->refs;
    if (slot == 2 && self->kind != kCache && self->kind != kService && self->kind != kDb) return --self->refs;
    switch (self->kind) {
    case kSource:
        if (slot == 14) Text(self, reinterpret_cast<void *>(a[0]), 0);
        if (slot == 19 && w) *reinterpret_cast<Fake **>(a[2]) = AddRefd(r & 0x10 ? &gW.record : nullptr);
        // FindTree (0x46c0c5) tests only the low word of the answer: 0x10000 ("not found" with upper bits) comes up too.
        if (slot == 22 && w) *reinterpret_cast<uint32_t *>(a[2]) = (r & 0x10001) ^ 1;
        if (slot == 23) Text(self, reinterpret_cast<void *>(a[1]), a[0] & 0xff);
        return r;
    case kPrims:
        if (slot == 5 && w) Text(self, reinterpret_cast<void *>(a[3]), 5);
        if (slot == 6) Text(self, reinterpret_cast<void *>(a[3]), 6);
        if (slot == 7) Text(self, reinterpret_cast<void *>(a[1]), a[0] & 0xffff);
        return r;
    case kTree: {
        const unsigned t = unsigned(self - gW.trees);
        if (slot == 0) *reinterpret_cast<Fake **>(a[1]) = AddRefd(&gW.persist);
        if (slot == 20) return (r & 0xffff0000u) | gW.treeNodes[t];
        if (slot == 22) return P(&gW.nodes[(t * 3 + a[0]) % kNodes]);
        if (slot == 32) return P(&gTreeNames[t].s);
        return r;
    }
    case kNode:
        if (slot == 15) {
            uint32_t *out = reinterpret_cast<uint32_t *>(a[0]);
            std::memcpy(out, gW.nodeOperands[self - gW.nodes], 0x18);
            return a[0];
        }
        return r;
    case kCache: {
        const uint32_t instance = reinterpret_cast<const uint32_t *>(a[0])[2];
        // A found key (al != 0) always comes with its resource: GetTree (0x46a22b) uses it without a null test. Not found
        // answers depend on the instance id (instance % 7 == 6 misses) and never write.
        if (instance % 7 == 6) return r & ~0xffu;
        Fake *answer = &gW.trees[instance % kTrees];
        if (reinterpret_cast<const uint32_t *>(a[0])[0] == B::kConstantsType) answer = &gW.constants;
        if (r & 0xff) *reinterpret_cast<Fake **>(a[2]) = AddRefd(answer);
        (void)w;
        return r;
    }
    case kService:
        if (slot == 3) return P(&gW.db);
        if (slot == 5 && (r & 0xff)) *reinterpret_cast<Fake **>(a[2]) = AddRefd(&gW.strings); // GetStringCount (0x469e3e) uses it untested
        if (slot == 34 && (r & 0xff)) *reinterpret_cast<Fake **>(a[2]) = AddRefd(&gW.record); // found: always a record
        return r;
    case kDb:
        if (slot == 7) *reinterpret_cast<Fake **>(a[2]) = AddRefd(&gW.trees[a[1] % kTrees]); // always (CreateTree uses it)
        if (slot == 34 && (r & 0xff)) *reinterpret_cast<Fake **>(a[1]) = AddRefd(&gW.record); // UncacheTree (0x46dccd) uses it untested
        return r;
    case kConstants:
        if (slot == 13) Text(self, reinterpret_cast<void *>(a[0]), 13);
        return r;
    case kFramework:
        if (slot == 3) *reinterpret_cast<Fake **>(a[2]) = AddRefd(&gW.checker); // always (CheckFramework uses it)
        return r;
    default: return r;
    }
}

template <unsigned S, unsigned... I> uint32_t __fastcall SlotThunk(Fake *self, void *, shape::StackWord<I>... w)
{
    const uint32_t args[] = {0u, static_cast<uint32_t>(w)...};
    return Handle(self, S, args + 1, sizeof...(I));
}
template <unsigned S, unsigned... I> void *ThunkAddress(std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<void *>(&SlotThunk<S, I...>);
}
template <unsigned S> void *ThunkFor(unsigned words)
{
    switch (words) {
    case 0: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 0>{});
    case 1: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 1>{});
    case 2: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 2>{});
    case 3: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 3>{});
    case 4: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 4>{});
    default: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 5>{});
    }
}
template <unsigned... S> void *ThunkOf(unsigned slot, unsigned words, std::integer_sequence<unsigned, S...>)
{
    void *result = nullptr;
    ((slot == S ? (result = ThunkFor<S>(words), 0) : 0), ...);
    return result;
}
uint32_t BadSlot(void *, unsigned) { gFailedSlot = true; return 0; }

void *__fastcall DirectorFramework(void *, void *) { return &gW.framework; }
void *gDirectorVt[16];

void BuildVTables()
{
    static bool built;
    if (built) return;
    built = true;
    for (unsigned k = 0; k < kKinds; ++k) {
        for (unsigned s = 0; s < kSlots; ++s) gVTables[k][s] = shape::TrapFor<BadSlot>(0);
        for (const SlotSpec &spec : kSpecs[k])
            gVTables[k][spec.slot] = ThunkOf(spec.slot, spec.words, std::make_integer_sequence<unsigned, kSlots>{});
    }
    for (void *&slot : gDirectorVt) slot = shape::TrapFor<BadSlot>(0);
    gDirectorVt[12] = reinterpret_cast<void *>(&DirectorFramework);
}

void Init(Fake &f, Kind kind, uint32_t id, std::mt19937 &r)
{
    f.vt = gVTables[kind];
    f.id = id;
    f.kind = kind;
    f.refs = 1 + r() % 3;
    for (unsigned s = 0; s < kSlots; ++s) {
        // Byte-valued answers include 0x100 / 0xff01 (al 0 or 1 with the upper bits set): a caller reading eax shows.
        const uint32_t pick = r() % 8;
        f.ret[s] = pick < 3 ? 0u : pick < 6 ? 1u : pick == 6 ? 0x100u : (r() | 1u);
        f.writes[s] = r() % 5 != 0;
    }
    std::snprintf(f.name, sizeof f.name, "f%u", id % 3);
}

void BuildWorld(uint32_t seed)
{
    BuildVTables();
    std::mt19937 r(seed);
    uint32_t id = 1;
    for (Fake &f : gW.sources) Init(f, kSource, id++, r);
    Init(gW.prims, kPrims, id++, r);
    for (Fake &f : gW.trees) Init(f, kTree, id++, r);
    for (Fake &f : gW.nodes) Init(f, kNode, id++, r);
    Init(gW.cache, kCache, id++, r);
    Init(gW.service, kService, id++, r);
    Init(gW.db, kDb, id++, r);
    Init(gW.record, kRecord, id++, r);
    Init(gW.persist, kPersist, id++, r);
    Init(gW.strings, kStrings, id++, r);
    Init(gW.constants, kConstants, id++, r);
    Init(gW.checker, kChecker, id++, r);
    Init(gW.framework, kFramework, id++, r);
    Init(gW.target, kTarget, id++, r);
    for (unsigned t = 0; t < kTrees; ++t) gW.treeNodes[t] = r() % 5 == 0 ? 0u : r() % 7;
    for (unsigned i = 0; i < kNodes; ++i) {
        uint32_t *o = gW.nodeOperands[i];
        for (unsigned j = 0; j < 6; ++j) o[j] = r();
        if (r() % 3 == 0) o[1] = o[2] = 0xffffffffu;
        else if (r() % 3 == 0) o[1] = 0xffffffffu;
        // node opcodes: primitives, tree calls into the fake trees' ids, and odd ones
        const uint32_t pick = r() % 4;
        gW.nodes[i].ret[3] = pick == 0 ? r() % 0x100 : pick == 1 ? 0x100 + r() % 8 : pick == 2 ? 0x1000 + r() % 8 : r();
    }
    for (unsigned t = 0; t < kTrees; ++t) {
        char text[16];
        std::snprintf(text, sizeof text, "tree%u", t);
        gTreeNames[t].s.Construct(text);
    }
}

void TearDownWorld()
{
    for (TreeName &n : gTreeNames) n.s.Destruct();
}

// The world's globals, saved and restored around a case.
struct Globals {
    uint32_t saved[8];
    const uint32_t rvas[8] = {kCacheC, kServiceCache, kDirector, kDirectorGuard, kSizeGuardA, kSizeGuardB, 0, 0};
    Globals()
    {
        for (unsigned i = 0; i < 6; ++i) saved[i] = *GameAddress<uint32_t>(rvas[i]);
        *GameAddress<void *>(kCacheC) = &gW.cache;
        *GameAddress<void *>(kServiceCache) = &gW.service;
        *GameAddress<void *>(kDirector) = gDirectorVt;
        *GameAddress<int32_t>(kDirectorGuard) = INT32_MIN;
        *GameAddress<int32_t>(kSizeGuardA) = INT32_MIN; // the statics count as built: the init path needs the CRT
        *GameAddress<int32_t>(kSizeGuardB) = INT32_MIN;
    }
    ~Globals()
    {
        for (unsigned rva : {kSizeVecA, kSizeVecB}) {
            uint32_t *v = GameAddress<uint32_t>(rva);
            if (v[0]) ::operator delete(reinterpret_cast<void *>(v[0]));
            v[0] = v[1] = v[2] = 0;
        }
        for (unsigned i = 0; i < 6; ++i) *GameAddress<uint32_t>(rvas[i]) = saved[i];
    }
};

// ---- breakpoints ----

void SeedBreakpoints(std::mt19937 &r)
{
    cTSBehaviorBreakpoints &list = B::Breakpoints();
    const uint32_t count = r() % 3 == 0 ? 0 : r() % 7;
    for (uint32_t i = 0; i < count; ++i) {
        cTSBehaviorBreakpoint bp;
        char text[12];
        std::snprintf(text, sizeof text, r() % 4 == 0 ? "zz" : "f%u-0", r() % 3);
        bp.mName.mString = text;
        const uint32_t pick = r() % 4;
        bp.mTreeId = pick == 0 ? int32_t(0x100 + r() % 8) : pick == 1 ? int32_t(0x1000 + r() % 8) : pick == 2 ? int32_t(0x2000 + r() % 4) : int32_t(r() % 0x3000);
        bp.mNode = r() % 5 == 0 ? -1 : int32_t(r() % 8);
        list.push_back(bp);
    }
}

void CaptureBreakpoints(Blob &out)
{
    cTSBehaviorBreakpoints &list = B::Breakpoints();
    out.insert(out.end(), {0xB9B9u, uint32_t(list.size()), uint32_t(list.capacity())});
    for (const cTSBehaviorBreakpoint &bp : list) {
        out.insert(out.end(), {P(bp.mName.mVTable) == P(GameAddress<void>(cRZString::kVTable)) ? 1u : 0u, bp.mName.mRefCount,
                               uint32_t(bp.mTreeId), uint32_t(bp.mNode), uint32_t(bp.mName.mString.size())});
        for (char c : bp.mName.mString) out.push_back(uint8_t(c));
    }
    cTSBehaviorBreakpoints().swap(list);
}

// ---- the object ----

void Seed(B *b, std::mt19937 &r)
{
    b->mRefCount = 1 + r() % 3;
    Fake *pick[3];
    for (Fake *&f : pick) f = r() % 5 == 0 ? nullptr : &gW.sources[r() % kSources];
    b->mGlobalSource = reinterpret_cast<cTSBehaviorSource *>(pick[0]);
    b->mSemiGlobalSource = reinterpret_cast<cTSBehaviorSource *>(pick[1]);
    b->mPrivateSource = reinterpret_cast<cTSBehaviorSource *>(pick[2]);
    for (Fake *f : pick) AddRefd(f);
    b->mGlobalGroup = r();
    b->mSemiGlobalGroup = r();
    b->mPrivateGroup = r();
    b->mOwner = r();
    b->mPrimitives = r() % 6 == 0 ? nullptr : reinterpret_cast<cIGZUnknown *>(AddRefd(&gW.prims));
}

void CaptureObject(Blob &out, const B *b)
{
    for (uint32_t off = 4; off < B::kSize; off += 4) out.push_back(Map(shape::Word(b, off)));
}

void CaptureWorld(Blob &out)
{
    unsigned count;
    Fake *all = AllFakes(count);
    out.push_back(0xFEFEu);
    for (unsigned i = 0; i < count; ++i) out.push_back(all[i].refs);
    for (cRZString *s : gStrings)
        if (s) {
            out.push_back(uint32_t(s->mString.size()));
            for (char c : s->mString) out.push_back(uint8_t(c));
        }
}

unsigned Slot(uint32_t rva)
{
    const unsigned slot = shape::SlotOf(B::kOriginalVTable, B::kVTableSlots, rva);
    if (slot == ~0u) {
        std::printf("  no slot holds 0x%x\n", rva);
        std::abort();
    }
    return slot;
}

// Both routes call through the object's own vtable: the game's (slot n holds the RVA) on route 0, ours on route 1.
template <class... Args> uint32_t CallAt(void *b, unsigned slot, Args... args)
{
    using Fn = uint32_t(__thiscall *)(void *, Args...);
    return (*static_cast<Fn *const *>(b))[slot](b, args...);
}
// Route 2 (the adapter route, for the slots whose RVA hook stays: the byte matches): the object built by the original
// factory carries a copy of the game's vtable with an arity-matched trap in the case's slot, and the hook's adapter is called
// (an adapter that dispatched virtually lands in the trap). Other cases run route 2 as route 0 and are not compared.
using A = cTSBehaviorAdapters;
struct AdapterOf {
    uint32_t rva;
    void *adapter;
};
const AdapterOf kAdapters[] = {
    {0x46d020, reinterpret_cast<void *>(&A::QueryInterface)},
    {0x46d7c0, reinterpret_cast<void *>(&A::ApplyAllBreakpoints)},
    {0x46e0e0, reinterpret_cast<void *>(&A::SetGlobalSource)},
    {0x46e2b0, reinterpret_cast<void *>(&A::SetSemiGlobalSource)},
    {0x46e270, reinterpret_cast<void *>(&A::SetPrivateSource)},
    {0x46a730, reinterpret_cast<void *>(&A::GetGroupForId)},
    {0x46bd90, reinterpret_cast<void *>(&A::GetSourceForId)},
    {0x46be30, reinterpret_cast<void *>(&A::GetSourceName)},
    {0x46bc50, reinterpret_cast<void *>(&A::DescribePrimitive)},
    {0x46c3e0, reinterpret_cast<void *>(&A::LoadBreakpoints)},
    {0x46c000, reinterpret_cast<void *>(&A::GetKindForId)},
};
void *gTrapVT[B::kVTableSlots];
bool gAdapterCalled;
uint32_t AdapterTrap(void *, unsigned)
{
    gLog.push_back(0x7A7A7A7Au);
    return 0;
}

template <class... Args> uint32_t CallRoute(difftest::Context &, int route, void *b, uint32_t rva, Args... args)
{
    const unsigned slot = Slot(rva);
    if (route == 2) {
        for (const AdapterOf &a : kAdapters) {
            if (a.rva != rva) continue;
            std::memcpy(gTrapVT, GameAddress<void>(B::kOriginalVTable), sizeof gTrapVT);
            gTrapVT[slot] = shape::TrapFor<&AdapterTrap>(sizeof...(Args));
            *static_cast<void **>(b) = gTrapVT;
            gAdapterCalled = true;
            using Fn = uint32_t(__fastcall *)(void *, void *, Args...);
            return reinterpret_cast<Fn>(a.adapter)(b, nullptr, args...);
        }
    }
    return CallAt(b, slot, args...);
}

// One case: the world and the object from `seed`, then `call(route, object, r)` with r drawn alike on both routes; the
// result masked by `mask`.
using Call = std::function<uint32_t(int route, B *b, std::mt19937 &r)>;

Blob RunRoute(difftest::Context &ctx, int route, uint32_t seed, uint32_t mask, const Call &call)
{
    BuildWorld(seed);
    Globals globals;
    std::mt19937 r(seed ^ 0x9e3779b9u);
    SeedBreakpoints(r);
    B *b = route == 1 ? B::Create() : static_cast<B *>(ctx.Original<void *(__cdecl *)()>(kFactory)());
    Seed(b, r);
    for (cRZString *&s : gStrings) {
        s = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
        s->Construct();
        if (r() % 2) s->mString.assign(r() % 20, char('a' + r() % 26));
    }
    gThis = b;
    uint32_t top = 0;
    gStackTop = P(&top);
    gLog.clear();
    gFailedSlot = false;
    const uint32_t result = call(route, b, r);
    Blob out = gLog;
    out.push_back(0xAAAAu);
    out.push_back(result & mask);
    out.push_back(gFailedSlot);
    CaptureObject(out, b);
    CaptureWorld(out);
    CaptureBreakpoints(out);
    for (cRZString *&s : gStrings) {
        s->Destruct();
        ::operator delete(s);
        s = nullptr;
    }
    ::operator delete(b); // the members are fakes: nothing else to free
    TearDownWorld();
    return out;
}

void Compare(difftest::Context &ctx, const char *name, uint32_t seed, const Blob &a, const Blob &b)
{
    if (a == b) {
        ++ctx.cases;
        return;
    }
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;    ctx.Fail("%s seed %u: differs at word %zu (%zu / %zu words): %08x / %08x", name, seed, at, a.size(), b.size(),
             at < a.size() ? a[at] : 0u, at < b.size() ? b[at] : 0u);
}

void Run(difftest::Context &ctx, const char *name, uint32_t seeds, uint32_t mask, const Call &call)
{
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        const Blob original = RunRoute(ctx, 0, seed, mask, call);
        const Blob ours = RunRoute(ctx, 1, seed, mask, call);
        Compare(ctx, name, seed, original, ours);
        gAdapterCalled = false;
        const Blob adapter = RunRoute(ctx, 2, seed, mask, call);
        if (gAdapterCalled) Compare(ctx, (std::string(name) + " (adapter)").c_str(), seed, original, adapter);
        if (ctx.failures.size() > 8) return;
    }
}

uint32_t TreeId(std::mt19937 &r)
{
    switch (r() % 10) {
    case 8:
    case 9: { // the edges of the id ranges exactly (16-bit compares), with and without upper bits
        const uint32_t edges[] = {0xff, 0x100, 0xfff, 0x1000, 0x1fff, 0x2000, 0x2710, 0x2711, 0x7fff, 0x8000, 0xffff, 0};
        const uint32_t edge = edges[r() % 12];
        return r() % 3 == 0 ? (r() & 0xffff0000u) | edge : edge;
    }
    case 0: return r() % 0x100;
    case 1: return 0x100 + r() % 8;
    case 2: return 0x1000 + r() % 8;
    case 3: return 0x2000 + r() % 8;
    case 4: return r();
    case 5: return 0x7fff;
    case 6: return 0xfff + r() % 3;
    default: return (r() & 0xffff0000u) | (0x2700 + r() % 32);
    }
}
// A kind argument: 0..4, and the edges of its 16-bit signed switch (-1, 0x8000, upper bits over 1 and 3).
uint32_t Kind(std::mt19937 &r)
{
    const uint32_t kinds[] = {0, 1, 2, 3, 4, 0xffff, 0xffffffffu, 0x8000, 0x10001, 0xffff0003u};
    return r() % 3 ? r() % 5 : kinds[r() % 10];
}
uint32_t Word(std::mt19937 &r) { return r() % 3 == 0 ? r() % 6 : r(); }
uint32_t Flag(std::mt19937 &r) { return r() % 3 == 0 ? 0u : r() % 2 ? 1u : (r() & 0xffffff00u) | 1u; }
uint32_t Str(std::mt19937 &r) { return P(gStrings[r() % 4]); }
uint32_t Src(std::mt19937 &r) { return r() % 4 == 0 ? 0u : P(&gW.sources[r() % kSources]); }

} // namespace gu
} // namespace

using namespace gu;

// Small slots: QueryInterface, AddRef/Release above zero, the source setters and getters, ids, kinds, names of sources.
DIFF_TEST(Behavior_Slots, 0x46d020)
{
    struct Case {
        uint32_t rva;
        uint32_t mask;
        Call call;
    };
    const Case cases[] = {
        {0x46d020, 0xff, [&](int route, B *b, std::mt19937 &r) {
             static void *out;
             out = nullptr;
             const uint32_t iids[] = {1, B::kInterfaceId, r(), 0x1b269450};
             const uint32_t result = CallRoute(ctx, route, b, 0x46d020, iids[r() % 4], P(&out));
             gLog.push_back(Map(P(out)));
             return result;
         }},
        {0x46c350, 0xff, [&](int route, B *b, std::mt19937 &r) {
             const uint32_t prims = r() % 3 == 0 ? 0u : r() % 2 ? P(&gW.prims) : P(b->mPrimitives);
             return CallRoute(ctx, route, b, 0x46c350, Word(r), prims, Src(r), Src(r), Src(r));
         }},
        {0x46e0e0, 0, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46e0e0, Word(r), r() % 3 ? Src(r) : P(b->mGlobalSource)); }},
        {0x46e2b0, 0, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46e2b0, Word(r), r() % 3 ? Src(r) : P(b->mSemiGlobalSource)); }},
        {0x46e270, 0, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46e270, Word(r), r() % 3 ? Src(r) : P(b->mPrivateSource)); }},
        {0x46a730, ~0u, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46a730, TreeId(r)); }},
        {0x46bd90, ~0u, [&](int route, B *b, std::mt19937 &r) { return Map(CallRoute(ctx, route, b, 0x46bd90, TreeId(r))); }},
        {0x46a780, ~0u, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46a780, Kind(r)); }},
        {0x46bde0, ~0u, [&](int route, B *b, std::mt19937 &r) { return Map(CallRoute(ctx, route, b, 0x46bde0, Kind(r))); }},
        {0x46a700, 0xff, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46a700, Str(r)); }},
        {0x46be60, 0xff, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46be60, Str(r)); }},
        {0x46bd60, 0xff, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46bd60, Str(r)); }},
        {0x46be30, 0xff, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46be30, TreeId(r), r() % 5 ? Str(r) : 0u); }},
        {0x46a130, ~0u, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46a130, Kind(r)); }},
        {0x46a800, ~0u, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46a800, Kind(r)); }},
        {0x46a280, 0, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46a280, Kind(r), Str(r)); }},
        {0x46c000, ~0u, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46c000, TreeId(r)); }},
        {0x46bc50, 0, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46bc50, Word(r), Word(r), Str(r)); }},
        // folded slots: AddRef, Release above one, the getters, the empty slots, AsBehavior
        // (Release keeps the object: the count starts at 2 or more; the last reference is Behavior_Lifetime's.)
        {0x148fb0, ~0u, [&](int, B *b, std::mt19937 &r) {
             const unsigned folded[] = {1, 2, 9, 10, 11, 14, 15, 16, 28, 37, 38, 42, 48, 49};
             b->mRefCount = 2 + r() % 3;
             const unsigned slot = folded[r() % 14];
             gLog.push_back(slot);
             const uint32_t result = Map(CallAt(b, slot));
             return slot == 28 || slot == 42 ? 0u : result; // the empty slots return void (eax is the call target)
         }},
    };
    for (const Case &c : cases) Run(ctx, "slot", 80, c.mask, c.call);
}

// Trees and resources: GetTree, CreateTree, UncacheTree, SaveTree, ReloadTree, GetConstants, GetConstantsName,
// GetStringCount, CheckFramework, FindTree, GetTreeSize.
DIFF_TEST(Behavior_Trees, 0x46a1a0)
{
    static void *out;
    const std::pair<uint32_t, std::pair<uint32_t, Call>> cases[] = {
        {0x46a1a0, {~0u, [&](int route, B *b, std::mt19937 &r) { return Map(CallRoute(ctx, route, b, 0x46a1a0, TreeId(r))); }}},
        {0x469820, {0xff, [&](int route, B *b, std::mt19937 &r) {
             out = nullptr;
             const uint32_t result = CallRoute(ctx, route, b, 0x469820, TreeId(r), Word(r), P(&out), Word(r));
             gLog.push_back(Map(P(out)));
             return result;
         }}},
        {0x46dc40, {0xff, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46dc40, TreeId(r)); }}},
        {0x46dd10, {0xff, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46dd10, TreeId(r)); }}},
        {0x46dbe0, {0xff, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46dbe0, TreeId(r)); }}},
        {0x46a2a0, {~0u, [&](int route, B *b, std::mt19937 &r) {
             out = nullptr;
             const uint32_t result = CallRoute(ctx, route, b, 0x46a2a0, TreeId(r), Word(r), P(&out));
             gLog.push_back(Map(P(out)));
             return result;
         }}},
        {0x46a300, {0, [&](int route, B *b, std::mt19937 &r) {
             // the constants come from the resource cache (slot 72 answers a BCON key with the constants fake)
             return CallRoute(ctx, route, b, 0x46a300, TreeId(r), Str(r));
         }}},
        {0x469dd0, {0xffff, [&](int route, B *b, std::mt19937 &) { return CallRoute(ctx, route, b, 0x469dd0); }}},
        {0x46be90, {0xff, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46be90, Word(r), Word(r), Word(r)); }}},
        {0x46c010, {~0u, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46c010, TreeId(r), Flag(r), Flag(r)); }}},
        {0x46a3b0, {~0u, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46a3b0, TreeId(r)); }}},
    };
    for (const auto &c : cases) Run(ctx, "tree", 150, c.second.first, c.second.second);
}

// Names and text: GetTreeName, DescribeNode.
DIFF_TEST(Behavior_Names, 0x46c120)
{
    // Both name primitives through +0x24 without a null test (0x46c14c, and DescribeNode's primitive branch): Init always
    // sets it in game, so the cases here keep it.
    Run(ctx, "tree name", 300, 0, [&](int route, B *b, std::mt19937 &r) {
        if (!b->mPrimitives) b->mPrimitives = reinterpret_cast<cIGZUnknown *>(AddRefd(&gW.prims));
        return CallRoute(ctx, route, b, 0x46c120, TreeId(r), Str(r));
    });
    Run(ctx, "describe node", 300, 0, [&](int route, B *b, std::mt19937 &r) {
        if (!b->mPrimitives) b->mPrimitives = reinterpret_cast<cIGZUnknown *>(AddRefd(&gW.prims));
        return CallRoute(ctx, route, b, 0x46b950, Word(r), P(&gW.nodes[r() % kNodes]), Str(r));
    });
}

// Breakpoints: ApplyBreakpoints, ApplyAllBreakpoints, the per-source walk (static, by RVA and ours), SetBreakpoint, and Init
// (which applies them all).
DIFF_TEST(Behavior_Breakpoints, 0x46d7f0)
{
    Run(ctx, "apply", 300, 0xff, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46d7f0, TreeId(r)); });
    Run(ctx, "apply all", 300, 0, [&](int route, B *b, std::mt19937 &) { return CallRoute(ctx, route, b, 0x46d7c0); });
    Run(ctx, "source walk", 300, 0, [&](int route, B *b, std::mt19937 &r) {
        const uint32_t source = Src(r);
        if (route == 0) ctx.Original<void(__cdecl *)(void *, uint32_t)>(0x46da40)(b, source);
        else B::ApplySourceBreakpoints(b, reinterpret_cast<cTSBehaviorSource *>(source));
        return 0u;
    });
    Run(ctx, "set", 400, 0, [&](int route, B *b, std::mt19937 &r) {
        const uint32_t node = r() % 3 == 0 ? r() : r() % 8;
        return CallRoute(ctx, route, b, 0x46de90, TreeId(r), node, Flag(r));
    });
    // The tree and node of a breakpoint already in the list (removed when off, kept when on, if the source's name matches).
    Run(ctx, "set existing", 300, 0, [&](int route, B *b, std::mt19937 &r) {
        const cTSBehaviorBreakpoints &list = B::Breakpoints();
        uint32_t tree = TreeId(r), node = r() % 8;
        if (!list.empty()) {
            const cTSBehaviorBreakpoint &bp = list[r() % list.size()];
            tree = uint32_t(bp.mTreeId);
            node = uint32_t(bp.mNode) & 0xff;
        }
        return CallRoute(ctx, route, b, 0x46de90, tree, node, Flag(r));
    });
    Run(ctx, "init", 200, 0xff, [&](int route, B *b, std::mt19937 &r) {
        return CallRoute(ctx, route, b, 0x46c350, Word(r), P(&gW.prims), Src(r), Src(r), Src(r));
    });
}

// The static helpers by RVA against ours: KindName 0x46f1c0, KindForId 0x46f230.
DIFF_TEST(Behavior_Kinds, 0x46f230)
{
    for (uint32_t v = 0; v < 0x10000; v += 1) {
        const uint32_t word = v | (v * 0x9e3779b9u & 0xffff0000u);
        const uint32_t a = ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0x46f230)(word), b = B::KindForId(word);
        if (a != b) ctx.Fail("KindForId(0x%x): %u / %u", word, a, b);
        ++ctx.cases;
    }
    Run(ctx, "kind name", 40, 0, [&](int route, B *, std::mt19937 &r) {
        const uint32_t kind = r() % 3 ? r() % 6 : r();
        cIGZString *out = gStrings[r() % 4]->AsInterface();
        if (route == 0) ctx.Original<void(__cdecl *)(uint32_t, cIGZString *)>(0x46f1c0)(kind, out);
        else B::KindName(kind, out);
        return 0u;
    });
}

// The two slots that hand file work to original code (LoadBreakpoints 0x46c3e0 -> 0x46f290, DumpTrees 0x46c5a0 -> 0x46c610):
// the file functions stood in on both routes (one patch of the image function: ours calls it at that address), logging their
// arguments.
uint32_t __cdecl StandLoad(void *file)
{
    gLog.insert(gLog.end(), {0x10ADu, Map(P(file))});
    return 0;
}
void __cdecl StandDump(void *self, void *source, void *target)
{
    gLog.insert(gLog.end(), {0xD0D0u, Map(P(self)), Map(P(source)), Map(P(target))});
}
DIFF_TEST(Behavior_FileSlots, 0x46c5a0)
{
    shape::CodePatch load(0x46f290, reinterpret_cast<const void *>(&StandLoad));
    shape::CodePatch dump(0x46c610, reinterpret_cast<const void *>(&StandDump));
    Run(ctx, "load", 20, 0, [&](int route, B *b, std::mt19937 &r) { return CallRoute(ctx, route, b, 0x46c3e0, Word(r)); });
    Run(ctx, "dump", 60, 0, [&](int route, B *b, std::mt19937 &) { return CallRoute(ctx, route, b, 0x46c5a0, P(&gW.target)); });
}

// The factory 0x469e70 and the end of life: the new object's words (+0x0c and +0x20 left out: the factory never writes them,
// see its listing, so they hold what the heap had), Release of the last reference (slot 50 with the delete: the held pointers
// released in the original's order), and slot 50 with flags 0.
DIFF_TEST(Behavior_Lifetime, 0x469560)
{
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        Blob sides[2];
        for (int route = 0; route < 2; ++route) {
            BuildWorld(seed);
            Globals globals;
            std::mt19937 r(seed);
            B *b = route == 0 ? static_cast<B *>(ctx.Original<void *(__cdecl *)()>(kFactory)()) : B::Create();
            Blob &out = sides[route];
            for (uint32_t off = 4; off < B::kSize; off += 4)
                if (off != 0xc && off != 0x20) out.push_back(shape::Word(b, off));
            Seed(b, r);
            gThis = b;
            uint32_t top = 0;
            gStackTop = P(&top);
            gLog.clear();
            if (seed % 2) {
                b->mRefCount = r() % 2; // 0 and 1 both delete
                out.push_back(CallAt(b, 2));
            } else {
                const uint32_t result = CallAt(b, 50, 0u);
                out.push_back(result == P(b));
                ::operator delete(b);
            }
            out.insert(out.end(), gLog.begin(), gLog.end());
            CaptureWorld(out);
            TearDownWorld();
        }
        Compare(ctx, "lifetime", seed, sides[0], sides[1]);
    }
}
