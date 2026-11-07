// Differential tests for the second batch of cTSPersistSystem functions (field map in src/ts/cTSPersistSystem.h).
//
// Each run gets a world built from a seed: a random primary object (0x300 bytes) whose base locks (+0x20,
// +0x68, +0x88, +0x98) are embedded lock objects with recording slots, a listener vector at +0x230, an empty or
// one-value list at +0x90 (the base's value list), and sixteen fake objects with their own vtables, whose slots
// each test fills with recording fakes of the right arity. The original runs on one world and ours on another
// built from the same seed; the recorded calls (kind, object, arguments), the return value (as the caller reads
// it) and every word of the world afterwards must match, with addresses normalised to ids.
#include "difftest.h"
#include "free_scope.h"
#include "gz/cGZPersistResourceManager.h"
#include "hook/hooks.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSPersistDBRecord.h"
#include "ts/cTSPersistSystem.h"
#include "ts_persist_system_shape.h"
#include "shape.h"
#include "ts/ChangeRecordSet.h"
#include "game/vtable.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <utility>
#include <memory>
#include <random>
#include <vector>

#include <windows.h>

namespace {

constexpr size_t kPrimaryBytes = 0x300;
constexpr int kObjects = 16;
constexpr size_t kObjBytes = 0x40;
constexpr int kSlots = 80;
constexpr int kKinds = 4096;

struct World {
    alignas(8) uint8_t primary[kPrimaryBytes];
    alignas(8) uint8_t obj[kObjects][kObjBytes];
    void *vt[kObjects][kSlots];
    void *vtP[kSlots];   // primary (+0)
    void *vtS[kSlots];   // service (+4)
    void *vtI[kSlots];   // cITSPersistSystem (+0xa8)
    void *vtLock[4];     // shared by the embedded locks
    void *listeners[8];  // the listener vector's storage
    uint32_t list2[2];   // sentinel of the dirty-record list at +0x210 (OnTick's flush)
    void *items[6];      // vf168_51's item vector storage
    void *itemsVec[2];   // its begin and end
    void *segs[8];       // the segment vector's storage (+0xd4)
    void *outVec[3];     // an out vector (first, last, end) over items[]
    uint32_t listNode[4]; // sentinel (next, prev, pad) of the value list at +0x90
    uint32_t valueNode[3]; // one value node (next, prev, value)
};

World *gWorld = nullptr;
std::vector<uint32_t> gTrace;
std::vector<std::pair<uintptr_t, uint32_t>> gIds;
uint32_t gRet[kKinds];
// Out-pointer fakes: which fake object they hand out (-1: null), and whether they write it when they fail.
int gOutIdx[kKinds];
bool gWriteOnFail = false;
// Whether every listener event carries a 16-byte block pointer (recorded by contents).
bool gAllBlocks = false;
// A field (primary offset) that some fakes clear when they run (0: none).
size_t gClearOff = 0;
// Per-kind call counters for fakes returning a sequence (reset per run).
int gCount[kKinds];

constexpr uint32_t kIdTag = 0xa5000000u;

uint32_t Map(uint32_t v)
{
    for (const auto &e : gIds)
        if (e.first == v) return kIdTag | e.second;
    return v;
}

uint32_t Map(const void *p)
{
    return Map(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)));
}

void SetIds(World &w)
{
    gIds.clear();
    auto add = [](const void *p, uint32_t id) { gIds.push_back({reinterpret_cast<uintptr_t>(p), id}); };
    add(w.primary, 1);
    add(w.primary + 4, 2);
    add(w.primary + 8, 3);
    add(w.primary + 0xa8, 4);
    add(w.primary + 0xac, 5);
    add(w.primary + 0xb0, 6);
    add(w.primary + 0x20, 7);
    add(w.primary + 0x68, 8);
    add(w.primary + 0x88, 9);
    add(w.primary + 0x98, 10);
    for (int i = 0; i < kObjects; ++i) add(w.obj[i], 20 + i);
    for (int i = 0; i < kObjects; ++i) add(w.vt[i], 40 + i);
    add(w.vtP, 60);
    add(w.vtS, 61);
    add(w.vtI, 62);
    add(w.vtLock, 63);
    for (int i = 0; i <= 8; ++i) add(w.listeners + i, 80 + i);
    add(w.list2, 90);
    for (int i = 0; i <= 6; ++i) add(w.items + i, 91 + i);
    add(w.itemsVec, 98);
    for (int i = 0; i <= 8; ++i) add(w.segs + i, 100 + i);
    add(w.outVec, 110);
    add(w.listNode, 69);
    add(w.valueNode, 70);
}

void Note(int kind, const void *self)
{
    gTrace.push_back(static_cast<uint32_t>(kind));
    gTrace.push_back(Map(self));
}

void Rec(uint32_t v)
{
    gTrace.push_back(Map(v));
}

void *OutTarget(int kind)
{
    int i = gOutIdx[kind];
    return i < 0 ? nullptr : gWorld->obj[i];
}

// A successful call always hands out an object (callers use it unchecked); a failing one may write null.
void *OutFor(int kind, uint8_t ok)
{
    void *t = OutTarget(kind);
    return ok && !t ? gWorld->obj[0] : t;
}

// Recording slots by arity. Stack arguments are popped by the callee (__fastcall with the edx dummy), as the
// game's __thiscall callees do.
template <int K> uint32_t __fastcall S0(void *self, void *)
{
    Note(K, self);
    return gRet[K];
}
template <int K> uint32_t __fastcall S1(void *self, void *, uint32_t a)
{
    Note(K, self);
    Rec(a);
    return gRet[K];
}
template <int K> uint32_t __fastcall S2(void *self, void *, uint32_t a, uint32_t b)
{
    Note(K, self);
    Rec(a);
    Rec(b);
    return gRet[K];
}
template <int K> uint32_t __fastcall S3(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Note(K, self);
    Rec(a);
    Rec(b);
    Rec(c);
    return gRet[K];
}
template <int K> uint32_t __fastcall S5(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    Note(K, self);
    for (uint32_t v : {a, b, c, d, e}) Rec(v);
    return gRet[K];
}
template <int K> uint32_t __fastcall S6(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    Note(K, self);
    for (uint32_t v : {a, b, c, d, e, f}) Rec(v);
    return gRet[K];
}
// Listener slot 3 (event, arg): events 9 and 10 carry a pointer to a 16-byte local, so its contents are
// recorded instead of its address.
template <int K> uint32_t __fastcall Listener(void *self, void *, uint32_t event, uint32_t arg)
{
    Note(K, self);
    Rec(event);
    if (event == 9 || event == 10 || gAllBlocks) {
        const auto *words = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(arg));
        for (int i = 0; i < 4; ++i) Rec(words[i]);
    } else {
        Rec(arg);
    }
    return gRet[K];
}
// Out pointer last: (a..., void **out). Writes OutTarget(K) when ok (gRet[K] & 1) or gWriteOnFail.
template <int K> uint8_t __fastcall Out1(void *self, void *, void **out)
{
    Note(K, self);
    uint8_t ok = gRet[K] & 1;
    if (ok || gWriteOnFail) *out = OutFor(K, ok);
    return ok;
}
template <int K> uint8_t __fastcall Out2(void *self, void *, uint32_t a, void **out)
{
    Note(K, self);
    Rec(a);
    uint8_t ok = gRet[K] & 1;
    if (ok || gWriteOnFail) *out = OutFor(K, ok);
    return ok;
}
template <int K> uint8_t __fastcall Out3(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    Note(K, self);
    Rec(a);
    Rec(b);
    uint8_t ok = gRet[K] & 1;
    if (ok || gWriteOnFail) *out = OutFor(K, ok);
    return ok;
}

// The base's slot 52 shape: (a, b, void **out, d, e, f).
template <int K> uint8_t __fastcall OutMid6(void *self, void *, uint32_t a, uint32_t b, void **out, uint32_t d, uint32_t e, uint32_t f)
{
    Note(K, self);
    for (uint32_t v : {a, b, d, e, f}) Rec(v);
    uint8_t ok = gRet[K] & 1;
    if (ok || gWriteOnFail) *out = OutFor(K, ok);
    return ok;
}

template <typename F> void *Fn(F f)
{
    return reinterpret_cast<void *>(f);
}

template <typename T> void Put(void *base, size_t off, T v)
{
    std::memcpy(static_cast<uint8_t *>(base) + off, &v, sizeof(v));
}

