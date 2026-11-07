// Differential tests for lane IV's part of cTSWantSimulator (src/ts/cTSWantSimulatorIV.cpp): Init (slot 3, 0x4905e0),
// Shutdown (slot 4, 0x496720) and AddCategoryWant (slot 5, 0x486430).
//
// Routes 0 and 1 of tests/ts_want_simulator_shape.h (these slots have no adapters): a simulator from our constructor
// carrying copies of the game's vtables with the original RVA called, against one carrying copies of ours with the slot
// called through our vtable. Every object the slots reach is a recording fake (tests/ts_services_fixture.h); the callees
// that are other classes' code are stood in for on both sides: the tree manager's creation (the original's constructor
// call 0x551c40 and our cTSWantTreeManager::Create), LoadTuning (0x494030, lane IL's, verified), the message-id walks
// (0x1c0458 / 0x1c081e, verified), GetOptionMask (0xb39380, verified), the cheat helper 0xb39640 and the want instance's
// pool (0x54e570) and constructor (0x54e2b0), both still the game's. The memory stores are real (their Clear / Destruct
// run: the image's on route 0, ours on route 1). Compared: the call log (arguments normalised: the simulator's own
// addresses as offsets), every fake's count, and a digest of what the slot changes.
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "shape.h"
#include "ts_services_fixture.h"
#include "ts_want_simulator_shape.h"

#include "app/AppInit.h"
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocate
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "ts/cTSWantSimulatorTuningLoader.h"
#include "ts/cTSWantTreeManager.h"

#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace wantiv {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{1, "d"}, {5, "pppppppp"}, {6, "p"}, {7, "p"}, {8, "p"}, {12, "ddddddd"}, {13, "ddddddd"}, {14, "ddddddd"}};

using namespace wantshape;
using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;

constexpr uint32_t kSimTag = 0x7b000000; // the simulator under test (its range)
constexpr uint32_t kBufferTag = 0x7c000000; // the want instance block the pool stand hands out

enum : unsigned {
    kGlobals = 0, kFramework = 1, kRegistry = 2, kManager = 3, kOldManager = 4, kTable = 5,
    kEntry0 = 6,  // 6..8: the table's entries
    kInner0 = 9,  // 9..11: what an entry's slot 1 returns
    kData0 = 12,  // 12..14: the definition data (slots 3 / 4)
    kDefinition = 15, kWant0 = 16, // 16..17: want instances
    kRef0 = 18,   // 18..23: counted objects in the simulator's tables
};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

// ---- stand-ins ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint32_t gOptionMask;
uint32_t __cdecl StandOptionMask()
{
    Ev(0xb39380);
    return gOptionMask;
}

// The tree manager: the original's `new` (0x44 bytes from GameNew) and constructor, ours Create; both give the manager fake.
void *__fastcall StandManagerConstruct(void *memory, void *)
{
    Ev(0x551c40);
    ::operator delete(memory);
    return &F(kManager);
}
cTSWantTreeManager *__cdecl StandManagerCreate()
{
    Ev(0x551c40);
    return reinterpret_cast<cTSWantTreeManager *>(&F(kManager));
}

void LogLoadTuning(void *simulator)
{
    Ev(0x494030);
    Ev(svcfx::Norm(P(simulator)));
}
void __fastcall StandLoadTuningImage(void *simulator, void *) { LogLoadTuning(simulator); }
void __cdecl StandLoadTuningOurs(cTSWantSimulator *simulator) { LogLoadTuning(simulator); }

// A cheat as handed over: its vtable's RVA and its two words, then freed (nobody else keeps it here).
void TakeCheat(uint32_t cheat)
{
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(cheat));
    Ev(words[0] - P(GameAddress<void>(0)));
    Ev(words[1]);
    Ev(words[2]);
    ::operator delete(reinterpret_cast<void *>(uintptr_t(cheat)));
}
void __cdecl StandCheatIfEnabled(uint32_t cheat)
{
    Ev(0xb39640);
    TakeCheat(cheat);
}
// The registry's slot 4 (add a command): logged like a generic slot, the cheat by content (its address is a heap block).
uint32_t __fastcall RegistryAdd(Obj *self, void *, uint32_t cheat)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(4);
    TakeCheat(cheat);
    return 0xabcd0001u;
}

