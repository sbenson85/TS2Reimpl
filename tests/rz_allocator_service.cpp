// Differential tests for cRZSystemService's constructor/destructor, cGZAllocatorService, its
// framework holder and GetAllocatorService (src/rz/cGZAllocatorService.*).
//
// Objects are built in buffers pre-filled with the same random bytes on both sides and compared
// whole. Fake framework and service objects record the slot calls. GetAllocatorService is run on the
// image's globals (the framework pointer 0x10c9378, the static instance 0x10cb5b4 and its guard
// 0x10cb5b8) for each path: a framework answer, an already-initialised static, and first use.
#include "difftest.h"
#include "free_recorder.h"
#include "service_shape.h"
#include "rz/cGZAllocatorService.h"

#include <windows.h>

#include <climits>
#include <cstring>
#include <iterator>
#include <random>
#include <stdexcept>
#include <vector>

namespace {

struct Call {
    int slot;
    const void *self;
    uint32_t a, b;
};
std::vector<Call> gCalls;
void *gQueryResult = nullptr;
int gReleaseMode = 0; // 0 returns, 1 raises an SEH exception, 2 throws a C++ exception

uint32_t __fastcall RecordAddRef(void *self, void *)
{
    gCalls.push_back({1, self, 0, 0});
    return 1;
}

uint32_t __fastcall RecordRelease(void *self, void *)
{
    gCalls.push_back({2, self, 0, 0});
    if (gReleaseMode == 1) RaiseException(0xE0001234, 0, 0, nullptr);
    if (gReleaseMode == 2) throw std::runtime_error("release");
    return 0;
}

bool __fastcall RecordQuery(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    gCalls.push_back({5, self, a, b});
    *out = gQueryResult;
    return gQueryResult != nullptr;
}

void __fastcall RecordFree(void *self, void *, void *mem)
{
    gCalls.push_back({4, self, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mem)), 0});
}

void *gFakeVTable[8] = {nullptr, reinterpret_cast<void *>(&RecordAddRef), reinterpret_cast<void *>(&RecordRelease), nullptr,
                        reinterpret_cast<void *>(&RecordFree), reinterpret_cast<void *>(&RecordQuery), nullptr, nullptr};
struct FakeObject {
    void *vtable = gFakeVTable;
    uint32_t pad = 0;
};

// The original's atexit (library code, RVA 0x824cbe) works on its CRT's exit table, which the
// harness never initialises; while the first-use case runs, the image's atexit is redirected to this
// stub (5-byte jmp patched over its entry, restored afterwards), which records the handler.
int gAtexitCalls = 0;
int __cdecl StubAtexit(void (*)())
{
    ++gAtexitCalls;
    return 0;
}

struct AtexitRedirect {
    uint8_t *site;
    uint8_t saved[5];
    explicit AtexitRedirect(difftest::Context &ctx) : site(ctx.image.At<uint8_t>(0x824cbe))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&StubAtexit) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~AtexitRedirect() { std::memcpy(site, saved, 5); }
};

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].slot != b[i].slot || a[i].self != b[i].self || a[i].a != b[i].a || a[i].b != b[i].b) return false;
    return true;
}

template <size_t N> void FillRandom(uint8_t (&a)[N], uint8_t (&b)[N], std::mt19937 &rng)
{
    for (size_t i = 0; i < N; ++i) a[i] = b[i] = static_cast<uint8_t>(rng());
}

} // namespace

DIFF_TEST(RZSystemService_ConstructDestruct, 0x1b9584)
{
    auto construct = ctx.Original<void *(__thiscall *)(void *, uint32_t, uint32_t)>(0x1b9584);
    auto destruct = ctx.Original<void(__thiscall *)(void *)>(0x1b95fe);
    std::mt19937 rng(0x5E5);
    for (int i = 0; i < 2000; ++i) {
        alignas(4) uint8_t a[0x24], b[0x24];
        FillRandom(a, b, rng);
        uint32_t id = rng(), priority = rng();
        void *ra = construct(a + 4, id, priority);
        void *rb = reinterpret_cast<cRZSystemService *>(b + 4)->Construct(id, priority);
        ++ctx.cases;
        if (static_cast<uint8_t *>(ra) - a != static_cast<uint8_t *>(rb) - b || std::memcmp(a, b, sizeof(a)) != 0)
            ctx.Fail("Construct(%08x, %08x)", id, priority);
        FillRandom(a, b, rng);
        destruct(a + 4);
        reinterpret_cast<cRZSystemService *>(b + 4)->Destruct();
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Destruct");
    }
}

