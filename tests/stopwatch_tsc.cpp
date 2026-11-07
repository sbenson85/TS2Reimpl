// Mode 0 of cRZStopwatch::Start (0x1c982b) and Elapsed (0x1c9256): the stopwatch reads the CPU
// time-stamp counter through the helper at 0x1c9244 (cpuid, rdtsc), which cannot be scripted.
// Both methods reach it through a direct call, so the test overwrites the first five bytes of that
// helper in the mapped original with a jump to a fake (restored afterwards) and points our reader at
// the same script (cRZStopwatch::sTimeStampCounterOverride). The helper itself stays unverified
// (tests/rz_stopwatch.cpp compares it back to back only). The performance counter is scripted too,
// to a different value, so a mode 0 stopwatch that read the wrong counter would show.
#include "difftest.h"
#include "rz/StopwatchMore.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZStopwatchCalibration.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <utility>

namespace {

constexpr uint32_t kIatQpc = 0xd9a24c;
constexpr uint32_t kCtor = 0x1c9030, kStart = 0x1c982b, kElapsed = 0x1c9256, kReadTsc = 0x1c9244;

uint64_t gTsc = 0, gQpcValue = 0;
int gTscCalls = 0, gQpcCalls = 0;

void __cdecl FakeTsc(uint64_t *out)
{
    ++gTscCalls;
    *out = gTsc;
}
BOOL WINAPI FakeQpc(LARGE_INTEGER *out)
{
    ++gQpcCalls;
    out->QuadPart = static_cast<int64_t>(gQpcValue);
    return TRUE;
}
void FakeQpcOverride(uint64_t *out)
{
    ++gQpcCalls;
    *out = gQpcValue;
}

struct Fakes {
    difftest::Context &ctx;
    uint8_t *site;
    uint8_t saved[5];
    void *savedSlot;
    explicit Fakes(difftest::Context &c) : ctx(c), site(c.image.At<uint8_t>(kReadTsc))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&FakeTsc) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
        void **slot = ctx.image.At<void *>(kIatQpc);
        savedSlot = *slot;
        *slot = reinterpret_cast<void *>(&FakeQpc);
        cRZStopwatch::sTimeStampCounterOverride = &FakeTsc;
        cRZStopwatch::sPerformanceCounterOverride = &FakeQpcOverride;
    }
    ~Fakes()
    {
        std::memcpy(site, saved, 5);
        *ctx.image.At<void *>(kIatQpc) = savedSlot;
        cRZStopwatch::sTimeStampCounterOverride = nullptr;
        cRZStopwatch::sPerformanceCounterOverride = nullptr;
    }
};

uint64_t Rand64(std::mt19937_64 &rng, int bits)
{
    return rng() >> (64 - bits);
}

} // namespace

DIFF_TEST(cRZStopwatch_StartMode0, 0x1c982b)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(kStart);
    auto ctor = ctx.Original<cRZStopwatch *(__thiscall *)(void *, int32_t)>(kCtor);
    Fakes fakes(ctx);
    std::mt19937_64 rng(0x7501);
    for (int run = 0; run < 300; ++run) {
        cRZStopwatch a, b;
        ctor(&a, 0);
        b.Construct(0);
        a.mUnused = b.mUnused = 0x5a5a;
        a.mAccumulated = b.mAccumulated = Rand64(rng, 40);
        a.mStart = b.mStart = Rand64(rng, 30);
        gTsc = run % 7 == 0 ? ~0ull : Rand64(rng, 1 + run % 63);
        gQpcValue = 0x1234567890ull;
        gTscCalls = gQpcCalls = 0;
        orig(&a);
        int tscA = gTscCalls, qpcA = gQpcCalls;
        gTscCalls = gQpcCalls = 0;
        b.Start();
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0 || a.mStart != gTsc || tscA != 1 || qpcA != 0 || gTscCalls != 1 || gQpcCalls != 0) {
            ctx.Fail("start %llx/%llx (want %llx), TSC reads %d/%d, QPC reads %d/%d", a.mStart, b.mStart, gTsc, tscA, gTscCalls, qpcA, gQpcCalls);
        }
    }
}

