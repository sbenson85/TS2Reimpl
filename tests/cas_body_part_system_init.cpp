// Differential tests for nTSCASCore::cTSCASBodyPartSystem's +0 face (QueryInterface, Init, Shutdown) and the start-up
// helpers its constructor calls (InitCategoryLists 0xcf8fd0, InitTargetSurfaceNames 0xcf9340, ResetKeys 0xce9690) plus the
// locked-object manager's RegisterNoFactory 0xd005c0.
//
// The world: a system object (0x168 bytes between canaries) whose containers our STL builds on both sides from the same
// seed; an allocator service fake (cache 0xa10c95d4: slot 3 allocates, slot 4 frees, each logged with the block's size);
// recording fakes for the face slots the functions call and the services they reach. After each call: the result, the
// event log, every container of the object walked node by node (tree links as in-order indexes, colours, texts, refcounts,
// vtable words), the object's other bytes, the heap frees (FreeRecorder) and the canaries.
#include "difftest.h"
#include "code_patch.h"
#include "cas/cTSCASBodyPartSystem.h"
#include "game/game.h"

#include <windows.h>

#include <array>

#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace nTSCASCore;
using Sys = cTSCASBodyPartSystem;

constexpr uint32_t kSystemVTable = 0xeba13c, kSystemVTable4 = 0xeba17c, kFaceVTable = 0xeba198;
constexpr int kFaceSlots = 89, kSystemSlots = 15, kSystem4Slots = 6;

// ---- events -----------------------------------------------------------------------------------------------------------

using Event = std::array<uint32_t, 8>;
std::vector<Event> gLog;
uint8_t *gBase = nullptr; // the side's object

// A pointer as the fakes record it: an offset into the object, or a marker for null / elsewhere.
uint32_t Rel(const void *p)
{
    if (!p) return 0xeeee0000u;
    const uintptr_t d = reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gBase);
    return d < 0x168 ? static_cast<uint32_t>(d) : 0xeeee0001u;
}

// ---- the allocator service fake ----------------------------------------------------------------------------------------

std::map<void *, uint32_t> gBlocks;
void *__fastcall SvcAlloc(void *, void *, uint32_t size)
{
    void *p = std::malloc(size ? size : 1);
    std::memset(p, 0xcd, size);
    gBlocks[p] = size;
    gLog.push_back({'A', size});
    return p;
}
void __fastcall SvcFree(void *, void *, void *p)
{
    auto it = gBlocks.find(p);
    uint32_t size = 0xdead;
    if (it != gBlocks.end()) {
        size = it->second;
        gBlocks.erase(it);
    }
    gLog.push_back({'F', size});
    std::free(p);
}
void __fastcall SvcOther(void *, void *) { gLog.push_back({'?'}); }
const void *gSvcVt[8] = {&SvcOther, &SvcOther, &SvcOther, reinterpret_cast<const void *>(&SvcAlloc),
                         reinterpret_cast<const void *>(&SvcFree), &SvcOther, &SvcOther, &SvcOther};
const void *gSvcObject[2] = {gSvcVt, nullptr};

// A game global saved and restored around a test.
struct SavedWords {
    std::vector<std::pair<uint32_t *, uint32_t>> saved;
    void Save(uint32_t rva, uint32_t words = 1)
    {
        for (uint32_t i = 0; i < words; ++i) {
            uint32_t *p = GameAddress<uint32_t>(rva + 4 * i);
            saved.push_back({p, *p});
        }
    }
    ~SavedWords()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *it->first = it->second;
    }
};

struct ServiceScope {
    SavedWords words;
    ServiceScope()
    {
        words.Save(0x10c95d4);
        words.Save(0x10c95d8);
        *GameAddress<const void *>(0x10c95d4) = gSvcObject;
        *GameAddress<int32_t>(0x10c95d8) = INT32_MIN;
    }
};

// ---- the object ------------------------------------------------------------------------------------------------------

struct Side {
    uint8_t front[32];
    alignas(16) uint8_t object[0x168];
    uint8_t back[32];
    const void *systemVt[kSystemSlots];
    const void *system4Vt[kSystem4Slots];
    const void *faceVt[kFaceSlots];
    Sys *S() { return reinterpret_cast<Sys *>(object); }
};
Side gSides[2];

