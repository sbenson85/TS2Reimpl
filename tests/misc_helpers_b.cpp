// Differential tests for the helpers in src/rz/MiscHelpersB.h.
//
// The service helpers (0x1c02bf, 0x9c5baa) and the walk (0x1c081e, which gets its object from the
// service when it is given none) use the service object in game global 0x10c9378. The tests point
// it at a fake whose slot 5 hands out a fake held object, or at null. Every fake vtable slot records
// its call (object, arguments), so the call sequences are compared along with the results.
//
// The walk's map is an std::map<uint32_t, uint32_t> built with random keys (the MSVC node layout
// the original reads: key at +0x10). Slot 6 of the object it calls records (object, arg, key).
#include "difftest.h"
#include "rz/MiscHelpersB.h"

#include <cstring>
#include <map>
#include <random>
#include <vector>

namespace {

using HolderFn = void *(__thiscall *)(void *);
using WalkFn = void(__thiscall *)(void *, uint32_t, void *);
using CachedFn = void *(__cdecl *)();

struct Call {
    int slot;
    const void *self;
    uint32_t a, b;
    const void *wrote;
};

std::vector<Call> gCalls;

struct FakeObject {
    void *vtable;
    uint32_t pad;
};

void *gServiceVTable[8];
void *gHeldVTable[8];
void *gTargetVTable[8];
FakeObject gService{}, gHeld{}, gTarget{};

// Fakes are __thiscall: `this` in ecx, the stack arguments popped by the callee. A __fastcall
// function with a dummy edx takes the same registers and stack layout.
uint32_t __fastcall RecordQuery(void *self, void * /*edx*/, uint32_t first, uint32_t second, void **out)
{
    gCalls.push_back({5, self, first, second, &gHeld});
    *out = &gHeld;
    return 0;
}

uint32_t __fastcall RecordRelease(void *self, void * /*edx*/)
{
    gCalls.push_back({2, self, 0, 0, nullptr});
    return 0;
}

uint32_t __fastcall RecordSlot6(void *self, void * /*edx*/, uint32_t arg, uint32_t key)
{
    gCalls.push_back({6, self, arg, key, nullptr});
    return 0;
}

void SetupFakes()
{
    std::memset(gServiceVTable, 0, sizeof(gServiceVTable));
    std::memset(gHeldVTable, 0, sizeof(gHeldVTable));
    std::memset(gTargetVTable, 0, sizeof(gTargetVTable));
    gServiceVTable[5] = reinterpret_cast<void *>(&RecordQuery);
    gHeldVTable[2] = reinterpret_cast<void *>(&RecordRelease);
    gHeldVTable[6] = reinterpret_cast<void *>(&RecordSlot6);
    gTargetVTable[6] = reinterpret_cast<void *>(&RecordSlot6);
    gService.vtable = gServiceVTable;
    gHeld.vtable = gHeldVTable;
    gTarget.vtable = gTargetVTable;
}

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const Call &x = a[i], &y = b[i];
        if (x.slot != y.slot || x.self != y.self || x.a != y.a || x.b != y.b || x.wrote != y.wrote) return false;
    }
    return true;
}

void SetService(difftest::Context &ctx, void *service)
{
    *ctx.image.At<void *>(0x10c9378) = service;
}

// The cache global of 0x9c5baa (RVA 0x10f51b0).
void SetCache(difftest::Context &ctx, void *value)
{
    *ctx.image.At<void *>(0x10f51b0) = value;
}

void *GetCache(difftest::Context &ctx)
{
    return *ctx.image.At<void *>(0x10f51b0);
}

// Builds a map of `count` random distinct keys. The same generator state gives the same map.
void FillMap(std::map<uint32_t, uint32_t> &map, std::mt19937 &gen, int count)
{
    while (static_cast<int>(map.size()) < count) map.emplace(gen() % 100000u, gen());
}

} // namespace