DIFF_TEST(cRZStopwatch_ElapsedMode0, 0x1c9256)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(kElapsed);
    auto ctor = ctx.Original<cRZStopwatch *(__thiscall *)(void *, int32_t)>(kCtor);
    Fakes fakes(ctx);
    std::mt19937_64 rng(0x7502);
    for (int run = 0; run < 5000; ++run) {
        uint64_t overhead = run % 7 == 0 ? 0 : Rand64(rng, 1 + rng() % 40);
        *ctx.image.At<uint64_t>(cRZStopwatch::kOverheadRva) = overhead;
        for (int i = 0; i < 6; ++i) ctx.image.At<float>(cRZStopwatch::kScalesRva)[i] = 3.0f + i;
        cRZStopwatch a, b;
        ctor(&a, 0);
        b.Construct(0);
        a.mUnused = b.mUnused = 0x1234;
        uint64_t accumulated = run % 4 == 0 ? 0 : Rand64(rng, 1 + rng() % 64);
        a.mAccumulated = b.mAccumulated = accumulated;
        uint64_t start = run % 5 != 0 ? 1 + Rand64(rng, 1 + rng() % 62) : 0;
        if (run % 13 == 0) start = ~0ull;        // both words set
        if (run % 17 == 0) start = 1ull << 32;   // only the high word set
        if (run % 19 == 0) start = 0xffffffffull; // only the low word set
        bool started = start != 0;
        a.mStart = b.mStart = start;
        uint64_t now;
        switch (rng() % 8) {
        case 0: now = start + overhead; break;
        case 1: now = start + overhead + 1; break;
        case 2: now = start + (overhead ? overhead - 1 : 0); break;
        case 3: now = start; break;
        case 4: now = start - 1 - rng() % 100; break;
        case 5: now = start + Rand64(rng, 1 + rng() % 40); break;
        case 6: now = start + overhead + Rand64(rng, 1 + rng() % 30); break;
        default: now = Rand64(rng, 1 + rng() % 63); break;
        }
        gTsc = now;
        gQpcValue = 0x5555;
        gTscCalls = gQpcCalls = 0;
        uint32_t ra = orig(&a);
        int tscA = gTscCalls, qpcA = gQpcCalls;
        gTscCalls = gQpcCalls = 0;
        uint32_t rb = b.Elapsed();
        ++ctx.cases;
        if (ra != rb || tscA != gTscCalls || tscA != (started ? 1 : 0) || qpcA != 0 || gQpcCalls != 0 || std::memcmp(&a, &b, sizeof(a)) != 0) {
            ctx.Fail("start %llx acc %llx now %llx overhead %llx: %u/%u, TSC reads %d/%d, QPC reads %d/%d", start, accumulated, now, overhead, ra, rb, tscA, gTscCalls, qpcA, gQpcCalls);
        }
    }
}

// ---- ElapsedAsFloat (0x1c9368) and ElapsedAs64 (0x1c948d) in mode 0 (src/rz/StopwatchMore.h) ----
// The same redirection; tests/framework_startup.cpp covers modes 1..8 and negative ones. Both timebases are filled with
// different junk per case (so reading the time-stamp counter's 0x10cb678 instead of 0x10cb648 shows), the counts reach
// past 2^53 and 2^63 with float tie patterns (the int64 to double to float rounding), and the output starts as the same
// NaN pattern on both sides. Ours reads the cells through the owned statics, which the harness resolves to the image's.

