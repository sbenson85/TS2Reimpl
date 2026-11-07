// Differential tests for nGZEvent::cEventSystemService in the standalone shape (lane KD). The original side is an object from
// the original constructor 0x1a84d5 or creator 0x1a8553 (the game's vtables), ours one from our constructor (the hook's
// adapter) or Create (our vtables); every slot is called through the side's own vtable, objects compared word by word with
// the vtable words as codes (tests/service_shape.h).
//
// RVAs: the creator 0x1a8553, the constructor 0x1a84d5, the destructor 0x1a850a, the deleting destructor 0x1a852f and its +8
// thunk 0x1a8527, QueryInterface 0x1a856f and its thunks 0x1a85a0 (+4) / 0x1a85a8 (+8), slot 3: the jump 0x1a854e to
// 0x1aad98 (the static connection manager 0x10cadd8, guard 0x10cae1c); the folded AddRef 0x145240 and Release 0x1a85b0 (and
// their thunks) and cRZSystemService's slots.
#include "difftest.h"
#include "free_recorder.h"
#include "code_patch.h"
#include "service_shape.h"
#include "gz/cConnectionManager.h"
#include "gz/cEventSystemService.h"

#include <new>

#include <windows.h>

#include <climits>
#include <cstring>
#include <iterator>
#include <random>
#include <string>

namespace {

using Service = nGZEvent::cEventSystemService;
constexpr uint32_t kCreate = 0x1a8553, kCtor = 0x1a84d5, kDtor = 0x1a850a, kDeleting = 0x1a852f, kDeletingThunk = 0x1a8527;
constexpr uint32_t kFaceOffset[3] = {0, 4, 8};
constexpr uint32_t kGameVt[3] = {Service::kVTable, Service::kSystemServiceVTable, Service::kUnknownVTable};

struct Maps {
    svc::CodeMap a, b;
    Maps()
    {
        alignas(4) static uint8_t buffer[sizeof(Service)];
        auto *ours = nGZEvent::cEventSystemServiceAdapters::Construct(buffer, nullptr);
        for (uint32_t f = 0; f < 3; ++f) {
            a.AddRva(kGameVt[f], f);
            b.AddRva(kGameVt[f], f);
            b.Add(svc::VTableAt(ours, kFaceOffset[f]), f);
        }
        ours->~cEventSystemService();
        svc::AddServiceBaseCodes(a, b, 0x10);
    }
    std::string Compare(const void *x, const void *y, uint32_t size) const { return svc::Diff(svc::Words(x, size, a), svc::Words(y, size, b)); }
};

template <size_t N> void FillRandom(uint8_t (&a)[N], uint8_t (&b)[N], std::mt19937 &rng)
{
    for (size_t i = 0; i < N; ++i) a[i] = b[i] = static_cast<uint8_t>(rng());
}

uint8_t *NewPair(difftest::Context &ctx, uint8_t *&b)
{
    auto *a = static_cast<uint8_t *>(::operator new(sizeof(Service)));
    b = static_cast<uint8_t *>(::operator new(sizeof(Service)));
    std::memset(a, 0xcd, sizeof(Service));
    std::memset(b, 0xcd, sizeof(Service));
    ctx.Original<void *(__thiscall *)(void *)>(kCtor)(a);
    nGZEvent::cEventSystemServiceAdapters::Construct(b, nullptr);
    return a;
}

void FreeOne(void *object)
{
    const uint32_t flags[1] = {1};
    svc::CallWords(svc::VTableAt(object, 8)[3], static_cast<uint8_t *>(object) + 8, 1, flags);
}

} // namespace

// Random-filled 0x28-byte buffers with the object at +4, so the words around it are compared too.
DIFF_TEST(cEventSystemService_Construct, 0x1a84d5)
{
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(kCtor);
    Maps maps;
    std::mt19937 rng(0xE5);
    for (int i = 0; i < 500; ++i) {
        alignas(4) uint8_t a[0x28], b[0x28];
        FillRandom(a, b, rng);
        void *ra = construct(a + 4);
        void *rb = nGZEvent::cEventSystemServiceAdapters::Construct(b + 4, nullptr);
        ++ctx.cases;
        const std::string diff = maps.Compare(a, b, sizeof(a));
        if (ra != a + 4 || rb != b + 4 || !diff.empty()) ctx.Fail("result or object differs at %s", diff.c_str());
        for (uint32_t f = 0; f < 3; ++f)
            if (svc::VTableAt(b + 4, kFaceOffset[f]) == GameAddress<void *>(kGameVt[f])) ctx.Fail("our face %u has the game's vtable", f);
    }
}