namespace {

// cGZAllocatorService in the standalone shape (lane KD): the original side is an object from the original constructor
// 0x1be59c (the game's vtables), ours one from our constructor (the hook's adapter: our vtables), every slot called through
// the side's own vtable; objects compared word by word with the vtable words as codes (tests/service_shape.h).
using Service = cGZAllocatorService;
constexpr uint32_t kFaceOffset[3] = {0, 4, 8};
constexpr uint32_t kGameVt[3] = {Service::kVTable, Service::kSystemServiceVTable, Service::kUnknownVTable};

struct Maps {
    svc::CodeMap a, b;
    Maps()
    {
        alignas(4) static uint8_t buffer[sizeof(Service)];
        auto *ours = cGZAllocatorServiceAdapters::Construct(buffer, nullptr);
        for (uint32_t f = 0; f < 3; ++f) {
            a.AddRva(kGameVt[f], f);
            b.AddRva(kGameVt[f], f);
            b.Add(svc::VTableAt(ours, kFaceOffset[f]), f);
        }
        ours->~cGZAllocatorService();
        svc::AddServiceBaseCodes(a, b, 0x10);
    }
    std::string Compare(const void *x, const void *y, uint32_t size) const { return svc::Diff(svc::Words(x, size, a), svc::Words(y, size, b)); }
};

} // namespace

// Buffers pre-filled with the same random bytes (the object at +4 of a 0x28-byte buffer, so the words around it are
// compared too).
DIFF_TEST(GZAllocatorService_Construct, 0x1be59c)
{
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(0x1be59c);
    Maps maps;
    std::mt19937 rng(0xA5);
    for (int i = 0; i < 1000; ++i) {
        alignas(4) uint8_t a[0x28], b[0x28];
        FillRandom(a, b, rng);
        void *ra = construct(a + 4);
        void *rb = cGZAllocatorServiceAdapters::Construct(b + 4, nullptr);
        ++ctx.cases;
        const std::string diff = maps.Compare(a, b, sizeof(a));
        if (ra != a + 4 || rb != b + 4 || !diff.empty()) ctx.Fail("Construct: result or object differs at %s", diff.c_str());
        for (uint32_t f = 0; f < 3; ++f)
            if (svc::VTableAt(b + 4, kFaceOffset[f]) == GameAddress<void *>(kGameVt[f])) ctx.Fail("our face %u has the game's vtable", f);
    }
}

// The destructor 0x1be610 (called directly; ours: our destructor, no dispatch), the deleting destructor 0x1be635 (on the
// primary pointer) and its +8 thunk 0x1be62d (ours: our +8 vtable's slot 3 for both), flags 0..3, random service fields.
DIFF_TEST(GZAllocatorService_Destruct, 0x1be610)
{
    auto destruct = ctx.Original<void(__thiscall *)(void *)>(0x1be610);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1be635);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1be62d);
    Maps maps;
    std::mt19937 rng(0xA6);
    for (int run = 0; run < 60; ++run) {
        const int route = run % 3;
        const uint32_t flags = route == 0 ? 0 : (run / 3) % 4;
        auto *a = static_cast<uint8_t *>(::operator new(sizeof(Service)));
        auto *b = static_cast<uint8_t *>(::operator new(sizeof(Service)));
        std::memset(a, 0xcd, sizeof(Service)); // the constructor leaves the padding at +0x1d untouched
        std::memset(b, 0xcd, sizeof(Service));
        ctx.Original<void *(__thiscall *)(void *)>(0x1be59c)(a);
        cGZAllocatorServiceAdapters::Construct(b, nullptr);
        for (uint32_t offset = 0xc; offset < 0x1c; offset += 4) { // count, id, priorities
            const uint32_t value = rng();
            std::memcpy(a + offset, &value, 4);
            std::memcpy(b + offset, &value, 4);
        }
        difftest::FreeRecorder recA;
        void *ra = route == 0 ? (destruct(a), a) : route == 1 ? deleting(a, flags) : thunk(a + 8, flags);
        recA.Stop();
        const bool freedA = recA.Freed(a);
        difftest::FreeRecorder recB;
        void *rb = b;
        if (route == 0) {
            reinterpret_cast<Service *>(b)->cGZAllocatorService::~cGZAllocatorService();
        } else {
            const uint32_t args[1] = {flags};
            rb = reinterpret_cast<void *>(svc::CallWords(svc::VTableAt(b, 8)[3], b + 8, 1, args));
        }
        recB.Stop();
        const bool freedB = recB.Freed(b);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("route %d flags %u: result", route, flags);
        if (freedA != freedB || freedA != ((flags & 1) != 0)) ctx.Fail("route %d flags %u: freed %d/%d", route, flags, freedA, freedB);
        if (!freedA && !freedB) {
            const std::string diff = maps.Compare(a, b, sizeof(Service));
            if (!diff.empty()) ctx.Fail("route %d: object after destruction differs at %s", route, diff.c_str());
        }
        if (!freedA) ::operator delete(a);
        if (!freedB) ::operator delete(b);
    }
}