bool CanariesIntact(const Side &s)
{
    for (int i = 0; i < 32; ++i) {
        if (s.front[i] != 0x5a || s.back[i] != 0xa5) return false;
    }
    return true;
}

// Random bytes for the object, its three vtable words pointing at the side's vtable copies (the game's, with the slots a
// test fakes replaced by `patch`).
template <typename Patch> void Prepare(difftest::Context &ctx, Side &s, uint32_t seed, Patch patch)
{
    std::mt19937 rng(seed);
    std::memset(s.front, 0x5a, sizeof(s.front));
    std::memset(s.back, 0xa5, sizeof(s.back));
    for (auto &b : s.object) b = static_cast<uint8_t>(rng());
    std::memcpy(s.systemVt, ctx.image.At<void>(kSystemVTable), sizeof(s.systemVt));
    std::memcpy(s.system4Vt, ctx.image.At<void>(kSystemVTable4), sizeof(s.system4Vt));
    std::memcpy(s.faceVt, ctx.image.At<void>(kFaceVTable), sizeof(s.faceVt));
    patch(s);
    Sys *sys = s.S();
    // The three vtable words (the class's own vtables are not these: the words are written by offset).
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(sys) + 0x0) = s.systemVt;
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(sys) + 0x4) = s.system4Vt;
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(sys) + 0x1c) = s.faceVt;
}

// ---- dumps -----------------------------------------------------------------------------------------------------------

using Dump = std::vector<uint32_t>;

void DumpText(Dump &d, const std::string &s)
{
    d.push_back(static_cast<uint32_t>(s.size()));
    d.push_back(static_cast<uint32_t>(s.capacity()));
    for (char c : s) d.push_back(static_cast<uint8_t>(c));
}
template <bool V> void DumpName(Dump &d, const BasicCasName<V> &n)
{
    d.push_back(n.mText.mVTable == GameAddress<void>(cRZString::kVTable) ? 1u : 0xbad0u);
    d.push_back(n.mText.mRefCount);
    DumpText(d, n.mText.mString);
}

// Every node of a tree in order: the links as indexes (-1 the head), colour and nil flag, then the value.
template <typename Tree, typename Value> void DumpTree(Dump &d, const Tree &t, Value value)
{
    const auto head = t.end()._Ptr; // the head node
    std::vector<const void *> nodes;
    for (auto it = t.begin(); it != t.end(); ++it) nodes.push_back(it._Ptr);
    auto index = [&](const void *p) -> uint32_t {
        if (p == head) return 0xffffffffu;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i] == p) return static_cast<uint32_t>(i);
        }
        return 0xfffffffeu;
    };
    d.push_back(static_cast<uint32_t>(t.size()));
    d.push_back(index(head->_Left));
    d.push_back(index(head->_Parent));
    d.push_back(index(head->_Right));
    d.push_back(static_cast<uint32_t>(head->_Color) | static_cast<uint32_t>(head->_Isnil) << 8);
    for (auto it = t.begin(); it != t.end(); ++it) {
        const auto n = it._Ptr;
        d.push_back(index(n->_Left));
        d.push_back(index(n->_Parent));
        d.push_back(index(n->_Right));
        d.push_back(static_cast<uint32_t>(n->_Color) | static_cast<uint32_t>(n->_Isnil) << 8);
        value(d, n->_Myval);
    }
}

void DumpIds(Dump &d, const IdVector &v)
{
    d.push_back(static_cast<uint32_t>(v.size()));
    d.push_back(static_cast<uint32_t>(v.capacity()));
    for (uint32_t x : v) d.push_back(x);
}

void DumpSurfaceMap(Dump &d, const SurfaceMap &m)
{
    DumpTree(d, m, [](Dump &dd, const std::pair<const SurfaceName, SurfaceName> &v) {
        DumpName(dd, v.first);
        DumpName(dd, v.second);
    });
}

void DumpSurfaceSets(Dump &d, const SurfaceSetVector &v)
{
    d.push_back(static_cast<uint32_t>(v.size()));
    d.push_back(static_cast<uint32_t>(v.capacity()));
    for (const SurfaceSet &set : v) DumpTree(d, set, [](Dump &dd, const SurfaceSetName &n) { DumpName(dd, n); });
}

