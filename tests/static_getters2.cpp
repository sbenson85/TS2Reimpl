// Differential tests for the cached service objects (0x20dcb6, 0x20dd00, 0x20de51, 0x20df6f,
// 0x20dde0, 0xaeb820), the dispatch through the cached object (0xaeb800 and its thunk 0xa3c4b0)
// and the cell operations on it (0xaeb730, 0xaeb790, 0xaeb7e0, 0xaeb760).
//
// The service and the objects it hands out are fakes whose vtable slots record their calls
// (object, arguments, return value). Both sides run from the same game globals, which are reset
// before each run, and the calls, results and globals are compared.
//
// Layout of the fakes (as the original calls them):
//   service:        slot 5 (a, b, out)          acquires the object through the output pointer
//   acquired obj:   slot 2 (release), slot 8 (no arguments: the cached object), slot 23 (none)
//   cached obj:     slot 2, slot 5 (a, b), slot 6 (a), slot 7 (a), slot 8 (a)
// Slots 8 and 23 are called with no argument on the acquired object; slot 8 of the cached object
// takes one argument. The fakes are __fastcall with a dummy edx, so their callee-cleans match the
// __thiscall callers.
//
// Exceptions: the frame of 0xaeb820 releases the acquired reference when slot 8 throws
// (ScopedRef::ReleaseObject, its unwind action); DIFF_TEST StaticGetters2_ObjectThrows checks it.
#include "difftest.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters2.h"

#include <cstring>
#include <utility>
#include <vector>