uint32_t Addr(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// Kinds: 100 * (fake object index + 1) + slot for the fakes; 2000 + slot for the primary's vtable, 2100 + slot
// for the service vtable, 2200 + slot for the ITS vtable; 2300/2301 for lock enter/leave.
constexpr int K(int obj, int slot)
{
    return 100 * (obj + 1) + slot;
}

// Slots no test set: a call reports the slot and object, then ends the run (the arity is unknown, so it can't return).
template <int N> void __fastcall Trap(void *self, void *)
{
    std::printf("unexpected call: slot %d of object 0x%x\n", N, Map(self));
    std::fflush(stdout);
    ExitProcess(3);
}

template <size_t... I> std::array<void *, kSlots> MakeTraps(std::index_sequence<I...>)
{
    return {reinterpret_cast<void *>(&Trap<static_cast<int>(I)>)...};
}

const std::array<void *, kSlots> &Traps()
{
    static const std::array<void *, kSlots> traps = MakeTraps(std::make_index_sequence<kSlots>());
    return traps;
}

// Builds the world from the seed: random bytes everywhere, the embedded locks, the listener vector (0 to 4
// listeners, fake objects 12..15, slot 3), the base's value list (empty, or one value: fake object 11 with
// AddRef/Release, slot 7 (a1, &out) and slot 14 (a1)), and the default fakes the base calls use.
void Build(World &w, uint32_t seed)
{
    std::mt19937 rng(seed);
    auto fill = [&rng](void *p, size_t n) {
        auto *b = static_cast<uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng());
    };
    fill(w.primary, sizeof(w.primary));
    fill(w.obj, sizeof(w.obj));
    for (auto &v : w.vt)
        for (int i = 0; i < kSlots; ++i) v[i] = Traps()[i];
    for (auto *v : {w.vtP, w.vtS, w.vtI})
        for (int i = 0; i < kSlots; ++i) v[i] = Traps()[i];
    for (int i = 0; i < kObjects; ++i) Put(w.obj[i], 0, Addr(w.vt[i]));
    for (int i = 0; i < kKinds; ++i) {
        gRet[i] = rng() % 3 ? rng() : 0;
        gOutIdx[i] = rng() % 4 ? static_cast<int>(rng() % 8) : -1;
    }
    gWriteOnFail = rng() % 4 == 0;
    gAllBlocks = false;
    gClearOff = 0;

    w.vtLock[0] = nullptr;
    w.vtLock[1] = Fn(&S0<2301>);
    w.vtLock[2] = Fn(&S0<2302>);
    w.vtLock[3] = nullptr;
    for (size_t off : {0x20, 0x68, 0x88, 0x98}) Put(w.primary, off, Addr(w.vtLock));
    Put(w.primary, 0, Addr(w.vtP));
    Put(w.primary, 4, Addr(w.vtS));
    Put(w.primary, 0xa8, Addr(w.vtI));

    // Listeners.
    int n = static_cast<int>(rng() % 5);
    for (int i = 0; i < 8; ++i) w.listeners[i] = i < n ? w.obj[12 + rng() % 4] : reinterpret_cast<void *>(rng());
    for (int i = 12; i < 16; ++i) {
        w.vt[i][1] = Fn(&S0<K(12, 1)>);
        w.vt[i][2] = Fn(&S0<K(12, 2)>);
        w.vt[i][3] = Fn(&Listener<K(12, 3)>);
    }
    Put(w.primary, 0x230, Addr(w.listeners));
    Put(w.primary, 0x234, Addr(w.listeners + n));
    Put(w.primary, 0x238, Addr(w.listeners + 8));
    fill(w.list2, sizeof(w.list2));
    fill(w.items, sizeof(w.items));
    fill(w.itemsVec, sizeof(w.itemsVec));
    fill(w.segs, sizeof(w.segs));
    fill(w.outVec, sizeof(w.outVec));

    // The base's value list at +0x90 (size at +0x94).
    w.listNode[2] = rng();
    w.listNode[3] = rng();
    if (rng() % 2) {
        w.listNode[0] = w.listNode[1] = Addr(w.valueNode);
        w.valueNode[0] = w.valueNode[1] = Addr(w.listNode);
        w.valueNode[2] = Addr(w.obj[11]);
        Put(w.primary, 0x94, 1u);
    } else {
        w.listNode[0] = w.listNode[1] = Addr(w.listNode);
        fill(w.valueNode, sizeof(w.valueNode));
        Put(w.primary, 0x94, 0u);
    }
    Put(w.primary, 0x90, Addr(w.listNode));
    w.vt[11][1] = Fn(&S0<K(11, 1)>);
    w.vt[11][2] = Fn(&S0<K(11, 2)>);
    w.vt[11][7] = Fn(&Out2<K(11, 7)>);
    w.vt[11][14] = Fn(&S1<K(11, 14)>);
    // Objects handed out by the base calls' out pointers: AddRef/Release and slot 0 (two arguments).
    for (int i = 0; i < 8; ++i) {
        w.vt[i][0] = Fn(&S2<K(0, 0)>);
        w.vt[i][1] = Fn(&S0<K(0, 1)>);
        w.vt[i][2] = Fn(&S0<K(0, 2)>);
    }
}

// The world's words, normalised.
void Snapshot(World &w, std::vector<uint32_t> &t)
{
    auto words = [&](const void *p, size_t n) {
        const auto *b = static_cast<const uint8_t *>(p);
        for (size_t i = 0; i + 4 <= n; i += 4) {
            uint32_t v;
            std::memcpy(&v, b + i, 4);
            t.push_back(Map(v));
        }
    };
    words(w.primary, sizeof(w.primary));
    words(w.obj, sizeof(w.obj));
    words(w.listeners, sizeof(w.listeners));
    words(w.listNode, sizeof(w.listNode));
    words(w.valueNode, sizeof(w.valueNode));
    words(w.list2, sizeof(w.list2));
    words(w.items, sizeof(w.items));
    words(w.itemsVec, sizeof(w.itemsVec));
    words(w.segs, sizeof(w.segs));
    words(w.outVec, sizeof(w.outVec));
}

struct Outcome {
    uint32_t ret = 0;
    std::vector<uint32_t> trace;
    std::vector<uint32_t> after;
};

// Where the world lives. Original and ours run on this one world, rebuilt from the seed before each (Compare), so
// they see it at the same address and the id map is the same function on both sides: equal words stay equal and
// different words stay different, whatever the call wrote. Two worlds at two addresses made the map depend on the
// heap layout (a random word equal to one world's address was renamed in that world only; lane NB,
// tests/gz_persist_db_segment_multi_packed_files.cpp).
World &SharedWorld()
{
    static World *const w = new World;
    return *w;
}

// Builds the world from the seed and lets the test customise it (deterministic in the seed, so each side gets the same).
void Prepare(World &w, uint32_t seed, void (*customise)(World &, std::mt19937 &))
{
    // Cleared first: what Build leaves unwritten must not carry over from the previous run.
    static_assert(std::is_trivially_copyable_v<World>, "cleared with memset");
    std::memset(&w, 0, sizeof(w));
    Build(w, seed);
    std::mt19937 rng(seed ^ 0x5eed);
    if (customise) customise(w, rng);
}

template <typename Call> Outcome Run(World &w, Call call)
{
    gWorld = &w;
    SetIds(w);
    gTrace.clear();
    std::memset(gCount, 0, sizeof(gCount));
    Outcome o;
    o.ret = call(w);
    o.trace = gTrace;
    Snapshot(w, o.after);
    gWorld = nullptr;
    return o;
}

// Builds the worlds (customise sets up what the test needs, the same way in both), runs both sides and compares.
template <typename OrigCall, typename OursCall>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, void (*customise)(World &, std::mt19937 &), OrigCall orig,
             OursCall ours)
{
    World &w = SharedWorld();
    Prepare(w, seed, customise);
    Outcome a = Run(w, orig);
    Prepare(w, seed, customise);
    Outcome b = Run(w, ours);
    ++ctx.cases;
    if (a.ret != b.ret) ctx.Fail("%s seed %u: original returned 0x%x, ours 0x%x", name, seed, a.ret, b.ret);
    if (a.trace != b.trace) {
        size_t i = 0;
        while (i < a.trace.size() && i < b.trace.size() && a.trace[i] == b.trace[i]) ++i;
        ctx.Fail("%s seed %u: calls differ at word %zu (original %zu words, ours %zu)", name, seed, i, a.trace.size(), b.trace.size());
    }
    if (a.after != b.after) {
        size_t i = 0;
        while (i < a.after.size() && a.after[i] == b.after[i]) ++i;
        ctx.Fail("%s seed %u: world word %zu differs afterwards (original 0x%x, ours 0x%x)", name, seed, i, a.after[i], b.after[i]);
    }
}

uint8_t *Sub(World &w, size_t off)
{
    return w.primary + off;
}

template <typename T> T *As(World &w, size_t off)
{
    return reinterpret_cast<T *>(w.primary + off);
}

constexpr int kSeeds = 300;

} // namespace

