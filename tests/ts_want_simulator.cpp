// Differential tests for cTSWantSimulator (src/ts/cTSWantSimulator.cpp): constructor, destructors,
// QueryInterface/Release, the map lookups and the tiny scoring helpers. Calls into the person-like
// argument, the cached singleton C (0x10cc290), the scaling object at +0x20 and the simulator's own
// vtable go to recording fakes; both sides must make the same calls in the same order and leave the
// same map contents.
#include "difftest.h"
#include "game/game.h"
#include "cas_body_part_system.h" // Env: the game allocator and allocator-service cache
#include "ts/cTSWantSimulator.h"
#include "ts_want_simulator_shape.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cITSWantInstance.h"

#include <intrin.h>
#include <malloc.h>
#include <cstring>
#include <new>
#include <random>
#include <tuple>
#include <vector>

namespace {

using W = cTSWantSimulator;
using namespace wantshape;
constexpr unsigned kSeeds = 400;

std::vector<uint32_t> gLog;
void Log(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0, uint32_t e = 0)
{
    gLog.insert(gLog.end(), {a, b, c, d, e});
}

template <typename F, typename M> F Cast(M m)
{
    static_assert(sizeof(M) == sizeof(F), "single-inheritance member pointer");
    F f;
    std::memcpy(&f, &m, sizeof f);
    return f;
}

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu;
    h ^= h >> 15;
    h *= 0xc2b2ae35u;
    return h ^ (h >> 13);
}

// A person-like argument: values depend on its seed and the request; upper halves are garbage so
// sign/zero extension of the 16-bit results is checked.
struct FakePerson {
    void **vt;
    uint32_t seed;
};
int16_t SmallValue(uint32_t h)
{
    if (h % 17 == 0) return int16_t(h >> 8); // anything
    if (h % 5 == 0) return int16_t(int32_t(h >> 8) % 7); // 0..6: around the 0..4 clamp
    return int16_t(int32_t(h >> 8) % 61 - 30);
}
uint32_t __fastcall P_Attribute(FakePerson *p, void *, uint32_t id, uint32_t flag)
{
    Log(10, id, flag);
    const uint32_t h = Mix(p->seed, id * 3 + flag);
    return (h & 0xffff0000u) | uint16_t(SmallValue(h));
}
uint32_t __fastcall P_Motive(FakePerson *p, void *, uint32_t id)
{
    Log(16, id);
    const uint32_t h = Mix(p->seed + 1, id);
    return (h & 0xffff0000u) | uint16_t(SmallValue(h));
}
uint32_t __fastcall P_Key(FakePerson *p, void *)
{
    Log(18);
    return p->seed % 7 == 0 ? Mix(p->seed, 18) : p->seed % 6;
}
uint32_t __fastcall P_Id(FakePerson *p, void *)
{
    Log(57);
    return 0xabcd0000u | (p->seed % 9);
}
uint32_t __fastcall P_Slot13(FakePerson *p, void *)
{
    Log(13);
    return Mix(p->seed, 13);
}
uint32_t __fastcall P_Slot14(FakePerson *p, void *)
{
    Log(14);
    return Mix(p->seed, 14);
}
void *gPersonVt[64];

// The cached singleton C: slot 74 maps an attribute to a level-table key.
uint32_t __fastcall S_Key(void *, void *, int32_t attribute)
{
    Log(74, uint32_t(attribute));
    return uint32_t(attribute) % 6;
}
void *gServiceVt[80];
struct { void **vt; } gService;

// The scaling object at +0x20 (slot 8 maps a float).
float __fastcall R_Scale(void *, void *, float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    Log(8, bits);
    return f * 1.75f + 3.0f;
}
// Reference-counted objects (Release = slot 2) identify themselves in the log.
struct FakeRef {
    void **vt;
    uint32_t id;
};
uint32_t __fastcall R_Release(FakeRef *r, void *)
{
    Log(2, r->id);
    return 0;
}
void *gRefVt[16];

// The simulator's own slots that the functions under test call.
uint32_t __fastcall W_AddRef(W *, void *) { Log(101); return 7; }
uint32_t __fastcall W_Slot7(W *, void *, uint32_t t) { Log(107, t); return 0x77; }
uint32_t __fastcall W_Slot17(W *, void *, uint32_t id, int32_t *a, int32_t *b)
{
    Log(117, id, a != nullptr, b != nullptr);
    return 0x1234565a;
}
uint32_t __fastcall W_Slot19(W *, void *, uint32_t id, int32_t *a)
{
    Log(119, id, a != nullptr);
    return 0x12345633;
}
uint32_t __fastcall W_Slot34(W *, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Log(134, a, b, c, d);
    return Mix(a, b ^ c);
}
uint32_t __fastcall W_Slot42(W *, void *, uint32_t key, int32_t value)
{
    Log(142, key, uint32_t(value));
    return Mix(key, uint32_t(value));
}
uint32_t __fastcall W_Slot50(W *, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Log(150, a, b, c);
    return Mix(a, b + c);
}
uint32_t __fastcall W_Slot40(W *, void *, void *p, int32_t mid, uint32_t z)
{
    Log(140, static_cast<FakePerson *>(p)->seed, uint32_t(mid), z);
    return Mix(uint32_t(mid), z);
}
void *__fastcall W_Slot62(W *w, void *, uint32_t flags)
{
    Log(162, flags);
    return w;
}
FakePerson gPersons[16];
FakeRef gRefs[64];

void Setup(difftest::Context &ctx)
{
    (void)ctx;
    // The log's own growth must not show among the recorded frees (the Destruct test, when it runs first: lane GA).
    gLog.reserve(1 << 16);
    ClearPatches();
    gPatch[0][1] = reinterpret_cast<void *>(&W_AddRef);
    gPatch[0][7] = reinterpret_cast<void *>(&W_Slot7);
    gPatch[0][17] = reinterpret_cast<void *>(&W_Slot17);
    gPatch[0][19] = reinterpret_cast<void *>(&W_Slot19);
    gPatch[0][34] = reinterpret_cast<void *>(&W_Slot34);
    gPatch[0][40] = reinterpret_cast<void *>(&W_Slot40);
    gPatch[0][42] = reinterpret_cast<void *>(&W_Slot42);
    gPatch[0][50] = reinterpret_cast<void *>(&W_Slot50);
    gPatch[0][62] = reinterpret_cast<void *>(&W_Slot62);
    for (auto &s : gPersonVt) s = nullptr;
    gPersonVt[8] = reinterpret_cast<void *>(&R_Scale);
    gPersonVt[10] = reinterpret_cast<void *>(&P_Attribute);
    gPersonVt[13] = reinterpret_cast<void *>(&P_Slot13);
    gPersonVt[14] = reinterpret_cast<void *>(&P_Slot14);
    gPersonVt[16] = reinterpret_cast<void *>(&P_Motive);
    gPersonVt[18] = reinterpret_cast<void *>(&P_Key);
    gPersonVt[57] = reinterpret_cast<void *>(&P_Id);
    gPersonVt[2] = reinterpret_cast<void *>(&R_Release);
    for (auto &s : gServiceVt) s = nullptr;
    gServiceVt[74] = reinterpret_cast<void *>(&S_Key);
    gService.vt = gServiceVt;
    *GameAddress<void *>(0x10cc290) = &gService;
    for (auto &s : gRefVt) s = nullptr;
    gRefVt[2] = reinterpret_cast<void *>(&R_Release);
    gRefVt[8] = reinterpret_cast<void *>(&R_Scale);
    for (uint32_t i = 0; i < 64; ++i) gRefs[i] = {gRefVt, 1000 + i};
}

FakePerson *Person(uint32_t seed)
{
    FakePerson *p = &gPersons[seed % 16];
    p->vt = gPersonVt;
    p->seed = seed;
    return p;
}

std::vector<int32_t> RandomList(std::mt19937 &rng, size_t n)
{
    std::vector<int32_t> v;
    for (size_t i = 0; i < n; ++i) v.push_back(rng() % 2 ? (int32_t(rng() % 61) - 30) * 10 : int32_t(rng() % 601) - 300); // multiples of 10 meet the values exactly
    return v;
}

// Fills an empty list with n counted pointers (some null) in a block from the allocator service.
void FillRefs(WantRefList &l, std::mt19937 &rng, size_t n)
{
    if (l.mFirst || n == 0) return;
    const size_t cap = n + rng() % 3;
    l.mFirst = static_cast<void **>(bpt::ServiceAlloc(cap * 4));
    l.mLast = l.mFirst + n;
    l.mEnd = l.mFirst + cap;
    for (size_t i = 0; i < n; ++i) l.mFirst[i] = rng() % 5 ? &gRefs[1 + rng() % 63] : nullptr;
}

// Builds the route's simulator (our constructor, the route's vtable copies) and fills its maps from the seed.
void Build(W *w, uint32_t seed)
{
    std::memset(static_cast<void *>(w), 0x5c, sizeof(W));
    wantshape::Construct(w);
    std::mt19937 rng(seed);
    for (int i = 0, n = rng() % 6; i < n; ++i) w->mMemoryStores[int16_t(rng() % 9)] = reinterpret_cast<WantMemoryStore *>(uintptr_t(rng()));
    for (int i = 0, n = rng() % 6; i < n; ++i) FillRefs(w->mWants[int16_t(rng() % 9)], rng, rng() % 5);
    for (int i = 0, n = rng() % 6; i < n; ++i) FillRefs(w->mFears[int16_t(rng() % 9)], rng, rng() % 5);
    for (int i = 0, n = rng() % 6; i < n; ++i) {
        WantRef &slot = w->mLifetimeWants[int16_t(rng() % 9)];
        if (!slot.object && rng() % 4) slot.object = &gRefs[1 + rng() % 63];
    }
    for (int i = 0, n = rng() % 6; i < n; ++i) w->mMap48[int16_t(rng() % 9)] = int16_t(rng() % 3 ? rng() % 50 : rng());
    for (int i = 0, n = rng() % 7; i < n; ++i) w->mLevelTables[int32_t(rng() % 6)] = RandomList(rng, 4 + rng() % 5);
    for (int i = 0, n = rng() % 6; i < n; ++i) w->mMap7c[int16_t(rng() % 9)] = {rng(), rng(), rng()};
    for (int i = 0, n = rng() % 6; i < n; ++i) w->mMap84[int32_t(rng() % 19) - 9 - (rng() % 4 ? 0 : 0x10000)] = int32_t(rng());
    w->mRef20.object = rng() % 2 ? &gRefs[0] : nullptr;
    w->mRefCount = int32_t(rng() % 4);
    w->mPaused = uint8_t(rng() % 3 ? rng() % 2 : rng());
}

void Teardown(W *w)
{
    w->mRef20.object = nullptr;
    Destroy(w);
}

std::vector<int64_t> Digest(W *w)
{
    std::vector<int64_t> d;
    auto lists = [&](auto &m) {
        d.push_back(int64_t(m.size()));
        for (auto &e : m) {
            d.push_back(e.first);
            d.push_back(int64_t(e.second.size()));
            for (int32_t x : e.second) d.push_back(x);
        }
    };
    auto refs = [&](auto &m) {
        d.push_back(int64_t(m.size()));
        for (auto &e : m) {
            d.push_back(e.first);
            d.push_back(int64_t(e.second.Size()) << 32 | uint32_t(e.second.mEnd - e.second.mFirst));
            for (void **it = e.second.mFirst; it != e.second.mLast; ++it) d.push_back(*it ? static_cast<FakeRef *>(*it)->id : 0);
        }
    };
    auto values = [&](auto &m) {
        d.push_back(int64_t(m.size()));
        for (auto &e : m) d.push_back(e.first), d.push_back(e.second);
    };
    d.push_back(int64_t(w->mMemoryStores.size()));
    for (auto &e : w->mMemoryStores) d.push_back(e.first), d.push_back(int64_t(reinterpret_cast<uintptr_t>(e.second)));
    refs(w->mWants);
    refs(w->mFears);
    d.push_back(int64_t(w->mLifetimeWants.size()));
    for (auto &e : w->mLifetimeWants) d.push_back(e.first), d.push_back(e.second.object ? static_cast<FakeRef *>(e.second.object)->id : 0);
    values(w->mMap48);
    lists(w->mLevelTables);
    d.push_back(int64_t(w->mMap7c.size()));
    for (auto &e : w->mMap7c) d.push_back(e.first), d.push_back(e.second.first), d.push_back(e.second.second), d.push_back(e.second.third);
    values(w->mMap84);
    d.push_back(w->mRefCount);
    d.push_back(w->mPaused);
    return d;
}