namespace {

struct Slot {
    uint32_t face, slot, words;
    svc::Ret ret;
    const char *name;
};

// Every slot of the three faces but the allocation slots (tests/rz_general_allocator.cpp RZMemory_Wrappers) and the
// deleting destructor (the destructor test): QueryInterface 0x1be661 and its thunks 0x1be683 / 0x1be68b, the folded
// AddRef / Release 0x145240 / 0x1a85b0 (and their thunks), and cRZSystemService's.
const Slot kSlots[] = {
    {0, 0, 2, svc::kByte, "QueryInterface"},       {0, 1, 0, svc::kWord, "AddRef"},
    {0, 2, 0, svc::kWord, "Release"},
    {4, 0, 2, svc::kByte, "+4 QueryInterface"},    {4, 1, 0, svc::kWord, "+4 AddRef"},
    {4, 2, 0, svc::kWord, "+4 Release"},           {4, 3, 0, svc::kWord, "GetServiceID"},
    {4, 4, 1, svc::kVoid, "SetServiceID"},         {4, 5, 0, svc::kWord, "GetServicePriority"},
    {4, 6, 0, svc::kByte, "IsServiceRunning"},     {4, 7, 1, svc::kVoid, "SetServiceRunning"},
    {4, 8, 0, svc::kByte, "Init"},                 {4, 9, 0, svc::kByte, "Shutdown"},
    {4, 10, 1, svc::kByte, "OnTick"},              {4, 11, 1, svc::kByte, "OnIdle"},
    {4, 12, 0, svc::kWord, "GetServiceTickPriority"},
    {8, 0, 2, svc::kByte, "+8 QueryInterface"},    {8, 1, 0, svc::kWord, "+8 AddRef"},
    {8, 2, 0, svc::kWord, "+8 Release"},           {8, 4, 0, svc::kWord, "RemoveRef"},
    {8, 5, 0, svc::kWord, "RefCount"},
};

constexpr uint32_t kIds[] = {0x3ae4beab, 0x3ae4bea3, 0x287fb697, 1, 0, 2, 0xffffffff, 0x3ae4beac, 0x3ae4beaa};

} // namespace

