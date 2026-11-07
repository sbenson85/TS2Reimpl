// Differential tests for cRZStopwatch. The clock is the only input that cannot be repeated, so the
// two counters are made controllable: the original's QueryPerformanceCounter import slot (0xd9a24c)
// is pointed at a fake that returns scripted values, and our code reads its counter through the
// override pointer (cRZStopwatch::sPerformanceCounterOverride), set to the same script. The
// time-stamp counter (mode 0) cannot be scripted; those cases compare the two sides' readings
// taken back to back, within a tolerance.
#include "difftest.h"
#include "rz/cRZStopwatch.h"

#include <windows.h>

#include <cstring>
#include <random>

namespace {

constexpr uint32_t kIatQpc = 0xd9a24c;
constexpr uint32_t kCtor = 0x1c9030, kStart = 0x1c982b, kElapsed = 0x1c9256, kReadTsc = 0x1c9244, kReadQpc = 0x1c95c4;

uint64_t gNow = 0;
int gQpcCalls = 0;
BOOL gQpcResult = TRUE;

BOOL WINAPI FakeQpc(LARGE_INTEGER *out)
{
    ++gQpcCalls;
    out->QuadPart = static_cast<int64_t>(gNow);
    return gQpcResult;
}

void FakeCounter(uint64_t *out)
{
    ++gQpcCalls;
    *out = gNow;
}

// Original's QueryPerformanceCounter slot -> the fake, ours -> the same script; both restored after.
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

// The calibration globals the original's Elapsed reads (and ours reads through GameAddress).
void SetCalibration(difftest::Context &ctx, uint64_t overhead, const float (&scales)[5])
{
    *ctx.image.At<uint64_t>(cRZStopwatch::kOverheadRva) = overhead;
    for (int i = 0; i < 5; ++i) ctx.image.At<float>(cRZStopwatch::kScalesRva)[i] = scales[i];
    // The dword after the five scales is not a scale: nonzero, so a mode that reads it would show.
    ctx.image.At<float>(cRZStopwatch::kScalesRva)[5] = 3.0f;
}

uint64_t Rand64(std::mt19937_64 &rng, int bits)
{
    return rng() >> (64 - bits);
}

} // namespace

DIFF_TEST(cRZStopwatch_Construct, 0x1c9030)
{
    auto orig = ctx.Original<cRZStopwatch *(__thiscall *)(void *, int32_t)>(kCtor);
    const int32_t modes[] = {0, 1, 2, 3, 4, 5, 6, 7, -1, 0x7fffffff, 100};
    for (int32_t mode : modes) {
        for (int fill : {0xCD, 0x00, 0xFF}) {
            alignas(8) unsigned char a[sizeof(cRZStopwatch)], b[sizeof(cRZStopwatch)];
            std::memset(a, fill, sizeof(a));
            std::memset(b, fill, sizeof(b));
            cRZStopwatch *ra = orig(a, mode);
            cRZStopwatch *rb = reinterpret_cast<cRZStopwatch *>(b)->Construct(mode);
            ++ctx.cases;
            // The dword at +4 is never written: it keeps the fill on both sides.
            if (ra != reinterpret_cast<cRZStopwatch *>(a) || rb != reinterpret_cast<cRZStopwatch *>(b) || std::memcmp(a, b, sizeof(a)) != 0) {
                ctx.Fail("mode %d fill %02x: constructor differs", mode, fill);
            }
        }
    }
}

DIFF_TEST(cRZStopwatch_ReadPerformanceCounter, 0x1c95c4)
{
    auto orig = ctx.Original<int32_t(__cdecl *)(uint64_t *)>(kReadQpc);
    {
        ScriptedClock clock(ctx);
        std::mt19937_64 rng(0x5701);
        for (int run = 0; run < 200; ++run) {
            gNow = run % 5 == 0 ? ~0ull : Rand64(rng, 1 + run % 63);
            gQpcResult = TRUE;
            uint64_t a = 0x1111, b = 0x1111;
            gQpcCalls = 0;
            int32_t ra = orig(&a);
            int callsA = gQpcCalls;
            gQpcCalls = 0;
            int32_t rb = cRZStopwatch::ReadPerformanceCounter(&b);
            ++ctx.cases;
            if (a != gNow || b != gNow || ra != rb || callsA != 1 || gQpcCalls != 1) ctx.Fail("run %d: reading %llx/%llx result %d/%d calls %d/%d", run, a, b, ra, rb, callsA, gQpcCalls);
        }
    }
    // The real counter on both sides (original through its import slot pointed at the real function).
    void **slot = ctx.image.At<void *>(kIatQpc);
    void *saved = *slot;
    *slot = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "QueryPerformanceCounter"));
    for (int run = 0; run < 50; ++run) {
        uint64_t a = 0, b = 0;
        int32_t ra = orig(&a);
        int32_t rb = cRZStopwatch::ReadPerformanceCounter(&b);
        ++ctx.cases;
        if (ra != 1 || rb != 1 || b < a || b - a > 100000000ull) ctx.Fail("real counter: %llu then %llu, results %d/%d", a, b, ra, rb);
    }
    *slot = saved;
}