// ---------------------------------------------------------------------------------------------------------
// NotifyListeners and the base-call wrappers.

PS_TEST(TSPersistSystem2_NotifyListeners, 0x1de870)
{
    using Fn = void(__thiscall *)(void *, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x1de870);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        uint32_t event = seed % 12, arg = seed * 0x9e3779b9u;
        // Events 9 and 10 take a block pointer; give them one.
        static uint32_t block[4] = {1, 2, 3, 4};
        if (event == 9 || event == 10) arg = Addr(block);
        Compare(ctx, "NotifyListeners", seed, nullptr,
                [&](World &w) {
                    orig(w.primary, event, arg);
                    return 0u;
                },
                [&](World &w) {
                    reinterpret_cast<cTSPersistSystem *>(As<uint8_t>(w, 0))->NotifyListeners(event, arg); // hooked at the member
                    return 0u;
                });
    }
}

namespace {

// Keys for vf3/vf4/vf5: the two routed types and others. Objects at +0xcc and +0xd0 are fakes 8 and 9, with
// slot 7 (five arguments) and slot 8 (five arguments). The base path calls the primary's slot 4 (vf3: six
// arguments), slots 49 and 52 (vf4) and slot 52 (vf5).
uint32_t gKey[1];

void RouteSetup(World &w, std::mt19937 &rng)
{
    static const uint32_t kTypes[] = {0x2026960b, 0x4d45f8e6, 0x2026960a, 0x4d45f8e7, 0};
    gKey[0] = rng() % 5 ? kTypes[rng() % 5] : rng();
    Put(w.primary, 0xcc, Addr(w.obj[8]));
    Put(w.primary, 0xd0, Addr(w.obj[9]));
    w.vt[8][7] = Fn(&S5<K(8, 7)>);
    w.vt[8][8] = Fn(&S5<K(8, 8)>);
    w.vt[9][7] = Fn(&S5<K(9, 7)>);
    w.vt[9][8] = Fn(&S5<K(9, 8)>);
    w.vtP[4] = Fn(&S6<2004>);
    w.vtP[49] = Fn(&S3<2049>);
    w.vtP[50] = Fn(&S2<2050>);
    w.vtP[52] = Fn(&OutMid6<2052>);
}

// vf52's base: this->slot23(a1, &obj1) and slot 34 (a1, &obj) as out-pointer fakes, slot 53 (seven arguments).
template <int Kd> uint32_t __fastcall S7(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                        uint32_t g)
{
    Note(Kd, self);
    for (uint32_t v : {a, b, c, d, e, f, g}) Rec(v);
    return gRet[Kd];
}

void Vf52Setup(World &w, std::mt19937 &)
{
    w.vtP[23] = Fn(&Out2<2023>);
    w.vtP[34] = Fn(&Out2<2034>);
    w.vtP[53] = Fn(&S7<2053>);
}

} // namespace

PS_TEST(TSPersistSystem2_vf3, 0x1e0890)
{
    using F = uint32_t(__thiscall *)(void *, const uint32_t *, uint32_t, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1e0890);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf3", seed, &RouteSetup, [&](World &w) { return orig(w.primary, gKey, 1, 2, 3, seed); },
                [&](World &w) { return psshape::Call(PS(vf3), As<uint8_t>(w, 0), gKey, 1, 2, 3, seed); });
    }
}

PS_TEST(TSPersistSystem2_vf168_5, 0x1e08cf)
{
    using F = uint32_t(__thiscall *)(void *, const uint32_t *, uint32_t, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1e08cf);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_5", seed, &RouteSetup, [&](World &w) { return orig(Sub(w, 0xa8), gKey, 7, 8, seed, 9); },
                [&](World &w) { return psshape::Call(PSX(GetResource), As<uint8_t>(w, 0xa8), gKey, 7, 8, seed, 9); });
    }
}

PS_TEST(TSPersistSystem2_vf4, 0x1e08e0)
{
    using F = uint8_t(__thiscall *)(void *, const uint32_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1e08e0);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf4", seed, &RouteSetup, [&](World &w) { return uint32_t(orig(w.primary, gKey, 1, seed, 3, 4, 5)); },
                [&](World &w) { return uint32_t(psshape::Call(PSX(vf4), As<uint8_t>(w, 0), gKey, 1, seed, 3, 4, 5)); });
    }
}

PS_TEST(TSPersistSystem2_vf5, 0x1e04a0)
{
    using F = uint8_t(__thiscall *)(void *, const uint32_t *, uint32_t, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1e04a0);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf5", seed, &RouteSetup, [&](World &w) { return uint32_t(orig(w.primary, gKey, seed, 2, 3, 4)); },
                [&](World &w) { return uint32_t(psshape::Call(PSX(vf5), As<uint8_t>(w, 0), gKey, seed, 2, 3, 4)); });
    }
}

PS_TEST(TSPersistSystem2_vf49, 0x1dfd00)
{
    using F = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1dfd00);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf49", seed, nullptr, [&](World &w) { return uint32_t(orig(w.primary, seed, 5, 6)); },
                [&](World &w) { return uint32_t(psshape::Call(PS(vf49), As<uint8_t>(w, 0), seed, 5, 6)); });
    }
}

PS_TEST(TSPersistSystem2_vf52, 0x1dd9d0)
{
    using F = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1dd9d0);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        uint32_t a6 = seed % 3 ? 0 : seed;
        Compare(ctx, "vf52", seed, &Vf52Setup, [&](World &w) { return uint32_t(orig(w.primary, seed, 2, 3, 4, 5, a6)); },
                [&](World &w) { return uint32_t(psshape::Call(PS(vf52), As<uint8_t>(w, 0), seed, 2, 3, 4, 5, a6)); });
    }
}

namespace {

void Vf54Setup(World &w, std::mt19937 &rng)
{
    Put(w.primary, 0x1c0, rng() % 4 ? Addr(w.obj[3]) : 0u);
    w.vt[3][11] = Fn(&S0<K(3, 11)>);
}

} // namespace

PS_TEST(TSPersistSystem2_vf54, 0x1defd0)
{
    using F = void(__thiscall *)(void *);
    auto orig = ctx.Original<F>(0x1defd0);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf54", seed, &Vf54Setup,
                [&](World &w) {
                    orig(w.primary);
                    return 0u;
                },
                [&](World &w) {
                    psshape::Call(PS(ClearSpeculativeLoader), As<uint8_t>(w, 0));
                    return 0u;
                });
    }
}

PS_TEST(TSPersistSystem2_OnIdle, 0x1e2740)
{
    using F = uint8_t(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<F>(0x1e2740);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        uint32_t a1 = seed % 3 ? 0 : seed;
        Compare(ctx, "OnIdle", seed, nullptr, [&](World &w) { return uint32_t(orig(Sub(w, 4), a1)); },
                [&](World &w) { return uint32_t(psshape::Call(PSX(OnIdle), As<uint8_t>(w, 4), a1)); });
    }
}

namespace {

// vf168_39/40: objects at sub+0x38/0x3c/0x40 (primary +0xe0/+0xe4/+0xe8) are fakes 4, 5, 6 or null; the global
// object at 0x10cc140 is fake 7 (slot 5).
void TripleSetup(World &w, std::mt19937 &rng)
{
    for (int i = 0; i < 3; ++i) {
        Put(w.primary, 0xe0 + 4 * i, rng() % 4 ? Addr(w.obj[4 + i]) : 0u);
        w.vt[4 + i][17] = Fn(&S0<K(4, 17)>);
        w.vt[4 + i][23] = Fn(&S3<K(4, 23)>);
    }
    w.vt[7][5] = Fn(&S0<K(7, 5)>);
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_39, 0x1def30)
{
    using F = void(__thiscall *)(void *);
    auto orig = ctx.Original<F>(0x1def30);
    auto *global = static_cast<void **>(ctx.image.At(0x10cc140));
    void *saved = *global;
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_39", seed, &TripleSetup,
                [&](World &w) {
                    *global = w.obj[7];
                    orig(Sub(w, 0xa8));
                    return 0u;
                },
                [&](World &w) {
                    *global = w.obj[7];
                    psshape::Call(PSX(AgeCaches), As<uint8_t>(w, 0xa8));
                    return 0u;
                });
    }
    *global = saved;
}

PS_TEST(TSPersistSystem2_vf168_40, 0x1df160)
{
    using F = void(__thiscall *)(void *);
    auto orig = ctx.Original<F>(0x1df160);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_40", seed, &TripleSetup,
                [&](World &w) {
                    orig(Sub(w, 0xa8));
                    return 0u;
                },
                [&](World &w) {
                    psshape::Call(PSX(PurgeCaches), As<uint8_t>(w, 0xa8));
                    return 0u;
                });
    }
}