// The object's bytes outside the ranges a test dumps by structure (`skip`: offset, length pairs).
void DumpBytes(Dump &d, const Side &s, std::initializer_list<std::pair<uint32_t, uint32_t>> skip)
{
    // The three vtable words point at the side's own copies: compared as "still the side's copy".
    const Sys *sys = reinterpret_cast<const Sys *>(s.object);
    d.push_back(*reinterpret_cast<const void *const *>(reinterpret_cast<const uint8_t *>(sys) + 0x0) == s.systemVt);
    d.push_back(*reinterpret_cast<const void *const *>(reinterpret_cast<const uint8_t *>(sys) + 0x4) == s.system4Vt);
    d.push_back(*reinterpret_cast<const void *const *>(reinterpret_cast<const uint8_t *>(sys) + 0x1c) == s.faceVt);
    for (uint32_t off = 0; off < 0x168; ++off) {
        if (off < 8 || (off >= 0x1c && off < 0x20)) continue;
        bool skipped = false;
        for (const auto &r : skip) skipped = skipped || (off >= r.first && off < r.first + r.second);
        if (!skipped) d.push_back(s.object[off]);
    }
}

// ---- the two sides of a case ------------------------------------------------------------------------------------------

struct Outcome {
    uint64_t result = 0;
    std::vector<Event> log;
    Dump state;
    std::vector<size_t> frees;
    bool canaries = true;
};

void CompareOutcomes(difftest::Context &ctx, const char *name, uint32_t seed, const Outcome &a, const Outcome &b)
{
    ++ctx.cases;
    if (a.result != b.result) ctx.Fail("%s seed %u: result original %llx, ours %llx", name, seed, a.result, b.result);
    if (a.log != b.log) {
        size_t k = 0;
        while (k < a.log.size() && k < b.log.size() && a.log[k] == b.log[k]) ++k;
        if (k < a.log.size() && k < b.log.size()) {
            ctx.Fail("%s seed %u: event %zu: original %x %x %x %x, ours %x %x %x %x", name, seed, k, a.log[k][0], a.log[k][1],
                     a.log[k][2], a.log[k][3], b.log[k][0], b.log[k][1], b.log[k][2], b.log[k][3]);
        } else {
            ctx.Fail("%s seed %u: events original %zu, ours %zu", name, seed, a.log.size(), b.log.size());
        }
    }
    if (a.state != b.state) {
        size_t k = 0;
        while (k < a.state.size() && k < b.state.size() && a.state[k] == b.state[k]) ++k;
        ctx.Fail("%s seed %u: state differs at word %zu (sizes %zu, %zu; %x, %x)", name, seed, k, a.state.size(), b.state.size(),
                 k < a.state.size() ? a.state[k] : 0u, k < b.state.size() ? b.state[k] : 0u);
    }
    if (a.frees != b.frees) {
        ctx.Fail("%s seed %u: heap frees differ (%zu, %zu; first sizes %zu, %zu)", name, seed, a.frees.size(), b.frees.size(),
                 a.frees.empty() ? size_t(0) : a.frees[0], b.frees.empty() ? size_t(0) : b.frees[0]);    }
    if (!a.canaries || !b.canaries) ctx.Fail("%s seed %u: canaries overwritten", name, seed);
}

std::vector<size_t> Frees(const difftest::FreeRecorder &rec)
{
    std::vector<size_t> sizes(rec.Count() + 1);
    sizes.resize(rec.SortedSizes(sizes.data(), sizes.size()));
    return sizes;
}

// Runs `run(side, original)` on both sides (original first), each after `build(side)`, and compares.
template <typename Build, typename Run, typename State, typename Teardown>
void Pair(difftest::Context &ctx, const char *name, uint32_t seed, Build build, Run run, State state, Teardown teardown)
{
    Outcome out[2];
    for (int k = 0; k < 2; ++k) {
        Side &s = gSides[k];
        gBase = s.object;
        build(s);
        gLog.clear();
        gLog.reserve(4096); // the log never reallocates while frees are recorded
        difftest::FreeRecorder rec;
        out[k].result = run(s, k == 0);
        rec.Stop();
        out[k].frees = Frees(rec);
        out[k].log = gLog;
        state(out[k].state, s);
        out[k].canaries = CanariesIntact(s);
        teardown(s);
    }
    CompareOutcomes(ctx, name, seed, out[0], out[1]);
}