uint32_t __fastcall StandWalk5(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c0458);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : reinterpret_cast<std::set<uint32_t> *>(self)[0]) Ev(id);
    return P(*static_cast<void **>(self)); // the real walk leaves the set's head in eax
}
void __fastcall StandWalk6(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c081e);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
}

// The want instance pool and constructor (game code on both sides).
alignas(8) uint8_t gBlock[0x3c];
bool gPoolNull;
void *__cdecl StandPool(uint32_t size)
{
    Ev(0x54e570);
    Ev(size);
    return gPoolNull ? nullptr : gBlock;
}
void *__fastcall StandWantConstruct(void *self, void *, int32_t sim, uint32_t guid, const WantInstanceParam *param, void *a, void *b)
{
    Ev(0x54e2b0);
    Ev(svcfx::Norm(P(self)));
    Ev(uint32_t(sim));
    Ev(guid);
    Ev(param->kind);
    Ev(param->value);
    Ev(uint16_t(param->extra));
    Ev(P(a));
    Ev(P(b));
    return &F(kWant0 + Draw(2));
}

// ---- fake handlers ----

uint32_t HandOut(uint32_t out, unsigned index)
{
    ++F(index).refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &F(index);
    return 1;
}
// Framework slot 5 GetSystemService(class, iid, out): value[0] = whether the cheat registry is there.
uint32_t GetSystemService(Obj *self, uint32_t *a)
{
    if (a[0] == 0x2a4915ea && self->value[0]) return HandOut(a[2], kRegistry);
    return 0;
}
// Slot 1 of an entry: what value[0] holds (not AddRef: these are no counted objects).
uint32_t ReturnValue0(Obj *self, uint32_t *) { return self->value[0]; }
// Table slot 13(index): the entry, or null (value[i] per index).
uint32_t TableEntry(Obj *self, uint32_t *a) { return self->value[a[0] & 7]; }
// Data slot 3(id): value[0] for 0x2f, [1] for 0x27, [2] for 0x12, else 7.
uint32_t DataInt(Obj *self, uint32_t *a)
{
    return a[0] == 0x2f ? self->value[0] : a[0] == 0x27 ? self->value[1] : a[0] == 0x12 ? self->value[2] : 7;
}
// Data slot 4(id, out): bit i of value[6] says whether id 0x45 / 0x28 / 0x5e is there; the value from [3] / [4] / [5].
uint32_t DataGet(Obj *self, uint32_t *a)
{
    const unsigned i = a[0] == 0x45 ? 0 : a[0] == 0x28 ? 1 : a[0] == 0x5e ? 2 : 3;
    if (i == 3 || !(self->value[6] >> i & 1)) return 0xab00u;
    *reinterpret_cast<int32_t *>(uintptr_t(a[1])) = int32_t(self->value[3 + i]);
    return 0xab01u;
}
// The simulator's slot 23 (GetWantDefinition), patched on both routes.
bool gDefinitionNull;
uint32_t __fastcall SimGetDefinition(void *self, void *, uint32_t guid)
{
    Ev(0x48dfc0);
    Ev(svcfx::Norm(P(self)));
    Ev(guid);
    return gDefinitionNull ? 0 : P(&F(kDefinition));
}

int32_t Small(std::mt19937 &rng) { return int32_t(rng() % 4) - 1; }

