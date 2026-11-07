// Differential tests for the lazily initialised singleton (0x20ddfb) and what it uses: the service
// getter (0x1427b5, reached through the thunk at 0x14323f), ScopedRef's acquire (0x20dc91) and
// release (0x1cbd79). The service and the objects it hands out are fakes whose vtable slots record
// their calls, so the call sequences are compared along with the results and the globals.
//
// Exceptions from Release: the original wraps the call in try/catch(...) built with /EHs, so C++
// exceptions are swallowed and SEH exceptions propagate; Singleton_20ddfb_ReleaseThrows checks both.
#include "difftest.h"
#include "rz/Singleton_20ddfb.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace {

struct Call {
    int slot;
    const void *self;
    uint32_t a, b;
    const void *wrote; // what slot 5 stored through its output pointer
};

std::vector<Call> gCalls;
void *gServiceResult = nullptr; // what the fake service's slot 5 stores (null is a valid answer)

// Fakes are called as __thiscall: `this` in ecx, stack arguments popped by the callee. A __fastcall
// function with a dummy edx takes the same registers and stack layout.
uint32_t __fastcall RecordRelease(void *self, void * /*edx*/)
{
    gCalls.push_back({2, self, 0, 0, nullptr});
    return 0;
}

uint32_t __fastcall RecordQuery(void *self, void * /*edx*/, uint32_t a, uint32_t b, void **out)
{
    gCalls.push_back({5, self, a, b, gServiceResult});
    *out = gServiceResult;
    return 0;
}

void *gFakeVTable[8];

struct FakeObject {
    void *vtable;
    uint32_t pad;
};

FakeObject gFramework{}, gCached{}, gFromServiceA{}, gFromServiceB{}, gHeld{};

void SetupFakes()
{
    std::memset(gFakeVTable, 0, sizeof(gFakeVTable));
    gFakeVTable[2] = reinterpret_cast<void *>(&RecordRelease);
    gFakeVTable[5] = reinterpret_cast<void *>(&RecordQuery);
    gFramework.vtable = gCached.vtable = gFromServiceA.vtable = gFromServiceB.vtable = gHeld.vtable = gFakeVTable;
}

// The two game globals the code reads: 0x10cc288 (cached singleton) and 0x10c9378 (service).
void SetGlobals(difftest::Context &ctx, void *cached, void *service)
{
    *ctx.image.At<void *>(0x10cc288) = cached;
    *ctx.image.At<void *>(0x10c9378) = service;
}

void ResetRun(void *result)
{
    gCalls.clear();
    gServiceResult = result;
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

} // namespace

DIFF_TEST(Singleton_20ddfb_Get, 0x20ddfb)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__cdecl *)()>(0x20ddfb);
    void *cachedStates[] = {nullptr, &gCached};
    void *services[] = {nullptr, &gFramework};
    void *results[] = {nullptr, &gFromServiceA, &gFromServiceB};
    for (void *cachedIn : cachedStates)
        for (void *service : services)
            for (void *result : results) {
                SetGlobals(ctx, cachedIn, service);
                ResetRun(result);
                void *a = orig();
                void *cachedA = *ctx.image.At<void *>(0x10cc288);
                std::vector<Call> callsA = gCalls;

                SetGlobals(ctx, cachedIn, service);
                ResetRun(result);
                void *b = GetCachedSingleton();
                void *cachedB = *ctx.image.At<void *>(0x10cc288);
                ++ctx.cases;
                if (a != b || cachedA != cachedB || !SameCalls(callsA, gCalls))
                    ctx.Fail("Get cached=%p service=%p result=%p: original %p (cache %p, %zu calls), ours %p (cache %p, %zu calls)",
                             cachedIn, service, result, a, cachedA, callsA.size(), b, cachedB, gCalls.size());
            }
    SetGlobals(ctx, nullptr, nullptr);
}