// Runs `call(original, w, rng)` on the three routes' simulators (tests/ts_want_simulator_shape.h) and compares the
// results, logs and maps of routes 1 and 2 with route 0's.
template <typename Call> void Pair(difftest::Context &ctx, const char *name, Call call, unsigned seeds = kSeeds)
{
    bpt::Env env(ctx);
    Setup(ctx);
    alignas(8) static unsigned char buf[kRoutes][sizeof(W)];
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        ++ctx.cases;
        uint64_t result[kRoutes];
        std::vector<uint32_t> logs[kRoutes];
        std::vector<int64_t> digests[kRoutes];
        for (int route = 0; route < kRoutes; ++route) {
            gRoute = route;
            gTrapHits = 0;
            W *w = reinterpret_cast<W *>(buf[route]);
            Build(w, seed);
            std::mt19937 rng(seed * 7 + 1);
            gLog.clear();
            result[route] = call(route == 0, w, rng);
            logs[route] = gLog;
            digests[route] = Digest(w);
            if (gTrapHits) ctx.Fail("%s seed %u route %d: the adapter dispatched through the vtable", name, seed, route);
            Teardown(w);
        }
        for (int route = 1; route < kRoutes; ++route) {
            const uint64_t ra = result[0], rb = result[route];
            if (ra != rb) ctx.Fail("%s seed %u route %d: result %08x%08x vs %08x%08x", name, seed, route, uint32_t(ra >> 32), uint32_t(ra), uint32_t(rb >> 32), uint32_t(rb));
            if (logs[0] != logs[route]) ctx.Fail("%s seed %u route %d: calls differ (%zu vs %zu words)", name, seed, route, logs[0].size(), logs[route].size());
            if (digests[0] != digests[route]) ctx.Fail("%s seed %u route %d: state differs", name, seed, route);
        }
    }
    gRoute = 0;
}

// Calls the function the route's way: the original RVA, our vtable's slot or the adapter (stack arguments as the game
// passes them on every route).
#define CALL(rva, member, ...) wantshape::Call<Fn>(ctx, w, rva, 0, WANT_ADAPTER(member), __VA_ARGS__)
#define CALL_NA(rva, member, ...) wantshape::Call<Fn>(ctx, w, rva, 0, WANT_NA(member), __VA_ARGS__) // adapter dropped (lane GT)

template <typename Fn> Fn Orig(difftest::Context &ctx, uint32_t rva) { return ctx.Original<Fn>(rva); }

uint32_t KeyArg(std::mt19937 &rng) { return (rng() % 3 ? 0 : rng() & 0xffff0000u) | (rng() % 11); }

} // namespace

DIFF_TEST(WantSimulator_QueryInterface, 0x4927b0)
{
    using Fn = uint32_t(__thiscall *)(W *, uint32_t, void **);
    Pair(ctx, "QueryInterface", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        const uint32_t ids[] = {1, 0xd7ee268, 0x8db5bf48, 0x8db5bf49, 0, rng()};
        const uint32_t id = ids[rng() % 6];
        void *out = nullptr;
        const uint32_t r = CALL_NA(0x4927b0, QueryInterface, id, &out);
        const uint64_t ok = r & 0xff;
        return ok << 32 | (out ? uint32_t(static_cast<char *>(out) - reinterpret_cast<char *>(w)) : 0xffffffffu);
    });
}

DIFF_TEST(WantSimulator_Release, 0x494800)
{
    using Fn = uint32_t(__thiscall *)(W *);
    Pair(ctx, "Release", [&](bool o, W *w, std::mt19937 &) -> uint64_t {
        return CALL(0x494800, Release);
    });
}

DIFF_TEST(WantSimulator_LevelForObject, 0x48d280)
{
    using Fn = uint32_t(__thiscall *)(W *, void *);
    Pair(ctx, "LevelForObject", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        return CALL_NA(0x48d280, LevelForObject, Person(rng()));
    });
}

DIFF_TEST(WantSimulator_LevelForKey, 0x48d330)
{
    using Fn = uint32_t(__thiscall *)(W *, void *);
    Pair(ctx, "LevelForKey", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        return CALL_NA(0x48d330, LevelForKey, Person(rng()));
    });
}

DIFF_TEST(WantSimulator_ScoreMotiveA2, 0x48d3c0)
{
    using Fn = uint32_t(__thiscall *)(W *, void *, uint32_t);
    Pair(ctx, "ScoreMotiveA2", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        void *p = Person(rng());
        const uint32_t flag = rng() % 2 ? 0 : (rng() % 2 ? 1 : rng() & 0xffffff00u);
        return CALL_NA(0x48d3c0, ScoreMotiveA2, p, flag);
    });
}

DIFF_TEST(WantSimulator_ScoreAttributeA2, 0x48d430)
{
    using Fn = uint32_t(__thiscall *)(W *, void *, uint32_t);
    Pair(ctx, "ScoreAttributeA2", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        void *p = Person(rng());
        const uint32_t flag = rng() % 2 ? 0 : (rng() % 2 ? 1 : rng() & 0xffffff00u);
        return CALL_NA(0x48d430, ScoreAttributeA2, p, flag);
    });
}

DIFF_TEST(WantSimulator_ScaledAttributeB2, 0x48d7c0)
{
    using Fn = uint32_t(__thiscall *)(W *, void *, uint32_t);
    Pair(ctx, "ScaledAttributeB2", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        void *p = Person(rng());
        const uint32_t flag = rng() % 3 ? 1 : 0;
        return CALL_NA(0x48d7c0, ScaledAttributeB2, p, flag);
    });
}

DIFF_TEST(WantSimulator_ScaledMotiveB2, 0x48d740)
{
    using Fn = uint32_t(__thiscall *)(W *, void *, uint32_t);
    Pair(ctx, "ScaledMotiveB2", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        void *p = Person(rng());
        const uint32_t flag = rng() % 3 ? 1 : 0;
        return CALL_NA(0x48d740, ScaledMotiveB2, p, flag);
    });
}

DIFF_TEST(WantSimulator_ClampedAttributeB0, 0x48d710)
{
    using Fn = uint32_t(__thiscall *)(W *, void *);
    Pair(ctx, "ClampedAttributeB0", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        return CALL_NA(0x48d710, ClampedAttributeB0, Person(rng() % 40));
    });
}

DIFF_TEST(WantSimulator_PositiveAttributeA1, 0x48de30)
{
    using Fn = uint32_t(__thiscall *)(W *, void *);
    Pair(ctx, "PositiveAttributeA1", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        return CALL(0x48de30, PositiveAttributeA1, Person(rng()));
    });
}

DIFF_TEST(WantSimulator_CountsForPerson, 0x48d9d0)
{
    using Fn = uint32_t(__thiscall *)(W *, void *, int32_t *, int32_t *);
    Pair(ctx, "CountsForPerson", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        void *p = rng() % 4 ? Person(rng()) : nullptr;
        int32_t x = 0, y = 0;
        int32_t *a = rng() % 2 ? &x : nullptr, *b = rng() % 2 ? &y : nullptr;
        return CALL_NA(0x48d9d0, CountsForPerson, p, a, b) & 0xff; // al only
    });
}

DIFF_TEST(WantSimulator_Counts, 0x48d940)
{
    using Fn = uint32_t(__thiscall *)(W *, uint32_t, int32_t *, int32_t *);
    Pair(ctx, "Counts", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        const uint32_t type = KeyArg(rng);
        int32_t x = -5, y = -6;
        const bool ha = rng() % 4 != 0, hb = rng() % 4 != 0;
        const uint32_t r = CALL_NA(0x48d940, Counts, type, ha ? &x : nullptr, hb ? &y : nullptr);
        return uint64_t(r & 0xff) << 40 | uint64_t(uint32_t(x) & 0xfffff) << 20 | (uint32_t(y) & 0xfffff);
    });
}

DIFF_TEST(WantSimulator_ValueForPerson, 0x48da40)
{
    using Fn = uint32_t(__thiscall *)(W *, void *, int32_t *);
    Pair(ctx, "ValueForPerson", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        void *p = rng() % 4 ? Person(rng()) : nullptr;
        int32_t x = 0;
        return CALL_NA(0x48da40, ValueForPerson, p, rng() % 2 ? &x : nullptr) & 0xff;
    });
}

DIFF_TEST(WantSimulator_Value48, 0x48da10)
{
    using Fn = uint32_t(__thiscall *)(W *, uint32_t, int32_t *);
    Pair(ctx, "Value48", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        const uint32_t key = KeyArg(rng);
        int32_t x = 0x5555;
        const bool has = rng() % 5 != 0;
        const uint32_t r = CALL_NA(0x48da10, Value48, key, has ? &x : nullptr);
        return uint64_t(r & 0xff) << 32 | uint32_t(x);
    });
}

DIFF_TEST(WantSimulator_Value40, 0x48d920)
{
    using Fn = void *(__thiscall *)(W *, uint32_t);
    Pair(ctx, "Value40", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        void *r = CALL_NA(0x48d920, Value40, KeyArg(rng));
        return r ? static_cast<FakeRef *>(r)->id : 0;
    });
}

DIFF_TEST(WantSimulator_List30, 0x48dcb0)
{
    using Fn = WantRefList *(__thiscall *)(W *, uint32_t);
    Pair(ctx, "List30", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        const uint32_t key = KeyArg(rng);
        WantRefList *r = CALL_NA(0x48dcb0, List30, key);
        return r == &w->mWants.at(int16_t(key)) ? 1 : 0;
    });
}

DIFF_TEST(WantSimulator_List38, 0x48dca0)
{
    using Fn = WantRefList *(__thiscall *)(W *, uint32_t);
    Pair(ctx, "List38", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        const uint32_t key = KeyArg(rng);
        WantRefList *r = CALL_NA(0x48dca0, List38, key);
        return r == &w->mFears.at(int16_t(key)) ? 1 : 0;
    });
}

namespace {
template <uint32_t Rva> uint64_t KeyCall(difftest::Context &ctx, W *w, std::mt19937 &rng, void *adapter)
{
    using Fn = WantResourceKey *(__thiscall *)(W *, WantResourceKey *, uint32_t);
    WantResourceKey key;
    std::memset(&key, 0xcc, sizeof key);
    WantResourceKey *r = [&] { const uint32_t k = KeyArg(rng); return wantshape::Call<Fn>(ctx, w, Rva, 0, adapter, &key, k); }();
    return uint64_t(r == &key) << 63 ^ Mix(Mix(key.type, key.group), Mix(key.instance, key.resource)) ^ uint64_t(key.instance) << 31;
}
} // namespace

DIFF_TEST(WantSimulator_KeyFirst, 0x48dbe0)
{
    Pair(ctx, "KeyFirst", [&](bool, W *w, std::mt19937 &rng) -> uint64_t { return KeyCall<0x48dbe0>(ctx, w, rng, WANT_NA(KeyFirst)); });
}

DIFF_TEST(WantSimulator_KeySecond, 0x48dc40)
{
    Pair(ctx, "KeySecond", [&](bool, W *w, std::mt19937 &rng) -> uint64_t { return KeyCall<0x48dc40>(ctx, w, rng, WANT_NA(KeySecond)); });
}

DIFF_TEST(WantSimulator_Find84, 0x48da70)
{
    using Fn = uint32_t(__thiscall *)(W *, uint32_t, int32_t *);
    Pair(ctx, "Find84", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        uint32_t key = KeyArg(rng);
        if (rng() % 4 == 0) key |= 0xfff0; // negative shorts
        int32_t x = 0x4444;
        const uint32_t r = CALL_NA(0x48da70, Find84, key, &x);
        return uint64_t(r & 0xff) << 32 | uint32_t(x);
    });
}

