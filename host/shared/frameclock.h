#pragma once
// The frame clock's arithmetic (TS2HOST_FRAMECLOCK, host/control_frameclock.cpp, docs/ui-testing.md). A diagnostic
// control like the rest of host/: it stands in for the operating system's clocks, never for game code. Inline here so
// the difftest runner checks it without the host (tests/host_frameclock.cpp).
//
// Virtual time is counted in 100 ns units (FILETIME's). There is one global virtual "now": each framework tick (the
// host's frame counter, ControlFrame) moves it on by one frame time (tick N starts at kBase + N * frame unless virtual
// waits went past that), and the lockstep scheduler (host/shared/lockstep.h) moves it on while the main thread sleeps
// or waits with a time-out. Every read on a thread returns
//
//     max(that thread's previous read + step, now (+ stall))
//
// where step is 0 for the thread's first `still` reads in a tick and kStep after them. So:
//   - every read in a tick returns the same value until the thread has read `still` times: how many reads a frame
//     makes (a message more or less, a poll more or less) does not change the time it sees;
//   - past that, time moves kStep per read, so a loop that waits for the clock to move still finishes;
//   - time never runs backwards on a thread; a thread that ran ahead (many reads, or a sleep: Advance) continues from
//     where it was until now catches up.
// still = 0 is the first version's rule (a step on every read).
//
// stall (off unless stallGraceMs is set): a tick that has lasted longer than stallGraceMs in real time lets virtual
// time follow real time at 1 / kStallDivisor speed past that point. That part is real-time driven, so it is not
// deterministic; the lockstep scheduler's virtual sleeps and time-outs make it unnecessary (waits for a fixed time end
// by virtual time).
#include <atomic>
#include <cstdint>

namespace frameclock {

constexpr uint64_t kUnitsPerSecond = 10'000'000;      // 100 ns units
constexpr uint64_t kUnitsPerMs = 10'000;
constexpr uint64_t kDefaultFrameUs = 33'333;           // 1/30 s
constexpr uint64_t kBase = 86'400ull * kUnitsPerSecond; // virtual time at tick 0: one day (GetTickCount 86 400 000)
constexpr uint64_t kStep = 10;                          // 1 us per read
constexpr uint32_t kStallGraceMs = 500;                 // the first version's grace (Configure's default is off)
constexpr uint32_t kStallDivisor = 8;
constexpr uint32_t kDefaultStill = 1024;
// Virtual wall clock: kBase is 2026-01-01 00:00:00 UTC (days 1601-1970 plus 1970-2026).
constexpr uint64_t kFileTimeAtBase = (134'774ull + 20'454ull) * 86'400ull * kUnitsPerSecond;
// Time-stamp counter ticks per unit: a 3 GHz counter, so start-up's CPU speed probe (0x1cb3b0) reads a plausible
// speed and the stopwatch calibration (cRZStopwatch::MeasureFrequency) gets a fixed rate.
constexpr uint64_t kTscPerUnit = 300;

// The current tick and the real time (ms since the clock started) it began at, in one word so a reader on another
// thread never pairs a new tick with the previous tick's start.
inline uint64_t PackTick(uint32_t tick, uint32_t realMs) { return static_cast<uint64_t>(tick) << 32 | realMs; }

// One thread's view: its previous read, and how many reads it made in the current tick.
struct ThreadClock {
    uint64_t last = 0;
    uint32_t tick = 0xffffffff;
    uint32_t reads = 0;
};

class Clock {
public:
    // frameUs: the nominal frame time in microseconds (0: kDefaultFrameUs). still: reads per tick and thread that see
    // the same time. stallGraceMs: 0 = time never follows real time.
    void Configure(uint64_t frameUs, uint32_t still = 0, uint32_t stallGraceMs = kStallGraceMs)
    {
        mFrame = (frameUs ? frameUs : kDefaultFrameUs) * 10;
        mStill = still;
        mStallGraceMs = stallGraceMs;
        mTick.store(PackTick(0, 0));
        mNow.store(kBase);
        mStart = kBase;
        mStartTick = 0;
        mStarted = false;
    }
    uint64_t FrameUnits() const { return mFrame; }
    uint32_t Still() const { return mStill; }
    uint32_t StallGraceMs() const { return mStallGraceMs; }

