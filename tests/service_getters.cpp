// Differential tests for the eight cached service getters (RVAs 0xb390c0, 0xb39110, 0xb38ea0,
// 0xb38e50, 0xb38fb0, 0xb8bcb0, 0xb9a990, 0xb9a940). Each runs the original and the
// CachedServiceGetter instantiation on the same globals, with a fake service whose slot 5 records
// its identifiers and stores a chosen object, and fake objects whose slot 2 (Release) records the
// call. Compared: the return value, the cache global afterwards, and the calls made.
//
// Release exceptions: the original's temporary-holder release (0x1466e0) catches C++ exceptions
// (try/catch(...) built with /EHs) and lets SEH exceptions through; ReleaseThrows checks both, and
// that the cache global is already set when the release raises.
#include "difftest.h"
#include "rz/ServiceGetters.h"

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

FakeObject gFramework{}, gCached{}, gFromA{}, gFromB{};

void SetupFakes()
{
    std::memset(gFakeVTable, 0, sizeof(gFakeVTable));
    gFakeVTable[2] = reinterpret_cast<void *>(&RecordRelease);
    gFakeVTable[5] = reinterpret_cast<void *>(&RecordQuery);
    gFramework.vtable = gCached.vtable = gFromA.vtable = gFromB.vtable = gFakeVTable;
}

// The cache globals of the eight getters, and the service global 0x10c9378 they read through.
constexpr uint32_t kCaches[] = {0x10faf6c, 0x10faf74, 0x10faf70, 0x10faf78, 0x10faf80, 0x10fc684, 0x10fc6c0, 0x10fc6bc};