DIFF_TEST(Singleton_20ddfb_GetService, 0x1427b5)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__cdecl *)()>(0x1427b5);
    for (void *service : {static_cast<void *>(nullptr), static_cast<void *>(&gFramework)}) {
        SetGlobals(ctx, &gCached, service);
        void *a = orig();
        SetGlobals(ctx, &gCached, service);
        void *b = GetServiceObject();
        ++ctx.cases;
        if (a != b) ctx.Fail("GetServiceObject with service %p: %p vs %p", service, a, b);
    }
    SetGlobals(ctx, nullptr, nullptr);
}

DIFF_TEST(Singleton_20ddfb_Acquire, 0x20dc91)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x20dc91);
    void *priors[] = {nullptr, &gHeld};
    void *services[] = {nullptr, &gFramework};
    void *results[] = {nullptr, &gFromServiceA};
    for (void *prior : priors)
        for (void *service : services)
            for (void *result : results) {
                ScopedRef a{prior}, b{prior};
                SetGlobals(ctx, &gCached, service);
                ResetRun(result);
                void *ra = orig(&a);
                std::vector<Call> callsA = gCalls;

                SetGlobals(ctx, &gCached, service);
                ResetRun(result);
                void *rb = b.AcquireFromService();
                ++ctx.cases;
                if (ra != &a || rb != &b || a.mObject != b.mObject || !SameCalls(callsA, gCalls))
                    ctx.Fail("Acquire prior=%p service=%p result=%p: original (%p, %p) ours (%p, %p)", prior, service, result, a.mObject,
                             ra, b.mObject, rb);
            }
    SetGlobals(ctx, nullptr, nullptr);
}

DIFF_TEST(Singleton_20ddfb_Release, 0x1cbd79)
{
    SetupFakes();
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x1cbd79);
    void *objects[] = {nullptr, &gHeld};
    for (void *object : objects) {
        ScopedRef a{object}, b{object};
        ResetRun(nullptr);
        orig(&a);
        std::vector<Call> callsA = gCalls;
        ResetRun(nullptr);
        b.ReleaseObject();
        ++ctx.cases;
        if (a.mObject != b.mObject || !SameCalls(callsA, gCalls))
            ctx.Fail("Release object=%p: original %zu calls, ours %zu calls", object, callsA.size(), gCalls.size());
    }
}

namespace {
uint32_t __fastcall RaisingRelease(void *, void *)
{
    RaiseException(0xE0001234, 0, 0, nullptr);
    return 0;
}
uint32_t __fastcall ThrowingRelease(void *, void *)
{
    throw 42;
}

// Runs fn(ref) and reports whether an SEH exception escaped it (C++ exceptions escaping would also
// show up here, as code 0xE06D7363). Free of C++ objects, as __try requires.
using ReleaseFn = void (*)(void *);
unsigned long EscapedCode(ReleaseFn fn, void *ref)
{
    __try {
        fn(ref);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

void *gOrigRelease;
void CallOriginal(void *ref)
{
    reinterpret_cast<void(__thiscall *)(void *)>(gOrigRelease)(ref);
}
void CallOurs(void *ref)
{
    static_cast<ScopedRef *>(ref)->ReleaseObject();
}
} // namespace

DIFF_TEST(Singleton_20ddfb_ReleaseThrows, 0x1cbd79)
{
    gOrigRelease = ctx.image.At(0x1cbd79);
    for (void *fn : {reinterpret_cast<void *>(&RaisingRelease), reinterpret_cast<void *>(&ThrowingRelease)}) {
        void *vtable[3] = {nullptr, nullptr, fn};
        void *object = vtable;
        void *holder = &object; // an object whose first word is its vtable
        ScopedRef a{holder}, b{holder};
        unsigned long ea = EscapedCode(&CallOriginal, &a), eb = EscapedCode(&CallOurs, &b);
        ++ctx.cases;
        if (ea != eb) ctx.Fail("Release %s: escaped 0x%08lx from the original, 0x%08lx from ours", fn == &RaisingRelease ? "raising SEH" : "throwing C++", ea, eb);
    }
}
