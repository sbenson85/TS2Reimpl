// Differential tests for cGZTimerService in the standalone shape (lane KD). The original side is an object from the original
// constructor 0xc6d0da (the game's vtables), ours one from our constructor (the hook's adapter: our vtables); every call goes
// through the side's own vtable slot (the game's vtable on the original side, ours on ours), and objects are compared word
// by word with the vtable words mapped to codes (tests/service_shape.h).
//
// The clock is scripted as in tests/rz_stopwatch.cpp (the original's QueryPerformanceCounter slot and our override pointer
// return the same value); Init and Shutdown talk to a fake framework in the game global 0x10c9378 whose
// AddToTick/RemoveFromTick slots record their calls.
//
// RVAs: the constructor 0xc6d0da, the destructor 0xc6d126, the deleting destructor 0xc6d155 and its +4 thunk 0xc6d14d,
// QueryInterface 0xc6d1d3 and its thunks 0xc6d1fe (+4) / 0xc6d206 (+0x1c), Init 0xc6d174, Shutdown 0xc6d20e, OnTick
// 0xc6d190; the folded slots our vtables now hold: AddRef 0x1cc320 (+4 / +0x1c thunks 0x1cc325 / 0x1cc32d), Release
// 0x1cd7a0 (0x1cd7a5 / 0x1cd7ad), SetTimeOffset 0x5506e0, GetTime 0x147a30, GetDelta 0x46390, GetTickCount 0x149cf0,
// SetMaxDelta 0x47fc80, and cRZSystemService's.
#include "difftest.h"
#include "free_scope.h"
#include "service_shape.h"
#include "gz/cGZTimerService.h"

#include <windows.h>

#include <cstring>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace {

using svc::P;

constexpr uint32_t kIatQpc = 0xd9a24c;
constexpr uint32_t kFrameworkGlobal = 0x10c9378;
constexpr uint32_t kCtor = 0xc6d0da, kDtor = 0xc6d126, kDeleting = 0xc6d155, kDeletingThunk = 0xc6d14d;
constexpr uint32_t kSize = cGZTimerService::kSize;
constexpr uint32_t kFaceOffset[3] = {0, 4, 0x1c};
constexpr uint32_t kGameVt[3] = {cGZTimerService::kVTable, cGZTimerService::kUnknownVTable, cGZTimerService::kTimerVTable};
constexpr unsigned kSlotCount[3] = {14, 6, 7};
constexpr int kSlotInit = 8, kSlotShutdown = 9, kSlotOnTick = 10;

uint64_t gNow = 0;
int gCounterReads = 0;

BOOL WINAPI FakeQpc(LARGE_INTEGER *out)
{
    ++gCounterReads;
    out->QuadPart = static_cast<int64_t>(gNow);
    return TRUE;
}

void FakeCounter(uint64_t *out)
{
    ++gCounterReads;
    *out = gNow;
}

struct ScriptedClock {
    difftest::Context &ctx;
    void *saved;
    explicit ScriptedClock(difftest::Context &c) : ctx(c)
    {
        void **slot = ctx.image.At<void *>(kIatQpc);
        saved = *slot;
        *slot = reinterpret_cast<void *>(&FakeQpc);
        cRZStopwatch::sPerformanceCounterOverride = &FakeCounter;
    }
    ~ScriptedClock()
    {
        *ctx.image.At<void *>(kIatQpc) = saved;
        cRZStopwatch::sPerformanceCounterOverride = nullptr;
    }
};

std::vector<int64_t> gLog;
unsigned char *gBase = nullptr;

// Framework fake: slots 9 and 10 record (slot, argument offset from the object under test).
template <int Slot> void __fastcall FakeFrameworkSlot(void * /*self*/, void * /*edx*/, void *arg)
{
    gLog.push_back(Slot);
    gLog.push_back(static_cast<unsigned char *>(arg) - gBase);
}

struct FakeFramework {
    void *vtable[16];
    FakeFramework()
    {
        for (void *&s : vtable) s = nullptr;
        vtable[9] = reinterpret_cast<void *>(&FakeFrameworkSlot<9>);
        vtable[10] = reinterpret_cast<void *>(&FakeFrameworkSlot<10>);
    }
    const void *object = vtable; // the object's first dword is its vtable pointer
};

