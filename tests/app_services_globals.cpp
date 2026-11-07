// Differential tests for cTSGlobals (src/ts/cTSGlobals.*) in the standalone shape (lane KE): the original side is an object
// from the original constructor 0xc2c4cf (the game's vtables), ours one from our constructor (the hook's adapter: our
// vtables); every slot is called through the side's own vtable, and objects are compared word by word with the vtable
// words mapped to codes (tests/service_shape.h).
//
// The objects the fields point to come from a pool of fakes whose slots log "slot@id"; Init talks to a fake framework that
// the fake director (the game's static at 0x10c9390, fast path of 0x14491b) hands out.
//
// RVAs: the constructor 0xc2c4cf, the destructor 0xc2c58b, the deleting destructor 0xc2c819 and its +8 thunk 0xc2c811,
// QueryInterface 0xc2c948 and its thunks 0xc2c96a (+4) / 0xc2c972 (+8), Init 0xc2c83b, Shutdown 0xc2cac9; the setters
// 0xc2c9ee 0xc2c9b6 0x760d95 0xc2c9c6 0xc2c9d6 0xc2c9ce 0xc2c998 0xc2c9ab 0xc2c9e6 0xc2c97a 0xc2c9de 0xc2cac1 0xc2c9be
// 0xc2c9a0 0xc2c982 0xc2c98d; the folded setters 0x5dabb0 0x230b9f 0x73c475 0x96de3f 0xb16f78 0x230b5d (0x210999 is
// cTSAudioEvent's) and getters 0x22fa70 0x239670 0x238f10 0x231570 0x24bde0 (the 4-byte ones are leaves), all through
// our vtable; slots 31 and 56 (0xc2cba3, 0xc2c9f6: tests/app_services2_cheats.cpp) forward to the AppServices2 bodies.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "service_shape.h"
#include "app/AppServices2.h"
#include "ts/cTSGlobals.h"

#include <intrin.h>
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kCtor = 0xc2c4cf, kDtor = 0xc2c58b, kDeleting = 0xc2c819, kDeletingThunk = 0xc2c811;
constexpr uint32_t kInit = 0xc2c83b, kShutdown = 0xc2cac9, kTestingCheats = 0xc2cba3, kSetTestingCheats = 0xc2c9f6;
constexpr uint32_t kSize = cTSGlobals::kSize;

std::vector<std::string> gLog;
unsigned char *gBase = nullptr;

void Log(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.emplace_back(buf);
}

std::string Joined()
{
    std::string out;
    for (const std::string &s : gLog) out += s + "|";
    return out;
}

struct Fake {
    void **vtable;
    int id;
};

constexpr int kPool = 16;
Fake gPool[kPool];
void *gFakeVTable[32];

template <int N> uint32_t __fastcall FakeSlot(Fake *self, void *)
{
    Log("%d@%d", N, self->id);
    if (N == 17) return reinterpret_cast<uint32_t>(&gPool[(self->id + 3) % kPool]); // the result Init assigns
    return 0x77;
}

template <int... N> void FillSlots(std::integer_sequence<int, N...>)
{
    ((gFakeVTable[N] = reinterpret_cast<void *>(&FakeSlot<N>)), ...);
}

void InitPool()
{
    FillSlots(std::make_integer_sequence<int, 24>());
    for (int i = 0; i < kPool; ++i) {
        gPool[i].vtable = gFakeVTable;
        gPool[i].id = i;
    }
}

// The fake framework: slot 3 logs its arguments and the field offset, and stores a pool object (or
// null) in the field. gNullMask picks null per call.
struct FakeFramework {
    void **vtable;
};
void *gFrameworkVTable[16];
FakeFramework gFramework;
uint32_t gNullMask = 0;
int gFetchCount = 0;