namespace {

struct Event {
    int slot;
    const void *self;
    uint32_t a, b;
    uintptr_t ret;
};

std::vector<Event> gEvents;
void *gServiceResult = nullptr;   // what the service's slot 5 stores through its output pointer
uint32_t gSlot5Result = 0;        // what the cached object's slot 5 returns
uint32_t gCachedSlot8Result = 0;  // what the cached object's slot 8 returns
void *gAcquiredSlot8Result = nullptr; // what the acquired object's slot 8 returns (the cached object)
void *gSlot23Result = nullptr;    // what the acquired object's slot 23 returns

// Game globals the code reads or writes.
constexpr uint32_t kService = 0x10c9378;      // GetServiceObject
constexpr uint32_t kSingletonA = 0x10cc288;   // GetCachedSingleton
constexpr uint32_t kCacheB = 0x10cc28c;       // GetCachedSingletonB
constexpr uint32_t kCacheC = 0x10cc290;       // GetCachedSingletonC
constexpr uint32_t kCacheSlot23 = 0x10cc2a8;  // GetCachedSlot23
constexpr uint32_t kCacheObject = 0x10f9070;  // GetCachedObjectB

struct Globals {
    void *service, *singleton, *cacheB, *cacheC, *cacheSlot23, *cacheObject;
};

void Apply(difftest::Context &ctx, const Globals &g)
{
    *ctx.image.At<void *>(kService) = g.service;
    *ctx.image.At<void *>(kSingletonA) = g.singleton;
    *ctx.image.At<void *>(kCacheB) = g.cacheB;
    *ctx.image.At<void *>(kCacheC) = g.cacheC;
    *ctx.image.At<void *>(kCacheSlot23) = g.cacheSlot23;
    *ctx.image.At<void *>(kCacheObject) = g.cacheObject;
}

void Snapshot(difftest::Context &ctx, Globals &g)
{
    g.service = *ctx.image.At<void *>(kService);
    g.singleton = *ctx.image.At<void *>(kSingletonA);
    g.cacheB = *ctx.image.At<void *>(kCacheB);
    g.cacheC = *ctx.image.At<void *>(kCacheC);
    g.cacheSlot23 = *ctx.image.At<void *>(kCacheSlot23);
    g.cacheObject = *ctx.image.At<void *>(kCacheObject);
}

bool SameGlobals(const Globals &a, const Globals &b)
{
    return a.service == b.service && a.singleton == b.singleton && a.cacheB == b.cacheB && a.cacheC == b.cacheC &&
           a.cacheSlot23 == b.cacheSlot23 && a.cacheObject == b.cacheObject;
}

void *gVtService[8];
void *gVtAcquired[24];
void *gVtCached[9];
void *gVtThrowing[9];

uint32_t __fastcall FakeRelease(void *self, void *)
{
    gEvents.push_back({2, self, 0, 0, 0});
    return 0;
}

uint32_t __fastcall FakeServiceQuery(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    gEvents.push_back({5, self, a, b, reinterpret_cast<uintptr_t>(gServiceResult)});
    *out = gServiceResult;
    return 0;
}

void *__fastcall FakeAcquiredSlot8(void *self, void *)
{
    gEvents.push_back({8, self, 0, 0, reinterpret_cast<uintptr_t>(gAcquiredSlot8Result)});
    return gAcquiredSlot8Result;
}

void *__fastcall FakeAcquiredSlot23(void *self, void *)
{
    gEvents.push_back({23, self, 0, 0, reinterpret_cast<uintptr_t>(gSlot23Result)});
    return gSlot23Result;
}

void *__fastcall ThrowingSlot8(void *self, void *)
{
    gEvents.push_back({8, self, 0, 0, 0});
    throw 42;
}

uint32_t __fastcall CachedSlot5(void *self, void *, uint32_t a, uint32_t b)
{
    gEvents.push_back({5, self, a, b, gSlot5Result});
    return gSlot5Result;
}

uint32_t __fastcall CachedSlot6(void *self, void *, uint32_t a)
{
    gEvents.push_back({6, self, a, 0, 0});
    return 0x66;
}

uint32_t __fastcall CachedSlot7(void *self, void *, uint32_t a)
{
    gEvents.push_back({7, self, a, 0, 0});
    return 0x77;
}

uint32_t __fastcall CachedSlot8(void *self, void *, uint32_t a)
{
    gEvents.push_back({8, self, a, 0, gCachedSlot8Result});
    return gCachedSlot8Result;
}

void SetupFakes()
{
    std::memset(gVtService, 0, sizeof(gVtService));
    std::memset(gVtAcquired, 0, sizeof(gVtAcquired));
    std::memset(gVtCached, 0, sizeof(gVtCached));
    std::memset(gVtThrowing, 0, sizeof(gVtThrowing));
    gVtService[5] = reinterpret_cast<void *>(&FakeServiceQuery);
    gVtAcquired[2] = reinterpret_cast<void *>(&FakeRelease);
    gVtAcquired[8] = reinterpret_cast<void *>(&FakeAcquiredSlot8);
    gVtAcquired[23] = reinterpret_cast<void *>(&FakeAcquiredSlot23);
    gVtCached[2] = reinterpret_cast<void *>(&FakeRelease);
    gVtCached[5] = reinterpret_cast<void *>(&CachedSlot5);
    gVtCached[6] = reinterpret_cast<void *>(&CachedSlot6);
    gVtCached[7] = reinterpret_cast<void *>(&CachedSlot7);
    gVtCached[8] = reinterpret_cast<void *>(&CachedSlot8);
    gVtThrowing[2] = reinterpret_cast<void *>(&FakeRelease);
    gVtThrowing[8] = reinterpret_cast<void *>(&ThrowingSlot8);
}

// Fake objects: the first word of each is its vtable. Service and acquired objects are separate
// so that a service's slot 5 and an object's slot 5 are not confused.
void *gService[1] = {gVtService};
void *gAcquired[1] = {gVtAcquired};
void *gCachedObject[1] = {gVtCached};
void *gThrowing[1] = {gVtThrowing};
void *gHeld[1] = {gVtAcquired};
void *gOtherCached[1] = {gVtCached};

void ResetRun(void *serviceResult)
{
    gEvents.clear();
    gServiceResult = serviceResult;
}

bool SameEvents(const std::vector<Event> &a, const std::vector<Event> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const Event &x = a[i], &y = b[i];
        if (x.slot != y.slot || x.self != y.self || x.a != y.a || x.b != y.b || x.ret != y.ret) return false;
    }
    return true;
}

// A cache global that is empty.
void *const kEmpty = nullptr;

} // namespace