// ---- random names --------------------------------------------------------------------------------------------------

const char *const kNames[] = {"jaw", "jaw_grip", "mouthtarget_surface", "nosetarget_surface", "tonguebase_grip",
                              "behindears_surface", "headtarget_surface", "l_earbase_trans", "r_earbase_trans",
                              "bellytarget_surface", "rumptarget_surface", "shouldertarget_surface", "", "a", "ja", "jaw_",
                              "snout", "zz_some_surface_name_longer_than_fifteen", "cranium", "Jaw"};
const char *RandomName(std::mt19937 &rng) { return kNames[rng() % (sizeof(kNames) / sizeof(kNames[0]))]; }

void FillIds(IdVector &v, std::mt19937 &rng)
{
    const unsigned cap = rng() % 12, size = cap ? rng() % (cap + 1) : 0;
    v.reserve(cap);
    for (unsigned i = 0; i < size; ++i) v.push_back(rng() % 4 ? rng() % 8 : static_cast<uint32_t>(rng()));
}

uint32_t RandomAge(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0:
    case 1: return 0;
    case 2: return 1;
    case 3: return 2;
    case 4: return 3;
    case 5: return 4;
    case 6: return 0x80000003u;
    default: return static_cast<uint32_t>(rng());
    }
}

} // namespace

// ---- InitCategoryLists (0xcf8fd0) --------------------------------------------------------------------------------------

DIFF_TEST(cTSCASBodyPartSystem_InitCategoryLists, 0xcf8fd0)
{
    ServiceScope services;
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xcf8fd0);
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        Pair(
            ctx, "InitCategoryLists", seed,
            [&](Side &s) {
                Prepare(ctx, s, seed, [](Side &) {});
                std::mt19937 rng(seed * 7 + 1);
                Sys *sys = s.S();
                ::new (&sys->mListA) IdVector();
                ::new (&sys->mListB) IdVector();
                ::new (&sys->mListC) IdVector();
                FillIds(sys->mListA, rng);
                FillIds(sys->mListB, rng);
                FillIds(sys->mListC, rng);
                sys->mAge = RandomAge(rng);
            },
            [&](Side &s, bool original) -> uint64_t {
                if (original) orig(s.S());
                else s.S()->InitCategoryLists();
                return 0;
            },
            [](Dump &d, Side &s) {
                DumpIds(d, s.S()->mListA);
                DumpIds(d, s.S()->mListB);
                DumpIds(d, s.S()->mListC);
                DumpBytes(d, s, {{0x7c, 0x24}});
            },
            [](Side &s) {
                s.S()->mListA.~IdVector();
                s.S()->mListB.~IdVector();
                s.S()->mListC.~IdVector();
            });
    }
}

// ---- InitTargetSurfaceNames (0xcf9340) ---------------------------------------------------------------------------------

DIFF_TEST(cTSCASBodyPartSystem_InitTargetSurfaceNames, 0xcf9340)
{
    ServiceScope services;
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xcf9340);
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        Pair(
            ctx, "InitTargetSurfaceNames", seed,
            [&](Side &s) {
                Prepare(ctx, s, seed, [](Side &) {});
                std::mt19937 rng(seed * 13 + 5);
                Sys *sys = s.S();
                ::new (&sys->mSurfaceMap) SurfaceMap();
                ::new (&sys->mSurfaceSets) SurfaceSetVector();
                for (unsigned i = rng() % 7; i; --i) {
                    const char *key = RandomName(rng), *value = RandomName(rng);
                    sys->mSurfaceMap[SurfaceName(key, std::strlen(key))].mText.mString.assign(value);
                }
                const unsigned cap = rng() % 10, size = cap ? rng() % (cap + 1) : 0;
                sys->mSurfaceSets.reserve(cap);
                sys->mSurfaceSets.resize(size);
                for (SurfaceSet &set : sys->mSurfaceSets) {
                    for (unsigned i = rng() % 5; i; --i) {
                        const char *name = RandomName(rng);
                        set.insert(SurfaceSetName(name, std::strlen(name)));
                    }
                }
                sys->mAge = rng() % 3 ? RandomAge(rng) : 0;
            },
            [&](Side &s, bool original) -> uint64_t {
                if (original) orig(s.S());
                else s.S()->InitTargetSurfaceNames();
                return 0;
            },
            [](Dump &d, Side &s) {
                DumpSurfaceMap(d, s.S()->mSurfaceMap);
                DumpSurfaceSets(d, s.S()->mSurfaceSets);
                DumpBytes(d, s, {{0xa0, 0x14}});
            },
            [](Side &s) {
                s.S()->mSurfaceMap.~SurfaceMap();
                s.S()->mSurfaceSets.~SurfaceSetVector();
            });
    }
}