// The game global holding the framework, pointed at a fake for the test's duration.
struct FrameworkScope {
    difftest::Context &ctx;
    FakeFramework fw;
    void *saved;
    explicit FrameworkScope(difftest::Context &c) : ctx(c)
    {
        void **global = ctx.image.At<void *>(kFrameworkGlobal);
        saved = *global;
        *global = &fw.object;
    }
    ~FrameworkScope() { *ctx.image.At<void *>(kFrameworkGlobal) = saved; }
};

cGZTimerService *Raw(int fill = 0xCD)
{
    auto *p = static_cast<cGZTimerService *>(::operator new(kSize));
    std::memset(p, fill, kSize);
    return p;
}

cGZTimerService *MakeOriginal(difftest::Context &ctx, int fill = 0xCD)
{
    cGZTimerService *p = Raw(fill);
    ctx.Original<cGZTimerService *(__thiscall *)(void *)>(kCtor)(p);
    return p;
}

cGZTimerService *MakeOurs(int fill = 0xCD) { return cGZTimerServiceAdapters::Construct(Raw(fill), nullptr); }

// Each side's code map: the game's and our vtable of each face as one code per face, the destroyed service part's.
struct Maps {
    svc::CodeMap a, b;
    explicit Maps(difftest::Context &ctx)
    {
        cGZTimerService *ours = MakeOurs();
        for (uint32_t f = 0; f < 3; ++f) {
            a.AddRva(kGameVt[f], f);
            b.AddRva(kGameVt[f], f);
            b.Add(svc::VTableAt(ours, kFaceOffset[f]), f);
        }
        ::operator delete(ours);
        svc::AddServiceBaseCodes(a, b, 0x10);
        (void)ctx;
    }
};

std::string Compare(const void *a, const void *b, const Maps &maps)
{
    return svc::Diff(svc::Words(a, kSize, maps.a), svc::Words(b, kSize, maps.b));
}

uint32_t CallSlot(void *object, unsigned face, unsigned slot, unsigned words, const uint32_t *args)
{
    void *self = static_cast<uint8_t *>(object) + kFaceOffset[face];
    return svc::CallWords(svc::VTableAt(object, kFaceOffset[face])[slot], self, words, args);
}

// The same random value in every word but the vtable words, on both sides.
void SeedFields(cGZTimerService *a, cGZTimerService *b, std::mt19937 &rng, int run)
{
    for (uint32_t offset = 0; offset < kSize; offset += 4) {
        if (offset == 0 || offset == 4 || offset == 0x1c) continue;
        uint32_t value = rng();
        if (offset == 8) value = run % 8 < 6 ? static_cast<uint32_t>(run % 6) : value; // the count: 0..5 or random
        if (offset == 0x18 && run % 4 == 0) value &= 0xffffff00;                     // the running byte 0 ...
        if (offset == 0x18 && run % 4 == 1) value = (value & 0xffffff00) | 0x80;     // ... or a raw 0x80
        std::memcpy(reinterpret_cast<uint8_t *>(a) + offset, &value, 4);
        std::memcpy(reinterpret_cast<uint8_t *>(b) + offset, &value, 4);
    }
}

struct Slot {
    unsigned face, slot, words;
    svc::Ret ret;
    const char *name;
};

// Every slot but Init / Shutdown / OnTick (their own tests) and the deleting destructor (the destructor test).
const Slot kSlotTable[] = {
    {0, 0, 2, svc::kByte, "QueryInterface"},     {0, 1, 0, svc::kWord, "AddRef"},
    {0, 2, 0, svc::kWord, "Release"},            {0, 3, 0, svc::kWord, "GetServiceID"},
    {0, 4, 1, svc::kVoid, "SetServiceID"},       {0, 5, 0, svc::kWord, "GetServicePriority"},
    {0, 6, 0, svc::kByte, "IsServiceRunning"},   {0, 7, 1, svc::kVoid, "SetServiceRunning"},
    {0, 11, 1, svc::kByte, "OnIdle"},            {0, 12, 0, svc::kWord, "GetServiceTickPriority"},
    {0, 13, 1, svc::kVoid, "SetTimeOffset"},
    {1, 0, 2, svc::kByte, "+4 QueryInterface"},  {1, 1, 0, svc::kWord, "+4 AddRef"},
    {1, 2, 0, svc::kWord, "+4 Release"},         {1, 4, 0, svc::kWord, "RemoveRef"},
    {1, 5, 0, svc::kWord, "RefCount"},
    {2, 0, 2, svc::kByte, "+0x1c QueryInterface"}, {2, 1, 0, svc::kWord, "+0x1c AddRef"},
    {2, 2, 0, svc::kWord, "+0x1c Release"},      {2, 3, 0, svc::kWord, "GetTime"},
    {2, 4, 0, svc::kWord, "GetDelta"},           {2, 5, 0, svc::kWord, "GetTickCount"},
    {2, 6, 1, svc::kVoid, "SetMaxDelta"},
};