void __fastcall FakeFetch(FakeFramework *, void *, uint32_t a, uint32_t b, void **field)
{
    Log("fetch %x %x @%d", a, b, static_cast<int>(reinterpret_cast<unsigned char *>(field) - gBase));
    bool isNull = (gNullMask >> gFetchCount) & 1;
    ++gFetchCount;
    *field = isNull ? nullptr : &gPool[(a ^ b) % kPool];
}

void *gDirectorVTable[16];
void *__fastcall FakeFrameworkGetter(void *, void *)
{
    return &gFramework;
}

int *ImageTlsBlock(difftest::Context &ctx)
{
    uint32_t index = *ctx.image.At<uint32_t>(0x10df814);
    auto **array = reinterpret_cast<int **>(__readfsdword(0x2c));
    return array[index];
}

// Makes the game's director static answer slot 12 with the fake framework, on the fast path of
// 0x14491b (guard not above the thread's epoch). Restores everything on exit.
struct DirectorFake {
    difftest::Context &ctx;
    void *savedVTable;
    int savedGuard;
    int savedEpoch;
    explicit DirectorFake(difftest::Context &c) : ctx(c)
    {
        gFramework.vtable = gFrameworkVTable;
        gFrameworkVTable[3] = reinterpret_cast<void *>(&FakeFetch);
        gDirectorVTable[12] = reinterpret_cast<void *>(&FakeFrameworkGetter);
        void **object = ctx.image.At<void *>(0x10c9390);
        savedVTable = *object;
        *object = gDirectorVTable;
        savedGuard = *ctx.image.At<int>(0x10c93f4);
        *ctx.image.At<int>(0x10c93f4) = 5;
        int *tls = ImageTlsBlock(ctx);
        savedEpoch = tls[1];
        tls[1] = 0x7fffffff;
    }
    ~DirectorFake()
    {
        *ctx.image.At<void *>(0x10c9390) = savedVTable;
        *ctx.image.At<int>(0x10c93f4) = savedGuard;
        ImageTlsBlock(ctx)[1] = savedEpoch;
    }
};

// The frees of the call under test (as tests/gz_registry.cpp): BeginFrees() right before it, Live() after.
std::optional<difftest::FreeRecorder> gFrees;
void BeginFrees()
{
    gFrees.reset();
    gFrees.emplace();
}

bool Live(const void *p)
{
    gFrees->Stop();
    return !gFrees->Freed(p);
}

constexpr uint32_t kFaceOffset[3] = {0, 4, 8};
constexpr uint32_t kGameVt[3] = {cTSGlobals::kVTable, cTSGlobals::kSystemServiceVTable, cTSGlobals::kUnknownVTable};
constexpr unsigned kSlotCount[3] = {59, 13, 6};

unsigned char *NewBlock(int fill)
{
    auto *p = static_cast<unsigned char *>(::operator new(kSize));
    std::memset(p, fill, kSize);
    return p;
}

// Each side's code map: the game's and our vtable of each face as one code per face; the destroyed service part's.
struct Maps {
    svc::CodeMap a, b;
    Maps()
    {
        unsigned char *ours = NewBlock(0);
        cTSGlobalsAdapters::Construct(ours, nullptr);
        for (uint32_t f = 0; f < 3; ++f) {
            a.AddRva(kGameVt[f], f);
            b.AddRva(kGameVt[f], f);
            b.Add(svc::VTableAt(ours, kFaceOffset[f]), f);
        }
        ::operator delete(ours); // every field null: nothing to release
        svc::AddServiceBaseCodes(a, b, 0x10);
    }
    std::string Compare(const void *x, const void *y) const { return svc::Diff(svc::Words(x, kSize, a), svc::Words(y, kSize, b)); }
};

struct Pair {
    unsigned char *a;
    unsigned char *b;
};