// ---- ResetKeys (0xce9690) ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kKeyGuard = 0x10d9f64, kKeyStatic = 0x10d9f54, kKeySource = 0x10d9f44;

void __fastcall StoreKeyFake(void *self, void *, uint32_t index, const uint32_t *key)
{
    gLog.push_back({58, Rel(self), index, key == GameAddress<uint32_t>(kKeyStatic) ? 1u : 0xbadu, key[0], key[1], key[2], key[3]});
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_ResetKeys, 0xce9690)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xce9690);
    SavedWords saved;
    saved.Save(kKeyGuard);
    saved.Save(kKeyStatic, 4);
    saved.Save(kKeySource, 4);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        std::mt19937 rng(seed);
        const bool firstUse = rng() % 2 == 0;
        uint32_t source[4], current[4];
        for (auto &w : source) w = static_cast<uint32_t>(rng());
        for (auto &w : current) w = static_cast<uint32_t>(rng());
        Pair(
            ctx, "ResetKeys", seed,
            [&](Side &s) {
                Prepare(ctx, s, seed, [](Side &side) { side.faceVt[58] = reinterpret_cast<const void *>(&StoreKeyFake); });
                std::memcpy(GameAddress<uint32_t>(kKeySource), source, 16);
                std::memcpy(GameAddress<uint32_t>(kKeyStatic), current, 16);
                // The guard: 0 takes the first-use path (the copy) on both CRTs; INT32_MIN counts as built for both.
                *GameAddress<int32_t>(kKeyGuard) = firstUse ? 0 : INT32_MIN;
            },
            [&](Side &s, bool original) -> uint64_t {
                if (original) orig(s.S());
                else s.S()->ResetKeys();
                return 0;
            },
            [](Dump &d, Side &s) {
                const uint32_t *key = GameAddress<uint32_t>(kKeyStatic);
                d.insert(d.end(), key, key + 4);
                const int32_t guard = *GameAddress<int32_t>(kKeyGuard);
                d.push_back(guard == 0 ? 0u : guard == -1 ? 1u : 2u); // each CRT stamps its own epoch: "built" is the state
                DumpBytes(d, s, {});
            },
            [](Side &) {});
    }
}

// ---- QueryInterface (0xcf5f00) -----------------------------------------------------------------------------------------

namespace {

uint32_t __fastcall AddRefFake(void *self, void *)
{
    gLog.push_back({1, Rel(self)});
    return 7;
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_QueryInterface, 0xcf5f00)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0xcf5f00);
    // The ids: this face's, cIGZSystemService's (0x287fb697), cIGZUnknown (1), and others (the base answers false).
    const uint32_t ids[] = {0xcb7d64f8, 0x287fb697, 1, 0, 0xcb7d64f9, 0x12345678};
    for (uint32_t seed = 1; seed <= 120; ++seed) {
        const uint32_t riid = ids[seed % 6];
        void *out = reinterpret_cast<void *>(0x13579bdf);
        Pair(
            ctx, "QueryInterface", seed,
            [&](Side &s) {
                Prepare(ctx, s, seed, [](Side &side) {
                    side.systemVt[1] = reinterpret_cast<const void *>(&AddRefFake);
                    side.system4Vt[1] = reinterpret_cast<const void *>(&AddRefFake);
                });
                out = reinterpret_cast<void *>(0x13579bdf);
            },
            [&](Side &s, bool original) -> uint64_t {
                const bool r = original ? orig(s.S(), riid, &out) : cTSCASBodyPartSystemAdapters::QueryInterface(s.S(), nullptr, riid, &out);
                return static_cast<uint64_t>(r) << 32 | (out == reinterpret_cast<void *>(0x13579bdf) ? 0xffffu : Rel(out));
            },
            [](Dump &d, Side &s) { DumpBytes(d, s, {}); }, [](Side &) {});
    }
}

