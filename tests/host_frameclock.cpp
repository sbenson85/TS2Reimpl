// The frame clock's arithmetic (host/shared/frameclock.h; TS2HOST_FRAMECLOCK, host/control_frameclock.cpp). Not a
// differential test (no original function): rva 0. What the host relies on: the first read of each tick is the tick's
// time whatever real time did, reads on a thread never stand still or run backwards, a thread that ran ahead continues
// from where it was until the ticks catch up, a tick longer than the grace lets time follow real time slowly, and the
// conversions to each clock function's unit. Version 2: the first `still` reads of a tick stand still, virtual time
// moved on by waits (AdvanceTo, Slept) is never undone, a tick starts a frame after the previous one or at now, and
// with the grace 0 real time never matters.
#include "difftest.h"

#include "../host/shared/frameclock.h"

#include <windows.h>

#include <cstdint>
#include <vector>

namespace {

using namespace frameclock;

constexpr uint64_t kFrame = 333'330; // 33 333 us

} // namespace

DIFF_TEST(HostFrameClock_Ticks, 0)
{
    Clock clock;
    clock.Configure(33'333, 0, kStallGraceMs);
    if (clock.FrameUnits() != kFrame) ctx.Fail("frame units %llu", static_cast<unsigned long long>(clock.FrameUnits()));
    Clock defaulted;
    defaulted.Configure(0);
    if (defaulted.FrameUnits() != kDefaultFrameUs * 10) ctx.Fail("default frame units");

    // Tick 0: the first read is the base, then one step per read.
    ThreadClock last;
    bool stalled = true;
    uint64_t v = clock.Read(last, 0, &stalled);
    if (v != kBase || last.last != kBase || stalled) ctx.Fail("tick 0 first read %llu", static_cast<unsigned long long>(v));
    v = clock.Read(last, 3, &stalled);
    if (v != kBase + kStep) ctx.Fail("tick 0 second read %llu", static_cast<unsigned long long>(v));
    ++ctx.cases;

    // Each tick's first read is exactly its time, whatever the real time (inside the grace).
    for (uint32_t tick = 1; tick < 50; ++tick) {
        clock.OnTick(tick, tick * 7);
        for (int k = 0; k < static_cast<int>(tick % 5); ++k) clock.Read(last, tick * 7 + 400);
        const uint32_t real = tick * 7 + (tick * 37) % kStallGraceMs;
        clock.OnTick(tick + 1, tick * 7);
        v = clock.Read(last, real, &stalled);
        if (v != kBase + (tick + 1) * kFrame || stalled)
            ctx.Fail("tick %u first read %llu, expected %llu", tick + 1, static_cast<unsigned long long>(v),
                     static_cast<unsigned long long>(kBase + (tick + 1) * kFrame));
        ++ctx.cases;
    }
    if (clock.Tick() != 50) ctx.Fail("Tick() %u", clock.Tick());
}

DIFF_TEST(HostFrameClock_Threads, 0)
{
    Clock clock;
    clock.Configure(33'333, 0, kStallGraceMs);
    clock.OnTick(10, 1000);
    // Two threads' sequences are independent: each starts at the tick's time.
    ThreadClock a, b;
    for (int i = 0; i < 5; ++i) clock.Read(a, 1000);
    const uint64_t firstB = clock.Read(b, 1000);
    if (firstB != kBase + 10 * kFrame) ctx.Fail("second thread's first read %llu", static_cast<unsigned long long>(firstB));
    if (a.last != kBase + 10 * kFrame + 4 * kStep) ctx.Fail("first thread after 5 reads %llu", static_cast<unsigned long long>(a.last));
    ++ctx.cases;

    // Same reads at different real times (inside the grace) give the same values: frame N is the same on every run.
    Clock other;
    other.Configure(33'333, 0, kStallGraceMs);
    std::vector<uint64_t> run1, run2;
    ThreadClock l1, l2;
    for (uint32_t tick = 0; tick < 20; ++tick) {
        clock.OnTick(tick + 100, tick * 50);
        other.OnTick(tick + 100, tick * 90 + 13);
        for (uint32_t k = 0; k < tick % 7 + 1; ++k) {
            run1.push_back(clock.Read(l1, tick * 50 + k * 3));
            run2.push_back(other.Read(l2, tick * 90 + 13 + k * 61));
        }
    }
    if (run1 != run2) ctx.Fail("same reads, different real times: the values differ");
    ++ctx.cases;
}

DIFF_TEST(HostFrameClock_AheadAndStall, 0)
{
    Clock clock;
    clock.Configure(33'333, 0, kStallGraceMs);
    ThreadClock last;
    clock.OnTick(1, 0);
    // More reads in one tick than the frame holds: the thread runs ahead of the next tick ...
    const uint64_t reads = kFrame / kStep + 1000;
    for (uint64_t i = 0; i < reads; ++i) clock.Read(last, 10);
    const uint64_t ahead = last.last;
    if (ahead != kBase + kFrame + (reads - 1) * kStep) ctx.Fail("after %llu reads %llu", static_cast<unsigned long long>(reads),
                                                                static_cast<unsigned long long>(ahead));
    clock.OnTick(2, 20);
    uint64_t v = clock.Read(last, 20);
    // ... and continues from where it was (never backwards) ...
    if (v != ahead + kStep) ctx.Fail("tick 2 while ahead: %llu", static_cast<unsigned long long>(v));
    // ... until the ticks catch up.
    clock.OnTick(3, 30);
    v = clock.Read(last, 30);
    if (v != kBase + 3 * kFrame) ctx.Fail("tick 3 caught up: %llu", static_cast<unsigned long long>(v));
    ++ctx.cases;

    // A long tick: inside the grace time stands at the tick (plus steps); past it, it follows real time / divisor.
    clock.OnTick(4, 1000);
    ThreadClock t;
    bool stalled = true;
    v = clock.Read(t, 1000 + kStallGraceMs, &stalled);
    if (v != kBase + 4 * kFrame || stalled) ctx.Fail("at the grace: %llu stalled %d", static_cast<unsigned long long>(v), stalled);
    v = clock.Read(t, 1000 + kStallGraceMs + 800, &stalled);
    const uint64_t expected = kBase + 4 * kFrame + 800ull * kUnitsPerMs / kStallDivisor;
    if (v != expected || !stalled) ctx.Fail("800 ms past the grace: %llu, expected %llu", static_cast<unsigned long long>(v),
                                            static_cast<unsigned long long>(expected));
    // A step-sized advance within the stall still counts as the stall's (floor above the step).
    v = clock.Read(t, 1000 + kStallGraceMs + 800, &stalled);
    if (v != expected + kStep || stalled) ctx.Fail("repeat read in a stall: %llu stalled %d", static_cast<unsigned long long>(v), stalled);
    // A real time from just before the tick was published counts as the tick's start, not as a huge stall.
    clock.OnTick(5, 5000);
    ThreadClock u;
    v = clock.Read(u, 4999, &stalled);
    if (v != kBase + 5 * kFrame || stalled) ctx.Fail("real time before the tick: %llu", static_cast<unsigned long long>(v));
    ++ctx.cases;
}

DIFF_TEST(HostFrameClock_Conversions, 0)
{
    if (ToMilliseconds(kBase) != 86'400'000u) ctx.Fail("GetTickCount at the base %u", ToMilliseconds(kBase));
    if (ToMilliseconds(kBase + 9'999) != 86'400'000u || ToMilliseconds(kBase + 10'000) != 86'400'001u) ctx.Fail("ms rounding");
    // The counter at 10 MHz is the units themselves; at other frequencies the exact product, without overflow.
    if (ToCounter(kBase + 12'345, 10'000'000) != kBase + 12'345) ctx.Fail("counter at 10 MHz");
    const uint64_t units = kBase + 3 * kUnitsPerSecond + 1'234'567;
    const uint64_t f = 3'579'545;
    // units * f / 1e7 exactly, with 64-bit pieces: (whole seconds * f) + (rest * f / 1e7).
    const uint64_t expected = (units / kUnitsPerSecond) * f + (units % kUnitsPerSecond) * f / kUnitsPerSecond;
    const uint64_t got = ToCounter(units, f);
    if (got != expected) ctx.Fail("counter at 3.58 MHz %llu", static_cast<unsigned long long>(got));
    // ... which is the real quotient to within one count.
    const double exact = static_cast<double>(units) * static_cast<double>(f) / 1e7;
    if (static_cast<double>(got) > exact + 1 || static_cast<double>(got) < exact - 1) ctx.Fail("counter quotient");
    if (ToTsc(kBase + 1) - ToTsc(kBase) != kTscPerUnit) ctx.Fail("tsc rate");
    // Wall clock: the base is 2026-01-01 00:00:00 UTC.
    SYSTEMTIME st{};
    st.wYear = 2026;
    st.wMonth = 1;
    st.wDay = 1;
    FILETIME ft{};
    SystemTimeToFileTime(&st, &ft);
    const uint64_t base = static_cast<uint64_t>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
    if (ToFileTime(kBase) != base) ctx.Fail("file time at the base %llx, expected %llx", static_cast<unsigned long long>(ToFileTime(kBase)),
                                            static_cast<unsigned long long>(base));
    if (ToFileTime(kBase + 77) != base + 77) ctx.Fail("file time steps");
    ctx.cases += 7;
}

DIFF_TEST(HostFrameClock_Still, 0)
{
    // still = 4: the first four reads of each tick and thread see the same time, then a step per read.
    Clock clock;
    clock.Configure(33'333, 4, 0);
    clock.OnTick(1, 0);
    ThreadClock c;
    std::vector<uint64_t> seen;
    for (int i = 0; i < 6; ++i) seen.push_back(clock.Read(c, 0));
    const uint64_t t1 = kBase + kFrame;
    const std::vector<uint64_t> want = {t1, t1, t1, t1, t1 + kStep, t1 + 2 * kStep};
    if (seen != want) ctx.Fail("still 4: reads %llu %llu %llu %llu %llu %llu", static_cast<unsigned long long>(seen[0]),
                               static_cast<unsigned long long>(seen[1]), static_cast<unsigned long long>(seen[2]),
                               static_cast<unsigned long long>(seen[3]), static_cast<unsigned long long>(seen[4]),
                               static_cast<unsigned long long>(seen[5]));
    // The count starts again at the next tick: one read more or less in a tick changes nothing after it.
    clock.OnTick(2, 0);
    ThreadClock d = c;
    uint64_t v = clock.Read(c, 0);
    if (v != kBase + 2 * kFrame || clock.Read(c, 0) != v) ctx.Fail("still: tick 2");
    clock.Read(d, 0);
    clock.Read(d, 0);
    clock.Read(d, 0);
    if (clock.Read(d, 0) != v) ctx.Fail("still: a thread with more reads in the tick sees the same time");
    ++ctx.cases;

    // Grace 0: a tick that lasts long in real time still reads its own time (no stall).
    bool stalled = true;
    clock.OnTick(3, 100);
    ThreadClock e;
    v = clock.Read(e, 100 + 60'000, &stalled);
    if (v != kBase + 3 * kFrame || stalled) ctx.Fail("grace 0: %llu stalled %d", static_cast<unsigned long long>(v), stalled);
    if (clock.StallGraceMs() != 0 || clock.Still() != 4) ctx.Fail("Configure's settings");
    ++ctx.cases;
}

DIFF_TEST(HostFrameClock_Virtual, 0)
{
    Clock clock;
    clock.Configure(33'333, 0, 0);
    clock.OnTick(1, 0);
    // AdvanceTo moves now on, never back; every thread's next read is at least now.
    clock.AdvanceTo(kBase + kFrame + 5'000);
    clock.AdvanceTo(kBase + kFrame + 1'000);
    if (clock.Now() != kBase + kFrame + 5'000) ctx.Fail("AdvanceTo went back: %llu", static_cast<unsigned long long>(clock.Now()));
    ThreadClock c;
    if (clock.ThreadNow(c) != clock.Now()) ctx.Fail("ThreadNow of a new thread");
    if (clock.Read(c, 0) != kBase + kFrame + 5'000) ctx.Fail("read after AdvanceTo");
    // A thread ahead of now: ThreadNow is its own time; Slept moves it to the end of a wait, never back.
    ThreadClock ahead;
    ahead.last = kBase + kFrame + 9'000;
    if (clock.ThreadNow(ahead) != ahead.last) ctx.Fail("ThreadNow of a thread ahead");
    clock.Slept(ahead, kBase + kFrame + 8'000);
    if (ahead.last != kBase + kFrame + 9'000) ctx.Fail("Slept went back");
    clock.Slept(ahead, kBase + kFrame + 9'001);
    if (ahead.last != kBase + kFrame + 9'001) ctx.Fail("Slept lost a microsecond");
    clock.Slept(ahead, kBase + kFrame + 20'000);
    if (clock.Read(ahead, 0) != kBase + kFrame + 20'000 + kStep) ctx.Fail("read after Slept");
    ++ctx.cases;

    // Virtual waits in tick 1 went past tick 2's time: tick 2 starts at now, tick 3 a frame later (time never stands
    // still across ticks), and a tick that doesn't follow the previous one starts at its own time or now.
    clock.AdvanceTo(kBase + 3 * kFrame);
    clock.OnTick(2, 0);
    if (clock.TickStart() != kBase + 3 * kFrame || clock.Now() != kBase + 3 * kFrame) ctx.Fail("tick 2 after a long wait");
    clock.OnTick(3, 0);
    if (clock.TickStart() != kBase + 4 * kFrame) ctx.Fail("tick 3: %llu", static_cast<unsigned long long>(clock.TickStart()));
    ThreadClock d;
    if (clock.Read(d, 0) != kBase + 4 * kFrame) ctx.Fail("tick 3 read");
    clock.OnTick(10, 0);
    if (clock.TickStart() != kBase + 10 * kFrame) ctx.Fail("tick 10 (a jump)");
    clock.OnTick(10, 0);
    if (clock.TickStart() != kBase + 10 * kFrame) ctx.Fail("tick 10 again");
    ++ctx.cases;

    // Two runs whose reads come at different real times and in different numbers within a tick (below still) agree.
    Clock r1, r2;
    r1.Configure(33'333, 64, 0);
    r2.Configure(33'333, 64, 0);
    ThreadClock a, b;
    for (uint32_t tick = 0; tick < 30; ++tick) {
        r1.OnTick(tick, tick * 10);
        r2.OnTick(tick, tick * 97 + 5);
        if (tick % 4 == 0) r1.AdvanceTo(r1.Now() + 1234), r2.AdvanceTo(r2.Now() + 1234); // the same virtual sleep
        uint64_t x = 0, y = 0;
        for (uint32_t k = 0; k < tick % 5 + 1; ++k) x = r1.Read(a, tick * 10 + k);
        for (uint32_t k = 0; k < tick % 3 + 1; ++k) y = r2.Read(b, tick * 97 + 900 * k);
        if (x != y) ctx.Fail("tick %u: %llu and %llu", tick, static_cast<unsigned long long>(x), static_cast<unsigned long long>(y));
    }
    ++ctx.cases;
}