DIFF_TEST(StaticGetters2_AcquireB, 0x20dcb6)
{
    SetupFakes();
    auto orig = ctx.Original<ScopedRef *(__thiscall *)(void *)>(0x20dcb6);
    void *priors[] = {nullptr, gHeld};
    for (void *prior : priors)
        for (void *service : {static_cast<void *>(nullptr), static_cast<void *>(gService)})
            for (void *result : {static_cast<void *>(nullptr), static_cast<void *>(gAcquired)}) {
                Globals g{service, nullptr, nullptr, nullptr, nullptr, nullptr};
                ScopedRef a{prior}, b{prior};
                Apply(ctx, g);
                ResetRun(result);
                ScopedRef *ra = orig(&a);
                std::vector<Event> eventsA = gEvents;

                Apply(ctx, g);
                ResetRun(result);
                ScopedRef *rb = AcquireServiceRefB(&b);
                ++ctx.cases;
                if (ra != &a || rb != &b || a.mObject != b.mObject || !SameEvents(eventsA, gEvents))
                    ctx.Fail("AcquireB prior=%p service=%p result=%p: original (%p, %p, %zu events), ours (%p, %p, %zu events)", prior,
                             service, result, a.mObject, ra, eventsA.size(), b.mObject, rb, gEvents.size());
            }
    Apply(ctx, Globals{});
}

DIFF_TEST(StaticGetters2_AcquireC, 0x20dd00)
{
    SetupFakes();
    auto orig = ctx.Original<ScopedRef *(__thiscall *)(void *)>(0x20dd00);
    void *priors[] = {nullptr, gHeld};
    for (void *prior : priors)
        for (void *service : {static_cast<void *>(nullptr), static_cast<void *>(gService)})
            for (void *result : {static_cast<void *>(nullptr), static_cast<void *>(gAcquired)}) {
                Globals g{service, nullptr, nullptr, nullptr, nullptr, nullptr};
                ScopedRef a{prior}, b{prior};
                Apply(ctx, g);
                ResetRun(result);
                ScopedRef *ra = orig(&a);
                std::vector<Event> eventsA = gEvents;

                Apply(ctx, g);
                ResetRun(result);
                ScopedRef *rb = AcquireServiceRefC(&b);
                ++ctx.cases;
                if (ra != &a || rb != &b || a.mObject != b.mObject || !SameEvents(eventsA, gEvents))
                    ctx.Fail("AcquireC prior=%p service=%p result=%p: original (%p, %p, %zu events), ours (%p, %p, %zu events)", prior,
                             service, result, a.mObject, ra, eventsA.size(), b.mObject, rb, gEvents.size());
            }
    Apply(ctx, Globals{});
}

namespace {

// Shared body for the two cached singletons (0x20de51 with cache 0x10cc28c, 0x20df6f with 0x10cc290).
template <typename Get>
void CheckCachedSingleton(difftest::Context &ctx, uint32_t rva, Get ours, bool useB)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__cdecl *)()>(rva);
    for (void *cacheIn : {kEmpty, static_cast<void *>(gCachedObject)})
        for (void *service : {static_cast<void *>(nullptr), static_cast<void *>(gService)})
            for (void *result : {static_cast<void *>(nullptr), static_cast<void *>(gAcquired)}) {
                Globals g{service, nullptr, nullptr, nullptr, nullptr, nullptr};
                (useB ? g.cacheB : g.cacheC) = cacheIn;
                Apply(ctx, g);
                ResetRun(result);
                void *a = orig();
                std::vector<Event> eventsA = gEvents;
                Globals afterA{};
                Snapshot(ctx, afterA);

                Apply(ctx, g);
                ResetRun(result);
                void *b = ours();
                Globals afterB{};
                Snapshot(ctx, afterB);
                ++ctx.cases;
                if (a != b || !SameGlobals(afterA, afterB) || !SameEvents(eventsA, gEvents))
                    ctx.Fail("cached singleton %08x cache=%p service=%p result=%p: original %p (%zu events), ours %p (%zu events)", rva,
                             cacheIn, service, result, a, eventsA.size(), b, gEvents.size());
            }
    Apply(ctx, Globals{});
}

} // namespace

DIFF_TEST(StaticGetters2_CachedB, 0x20de51)
{
    CheckCachedSingleton(ctx, 0x20de51, &GetCachedSingletonB, true);
}