// Through each side's own vtable (a Release at a count of 1 or below deletes the object).
DIFF_TEST(GZAllocatorService_Slots, 0x1be661)
{
    Maps maps;
    std::mt19937 rng(0xA7);
    void *const kUntouched = reinterpret_cast<void *>(0x5a5a5a5a);
    constexpr unsigned kSlotCount[3] = {6, 13, 6};
    for (const Slot &s : kSlots) {
        if (s.slot >= kSlotCount[s.face / 4]) ctx.Fail("%s: slot out of range", s.name);
        const int runs = s.slot == 0 ? 2 * static_cast<int>(std::size(kIds)) : 24;
        for (int run = 0; run < runs; ++run) {
            auto *a = static_cast<uint8_t *>(::operator new(sizeof(Service)));
            auto *b = static_cast<uint8_t *>(::operator new(sizeof(Service)));
            std::memset(a, 0xcd, sizeof(Service));
            std::memset(b, 0xcd, sizeof(Service));
            ctx.Original<void *(__thiscall *)(void *)>(0x1be59c)(a);
            cGZAllocatorServiceAdapters::Construct(b, nullptr);
            // The same random service fields: count (0..5 or random), id, priorities, the running byte (0, 0x80 or random).
            for (uint32_t offset = 0xc; offset < 0x20; offset += 4) {
                uint32_t value = rng();
                if (offset == 0xc && run % 8 < 6) value = static_cast<uint32_t>(run % 6);
                if (offset == 0x1c && run % 4 == 0) value &= 0xffffff00;
                if (offset == 0x1c && run % 4 == 1) value = (value & 0xffffff00) | 0x80;
                std::memcpy(a + offset, &value, 4);
                std::memcpy(b + offset, &value, 4);
            }
            uint32_t args[2] = {rng(), rng()};
            if (s.slot == 0) args[0] = kIds[run % std::size(kIds)];
            void *outA = kUntouched, *outB = kUntouched;
            uint32_t argsA[2] = {args[0], s.slot == 0 ? svc::P(&outA) : args[1]};
            uint32_t argsB[2] = {args[0], s.slot == 0 ? svc::P(&outB) : args[1]};
            difftest::FreeRecorder recA;
            const uint32_t ra = svc::CallWords(GameAddress<void *>(kGameVt[s.face / 4])[s.slot], a + s.face, s.words, argsA);
            recA.Stop();
            const bool freedA = recA.Freed(a);
            difftest::FreeRecorder recB;
            const uint32_t rb = svc::CallWords(svc::VTableAt(b, s.face)[s.slot], b + s.face, s.words, argsB);
            recB.Stop();
            const bool freedB = recB.Freed(b);
            ++ctx.cases;
            if (svc::Masked(s.ret, ra) != svc::Masked(s.ret, rb)) ctx.Fail("%s run %d: result 0x%x/0x%x", s.name, run, ra, rb);
            if (freedA != freedB) ctx.Fail("%s run %d: freed %d/%d", s.name, run, freedA, freedB);
            const int32_t offA = outA == kUntouched ? -1 : static_cast<int32_t>(svc::P(outA) - svc::P(a));
            const int32_t offB = outB == kUntouched ? -1 : static_cast<int32_t>(svc::P(outB) - svc::P(b));
            if (offA != offB) ctx.Fail("%s run %d id 0x%x: out offset %d/%d", s.name, run, args[0], offA, offB);
            if (!freedA && !freedB) {
                const std::string diff = maps.Compare(a, b, sizeof(Service));
                if (!diff.empty()) ctx.Fail("%s run %d: object differs at %s", s.name, run, diff.c_str());
            }
            if (s.slot == 0 && args[0] == Service::kInterfaceId && ((ra & 0xff) != 1 || offA != 0)) ctx.Fail("%s: own id: result %u offset %d", s.name, ra & 0xff, offA);
            if (s.slot == 0 && args[0] == 0x287fb697 && ((ra & 0xff) != 1 || offA != 4)) ctx.Fail("%s: system service id: result %u offset %d", s.name, ra & 0xff, offA);
            if (!freedA) ::operator delete(a);
            if (!freedB) ::operator delete(b);
        }
    }
}

DIFF_TEST(GZAllocatorService_Holder, 0x1be577)
{
    auto acquire = ctx.Original<void *(__thiscall *)(void *)>(0x1be577);
    auto release = ctx.Original<void(__thiscall *)(void *)>(0x1be5d1);
    FakeObject framework, result;
    void **frameworkGlobal = ctx.image.At<void *>(0x10c9378);
    void *savedFramework = *frameworkGlobal;
    for (int hasFramework = 0; hasFramework < 2; ++hasFramework) {
        for (int hasResult = 0; hasResult < 2; ++hasResult) {
            *frameworkGlobal = hasFramework ? &framework : nullptr;
            gQueryResult = hasResult ? &result : nullptr;
            void *ha = reinterpret_cast<void *>(0x5555), *hb = ha;
            gCalls.clear();
            void *ra = acquire(&ha);
            auto callsA = gCalls;
            gCalls.clear();
            void *rb = reinterpret_cast<AllocatorServiceHolder *>(&hb)->Acquire();
            ++ctx.cases;
            if (ra != &ha || rb != &hb || ha != hb || !SameCalls(callsA, gCalls)) ctx.Fail("Acquire framework %d result %d", hasFramework, hasResult);
        }
    }
    // Release: null, a normal release, and releases raising SEH and C++ exceptions (all swallowed).
    for (int mode = 0; mode < 4; ++mode) {
        void *ha = mode == 3 ? nullptr : &result, *hb = ha;
        gReleaseMode = mode == 3 ? 0 : mode;
        gCalls.clear();
        release(&ha);
        auto callsA = gCalls;
        gCalls.clear();
        reinterpret_cast<AllocatorServiceHolder *>(&hb)->Release();
        ++ctx.cases;
        if (ha != hb || !SameCalls(callsA, gCalls)) ctx.Fail("Release mode %d", mode);
    }
    gReleaseMode = 0;
    *frameworkGlobal = savedFramework;
}

