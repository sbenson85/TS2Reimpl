// Differential tests for src/misc/UtilitiesE.h.
//
// The constructor and the base destructor body are compared on their object bytes (with canary
// words after the object). The service cache (0xae8900) uses the service object in game global
// 0x10c9378 and the cache global 0xa10f8fcc: the service's slot 5 hands out a fake holder, and the
// holder's slot 5 gives the cached value. The notify functions (0xb6dfb0, 0xb6dfe0) use fake
// objects whose slot 6 gives a target, and the target's slot 13 records (object, key). Every fake
// records its call, so the call sequences are compared as well as the results.
#include "difftest.h"
#include "misc/UtilitiesE.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using DtorFn = void(__thiscall *)(void *);
using CachedFn = void *(__cdecl *)();
using NotifyFn = uint8_t(__cdecl *)(void *, const void *);
using NotifyWordsFn = uint8_t(__cdecl *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);

struct Call {
    int slot;
    const void *self;
    const void *object; // the object argument of slot 13
    uint32_t words[4];  // the key (slot 13) or the query identifiers (slot 5 of the service)
};

std::vector<Call> gCalls;
bool gQueryThrows = false;
void *gHeldValue = nullptr;  // what the holder's slot 5 gives
void *gNextTarget = nullptr; // what slot 6 of the notify objects gives

struct FakeObject {
    void *vtable;
    uint32_t pad;
};

void *gServiceVTable[8];
void *gHeldVTable[16];
void *gNotifyVTable[8];
void *gTargetVTable[16];
FakeObject gService{}, gHeld{}, gNotify{}, gTarget{};

// Fakes are __thiscall: `this` in ecx, the stack arguments popped by the callee. A __fastcall
// function with a dummy edx takes the same registers and stack layout.
uint32_t __fastcall RecordQuery(void *self, void * /*edx*/, uint32_t first, uint32_t second, void **out)
{
    gCalls.push_back({5, self, nullptr, {first, second, 0, 0}});
    *out = &gHeld;
    if (gQueryThrows) throw 7;
    return 0;
}

void *__fastcall HeldSlot11(void *self, void * /*edx*/)
{
    gCalls.push_back({11, self, nullptr, {0, 0, 0, 0}});
    return gHeldValue;
}

void *__fastcall HeldSlot5(void *self, void * /*edx*/)
{
    gCalls.push_back({5, self, nullptr, {0, 0, 0, 0}});
    return gHeldValue;
}

uint32_t __fastcall RecordRelease(void *self, void * /*edx*/)
{
    gCalls.push_back({2, self, nullptr, {0, 0, 0, 0}});
    return 0;
}

void *__fastcall NotifySlot6(void *self, void * /*edx*/)
{
    gCalls.push_back({6, self, nullptr, {0, 0, 0, 0}});
    return gNextTarget;
}

void __fastcall RecordSlot13(void *self, void * /*edx*/, void *object, const void *key)
{
    Call c{13, self, object, {0, 0, 0, 0}};
    std::memcpy(c.words, key, sizeof(c.words));
    gCalls.push_back(c);
}

void SetupFakes()
{
    std::memset(gServiceVTable, 0, sizeof(gServiceVTable));
    std::memset(gHeldVTable, 0, sizeof(gHeldVTable));
    std::memset(gNotifyVTable, 0, sizeof(gNotifyVTable));
    std::memset(gTargetVTable, 0, sizeof(gTargetVTable));
    gServiceVTable[5] = reinterpret_cast<void *>(&RecordQuery);
    gHeldVTable[2] = reinterpret_cast<void *>(&RecordRelease);
    gHeldVTable[5] = reinterpret_cast<void *>(&HeldSlot5);
    gHeldVTable[11] = reinterpret_cast<void *>(&HeldSlot11);
    gNotifyVTable[6] = reinterpret_cast<void *>(&NotifySlot6);
    gTargetVTable[13] = reinterpret_cast<void *>(&RecordSlot13);
    gService.vtable = gServiceVTable;
    gHeld.vtable = gHeldVTable;
    gNotify.vtable = gNotifyVTable;
    gTarget.vtable = gTargetVTable;
}

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const Call &x = a[i], &y = b[i];
        if (x.slot != y.slot || x.self != y.self || x.object != y.object ||
            std::memcmp(x.words, y.words, sizeof(x.words)) != 0)
            return false;
    }
    return true;
}