DIFF_TEST(StaticGetters2_CachedC, 0x20df6f)
{
    CheckCachedSingleton(ctx, 0x20df6f, &GetCachedSingletonC, false);
}

DIFF_TEST(StaticGetters2_Slot23, 0x20dde0)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__cdecl *)()>(0x20dde0);
    for (void *cacheIn : {kEmpty, static_cast<void *>(gOtherCached)})
        for (void *singleton : {kEmpty, static_cast<void *>(gAcquired)})
            for (void *service : {static_cast<void *>(nullptr), static_cast<void *>(gService)})
                for (void *slot23 : {static_cast<void *>(nullptr), static_cast<void *>(gCachedObject)}) {
                    // Nothing to call slot 23 on: no singleton and no service.
                    if (singleton == nullptr && service == nullptr && cacheIn == nullptr) continue;
                    Globals g{service, singleton, nullptr, nullptr, cacheIn, nullptr};
                    Apply(ctx, g);
                    ResetRun(gAcquired);
                    gSlot23Result = slot23;
                    void *a = orig();
                    std::vector<Event> eventsA = gEvents;
                    Globals afterA{};
                    Snapshot(ctx, afterA);

                    Apply(ctx, g);
                    ResetRun(gAcquired);
                    gSlot23Result = slot23;
                    void *b = GetCachedSlot23();
                    Globals afterB{};
                    Snapshot(ctx, afterB);
                    ++ctx.cases;
                    if (a != b || !SameGlobals(afterA, afterB) || !SameEvents(eventsA, gEvents))
                        ctx.Fail("Slot23 cache=%p singleton=%p service=%p slot23=%p: original %p (%zu events), ours %p (%zu events)",
                                 cacheIn, singleton, service, slot23, a, eventsA.size(), b, gEvents.size());
                }
    gSlot23Result = nullptr;
    Apply(ctx, Globals{});
}

DIFF_TEST(StaticGetters2_CachedObject, 0xaeb820)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__cdecl *)()>(0xaeb820);
    for (void *cacheIn : {kEmpty, static_cast<void *>(gCachedObject)})
        for (void *cached8 : {static_cast<void *>(gCachedObject), static_cast<void *>(gOtherCached)})
            for (uint32_t slot8 : {0u, 0xdeadbeefu}) {
                // A null service crashes in the original (no check before slot 5's object is used),
                // so only the non-null service is run.
                Globals g{gService, nullptr, nullptr, nullptr, nullptr, cacheIn};
                Apply(ctx, g);
                ResetRun(gAcquired);
                gAcquiredSlot8Result = cached8;
                gCachedSlot8Result = slot8;
                void *a = orig();
                std::vector<Event> eventsA = gEvents;
                Globals afterA{};
                Snapshot(ctx, afterA);

                Apply(ctx, g);
                ResetRun(gAcquired);
                gAcquiredSlot8Result = cached8;
                gCachedSlot8Result = slot8;
                void *b = GetCachedObjectB();
                Globals afterB{};
                Snapshot(ctx, afterB);
                ++ctx.cases;
                if (a != b || !SameGlobals(afterA, afterB) || !SameEvents(eventsA, gEvents))
                    ctx.Fail("CachedObject cache=%p cached8=%p slot8=%08x: original %p (%zu events), ours %p (%zu events)", cacheIn, cached8,
                             slot8, a, eventsA.size(), b, gEvents.size());
            }
    gAcquiredSlot8Result = nullptr;
    gCachedSlot8Result = 0;
    Apply(ctx, Globals{});
}