void BuildFakes(std::mt19937 &rng)
{
    svcfx::ResetAll();
    F(kGlobals).Set(16, 0, rng() % 6 ? &F(kTable) : nullptr);
    Obj &fw = F(kFramework);
    fw.Set(5, "ddp", 0, &GetSystemService);
    fw.value[0] = rng() % 4 != 0;
    F(kRegistry).Set(4, 1);
    F(kRegistry).table[4] = reinterpret_cast<void *>(&RegistryAdd);
    for (unsigned m : {kManager, kOldManager}) {
        F(m).Set(3, 0, 1);
        F(m).Set(4, 0, 1);
    }
    Obj &table = F(kTable);
    table.Set(11, 0, rng() % 6);
    table.Set(13, "d", 0, &TableEntry);
    for (unsigned i = 0; i < 8; ++i) table.value[i] = rng() % 5 ? P(&F(kEntry0 + rng() % 3)) : 0;
    for (unsigned i = 0; i < 3; ++i) {
        Obj &entry = F(kEntry0 + i);
        entry.Set(1, 0, 0, &ReturnValue0);
        entry.value[0] = rng() % 5 ? P(&F(kInner0 + i)) : 0;
        F(kInner0 + i).Set(4, 0, rng() % 5 ? &F(kData0 + i) : nullptr);
        Obj &data = F(kData0 + i);
        data.Set(3, "d", 0, &DataInt);
        data.Set(4, "dp", 0, &DataGet);
        data.value[0] = rng() % 4 ? 1 + rng() % 3 : 0; // the aspiration key (0: none)
        data.value[1] = rng() % 4 != 0;
        data.value[2] = uint32_t(int32_t(rng() % 7) - 1); // the level
        for (unsigned k = 3; k < 6; ++k) data.value[k] = uint32_t(Small(rng));
        data.value[6] = rng() % 8;
    }
    F(kDefinition).Set(16, 0, rng() % 7);
}

// A list of counted fakes (some null) in an allocator-service block.
void FillRefs(WantRefList &l, std::mt19937 &rng)
{
    const uint32_t n = rng() % 4;
    if (!n && rng() % 2) return;
    const uint32_t cap = n + rng() % 3 + (n ? 0 : 1);
    l.mFirst = static_cast<void **>(GZServiceAllocate(cap * 4));
    l.mLast = l.mFirst + n;
    l.mEnd = l.mFirst + cap;
    for (uint32_t i = 0; i < n; ++i) l.mFirst[i] = rng() % 5 ? &F(kRef0 + rng() % 6) : nullptr;
}
void *AnyRef(std::mt19937 &rng) { return rng() % 3 ? &F(kRef0 + rng() % 6) : nullptr; }

// The simulator for a seed: our constructor, the route's vtables, contents from the seed.
alignas(8) unsigned char gBuffer[sizeof(W)];
W *Build(std::mt19937 &rng)
{
    std::memset(gBuffer, 0x5c, sizeof(W));
    W *w = wantshape::Construct(gBuffer);
    svcfx::AddRange(w, sizeof(W), kSimTag);
    w->mInitialized = uint8_t(rng() % 4 == 0);
    const uint32_t old = rng() % 3;
    w->mDefinitions.object = old == 0 ? nullptr : old == 1 ? &F(kOldManager) : &F(kManager);
    for (int i = 0, n = rng() % 3; i < n; ++i) w->mMap84[int32_t(rng() % 4)] = int32_t(rng() % 5) - 1;
    for (int i = 0, n = rng() % 4; i < n; ++i) {
        WantRef &slot = w->mLifetimeWants[int16_t(rng() % 8)];
        if (!slot.object) slot.object = AnyRef(rng);
    }
    FillRefs(w->mVec68, rng);
    for (int i = 0, n = rng() % 3; i < n; ++i) w->mMap48[int16_t(rng() % 8)] = int16_t(rng());
    for (auto *m : {&w->mWants, &w->mFears, &w->mMap50, &w->mMap58})
        for (int i = 0, n = rng() % 3; i < n; ++i) FillRefs((*m)[int16_t(rng() % 8)], rng);
    static const int32_t kCategories[] = {1, 2, 4, 0x100, -1, 0x10000};
    for (int i = 0, n = rng() % 4; i < n; ++i) FillRefs(w->mCategoryWants[kCategories[rng() % 6]], rng);
    for (int i = 0, n = rng() % 3; i < n; ++i) {
        std::vector<int32_t> &levels = w->mLevelTables[int32_t(rng() % 14)];
        for (int k = 0, m = rng() % 6; k < m; ++k) levels.push_back(int32_t(rng() % 1000));
    }
    for (int i = 0, n = rng() % 3; i < n; ++i) w->mMap7c[int16_t(rng() % 8)] = {rng(), rng(), rng()};
    for (int i = 0, n = rng() % 3; i < n; ++i) {
        const int16_t sim = int16_t(rng() % 8);
        if (w->mMemoryStores.count(sim)) continue;
        auto *store = static_cast<WantMemoryStore *>(::operator new(sizeof(WantMemoryStore)));
        store->Construct(); // empty: Clear / Destruct are verified on their own (lane CY); Shutdown only calls them
        w->mMemoryStores[sim] = store;
    }
    w->mCurrentStore = nullptr;
    if (!w->mMemoryStores.empty() && rng() % 2) w->mCurrentStore = w->mMemoryStores.begin()->second;
    w->mRef90.object = AnyRef(rng);
    w->mRef94.object = AnyRef(rng);
    w->mRef1c.object = AnyRef(rng);
    w->mRef20.object = AnyRef(rng);
    return w;
}