DIFF_TEST(ServiceHolder_QueryFromService, 0x1c02bf)
{
    SetupFakes();
    auto orig = ctx.Original<HolderFn>(0x1c02bf);
    std::mt19937 gen(4242);
    for (int i = 0; i < 200; ++i) {
        // A stale object in the holder: the original clears it before asking the service.
        void *stale = i % 3 == 0 ? nullptr : reinterpret_cast<void *>(static_cast<uintptr_t>(gen()));
        bool withService = i % 2 == 0;

        ServiceHolder holderA{stale}, holderB{stale};
        SetService(ctx, withService ? &gService : nullptr);
        gCalls.clear();
        void *retA = orig(&holderA);
        std::vector<Call> callsA = gCalls;

        SetService(ctx, withService ? &gService : nullptr);
        gCalls.clear();
        void *retB = holderB.QueryFromService();
        ++ctx.cases;
        if (retA != &holderA || retB != &holderB)
            ctx.Fail("QueryFromService (case %d) returned %p / %p, expected this", i, retA, retB);
        if (holderA.mObject != holderB.mObject || !SameCalls(callsA, gCalls))
            ctx.Fail("QueryFromService (case %d): holder %p / %p, %zu / %zu calls", i, holderA.mObject,
                     holderB.mObject, callsA.size(), gCalls.size());
    }
    SetService(ctx, nullptr);
}

DIFF_TEST(KeyMapWalker_CallSlot6ForEachKey, 0x1c081e)
{
    SetupFakes();
    auto orig = ctx.Original<WalkFn>(0x1c081e);
    std::mt19937 gen(9001);
    for (int i = 0; i < 400; ++i) {
        KeyMapWalker walker;
        int count = i % 7 == 0 ? 0 : static_cast<int>(gen() % 40u);
        FillMap(walker.mMap, gen, count);
        uint32_t arg = i % 11 == 0 ? 0 : gen();

        // Object cases: an object given; none with a service that hands out gHeld; none with no service.
        int mode = i % 3;
        void *obj = mode == 0 ? static_cast<void *>(&gTarget) : nullptr;
        SetService(ctx, mode == 1 ? &gService : nullptr);

        gCalls.clear();
        orig(&walker, arg, obj);
        std::vector<Call> callsA = gCalls;

        SetService(ctx, mode == 1 ? &gService : nullptr);
        gCalls.clear();
        walker.CallSlot6ForEachKey(arg, obj);
        ++ctx.cases;
        if (!SameCalls(callsA, gCalls))
            ctx.Fail("CallSlot6ForEachKey (case %d, %d keys, arg 0x%08x, mode %d): original %zu calls, ours %zu", i,
                     count, arg, mode, callsA.size(), gCalls.size());
    }
    SetService(ctx, nullptr);
}

DIFF_TEST(GetCachedServiceObjectB, 0x9c5baa)
{
    SetupFakes();
    auto orig = ctx.Original<CachedFn>(0x9c5baa);

    struct Case {
        bool service;
        void *cache; // the global before the call
        bool repeat; // call a second time without touching the global
    };
    const Case cases[] = {
        {false, nullptr, false}, // no service: nothing is cached
        {true, nullptr, true},   // queried, cached, released; the second call hits the cache
        {true, &gTarget, false}, // already cached: no query at all
        {false, &gTarget, true}, // cached garbage returned as is
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const Case &c = cases[i];

        SetService(ctx, c.service ? &gService : nullptr);
        SetCache(ctx, c.cache);
        gCalls.clear();
        void *retA = orig();
        void *cacheA = GetCache(ctx);
        std::vector<Call> callsA = gCalls;
        if (c.repeat) {
            gCalls.clear();
            retA = orig();
            callsA.insert(callsA.end(), gCalls.begin(), gCalls.end());
        }

        SetService(ctx, c.service ? &gService : nullptr);
        SetCache(ctx, c.cache);
        gCalls.clear();
        void *retB = GetCachedServiceObjectB();
        void *cacheB = GetCache(ctx);
        std::vector<Call> callsB = gCalls;
        if (c.repeat) {
            gCalls.clear();
            retB = GetCachedServiceObjectB();
            callsB.insert(callsB.end(), gCalls.begin(), gCalls.end());
        }
        ++ctx.cases;
        if (retA != retB || cacheA != cacheB || !SameCalls(callsA, callsB))
            ctx.Fail("GetCachedServiceObjectB (case %zu): returned %p / %p, global %p / %p, %zu / %zu calls", i, retA,
                     retB, cacheA, cacheB, callsA.size(), callsB.size());
    }
    SetService(ctx, nullptr);
    SetCache(ctx, nullptr);
}