constexpr uint32_t kIds[] = {cGZTimerService::kTimerInterfaceId, 0x287fb697, 1, 0, 2, 0x48e9562f, 0x48e9562d, 0xffffffff, 0x089b7dc8, 0xa76c78e7};

} // namespace

DIFF_TEST(cGZTimerService_Construct, 0xc6d0da)
{
    auto orig = ctx.Original<cGZTimerService *(__thiscall *)(void *)>(kCtor);
    Maps maps(ctx);
    for (int fill : {0xCD, 0x00, 0xFF}) {
        cGZTimerService *a = Raw(fill), *b = Raw(fill);
        cGZTimerService *ra = orig(a), *rb = cGZTimerServiceAdapters::Construct(b, nullptr);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("fill %02x: return value is not this", fill);
        const std::string diff = Compare(a, b, maps);
        if (!diff.empty()) ctx.Fail("fill %02x: constructor differs at %s", fill, diff.c_str());
        for (uint32_t f = 0; f < 3; ++f) {
            if (svc::VTableAt(a, kFaceOffset[f]) != GameAddress<void *>(kGameVt[f])) ctx.Fail("fill %02x: original's face %u vtable", fill, f);
            if (svc::VTableAt(b, kFaceOffset[f]) == GameAddress<void *>(kGameVt[f])) ctx.Fail("fill %02x: our face %u has the game's vtable", fill, f);
        }
        if (a->mServiceId != cGZTimerService::kServiceId || a->mMaxDelta != cGZTimerService::kDefaultMaxDelta || a->mStopwatch.mMode != 4) ctx.Fail("fill %02x: unexpected state", fill);
        ::operator delete(a);
        ::operator delete(b);
    }
}

// Every slot of the three faces but Init / Shutdown / OnTick and the deleting destructor, through each side's own vtable.
DIFF_TEST(cGZTimerService_Slots, 0xc6d1d3)
{
    Maps maps(ctx);
    std::mt19937 rng(0x71e5);
    void *const kUntouched = reinterpret_cast<void *>(0x5a5a5a5a);
    for (const Slot &slot : kSlotTable) {
        if (slot.slot >= kSlotCount[slot.face]) ctx.Fail("%s: slot out of range", slot.name);
        const int runs = slot.slot == 0 ? 2 * static_cast<int>(std::size(kIds)) : 24;
        for (int run = 0; run < runs; ++run) {
            cGZTimerService *a = MakeOriginal(ctx), *b = MakeOurs();
            SeedFields(a, b, rng, run);
            uint32_t args[2] = {rng(), rng()};
            if (slot.slot == 0) args[0] = kIds[run % std::size(kIds)];
            void *outA = kUntouched, *outB = kUntouched;
            uint32_t argsA[2] = {args[0], slot.slot == 0 ? P(&outA) : args[1]};
            uint32_t argsB[2] = {args[0], slot.slot == 0 ? P(&outB) : args[1]};
            difftest::BeginFrees();
            const uint32_t ra = svc::CallWords(GameAddress<void *>(kGameVt[slot.face])[slot.slot], reinterpret_cast<uint8_t *>(a) + kFaceOffset[slot.face], slot.words, argsA);
            const bool freedA = difftest::FreedSinceBegin(a);
            difftest::BeginFrees();
            const uint32_t rb = CallSlot(b, slot.face, slot.slot, slot.words, argsB);
            const bool freedB = difftest::FreedSinceBegin(b);
            ++ctx.cases;
            if (svc::Masked(slot.ret, ra) != svc::Masked(slot.ret, rb)) ctx.Fail("%s run %d: result 0x%x / 0x%x", slot.name, run, ra, rb);
            if (freedA != freedB) ctx.Fail("%s run %d: freed %d / %d", slot.name, run, freedA, freedB);
            const int32_t offA = outA == kUntouched ? -1 : static_cast<int32_t>(P(outA) - P(a));
            const int32_t offB = outB == kUntouched ? -1 : static_cast<int32_t>(P(outB) - P(b));
            if (offA != offB) ctx.Fail("%s run %d id 0x%x: out offset %d / %d", slot.name, run, args[0], offA, offB);
            if (!freedA && !freedB) {
                const std::string diff = Compare(a, b, maps);
                if (!diff.empty()) ctx.Fail("%s run %d: object differs at %s", slot.name, run, diff.c_str());
            }
            if (slot.slot == 0 && args[0] == cGZTimerService::kTimerInterfaceId && ((ra & 0xff) != 1 || offA != 0x1c)) ctx.Fail("%s: timer interface: result %u offset %d", slot.name, ra & 0xff, offA);
            if (!freedA) ::operator delete(a);
            if (!freedB) ::operator delete(b);
        }
    }
}