// The original constructor's object and ours, then the same random state on both: fields null (one in `nullOneIn`) or pool
// objects, the byte at +0x90 (0, 1, 0x80 or random), the service part's count (0..5 or random), id, priorities and running
// byte (0, 0x80 or random, with random padding).
Pair MakePair(difftest::Context &ctx, std::mt19937 &rng, int nullOneIn, int run)
{
    Pair p{NewBlock(0xCD), NewBlock(0xCD)};
    ctx.Original<void *(__thiscall *)(void *)>(kCtor)(p.a);
    cTSGlobalsAdapters::Construct(p.b, nullptr);
    for (uint32_t off = 0x20; off <= 0x94; off += 4) {
        if (off == 0x90) continue;
        void *v = static_cast<int>(rng() % nullOneIn) == 0 ? nullptr : &gPool[rng() % kPool];
        std::memcpy(p.a + off, &v, 4);
        std::memcpy(p.b + off, &v, 4);
    }
    const uint8_t flag = run % 4 == 0 ? 0 : run % 4 == 1 ? 1 : run % 4 == 2 ? 0x80 : static_cast<uint8_t>(rng());
    p.a[0x90] = p.b[0x90] = flag;
    for (uint32_t offset = 0xc; offset < 0x20; offset += 4) {
        uint32_t value = rng();
        if (offset == 0xc && run % 8 < 6) value = static_cast<uint32_t>(run % 6);
        if (offset == 0x1c && run % 4 == 0) value &= 0xffffff00;
        if (offset == 0x1c && run % 4 == 1) value = (value & 0xffffff00) | 0x80;
        std::memcpy(p.a + offset, &value, 4);
        std::memcpy(p.b + offset, &value, 4);
    }
    return p;
}

void Free(Pair p)
{
    ::operator delete(p.a);
    ::operator delete(p.b);
}

void *Slot(const unsigned char *object, unsigned face, unsigned slot)
{
    return svc::VTableAt(object, kFaceOffset[face])[slot];
}

} // namespace

DIFF_TEST(AppServices_TSGlobals_Construct, kCtor)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(kCtor);
    Maps maps;
    for (int fill : {0xCD, 0x00, 0xFF, 0x5A}) {
        unsigned char *a = NewBlock(fill), *b = NewBlock(fill);
        void *ra = orig(a);
        void *rb = cTSGlobalsAdapters::Construct(b, nullptr);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("fill %02x: return value is not this", fill);
        const std::string diff = maps.Compare(a, b);
        if (!diff.empty()) ctx.Fail("fill %02x: object differs at %s", fill, diff.c_str());
        for (uint32_t f = 0; f < 3; ++f) {
            if (svc::VTableAt(a, kFaceOffset[f]) != GameAddress<void *>(kGameVt[f])) ctx.Fail("fill %02x: original's face %u vtable", fill, f);
            if (svc::VTableAt(b, kFaceOffset[f]) == GameAddress<void *>(kGameVt[f])) ctx.Fail("fill %02x: our face %u has the game's vtable", fill, f);
        }
        auto *g = reinterpret_cast<cTSGlobals *>(b);
        if (g->mServiceId != cTSGlobals::kServiceId || g->mPriority != cTSGlobals::kPriority || g->mTickPriority != cTSGlobals::kPriority)
            ctx.Fail("fill %02x: service id or priorities", fill);
        if (g->mField20.mObject || g->mGlobalInventory.mObject || g->mField94.mObject || g->mFlag90) ctx.Fail("fill %02x: fields not cleared", fill);
        ::operator delete(a);
        ::operator delete(b);
    }
}

