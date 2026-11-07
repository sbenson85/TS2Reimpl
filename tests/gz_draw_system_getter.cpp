// Differential test for the cached draw system getter 0x14f330 (lane OZ): the CachedServiceGetter instantiation with the
// cache 0x10c95dc and the identifiers 0xeb2ea3cb / 0x4b2ea3b3 (cDraw2D's DrawSystem()). As tests/service_getters.cpp: a fake
// service object whose slot 5 records the identifiers and stores a chosen object, fake objects whose slot 2 (Release)
// records the call. Compared: the return value, the cache global afterwards and the calls made; the temporary's Release
// raising an SEH or a C++ exception (the original swallows only the C++ one, and the cache is already set).
#include "difftest.h"

#include "gz/cDraw2D.h"
#include "gz/cIGZDrawContextEngine.h"
#include "rz/ServiceGetters.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace {

using nGZGraphic4::cDraw2D;

struct Call {
    int slot;
    const void *self;
    uint32_t a, b;
    const void *wrote;
};
std::vector<Call> gCalls;
void *gServiceResult = nullptr;

uint32_t __fastcall RecordRelease(void *self, void *)
{
    gCalls.push_back({2, self, 0, 0, nullptr});
    return 0;
}
uint32_t __fastcall RecordQuery(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    gCalls.push_back({5, self, a, b, gServiceResult});
    *out = gServiceResult;
    return 0;
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

struct FakeObject {
    void *vtable;
    uint32_t pad;
};
void *gVTable[8], *gRaisingVTable[8];
FakeObject gFramework{}, gCached{}, gFromA{}, gFromB{}, gRaising{}, gThrowing{};

constexpr uint32_t kCache = cDraw2D::kDrawSystemCell;
using Getter = void *(*)();
Getter gOurs;
void *gOriginal;

void SetGlobals(difftest::Context &ctx, void *cached, void *service)
{
    *ctx.image.At<void *>(kCache) = cached;
    *ctx.image.At<void *>(0x10c9378) = service;
}
void CallOriginal() { reinterpret_cast<void *(__cdecl *)()>(gOriginal)(); }
void CallOurs() { gOurs(); }

unsigned long EscapedCode(void (*fn)())
{
    __try {
        fn();
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].slot != b[i].slot || a[i].self != b[i].self || a[i].a != b[i].a || a[i].b != b[i].b || a[i].wrote != b[i].wrote)
            return false;
    return true;
}

} // namespace

DIFF_TEST(cDraw2D_DrawSystemGetter, 0x14f330)
{
    std::memset(gVTable, 0, sizeof gVTable);
    std::memset(gRaisingVTable, 0, sizeof gRaisingVTable);
    gVTable[2] = reinterpret_cast<void *>(&RecordRelease);
    gVTable[5] = reinterpret_cast<void *>(&RecordQuery);
    gFramework.vtable = gCached.vtable = gFromA.vtable = gFromB.vtable = gVTable;
    gRaising.vtable = gThrowing.vtable = gRaisingVTable;
    gOriginal = ctx.image.At(0x14f330);
    gOurs = &CachedServiceGetter<cDraw2D::kDrawSystemCell, cIGZDrawSystem::kServiceId, cIGZDrawSystem::kIid>;

    void *caches[] = {nullptr, &gCached};
    void *services[] = {nullptr, &gFramework};
    void *results[] = {nullptr, &gFromA, &gFromB};
    for (void *cachedIn : caches)
        for (void *service : services)
            for (void *result : results) {
                SetGlobals(ctx, cachedIn, service);
                gCalls.clear();
                gServiceResult = result;
                void *a = reinterpret_cast<void *(__cdecl *)()>(gOriginal)();
                void *cacheA = *ctx.image.At<void *>(kCache);
                const std::vector<Call> callsA = gCalls;

                SetGlobals(ctx, cachedIn, service);
                gCalls.clear();
                gServiceResult = result;
                void *b = gOurs();
                void *cacheB = *ctx.image.At<void *>(kCache);
                ++ctx.cases;
                if (a != b || cacheA != cacheB || !SameCalls(callsA, gCalls))
                    ctx.Fail("cache=%p service=%p result=%p: original %p (cache %p, %zu calls), ours %p (cache %p, %zu calls)",
                             cachedIn, service, result, a, cacheA, callsA.size(), b, cacheB, gCalls.size());
            }

    struct Case {
        const char *what;
        uint32_t(__fastcall *release)(void *, void *);
        FakeObject *object;
    };
    Case cases[] = {{"SEH", &RaisingRelease, &gRaising}, {"C++", &ThrowingRelease, &gThrowing}};
    for (Case &c : cases) {
        gRaisingVTable[2] = reinterpret_cast<void *>(c.release);
        SetGlobals(ctx, nullptr, &gFramework);
        gCalls.clear();
        gServiceResult = c.object;
        const unsigned long ea = EscapedCode(&CallOriginal);
        void *cacheA = *ctx.image.At<void *>(kCache);

        SetGlobals(ctx, nullptr, &gFramework);
        gCalls.clear();
        gServiceResult = c.object;
        const unsigned long eb = EscapedCode(&CallOurs);
        void *cacheB = *ctx.image.At<void *>(kCache);
        ++ctx.cases;
        if (ea != eb || cacheA != cacheB)
            ctx.Fail("release of %s: original escaped 0x%08lx, cache %p; ours escaped 0x%08lx, cache %p", c.what, ea, cacheA, eb,
                     cacheB);
    }
    SetGlobals(ctx, nullptr, nullptr);
}