namespace {

// vf168_69: obj is fake 2 (slot 19: (a2, a3, &out)); out objects have slot 0 (two arguments) and Release.
void Vf69Setup(World &w, std::mt19937 &)
{
    w.vt[2][19] = Fn(&Out3<K(2, 19)>);
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_69, 0x1e0680)
{
    using F = uint8_t(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *);
    auto orig = ctx.Original<F>(0x1e0680);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        bool nullObj = seed % 7 == 0, nullStatus = seed % 5 == 0;
        uint32_t a5 = seed % 6 == 1 ? 0 : seed;
        Compare(ctx, "vf168_69", seed, &Vf69Setup,
                [&](World &w) {
                    auto *status = reinterpret_cast<uint32_t *>(w.obj[15] + 8);
                    uint8_t r = orig(Sub(w, 0xa8), nullObj ? nullptr : w.obj[2], 3, 4, 5, a5, nullStatus ? nullptr : status);
                    return uint32_t(r);
                },
                [&](World &w) {
                    auto *status = reinterpret_cast<uint32_t *>(w.obj[15] + 8);
                    uint8_t r = psshape::Call(PSX(OpenRecordFromFileRef), As<uint8_t>(w, 0xa8), nullObj ? nullptr : w.obj[2], 3, 4, 5, a5,
                                                                         nullStatus ? nullptr : status);
                    return uint32_t(r);
                });
    }
}

// vf168_70 creates a real cTSPersistDBRecord and queries it. Compared: the result, whether the out pointer was
// written, the vtable of what it names (which subobject), and the reference count afterwards (AddRef through the
// out pointer returns it); then both references are dropped, which frees the record. A record the query does not
// hand out is released inside the call (count 1 to 0), so its delete runs there on both sides.
PS_TEST(TSPersistSystem2_vf168_70, 0x1de1e0)
{
    using F = uint8_t(__thiscall *)(void *, uint32_t, void **);
    using Count = uint32_t(__thiscall *)(void *);
    auto orig = ctx.Original<F>(0x1de1e0);
    static const uint32_t kIds[] = {cTSPersistDBRecordObject::kIdITS, cTSPersistDBRecordObject::kIdIGZ, 1, 0, 0x12345678};
    alignas(8) static uint8_t dummy[0x300] = {};
    for (int round = 0; round < 4; ++round) {
        for (uint32_t riid : kIds) {
            void *const kUnset = reinterpret_cast<void *>(0x1234);
            struct Seen {
                uint8_t r;
                bool written;
                const void *vtable;
                uint32_t count;
            } seen[2];
            for (int side = 0; side < 2; ++side) {
                void *out = kUnset;
                uint8_t r = side == 0 ? orig(dummy + 0xa8, riid, &out)
                                      : psshape::Call(PSX(CreateRecord), dummy + 0xa8, riid, &out);
                Seen s{r, out != kUnset, nullptr, 0};
                if (s.written && out) {
                    s.vtable = *static_cast<void **>(out);
                    s.count = VirtualSlot<Count>(out, 1)(out);
                    VirtualSlot<Count>(out, 2)(out);
                    VirtualSlot<Count>(out, 2)(out);
                }
                seen[side] = s;
            }
            ++ctx.cases;
            if (seen[0].r != seen[1].r || seen[0].written != seen[1].written || seen[0].vtable != seen[1].vtable ||
                seen[0].count != seen[1].count)
                ctx.Fail("vf168_70 riid 0x%x: original (%u, written %d, count %u), ours (%u, written %d, count %u)", riid, seen[0].r,
                         seen[0].written, seen[0].count, seen[1].r, seen[1].written, seen[1].count);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------
// Second batch: listener vector, record creation, framework queries, shutdown and tick.

namespace {

// Fakes that return an object of the world (gOutIdx[K]; -1 gives null).
template <int Kd> void *__fastcall RetObj(void *self, void *)
{
    Note(Kd, self);
    return OutTarget(Kd);
}
// A fake that clears the field at gClearOff (when set) after recording, so callers that read the field again
// see the change.
template <int Kd> uint32_t __fastcall S0Clear(void *self, void *)
{
    Note(Kd, self);
    if (gClearOff && (gRet[Kd] & 4)) {
        Put(gWorld->primary, gClearOff, 0u);
        Rec(0xc1ea);
    }
    return gRet[Kd];
}
// One argument, returning gRet[K] on the first call and gRet[K + 1] on later ones.
template <int Kd> uint32_t __fastcall S1Seq(void *self, void *, uint32_t a)
{
    Note(Kd, self);
    Rec(a);
    return gRet[Kd + (gCount[Kd]++ ? 1 : 0)];
}
// One argument that points at a 16-byte block: records the block's words.
template <int Kd> uint32_t __fastcall Block1(void *self, void *, uint32_t block)
{
    Note(Kd, self);
    const auto *words = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(block));
    for (int i = 0; i < 4; ++i) Rec(words[i]);
    return gRet[Kd];
}

// Stands in for GetDirectorFrameworkObject (0x1ae18d) on both sides: the framework is fake object 10.
void *__cdecl FrameworkStandIn()
{
    gTrace.push_back(0xf00d);
    return gWorld->obj[10];
}

// Redirects a function (the image's, or ours) to a stand-in while it lives (a jmp over its first five bytes).
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(void *at, void *stand) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

struct FrameworkPatches {
    CodePatch original;
    CodePatch ours;
    explicit FrameworkPatches(difftest::Context &ctx)
        : original(ctx.image.At(0x1ae18d), reinterpret_cast<void *>(&FrameworkStandIn)),
          ours(reinterpret_cast<void *>(&GetDirectorFrameworkObject), reinterpret_cast<void *>(&FrameworkStandIn))
    {
    }
};

// Out objects (fakes 0..7) get the given slot fakes in every test of this batch that needs them.
template <typename F> void ForOut(World &w, int slot, F f)
{
    for (int i = 0; i < 8; ++i) w.vt[i][slot] = Fn(f);
}

void Vf21Setup(World &w, std::mt19937 &)
{
    w.vt[10][3] = Fn(&Out3<K(10, 3)>);
    gRet[K(10, 3)] |= 1; // the original uses the queried object unchecked
    ForOut(w, 11, &S5<K(0, 11)>);
    w.vtI[20] = Fn(&S3<2220>);
}

uint32_t gInfo[3];

void Vf32Setup(World &w, std::mt19937 &rng)
{
    w.vtI[30] = Fn(&S2<2230>);
    w.vt[9][20] = Fn(&S1<K(9, 20)>);
    w.vt[9][0] = Fn(&Out2<K(9, 0)>);
    ForOut(w, 13, &S1Seq<K(0, 13)>);
    static const uint32_t kReads[] = {0xffffffffu, 2, 0, 5};
    gRet[K(0, 13)] = kReads[rng() % 4];
    gRet[K(0, 13) + 1] = kReads[rng() % 4];
    static const uint32_t kKinds3[] = {0x93, 0x12d, 0x12e, 0x92, 0x12f};
    gInfo[0] = rng();
    gInfo[1] = rng();
    gInfo[2] = kKinds3[rng() % 5];
}

void Vf51Setup(World &w, std::mt19937 &rng)
{
    w.vtP[54] = Fn(&S0<2054>);
    ForOut(w, 0, &Out2<K(0, 0)>);
    ForOut(w, 5, &S0<K(0, 5)>);
    ForOut(w, 8, &S1<K(0, 8)>);
    int n = static_cast<int>(rng() % 7);
    for (int i = 0; i < n; ++i) w.items[i] = w.obj[rng() % 8];
    w.itemsVec[0] = w.items;
    w.itemsVec[1] = w.items + n;
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_21, 0x1dda60)
{
    using F = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1dda60);
    FrameworkPatches patches(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_21", seed, &Vf21Setup, [&](World &w) { return uint32_t(orig(Sub(w, 0xa8), seed, 2, 3, 4, 5, 6, 7)); },
                [&](World &w) { return uint32_t(psshape::Call(PSX(OpenSegmentFromValues), As<uint8_t>(w, 0xa8), seed, 2, 3, 4, 5, 6, 7)); });
    }
}

PS_TEST(TSPersistSystem2_vf168_32, 0x1dfe10)
{
    using F = uint32_t(__thiscall *)(void *, void *, const uint32_t *);
    auto orig = ctx.Original<F>(0x1dfe10);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        Compare(ctx, "vf168_32", seed, &Vf32Setup, [&](World &w) { return orig(Sub(w, 0xa8), w.obj[9], gInfo); },
                [&](World &w) { return psshape::Call(PSX(ScoreSegment), As<uint8_t>(w, 0xa8), w.obj[9], gInfo); });
    }
}