DIFF_TEST(WantSimulator_ForwardToSlot34, 0x4918f0)
{
    using Fn = uint32_t(__thiscall *)(W *, uint32_t, void *);
    Pair(ctx, "ForwardToSlot34", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        const uint32_t a = rng();
        return CALL_NA(0x4918f0, ForwardToSlot34, a, Person(rng()));
    });
}

DIFF_TEST(WantSimulator_ForwardPersonToSlot34, 0x491920)
{
    using Fn = uint32_t(__thiscall *)(W *, void *, void *);
    Pair(ctx, "ForwardPersonToSlot34", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        void *p = Person(rng());
        void *s = Person(rng() + 3);
        return CALL_NA(0x491920, ForwardPersonToSlot34, p, s);
    });
}

DIFF_TEST(WantSimulator_ForwardPersonToSlot50, 0x496290)
{
    using Fn = uint32_t(__thiscall *)(W *, void *, uint32_t, uint32_t);
    Pair(ctx, "ForwardPersonToSlot50", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        void *p = rng() % 4 ? Person(rng()) : nullptr;
        const uint32_t a = rng(), b = rng();
        const uint32_t r = CALL_NA(0x496290, ForwardPersonToSlot50, p, a, b);
        return p ? r : 0; // with no person the original returns eax untouched (undefined)
    });
}

DIFF_TEST(WantSimulator_TogglePaused, 0x497460)
{
    using Fn = void(__thiscall *)(W *);
    Pair(ctx, "TogglePaused", [&](bool o, W *w, std::mt19937 &) -> uint64_t {
        CALL(0x497460, TogglePaused);
        return 0;
    });
}

DIFF_TEST(WantSimulator_IsPaused, 0x496920)
{
    using Fn = uint32_t(__thiscall *)(W *);
    Pair(ctx, "IsPaused", [&](bool o, W *w, std::mt19937 &) -> uint64_t {
        return CALL(0x496920, IsPaused) & 0xff;
    });
}

// Constructor: identical random bytes before; the original against the hook's adapter (our constructor). Afterwards
// the vtable words must be the game's on the original side and ours on ours, every other byte except the container
// heads is compared, each head must be an empty tree head, and the message-id sets must hold the same ids.
DIFF_TEST(WantSimulator_Construct, 0x4855d0)
{
    using Fn = W *(__thiscall *)(W *);
    static const uint32_t kHeads[] = {0x14, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x74, 0x7c, 0x84, 0x98, 0xa0, 0xa8, 0xfc};
    alignas(8) static unsigned char bufA[sizeof(W)], bufB[sizeof(W)];
    for (uint32_t seed = 0; seed < 50; ++seed) {
        ++ctx.cases;
        std::mt19937 rng(seed);
        for (size_t i = 0; i < sizeof(W); ++i) bufA[i] = bufB[i] = uint8_t(rng());
        W *a = Orig<Fn>(ctx, 0x4855d0)(reinterpret_cast<W *>(bufA));
        W *b = cTSWantSimulatorAdapters::Construct(bufB, nullptr);
        if (a != reinterpret_cast<W *>(bufA) || b != reinterpret_cast<W *>(bufB)) ctx.Fail("Construct seed %u: return", seed);
        unsigned char ca[sizeof(W)], cb[sizeof(W)];
        std::memcpy(ca, bufA, sizeof ca);
        std::memcpy(cb, bufB, sizeof cb);
        for (unsigned f = 0; f < kFaces; ++f) {
            if (shape::VTableAt(bufA, kFaceOffset[f]) != GameVt(f) || shape::VTableAt(bufB, kFaceOffset[f]) != OurVt(f))
                ctx.Fail("Construct seed %u: face %u's vtable", seed, f);
            std::memset(ca + kFaceOffset[f], 0, 4);
            std::memset(cb + kFaceOffset[f], 0, 4);
        }
        for (uint32_t off : kHeads) {
            for (unsigned char *buf : {bufA, bufB}) {
                unsigned char *head = *reinterpret_cast<unsigned char **>(buf + off);
                void **links = reinterpret_cast<void **>(head);
                if (off != 0x14 && (links[0] != head || links[1] != head || links[2] != head || head[0xc] != 1 || head[0xd] != 1))
                    ctx.Fail("Construct seed %u: head at %x not empty", seed, off);
            }
            std::memset(ca + off, 0, 4);
            std::memset(cb + off, 0, 4);
        }
        for (size_t i = 0; i < sizeof(W); ++i)
            if (ca[i] != cb[i]) {
                ctx.Fail("Construct seed %u: byte %zx differs (%02x vs %02x)", seed, i, ca[i], cb[i]);
                break;
            }
        if (a->mMessageIds != b->mMessageIds) ctx.Fail("Construct seed %u: message ids differ", seed);
        Destroy(a);
        Destroy(b);
    }
}

namespace {
// Fills the members the destructor releases or frees: reference fields, the reference maps, vectors.
void FillForDestruct(W *w, uint32_t seed)
{
    Build(w, seed);
    std::mt19937 rng(seed + 99);
    uint32_t next = 1;
    auto ref = [&]() -> void * { return rng() % 4 ? &gRefs[next++ % 64] : nullptr; };
    w->mRef1c.object = ref();
    w->mRef20.object = ref();
    w->mDefinitions.object = ref();
    w->mRef90.object = ref();
    w->mRef94.object = ref();
    w->mRefC0.object = ref();
    w->mRefC4.object = ref();
    for (int i = 0, n = rng() % 5; i < n; ++i)
        w->mRefMap98.emplace(std::piecewise_construct, std::forward_as_tuple(&gRefs[20 + i]), std::forward_as_tuple(&gRefs[40 + i]));
    for (int i = 0, n = rng() % 5; i < n; ++i)
        w->mRefMapA8.emplace(std::piecewise_construct, std::forward_as_tuple(&gRefs[30 + i]), std::forward_as_tuple(rng()));
    for (int i = 0, n = rng() % 4; i < n; ++i) FillRefs(w->mMap50[int16_t(i)], rng, rng() % 4);
    for (int i = 0, n = rng() % 4; i < n; ++i) FillRefs(w->mMapA0[int16_t(i)], rng, rng() % 4);
    for (int i = 0, n = rng() % 4; i < n; ++i) w->mPendingSims[i] = int32_t(rng());
    FillRefs(w->mVec68, rng, rng() % 4);
    FillRefs(w->mVecD4, rng, rng() % 4);
    FillRefs(w->mVecE0, rng, rng() % 4);
    FillRefs(w->mVecEC, rng, rng() % 4);
    for (int i = 0, n = rng() % 3; i < n; ++i) w->mMessageIds.insert(rng());
}
constexpr size_t kMaxSizes = 512;
} // namespace

DIFF_TEST(WantSimulator_Destruct, 0x485ba0)
{
    using Fn = void(__thiscall *)(W *);
    bpt::Env env(ctx);
    Setup(ctx);
    alignas(8) static unsigned char bufA[sizeof(W)], bufB[sizeof(W)];
    static size_t sizes[2][kMaxSizes];
    for (uint32_t seed = 0; seed < 200; ++seed) {
        ++ctx.cases;
        std::vector<uint32_t> logs[2];
        size_t counts[2];
        for (int side = 0; side < 2; ++side) {
            W *w = reinterpret_cast<W *>(side ? bufB : bufA);
            FillForDestruct(w, seed);
            gLog.clear();
            difftest::FreeRecorder rec;
            if (side == 0) Orig<Fn>(ctx, 0x485ba0)(w);
            else cTSWantSimulatorAdapters::Destruct(w, nullptr);
            rec.Stop();
            counts[side] = rec.SortedSizes(sizes[side], kMaxSizes);
            logs[side] = gLog;
            for (unsigned f = 0; f < kFaces; ++f)
                if (shape::VTableAt(w, kFaceOffset[f]) != (side == 0 ? GameVt(f) : OurVt(f)))
                    ctx.Fail("Destruct seed %u side %d: face %u's vtable", seed, side, f);
        }
        if (logs[0] != logs[1]) ctx.Fail("Destruct seed %u: releases differ (%zu vs %zu)", seed, logs[0].size(), logs[1].size());
        if (counts[0] != counts[1] || std::memcmp(sizes[0], sizes[1], counts[0] * sizeof(size_t)) != 0)
            ctx.Fail("Destruct seed %u: frees differ (%zu vs %zu)", seed, counts[0], counts[1]);
    }
}

DIFF_TEST(WantSimulator_DeletingDestructor, 0x4862e0)
{
    using Fn = W *(__thiscall *)(W *, uint32_t);
    bpt::Env env(ctx);
    Setup(ctx);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        ++ctx.cases;
        const uint32_t flags = seed % 2 ? 1 : (seed % 4 ? 0 : 0xfffffffe);
        // Route 0 the original, route 1 our vtable's slot 62 (the compiler's scalar deleting destructor), route 2 the same (its
        // adapter is gone, lane GT: only the game's vtable reaches 0x4862e0).
        std::vector<uint32_t> logs[kRoutes];
        size_t counts[kRoutes];
        bool freed[kRoutes];
        for (int side = 0; side < kRoutes; ++side) {
            gRoute = side;
            W *w = static_cast<W *>(::operator new(sizeof(W)));
            FillForDestruct(w, seed);
            gLog.clear();
            difftest::FreeRecorder rec;
            W *r = side == 0 ? Orig<Fn>(ctx, 0x4862e0)(w, flags) : reinterpret_cast<Fn>(OurVt(0)[62])(w, flags);
            rec.Stop();
            counts[side] = rec.Count();
            freed[side] = rec.Freed(w);
            logs[side] = gLog;
            if (r != w) ctx.Fail("DeletingDestructor seed %u side %d: return", seed, side);
            if (!(flags & 1)) {
                for (unsigned f = 0; f < kFaces; ++f)
                    if (shape::VTableAt(w, kFaceOffset[f]) != (side == 0 ? GameVt(f) : OurVt(f)))
                        ctx.Fail("DeletingDestructor seed %u side %d: face %u's vtable", seed, side, f);
                ::operator delete(w);
            }
        }
        gRoute = 0;
        for (int side = 1; side < kRoutes; ++side) {
            if (logs[0] != logs[side]) ctx.Fail("DeletingDestructor seed %u side %d: releases differ", seed, side);
            if (counts[0] != counts[side] || freed[0] != freed[side] || freed[0] != bool(flags & 1))
                ctx.Fail("DeletingDestructor seed %u: frees %zu/%d vs %zu/%d", seed, counts[0], freed[0], counts[side], freed[side]);
        }
    }
}

DIFF_TEST(WantSimulator_MidpointScoreForPerson, 0x495fa0)
{
    using Fn = uint32_t(__thiscall *)(W *, void *, int32_t);
    Pair(ctx, "MidpointScoreForPerson", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        void *p = rng() % 5 ? Person(rng()) : nullptr;
        const int32_t level = rng() % 4 ? int32_t(rng() % 9) - 2 : int32_t(rng());
        const uint32_t r = CALL_NA(0x495fa0, MidpointScoreForPerson, p, level);
        return p ? r : 0; // with no person the original returns eax untouched (undefined)
    });
}

DIFF_TEST(WantSimulator_ScoreForValue, 0x491aa0)
{
    using Fn = int32_t(__thiscall *)(W *, int32_t, int32_t);
    Pair(ctx, "ScoreForValue", [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        const int32_t key = rng() % 8 ? int32_t(rng() % 6) : int32_t(rng());
        int32_t value;
        switch (rng() % 4) {
        case 0: value = (int32_t(rng() % 61) - 30) * 10; break;
        case 1: value = int32_t(rng() % 801) - 400; break;
        case 2: value = int32_t(rng() % 0xe0000) - 0x70000; break;
        default: value = int32_t(rng()); break;
        }
        if (rng() % 4 == 0) {
            // The top band empty and the value on it: 0 / 0 gives NaN, where maxss/minss operand order matters.
            std::vector<int32_t> &t = w->mLevelTables[key];
            t.resize(7);
            t[5] = t[6] = value = int32_t(rng() % 401) - 200;
            for (int i = 0; i < 5; ++i) t[i] = value - 1000 + i;
        }
        return uint32_t(CALL_NA(0x491aa0, ScoreForValue, key, value));
    });
}