void SetService(difftest::Context &ctx, void *service)
{
    *ctx.image.At<void *>(0x10c9378) = service;
}

void SetCache(difftest::Context &ctx, void *value)
{
    *ctx.image.At<void *>(0x10f8fcc) = value;
}

void SetParserCache(difftest::Context &ctx, void *value)
{
    *ctx.image.At<void *>(0x10f8fd0) = value;
}

void *GetParserCache(difftest::Context &ctx)
{
    return *ctx.image.At<void *>(0x10f8fd0);
}

void *GetCache(difftest::Context &ctx)
{
    return *ctx.image.At<void *>(0x10f8fcc);
}

// A block of random bytes with canary words after it. Both sides start from the same bytes.
template <typename T> struct Guarded {
    T object;
    uint32_t canary[4];
};

template <typename T> void Fill(Guarded<T> &g, std::mt19937 &gen)
{
    for (auto &w : reinterpret_cast<uint32_t(&)[sizeof(T) / 4]>(g.object)) w = gen();
    for (auto &c : g.canary) c = gen();
}

template <typename T> void CopyGuarded(Guarded<T> &dst, const Guarded<T> &src)
{
    dst = src;
}

} // namespace

DIFF_TEST(TSLuaWrapperBase_Destruct, 0xb65860)
{
    auto orig = ctx.Original<DtorFn>(0xb65860);
    std::mt19937 gen(2202);
    for (int i = 0; i < 200; ++i) {
        Guarded<TSLuaWrapperBase> a, b;
        Fill(a, gen);
        CopyGuarded(b, a);
        orig(&a.object);
        b.object.Destruct();
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0)
            ctx.Fail("TSLuaWrapper dtor body (case %d): vtable %p / %p, refcount %u / %u", i,
                     a.object.mVTable, b.object.mVTable, a.object.mRefCount, b.object.mRefCount);
    }
}

DIFF_TEST(GetCachedQueriedObject, 0xae8900)
{
    SetupFakes();
    auto orig = ctx.Original<CachedFn>(0xae8900);
    std::mt19937 gen(3303);
    for (int i = 0; i < 200; ++i) {
        // Mostly a cache miss. Some cases start with a cached value, and some make the query throw.
        void *startCache = i % 4 == 0 ? reinterpret_cast<void *>(static_cast<uintptr_t>(gen() | 1)) : nullptr;
        // The held object's slot 5 result: usually non-null, sometimes null.
        gHeldValue = i % 5 == 0 ? nullptr : reinterpret_cast<void *>(static_cast<uintptr_t>((gen() | 2) & ~1u));
        gQueryThrows = i % 7 == 3 && startCache == nullptr;

        SetService(ctx, &gService);
        SetCache(ctx, startCache);
        gCalls.clear();
        void *retA = nullptr;
        bool thrownA = false;
        try {
            retA = orig();
        } catch (int) {
            thrownA = true;
        }
        std::vector<Call> callsA = gCalls;
        void *cacheA = GetCache(ctx);

        SetService(ctx, &gService);
        SetCache(ctx, startCache);
        gCalls.clear();
        void *retB = nullptr;
        bool thrownB = false;
        try {
            retB = GetCachedQueriedObject();
        } catch (int) {
            thrownB = true;
        }
        ++ctx.cases;
        void *cacheB = GetCache(ctx);

        if (thrownA != thrownB || retA != retB || cacheA != cacheB || !SameCalls(callsA, gCalls))
            ctx.Fail("GetCachedQueriedObject (case %d): return %p / %p, cache %p / %p, throw %d / %d, %zu / %zu calls",
                     i, retA, retB, cacheA, cacheB, thrownA, thrownB, callsA.size(), gCalls.size());
    }
    gQueryThrows = false;
    gHeldValue = nullptr;
    SetCache(ctx, nullptr);
    SetService(ctx, nullptr);
}

