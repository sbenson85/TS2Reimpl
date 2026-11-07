// Differential tests for the helpers in src/misc/UtilitiesC.h.
//
// The service queries (0x20dbd8, 0x2365e3) and the cached getters (0x2366b8, 0x9c5bd6) use the service
// object in game global 0x10c9378. The tests point it at a fake whose slot 5 hands out a fake held
// object, or at null. Each fake vtable slot records its call (slot, object, arguments), so the call
// sequences are compared along with the results and the holders.
//
// The walk (0x1c0458) walks an std::map<uint32_t, uint32_t> built with random keys (the MSVC node
// layout the original reads: key at +0x10). Slot 5 of the object it calls records (object, arg, key).
#include "difftest.h"
#include "misc/UtilitiesC.h"

#include <cstring>
#include <limits>
#include <map>
#include <random>
#include <vector>

namespace {

using PtrFn = void *(__thiscall *)(void *);
using PtrArgFn = void *(__thiscall *)(void *, uint32_t);
using WalkFn = void(__thiscall *)(void *, uint32_t, void *);
using CachedFn = void *(__cdecl *)();
using TransformFn = void(__thiscall *)(void *, float *, const float *);

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

uint32_t __fastcall RecordWalk(void *self, void * /*edx*/, uint32_t arg, uint32_t key)
{
    gCalls.push_back({7, self, arg, key, nullptr});
    return 0;
}

void SetupFakes()
{
    std::memset(gServiceVTable, 0, sizeof(gServiceVTable));
    std::memset(gHeldVTable, 0, sizeof(gHeldVTable));
    std::memset(gTargetVTable, 0, sizeof(gTargetVTable));
    gServiceVTable[5] = reinterpret_cast<void *>(&RecordQuery);
    gHeldVTable[2] = reinterpret_cast<void *>(&RecordRelease);
    gHeldVTable[5] = reinterpret_cast<void *>(&RecordWalk);
    gTargetVTable[5] = reinterpret_cast<void *>(&RecordWalk);
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

void SetGlobal(difftest::Context &ctx, uint32_t rva, void *value)
{
    *ctx.image.At<void *>(rva) = value;
}

void *GetGlobal(difftest::Context &ctx, uint32_t rva)
{
    return *ctx.image.At<void *>(rva);
}

// Builds a map of `count` random distinct keys. The same generator state gives the same map.
void FillMap(std::map<uint32_t, uint32_t> &map, std::mt19937 &gen, int count)
{
    while (static_cast<int>(map.size()) < count) map.emplace(gen() % 100000u, gen());
}

} // namespace

DIFF_TEST(UtilObjectA_Construct, 0x1b9035)
{
    SetupFakes();
    auto orig = ctx.Original<PtrFn>(0x1b9035);
    std::mt19937 gen(11);
    for (int i = 0; i < 64; ++i) {
        // Same garbage in both objects: the function writes only the vtable and the dword.
        uint32_t garbage[2] = {gen(), gen()};
        UtilObjectA a{nullptr, 0}, b{nullptr, 0};
        std::memcpy(&a, garbage, sizeof(a));
        std::memcpy(&b, garbage, sizeof(b));
        void *retA = orig(&a);
        void *retB = b.Construct();
        ++ctx.cases;
        if (retA != &a || retB != &b)
            ctx.Fail("UtilObjectA::Construct (case %d) returned %p / %p, expected this", i, retA, retB);
        if (std::memcmp(&a, &b, sizeof(a)) != 0)
            ctx.Fail("UtilObjectA::Construct (case %d): vtable %p / %p, value %u / %u", i, a.mVTable, b.mVTable,
                     a.mValue, b.mValue);
    }
}

DIFF_TEST(UtilObjectB_Construct, 0x9c7b3d)
{
    SetupFakes();
    auto orig = ctx.Original<PtrArgFn>(0x9c7b3d);
    std::mt19937 gen(12);
    for (int i = 0; i < 64; ++i) {
        // The 0x14 bytes are filled with the same garbage in both (the function leaves no padding
        // untouched, but the garbage shows any field it does not write).
        uint8_t garbage[0x14];
        for (auto &byte : garbage) byte = static_cast<uint8_t>(gen());
        UtilObjectB a{}, b{};
        std::memcpy(&a, garbage, sizeof(a));
        std::memcpy(&b, garbage, sizeof(b));
        uint32_t arg = i % 5 == 0 ? 0 : gen();
        void *retA = orig(&a, arg);
        void *retB = b.Construct(arg);
        ++ctx.cases;
        if (retA != &a || retB != &b)
            ctx.Fail("UtilObjectB::Construct (case %d) returned %p / %p, expected this", i, retA, retB);
        if (std::memcmp(&a, &b, sizeof(a)) != 0)
            ctx.Fail("UtilObjectB::Construct (case %d, arg 0x%08x): fields differ", i, arg);
    }
}

DIFF_TEST(ServiceQueryHolder_QueryWithIdsA, 0x20dbd8)
{
    SetupFakes();
    auto orig = ctx.Original<PtrFn>(0x20dbd8);
    std::mt19937 gen(13);
    for (int i = 0; i < 100; ++i) {
        void *stale = i % 3 == 0 ? nullptr : reinterpret_cast<void *>(static_cast<uintptr_t>(gen()));
        bool withService = i % 2 == 0;

        ServiceQueryHolder holderA{stale}, holderB{stale};
        SetService(ctx, withService ? &gService : nullptr);
        gCalls.clear();
        void *retA = orig(&holderA);
        std::vector<Call> callsA = gCalls;

        SetService(ctx, withService ? &gService : nullptr);
        gCalls.clear();
        void *retB = holderB.QueryWithIdsA();
        ++ctx.cases;
        if (retA != &holderA || retB != &holderB)
            ctx.Fail("QueryWithIdsA (case %d) returned %p / %p, expected this", i, retA, retB);
        if (holderA.mObject != holderB.mObject || !SameCalls(callsA, gCalls))
            ctx.Fail("QueryWithIdsA (case %d): holder %p / %p, %zu / %zu calls", i, holderA.mObject, holderB.mObject,
                     callsA.size(), gCalls.size());
    }
    SetService(ctx, nullptr);
}

DIFF_TEST(ServiceQueryHolder_QueryWithIdsB, 0x2365e3)
{
    SetupFakes();
    auto orig = ctx.Original<PtrFn>(0x2365e3);
    std::mt19937 gen(14);
    for (int i = 0; i < 100; ++i) {
        void *stale = i % 3 == 0 ? nullptr : reinterpret_cast<void *>(static_cast<uintptr_t>(gen()));
        bool withService = i % 2 == 0;

        ServiceQueryHolder holderA{stale}, holderB{stale};
        SetService(ctx, withService ? &gService : nullptr);
        gCalls.clear();
        void *retA = orig(&holderA);
        std::vector<Call> callsA = gCalls;

        SetService(ctx, withService ? &gService : nullptr);
        gCalls.clear();
        void *retB = holderB.QueryWithIdsB();
        ++ctx.cases;
        if (retA != &holderA || retB != &holderB)
            ctx.Fail("QueryWithIdsB (case %d) returned %p / %p, expected this", i, retA, retB);
        if (holderA.mObject != holderB.mObject || !SameCalls(callsA, gCalls))
            ctx.Fail("QueryWithIdsB (case %d): holder %p / %p, %zu / %zu calls", i, holderA.mObject, holderB.mObject,
                     callsA.size(), gCalls.size());
    }
    SetService(ctx, nullptr);
}

namespace {

// Shared body of the two cached getter tests: the global is the cache, the object it holds is a
// fake target. Cases: no service, a query that caches and releases, an already cached value, and
// cached garbage. Each is run once more on a repeated call.
void CheckCachedGetter(difftest::Context &ctx, uint32_t cacheRva, CachedFn orig, void *(*ours)(), const char *name)
{
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
        SetGlobal(ctx, cacheRva, c.cache);
        gCalls.clear();
        void *retA = orig();
        void *cacheA = GetGlobal(ctx, cacheRva);
        std::vector<Call> callsA = gCalls;
        if (c.repeat) {
            gCalls.clear();
            retA = orig();
            callsA.insert(callsA.end(), gCalls.begin(), gCalls.end());
        }

        SetService(ctx, c.service ? &gService : nullptr);
        SetGlobal(ctx, cacheRva, c.cache);
        gCalls.clear();
        void *retB = ours();
        void *cacheB = GetGlobal(ctx, cacheRva);
        std::vector<Call> callsB = gCalls;
        if (c.repeat) {
            gCalls.clear();
            retB = ours();
            callsB.insert(callsB.end(), gCalls.begin(), gCalls.end());
        }
        ++ctx.cases;
        if (retA != retB || cacheA != cacheB || !SameCalls(callsA, callsB))
            ctx.Fail("%s (case %zu): returned %p / %p, global %p / %p, %zu / %zu calls", name, i, retA, retB, cacheA,
                     cacheB, callsA.size(), callsB.size());
    }
    SetService(ctx, nullptr);
    SetGlobal(ctx, cacheRva, nullptr);
}

} // namespace