// ======================================================================================================
// Persistence (lane DB): WantMemoryStore, GetMemoryStore, AddDefaultMemory, IsWantAllowed, WriteSimWants,
// ReadSimWants, ReadSimRecord, StoreSimWants. The original (by RVA) and ours run on identical worlds built from one
// seed; results, the ordered log of every fake call and stream operation, the lists, stores and instances reached
// from the simulator, the instance pool's free list and the fakes' counts must agree.
//
// World: the globals object (0x10cc288) answers slot 4 with the simulator under test and slot 8 with the
// neighbourhood table; the cached slot-23 object (0x10cc2a8) leads to the current hood key; the persist system
// (0x10faf6c) hands out a segment, a record and a stream (all fakes recording). Want instances read from streams are
// the game's own (pool 0x54e570, constructor 0x54e490, Read 0x550420): the pool is one 4 KB page planted at
// [0x1015d5c] with a count that never reaches zero. Definitions come from fakes behind simulator slot 23.
// Short streams: an exhausted fake stream raises its error flag and writes 0 into an out value on the stack (the
// original reads uninitialised stack for the version and the counts, so something has to be there; preset locals
// such as the default slot counts, 0x492ac5 / 0x492c43, are therefore not distinguished from 0 on that path); an out
// value off the stack (an object's field) is left untouched, as a real stream leaves it.
// ======================================================================================================
namespace {
namespace pw {

using Tok = std::vector<uint32_t>;

// ---- definitions ----
struct FakeDef {
    void **vt;
    uint32_t guid, flags, rule, a, b;
};
constexpr uint32_t kKnownGuidBase = 0x5000;
constexpr int kKnownGuids = 7;
FakeDef gDefs[8];
void *gDefVt[32];
uint32_t __fastcall D_A(FakeDef *d, void *) { Log(206, d->guid); return d->a; }
uint32_t __fastcall D_B(FakeDef *d, void *) { Log(207, d->guid); return d->b; }
uint32_t __fastcall D_Rule(FakeDef *d, void *) { Log(217, d->guid); return d->rule; }
uint32_t __fastcall D_Flags(FakeDef *d, void *) { Log(219, d->guid); return d->flags; }
FakeDef *FindDef(uint32_t guid)
{
    for (FakeDef &d : gDefs)
        if (d.guid == guid) return &d;
    return nullptr;
}
uint32_t KnownGuid(uint32_t i) { return i < kKnownGuids ? kKnownGuidBase + i : kDefaultMemoryGuid; }

// ---- streams ----
struct FakeStream {
    void **vt;
    Tok tokens;
    size_t pos;
    bool exhausted;
    size_t errorAt;
    int refs;
    uint32_t calls;
};
FakeStream gStream;
void *gStreamVt[24];
uint32_t Next(FakeStream *s)
{
    if (s->pos >= s->tokens.size()) {
        s->exhausted = true;
        return 0; // filler: see the header comment
    }
    return s->tokens[s->pos++];
}
// An exhausted read stores its filler only into a stack local (DT, after DL): a field or element off the stack keeps
// what it held, as a real stream leaves it. So a read straight into a field (WantMemoryStore +4, 0x5513e0) is told
// apart from a read into a local that is then copied.
template <typename T> void Put(FakeStream *s, T *out, uint32_t v)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(out);
    if (!s->exhausted || (at >= __readfsdword(8) && at < __readfsdword(4))) *out = T(v);
}
uint8_t __fastcall IS_GetU8(FakeStream *s, void *, uint8_t *out) { const uint32_t v = Next(s); Log(405, v); Put(s, out, v); return 1; }
uint8_t __fastcall IS_GetS16(FakeStream *s, void *, int16_t *out) { const uint32_t v = Next(s); Log(406, v); Put(s, out, v); return 1; }
uint8_t __fastcall IS_GetS32(FakeStream *s, void *, int32_t *out) { const uint32_t v = Next(s); Log(408, v); Put(s, out, v); return 1; }
uint8_t __fastcall IS_GetU32(FakeStream *s, void *, uint32_t *out) { const uint32_t v = Next(s); Log(409, v); Put(s, out, v); return 1; }
uint8_t __fastcall IS_GetError(FakeStream *s, void *)
{
    Log(418, uint32_t(s->pos), s->exhausted);
    return s->exhausted || s->pos >= s->errorAt ? 1 : 0;
}
uint8_t __fastcall OS_SetU8(FakeStream *s, void *, uint8_t v) { ++s->calls; Log(505, v); return 1; }
uint8_t __fastcall OS_SetS16(FakeStream *s, void *, int16_t v) { ++s->calls; Log(506, uint32_t(int32_t(v))); return 1; }
uint8_t __fastcall OS_SetS32(FakeStream *s, void *, int32_t v) { ++s->calls; Log(508, uint32_t(v)); return 1; }
uint8_t __fastcall OS_SetU32(FakeStream *s, void *, uint32_t v) { ++s->calls; Log(509, v); return 1; }
uint8_t __fastcall OS_GetError(FakeStream *s, void *)
{
    Log(518, s->calls);
    return s->calls >= s->errorAt ? 1 : 0;
}
uint32_t __fastcall ST_AddRef(FakeStream *s, void *) { return uint32_t(++s->refs); }
uint32_t __fastcall ST_Release(FakeStream *s, void *) { Log(402, uint32_t(s->refs - 1)); return uint32_t(--s->refs); }

// ---- persist system, segment, record ----
struct FakeCounted {
    void **vt;
    int refs;
    uint32_t tag;
};
FakeCounted gSegment, gRecord;
void *gSegmentVt[40], *gRecordVt[8], *gPersistVt[40];
struct { void **vt; } gPersist;
struct PersistCfg {
    bool findOk = true, openOk = true, qiOk = true;
} gCfg;
void LogKey(uint32_t code, const uint32_t *key) { Log(code, key[0], key[1], key[2], key[3]); }
uint8_t __fastcall P_FindSegment(void *, void *, const uint32_t *key, uint32_t iid, void **out)
{
    LogKey(334, key);
    Log(335, iid);
    if (!gCfg.findOk) return 0;
    ++gSegment.refs;
    *out = &gSegment;
    return 1;
}
uint8_t __fastcall SG_OpenRecord(FakeCounted *, void *, const uint32_t *key, void **out, uint32_t mode)
{
    LogKey(322, key);
    Log(323, mode);
    if (!gCfg.openOk) return 0;
    ++gRecord.refs;
    *out = &gRecord;
    return 1;
}
uint8_t __fastcall SG_CloseRecord(FakeCounted *, void *, void *record) { Log(324, record == &gRecord); return 1; }
void __fastcall SG_DeleteRecord(FakeCounted *, void *, const uint32_t *key) { LogKey(326, key); }
uint32_t __fastcall CN_Release(FakeCounted *c, void *) { Log(302, c->tag, uint32_t(c->refs - 1)); return uint32_t(--c->refs); }
uint32_t __fastcall RC_QueryInterface(FakeCounted *, void *, uint32_t iid, void **out)
{
    Log(300, iid);
    if (!gCfg.qiOk) return 0;
    ++gStream.refs;
    *out = &gStream;
    return 1;
}

// ---- globals, current hood, key chain ----
struct Obj {
    void **vt;
};
Obj gGlobals, gTable, gInfo, gSource, gHolder, gHood, gKeyHolder;
void *gGlobalsVt[40], *gTableVt[16], *gInfoVt[32], *gSourceVt[16], *gHolderVt[16], *gHoodVt[16], *gKeyHolderVt[16];
W *gSim;
uint32_t gTick = 0;
uint32_t gGroup = 0x11223344, gKey = 0x55667788;
uint32_t __fastcall G_WantService(Obj *, void *) { Log(204); return reinterpret_cast<uint32_t>(gSim); }
uint32_t __fastcall G_Table(Obj *, void *) { Log(208); return reinterpret_cast<uint32_t>(&gTable); }
uint32_t __fastcall T_Find(Obj *, void *, uint32_t key) { Log(310, key); return reinterpret_cast<uint32_t>(&gInfo); }
uint32_t __fastcall I_Source(Obj *, void *) { Log(324); return reinterpret_cast<uint32_t>(&gSource); }
uint32_t __fastcall S_Group(Obj *, void *) { Log(306); return gGroup; }
uint32_t __fastcall H_Hood(Obj *, void *) { Log(406); return reinterpret_cast<uint32_t>(&gHood); }
uint32_t __fastcall HD_KeyHolder(Obj *, void *) { Log(408); return reinterpret_cast<uint32_t>(&gKeyHolder); }
uint32_t __fastcall K_Key(Obj *, void *) { Log(405); return gKey; }

// ---- the simulator's own slots (patched over each route's vtable copies) ----
uint32_t __fastcall S_Definition(W *, void *, uint32_t guid)
{
    Log(223, guid);
    return reinterpret_cast<uint32_t>(FindDef(guid));
}
uint32_t __fastcall S_Tick(W *, void *) { Log(226); return gTick; }
uint32_t __fastcall S_AddRef(W *, void *) { Log(101); return 7; }
// Slot 30 (IsWantAllowed's level lookup): logged, then the route's own slot, so a call that skips the vtable shows.
uint8_t __fastcall S_Find84(W *w, void *, uint32_t key, int32_t *out)
{
    Log(230, key & 0xffff);
    return reinterpret_cast<uint8_t(__fastcall *)(W *, void *, uint32_t, int32_t *)>(OwnSlot(0, 30))(w, nullptr, key, out);
}

// ---- fake instances (writes, memories): a primary vtable and a serializable face at +4 ----
struct FakeInst {
    void **vt;
    void **vtFace;
    int refs;
    uint32_t tag, guid, id;
    uint8_t hasId, flag10;
    uint16_t simId;
    FakeDef *def;
    WantInstanceParam param;
};
FakeInst gInst[24];
void *gInstVt[48], *gFaceVt[8];
FakeInst *FromFace(void *face) { return reinterpret_cast<FakeInst *>(static_cast<char *>(face) - 4); }
uint32_t __fastcall FI_QI(FakeInst *f, void *, uint32_t iid, void **out)
{
    Log(500, f->tag, iid);
    if (iid != kSerializableIid) return 0;
    ++f->refs;
    *out = &f->vtFace;
    return 1;
}
uint32_t __fastcall FI_AddRef(FakeInst *f, void *) { Log(501, f->tag, uint32_t(f->refs + 1)); return uint32_t(++f->refs); }
uint32_t __fastcall FI_Release(FakeInst *f, void *) { Log(502, f->tag, uint32_t(f->refs - 1)); return uint32_t(--f->refs); }
uint32_t __fastcall FI_Def(FakeInst *f, void *) { Log(512, f->tag); return reinterpret_cast<uint32_t>(f->def); }
uint32_t __fastcall FI_Guid(FakeInst *f, void *) { Log(513, f->tag); return f->guid; }
uint32_t __fastcall FI_Param(FakeInst *f, void *) { Log(514, f->tag); return reinterpret_cast<uint32_t>(&f->param); }
uint8_t __fastcall FI_HasId(FakeInst *f, void *) { Log(523, f->tag); return f->hasId; }
uint32_t __fastcall FI_GetId(FakeInst *f, void *) { Log(524, f->tag); return f->id; }
uint8_t __fastcall FI_Flag10(FakeInst *f, void *) { Log(528, f->tag); return f->flag10; }
void __fastcall FI_SetId(FakeInst *f, void *, uint32_t id) { Log(532, f->tag, id); f->id = id; f->hasId = 1; }
uint32_t __fastcall FF_AddRef(void *face, void *) { return FI_AddRef(FromFace(face), nullptr); }
uint32_t __fastcall FF_Release(void *face, void *) { return FI_Release(FromFace(face), nullptr); }
uint8_t __fastcall FF_Write(void *face, void *, FakeStream *stream)
{
    FakeInst *f = FromFace(face);
    Log(503, f->tag);
    OS_SetU32(stream, nullptr, 0xe0000000u + f->tag);
    return 1;
}

// ---- the game's instance pool: one aligned page ----
uint8_t *gPage;
constexpr uint32_t kBlock = 0x3c, kBlocks = 68;
void ResetPool(difftest::Context &ctx)
{
    if (!gPage) gPage = static_cast<uint8_t *>(_aligned_malloc(4096, 4096));
    std::memset(gPage, 0xcd, 4096);
    for (uint32_t i = 0; i < kBlocks; ++i) {
        uint8_t *block = gPage + 0x10 + i * kBlock;
        *reinterpret_cast<uint8_t **>(block) = i + 1 < kBlocks ? block + kBlock : nullptr;
    }
    *reinterpret_cast<uint8_t **>(gPage + 8) = gPage + 0x10;
    *reinterpret_cast<uint32_t *>(gPage + 0xc) = 100000;
    *ctx.image.At<void *>(0x1015d5c) = gPage;
}
void PoolState(std::vector<int64_t> &d)
{
    d.push_back(*reinterpret_cast<uint32_t *>(gPage + 0xc));
    for (uint8_t *b = *reinterpret_cast<uint8_t **>(gPage + 8); b; b = *reinterpret_cast<uint8_t **>(b)) d.push_back((b - gPage - 0x10) / kBlock);
    d.push_back(-1);
}
bool InPool(const void *p) { return p >= gPage && p < gPage + 4096; }

// ---- sim lifecycle (lane DR): neighbours, live persons, the hood, the simulator's slots 6 / 7 / 35 ----
// Sims 0..9: each may have a neighbour (cITSNeighborManager::GetNeighbor) and a live person (GscObjectTable
// FindObjectByHandle of the neighbour's GetPersonDataId); the neighbour's attribute 0xba is configurable.
constexpr int kSims = 10;
struct FakeNbr {
    void **vt;
    int refs;
    uint32_t sim;
    uint16_t block[4];
};
struct NbrCfg {
    bool has, live;
    uint16_t attrBA;
};
FakeNbr gNbr[kSims], gPerson[kSims];
NbrCfg gNbrCfg[kSims];
Obj gNbrManager, gObjTable, gHoodN;
void *gNbrVt[24], *gPersonVt[16], *gNbrManagerVt[24], *gObjTableVt[48], *gHoodNVt[40];
std::mt19937 gFakeRng;
struct StorePlan {
    bool plantOnRestore = false;
    uint32_t count = 0;
} gPlan;
uint32_t __fastcall N_AddRef(FakeNbr *n, void *) { Log(801, n->sim, uint32_t(n->refs + 1)); return uint32_t(++n->refs); }
uint32_t __fastcall N_Release(FakeNbr *n, void *) { Log(802, n->sim, uint32_t(n->refs - 1)); return uint32_t(--n->refs); }
uint32_t __fastcall N_PersonId(FakeNbr *n, void *) { Log(815, n->sim); return 0x1000 + n->sim; }
uint32_t __fastcall N_Attribute(FakeNbr *n, void *, int32_t index) { Log(816, n->sim, uint32_t(index)); return index == 0xba ? gNbrCfg[n->sim].attrBA : 0x7777; }
uint32_t __fastcall N_Block(FakeNbr *n, void *, uint8_t keepClean) { Log(818, n->sim, keepClean); return reinterpret_cast<uint32_t>(n->block); }
uint32_t __fastcall PS_Slot12(FakeNbr *p, void *) { Log(812, p->sim); return 0xabc00000u + p->sim; }
uint32_t __fastcall NM_GetNeighbor(Obj *, void *, int32_t id, uint8_t all)
{
    Log(716, uint32_t(id), all);
    return id >= 0 && id < kSims && gNbrCfg[id].has ? reinterpret_cast<uint32_t>(&gNbr[id]) : 0;
}
uint32_t __fastcall OT_Find(Obj *, void *, uint32_t handle)
{
    Log(743, handle);
    const uint32_t sim = handle - 0x1000;
    return sim < uint32_t(kSims) && gNbrCfg[sim].live ? reinterpret_cast<uint32_t>(&gPerson[sim]) : 0;
}
void __fastcall HN_CopyPerson(Obj *, void *, FakeNbr *person) { Log(729, person ? person->sim : 99); }
uint32_t __fastcall G_Hood(Obj *, void *) { Log(706); return reinterpret_cast<uint32_t>(&gHoodN); }
uint32_t __fastcall G_NbrManager(Obj *, void *) { Log(710); return reinterpret_cast<uint32_t>(&gNbrManager); }
uint32_t __fastcall G_ObjTable(Obj *, void *) { Log(726); return reinterpret_cast<uint32_t>(&gObjTable); }
uint8_t __fastcall S_Store(W *, void *, uint32_t sim) { Log(606, sim & 0xffff); return 1; }
uint8_t __fastcall S_Restore(W *w, void *, uint32_t sim);
void __fastcall S_Queue(W *, void *, uint32_t sim, int32_t priority) { Log(635, sim & 0xffff, uint32_t(priority)); }
void SetupLifecycleVtables()
{
    gPatch[0][6] = reinterpret_cast<void *>(&S_Store);
    gPatch[0][7] = reinterpret_cast<void *>(&S_Restore);
    gPatch[0][35] = reinterpret_cast<void *>(&S_Queue);
    gGlobalsVt[6] = reinterpret_cast<void *>(&G_Hood);
    gGlobalsVt[10] = reinterpret_cast<void *>(&G_NbrManager);
    gGlobalsVt[26] = reinterpret_cast<void *>(&G_ObjTable);
    for (auto &s : gNbrVt) s = nullptr;
    gNbrVt[1] = reinterpret_cast<void *>(&N_AddRef);
    gNbrVt[2] = reinterpret_cast<void *>(&N_Release);
    gNbrVt[15] = reinterpret_cast<void *>(&N_PersonId);
    gNbrVt[16] = reinterpret_cast<void *>(&N_Attribute);
    gNbrVt[18] = reinterpret_cast<void *>(&N_Block);
    for (auto &s : gPersonVt) s = nullptr;
    gPersonVt[1] = reinterpret_cast<void *>(&N_AddRef);
    gPersonVt[2] = reinterpret_cast<void *>(&N_Release);
    gPersonVt[12] = reinterpret_cast<void *>(&PS_Slot12);
    gNbrManagerVt[16] = reinterpret_cast<void *>(&NM_GetNeighbor);
    gObjTableVt[43] = reinterpret_cast<void *>(&OT_Find);
    gHoodNVt[29] = reinterpret_cast<void *>(&HN_CopyPerson);
    gNbrManager.vt = gNbrManagerVt;
    gObjTable.vt = gObjTableVt;
    gHoodN.vt = gHoodNVt;
}
void ResetLifecycle(std::mt19937 &rng)
{
    for (uint32_t i = 0; i < uint32_t(kSims); ++i) {
        gNbr[i] = {gNbrVt, 1, i, {1, 2, 3, 4}};
        gPerson[i] = {gPersonVt, 1, 100 + i, {0, 0, 0, 0}};
        gPerson[i].sim = i;
        gNbrCfg[i].has = rng() % 4 != 0;
        gNbrCfg[i].live = rng() % 2 != 0;
        gNbrCfg[i].attrBA = uint16_t(rng() % 5 == 0 ? 1 + rng() % 3 : 0);
    }
    gFakeRng.seed(rng());
    gPlan = StorePlan();
}

// ---- worlds ----
void SetupVtables(difftest::Context &ctx)
{
    ClearPatches();
    gPatch[0][1] = reinterpret_cast<void *>(&S_AddRef);
    gPatch[0][23] = reinterpret_cast<void *>(&S_Definition);
    gPatch[0][26] = reinterpret_cast<void *>(&S_Tick);
    gPatch[0][30] = reinterpret_cast<void *>(&S_Find84);
    for (auto &s : gDefVt) s = nullptr;
    gDefVt[6] = reinterpret_cast<void *>(&D_A);
    gDefVt[7] = reinterpret_cast<void *>(&D_B);
    gDefVt[17] = reinterpret_cast<void *>(&D_Rule);
    gDefVt[19] = reinterpret_cast<void *>(&D_Flags);
    for (auto &s : gStreamVt) s = nullptr;
    gStreamVt[1] = reinterpret_cast<void *>(&ST_AddRef);
    gStreamVt[2] = reinterpret_cast<void *>(&ST_Release);
    for (auto &s : gSegmentVt) s = nullptr;
    gSegmentVt[2] = reinterpret_cast<void *>(&CN_Release);
    gSegmentVt[22] = reinterpret_cast<void *>(&SG_OpenRecord);
    gSegmentVt[24] = reinterpret_cast<void *>(&SG_CloseRecord);
    gSegmentVt[26] = reinterpret_cast<void *>(&SG_DeleteRecord);
    for (auto &s : gRecordVt) s = nullptr;
    gRecordVt[0] = reinterpret_cast<void *>(&RC_QueryInterface);
    gRecordVt[2] = reinterpret_cast<void *>(&CN_Release);
    for (auto &s : gPersistVt) s = nullptr;
    gPersistVt[34] = reinterpret_cast<void *>(&P_FindSegment);
    gPersist.vt = gPersistVt;
    for (auto &s : gGlobalsVt) s = nullptr;
    gGlobalsVt[4] = reinterpret_cast<void *>(&G_WantService);
    gGlobalsVt[8] = reinterpret_cast<void *>(&G_Table);
    gTableVt[10] = reinterpret_cast<void *>(&T_Find);
    gInfoVt[24] = reinterpret_cast<void *>(&I_Source);
    gSourceVt[6] = reinterpret_cast<void *>(&S_Group);
    gHolderVt[6] = reinterpret_cast<void *>(&H_Hood);
    gHoodVt[8] = reinterpret_cast<void *>(&HD_KeyHolder);
    gKeyHolderVt[5] = reinterpret_cast<void *>(&K_Key);
    gGlobals.vt = gGlobalsVt;
    gTable.vt = gTableVt;
    gInfo.vt = gInfoVt;
    gSource.vt = gSourceVt;
    gHolder.vt = gHolderVt;
    gHood.vt = gHoodVt;
    gKeyHolder.vt = gKeyHolderVt;
    for (auto &s : gInstVt) s = nullptr;
    gInstVt[0] = reinterpret_cast<void *>(&FI_QI);
    gInstVt[1] = reinterpret_cast<void *>(&FI_AddRef);
    gInstVt[2] = reinterpret_cast<void *>(&FI_Release);
    gInstVt[12] = reinterpret_cast<void *>(&FI_Def);
    gInstVt[13] = reinterpret_cast<void *>(&FI_Guid);
    gInstVt[14] = reinterpret_cast<void *>(&FI_Param);
    gInstVt[23] = reinterpret_cast<void *>(&FI_HasId);
    gInstVt[24] = reinterpret_cast<void *>(&FI_GetId);
    gInstVt[28] = reinterpret_cast<void *>(&FI_Flag10);
    gInstVt[32] = reinterpret_cast<void *>(&FI_SetId);
    for (auto &s : gFaceVt) s = nullptr;
    gFaceVt[1] = reinterpret_cast<void *>(&FF_AddRef);
    gFaceVt[2] = reinterpret_cast<void *>(&FF_Release);
    gFaceVt[3] = reinterpret_cast<void *>(&FF_Write);
    SetupLifecycleVtables();
    *ctx.image.At<void *>(0x10cc288) = &gGlobals;
    *ctx.image.At<void *>(0x10cc2a8) = &gHolder;
    *ctx.image.At<void *>(0x10faf6c) = &gPersist;
}

// Everything the fakes hold, from one seed.
void ResetFakes(difftest::Context &ctx, uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 7);
    ResetPool(ctx);
    gCfg = PersistCfg();
    gTick = rng() % 4 ? 1000 + rng() % 50 : rng();
    gGroup = rng();
    gKey = rng();
    for (int i = 0; i < 8; ++i) {
        gDefs[i].vt = gDefVt;
        gDefs[i].guid = KnownGuid(i);
        gDefs[i].flags = rng() % 5 ? (1u << (rng() % 4)) | (rng() % 3 ? 0 : 8u) : 0;
        gDefs[i].rule = rng() % 3 == 0 ? 3 : (rng() % 2 ? 5 : rng() % 8);
        gDefs[i].a = rng();
        gDefs[i].b = rng();
    }
    for (uint32_t i = 0; i < 24; ++i) {
        FakeInst &f = gInst[i];
        f.vt = gInstVt;
        f.vtFace = gFaceVt;
        f.refs = 1;
        f.tag = 700 + i;
        f.guid = rng() % 8 == 0 ? 0x7777 : KnownGuid(rng() % 8);
        f.hasId = rng() % 2;
        f.id = rng() % 5 ? rng() % 200 : rng();
        f.flag10 = rng() % 3 == 0;
        f.simId = uint16_t(rng() % 6);
        f.def = rng() % 6 ? FindDef(f.guid) : nullptr;
        f.param.kind = rng() % 3 == 0 ? 3 : rng() % 6;
        f.param.value = rng() % 6 ? rng() % 10 : rng();
        f.param.extra = int16_t(rng() % 3 ? int32_t(rng() % 7) - 1 : int32_t(rng()));
    }
    gStream.vt = gStreamVt;
    gStream.tokens.clear();
    gStream.pos = 0;
    gStream.exhausted = false;
    gStream.errorAt = ~size_t(0);
    gStream.refs = 0;
    gStream.calls = 0;
    gSegment = {gSegmentVt, 0, 1};
    gRecord = {gRecordVt, 0, 2};
    ResetLifecycle(rng);
    gLog.clear();
}