// The destructor 0xc6d126 (called directly; ours: our destructor, no dispatch), the deleting destructor 0xc6d155 (on the
// primary pointer) and its +4 thunk 0xc6d14d (ours: our +4 vtable's slot 3 for both), flags 0..3.
DIFF_TEST(cGZTimerService_Destruct, 0xc6d126)
{
    Maps maps(ctx);
    auto dtor = ctx.Original<void(__thiscall *)(void *)>(kDtor);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeletingThunk);
    std::mt19937 rng(0x7101);
    for (int run = 0; run < 48; ++run) {
        const int route = run % 3;
        const uint32_t flags = route == 0 ? 0 : (run / 3) % 4;
        cGZTimerService *a = MakeOriginal(ctx), *b = MakeOurs();
        SeedFields(a, b, rng, run);
        difftest::BeginFrees();
        void *ra = route == 0 ? (dtor(a), a) : route == 1 ? deleting(a, flags) : thunk(reinterpret_cast<uint8_t *>(a) + 4, flags);
        const bool freedA = difftest::FreedSinceBegin(a);
        difftest::BeginFrees();
        void *rb = b;
        if (route == 0) {
            b->cGZTimerService::~cGZTimerService();
        } else {
            const uint32_t args[1] = {flags};
            rb = reinterpret_cast<void *>(CallSlot(b, 1, 3, 1, args));
        }
        const bool freedB = difftest::FreedSinceBegin(b);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("route %d flags %u: return value is not the object", route, flags);
        if (freedA != freedB || freedA != ((flags & 1) != 0)) ctx.Fail("route %d flags %u: freed %d / %d", route, flags, freedA, freedB);
        if (!freedA && !freedB) {
            const std::string diff = Compare(a, b, maps);
            if (!diff.empty()) ctx.Fail("route %d flags %u: object differs at %s", route, flags, diff.c_str());
        }
        if (!freedA) ::operator delete(a);
        if (!freedB) ::operator delete(b);
    }
}

DIFF_TEST(cGZTimerService_InitShutdown, 0xc6d174)
{
    Maps maps(ctx);
    ScriptedClock clock(ctx);
    FrameworkScope framework(ctx);
    std::mt19937_64 rng(0x7102);
    for (int run = 0; run < 100; ++run) {
        cGZTimerService *a = MakeOriginal(ctx), *b = MakeOurs();
        a->mStopwatch.mStart = b->mStopwatch.mStart = rng() >> 20;
        gNow = rng() >> (1 + run % 40);
        gLog.clear();
        gCounterReads = 0;
        gBase = reinterpret_cast<unsigned char *>(a);
        uint8_t ra = static_cast<uint8_t>(CallSlot(a, 0, kSlotInit, 0, nullptr));
        std::vector<int64_t> logA = gLog;
        int readsA = gCounterReads;
        gLog.clear();
        gCounterReads = 0;
        gBase = reinterpret_cast<unsigned char *>(b);
        uint8_t rb = static_cast<uint8_t>(CallSlot(b, 0, kSlotInit, 0, nullptr));
        ++ctx.cases;
        if (ra != rb || ra != 1 || logA != gLog || logA.size() != 2 || logA[0] != 9 || logA[1] != 0 || readsA != 1 || gCounterReads != 1
            || !Compare(a, b, maps).empty() || a->mStopwatch.mStart != gNow) {
            ctx.Fail("run %d Init: result %u/%u, framework calls %zu/%zu, counter reads %d/%d", run, ra, rb, logA.size(), gLog.size(), readsA, gCounterReads);
        }
        gLog.clear();
        gBase = reinterpret_cast<unsigned char *>(a);
        ra = static_cast<uint8_t>(CallSlot(a, 0, kSlotShutdown, 0, nullptr));
        logA = gLog;
        gLog.clear();
        gBase = reinterpret_cast<unsigned char *>(b);
        rb = static_cast<uint8_t>(CallSlot(b, 0, kSlotShutdown, 0, nullptr));
        if (ra != rb || ra != 1 || logA != gLog || logA.size() != 2 || logA[0] != 10 || logA[1] != 0 || !Compare(a, b, maps).empty()) {
            ctx.Fail("run %d Shutdown (0xc6d20e): result %u/%u, framework calls %zu/%zu", run, ra, rb, logA.size(), gLog.size());
        }
        ::operator delete(a);
        ::operator delete(b);
    }
}