DIFF_TEST(GZAllocatorService_Get, 0x1be693)
{
    auto get = ctx.Original<void *(__cdecl *)()>(0x1be693);
    void **frameworkGlobal = ctx.image.At<void *>(0x10c9378);
    void **instance = ctx.image.At<void *>(0x10cb5b4);
    int *guard = ctx.image.At<int>(0x10cb5b8);
    void *savedFramework = *frameworkGlobal, *savedInstance = *instance;
    int savedGuard = *guard;
    FakeObject framework, fromFramework, existing;

    // The framework answers: its object is returned and the holder's reference released.
    *frameworkGlobal = &framework;
    gQueryResult = &fromFramework;
    gCalls.clear();
    void *ra = get();
    auto callsA = gCalls;
    gCalls.clear();
    void *rb = GetAllocatorService();
    ++ctx.cases;
    if (ra != rb || ra != &fromFramework || !SameCalls(callsA, gCalls)) ctx.Fail("framework path");

    // No framework answer, static already initialised (guard at the lowest epoch): the instance.
    for (int hasFramework = 0; hasFramework < 2; ++hasFramework) {
        *frameworkGlobal = hasFramework ? &framework : nullptr;
        gQueryResult = nullptr;
        *instance = &existing;
        *guard = INT_MIN;
        gCalls.clear();
        ra = get();
        callsA = gCalls;
        gCalls.clear();
        rb = GetAllocatorService();
        ++ctx.cases;
        if (ra != rb || ra != &existing || !SameCalls(callsA, gCalls) || *guard != INT_MIN) ctx.Fail("initialised path, framework %d", hasFramework);
    }

    // First use: a new instance is constructed and AddRef'd once. The guard ends up holding each CRT's
    // own epoch counter and the exit handler goes to each CRT's atexit table, so those aren't
    // compared; the guard must just be marked initialised.
    *frameworkGlobal = nullptr;
    *instance = nullptr;
    *guard = 0;
    {
        AtexitRedirect redirect(ctx);
        ra = get();
    }
    ++ctx.cases;
    if (gAtexitCalls != 1) ctx.Fail("first use: the original registered %d exit handlers", gAtexitCalls);
    int guardA = *guard;
    // The new blocks' padding (+0x1d..0x1f) is whatever the heap left there, so it isn't compared.
    alignas(4) uint8_t objA[sizeof(cGZAllocatorService)];
    std::memcpy(objA, ra, sizeof(objA));
    *instance = nullptr;
    *guard = 0;
    rb = GetAllocatorService();
    ++ctx.cases;
    // The vtable words as codes (the original's instance has the game's vtables, ours our class's).
    const Maps maps;
    const std::string diff = maps.Compare(objA, rb, 0x1c);
    if (!ra || !rb || ra == rb || !diff.empty() || std::memcmp(objA + 0x1c, static_cast<uint8_t *>(rb) + 0x1c, 1) != 0) ctx.Fail("first use: objects differ at %s", diff.c_str());
    if (guardA == 0 || guardA == -1 || *guard == 0 || *guard == -1) ctx.Fail("first use: guard %d/%d", guardA, *guard);

    *frameworkGlobal = savedFramework;
    *instance = savedInstance;
    *guard = savedGuard;
}