    // A new tick began at real time realMs (main thread, from the host's frame counter). It starts one frame after the
    // previous tick started (TickTime(tick) when it doesn't follow the previous one), or at now if virtual sleeps
    // during the previous tick took longer: time passes by at least a frame per tick, so a long virtual wait never
    // leaves the following frames standing still.
    void OnTick(uint32_t tick, uint32_t realMs)
    {
        const uint64_t start = mStartTick + 1 == tick && mStarted ? mStart + mFrame : TickTime(tick);
        AdvanceTo(start);
        mStart = Now();
        mStartTick = tick;
        mStarted = true;
        mTick.store(PackTick(tick, realMs)); // after now: a reader that sees the tick sees its start
    }
    uint32_t Tick() const { return static_cast<uint32_t>(mTick.load() >> 32); }
    uint64_t TickTime(uint32_t tick) const { return kBase + static_cast<uint64_t>(tick) * mFrame; }
    uint64_t TickStart() const { return mStart; } // the current tick's start (main thread)

    // The global virtual now; AdvanceTo moves it on (never back).
    uint64_t Now() const { return mNow.load(); }
    void AdvanceTo(uint64_t t)
    {
        uint64_t now = mNow.load();
        while (t > now && !mNow.compare_exchange_weak(now, t)) {
        }
    }

    // What `c`'s thread sees as now without reading (the start of a sleep or a time-out).
    uint64_t ThreadNow(const ThreadClock &c) const
    {
        const uint64_t now = mNow.load();
        return c.last > now ? c.last : now;
    }

    // The next read for thread `c` at real time realMs. stalled (optional) is set when the stall rule decided it.
    uint64_t Read(ThreadClock &c, uint32_t realMs, bool *stalled = nullptr) const
    {
        const uint64_t packed = mTick.load();
        const uint32_t tick = static_cast<uint32_t>(packed >> 32), began = static_cast<uint32_t>(packed);
        if (c.tick != tick) c.tick = tick, c.reads = 0;
        if (c.reads != 0xffffffff) ++c.reads;
        uint64_t floor = mNow.load(); // OnTick moved it to the tick's start before publishing the tick
        bool byStall = false;
        if (mStallGraceMs) {
            // A real time taken just before the tick was published counts as the tick's start.
            const uint32_t since = static_cast<int32_t>(realMs - began) > 0 ? realMs - began : 0;
            if (since > mStallGraceMs) {
                floor += static_cast<uint64_t>(since - mStallGraceMs) * kUnitsPerMs / kStallDivisor;
                byStall = true;
            }
        }
        uint64_t next = c.last + (c.reads > mStill ? kStep : 0);
        if (stalled) *stalled = byStall && floor > next;
        if (floor > next) next = floor;
        c.last = next;
        return next;
    }

    // A sleep or time-out of `units` on thread `c` that ended by virtual time: its clock continues from the end.
    void Slept(ThreadClock &c, uint64_t until) const
    {
        if (until > c.last) c.last = until;
    }

private:
    uint64_t mFrame = kDefaultFrameUs * 10;
    uint32_t mStill = 0;
    uint32_t mStallGraceMs = kStallGraceMs;
    std::atomic<uint64_t> mTick{0};
    std::atomic<uint64_t> mNow{kBase};
    uint64_t mStart = kBase; // main thread only
    uint32_t mStartTick = 0;
    bool mStarted = false;
};

// Conversions of a virtual time to what each clock function returns.
inline uint64_t ToCounter(uint64_t units, uint64_t frequency) // QueryPerformanceCounter, at QueryPerformanceFrequency
{
    return units / kUnitsPerSecond * frequency + units % kUnitsPerSecond * frequency / kUnitsPerSecond;
}
inline uint32_t ToMilliseconds(uint64_t units) { return static_cast<uint32_t>(units / kUnitsPerMs); } // GetTickCount
inline uint64_t ToFileTime(uint64_t units) { return kFileTimeAtBase + (units - kBase); }              // UTC
inline uint64_t ToTsc(uint64_t units) { return units * kTscPerUnit; }
inline uint64_t FromMilliseconds(uint32_t ms) { return static_cast<uint64_t>(ms) * kUnitsPerMs; }

} // namespace frameclock