void Teardown(W *w)
{
    for (auto &e : w->mMemoryStores) {
        e.second->Clear();
        e.second->Destruct();
        ::operator delete(e.second, sizeof(WantMemoryStore));
    }
    w->mMemoryStores.clear();
    w->mCurrentStore = nullptr;
    for (WantRef *r : {&w->mDefinitions, &w->mRef90, &w->mRef94, &w->mRef1c, &w->mRef20}) r->object = nullptr;
    w->mLifetimeWants.clear(); // releases the fakes (their counts are compared before this)
    Destroy(w);
}

void Digest(Blob &b, W *w)
{
    auto refs = [&](auto &m) {
        b.push_back(uint32_t(m.size()));
        for (auto &e : m) {
            b.push_back(uint32_t(int32_t(e.first)));
            b.push_back(e.second.Size());
            b.push_back(uint32_t(e.second.mEnd - e.second.mFirst));
            for (void **it = e.second.mFirst; it != e.second.mLast; ++it) b.push_back(svcfx::Norm(P(*it)));
        }
    };
    b.push_back(w->mInitialized);
    b.push_back(uint32_t(w->mOptionMask));
    b.push_back(svcfx::Norm(P(w->mDefinitions.object)));
    b.push_back(uint32_t(w->mMap84.size()));
    for (auto &e : w->mMap84) b.push_back(uint32_t(e.first)), b.push_back(uint32_t(e.second));
    b.push_back(uint32_t(w->mLifetimeWants.size()));
    b.push_back(w->mVec68.Size());
    b.push_back(uint32_t(w->mVec68.mEnd - w->mVec68.mFirst));
    b.push_back(uint32_t(w->mMap48.size()));
    refs(w->mWants);
    refs(w->mFears);
    refs(w->mMap50);
    refs(w->mMap58);
    refs(w->mCategoryWants);
    b.push_back(uint32_t(w->mLevelTables.size()));
    b.push_back(uint32_t(w->mMap7c.size()));
    b.push_back(uint32_t(w->mMemoryStores.size()));
    b.push_back(w->mCurrentStore != nullptr);
    for (WantRef *r : {&w->mRef90, &w->mRef94, &w->mRef1c, &w->mRef20}) b.push_back(svcfx::Norm(P(r->object)));
}

// Runs `body(w)` on routes 0 and 1 for each seed and compares what it returns and leaves.
template <typename Body> void Compare(difftest::Context &ctx, const char *name, uint32_t seeds, Body body)
{
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        Blob out[2];
        for (int route = 0; route < 2; ++route) {
            gRoute = route;
            std::mt19937 rng(seed * 2654435761u + 5);
            BuildFakes(rng);
            gOptionMask = rng() % 2 ? 0x1000 | (rng() & 0xff) : 0;
            gPoolNull = rng() % 6 == 0;
            gDefinitionNull = rng() % 5 == 0;
            W *w = Build(rng);
            gRng.seed(seed);
            Blob &b = out[route];
            body(b, w, rng);
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            Digest(b, w);
            svcfx::CaptureFakes(b, kValueKinds);
            Teardown(w);
        }
        gRoute = 0;
        if (!svcfx::Same(ctx, out[0], out[1], name, seed)) return;
    }
}