namespace {

constexpr uint32_t kElapsedFloat = 0x1c9368, kElapsed64 = 0x1c948d;
constexpr uint32_t kStampTimebaseRva = 0x10cb678;

struct Mode0Case {
    uint64_t overhead, accumulated, start, now;
    bool started;
};

Mode0Case RandomMode0Case(difftest::Context &ctx, std::mt19937_64 &rng, int run)
{
    Mode0Case c;
    c.overhead = run % 7 == 0 ? 0 : Rand64(rng, 1 + rng() % 40);
    *ctx.image.At<uint64_t>(cRZStopwatch::kOverheadRva) = c.overhead;
    for (int i = 0; i < 10; ++i) ctx.image.At<uint32_t>(cRZStopwatch::kScalesRva)[i] = static_cast<uint32_t>(rng());
    *ctx.image.At<uint64_t>(kStampTimebaseRva) = c.overhead ^ (1 + Rand64(rng, 40));
    for (int i = 0; i < 10; ++i) ctx.image.At<uint32_t>(kStampTimebaseRva + 8)[i] = static_cast<uint32_t>(rng());
    switch (run % 6) {
    case 0: c.accumulated = 0; break;
    case 1: c.accumulated = Rand64(rng, 54 + rng() % 11); break; // past a double's 53 bits
    case 2: {                                                    // a float tie plus one
        uint32_t k = 30 + rng() % 34;
        c.accumulated = (1ull << k) + (1ull << (k - 24)) + (rng() % 2);
        break;
    }
    case 3: c.accumulated = 0x8000000000000000ull + Rand64(rng, 1 + rng() % 62); break; // negative as int64
    default: c.accumulated = Rand64(rng, 1 + rng() % 64); break;
    }
    c.start = run % 5 != 0 ? 1 + Rand64(rng, 1 + rng() % 62) : 0;
    if (run % 13 == 0) c.start = ~0ull;
    if (run % 17 == 0) c.start = 1ull << 32;
    if (run % 19 == 0) c.start = 0xffffffffull;
    c.started = c.start != 0;
    switch (rng() % 8) {
    case 0: c.now = c.start + c.overhead; break;
    case 1: c.now = c.start + c.overhead + 1; break;
    case 2: c.now = c.start + (c.overhead ? c.overhead - 1 : 0); break;
    case 3: c.now = c.start; break;
    case 4: c.now = c.start - 1 - rng() % 100; break;
    case 5: c.now = c.start + Rand64(rng, 1 + rng() % 40); break;
    case 6: c.now = c.start + c.overhead + Rand64(rng, 1 + rng() % 30); break;
    default: c.now = Rand64(rng, 1 + rng() % 63); break;
    }
    return c;
}

// Runs one side (`call` on `w`) and returns the counter reads it made.
template <class Call> std::pair<int, int> RunSide(cRZStopwatch &w, const Mode0Case &c, Call call)
{
    gTsc = c.now;
    gQpcValue = c.now ^ 0x5555;
    gTscCalls = gQpcCalls = 0;
    call(w);
    return {gTscCalls, gQpcCalls};
}

template <class Out, class Orig, class Ours> void CompareMode0(difftest::Context &ctx, uint32_t rva, uint32_t seed, Orig orig, Ours ours)
{
    if (static_cast<void *>(&ts2_rva_10cb648) != ctx.image.At<void>(cRZStopwatch::kOverheadRva))
        ctx.Fail("the owned timebase is not the image's cell");
    auto ctor = ctx.Original<cRZStopwatch *(__thiscall *)(void *, int32_t)>(kCtor);
    Fakes fakes(ctx);
    std::mt19937_64 rng(seed);
    for (int run = 0; run < 6000; ++run) {
        Mode0Case c = RandomMode0Case(ctx, rng, run);
        cRZStopwatch a, b;
        ctor(&a, 0);
        b.Construct(0);
        a.mUnused = b.mUnused = 0x2468;
        a.mAccumulated = b.mAccumulated = c.accumulated;
        a.mStart = b.mStart = c.start;
        Out oa, ob;
        std::memset(&oa, 0xa5, sizeof(oa));
        std::memset(&ob, 0xa5, sizeof(ob));
        if (sizeof(Out) == 4) std::memcpy(&oa, "\x01\x00\xc0\x7f", 4), std::memcpy(&ob, "\x01\x00\xc0\x7f", 4);
        auto readsA = RunSide(a, c, [&](cRZStopwatch &w) { orig(&w, &oa); });
        auto readsB = RunSide(b, c, [&](cRZStopwatch &w) { ours(&w, &ob); });
        ++ctx.cases;
        if (std::memcmp(&oa, &ob, sizeof(Out)) != 0 || readsA != readsB || readsA.first != (c.started ? 1 : 0) ||
            readsA.second != 0 || std::memcmp(&a, &b, sizeof(a)) != 0) {
            uint64_t va = 0, vb = 0;
            std::memcpy(&va, &oa, sizeof(Out));
            std::memcpy(&vb, &ob, sizeof(Out));
            ctx.Fail("%x: start %llx acc %llx now %llx overhead %llx: %llx/%llx, TSC reads %d/%d, QPC reads %d/%d", rva,
                     c.start, c.accumulated, c.now, c.overhead, va, vb, readsA.first, readsB.first, readsA.second, readsB.second);
        }
    }
}

} // namespace

DIFF_TEST(cRZStopwatch_ElapsedAsFloatMode0, 0x1c9368)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, float *)>(kElapsedFloat);
    CompareMode0<float>(ctx, kElapsedFloat, 0x9368, orig,
                        [](cRZStopwatch *w, float *out) { stopwatch_more::ElapsedAsFloat(w, nullptr, out); });
}

DIFF_TEST(cRZStopwatch_ElapsedAs64Mode0, 0x1c948d)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint64_t *)>(kElapsed64);
    CompareMode0<uint64_t>(ctx, kElapsed64, 0x948d, orig,
                           [](cRZStopwatch *w, uint64_t *out) { stopwatch_more::ElapsedAs64(w, nullptr, out); });
}