// ---- Init (0xcf3580) and RegisterNoFactory (0xd005c0) ----------------------------------------------------------------

namespace {

constexpr uint32_t kPersistCache = 0x10faf6c;  // CachedServiceGetter<0x10faf6c, ...> (0xb390c0)
constexpr uint32_t kServiceObject = 0x10c9378; // GetServiceObject (0x1427b5)
constexpr uint32_t kDirector = 0x10c9390, kDirectorGuard = 0x10c93f4;

struct FakeObject {
    const void *const *vt;
};

// A heap object as the fakes see it: its first three words (vtable as a code, then two words).
uint32_t VtCode(const void *vt)
{
    for (uint32_t rva : {0xeba300u, 0xeba440u, 0xeba3b4u, 0xda6718u}) {
        if (vt == GameAddress<void>(rva)) return rva;
    }
    return 0xbadbadu;
}

bool gManagerKeeps = false;   // slot 20 AddRefs the factory (as a real manager would keep it)
bool gHaveCheatSystem = true; // the framework answers the cheat system
std::vector<void *> gKept;    // factories the fake manager holds, released after the case

uint32_t __fastcall ManagerRegister(void *, void *, uint32_t type, uint32_t group, void *factory)
{
    if (factory) {
        const uint32_t *w = static_cast<const uint32_t *>(factory);
        gLog.push_back({20, type, group, VtCode(reinterpret_cast<const void *>(w[0])), VtCode(reinterpret_cast<const void *>(w[1])), w[2]});
        if (gManagerKeeps) {
            using AddRef = uint32_t(__thiscall *)(void *);
            reinterpret_cast<AddRef>((*static_cast<void ***>(factory))[1])(factory);
            gKept.push_back(factory);
        }
    } else {
        gLog.push_back({20, type, group, 0});
    }
    return 1;
}
void *__fastcall PersistSlot3(void *self, void *);
const void *gManagerVt[24];
FakeObject gManager{gManagerVt};
const void *gPersistVt[8];
FakeObject gPersist{gPersistVt};
void *__fastcall PersistSlot3(void *, void *)
{
    gLog.push_back({3});
    return &gManager;
}

// The cheat system: slot 4 registers a command, slot 2 releases the reference the framework handed out.
uint32_t __fastcall CheatRegister(void *, void *, void *command)
{
    const uint32_t *w = static_cast<const uint32_t *>(command);
    gLog.push_back({0xc4, VtCode(reinterpret_cast<const void *>(w[0])), w[1], w[2]});
    return 1;
}
uint32_t __fastcall CheatRelease(void *, void *)
{
    gLog.push_back({0xc2});
    return 0;
}
const void *gCheatVt[8];
FakeObject gCheat{gCheatVt};

// The framework (GetServiceObject): slot 5 answers the cheat system for its ids.
uint32_t __fastcall FrameworkQuery(void *, void *, uint32_t clsid, uint32_t iid, void **out)
{
    gLog.push_back({0xf5, clsid, iid});
    if (gHaveCheatSystem) {
        *out = &gCheat;
        return 1;
    }
    return 0;
}
const void *gFrameworkVt[8];
FakeObject gFramework{gFrameworkVt};

// The COM director's slot 12 returns its framework object, whose slot 7 is what Init calls with the cookie's address.
uint32_t __fastcall DirectorSlot7(void *, void *, void *cookie)
{
    gLog.push_back({0xd7, Rel(cookie)});
    return 1;
}
const void *gDirFwVt[8];
FakeObject gDirFw{gDirFwVt};
void *__fastcall DirectorSlot12(void *, void *)
{
    gLog.push_back({0xdc});
    return &gDirFw;
}
const void *gDirectorVt[16];

// 0xb39640 (cdecl): registers a command with the cheat system.
void __cdecl RegisterCheatFake(void *command)
{
    const uint32_t *w = static_cast<const uint32_t *>(command);
    gLog.push_back({0xb39640, VtCode(reinterpret_cast<const void *>(w[0])), w[1], w[2]});
}

void __fastcall LoadSlotFake(void *self, void *) { gLog.push_back({4, Rel(self)}); }

void __fastcall Trap(void *, void *) { gLog.push_back({0x7a7}); }

void BuildFakes()
{
    for (auto *vt : {gManagerVt, gPersistVt, gCheatVt, gFrameworkVt, gDirFwVt}) {
        for (int i = 0; i < 8; ++i) vt[i] = reinterpret_cast<const void *>(&Trap);
    }
    for (auto &p : gManagerVt) p = reinterpret_cast<const void *>(&Trap);
    for (auto &p : gDirectorVt) p = reinterpret_cast<const void *>(&Trap);
    gManagerVt[20] = reinterpret_cast<const void *>(&ManagerRegister);
    gPersistVt[3] = reinterpret_cast<const void *>(&PersistSlot3);
    gCheatVt[4] = reinterpret_cast<const void *>(&CheatRegister);
    gCheatVt[2] = reinterpret_cast<const void *>(&CheatRelease);
    gFrameworkVt[5] = reinterpret_cast<const void *>(&FrameworkQuery);
    gDirFwVt[7] = reinterpret_cast<const void *>(&DirectorSlot7);
    gDirectorVt[12] = reinterpret_cast<const void *>(&DirectorSlot12);
}

struct InitGlobals {
    SavedWords words;
    InitGlobals()
    {
        BuildFakes();
        words.Save(kPersistCache);
        words.Save(kServiceObject);
        words.Save(kDirector);
        words.Save(kDirectorGuard);
        *GameAddress<void *>(kPersistCache) = &gPersist;
        *GameAddress<void *>(kServiceObject) = &gFramework;
        *GameAddress<const void *>(kDirector) = gDirectorVt;
        *GameAddress<int32_t>(kDirectorGuard) = INT32_MIN; // the director counts as built
    }
};

void ReleaseKept()
{
    for (void *factory : gKept) {
        using Release = uint32_t(__thiscall *)(void *);
        reinterpret_cast<Release>((*static_cast<void ***>(factory))[2])(factory);
    }
    gKept.clear();
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_Init, 0xcf3580)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *)>(0xcf3580);
    InitGlobals globals;
    shape::CodePatch cheat(0xb39640, reinterpret_cast<const void *>(&RegisterCheatFake)); // ours calls it as an original
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        gManagerKeeps = seed % 3 == 0;
        gHaveCheatSystem = seed % 4 != 1;
        Pair(
            ctx, "Init", seed,
            [&](Side &s) { Prepare(ctx, s, seed, [](Side &side) { side.faceVt[4] = reinterpret_cast<const void *>(&LoadSlotFake); }); },
            [&](Side &s, bool original) -> uint64_t {
                const bool r = original ? orig(s.S()) : cTSCASBodyPartSystemAdapters::Init(s.S(), nullptr);
                return static_cast<uint8_t>(r);
            },
            [](Dump &d, Side &s) { DumpBytes(d, s, {}); }, [](Side &) { ReleaseKept(); });
    }
}