DIFF_TEST(GetMaterialParserCached, 0xae8980)
{
    SetupFakes();
    auto orig = ctx.Original<CachedFn>(0xae8980);
    std::mt19937 gen(3304);
    for (int i = 0; i < 200; ++i) {
        // Mostly a cache miss. Some cases start with a cached value, and some make the query throw.
        void *startCache = i % 4 == 0 ? reinterpret_cast<void *>(static_cast<uintptr_t>(gen() | 1)) : nullptr;
        // The held object's slot 5 result: usually non-null, sometimes null.
        gHeldValue = i % 5 == 0 ? nullptr : reinterpret_cast<void *>(static_cast<uintptr_t>((gen() | 2) & ~1u));
        gQueryThrows = i % 7 == 3 && startCache == nullptr;

        SetService(ctx, &gService);
        SetParserCache(ctx, startCache);
        gCalls.clear();
        void *retA = nullptr;
        bool thrownA = false;
        try {
            retA = orig();
        } catch (int) {
            thrownA = true;
        }
        std::vector<Call> callsA = gCalls;
        void *cacheA = GetParserCache(ctx);

        SetService(ctx, &gService);
        SetParserCache(ctx, startCache);
        gCalls.clear();
        void *retB = nullptr;
        bool thrownB = false;
        try {
            retB = GetMaterialParserCached();
        } catch (int) {
            thrownB = true;
        }
        ++ctx.cases;
        void *cacheB = GetParserCache(ctx);

        if (thrownA != thrownB || retA != retB || cacheA != cacheB || !SameCalls(callsA, gCalls))
            ctx.Fail("GetMaterialParserCached (case %d): return %p / %p, cache %p / %p, throw %d / %d, %zu / %zu calls",
                     i, retA, retB, cacheA, cacheB, thrownA, thrownB, callsA.size(), gCalls.size());
    }
    gQueryThrows = false;
    gHeldValue = nullptr;
    SetParserCache(ctx, nullptr);
    SetService(ctx, nullptr);
}

DIFF_TEST(NotifySlot6ResultWithKey, 0xb6dfb0)
{
    SetupFakes();
    auto orig = ctx.Original<NotifyFn>(0xb6dfb0);
    std::mt19937 gen(4404);
    for (int i = 0; i < 200; ++i) {
        // Case kinds: null object, object whose slot 6 gives null, object with a target.
        int kind = i % 3;
        void *object = kind == 0 ? nullptr : &gNotify;
        gNextTarget = kind == 2 ? &gTarget : nullptr;
        uint32_t key[4] = {gen(), gen(), gen(), gen()};

        gCalls.clear();
        uint8_t retA = orig(object, key);
        std::vector<Call> callsA = gCalls;

        gCalls.clear();
        bool retB = NotifySlot6ResultWithKey(object, key);
        ++ctx.cases;

        if (retA != static_cast<uint8_t>(retB ? 1 : 0) || !SameCalls(callsA, gCalls))
            ctx.Fail("NotifySlot6ResultWithKey (case %d): al %u / %u, %zu / %zu calls", i, retA, retB ? 1u : 0u,
                     callsA.size(), gCalls.size());
    }
    gNextTarget = nullptr;
}

DIFF_TEST(NotifySlot6ResultWithKeyWords, 0xb6dfe0)
{
    SetupFakes();
    auto orig = ctx.Original<NotifyWordsFn>(0xb6dfe0);
    std::mt19937 gen(5505);
    for (int i = 0; i < 200; ++i) {
        int kind = i % 3;
        void *object = kind == 0 ? nullptr : &gNotify;
        gNextTarget = kind == 2 ? &gTarget : nullptr;
        uint32_t k0 = gen(), k1 = gen(), k2 = gen(), k3 = gen();
        if (i % 11 == 0) k0 = k1 = k2 = k3 = 0; // an all-zero key is still passed through

        gCalls.clear();
        uint8_t retA = orig(object, k0, k1, k2, k3);
        std::vector<Call> callsA = gCalls;

        gCalls.clear();
        bool retB = NotifySlot6ResultWithKeyWords(object, k0, k1, k2, k3);
        ++ctx.cases;

        if (retA != static_cast<uint8_t>(retB ? 1 : 0) || !SameCalls(callsA, gCalls))
            ctx.Fail("NotifySlot6ResultWithKeyWords (case %d): al %u / %u, %zu / %zu calls", i, retA, retB ? 1u : 0u,
                     callsA.size(), gCalls.size());
    }
    gNextTarget = nullptr;
}