// Slot 8 throws: the original's frame releases the acquired reference during unwinding (its
// unwind action), and the exception leaves the call. The cache stays empty.
DIFF_TEST(StaticGetters2_ObjectThrows, 0xaeb820)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__cdecl *)()>(0xaeb820);
    gAcquiredSlot8Result = nullptr;

    Globals g{gService, nullptr, nullptr, nullptr, nullptr, nullptr};
    Apply(ctx, g);
    ResetRun(gThrowing);
    bool thrownA = false;
    try {
        orig();
    } catch (int) {
        thrownA = true;
    }
    std::vector<Event> eventsA = gEvents;
    Globals afterA{};
    Snapshot(ctx, afterA);

    Apply(ctx, g);
    ResetRun(gThrowing);
    bool thrownB = false;
    try {
        GetCachedObjectB();
    } catch (int) {
        thrownB = true;
    }
    Globals afterB{};
    Snapshot(ctx, afterB);
    ++ctx.cases;
    if (!thrownA || !thrownB || !SameGlobals(afterA, afterB) || !SameEvents(eventsA, gEvents))
        ctx.Fail("CachedObject throwing slot 8: original thrown=%d (%zu events), ours thrown=%d (%zu events)", thrownA, eventsA.size(),
                 thrownB, gEvents.size());
    Apply(ctx, Globals{});
}

DIFF_TEST(StaticGetters2_Dispatch8, 0xaeb800)
{
    SetupFakes();
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0xaeb800);
    for (void *cacheIn : {kEmpty, static_cast<void *>(gCachedObject)})
        for (uint32_t word : {0u, 0x55aa1234u})
            for (uint32_t slot8 : {0u, 0xfffffffeu}) {
                Globals g{gService, nullptr, nullptr, nullptr, nullptr, cacheIn};
                uint32_t cellA = word, cellB = word;
                Apply(ctx, g);
                ResetRun(gAcquired);
                gAcquiredSlot8Result = gCachedObject;
                gCachedSlot8Result = slot8;
                uint32_t a = orig(&cellA);
                std::vector<Event> eventsA = gEvents;

                Apply(ctx, g);
                ResetRun(gAcquired);
                gAcquiredSlot8Result = gCachedObject;
                gCachedSlot8Result = slot8;
                uint32_t b = Dispatch8(&cellB);
                ++ctx.cases;
                if (a != b || cellA != cellB || !SameEvents(eventsA, gEvents))
                    ctx.Fail("Dispatch8 cache=%p word=%08x slot8=%08x: original %08x (%zu events), ours %08x (%zu events)", cacheIn, word,
                             slot8, a, eventsA.size(), b, gEvents.size());
            }
    gAcquiredSlot8Result = nullptr;
    gCachedSlot8Result = 0;
    Apply(ctx, Globals{});
}

// The thunk takes the cell at +0x10 of the object it is called with, so the dword read is at
// this - 0x10. The test passes the address of a dword at +0x10 of a buffer.
DIFF_TEST(StaticGetters2_Dispatch8Thunk, 0xa3c4b0)
{
    SetupFakes();
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0xa3c4b0);
    for (uint32_t word : {0u, 0x12345678u}) {
        for (uint32_t slot8 : {0u, 0x80000001u}) {
            uint32_t bufferA[8] = {word, 0, 0, 0, 0, 0, 0, 0};
            uint32_t bufferB[8] = {word, 0, 0, 0, 0, 0, 0, 0};
            Globals g{gService, nullptr, nullptr, nullptr, nullptr, gCachedObject};
            Apply(ctx, g);
            ResetRun(gAcquired);
            gCachedSlot8Result = slot8;
            uint32_t a = orig(&bufferA[4]);
            std::vector<Event> eventsA = gEvents;

            Apply(ctx, g);
            ResetRun(gAcquired);
            gCachedSlot8Result = slot8;
            uint32_t b = Dispatch8Adjusted(&bufferB[4]);
            ++ctx.cases;
            if (a != b || !SameEvents(eventsA, gEvents))
                ctx.Fail("Dispatch8Adjusted word=%08x slot8=%08x: original %08x (%zu events), ours %08x (%zu events)", word, slot8, a,
                         eventsA.size(), b, gEvents.size());
        }
    }
    gCachedSlot8Result = 0;
    Apply(ctx, Globals{});
}