DIFF_TEST(cTSCASLockedObjectManager_RegisterNoFactory, 0xd005c0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xd005c0);
    InitGlobals globals;
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        Pair(
            ctx, "RegisterNoFactory", seed, [&](Side &s) { Prepare(ctx, s, seed, [](Side &) {}); },
            [&](Side &s, bool original) -> uint64_t {
                if (original) orig(&s.S()->mLockedObjects);
                else s.S()->mLockedObjects.RegisterNoFactory();
                return 0;
            },
            [](Dump &d, Side &s) { DumpBytes(d, s, {}); }, [](Side &) {});
    }
}

// ---- Shutdown (0xcfa200) -----------------------------------------------------------------------------------------------

namespace {

void __fastcall Slot9Fake(void *self, void *) { gLog.push_back({9, Rel(self)}); }
void __fastcall Slot10Fake(void *self, void *) { gLog.push_back({10, Rel(self)}); }

// The original erase helpers of the two maps and the locked-object manager's clear: recorded (the maps' nodes are tokens).
void __fastcall EraseCdbf10(void *self, void *, void *tree, void *root)
{
    gLog.push_back({0xcdbf10, Rel(self), Rel(tree), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(root))});
}
void __fastcall EraseCdbc10(void *self, void *, void *tree, void *root)
{
    gLog.push_back({0xcdbc10, Rel(self), Rel(tree), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(root))});
}
void __fastcall ClearLocked(void *self, void *) { gLog.push_back({0xcfebc0, Rel(self)}); }