// The destructor 0xc2c58b (called directly; ours: our destructor, no dispatch), the deleting destructor 0xc2c819 (on the
// object) and its +8 thunk 0xc2c811 (ours: our +8 vtable's slot 3 for both), flags 0..3; fields null or pool objects (their
// AddRef / Release / slot 4 / slot 16 calls logged, Shutdown's included), the object's frees and every word compared.
DIFF_TEST(AppServices_TSGlobals_Destruct, kDtor)
{
    InitPool();
    Maps maps;
    auto dtor = ctx.Original<void(__thiscall *)(void *)>(kDtor);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeletingThunk);
    std::mt19937 rng(0xc2c58b);
    for (int run = 0; run < 300; ++run) {
        const int route = run % 3;
        const uint32_t flags = route == 0 ? 0 : (run / 3) % 4;
        Pair p = MakePair(ctx, rng, 1 + run % 5, run);
        gLog.clear();
        BeginFrees();
        void *ra = route == 0 ? (dtor(p.a), p.a) : route == 1 ? deleting(p.a, flags) : thunk(p.a + 8, flags);
        const bool freedA = !Live(p.a);
        const std::string logA = Joined();
        gLog.clear();
        BeginFrees();
        void *rb = p.b;
        if (route == 0) {
            reinterpret_cast<cTSGlobals *>(p.b)->cTSGlobals::~cTSGlobals();
        } else {
            const uint32_t args[1] = {flags};
            rb = reinterpret_cast<void *>(svc::CallWords(Slot(p.b, 2, 3), p.b + 8, 1, args));
        }
        const bool freedB = !Live(p.b);
        const std::string logB = Joined();
        ++ctx.cases;
        if (ra != p.a || rb != p.b) ctx.Fail("run %d route %d: return value is not the object", run, route);
        if (freedA != freedB || freedA != ((flags & 1) != 0)) ctx.Fail("run %d route %d flags %u: object freed %d / %d", run, route, flags, freedA, freedB);
        if (logA != logB) ctx.Fail("run %d route %d: calls differ\n  original: %s\n  ours:     %s", run, route, logA.substr(0, 300).c_str(), logB.substr(0, 300).c_str());
        if (!freedA && !freedB) {
            const std::string diff = maps.Compare(p.a, p.b);
            if (!diff.empty()) ctx.Fail("run %d route %d: state after destruction differs at %s", run, route, diff.c_str());
        }
        if (!freedA) ::operator delete(p.a);
        if (!freedB) ::operator delete(p.b);
    }
}

namespace {

// Runs the original's slot body at `rva` on a (this = a + 4) and our +4 vtable's `slot` on b, each with its own log; compares
// the low byte of the results, the logs and every word.
void RunSystemSlot(difftest::Context &ctx, const Maps &maps, const char *what, int run, Pair p, uint32_t rva, unsigned slot)
{
    gLog.clear();
    gBase = p.a;
    gFetchCount = 0;
    const uint32_t ra = ctx.Original<uint32_t(__thiscall *)(void *)>(rva)(p.a + 4);
    const std::string logA = Joined();
    gLog.clear();
    gBase = p.b;
    gFetchCount = 0;
    const uint32_t rb = svc::CallWords(Slot(p.b, 1, slot), p.b + 4, 0, nullptr);
    const std::string logB = Joined();
    ++ctx.cases;
    const std::string diff = maps.Compare(p.a, p.b);
    if ((ra & 0xff) != (rb & 0xff) || logA != logB || !diff.empty())
        ctx.Fail("%s run %d: result %x/%x, object %s\n  original: %s\n  ours:     %s", what, run, ra & 0xff, rb & 0xff, diff.empty() ? "same" : diff.c_str(),
                 logA.substr(0, 300).c_str(), logB.substr(0, 300).c_str());
}

} // namespace

DIFF_TEST(AppServices_TSGlobals_Init, kInit)
{
    InitPool();
    Maps maps;
    DirectorFake director(ctx);
    std::mt19937 rng(0xc2c83b);
    for (int run = 0; run < 400; ++run) {
        Pair p = MakePair(ctx, rng, 1 + run % 4, run);
        gNullMask = run < 40 ? (run % 2 ? 0 : 0x1f) : static_cast<uint32_t>(rng());
        if (run == 1) gNullMask = 0;
        RunSystemSlot(ctx, maps, "Init", run, p, kInit, 8);
        Free(p);
    }
}