PS_TEST(TSPersistSystem2_vf168_51, 0x1de7b0)
{
    using F = uint8_t(__thiscall *)(void *, void *const *);
    auto orig = ctx.Original<F>(0x1de7b0);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_51", seed, &Vf51Setup, [&](World &w) { return uint32_t(orig(Sub(w, 0xa8), w.itemsVec)); },
                [&](World &w) { return uint32_t(psshape::Call(PSX(CommitSegments), As<uint8_t>(w, 0xa8), w.itemsVec)); });
    }
}

namespace {

// AddListener: the listener is one of fakes 12..15; the vector has spare room (the world's array, capacity 8) or,
// in a third of the worlds, is a full heap block that the call must reallocate (and free).
bool gHeapVector = false;

void Vf61Setup(World &w, std::mt19937 &rng)
{
    gAllBlocks = true;
    gHeapVector = rng() % 3 == 0;
}

// After the call, a heap vector is moved back into the world's array so the world compares by contents (its
// size and capacity are recorded), and the heap block is freed.
void NormaliseVector(World &w)
{
    void **first, **last, **end;
    std::memcpy(&first, w.primary + 0x230, 4);
    std::memcpy(&last, w.primary + 0x234, 4);
    std::memcpy(&end, w.primary + 0x238, 4);
    Rec(static_cast<uint32_t>(last - first));
    Rec(static_cast<uint32_t>(end - first));
    if (first == w.listeners) return;
    size_t n = static_cast<size_t>(last - first);
    if (n > 8) n = 8;
    for (size_t i = 0; i < n; ++i) w.listeners[i] = first[i];
    ::operator delete(first);
    Put(w.primary, 0x230, Addr(w.listeners));
    Put(w.primary, 0x234, Addr(w.listeners + n));
    Put(w.primary, 0x238, Addr(w.listeners + 8));
}

// A full heap vector with the world's listeners (its size equals its capacity).
void MakeHeapVector(World &w)
{
    void **first, **last;
    std::memcpy(&first, w.primary + 0x230, 4);
    std::memcpy(&last, w.primary + 0x234, 4);
    size_t n = static_cast<size_t>(last - first);
    auto **block = static_cast<void **>(::operator new(n ? n * 4 : 4));
    for (size_t i = 0; i < n; ++i) block[i] = first[i];
    Put(w.primary, 0x230, Addr(block));
    Put(w.primary, 0x234, Addr(block + n));
    Put(w.primary, 0x238, Addr(block + n));
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_61, 0x1daa20)
{
    using F = void(__thiscall *)(void *, void *);
    auto orig = ctx.Original<F>(0x1daa20);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        int which = 12 + static_cast<int>(seed % 4);
        Compare(ctx, "vf168_61", seed, &Vf61Setup,
                [&](World &w) {
                    if (gHeapVector) MakeHeapVector(w);
                    orig(Sub(w, 0xa8), w.obj[which]);
                    NormaliseVector(w);
                    return 0u;
                },
                [&](World &w) {
                    if (gHeapVector) MakeHeapVector(w);
                    psshape::Call(PSX(AddListener), As<uint8_t>(w, 0xa8), w.obj[which]);
                    NormaliseVector(w);
                    return 0u;
                });
    }
}

namespace {

// RemoveListener: the vector holds 0..8 entries of fakes 12..15 (some null); the listener removed may be absent.
void Vf62Setup(World &w, std::mt19937 &rng)
{
    gAllBlocks = true;
    int n = static_cast<int>(rng() % 9);
    for (int i = 0; i < n; ++i) w.listeners[i] = rng() % 6 ? w.obj[12 + rng() % 4] : nullptr;
    Put(w.primary, 0x234, Addr(w.listeners + n));
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_62, 0x1e39c0)
{
    using F = void(__thiscall *)(void *, void *);
    auto orig = ctx.Original<F>(0x1e39c0);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        int which = 12 + static_cast<int>(seed % 4);
        Compare(ctx, "vf168_62", seed, &Vf62Setup,
                [&](World &w) {
                    orig(Sub(w, 0xa8), w.obj[which]);
                    return 0u;
                },
                [&](World &w) {
                    psshape::Call(PSX(RemoveListener), As<uint8_t>(w, 0xa8), w.obj[which]);
                    return 0u;
                });
    }
}

namespace {

// vf168_71's slot 27 gets a cRZString local: its text is recorded.
template <int Kd>
uint32_t __fastcall StringSlot(void *self, void *, uint32_t a, uint32_t b, uint32_t c, const cRZString *s, uint32_t flag)
{
    Note(Kd, self);
    Rec(a);
    Rec(b);
    Rec(c);
    for (const char *p = s->Data(); *p; ++p) gTrace.push_back(static_cast<uint8_t>(*p));
    Rec(static_cast<uint32_t>(s->mString.size()));
    Rec(flag);
    return gRet[Kd];
}

void Vf71Setup(World &w, std::mt19937 &)
{
    w.vt[2][27] = Fn(&StringSlot<K(2, 27)>);
    w.vt[2][28] = Fn(&S1<K(2, 28)>);
    w.vt[2][29] = Fn(&S1<K(2, 29)>);
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_71, 0x1dd410)
{
    using F = uint8_t(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t, void *);
    auto orig = ctx.Original<F>(0x1dd410);
    cRZString name;
    name.Construct("segment name");
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        bool nullObj = seed % 9 == 0, nullName = seed % 4 == 1;
        uint32_t a2 = seed % 7 == 3 ? 0 : seed + 1;
        void *n = nullName ? nullptr : &name;
        Compare(ctx, "vf168_71", seed, &Vf71Setup,
                [&](World &w) { return uint32_t(orig(Sub(w, 0xa8), nullObj ? nullptr : w.obj[2], a2, 3, 4, n)); },
                [&](World &w) { return uint32_t(psshape::Call(PSX(CommitRecordToFileRef), As<uint8_t>(w, 0xa8), nullObj ? nullptr : w.obj[2], a2, 3, 4, n)); });
    }
    name.Destruct();
}

namespace {

// vf168_72: a1 is fake 9 (slots 3, 5), a2 fake 8 (slot 11); the record comes from ITS slot 70 (an out object
// with slots 25 (block), 3); ITS slot 71 takes five arguments.
void Vf72Setup(World &w, std::mt19937 &)
{
    gAllBlocks = true;
    w.vtI[70] = Fn(&Out2<2270>);
    gRet[2270] |= 1; // the original uses the record unchecked
    w.vtI[71] = Fn(&S5<2271>);
    ForOut(w, 25, &Block1<K(0, 25)>);
    ForOut(w, 3, &S0<K(0, 3)>);
    w.vt[9][3] = Fn(&S1<K(9, 3)>);
    w.vt[9][5] = Fn(&S0<K(9, 5)>);
    w.vt[8][11] = Fn(&S0<K(8, 11)>);
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_72, 0x1e5e70)
{
    using F = uint32_t(__thiscall *)(void *, void *, void *, uint32_t);
    auto orig = ctx.Original<F>(0x1e5e70);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        uint32_t a3 = seed * 0x10001u + 0x8000u;
        Compare(ctx, "vf168_72", seed, &Vf72Setup, [&](World &w) { return orig(Sub(w, 0xa8), w.obj[9], w.obj[8], a3); },
                [&](World &w) { return psshape::Call(PSX(WriteResourceToFileRef), As<uint8_t>(w, 0xa8), w.obj[9], w.obj[8], a3); });
    }
}

namespace {

// ITS slot 69 for vf168_73: (a2, group, a3, riid, &local, &status); writes the out object as the out fakes do and
// the status (gRet[K + 1]).
template <int Kd>
uint8_t __fastcall Slot69(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, void **out, uint32_t *status)
{
    Note(Kd, self);
    for (uint32_t v : {a, b, c, d}) Rec(v);
    uint8_t ok = gRet[Kd] & 1;
    if (ok || gWriteOnFail) *out = OutFor(Kd, ok);
    *status = gRet[Kd + 1];
    return ok;
}
// The record's target slot 19 receives a new UtilObjectB: its words are recorded and the block is freed.
template <int Kd> uint32_t __fastcall TakeUtil(void *self, void *, const uint32_t *util)
{
    Note(Kd, self);
    if (util) {
        for (int i = 0; i < 5; ++i) Rec(util[i]);
        ::operator delete(const_cast<uint32_t *>(util));
    }
    return 0;
}

void Vf73Setup(World &w, std::mt19937 &)
{
    gAllBlocks = true;
    w.vtI[69] = Fn(&Slot69<2269>);
    ForOut(w, 4, &RetObj<K(0, 4)>);
    ForOut(w, 19, &TakeUtil<K(0, 19)>);
    gOutIdx[K(0, 4)] = static_cast<int>(gRet[K(0, 4)] % 8);
    w.vt[9][4] = Fn(&S1<K(9, 4)>);
    w.vt[9][5] = Fn(&S0<K(9, 5)>);
    w.vt[8][11] = Fn(&S0<K(8, 11)>);
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_73, 0x1e3030)
{
    using F = uint32_t(__thiscall *)(void *, void *, void *, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1e3030);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        uint32_t a3 = seed * 0x10001u + 0x7ff0u, a4 = seed % 3 ? seed : 0;
        Compare(ctx, "vf168_73", seed, &Vf73Setup, [&](World &w) { return orig(Sub(w, 0xa8), w.obj[9], w.obj[8], a3, a4); },
                [&](World &w) { return psshape::Call(PSX(ReadResourceFromSegment), As<uint8_t>(w, 0xa8), w.obj[9], w.obj[8], a3, a4); });
    }
}