DIFF_TEST(cRZStopwatch_ReadTsc, 0x1c9244)
{
    auto orig = ctx.Original<void(__cdecl *)(uint64_t *)>(kReadTsc);
    for (int run = 0; run < 100; ++run) {
        uint64_t a = 0, b = 0;
        orig(&a);
        cRZStopwatch::ReadTsc(&b);
        ++ctx.cases;
        // Back to back: ours is later by a few thousand cycles.
        if (b < a || b - a > 3000000000ull) ctx.Fail("run %d: original %llu then ours %llu", run, a, b);
    }
}

DIFF_TEST(cRZStopwatch_Start, 0x1c982b)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(kStart);
    auto ctor = ctx.Original<cRZStopwatch *(__thiscall *)(void *, int32_t)>(kCtor);
    {
        ScriptedClock clock(ctx);
        std::mt19937_64 rng(0x5702);
        for (int run = 0; run < 300; ++run) {
            int32_t mode = 1 + run % 7; // every mode but 0 reads the (scripted) performance counter
            if (run % 11 == 0) mode = -1 - run % 3;
            cRZStopwatch a, b;
            ctor(&a, mode);
            b.Construct(mode);
            a.mUnused = b.mUnused = 0x5a5a;
            a.mAccumulated = b.mAccumulated = Rand64(rng, 40);
            a.mStart = b.mStart = Rand64(rng, 30); // overwritten
            gNow = Rand64(rng, 1 + run % 63);
            gQpcCalls = 0;
            orig(&a);
            int callsA = gQpcCalls;
            gQpcCalls = 0;
            b.Start();
            ++ctx.cases;
            if (std::memcmp(&a, &b, sizeof(a)) != 0 || a.mStart != gNow || callsA != 1 || gQpcCalls != 1) {
                ctx.Fail("mode %d: start %llx/%llx counter reads %d/%d", mode, a.mStart, b.mStart, callsA, gQpcCalls);
            }
        }
    }
    // Mode 0 reads the time-stamp counter: compare back to back.
    for (int run = 0; run < 50; ++run) {
        cRZStopwatch a, b;
        ctor(&a, 0);
        b.Construct(0);
        orig(&a);
        b.Start();
        ++ctx.cases;
        if (b.mStart < a.mStart || b.mStart - a.mStart > 3000000000ull || a.mAccumulated != 0 || b.mAccumulated != 0) ctx.Fail("mode 0: start %llu then %llu", a.mStart, b.mStart);
    }
}