DIFF_TEST(AppServices_TSGlobals_Shutdown, kShutdown)
{
    InitPool();
    Maps maps;
    std::mt19937 rng(0xc2cac9);
    for (int run = 0; run < 400; ++run) {
        Pair p = MakePair(ctx, rng, 1 + run % 4, run);
        RunSystemSlot(ctx, maps, "Shutdown", run, p, kShutdown, 9);
        Free(p);
    }
}

namespace {

// What a slot takes and returns: nothing, an interface id and an out pointer (QueryInterface), a pool object or null (a
// setter), a raw dword (the byte setters, the service's id and counters); the result as a byte, a dword (a held pointer or
// a count) or the address of a field (compared as its offset in the object).
enum Args : uint8_t { kNone, kQuery, kObject, kRaw };
enum Result : uint8_t { kByte, kWord, kField, kVoid };

struct SlotCase {
    unsigned face, slot;
    Args args;
    Result result;
};

// Every slot but the deleting destructor (the destructor test), Init and Shutdown (their tests) and 31 / 56 (the forwarding
// test).
std::vector<SlotCase> SlotCases()
{
    std::vector<SlotCase> cases = {{0, 0, kQuery, kByte}, {0, 1, kNone, kWord}, {0, 2, kNone, kWord}};
    for (unsigned s = 3; s <= 30; ++s) cases.push_back({0, s, kNone, kWord});
    cases.push_back({0, 32, kNone, kByte});
    cases.push_back({0, 33, kNone, kWord});
    for (unsigned s = 34; s <= 55; ++s) cases.push_back({0, s, kObject, kField});
    cases.push_back({0, 57, kRaw, kVoid});
    cases.push_back({0, 58, kObject, kField});
    const SlotCase system[] = {{1, 0, kQuery, kByte}, {1, 1, kNone, kWord}, {1, 2, kNone, kWord}, {1, 3, kNone, kWord},
                               {1, 4, kRaw, kVoid},   {1, 5, kNone, kWord}, {1, 6, kNone, kByte}, {1, 7, kRaw, kVoid},
                               {1, 10, kRaw, kByte},  {1, 11, kRaw, kByte}, {1, 12, kNone, kWord},
                               {2, 0, kQuery, kByte}, {2, 1, kNone, kWord}, {2, 2, kNone, kWord}, {2, 4, kNone, kWord},
                               {2, 5, kNone, kWord}};
    cases.insert(cases.end(), std::begin(system), std::end(system));
    return cases;
}

// The field each setter (slots 34..55, 58) assigns.
uint32_t SetterField(unsigned slot)
{
    static const uint8_t kFields[] = {0x20, 0x24, 0x28, 0x30, 0x34, 0x38, 0x68, 0x6c, 0x60, 0x64, 0x3c, 0x2c,
                                      0x8c, 0x48, 0x4c, 0x50, 0x70, 0x74, 0x78, 0x80, 0x84, 0x88};
    return slot == 58 ? 0x94 : kFields[slot - 34];
}

constexpr uint32_t kIds[] = {cTSGlobals::kInterfaceId, 0x287fb697, 1, 0, 2, 0x7a3e566e, 0x7a3e566c, 0xffffffff, 0x089b7dc8, 0x7a3e667d};

} // namespace