namespace {

// Runs a cell operation on both sides from the same starting state and compares the cell,
// the calls and the globals.
template <typename RunOriginal, typename RunOurs>
void CheckCell(difftest::Context &ctx, uint32_t cellStart, void *cacheIn, RunOriginal runOriginal, RunOurs runOurs, const char *name)
{
    Globals g{gService, nullptr, nullptr, nullptr, nullptr, cacheIn};
    uint32_t cellA = cellStart, cellB = cellStart;
    Apply(ctx, g);
    ResetRun(gAcquired);
    gCachedSlot8Result = 0;
    gAcquiredSlot8Result = gCachedObject;
    runOriginal(&cellA);
    std::vector<Event> eventsA = gEvents;
    Globals afterA{};
    Snapshot(ctx, afterA);

    Apply(ctx, g);
    ResetRun(gAcquired);
    gCachedSlot8Result = 0;
    gAcquiredSlot8Result = gCachedObject;
    runOurs(&cellB);
    Globals afterB{};
    Snapshot(ctx, afterB);
    ++ctx.cases;
    if (cellA != cellB || !SameGlobals(afterA, afterB) || !SameEvents(eventsA, gEvents))
        ctx.Fail("%s cell=%08x cache=%p: original cell %08x (%zu events), ours cell %08x (%zu events)", name, cellStart, cacheIn, cellA,
                 eventsA.size(), cellB, gEvents.size());
}

} // namespace

DIFF_TEST(StaticGetters2_CellReset, 0xaeb7e0)
{
    SetupFakes();
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xaeb7e0);
    auto ours = [](void *cell) { static_cast<ServiceCell *>(cell)->Reset(); };
    for (uint32_t cellStart : {0xffffffffu, 5u, 0u})
        for (void *cacheIn : {kEmpty, static_cast<void *>(gCachedObject)})
            CheckCell(ctx, cellStart, cacheIn, orig, ours, "Reset");
    Apply(ctx, Globals{});
}

DIFF_TEST(StaticGetters2_CellClose, 0xaeb760)
{
    SetupFakes();
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xaeb760);
    auto ours = [](void *cell) { static_cast<ServiceCell *>(cell)->Close(); };
    for (uint32_t cellStart : {0xffffffffu, 5u})
        for (void *cacheIn : {kEmpty, static_cast<void *>(gCachedObject)})
            CheckCell(ctx, cellStart, cacheIn, orig, ours, "Close");
    Apply(ctx, Globals{});
}

DIFF_TEST(StaticGetters2_CellOpen, 0xaeb730)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t, uint32_t)>(0xaeb730);
    for (uint32_t cellStart : {0x11223344u, 0xffffffffu})
        for (void *cacheIn : {kEmpty, static_cast<void *>(gCachedObject)})
            for (uint32_t slot5 : {0u, 0x7fffffffu})
                for (auto args : {std::pair<uint32_t, uint32_t>{1, 2}, std::pair<uint32_t, uint32_t>{0xdeadbeef, 0}}) {
                    Globals g{gService, nullptr, nullptr, nullptr, nullptr, cacheIn};
                    uint32_t cellA = cellStart, cellB = cellStart;
                    Apply(ctx, g);
                    ResetRun(gAcquired);
                    gAcquiredSlot8Result = gCachedObject;
                    gSlot5Result = slot5;
                    void *ra = orig(&cellA, args.first, args.second);
                    std::vector<Event> eventsA = gEvents;
                    Globals afterA{};
                    Snapshot(ctx, afterA);

                    Apply(ctx, g);
                    ResetRun(gAcquired);
                    gAcquiredSlot8Result = gCachedObject;
                    gSlot5Result = slot5;
                    void *rb = reinterpret_cast<ServiceCell *>(&cellB)->Open(args.first, args.second);
                    Globals afterB{};
                    Snapshot(ctx, afterB);
                    ++ctx.cases;
                    if (ra != &cellA || rb != &cellB || cellA != cellB || !SameGlobals(afterA, afterB) || !SameEvents(eventsA, gEvents))
                        ctx.Fail("Open cell=%08x cache=%p slot5=%08x args=%08x,%08x: original cell %08x (%zu events), ours %08x (%zu events)",
                                 cellStart, cacheIn, slot5, args.first, args.second, cellA, eventsA.size(), cellB, gEvents.size());
                }
    gSlot5Result = 0;
    Apply(ctx, Globals{});
}