// ---- a simulator per side ----
alignas(8) unsigned char gSimBuf[sizeof(W)];
W *BuildSim(uint32_t seed)
{
    W *w = reinterpret_cast<W *>(gSimBuf);
    std::mt19937 rng(seed * 17 + 3);
    std::memset(static_cast<void *>(w), 0x5c, sizeof(W));
    wantshape::Construct(w);
    gSim = w;
    w->mOptionMask = rng() % 4 ? int32_t(1u << (rng() % 4)) | int32_t(rng() % 2) : int32_t(rng());
    w->mTick = rng() % 50;
    w->mCurrentSim = uint16_t(rng() % 3 ? 99 : rng());
    for (int i = 0, n = rng() % 6; i < n; ++i) w->mMap84[int32_t(rng() % 12) - 2] = int32_t(rng() % 7);
    // Pre-existing contents of the sim under test (id 3) that the calls replace, clear or release.
    const int16_t sim = 3;
    if (rng() % 3 == 0) w->mLifetimeWants[sim].object = &gInst[20];
    auto fill = [&](WantRefList &l, uint32_t first) {
        const uint32_t n = rng() % 4;
        if (!n) return;
        const uint32_t cap = n + rng() % 3;
        l.mFirst = static_cast<void **>(bpt::ServiceAlloc(cap * 4));
        l.mLast = l.mFirst + n;
        l.mEnd = l.mFirst + cap;
        for (uint32_t i = 0; i < n; ++i) l.mFirst[i] = rng() % 4 ? &gInst[first + i] : nullptr;
    };
    if (rng() % 2) fill(w->mWants[sim], 14);
    if (rng() % 2) fill(w->mFears[sim], 17);
    if (rng() % 2) w->mMap48[sim] = int16_t(rng());
    return w;
}
// A store with memories of fake instances, owned by the simulator's table.
WantMemoryStore *PlantStore(W *w, int16_t sim, std::mt19937 &rng, uint32_t first, uint32_t count)
{
    auto *store = static_cast<WantMemoryStore *>(::operator new(sizeof(WantMemoryStore)));
    store->Construct();
    store->lastTick = rng() % 2 ? 0x3fffffff : rng() % 900;
    store->nextId = rng() % 3 ? int32_t(rng() % 100) : int32_t(rng());
    for (uint32_t i = 0; i < count; ++i) {
        FakeInst &f = gInst[first + i];
        MemoryList &list = store->byGuid[f.guid];
        list.Append(reinterpret_cast<ifaces::cITSWantInstance *>(&f));
    }
    if (rng() % 3 == 0) store->byGuid[KnownGuid(rng() % 8)]; // an empty list stays
    w->mMemoryStores[sim] = store;
    return store;
}
// Simulator slot 7 (RestoreSimWants) as ResetMemories reaches it: logged, and on planned seeds it loads a store of
// fake memories for the sim.
uint8_t __fastcall S_Restore(W *w, void *, uint32_t sim)
{
    Log(607, sim & 0xffff);
    if (gPlan.plantOnRestore) PlantStore(w, int16_t(sim), gFakeRng, 12, gPlan.count);
    return 1;
}
// A want/fear list of fake instances (some null) in an allocator-service block, as BuildSim fills them.
void FillRefsFor(WantRefList &l, std::mt19937 &rng, uint32_t first)
{
    const uint32_t n = 1 + rng() % 4;
    const uint32_t cap = n + rng() % 3;
    l.mFirst = static_cast<void **>(bpt::ServiceAlloc(cap * 4));
    l.mLast = l.mFirst + n;
    l.mEnd = l.mFirst + cap;
    for (uint32_t i = 0; i < n; ++i) l.mFirst[i] = rng() % 4 ? &gInst[first + i] : nullptr;
}
void TeardownSim(W *w)
{
    for (auto &e : w->mMemoryStores) {
        if (!e.second) continue;
        e.second->Clear();
        e.second->Destruct();
        ::operator delete(e.second, sizeof(WantMemoryStore));
    }
    w->mMemoryStores.clear();
    Destroy(w);
}