namespace {

uint32_t gKey3[3];

void Vf74Setup(World &w, std::mt19937 &rng)
{
    w.vtP[11] = Fn(&S1<2011>);
    w.vtP[8] = Fn(&S1<2008>);
    w.vtP[4] = Fn(&S6<2004>);
    w.vt[10][3] = Fn(&Out3<K(10, 3)>);
    gRet[K(10, 3)] |= 1; // the original uses the queried object unchecked
    ForOut(w, 4, &S2<K(0, 4)>);
    ForOut(w, 14, &S0<K(0, 14)>);
    for (auto &k : gKey3) k = rng();
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_74, 0x1df640)
{
    using F = uint8_t(__thiscall *)(void *, const uint32_t *, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1df640);
    FrameworkPatches patches(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_74", seed, &Vf74Setup, [&](World &w) { return uint32_t(orig(Sub(w, 0xa8), gKey3, seed, 9)); },
                [&](World &w) { return uint32_t(psshape::Call(PSX(GetAnimTable), As<uint8_t>(w, 0xa8), gKey3, seed, 9)); });
    }
}

namespace {

// Shutdown: object B (+0x1c0, fake 3: slots 4, 2) and the segments at +0xe0..+0xec (fakes 4..7: slots 4, 2) may
// be null; slot 4 of either may clear its own field (so the reloads matter).
void ShutdownSetup(World &w, std::mt19937 &rng)
{
    w.vtI[68] = Fn(&S0<2268>);
    w.vtP[32] = Fn(&S0<2032>);
    w.vtP[45] = Fn(&S0<2045>);
    w.vtP[42] = Fn(&S1<2042>);
    Put(w.primary, 0x1c0, rng() % 4 ? Addr(w.obj[3]) : 0u);
    for (int i = 0; i < 4; ++i) Put(w.primary, 0xe0 + 4 * i, rng() % 4 ? Addr(w.obj[4 + i]) : 0u);
    for (int i = 3; i < 8; ++i) {
        w.vt[i][4] = Fn(&S0Clear<K(3, 4)>);
        w.vt[i][2] = Fn(&S0<K(3, 2)>);
    }
    static const size_t kOffs[] = {0, 0x1c0, 0xe0, 0xe4, 0xe8, 0xec};
    gClearOff = kOffs[rng() % 6];
}

} // namespace

PS_TEST(TSPersistSystem2_ReleaseSegments, 0x1e5580)
{
    using F = void(__thiscall *)(void *);
    auto orig = ctx.Original<F>(0x1e5580);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "ReleaseSegments", seed, &ShutdownSetup,
                [&](World &w) {
                    orig(w.primary);
                    return 0u;
                },
                [&](World &w) {
                    reinterpret_cast<cTSPersistSystem *>(As<uint8_t>(w, 0))->ReleaseSegments(); // hooked at the member
                    return 0u;
                });
    }
}

PS_TEST(TSPersistSystem2_PreAppShutdown, 0x1e2ce0)
{
    using F = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<F>(0x1e2ce0);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "PreAppShutdown", seed, &ShutdownSetup, [&](World &w) { return uint32_t(orig(Sub(w, 0xac))); },
                [&](World &w) { return uint32_t(psshape::Call(PSX(PreAppShutdown), As<uint8_t>(w, 0xac))); });
    }
}

namespace {

// OnTick: object B (+0x1c0, fake 3, slot 10) may be null; the byte at +0x22d gates the primary's slot 47 (an
// object or null, slot 10); the flush 0x1dedf0 runs (the original, and ours on our side) with its flag at +0x22c and an empty
// list at +0x210; the stopwatch at +0x1c8 is stopped in mode 1 (start 0), so Elapsed is its accumulated count,
// compared against the limit at +0x1e0 (zero in some worlds).
void TickSetup(World &w, std::mt19937 &rng)
{
    Put(w.primary, 0x1c0, rng() % 4 ? Addr(w.obj[3]) : 0u);
    w.vt[3][10] = Fn(&S0<K(3, 10)>);
    ForOut(w, 10, &S0<K(0, 10)>);
    w.vtP[47] = Fn(&RetObj<2047>);
    w.vtP[32] = Fn(&S0<2032>);
    w.primary[0x22d] = rng() % 3 == 0;
    w.primary[0x22c] = rng() % 2;
    w.list2[0] = Addr(w.list2);
    w.list2[1] = Addr(w.list2);
    Put(w.primary, 0x210, Addr(w.list2));
    Put(w.primary, 0x1c8, 1u);
    Put(w.primary, 0x1d0, 0u);
    Put(w.primary, 0x1d4, 0u);
    uint32_t acc = rng() % 1000;
    Put(w.primary, 0x1d8, acc);
    Put(w.primary, 0x1dc, rng() % 4 ? 0u : 1u);
    uint32_t limit = rng() % 4 == 0 ? 0 : acc + static_cast<uint32_t>(rng() % 5) - 2;
    Put(w.primary, 0x1e0, limit);
}

// The restart writes the counter's current value into the stopwatch: record whether it did, then clear it.
void NormaliseStopwatch(World &w)
{
    uint32_t lo, hi;
    std::memcpy(&lo, w.primary + 0x1d0, 4);
    std::memcpy(&hi, w.primary + 0x1d4, 4);
    Rec((lo | hi) != 0);
    Put(w.primary, 0x1d0, 0u);
    Put(w.primary, 0x1d4, 0u);
}

} // namespace

PS_TEST(TSPersistSystem2_OnTick, 0x1e27c0)
{
    using F = uint8_t(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<F>(0x1e27c0);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "OnTick", seed, &TickSetup,
                [&](World &w) {
                    uint32_t r = orig(Sub(w, 4), seed);
                    NormaliseStopwatch(w);
                    return r & 0xff;
                },
                [&](World &w) {
                    uint32_t r = psshape::Call(PSX(OnTick), As<uint8_t>(w, 4), seed);
                    NormaliseStopwatch(w);
                    return r;
                });
    }
}

// ---------------------------------------------------------------------------------------------------------
// Third batch: segment vector, key filter, registration. Unreplaced original callees are stood in for on both
// sides (the image's entry is patched; ours reaches the same entry through OriginalFunction).