DIFF_TEST(cGZTimerService_OnTick, 0xc6d190)
{
    Maps maps(ctx);
    ScriptedClock clock(ctx);
    std::mt19937_64 rng(0x7103);
    for (int run = 0; run < 30000; ++run) {
        cGZTimerService *a = MakeOriginal(ctx), *b = MakeOurs();
        // Scale of mode 4 (the milliseconds factor) and the counter-reading overhead.
        float scales[5] = {1, 1, 1, 1, 1};
        const float picks[] = {1.0f, 0.001f, 0.5f, 0.000123f, 2.0f, 0.0f, 1000.0f};
        scales[2] = picks[rng() % 7];
        uint64_t overhead = run % 3 == 0 ? 0 : rng() >> (40 + rng() % 20);
        *ctx.image.At<uint64_t>(cRZStopwatch::kOverheadRva) = overhead;
        for (int i = 0; i < 5; ++i) ctx.image.At<float>(cRZStopwatch::kScalesRva)[i] = scales[i];
        uint32_t time = static_cast<uint32_t>(rng() >> (32 + rng() % 30)), offset = static_cast<uint32_t>(rng() >> (32 + rng() % 30));
        uint32_t maxDelta = run % 5 == 0 ? 0x7d0 : run % 5 == 1 ? 0 : static_cast<uint32_t>(rng() >> (40 + rng() % 24));
        uint32_t ticks = static_cast<uint32_t>(rng()) >> (rng() % 30);
        if (run % 17 == 0) ticks = 0xffffffff;
        uint64_t start = run % 6 == 0 ? 0 : 1 + (rng() >> (20 + rng() % 40));
        uint64_t accumulated = rng() >> (30 + rng() % 30);
        for (cGZTimerService *t : {a, b}) {
            t->mTime = time;
            t->mOffset = offset;
            t->mMaxDelta = maxDelta;
            t->mTicks = ticks;
            t->mDelta = 0x1111;
            t->mRaw = 0x2222;
            t->mStopwatch.mStart = start;
            t->mStopwatch.mAccumulated = accumulated;
        }
        // The counter: at the start, within the overhead, and further on.
        gNow = start + overhead + (rng() >> (30 + rng() % 34));
        if (run % 7 == 0) gNow = start;
        const uint32_t args[1] = {static_cast<uint32_t>(rng())};
        gCounterReads = 0;
        uint8_t ra = static_cast<uint8_t>(CallSlot(a, 0, kSlotOnTick, 1, args));
        int readsA = gCounterReads;
        gCounterReads = 0;
        uint8_t rb = static_cast<uint8_t>(CallSlot(b, 0, kSlotOnTick, 1, args));
        ++ctx.cases;
        if (ra != rb || ra != 1 || readsA != gCounterReads || !Compare(a, b, maps).empty()) {
            ctx.Fail("run %d: time %u offset %u max %u: result %u/%u, time %u/%u delta %u/%u offset %u/%u ticks %u/%u", run, time, offset, maxDelta, ra, rb,
                     a->mTime, b->mTime, a->mDelta, b->mDelta, a->mOffset, b->mOffset, a->mTicks, b->mTicks);
        }
        ::operator delete(a);
        ::operator delete(b);
    }
}