void SetGlobals(difftest::Context &ctx, uint32_t cacheRva, void *cached, void *service)
{
    for (uint32_t rva : kCaches) *ctx.image.At<void *>(rva) = nullptr;
    *ctx.image.At<void *>(cacheRva) = cached;
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

using Getter = void *(*)();

void *gOrigGetter;
Getter gOursGetter;

void CallOriginal()
{
    reinterpret_cast<void *(__cdecl *)()>(gOrigGetter)();
}
void CallOurs()
{
    gOursGetter();
}

// Runs fn and reports whether an SEH exception escaped it (C++ exceptions escaping would also show
// up here, as code 0xE06D7363). Free of C++ objects, as __try requires.
using Thunk = void (*)();
unsigned long EscapedCode(Thunk fn)
{
    __try {
        fn();
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

uint32_t __fastcall RaisingRelease(void *, void *)
{
    RaiseException(0xE0001234, 0, 0, nullptr);
    return 0;
}
uint32_t __fastcall ThrowingRelease(void *, void *)
{
    throw 42;
}

void *gRaisingVTable[8];
FakeObject gRaising{};
FakeObject gThrowing{};

// Compares the original getter with ours over the matrix of cache, service and result states.
void CheckGetter(difftest::Context &ctx, const char *name, uint32_t cacheRva, void *(*ours)(), uint32_t rva)
{
    SetupFakes();
    gOrigGetter = ctx.image.At(rva);
    gOursGetter = ours;

    void *caches[] = {nullptr, &gCached};
    void *services[] = {nullptr, &gFramework};
    void *results[] = {nullptr, &gFromA, &gFromB};
    for (void *cachedIn : caches)
        for (void *service : services)
            for (void *result : results) {
                SetGlobals(ctx, cacheRva, cachedIn, service);
                ResetRun(result);
                void *a = reinterpret_cast<void *(__cdecl *)()>(gOrigGetter)();
                void *cacheA = *ctx.image.At<void *>(cacheRva);
                std::vector<Call> callsA = gCalls;

                SetGlobals(ctx, cacheRva, cachedIn, service);
                ResetRun(result);
                void *b = ours();
                void *cacheB = *ctx.image.At<void *>(cacheRva);
                ++ctx.cases;
                if (a != b || cacheA != cacheB || !SameCalls(callsA, gCalls))
                    ctx.Fail("%s cache=%p service=%p result=%p: original %p (cache %p, %zu calls), ours %p (cache %p, %zu calls)",
                             name, cachedIn, service, result, a, cacheA, callsA.size(), b, cacheB, gCalls.size());
            }

    // Release raising an SEH exception, or a C++ exception, from the temporary: the cache is already
    // set when the release runs, and the original swallows only the C++ one.
    struct Case {
        const char *what;
        uint32_t (__fastcall *release)(void *, void *);
        FakeObject *object;
    };
    gRaising.vtable = gThrowing.vtable = gRaisingVTable;
    Case cases[] = {{"SEH", &RaisingRelease, &gRaising}, {"C++", &ThrowingRelease, &gThrowing}};
    for (Case &c : cases) {
        gRaisingVTable[2] = reinterpret_cast<void *>(c.release);
        SetGlobals(ctx, cacheRva, nullptr, &gFramework);
        ResetRun(c.object);
        unsigned long ea = EscapedCode(&CallOriginal);
        void *cacheA = *ctx.image.At<void *>(cacheRva);

        SetGlobals(ctx, cacheRva, nullptr, &gFramework);
        ResetRun(c.object);
        unsigned long eb = EscapedCode(&CallOurs);
        void *cacheB = *ctx.image.At<void *>(cacheRva);
        ++ctx.cases;
        if (ea != eb || cacheA != cacheB)
            ctx.Fail("%s release of %s: original escaped 0x%08lx, cache %p; ours escaped 0x%08lx, cache %p", name, c.what, ea, cacheA,
                     eb, cacheB);
    }
    SetGlobals(ctx, cacheRva, nullptr, nullptr);
}

} // namespace

DIFF_TEST(ServiceGetter_b390c0, 0xb390c0)
{
    CheckGetter(ctx, "b390c0", 0x10faf6c, &CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>, 0xb390c0);
}

DIFF_TEST(ServiceGetter_b39110, 0xb39110)
{
    CheckGetter(ctx, "b39110", 0x10faf74, &CachedServiceGetter<0x10faf74, 0x4bcf1bb0, 0x0bcf1b58>, 0xb39110);
}

DIFF_TEST(ServiceGetter_b38ea0, 0xb38ea0)
{
    CheckGetter(ctx, "b38ea0", 0x10faf70, &CachedServiceGetter<0x10faf70, 0xeb7d6513, 0xcb7d64f8>, 0xb38ea0);
}

DIFF_TEST(ServiceGetter_b38e50, 0xb38e50)
{
    CheckGetter(ctx, "b38e50", 0x10faf78, &CachedServiceGetter<0x10faf78, 0x0c4f252f, 0x2c4f2954>, 0xb38e50);
}

DIFF_TEST(ServiceGetter_b38fb0, 0xb38fb0)
{
    CheckGetter(ctx, "b38fb0", 0x10faf80, &CachedServiceGetter<0x10faf80, 0x0dc7057a, 0xcdb467ae>, 0xb38fb0);
}

DIFF_TEST(ServiceGetter_b8bcb0, 0xb8bcb0)
{
    CheckGetter(ctx, "b8bcb0", 0x10fc684, &CachedServiceGetter<0x10fc684, 0x056b906e, 0x9af5e924>, 0xb8bcb0);
}

DIFF_TEST(ServiceGetter_b9a990, 0xb9a990)
{
    CheckGetter(ctx, "b9a990", 0x10fc6c0, &CachedServiceGetter<0x10fc6c0, 0x4bcf1bb0, 0x0bcf1b58>, 0xb9a990);
}

DIFF_TEST(ServiceGetter_b9a940, 0xb9a940)
{
    CheckGetter(ctx, "b9a940", 0x10fc6bc, &CachedServiceGetter<0x10fc6bc, 0x056b906e, 0x9af5e924>, 0xb9a940);
}