// A head node whose links are tokens (the stood-in erase never walks it), and the state the clear leaves.
struct HeadNode {
    void *left, *parent, *right;
};
uint32_t HeadState(const HeadNode *h, uint32_t size)
{
    const bool reset = h->left == h && h->parent == h && h->right == h;
    return reset ? size : 0xbad00000u | size;
}

HeadNode gHeads[2][2];        // per side: the +0x20 map's head and the +0x4c map's head
uint32_t gHeap20[2][2];       // per side: the +0x20 map object (head pointer, size)

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_Shutdown, 0xcfa200)
{
    ServiceScope services;
    auto orig = ctx.Original<bool(__thiscall *)(void *)>(0xcfa200);
    shape::CodePatch e1(0xcdbf10, reinterpret_cast<const void *>(&EraseCdbf10));
    shape::CodePatch e2(0xcdbc10, reinterpret_cast<const void *>(&EraseCdbc10));
    shape::CodePatch e3(0xcfebc0, reinterpret_cast<const void *>(&ClearLocked));
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        int side = 0;
        Pair(
            ctx, "Shutdown", seed,
            [&](Side &s) {
                side = &s == &gSides[0] ? 0 : 1;
                Prepare(ctx, s, seed, [](Side &sd) {
                    sd.faceVt[9] = reinterpret_cast<const void *>(&Slot9Fake);
                    sd.faceVt[10] = reinterpret_cast<const void *>(&Slot10Fake);
                });
                std::mt19937 rng(seed * 3 + 11);
                Sys *sys = s.S();
                auto *records = new std::vector<SurfaceRecord, CasAlloc<SurfaceRecord>>();
                const unsigned cap = rng() % 6, size = cap ? rng() % (cap + 1) : 0;
                records->reserve(cap);
                for (unsigned i = 0; i < size; ++i) {
                    SurfaceRecord r;
                    for (auto &t : r.mText) t = RandomName(rng);
                    records->push_back(r);
                }
                sys->mRecords = records;
                const uint32_t token = static_cast<uint32_t>(rng() % 1000);
                gHeads[side][0] = {reinterpret_cast<void *>(0x11110000u + token), reinterpret_cast<void *>(0x22220000u + token),
                                   reinterpret_cast<void *>(0x33330000u + token)};
                gHeads[side][1] = {reinterpret_cast<void *>(0x44440000u + token), reinterpret_cast<void *>(0x55550000u + token),
                                   reinterpret_cast<void *>(0x66660000u + token)};
                gHeap20[side][0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gHeads[side][0]));
                gHeap20[side][1] = rng() % 40;
                sys->mTree20 = reinterpret_cast<TreeHeader *>(gHeap20[side]);
                reinterpret_cast<uint32_t *>(static_cast<TreeHeader *>(&sys->mResources))[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gHeads[side][1]));
                reinterpret_cast<uint32_t *>(static_cast<TreeHeader *>(&sys->mResources))[1] = rng() % 40;
            },
            [&](Side &s, bool original) -> uint64_t {
                const bool r = original ? orig(s.S()) : cTSCASBodyPartSystemAdapters::Shutdown(s.S(), nullptr);
                return static_cast<uint8_t>(r);
            },
            [&](Dump &d, Side &s) {
                Sys *sys = s.S();
                d.push_back(static_cast<uint32_t>(sys->mRecords->size()));
                d.push_back(static_cast<uint32_t>(sys->mRecords->capacity()));
                d.push_back(HeadState(&gHeads[side][0], gHeap20[side][1]));
                d.push_back(HeadState(&gHeads[side][1], reinterpret_cast<uint32_t *>(static_cast<TreeHeader *>(&sys->mResources))[1]));
                d.push_back(gHeap20[side][0] == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gHeads[side][0])));
                d.push_back(reinterpret_cast<uint32_t *>(static_cast<TreeHeader *>(&sys->mResources))[0] == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gHeads[side][1])));
                d.push_back(sys->mTree20 == reinterpret_cast<TreeHeader *>(gHeap20[side]));
                DumpBytes(d, s, {{0x20, 8}, {0x4c, 4}});
            },
            [](Side &s) { delete s.S()->mRecords; });
    }
}