DIFF_TEST(GetCachedServiceObjectC, 0x2366b8)
{
    SetupFakes();
    auto orig = ctx.Original<CachedFn>(0x2366b8);
    CheckCachedGetter(ctx, 0x10cc648, orig, &GetCachedServiceObjectC, "GetCachedServiceObjectC");
}

DIFF_TEST(GetCachedServiceObjectD, 0x9c5bd6)
{
    SetupFakes();
    auto orig = ctx.Original<CachedFn>(0x9c5bd6);
    CheckCachedGetter(ctx, 0x10f51ac, orig, &GetCachedServiceObjectD, "GetCachedServiceObjectD");
}

DIFF_TEST(KeyMapSlot5Walker_CallSlot5ForEachKey, 0x1c0458)
{
    SetupFakes();
    auto orig = ctx.Original<WalkFn>(0x1c0458);
    std::mt19937 gen(15);
    for (int i = 0; i < 400; ++i) {
        // Object cases: an object given; none with a service that hands out gHeld; none with no service.
        // The last one walks an empty map only: with a non-empty map the original calls through null
        // (ORIGINAL-BUG, see the replacement).
        int mode = i % 3;
        int count = mode == 2 || i % 7 == 0 ? 0 : static_cast<int>(gen() % 40u);
        uint32_t arg = gen();
        void *obj = mode == 0 ? static_cast<void *>(&gTarget) : nullptr;

        // The original and ours walk separate maps built from the same keys.
        KeyMapSlot5Walker walker, walkerB;
        std::mt19937 keys = gen;
        FillMap(walker.mMap, gen, count);
        FillMap(walkerB.mMap, keys, count);

        SetService(ctx, mode == 1 ? &gService : nullptr);
        gCalls.clear();
        orig(&walker, arg, obj);
        std::vector<Call> callsA = gCalls;

        SetService(ctx, mode == 1 ? &gService : nullptr);
        gCalls.clear();
        walkerB.CallSlot5ForEachKey(arg, obj);
        ++ctx.cases;
        if (!SameCalls(callsA, gCalls))
            ctx.Fail("CallSlot5ForEachKey (case %d, %d keys, arg 0x%08x, mode %d): original %zu calls, ours %zu", i,
                     count, arg, mode, callsA.size(), gCalls.size());
    }
    SetService(ctx, nullptr);
}