namespace {

// Stand-in for the base vf14 (0x9b5610, thiscall, ret 0xc): (a1, key, local).
uint8_t __fastcall BaseVf14StandIn(void *self, void *, uint32_t a1, const uint32_t *key, uint32_t local)
{
    Note(3014, self);
    Rec(a1);
    for (int i = 0; i < 4; ++i) Rec(key[i]);
    Rec(local);
    return static_cast<uint8_t>(gRet[3014]);
}
// Stand-in for 0x1c3505 (cdecl: path string, 2, 1): records the text.
uint32_t __cdecl FileAttrStandIn(const cRZString *path, uint32_t a, uint32_t b)
{
    gTrace.push_back(3505);
    for (const char *p = path->Data(); *p; ++p) gTrace.push_back(static_cast<uint8_t>(*p));
    Rec(a);
    Rec(b);
    return gRet[3505];
}
// Stand-in for 0x1e9b50 (thiscall on the primary, ret 4).
void __fastcall ReplaceSegmentStandIn(void *self, void *, uint32_t seg)
{
    Note(3950, self);
    Rec(seg);
}
// Stand-in for the vector's InsertGrown (0x1d57e0 and ours, thiscall, ret 8): records the value and the place.
void **__fastcall InsertGrownStandIn(void *self, void *, void **where, void *const *value)
{
    Note(3570, self);
    Rec(Addr(where));
    Rec(Addr(*value));
    return where;
}

template <typename M> void *MemberCode(M m)
{
    static_assert(sizeof(m) == sizeof(void *), "plain member pointer");
    void *p;
    std::memcpy(&p, &m, sizeof(p));
    return p;
}

// a1->slot3(&key): writes a 16-byte key derived from gRet.
template <int Kd> uint32_t __fastcall FillKey(void *self, void *, uint32_t *key)
{
    Note(Kd, self);
    for (int i = 0; i < 4; ++i) key[i] = gRet[Kd + i];
    return 0;
}
// ITS slot 34 for vf168_16: (key, iid, &out).
template <int Kd> uint32_t __fastcall KeyQuery(void *self, void *, const uint32_t *key, uint32_t iid, void **out)
{
    Note(Kd, self);
    for (int i = 0; i < 4; ++i) Rec(key[i]);
    Rec(iid);
    uint8_t ok = gRet[Kd] & 1;
    if (ok || gWriteOnFail) *out = OutFor(Kd, ok);
    return ok;
}
// ITS slot 57 for vf168_16: (local, key, 1).
template <int Kd> uint32_t __fastcall Slot57(void *self, void *, uint32_t local, const uint32_t *key, uint32_t flag)
{
    Note(Kd, self);
    Rec(local);
    for (int i = 0; i < 4; ++i) Rec(key[i]);
    Rec(flag);
    return 0;
}
// local->slot11(&string): records the string's text and writes a path into it.
template <int Kd> uint32_t __fastcall NameOut(void *self, void *, cRZString *s)
{
    Note(Kd, self);
    for (const char *p = s->Data(); *p; ++p) gTrace.push_back(static_cast<uint8_t>(*p));
    s->mString.assign("seg\\file.package");
    return 0;
}

void Vf16Setup(World &w, std::mt19937 &rng)
{
    gAllBlocks = true;
    w.vt[9][3] = Fn(&FillKey<K(9, 3)>);
    w.vtP[10] = Fn(&Block1<2010>);
    w.vtI[34] = Fn(&KeyQuery<2234>);
    w.vtI[28] = Fn(&S1<2228>);
    w.vtI[57] = Fn(&Slot57<2257>);
    w.vt[8][1] = Fn(&S0<K(8, 1)>);
    w.vt[8][2] = Fn(&S0<K(8, 2)>);
    w.vt[8][11] = Fn(&NameOut<K(8, 11)>);
    ForOut(w, 11, &NameOut<K(0, 11)>);
    (void)rng;
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_16, 0x1e3b20)
{
    using F = uint8_t(__thiscall *)(void *, void *, void *);
    auto orig = ctx.Original<F>(0x1e3b20);
    // The base vf14 is stood in for on both sides: the original's entry, and ours (verified on its own,
    // tests/gz_persist_resource_manager_write.cpp), which our vf168_16 now calls directly.
    CodePatch base(ctx.image.At(0x9b5610), reinterpret_cast<void *>(&BaseVf14StandIn));
    CodePatch baseOurs(MemberAddress(&cGZPersistResourceManager::vf14), reinterpret_cast<void *>(&BaseVf14StandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        bool nullA2 = seed % 2 == 0;
        Compare(ctx, "vf168_16", seed, &Vf16Setup,
                [&](World &w) { return uint32_t(orig(Sub(w, 0xa8), w.obj[9], nullA2 ? nullptr : w.obj[8])); },
                [&](World &w) { return uint32_t(psshape::Call(PSX(SaveResource), As<uint8_t>(w, 0xa8), w.obj[9], nullA2 ? nullptr : w.obj[8])); });
    }
}

namespace {

void Vf26Setup(World &w, std::mt19937 &)
{
    w.vt[9][0] = Fn(&Out2<K(9, 0)>);
    w.vt[9][9] = Fn(&S0<K(9, 9)>);
    w.vt[9][10] = Fn(&S0<K(9, 10)>);
    w.vt[9][11] = Fn(&NameOut<K(9, 11)>);
    w.vt[9][6] = Fn(&S2<K(9, 6)>);
    w.vtI[28] = Fn(&S1<2228>);
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_26, 0x1e2430)
{
    using F = void(__thiscall *)(void *, void *);
    auto orig = ctx.Original<F>(0x1e2430);
    CodePatch attr(ctx.image.At(0x1c3505), reinterpret_cast<void *>(&FileAttrStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_26", seed, &Vf26Setup,
                [&](World &w) {
                    orig(Sub(w, 0xa8), w.obj[9]);
                    return 0u;
                },
                [&](World &w) {
                    psshape::Call(PSX(ReopenSegment), As<uint8_t>(w, 0xa8), w.obj[9]);
                    return 0u;
                });
    }
}

namespace {

// The segment vector (+0xd4/+0xd8/+0xdc) holds 0..8 of fakes 0..7 (slot 15 per object, AddRef/Release); the out
// vector is over items[] with 0..6 used and room for 0..6.
template <size_t... I> void SetSlot15(World &w, std::index_sequence<I...>)
{
    ((w.vt[I][15] = Fn(&S0<K(static_cast<int>(I), 15)>)), ...);
}

void SegmentSetup(World &w, std::mt19937 &rng)
{
    int n = static_cast<int>(rng() % 9);
    for (int i = 0; i < n; ++i) w.segs[i] = w.obj[rng() % 8];
    Put(w.primary, 0xd4, Addr(w.segs));
    Put(w.primary, 0xd8, Addr(w.segs + n));
    Put(w.primary, 0xdc, Addr(w.segs + 8));
    int used = static_cast<int>(rng() % 4), cap = used + static_cast<int>(rng() % 3);
    for (int i = 0; i < used; ++i) w.items[i] = w.obj[rng() % 8];
    w.outVec[0] = w.items;
    w.outVec[1] = w.items + used;
    w.outVec[2] = w.items + cap;
    w.vtI[28] = Fn(&S1<2228>);
    SetSlot15(w, std::make_index_sequence<8>());
    w.vt[9][15] = Fn(&S0<K(9, 15)>);
    // Some worlds look up an id one of the segments has.
    if (rng() % 2) gRet[K(9, 15)] = gRet[K(static_cast<int>(rng() % 8), 15)];
    w.vtP[27] = Fn(&S1<2027>);
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_29, 0x1e0980)
{
    using F = void(__thiscall *)(void *, void *);
    auto orig = ctx.Original<F>(0x1e0980);
    CodePatch grow(ctx.image.At(0x1d57e0), reinterpret_cast<void *>(&InsertGrownStandIn));
    CodePatch growOurs(MemberCode(&ServiceRefVector::InsertGrown), reinterpret_cast<void *>(&InsertGrownStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_29", seed, &SegmentSetup,
                [&](World &w) {
                    orig(Sub(w, 0xa8), w.outVec);
                    return 0u;
                },
                [&](World &w) {
                    psshape::Call(PSX(GetWritableSegments), As<uint8_t>(w, 0xa8), w.outVec);
                    return 0u;
                });
    }
}

PS_TEST(TSPersistSystem2_vf168_45, 0x1e3630)
{
    using F = uint8_t(__thiscall *)(void *, void *);
    auto orig = ctx.Original<F>(0x1e3630);
    CodePatch replace(ctx.image.At(0x1e9b50), reinterpret_cast<void *>(&ReplaceSegmentStandIn));
    CodePatch replaceOurs(shape::MemberCode(&cTSPersistSystem::IndexSegmentGroups), reinterpret_cast<void *>(&ReplaceSegmentStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_45", seed, &SegmentSetup, [&](World &w) { return uint32_t(orig(Sub(w, 0xa8), w.obj[9])); },
                [&](World &w) { return uint32_t(psshape::Call(PSX(RegisterSegment), As<uint8_t>(w, 0xa8), w.obj[9])); });
    }
}

namespace {

// vf19: ITS slot 3 hands back the primary; primary slot 18 receives the new filter and records its fields (the
// tree head by shape, not address); the filter is then released through the game's vtable (count 1 to 0: freed).
void *__fastcall ItsPrimary(void *self, void *)
{
    Note(2203, self);
    return gWorld->primary;
}

const void *gFilter = nullptr;
const void *gFilterHead = nullptr;

uint32_t __fastcall TakeFilter(void *self, void *, uint32_t a1, const uint8_t *filter)
{
    Note(2018, self);
    gFilter = filter;
    std::memcpy(&gFilterHead, filter + 8, 4);
    Rec(a1);
    uint32_t words[7];
    std::memcpy(words, filter, sizeof(words));
    Rec(words[0]); // the game's vtable (the same address on both sides)
    Rec(words[1]);
    const auto *head = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(words[2]));
    Rec(head[0] == words[2] && head[1] == words[2] && head[2] == words[2]);
    Rec(reinterpret_cast<const uint8_t *>(head)[0xc]);
    Rec(reinterpret_cast<const uint8_t *>(head)[0xd]);
    Rec(words[3]);
    Rec(words[4]);
    Rec(words[6] & 0xffff); // +0x18 word (+0x14 is not written by the creator)
    difftest::BeginFrees(); // from here to the end of the call: are the filter and its head freed
    return gRet[2018];
}

void Vf19Setup(World &w, std::mt19937 &)
{
    w.vtI[3] = Fn(&ItsPrimary);
    w.vtP[18] = Fn(&TakeFilter);
}

} // namespace

PS_TEST(TSPersistSystem2_vf19, 0x1df9b0)
{
    using F = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1df9b0);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        // After the call the filter and its tree head must have been freed (the release drops the only reference).
        // (A FreeRecorder from the filter's hand-over in TakeFilter to here; 2 when no filter was handed over.)
        auto freed = [] {
            if (gFilter == nullptr) {
                Rec(2u);
                Rec(2u);
                return;
            }
            const bool filterFreed = difftest::FreedSinceBegin(gFilter), headFreed = difftest::FreedSinceBegin(gFilterHead);
            Rec(filterFreed ? 0u : 1u);
            Rec(headFreed ? 0u : 1u);
            gFilter = gFilterHead = nullptr;
            difftest::CurrentFrees().reset();
        };
        Compare(ctx, "vf19", seed, &Vf19Setup,
                [&](World &w) {
                    uint32_t r = orig(w.primary, seed, seed * 7);
                    freed();
                    return r;
                },
                [&](World &w) {
                    uint32_t r = psshape::Call(PSX(vf19), As<uint8_t>(w, 0), seed, seed * 7);
                    freed();
                    return r;
                });
    }
}

// ---------------------------------------------------------------------------------------------------------
// The secondary subobjects' AddRef / Release / QueryInterface thunks.

namespace {

// The cRZUnknownMT part at +8 gets fake 5's vtable (slot 3: the deleting destructor a last Release calls) and a
// reference count (+0xc) of 0, 1, 2 or a random value; the primary's slot 1 (QueryInterface's AddRef) records.
void ThunkSetup(World &w, std::mt19937 &rng)
{
    Put(w.primary, 8, Addr(w.vt[5]));
    w.vt[5][3] = Fn(&S1<K(5, 3)>);
    static const uint32_t kCounts[] = {0, 1, 2, 5};
    Put(w.primary, 0xc, rng() % 5 ? kCounts[rng() % 4] : static_cast<uint32_t>(rng()));
    w.vtP[1] = Fn(&S0<2001>);
}

struct ThunkCase {
    const char *name;
    uint32_t rva;
    uint32_t offset;
    int kind; // 0 AddRef, 1 Release, 2 QueryInterface
    void *ours;
};

// Since lane GD these are the compiler's adjustor thunks in our vtables: the slot `slot` of our vtable at `offset`.
unsigned FaceAt(uint32_t offset)
{
    for (unsigned f = 0; f < 6; ++f)
        if (psshape::kFaces[f] == offset) return f;
    return 0;
}
void *OurSlot(uint32_t offset, unsigned slot) { return psshape::OurVTable(FaceAt(offset))[slot]; }

} // namespace

DIFF_TEST(TSPersistSystem2_Thunks, 0x1dab35)
{
    // Every RVA here is covered: 0x1dab35 0x1dab3d 0x1dab45 0x1dab50 0x1e3985 0x1e398d 0x1e3995 0x1e39a0 0x1e39ab
    // 0x1e2ed7 0x1e2edf 0x1e2ee7 0x1e2ef2.
    const ThunkCase cases[] = {
        {"AddRef+4", 0x1dab35, 4, 0, OurSlot(4, 1)},
        {"AddRef+8", 0x1dab3d, 8, 0, OurSlot(8, 1)},
        {"AddRef+a8", 0x1dab45, 0xa8, 0, OurSlot(0xa8, 1)},
        {"AddRef+ac", 0x1dab50, 0xac, 0, OurSlot(0xac, 1)},
        {"Release+4", 0x1e3985, 4, 1, OurSlot(4, 2)},
        {"Release+8", 0x1e398d, 8, 1, OurSlot(8, 2)},
        {"Release+a8", 0x1e3995, 0xa8, 1, OurSlot(0xa8, 2)},
        {"Release+ac", 0x1e39a0, 0xac, 1, OurSlot(0xac, 2)},
        {"Release+b0", 0x1e39ab, 0xb0, 1, OurSlot(0xb0, 1)},
        {"QI+4", 0x1e2ed7, 4, 2, OurSlot(4, 0)},
        {"QI+8", 0x1e2edf, 8, 2, OurSlot(8, 0)},
        {"QI+a8", 0x1e2ee7, 0xa8, 2, OurSlot(0xa8, 0)},
        {"QI+ac", 0x1e2ef2, 0xac, 2, OurSlot(0xac, 0)},
    };
    using Count = uint32_t(__fastcall *)(void *, void *);
    using Qi = uint8_t(__fastcall *)(void *, void *, uint32_t, void **);
    static const uint32_t kIds[] = {0x9af5e924, 0x03fa40bf, 0x2efe1873, 0x656b8efc};
    for (const ThunkCase &c : cases) {
        // Our function sits in the slot where the game's vtable of that face holds the thunk's RVA.
        const unsigned face = FaceAt(c.offset);
        const unsigned slot = c.kind == 2 ? 0 : c.kind == 0 ? 1 : (c.offset == 0xb0 ? 1 : 2);
        if (GameAddress<void *>(psshape::kGameVTables[face])[slot] != ctx.image.At(c.rva)) ctx.Fail("%s: the game's slot %u is not %x", c.name, slot, c.rva);
        for (uint32_t seed = 0; seed < 60; ++seed) {
            uint32_t riid = kIds[seed % 4];
            void *origFn = reinterpret_cast<void *>(ctx.Original<Count>(c.rva));
            auto run = [&](World &w, void *fn) -> uint32_t {
                void *self = Sub(w, c.offset);
                if (c.kind == 2) {
                    void *out = nullptr;
                    uint8_t r = reinterpret_cast<Qi>(fn)(self, nullptr, riid, &out);
                    Rec(Addr(out));
                    return r;
                }
                return reinterpret_cast<Count>(fn)(self, nullptr);
            };
            Compare(ctx, c.name, seed, &ThunkSetup, [&](World &w) { return run(w, origFn); },
                    [&](World &w) { return run(w, c.ours); });
        }
    }
}

// vf168_59 (0x1e4b40): the map entry (0x1d7970) and the entry recorder (0x1f6de0, and ours: ChangeRecordSet::Put) are
// stood in for on both sides.
namespace {

// Slot 55 of the primary: (group, uint32_t *out); writes a value (zero in some seeds) and returns gRet.
template <int K> uint8_t __fastcall Slot55Out(void *self, void *, uint32_t a, uint32_t *out)
{
    Note(K, self);
    Rec(a);
    *out = gRet[K + 1] % 3 ? gRet[K + 1] : 0;
    return static_cast<uint8_t>(gRet[K]);
}

// 0x1d7970 (thiscall map, result *, const key *): the map as an offset in the primary, the key; the
// entry is obj[3].
uint32_t *__fastcall EntryStandIn(uint8_t *self, void *, uint32_t *result, const uint32_t *key)
{
    gTrace.push_back(1970);
    Rec(static_cast<uint32_t>(self - gWorld->primary));
    Rec(*key);
    result[0] = static_cast<uint32_t>(Addr(gWorld->obj[3]));
    result[1] = 0;
    return result;
}

// 0x1f6de0 (thiscall entry value, a, b, c; ret 0xc): the value as an offset in obj[3], the arguments.
void __fastcall RecordStandIn(uint8_t *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    gTrace.push_back(0x6de0);
    Rec(static_cast<uint32_t>(self - gWorld->obj[3]));
    Rec(a);
    Rec(b);
    Rec(c);
}

void Vf59Setup(World &w, std::mt19937 &rng)
{
    w.vtP[55] = Fn(&Slot55Out<2555>);
    if (rng() % 4 == 0) Put(w.primary, 0x84, uint32_t(0));
}

} // namespace

PS_TEST(TSPersistSystem2_vf168_59, 0x1e4b40)
{
    using F = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1e4b40);
    CodePatch entry(ctx.image.At(0x1d7970), reinterpret_cast<void *>(&EntryStandIn));
    CodePatch record(ctx.image.At(0x1f6de0), reinterpret_cast<void *>(&RecordStandIn));
    CodePatch recordOurs(shape::MemberCode(&ChangeRecordSet::Put), reinterpret_cast<void *>(&RecordStandIn));
    std::mt19937 args(59);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        uint32_t group = args(), a2 = args(), a3 = args();
        Compare(ctx, "vf168_59", seed, &Vf59Setup,
                [&](World &w) { orig(Sub(w, 0xa8), group, a2, a3); return 0u; },
                [&](World &w) { psshape::Call(PSX(NoteResourceChange), As<uint8_t>(w, 0xa8), group, a2, a3); return 0u; });
    }
}