// Every slot of the three faces but those above, through each side's own vtable: results, out-parameters, the pool
// objects' calls, frees (a Release at a count of 1 or below deletes the object, its destructor's calls logged) and every
// word. The setters get a pool object, null, or the object already held.
DIFF_TEST(AppServices_TSGlobals_Slots, 0xc2c948)
{
    InitPool();
    Maps maps;
    std::mt19937 rng(0xc2c948);
    void *const kUntouched = reinterpret_cast<void *>(0x5a5a5a5a);
    for (const SlotCase &c : SlotCases()) {
        if (c.slot >= kSlotCount[c.face]) ctx.Fail("face %u slot %u: out of range", c.face, c.slot);
        const int runs = c.args == kQuery ? 2 * static_cast<int>(std::size(kIds)) : 24;
        for (int run = 0; run < runs; ++run) {
            Pair p = MakePair(ctx, rng, 1 + run % 3, run);
            uint32_t arg = rng();
            if (c.args == kQuery) arg = kIds[run % std::size(kIds)];
            if (c.args == kObject) {
                void *object = run % 5 == 0 ? nullptr : &gPool[rng() % kPool];
                if (run % 7 == 0) std::memcpy(&object, p.a + SetterField(c.slot), 4); // what the field holds already
                arg = svc::P(object);
            }
            void *outA = kUntouched, *outB = kUntouched;
            const uint32_t argsA[2] = {arg, svc::P(&outA)}, argsB[2] = {arg, svc::P(&outB)};
            const unsigned words = c.args == kNone ? 0 : c.args == kQuery ? 2 : 1;
            gLog.clear();
            BeginFrees();
            const uint32_t ra = svc::CallWords(GameAddress<void *>(kGameVt[c.face])[c.slot], p.a + kFaceOffset[c.face], words, argsA);
            const bool freedA = !Live(p.a);
            const std::string logA = Joined();
            gLog.clear();
            BeginFrees();
            const uint32_t rb = svc::CallWords(Slot(p.b, c.face, c.slot), p.b + kFaceOffset[c.face], words, argsB);
            const bool freedB = !Live(p.b);
            const std::string logB = Joined();
            ++ctx.cases;
            uint32_t va = ra, vb = rb;
            if (c.result == kByte) va &= 0xff, vb &= 0xff;
            if (c.result == kVoid) va = vb = 0;
            if (c.result == kField) va -= svc::P(p.a), vb -= svc::P(p.b);
            if (va != vb) ctx.Fail("face %u slot %u run %d: result 0x%x / 0x%x", c.face, c.slot, run, va, vb);
            if (freedA != freedB) ctx.Fail("face %u slot %u run %d: freed %d / %d", c.face, c.slot, run, freedA, freedB);
            if (logA != logB) ctx.Fail("face %u slot %u run %d: calls differ\n  original: %s\n  ours:     %s", c.face, c.slot, run, logA.substr(0, 300).c_str(), logB.substr(0, 300).c_str());
            const int32_t offA = outA == kUntouched ? -1 : static_cast<int32_t>(svc::P(outA) - svc::P(p.a));
            const int32_t offB = outB == kUntouched ? -1 : static_cast<int32_t>(svc::P(outB) - svc::P(p.b));
            if (offA != offB) ctx.Fail("face %u slot %u id 0x%x: out offset %d / %d", c.face, c.slot, arg, offA, offB);
            if (c.args == kQuery && arg == cTSGlobals::kInterfaceId && ((ra & 0xff) != 1 || offA != 0)) ctx.Fail("face %u: globals id: result %u offset %d", c.face, ra & 0xff, offA);
            if (c.args == kQuery && arg == 0x287fb697 && ((ra & 0xff) != 1 || offA != 4)) ctx.Fail("face %u: system service id: result %u offset %d", c.face, ra & 0xff, offA);
            if (!freedA && !freedB) {
                const std::string diff = maps.Compare(p.a, p.b);
                if (!diff.empty()) ctx.Fail("face %u slot %u run %d: object differs at %s", c.face, c.slot, run, diff.c_str());
            }
            if (!freedA) ::operator delete(p.a);
            if (!freedB) ::operator delete(p.b);
        }
    }
}