struct Scope {
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624, shape::CodeOf(&GameDelete)};
    Scope()
    {
        ClearPatches();
        gPatch[0][23] = reinterpret_cast<void *>(&SimGetDefinition);
    }
};

} // namespace wantiv
using namespace wantiv;

DIFF_TEST(WantSimulator_Init, 0x4905e0)
{
    Scope scope;
    shape::StandIn optionMask(0xb39380, &appinit::GetOptionMask, &StandOptionMask);
    shape::CodePatch managerImage(0x551c40, shape::CodeOf(&StandManagerConstruct));
    shape::CodePatch managerOurs(shape::CodeOf(&cTSWantTreeManager::Create), shape::CodeOf(&StandManagerCreate));
    shape::CodePatch tuningImage(0x494030, shape::CodeOf(&StandLoadTuningImage));
    shape::CodePatch tuningOurs(shape::CodeOf(&cTSWantSimulatorTuningLoader::LoadTuning), shape::CodeOf(&StandLoadTuningOurs));
    shape::CodePatch cheats(0xb39640, shape::CodeOf(&StandCheatIfEnabled));
    shape::StandIn walk(0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5);
    Compare(ctx, "WantSimulator_Init", 3000, [&](Blob &b, W *w, std::mt19937 &rng) {
        const bool framework = rng() % 5 != 0;
        scope.globals.Set(0x10c9378, framework ? &F(kFramework) : nullptr);
        // Without the app singleton, 0x20ddfb asks the framework for it (slot 5, which answers nothing for it here); the
        // original calls through a null framework there, so the singleton is missing only when the framework is not.
        scope.globals.Set(svcfx::kAppCell, framework && rng() % 6 == 0 ? nullptr : &F(kGlobals));
        const bool was = w->mInitialized != 0;
        const uint32_t r = Call<uint32_t(__thiscall *)(void *)>(ctx, w, 0x4905e0, 0, WANT_NA(Init));
        // The result is the walk's (the set's head) after a first Init; an Init that finds the simulator initialised
        // returns whatever eax held (0x4905e3 loads fs:[0] into it, 0x490602 jumps to the epilogue): not compared.
        b.push_back(was ? 2u : uint32_t(r == P(*reinterpret_cast<void **>(&w->mMessageIds))));
    });
}

DIFF_TEST(WantSimulator_Shutdown, 0x496720)
{
    Scope scope;
    shape::StandIn walk(0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6);
    Compare(ctx, "WantSimulator_Shutdown", 3000, [&](Blob &b, W *w, std::mt19937 &) {
        const bool initialized = w->mInitialized != 0;
        const bool last = w->mRef20.object != nullptr;
        const uint32_t r = Call<uint32_t(__thiscall *)(void *)>(ctx, w, 0x496720, 0, WANT_NA(Shutdown));
        // The original ends with a tail call to +0x20's Release (0x496911): its result is that Release's when +0x20 was
        // set, else what the previous code left in eax (not compared).
        b.push_back(initialized && last ? r : 0xeeeeeeeeu);
    });
}

DIFF_TEST(WantSimulator_AddCategoryWant, 0x486430)
{
    Scope scope;
    shape::CodePatch pool(0x54e570, shape::CodeOf(&StandPool));
    shape::CodePatch want(0x54e2b0, shape::CodeOf(&StandWantConstruct));
    static const uint32_t kCategories[] = {1, 2, 4, 0x100, 0xffffffffu, 0x10000, 8};
    Compare(ctx, "WantSimulator_AddCategoryWant", 3000, [&](Blob &, W *w, std::mt19937 &rng) {
        svcfx::AddRange(gBlock, sizeof(gBlock), kBufferTag);
        for (int i = 0, n = 1 + rng() % 3; i < n; ++i) {
            const uint32_t category = kCategories[rng() % 7];
            const uint32_t guid = rng();
            // The result is what the last call left in eax (0 without a definition, else the instance's Release or
            // the push's; 0x486588 returns it as is): not compared.
            Call<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(ctx, w, 0x486430, 0, WANT_NA(AddCategoryWant), category, guid);
        }
    });
}