// The creator: a new object of 0x20 bytes on each side (the padding byte after the running flag compared too: both leave
// it as the heap did, so it is masked), or null when operator new fails (not reachable here: the original's throws).
DIFF_TEST(cEventSystemService_Create, 0x1a8553)
{
    auto create = ctx.Original<void *(__cdecl *)()>(kCreate);
    Maps maps;
    for (int i = 0; i < 20; ++i) {
        auto *a = static_cast<uint8_t *>(create());
        auto *b = reinterpret_cast<uint8_t *>(Service::Create());
        ++ctx.cases;
        if (!a || !b) {
            ctx.Fail("null object %p/%p", a, b);
            continue;
        }
        a[0x1d] = a[0x1e] = a[0x1f] = b[0x1d] = b[0x1e] = b[0x1f] = 0; // never written
        const std::string diff = maps.Compare(a, b, sizeof(Service));
        if (!diff.empty()) ctx.Fail("object differs at %s", diff.c_str());
        FreeOne(a);
        FreeOne(b);
    }
}

// The destructor (called directly; ours: our destructor, no dispatch), the deleting destructor (on the primary pointer) and
// its +8 thunk (ours: our +8 vtable's slot 3 for both), flags 0..3, random service fields.
DIFF_TEST(cEventSystemService_Destruct, 0x1a850a)
{
    auto destruct = ctx.Original<void(__thiscall *)(void *)>(kDtor);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeletingThunk);
    Maps maps;
    std::mt19937 rng(0xE6);
    for (int run = 0; run < 60; ++run) {
        const int route = run % 3;
        const uint32_t flags = route == 0 ? 0 : (run / 3) % 4;
        uint8_t *b = nullptr;
        uint8_t *a = NewPair(ctx, b);
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
            reinterpret_cast<Service *>(b)->Service::~cEventSystemService();
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

// Every slot of the three faces but the deleting destructor (the destructor test). Slot 3 runs with the connection
// manager already built (the guard at the lowest epoch): it returns the static's address and calls nothing.
const Slot kSlots[] = {
    {0, 0, 2, svc::kByte, "QueryInterface"},       {0, 1, 0, svc::kWord, "AddRef"},
    {0, 2, 0, svc::kWord, "Release"},              {0, 3, 0, svc::kWord, "GetConnectionManager"},
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

constexpr uint32_t kIds[] = {Service::kInterfaceId, Service::kServiceId, Service::kClassId, 0x287fb697, 1, 0, 2, 0xffffffff, 0x1b1e8959, 0x1b1e895b};

} // namespace

// Through each side's own vtable (a Release at a count of 1 or below deletes the object). QueryInterface also with a null
// out pointer (every third run): false, nothing touched.
DIFF_TEST(cEventSystemService_Slots, 0x1a856f)
{
    Maps maps;
    std::mt19937 rng(0xE7);
    int *guard = ctx.image.At<int>(Service::kConnectionManagerGuard);
    const int savedGuard = *guard;
    *guard = INT_MIN; // built: slot 3 only returns the address
    void *const kUntouched = reinterpret_cast<void *>(0x5a5a5a5a);
    constexpr unsigned kSlotCount[3] = {4, 13, 6};
    for (const Slot &s : kSlots) {
        if (s.slot >= kSlotCount[s.face / 4]) ctx.Fail("%s: slot out of range", s.name);
        const int runs = s.slot == 0 ? 3 * static_cast<int>(std::size(kIds)) : 24;
        for (int run = 0; run < runs; ++run) {
            uint8_t *b = nullptr;
            uint8_t *a = NewPair(ctx, b);
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
            const bool nullOut = s.slot == 0 && run % 3 == 2;
            if (s.slot == 0) args[0] = kIds[(run / 3) % std::size(kIds)];
            void *outA = kUntouched, *outB = kUntouched;
            uint32_t argsA[2] = {args[0], s.slot == 0 ? (nullOut ? 0 : svc::P(&outA)) : args[1]};
            uint32_t argsB[2] = {args[0], s.slot == 0 ? (nullOut ? 0 : svc::P(&outB)) : args[1]};
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
            if (s.face == 0 && s.slot == 3 && ra != svc::P(GameAddress<void>(Service::kConnectionManager))) ctx.Fail("slot 3: not the static");
            if (freedA != freedB) ctx.Fail("%s run %d: freed %d/%d", s.name, run, freedA, freedB);
            const int32_t offA = outA == kUntouched ? -1 : static_cast<int32_t>(svc::P(outA) - svc::P(a));
            const int32_t offB = outB == kUntouched ? -1 : static_cast<int32_t>(svc::P(outB) - svc::P(b));
            if (offA != offB) ctx.Fail("%s run %d id 0x%x: out offset %d/%d", s.name, run, args[0], offA, offB);
            if (!freedA && !freedB) {
                const std::string diff = maps.Compare(a, b, sizeof(Service));
                if (!diff.empty()) ctx.Fail("%s run %d: object differs at %s", s.name, run, diff.c_str());
            }
            if (s.slot == 0 && !nullOut && args[0] == Service::kInterfaceId && ((ra & 0xff) != 1 || offA != 0)) ctx.Fail("%s: own id: result %u offset %d", s.name, ra & 0xff, offA);
            if (s.slot == 0 && nullOut && (ra & 0xff) != 0) ctx.Fail("%s: null out: result %u", s.name, ra & 0xff);
            if (!freedA) ::operator delete(a);
            if (!freedB) ::operator delete(b);
        }
    }
    *guard = savedGuard;
}

namespace {

// The original's atexit (library code, RVA 0x824cbe) works on its CRT's exit table, which the harness never initialises;
// while the original's first use runs, the image's atexit is redirected to this stub (5-byte jmp patched over its entry,
// restored afterwards), which records the handler. Ours registers with our CRT (not observable here; the handler then
// destroys the static at exit, so the test leaves it built).
int gAtexitCalls = 0;
void (*gAtexitHandler)() = nullptr;
int __cdecl StubAtexit(void (*handler)())
{
    ++gAtexitCalls;
    gAtexitHandler = handler;
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

// The connection manager's constructor (0x1aa630, hooked to our adapter): both sides' calls land here, which counts them
// and builds our manager at the static (so our exit handler destroys a valid object).
int gManagerBuilds = 0;
void *gManagerBuiltAt = nullptr;
nGZEvent::cConnectionManager *__fastcall StandManager(void *memory, void *)
{
    ++gManagerBuilds;
    gManagerBuiltAt = memory;
    return ::new (memory) nGZEvent::cConnectionManager;
}

} // namespace

// Slot 3 on first use (the guard at 0): the connection manager is built at the static (the constructor 0x1aa630 on the
// original side, our adapter on ours, both stood in by StandManager), the guard marked initialised, the static's address
// returned; the original registers its exit handler 0xd90644 once. Each side's builds are counted and the static's first
// word (its vtable) is compared; the rest holds each run's heap pointers.
DIFF_TEST(cEventSystemService_ConnectionManagerFirstUse, 0x1aad98)
{
    shape::StandIn construct(0x1aa630, &nGZEvent::cConnectionManagerAdapters::Construct, &StandManager);
    int *guard = ctx.image.At<int>(Service::kConnectionManagerGuard);
    void *const manager = ctx.image.At<void>(Service::kConnectionManager);
    uint8_t *b = nullptr;
    uint8_t *a = NewPair(ctx, b);
    *guard = 0;
    *static_cast<uint32_t *>(manager) = 0;
    gManagerBuilds = 0;
    gManagerBuiltAt = nullptr;
    uint32_t ra = 0;
    {
        AtexitRedirect redirect(ctx);
        ra = svc::CallWords(GameAddress<void *>(Service::kVTable)[3], a, 0, nullptr);
    }
    const int guardA = *guard;
    const uint32_t vtableA = *static_cast<uint32_t *>(manager);
    const int buildsA = gManagerBuilds;
    const void *const builtA = gManagerBuiltAt;
    *guard = 0;
    *static_cast<uint32_t *>(manager) = 0;
    gManagerBuilds = 0;
    gManagerBuiltAt = nullptr;
    const uint32_t rb = svc::CallWords(svc::VTableAt(b, 0)[3], b, 0, nullptr);
    const uint32_t vtableB = *static_cast<uint32_t *>(manager);
    ++ctx.cases;
    if (buildsA != 1 || gManagerBuilds != 1 || builtA != manager || gManagerBuiltAt != manager)
        ctx.Fail("manager builds %d/%d at %p/%p", buildsA, gManagerBuilds, builtA, gManagerBuiltAt);
    if (ra != svc::P(manager) || rb != ra) ctx.Fail("result 0x%x/0x%x", ra, rb);
    if (gAtexitCalls != 1 || gAtexitHandler != GameAddress<void()>(0xd90644)) ctx.Fail("the original registered %d exit handlers", gAtexitCalls);
    if (guardA == 0 || guardA == -1 || *guard == 0 || *guard == -1) ctx.Fail("guard %d/%d", guardA, *guard);
    if (vtableA == 0 || vtableA != vtableB) ctx.Fail("the static's vtable 0x%x/0x%x", vtableA, vtableB);
    // Built: a second call on our side returns the static without building it again.
    *static_cast<uint32_t *>(manager) ^= 0x1;
    const uint32_t again = svc::CallWords(svc::VTableAt(b, 0)[3], b, 0, nullptr);
    ++ctx.cases;
    if (again != rb || *static_cast<uint32_t *>(manager) != (vtableB ^ 0x1)) ctx.Fail("second call rebuilt the static");
    *static_cast<uint32_t *>(manager) ^= 0x1;
    FreeOne(a);
    FreeOne(b);
}