DIFF_TEST(cRZStopwatch_Elapsed, 0x1c9256)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(kElapsed);
    auto ctor = ctx.Original<cRZStopwatch *(__thiscall *)(void *, int32_t)>(kCtor);
    ScriptedClock clock(ctx);
    std::mt19937_64 rng(0x5703);
    for (int run = 0; run < 20000; ++run) {
        int32_t mode = run % 9; // 0 is covered below; 7 and 8 are unknown modes
        if (mode == 0) mode = 1;
        if (run % 50 == 7) mode = -2;
        // Scale factors small enough that the scaled count stays inside 64 bits.
        float scales[5];
        for (float &s : scales) {
            uint32_t pick = rng() % 6;
            s = pick == 0 ? 0.0f : pick == 1 ? -0.001f * (rng() % 100) : pick == 2 ? 1.0f : 0.000001f * (1 + rng() % 5000000) / 1000.0f;
            if (pick == 5) s = 0.001f * (rng() % 90000);
        }
        uint64_t overhead = run % 7 == 0 ? 0 : Rand64(rng, 1 + rng() % 40);
        SetCalibration(ctx, overhead, scales);
        cRZStopwatch a, b;
        ctor(&a, mode);
        b.Construct(mode);
        a.mUnused = b.mUnused = 0x1234;
        uint64_t accumulated = run % 4 == 0 ? 0 : Rand64(rng, 1 + rng() % 50);
        bool tie = false;
        if (run % 8 == 3 && mode >= 2 && mode <= 6) {
            // Counts beyond a double's 53 bits (and past 2^63, which the conversion reads as
            // negative), with scales that keep the product inside 64 bits. Half the time the count
            // is a float tie plus one: converting to double first rounds the extra bit away.
            accumulated = Rand64(rng, 54 + rng() % 11);
            const float bigScales[] = {0.0f, 0.25f, 0.5f, 0.001f, -0.5f, 0.125f};
            for (float &s : scales) s = bigScales[rng() % 6];
            SetCalibration(ctx, overhead, scales);
            if (rng() % 2) {
                uint32_t k = 30 + rng() % 32;
                accumulated = (1ull << k) + (1ull << (k - 24)) + 1;
                tie = true;
            }
        }
        a.mAccumulated = b.mAccumulated = accumulated;
        bool started = run % 5 != 0 && !tie;
        uint64_t start = started ? 1 + Rand64(rng, 1 + rng() % 62) : 0;
        a.mStart = b.mStart = start;
        // The current counter relative to the start: before it, inside the overhead, at its edges,
        // just past it, far past it.
        uint64_t now;
        switch (rng() % 8) {
        case 0: now = start + overhead; break;
        case 1: now = start + overhead + 1; break;
        case 2: now = start + (overhead ? overhead - 1 : 0); break;
        case 3: now = start; break;
        case 4: now = start - 1 - rng() % 100; break; // before the start: the span wraps around
        case 5: now = start + Rand64(rng, 1 + rng() % 40); break;
        case 6: now = start + overhead + Rand64(rng, 1 + rng() % 30); break;
        default: now = Rand64(rng, 1 + rng() % 63); break;
        }
        gNow = now;
        gQpcCalls = 0;
        uint32_t ra = orig(&a);
        int callsA = gQpcCalls;
        gQpcCalls = 0;
        uint32_t rb = b.Elapsed();
        ++ctx.cases;
        if (ra != rb || callsA != gQpcCalls || callsA != (started ? 1 : 0) || std::memcmp(&a, &b, sizeof(a)) != 0) {
            ctx.Fail("mode %d start %llx acc %llx now %llx overhead %llx: %u/%u, counter reads %d/%d", mode, start, accumulated, now, overhead, ra, rb, callsA, gQpcCalls);
        }
    }
    // Mode 0 reads the time-stamp counter: not scriptable. With the span far below the overhead the
    // result is accumulated + 1, and with a huge overhead-free span it grows by about the elapsed time.
    SetCalibration(ctx, ~0ull >> 1, {1, 1, 1, 1, 1});
    for (int run = 0; run < 50; ++run) {
        cRZStopwatch a, b;
        ctor(&a, 0);
        b.Construct(0);
        a.mAccumulated = b.mAccumulated = 1000 + run;
        a.mStart = b.mStart = 1;
        ++ctx.cases;
        uint32_t ra = orig(&a), rb = b.Elapsed();
        if (ra != rb || ra != 1001u + run) ctx.Fail("mode 0 within the overhead: %u/%u", ra, rb);
    }
    SetCalibration(ctx, 0, {1, 1, 1, 1, 1});
    for (int run = 0; run < 50; ++run) {
        cRZStopwatch a, b;
        ctor(&a, 0);
        b.Construct(0);
        // A start in the recent past: the two readings are taken moments apart.
        uint64_t t = 0;
        cRZStopwatch::ReadTsc(&t);
        a.mStart = b.mStart = t - 1000000;
        a.mAccumulated = b.mAccumulated = 0;
        uint32_t ra = orig(&a), rb = b.Elapsed();
        ++ctx.cases;
        int32_t diff = static_cast<int32_t>(rb - ra);
        if (diff < 0 || diff > 2000000000 || ra < 1000000) ctx.Fail("mode 0: %u then %u", ra, rb);
    }
}