// ---- snapshots ----
void DumpInst(std::vector<int64_t> &d, const void *p)
{
    if (!p) {
        d.push_back(-1);
        return;
    }
    const uint8_t *b = static_cast<const uint8_t *>(p);
    if (InPool(p)) {
        d.push_back((b - gPage - 0x10) / kBlock);
        d.push_back(*reinterpret_cast<const uint32_t *>(b + 8));
        d.push_back(*reinterpret_cast<const uint16_t *>(b + 0xc));
        d.push_back(*reinterpret_cast<const uint32_t *>(b + 0x10));
        for (uint32_t off : {0x14u, 0x18u, 0x28u, 0x2cu, 0x30u}) d.push_back(*reinterpret_cast<const uint32_t *>(b + off));
        d.push_back(*reinterpret_cast<const uint16_t *>(b + 0x1c));
        d.push_back(b[0x38]);
        return;
    }
    for (const FakeInst &f : gInst)
        if (p == &f) {
            d.push_back(f.tag);
            d.push_back(f.refs);
            d.push_back(f.id);
            d.push_back(f.hasId);
            return;
        }
    d.push_back(-2);
}
void DumpRefs(std::vector<int64_t> &d, const WantRefList &l)
{
    d.push_back(l.Size());
    d.push_back(l.mEnd - l.mFirst);
    for (void **it = l.mFirst; it != l.mLast; ++it) DumpInst(d, *it);
}
void DumpStore(std::vector<int64_t> &d, const WantMemoryStore *s)
{
    if (!s) {
        d.push_back(-3);
        return;
    }
    d.push_back(s->lastTick);
    d.push_back(s->nextId);
    d.push_back(int64_t(s->byGuid.size()));
    for (const auto &e : s->byGuid) {
        d.push_back(e.first);
        d.push_back(e.second.size);
        const MemoryNode *head = e.second.head;
        uint32_t n = 0;
        for (const MemoryNode *node = head->next; node != head && n < 1000; node = node->next, ++n) {
            if (node->next->prev != node) d.push_back(-4);
            DumpInst(d, node->memory);
        }
        d.push_back(n);
    }
}
std::vector<int64_t> Snapshot(W *w)
{
    std::vector<int64_t> d;
    d.push_back(w->mMemoryStores.size());
    for (auto &e : w->mMemoryStores) {
        d.push_back(e.first);
        DumpStore(d, e.second);
    }
    d.push_back(w->mWants.size());
    for (auto &e : w->mWants) d.push_back(e.first), DumpRefs(d, e.second);
    d.push_back(w->mFears.size());
    for (auto &e : w->mFears) d.push_back(e.first), DumpRefs(d, e.second);
    d.push_back(w->mLifetimeWants.size());
    for (auto &e : w->mLifetimeWants) d.push_back(e.first), DumpInst(d, e.second.object);
    d.push_back(w->mMap48.size());
    for (auto &e : w->mMap48) d.push_back(e.first), d.push_back(e.second);
    d.push_back(w->mTick);
    // Lane DR: the update queue, the queued copies, the update in progress and the fakes' counts.
    d.push_back(w->mPendingSims.size());
    for (auto &e : w->mPendingSims) d.push_back(e.first), d.push_back(e.second);
    d.push_back(w->mMap50.size());
    for (auto &e : w->mMap50) d.push_back(e.first), DumpRefs(d, e.second);
    d.push_back(w->mMap58.size());
    for (auto &e : w->mMap58) d.push_back(e.first), DumpRefs(d, e.second);
    d.push_back(w->mCurrentSim), d.push_back(w->mUpdateState);
    DumpRefs(d, w->mVecEC);
    int64_t current = w->mCurrentStore ? 999 : -1; // 999: points at a store no longer in the table
    for (auto &e : w->mMemoryStores)
        if (e.second && e.second == w->mCurrentStore) current = e.first;
    d.push_back(current);
    for (int i = 0; i < kSims; ++i) d.push_back(gNbr[i].refs), d.push_back(gPerson[i].refs);
    for (const FakeInst &f : gInst) d.push_back(f.refs), d.push_back(f.id), d.push_back(f.hasId);
    d.push_back(gStream.refs), d.push_back(gSegment.refs), d.push_back(gRecord.refs);
    d.push_back(int64_t(gStream.pos)), d.push_back(gStream.exhausted);
    PoolState(d);
    return d;
}

struct Outcome {
    uint64_t result = 0;
    std::vector<int64_t> state;
    std::vector<uint32_t> log;
};

// Runs `body(original, sim, rng)` on worlds built from the seed, one per route (tests/ts_want_simulator_shape.h; `routes` 2
// for functions of other classes, which have no vtable route), and compares each route's outcome with route 0's.
template <typename Prepare, typename Body>
void Compare(difftest::Context &ctx, const char *name, uint32_t seeds, Prepare prepare, Body body, int routes = kRoutes)
{
    bpt::Env env(ctx);
    SetupVtables(ctx);
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        ++ctx.cases;
        Outcome out[kRoutes];
        for (int side = 0; side < routes; ++side) {
            gRoute = side;
            gTrapHits = 0;
            ResetFakes(ctx, seed);
            W *w = BuildSim(seed);
            std::mt19937 rng(seed * 101 + 13);
            prepare(w, rng);
            gLog.clear();
            out[side].result = body(side == 0, w, rng);
            out[side].log = gLog;
            out[side].state = Snapshot(w);
            if (gTrapHits) ctx.Fail("%s seed %u route %d: the adapter dispatched through the vtable", name, seed, side);
            TeardownSim(w);
        }
        gRoute = 0;
        for (int k = 1; k < routes; ++k) {
            const Outcome &a = out[0], &b = out[k];
            if (a.result != b.result) ctx.Fail("%s seed %u route %d: result %llx vs %llx", name, seed, k, (unsigned long long)a.result, (unsigned long long)b.result);
            if (a.log != b.log) {
                size_t i = 0;
                while (i < a.log.size() && i < b.log.size() && a.log[i] == b.log[i]) ++i;
                ctx.Fail("%s seed %u route %d: calls differ at word %zu (%zu vs %zu words; %u vs %u)", name, seed, k, i, a.log.size(), b.log.size(),
                         i < a.log.size() ? a.log[i] : 0, i < b.log.size() ? b.log[i] : 0);
            }
            if (a.state != b.state) {
                size_t i = 0;
                while (i < a.state.size() && i < b.state.size() && a.state[i] == b.state[i]) ++i;
                ctx.Fail("%s seed %u route %d: state differs at %zu (%zu vs %zu entries; %lld vs %lld)", name, seed, k, i, a.state.size(), b.state.size(),
                         i < a.state.size() ? (long long)a.state[i] : 0LL, i < b.state.size() ? (long long)b.state[i] : 0LL);
            }
        }
    }
}