DIFF_TEST(StaticGetters2_CellAssign, 0xaeb790)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__thiscall *)(void *, const uint32_t *)>(0xaeb790);
    for (uint32_t cellStart : {0xffffffffu, 9u})
        for (void *cacheIn : {kEmpty, static_cast<void *>(gCachedObject)})
            for (uint32_t value : {3u, 0x80000000u}) {
                Globals g{gService, nullptr, nullptr, nullptr, nullptr, cacheIn};
                uint32_t cellA = cellStart, cellB = cellStart;
                uint32_t srcA = value, srcB = value;
                Apply(ctx, g);
                ResetRun(gAcquired);
                gAcquiredSlot8Result = gCachedObject;
                void *ra = orig(&cellA, &srcA);
                std::vector<Event> eventsA = gEvents;
                Globals afterA{};
                Snapshot(ctx, afterA);

                Apply(ctx, g);
                ResetRun(gAcquired);
                gAcquiredSlot8Result = gCachedObject;
                void *rb = reinterpret_cast<ServiceCell *>(&cellB)->Assign(&srcB);
                Globals afterB{};
                Snapshot(ctx, afterB);
                ++ctx.cases;
                if (ra != &cellA || rb != &cellB || cellA != cellB || !SameGlobals(afterA, afterB) || !SameEvents(eventsA, gEvents))
                    ctx.Fail("Assign cell=%08x value=%08x cache=%p: original cell %08x (%zu events), ours %08x (%zu events)", cellStart,
                             value, cacheIn, cellA, eventsA.size(), cellB, gEvents.size());
            }
    Apply(ctx, Globals{});
}

namespace {

// Lane VS: the two constructors (0xaeb710 from a value, 0xaeb6f0 from another cell), from a garbage cell, compared as
// Assign is (the cell, the returned pointer, the globals, the calls).
template <class RunOriginal, class RunOurs> void CheckConstruct(difftest::Context &ctx, const char *name, RunOriginal original,
                                                                RunOurs ours)
{
    for (uint32_t cellStart : {0xcdcdcdcdu, 9u})
        for (void *cacheIn : {kEmpty, static_cast<void *>(gCachedObject)})
            for (uint32_t value : {3u, 0xffffffffu, 0x80000000u}) {
                Globals g{gService, nullptr, nullptr, nullptr, nullptr, cacheIn};
                uint32_t cellA = cellStart, cellB = cellStart;
                Apply(ctx, g);
                ResetRun(gAcquired);
                gAcquiredSlot8Result = gCachedObject;
                void *ra = original(&cellA, value);
                std::vector<Event> eventsA = gEvents;
                Globals afterA{};
                Snapshot(ctx, afterA);

                Apply(ctx, g);
                ResetRun(gAcquired);
                gAcquiredSlot8Result = gCachedObject;
                void *rb = ours(reinterpret_cast<ServiceCell *>(&cellB), value);
                Globals afterB{};
                Snapshot(ctx, afterB);
                ++ctx.cases;
                if (ra != &cellA || rb != &cellB || cellA != cellB || !SameGlobals(afterA, afterB) || !SameEvents(eventsA, gEvents))
                    ctx.Fail("%s cell=%08x value=%08x cache=%p: original cell %08x (%zu events), ours %08x (%zu events)", name,
                             cellStart, value, cacheIn, cellA, eventsA.size(), cellB, gEvents.size());
            }
    Apply(ctx, Globals{});
}

} // namespace

DIFF_TEST(StaticGetters2_CellConstructFrom, 0xaeb710)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xaeb710);
    CheckConstruct(ctx, "ConstructFrom", [&](uint32_t *cell, uint32_t value) { return orig(cell, value); },
                   [](ServiceCell *cell, uint32_t value) { return static_cast<void *>(cell->ConstructFrom(value)); });
}

DIFF_TEST(StaticGetters2_CellConstructCopy, 0xaeb6f0)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__thiscall *)(void *, const uint32_t *)>(0xaeb6f0);
    CheckConstruct(ctx, "ConstructCopy",
                   [&](uint32_t *cell, uint32_t value) {
                       const uint32_t src = value;
                       return orig(cell, &src);
                   },
                   [](ServiceCell *cell, uint32_t value) {
                       const ServiceCell src{value};
                       return static_cast<void *>(cell->ConstructCopy(&src));
                   });
}