// The one slot adapter left, 0x760d95 (slot 37's setter, which cTSAlbumLoader::vf3 also calls directly on its own object):
// the original at its RVA against the adapter, both on an object built by the original constructor (game vtables, as the
// direct caller's object is not ours); result as a field offset, the held objects' calls, every byte.
DIFF_TEST(AppServices_TSGlobals_Adapters, 0x760d95)
{
    InitPool();
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0x760d95);
    std::mt19937 rng(0x760d95);
    for (int run = 0; run < 60; ++run) {
        Pair p = MakePair(ctx, rng, 1 + run % 3, run);
        std::memcpy(p.b, p.a, kSize); // both the original constructor's object
        void *object = run % 5 == 0 ? nullptr : &gPool[rng() % kPool];
        if (run % 7 == 0) std::memcpy(&object, p.a + 0x30, 4);
        gLog.clear();
        const uint32_t ra = svc::P(orig(p.a, object)) - svc::P(p.a);
        const std::string logA = Joined();
        gLog.clear();
        const uint32_t rb = svc::P(cTSGlobalsAdapters::SetNeighborhood(reinterpret_cast<cITSGlobals *>(p.b), nullptr, object)) - svc::P(p.b);
        const std::string logB = Joined();
        ++ctx.cases;
        if (ra != rb || ra != 0x30 || logA != logB || std::memcmp(p.a, p.b, kSize) != 0)
            ctx.Fail("run %d: result +0x%x / +0x%x, calls %s / %s, object %s", run, ra, rb, logA.c_str(), logB.c_str(), std::memcmp(p.a, p.b, kSize) ? "differs" : "same");
        Free(p);
    }
}

namespace {

// Stand-ins for the testing-cheats bodies (0xc2cba3 / 0xc2c9f6 and their AppServices2 replacements): log `this` as an offset
// and the argument; the getter answers a boolean in al with junk above it.
uint32_t gCheatsAnswer = 0;
uint32_t __fastcall StandTestingCheats(void *self, void *)
{
    Log("get @%d", static_cast<int>(static_cast<unsigned char *>(self) - gBase));
    return gCheatsAnswer;
}
void __fastcall StandSetTestingCheats(void *self, void *, uint32_t value)
{
    Log("set @%d %x", static_cast<int>(static_cast<unsigned char *>(self) - gBase), value);
}

} // namespace

// Slots 31 and 56 through each side's own vtable, the bodies stood in on both sides: ours must reach the AppServices2 body
// once, with this = the object and the argument unchanged.
DIFF_TEST(AppServices_TSGlobals_TestingCheatsSlots, kTestingCheats)
{
    InitPool();
    Maps maps;
    shape::StandIn getter(kTestingCheats, &as2::TestingCheatsEnabled, &StandTestingCheats);
    shape::StandIn setter(kSetTestingCheats, &as2::SetTestingCheatsEnabled, &StandSetTestingCheats);
    std::mt19937 rng(0xc2c9f6);
    for (int run = 0; run < 64; ++run) {
        Pair p = MakePair(ctx, rng, 2, run);
        const bool set = run % 2 != 0;
        const uint32_t arg = run % 8 < 4 ? static_cast<uint32_t>(run % 3) : rng();
        gCheatsAnswer = (rng() & 0xffffff00u) | static_cast<uint32_t>(run % 4 < 2);
        uint32_t r[2];
        std::string logs[2];
        for (int side = 0; side < 2; ++side) {
            unsigned char *object = side ? p.b : p.a;
            void *fn = side ? Slot(object, 0, set ? 56 : 31) : GameAddress<void *>(cTSGlobals::kVTable)[set ? 56 : 31];
            gLog.clear();
            gBase = object;
            r[side] = svc::CallWords(fn, object, set ? 1 : 0, &arg);
            logs[side] = Joined();
        }
        ++ctx.cases;
        if (!set && (r[0] & 0xff) != (r[1] & 0xff)) ctx.Fail("run %d: TestingCheatsEnabled 0x%x / 0x%x", run, r[0] & 0xff, r[1] & 0xff);
        if (logs[0] != logs[1] || logs[0].empty()) ctx.Fail("run %d: calls differ\n  original: %s\n  ours:     %s", run, logs[0].c_str(), logs[1].c_str());
        const std::string diff = maps.Compare(p.a, p.b);
        if (!diff.empty()) ctx.Fail("run %d: object differs at %s", run, diff.c_str());
        Free(p);
    }
}