// ---- token scripts for the streams ----
struct Gen {
    std::mt19937 rng;
    explicit Gen(uint32_t seed) : rng(seed) {}
    uint32_t Pick(uint32_t n) { return rng() % n; }
    uint32_t Guid()
    {
        const uint32_t r = Pick(10);
        return r < 8 ? KnownGuid(Pick(7)) : (r == 8 ? kDefaultMemoryGuid : 0x7777);
    }
    // One cTSWantInstance record of the given version (notes/feedback/CY.md, "Entry").
    void Instance(Tok &t, uint32_t v)
    {
        t.push_back(v);
        if (v > 10) return; // Read refuses a newer version after the version word; the rest of the stream goes on
        t.push_back(Pick(5) ? Pick(6) : rng());                // sim id (i16)
        t.push_back(Guid());
        const uint32_t kind = Pick(9);
        t.push_back(kind);                                      // u8
        const uint32_t tiny = Pick(10);
        switch (kind) {
        case 1: case 4: if (kind == 1 || v >= 2) t.push_back(tiny); break;
        case 2: t.push_back(rng()); break;
        case 3: case 5: if (v >= 2) t.push_back(Pick(4) ? tiny : rng()); break;
        case 6: if (v >= 2) t.push_back(v >= 10 ? kDefaultMemoryGuid : Pick(10)); break;
        default: break;
        }
        const uint32_t extraFrom[] = {7, 8, 3, 3, 2, 2, 2};
        if (kind < 7 && v >= extraFrom[kind]) t.push_back(Pick(3) ? Pick(7) : rng());
        t.push_back(v >= 4 ? (Pick(3) ? Pick(300) : rng()) : Pick(100));
        if (v >= 6) t.push_back(rng());
        if (v >= 9) t.push_back(rng());
        if (v >= 5) t.push_back(Pick(3) ? Pick(2) * 0x10 : rng());
    }
    uint32_t InstanceVersion() { return Pick(14) == 0 ? 11 : Pick(11); }
    void Wants(Tok &t, uint32_t v)
    {
        t.push_back(v);
        if (v >= 3) {
            const bool has = Pick(2);
            t.push_back(has);
            if (has) Instance(t, InstanceVersion());
        }
        for (uint32_t defaultSlots : {4u, 3u}) {
            uint32_t slots = defaultSlots;
            if (v >= 2) {
                slots = Pick(4) ? defaultSlots : Pick(8);
                t.push_back(slots);
            }
            const uint32_t count = Pick(slots + 3);
            t.push_back(count);
            for (uint32_t i = 0; i < count; ++i) Instance(t, InstanceVersion());
        }
        if (v >= 4) t.push_back(Pick(3) ? Pick(40) : rng());
    }
    void Memories(Tok &t, uint32_t v)
    {
        t.push_back(v);
        if (v == 2) t.push_back(Pick(3) ? Pick(300) : rng());
        const uint32_t keys = Pick(4);
        t.push_back(keys);
        for (uint32_t k = 0; k < keys; ++k) {
            t.push_back(Guid());
            const uint32_t n = Pick(4);
            t.push_back(n);
            for (uint32_t i = 0; i < n; ++i) Instance(t, InstanceVersion());
        }
    }
    // Corrupts a script: truncation, or a stream error from some point.
    void Damage(Tok &t, size_t &errorAt)
    {
        errorAt = ~size_t(0);
        const uint32_t r = Pick(8);
        if (r == 0 && !t.empty()) t.resize(Pick(uint32_t(t.size())));
        else if (r == 1) errorAt = Pick(uint32_t(t.size() + 1));
    }
};

uint32_t Dirty(std::mt19937 &rng, int16_t sim) { return (rng() & 0xffff0000u) | uint16_t(sim); }

// The stream fake as a cIGZOStream (Set*) or a cIGZIStream (Get*); the same slots, so one is chosen per test.
void UseOutStream()
{
    gStreamVt[5] = reinterpret_cast<void *>(&OS_SetU8);
    gStreamVt[6] = reinterpret_cast<void *>(&OS_SetS16);
    gStreamVt[8] = reinterpret_cast<void *>(&OS_SetS32);
    gStreamVt[9] = reinterpret_cast<void *>(&OS_SetU32);
    gStreamVt[18] = reinterpret_cast<void *>(&OS_GetError);
}
void UseInStream()
{
    gStreamVt[5] = reinterpret_cast<void *>(&IS_GetU8);
    gStreamVt[6] = reinterpret_cast<void *>(&IS_GetS16);
    gStreamVt[8] = reinterpret_cast<void *>(&IS_GetS32);
    gStreamVt[9] = reinterpret_cast<void *>(&IS_GetU32);
    gStreamVt[18] = reinterpret_cast<void *>(&IS_GetError);
}

} // namespace pw
} // namespace

// ---- WantMemoryStore: construction, tick, add, clear, destroy ----
DIFF_TEST(WantMemoryStore_Lifecycle, 0x5509c0)
{
    using Ctor = WantMemoryStore *(__thiscall *)(WantMemoryStore *);
    using Op = void(__thiscall *)(WantMemoryStore *);
    pw::Compare(ctx, "lifecycle", 300, [](W *, std::mt19937 &) {}, [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        alignas(8) unsigned char raw[sizeof(WantMemoryStore)];
        std::memset(raw, 0xa5, sizeof raw);
        auto *s = reinterpret_cast<WantMemoryStore *>(raw);
        WantMemoryStore *r = o ? Orig<Ctor>(ctx, 0x5509c0)(s) : s->Construct();
        uint64_t sig = r == s;
        sig = sig << 8 | (s->lastTick == 0x3fffffff) << 1 | (s->nextId == 0);
        if (o) Orig<Op>(ctx, 0x551360)(s);
        else s->SyncTick();
        sig = sig << 32 | s->lastTick;
        // Populate with fake memories, clear, repopulate, destroy: releases go to the fakes.
        for (uint32_t k = 0; k < 6; ++k) {
            pw::FakeInst &f = pw::gInst[k];
            s->byGuid[f.guid].Append(reinterpret_cast<ifaces::cITSWantInstance *>(&f));
        }
        if (rng() % 2) {
            if (o) Orig<Op>(ctx, 0x5516d0)(s);
            else s->Clear();
            for (uint32_t k = 6; k < 9; ++k) {
                pw::FakeInst &f = pw::gInst[k];
                s->byGuid[f.guid].Append(reinterpret_cast<ifaces::cITSWantInstance *>(&f));
            }
        }
        gLog.push_back(uint32_t(s->byGuid.size()));
        if (o) Orig<Op>(ctx, 0x550b70)(s);
        else s->Destruct();
        (void)w;
        return sig;
    }, 2);
}

DIFF_TEST(WantMemoryStore_AddMemory, 0x550cb0)
{
    using Fn = void(__thiscall *)(WantMemoryStore *, void *);
    pw::Compare(ctx, "AddMemory", 600, [](W *w, std::mt19937 &rng) { pw::PlantStore(w, 3, rng, 0, rng() % 4); }, [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        WantMemoryStore *s = w->mMemoryStores[3];
        const uint32_t n = 1 + rng() % 4;
        for (uint32_t i = 0; i < n; ++i) {
            pw::FakeInst *f = &pw::gInst[8 + i];
            if (rng() % 3 == 0) pw::gTick += rng() % 30;
            if (o) Orig<Fn>(ctx, 0x550cb0)(s, f);
            else s->AddMemory(reinterpret_cast<ifaces::cITSWantInstance *>(f));
        }
        return uint64_t(s->nextId) << 32 | s->lastTick;
    }, 2);
}

DIFF_TEST(WantMemoryStore_Write, 0x551700)
{
    using Fn = uint8_t(__thiscall *)(WantMemoryStore *, void *);
    pw::Compare(ctx, "Write", 600, [](W *w, std::mt19937 &rng) { pw::UseOutStream(); pw::PlantStore(w, 3, rng, 0, rng() % 12); }, [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        WantMemoryStore *s = w->mMemoryStores[3];
        pw::gStream.errorAt = rng() % 4 == 0 ? rng() % 30 : ~size_t(0);
        const uint8_t r = o ? Orig<Fn>(ctx, 0x551700)(s, &pw::gStream) : s->Write(reinterpret_cast<ifaces::cIGZOStream *>(&pw::gStream));
        return r;
    }, 2);
}

DIFF_TEST(WantMemoryStore_Read, 0x551380)
{
    using Fn = uint8_t(__thiscall *)(WantMemoryStore *, void *);
    pw::Compare(
        ctx, "Read", 1500,
        [](W *w, std::mt19937 &rng) {
            pw::UseInStream();
            if (rng() % 2) pw::PlantStore(w, 3, rng, 0, rng() % 6);
            else pw::PlantStore(w, 3, rng, 0, 0);
            pw::Gen g(rng());
            g.Memories(pw::gStream.tokens, rng() % 8 == 0 ? 3 : g.Pick(3));
            g.Damage(pw::gStream.tokens, pw::gStream.errorAt);
        },
        [&](bool o, W *w, std::mt19937 &) -> uint64_t {
            WantMemoryStore *s = w->mMemoryStores[3];
            return o ? Orig<Fn>(ctx, 0x551380)(s, &pw::gStream) : s->Read(reinterpret_cast<ifaces::cIGZIStream *>(&pw::gStream));
        }, 2);
}

DIFF_TEST(WantSimulator_GetMemoryStore, 0x48d650)
{
    using Fn = WantMemoryStore *(__thiscall *)(W *, uint32_t);
    pw::Compare(
        ctx, "GetMemoryStore", 500,
        [](W *w, std::mt19937 &rng) {
            if (rng() % 2) pw::PlantStore(w, 3, rng, 0, rng() % 4);
            if (rng() % 3 == 0) w->mMemoryStores[5] = nullptr;
        },
        [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
            uint64_t sig = 0;
            for (int16_t sim : {int16_t(3), int16_t(4), int16_t(3), int16_t(7)}) {
                const uint32_t arg = pw::Dirty(rng, sim);
                WantMemoryStore *s = wantshape::Call<Fn>(ctx, w, 0x48d650, kNoFace, WANT_ADAPTER(GetMemoryStore), arg);
                sig = sig * 3 + (s == w->mMemoryStores[sim]);
            }
            return sig;
        });
}

DIFF_TEST(WantSimulator_AddDefaultMemory, 0x486310)
{
    using Fn = void(__thiscall *)(W *, uint32_t);
    pw::Compare(
        ctx, "AddDefaultMemory", 500,
        [](W *w, std::mt19937 &rng) {
            if (rng() % 2) {
                pw::PlantStore(w, 3, rng, 0, rng() % 4);
                if (rng() % 2) pw::gInst[0].guid = kDefaultMemoryGuid;
            }
        },
        [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
            for (int16_t sim : {int16_t(3), int16_t(6), int16_t(3)}) {
                const uint32_t arg = pw::Dirty(rng, sim);
                wantshape::Call<Fn>(ctx, w, 0x486310, kNoFace, WANT_ADAPTER(AddDefaultMemory), arg);
            }
            return 0;
        });
}

DIFF_TEST(WantSimulator_IsWantAllowed, 0x48e2f0)
{
    using Fn = uint8_t(__thiscall *)(W *, void *);
    pw::Compare(ctx, "IsWantAllowed", 2000, [](W *, std::mt19937 &) {}, [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
        uint64_t sig = 0;
        for (uint32_t i = 0; i < 6; ++i) {
            pw::FakeInst *f = rng() % 10 ? &pw::gInst[i] : nullptr;
            if (f && rng() % 3 == 0) f->param.kind = 3;
            const uint8_t r = wantshape::Call<Fn>(ctx, w, 0x48e2f0, kNoFace, WANT_ADAPTER(IsWantAllowed), f);
            sig = sig * 2 + (r != 0);
        }
        return sig;
    });
}