// FreeThroughAllocatorService (0x197100): the cached service's Free (slot 4) gets the pointer; on first
// use the service comes from GetAllocatorService (here the framework answers with a fake service).
DIFF_TEST(GZAllocatorService_FreeThrough, 0x197100)
{
    auto freeThrough = ctx.Original<void(__stdcall *)(void *, uint32_t)>(0x197100);
    void **frameworkGlobal = ctx.image.At<void *>(0x10c9378);
    void **cached = ctx.image.At<void *>(0x10c95d4);
    int *guard = ctx.image.At<int>(0x10c95d8);
    void *savedFramework = *frameworkGlobal, *savedCached = *cached;
    int savedGuard = *guard;
    FakeObject framework, service, other;
    for (int firstUse = 0; firstUse < 2; ++firstUse) {
        for (int i = 0; i < 50; ++i) {
            void *mem = reinterpret_cast<void *>(static_cast<uintptr_t>(0x10000 + i * 0x1234));
            void *ra, *rb;
            std::vector<Call> callsA;
            for (int side = 0; side < 2; ++side) {
                *frameworkGlobal = &framework;
                gQueryResult = &service;
                if (firstUse) {
                    *cached = nullptr;
                    *guard = 0;
                } else {
                    *cached = &other; // already initialised: the framework isn't asked
                    *guard = INT_MIN;
                }
                gCalls.clear();
                if (side == 0) freeThrough(mem, 0x5a5a5a5a);
                else FreeThroughAllocatorService(mem, 0x5a5a5a5a);
                (side ? rb : ra) = *cached;
                if (side == 0) callsA = gCalls;
            }
            ++ctx.cases;
            if (ra != rb || !SameCalls(callsA, gCalls)) ctx.Fail("first use %d, case %d: cached %p/%p, calls differ", firstUse, i, ra, rb);
            if (firstUse && (*guard == 0 || *guard == -1)) ctx.Fail("first use: guard not marked initialised");
        }
    }
    *frameworkGlobal = savedFramework;
    *cached = savedCached;
    *guard = savedGuard;
}

// GZServiceAllocatorBase::Deallocate (0x1d0903): 0x197100's body as a __thiscall member (ret 8, `this`
// unused), compared the same way. `this` is a random value: neither side may read it.
DIFF_TEST(GZAllocatorService_AllocatorDeallocate, 0x1d0903)
{
    auto deallocate = ctx.Original<void(__thiscall *)(void *, void *, uint32_t)>(0x1d0903);
    void **frameworkGlobal = ctx.image.At<void *>(0x10c9378);
    void **cached = ctx.image.At<void *>(0x10c95d4);
    int *guard = ctx.image.At<int>(0x10c95d8);
    void *savedFramework = *frameworkGlobal, *savedCached = *cached;
    int savedGuard = *guard;
    FakeObject framework, service, other;
    for (int firstUse = 0; firstUse < 2; ++firstUse) {
        for (int i = 0; i < 50; ++i) {
            void *mem = reinterpret_cast<void *>(static_cast<uintptr_t>(0x10000 + i * 0x1234));
            auto *self = reinterpret_cast<GZServiceAllocatorBase *>(static_cast<uintptr_t>(0x13 + i * 0x777));
            uint32_t count = static_cast<uint32_t>(i * 37);
            void *ra, *rb;
            std::vector<Call> callsA;
            for (int side = 0; side < 2; ++side) {
                *frameworkGlobal = &framework;
                gQueryResult = &service;
                if (firstUse) {
                    *cached = nullptr;
                    *guard = 0;
                } else {
                    *cached = &other;
                    *guard = INT_MIN;
                }
                gCalls.clear();
                if (side == 0) deallocate(self, mem, count);
                else self->Deallocate(mem, count);
                (side ? rb : ra) = *cached;
                if (side == 0) callsA = gCalls;
            }
            ++ctx.cases;
            if (ra != rb || !SameCalls(callsA, gCalls)) ctx.Fail("first use %d, case %d: cached %p/%p, calls differ", firstUse, i, ra, rb);
            if (firstUse && (*guard == 0 || *guard == -1)) ctx.Fail("first use: guard not marked initialised");
        }
    }
    *frameworkGlobal = savedFramework;
    *cached = savedCached;
    *guard = savedGuard;
}