DIFF_TEST(AffineTransform3_TransformPoint, 0x19c820)
{
    SetupFakes();
    auto orig = ctx.Original<TransformFn>(0x19c820);
    std::mt19937 gen(16);
    std::uniform_real_distribution<float> dist(-1000.0f, 1000.0f);
    for (int i = 0; i < 2000; ++i) {
        AffineTransform3 t{};
        t.mUnknown0 = gen();
        for (float &m : t.mMatrix) m = dist(gen);
        for (float &o : t.mOffset) o = dist(gen);
        // A few cases with raw bit patterns (infinities, NaNs, denormals, signed zeros).
        if (i % 50 == 0) {
            uint32_t bits = gen();
            std::memcpy(&t.mMatrix[gen() % 9], &bits, sizeof(bits));
        }
        if (i % 97 == 0) t.mMatrix[1] = std::numeric_limits<float>::infinity();
        if (i % 89 == 0) t.mOffset[2] = -0.0f;
        float in[3] = {dist(gen), dist(gen), dist(gen)};
        if (i % 13 == 0) in[1] = std::numeric_limits<float>::quiet_NaN();

        // Separate outputs, plus an aliased case where out is in (every 4th case).
        AffineTransform3 tB = t;
        float outA[3] = {1.0f, 2.0f, 3.0f};
        float outB[3] = {1.0f, 2.0f, 3.0f};
        float inA[3], inB[3];
        std::memcpy(inA, in, sizeof(in));
        std::memcpy(inB, in, sizeof(in));
        bool alias = i % 4 == 0;
        if (alias) {
            orig(&t, inA, inA);
            tB.TransformPoint(inB, inB);
            std::memcpy(outA, inA, sizeof(outA));
            std::memcpy(outB, inB, sizeof(outB));
        } else {
            orig(&t, outA, inA);
            tB.TransformPoint(outB, inB);
        }
        ++ctx.cases;
        if (std::memcmp(outA, outB, sizeof(outA)) != 0)
            ctx.Fail("TransformPoint (case %d%s): (%a %a %a) / (%a %a %a)", i, alias ? ", aliased" : "", outA[0],
                     outA[1], outA[2], outB[0], outB[1], outB[2]);
    }
}