DIFF_TEST(WantSimulator_WriteSimWants, 0x4983a0)
{
    using Fn = uint8_t(__thiscall *)(W *, uint32_t, void *);
    pw::Compare(
        ctx, "WriteSimWants", 800,
        [](W *w, std::mt19937 &rng) {
            pw::UseOutStream();
            pw::gStream.errorAt = rng() % 4 == 0 ? rng() % 24 : ~size_t(0);
        },
        [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
            uint64_t sig = 0;
            for (int16_t sim : {int16_t(3), int16_t(5), int16_t(3)}) {
                const uint32_t arg = pw::Dirty(rng, sim);
                const uint8_t r = wantshape::Call<Fn>(ctx, w, 0x4983a0, kNoFace, WANT_ADAPTER(WriteSimWants), arg, &pw::gStream);
                sig = sig * 2 + (r != 0);
            }
            return sig;
        });
}

DIFF_TEST(WantSimulator_ReadSimWants, 0x492970)
{
    using Fn = uint8_t(__thiscall *)(W *, uint32_t, void *);
    pw::Compare(
        ctx, "ReadSimWants", 2500,
        [](W *w, std::mt19937 &rng) {
            pw::UseInStream();
            (void)w;
            pw::Gen g(rng());
            g.Wants(pw::gStream.tokens, rng() % 10 == 0 ? 7 : g.Pick(7));
            g.Damage(pw::gStream.tokens, pw::gStream.errorAt);
        },
        [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
            const int16_t sim = rng() % 4 ? 3 : 5;
            const uint32_t arg = pw::Dirty(rng, sim);
            return wantshape::Call<Fn>(ctx, w, 0x492970, kNoFace, WANT_ADAPTER(ReadSimWants), arg, &pw::gStream);
        });
}

DIFF_TEST(WantSimulator_ReadSimRecord, 0x492ea0)
{
    using Fn = uint8_t(__thiscall *)(W *, uint32_t, void *);
    pw::Compare(
        ctx, "ReadSimRecord", 2000,
        [](W *w, std::mt19937 &rng) {
            pw::UseInStream();
            if (rng() % 3 == 0) pw::PlantStore(w, 3, rng, 0, rng() % 4);
            pw::Gen g(rng());
            g.Wants(pw::gStream.tokens, g.Pick(7));
            g.Memories(pw::gStream.tokens, g.Pick(3));
            g.Damage(pw::gStream.tokens, pw::gStream.errorAt);
            pw::gCfg.findOk = rng() % 10 != 0;
            pw::gCfg.openOk = rng() % 10 != 0;
            pw::gCfg.qiOk = rng() % 10 != 0;
        },
        [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
            uint32_t key[4] = {kMemoriesRecordType, uint32_t(rng()), uint32_t(rng()), 0};
            const int16_t sim = 3;
            const uint32_t arg = pw::Dirty(rng, sim);
            return wantshape::Call<Fn>(ctx, w, 0x492ea0, kNoFace, WANT_ADAPTER(ReadSimRecord), arg, key);
        });
}

DIFF_TEST(WantSimulator_StoreSimWants, 0x4985e0)
{
    using Fn = uint8_t(__thiscall *)(W *, uint32_t);
    pw::Compare(
        ctx, "StoreSimWants", 1500,
        [](W *w, std::mt19937 &rng) {
            pw::UseOutStream();
            pw::gStream.errorAt = rng() % 5 == 0 ? rng() % 40 : ~size_t(0);
            if (rng() % 3 != 0) pw::PlantStore(w, 3, rng, 0, rng() % 10);
            w->mCurrentSim = 99;
            pw::gCfg.findOk = rng() % 10 != 0;
            pw::gCfg.openOk = rng() % 10 != 0;
            pw::gCfg.qiOk = rng() % 10 != 0;
        },
        [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
            uint64_t sig = 0;
            for (int16_t sim : {int16_t(3), int16_t(0), int16_t(8)}) {
                const uint32_t arg = pw::Dirty(rng, sim);
                const uint8_t r = wantshape::Call<Fn>(ctx, w, 0x4985e0, 0, WANT_NA(StoreSimWants), arg);
                sig = sig * 4 + r;
            }
            return sig;
        });
}

// ======================================================================================================
// Sim lifecycle (lane DR): WantSimContext, RestoreSimWants, SaveAllSims, DeleteSim, ResetMemories, QueueSimUpdate,
// FlushUpdates. Same worlds as above plus neighbours / live persons per sim (pw::gNbrCfg) and logging fakes for the
// simulator's slots 6, 7 and 35. RunUpdate (0x4880b0, unreplaced) is never reached: the wait loops need the sim queued
// or current, and every world keeps mPendingSims free of the sims under test and mCurrentSim on another sim (the
// RestoreSimWants test sets mCurrentSim to the sim on purpose: that path abandons the update without running it).
// ======================================================================================================

DIFF_TEST(WantSimContext_Construct, 0x4854b0)
{
    using Fn = WantSimContext *(__thiscall *)(WantSimContext *, uint32_t);
    pw::Compare(ctx, "WantSimContext", 400, [](W *, std::mt19937 &) {}, [&](bool o, W *, std::mt19937 &rng) -> uint64_t {
        uint64_t sig = 0;
        for (int16_t sim : {int16_t(0), int16_t(1), int16_t(2), int16_t(3), int16_t(4), int16_t(5), int16_t(-3)}) {
            WantSimContext c;
            std::memset(&c, 0xee, sizeof c);
            const uint32_t arg = pw::Dirty(rng, sim);
            WantSimContext *r = o ? Orig<Fn>(ctx, 0x4854b0)(&c, arg) : c.Construct(sim);
            const auto *n = reinterpret_cast<pw::FakeNbr *>(c.neighbor);
            const auto *p = static_cast<pw::FakeNbr *>(c.person);
            uint32_t attributes = c.attributes;
            if (n && attributes == reinterpret_cast<uint32_t>(n->block)) attributes = 0x5555;
            Log(900, uint32_t(c.simId), n ? n->sim : 99, p ? p->sim : 99, attributes);
            sig = sig * 2 + (r == &c);
            c.Destruct();
        }
        return sig;
    }, 2);
}

DIFF_TEST(WantSimulator_RestoreSimWants, 0x493510)
{
    using Fn = uint8_t(__thiscall *)(W *, uint32_t);
    pw::Compare(
        ctx, "RestoreSimWants", 2000,
        [](W *w, std::mt19937 &rng) {
            pw::UseInStream();
            if (rng() % 3 == 0) pw::PlantStore(w, 3, rng, 0, rng() % 4);
            if (rng() % 4 == 0) pw::PlantStore(w, 4, rng, 4, rng() % 3);
            pw::Gen g(rng());
            g.Wants(pw::gStream.tokens, g.Pick(7));
            g.Memories(pw::gStream.tokens, g.Pick(3));
            g.Damage(pw::gStream.tokens, pw::gStream.errorAt);
            pw::gCfg.findOk = rng() % 6 != 0;
            pw::gCfg.openOk = rng() % 10 != 0;
            pw::gCfg.qiOk = rng() % 10 != 0;
            w->mPendingPriority = rng() % 3 ? rng() % 5 : rng();
            w->mUpdateState = rng() % 4;
            if (rng() % 3 == 0) w->mCurrentSim = 3;
            if (rng() % 3 == 0) pw::FillRefsFor(w->mVecEC, rng, 0);
            if (rng() % 2) w->mLifetimeWants[4].object = &pw::gInst[21];
        },
        [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
            const uint32_t r = rng() % 8;
            const int16_t sim = r < 5 ? 3 : (r < 7 ? 4 : 0);
            const uint32_t arg = pw::Dirty(rng, sim);
            return wantshape::Call<Fn>(ctx, w, 0x493510, 0, WANT_NA(RestoreSimWants), arg);
        });
}

DIFF_TEST(WantSimulator_SaveAllSims, 0x4952b0)
{
    using Fn = uint8_t(__thiscall *)(W *, uint32_t);
    pw::Compare(
        ctx, "SaveAllSims", 600,
        [](W *w, std::mt19937 &rng) {
            for (int16_t sim : {int16_t(1), int16_t(3), int16_t(4), int16_t(6), int16_t(9)})
                if (rng() % 2) pw::PlantStore(w, sim, rng, 0, rng() % 2);
        },
        [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
            const uint32_t flag = (rng() & 0xffffff00u) | (rng() % 3 ? 1u : 0u);
            const uint32_t r = wantshape::Call<Fn>(ctx, w, 0x4952b0, 0, WANT_NA(SaveAllSims), flag);
            return r & 0xff;
        });
}

DIFF_TEST(WantSimulator_DeleteSim, 0x487be0)
{
    using Fn = void(__thiscall *)(W *, uint32_t);
    pw::Compare(
        ctx, "DeleteSim", 800,
        [](W *w, std::mt19937 &rng) {
            if (rng() % 4) {
                WantMemoryStore *s = pw::PlantStore(w, 3, rng, 0, rng() % 5);
                if (rng() % 2) w->mCurrentStore = s;
            }
            if (rng() % 2) pw::PlantStore(w, 5, rng, 6, rng() % 3);
            pw::gCfg.findOk = rng() % 4 != 0;
            w->mCurrentSim = 99; // RunUpdate not reached
        },
        [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
            for (int16_t sim : {int16_t(3), int16_t(0), int16_t(4), int16_t(3)}) {
                const uint32_t arg = pw::Dirty(rng, sim);
                wantshape::Call<Fn>(ctx, w, 0x487be0, kNoFace, WANT_ADAPTER(DeleteSim), arg);
            }
            return 0;
        });
}

DIFF_TEST(WantSimulator_ResetMemories, 0x487a70)
{
    using Fn = void(__thiscall *)(W *, uint32_t);
    pw::Compare(
        ctx, "ResetMemories", 800,
        [](W *w, std::mt19937 &rng) {
            if (rng() % 2) {
                WantMemoryStore *s = pw::PlantStore(w, 3, rng, 0, rng() % 5);
                if (rng() % 2) w->mCurrentStore = s;
            }
            pw::gPlan.plantOnRestore = rng() % 2 != 0;
            pw::gPlan.count = rng() % 4;
            w->mCurrentSim = 99; // RunUpdate not reached
        },
        [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
            for (int16_t sim : {int16_t(3), int16_t(0), int16_t(5), int16_t(3)}) {
                const uint32_t arg = pw::Dirty(rng, sim);
                wantshape::Call<Fn>(ctx, w, 0x487a70, 0, WANT_NA(ResetMemories), arg);
            }
            return 0;
        });
}

DIFF_TEST(WantSimulator_QueueSimUpdate, 0x494870)
{
    using Fn = void(__thiscall *)(W *, uint32_t, int32_t);
    pw::Compare(
        ctx, "QueueSimUpdate", 1500,
        [](W *w, std::mt19937 &rng) {
            for (int i = 0, n = rng() % 4; i < n; ++i) w->mPendingSims[int16_t(int32_t(rng() % 8) - 2)] = int32_t(rng() % 5) - 1;
            if (rng() % 3 == 0) pw::FillRefsFor(w->mMap50[3], rng, 18);
            if (rng() % 3 == 0) pw::FillRefsFor(w->mWants[4], rng, 10);
        },
        [&](bool o, W *w, std::mt19937 &rng) -> uint64_t {
            for (int16_t sim : {int16_t(3), int16_t(0), int16_t(4), int16_t(3), int16_t(-2), int16_t(4)}) {
                const uint32_t pick = rng() % 9;
                const int32_t priority = pick < 7 ? int32_t(pick) - 2 : int32_t(rng());
                const uint32_t arg = pw::Dirty(rng, sim);
                wantshape::Call<Fn>(ctx, w, 0x494870, 0, WANT_NA(QueueSimUpdate), arg, priority);
            }
            return 0;
        });
}

// Only the idle case can run here (RunUpdate is not isolated): nothing queued, no update in progress. The function is
// verified by byte match; this case checks the hook's calling convention and that the idle case returns at once.
DIFF_TEST(WantSimulator_FlushUpdates, 0x48d1e0)
{
    using Fn = void(__thiscall *)(W *);
    pw::Compare(ctx, "FlushUpdates", 20, [](W *w, std::mt19937 &) { w->mUpdateState = 0; }, [&](bool o, W *w, std::mt19937 &) -> uint64_t {
        wantshape::Call<Fn>(ctx, w, 0x48d1e0, 0, WANT_ADAPTER(FlushUpdates));
        return 0;
    });
}
