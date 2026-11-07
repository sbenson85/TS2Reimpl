#pragma once
#include <cstddef>
#include <cstdint>

// The game's stopwatch (0x18 bytes, no vtable; the class name is not known): a mode, a start
// counter value and an accumulated count. Used by the timer service, the callback thread and some
// forty other constructors/functions (RVAs 1.18.0.187: 0x1c9030 constructor, 0x1c982b Start, 0x1c9256
// Elapsed). These, the two counter readers, SetCount (0x1c960f) and the calibration probe
// MeasureFrequency (0x1c906a) are here; 0x1c9368, 0x1c948d and 0x1c9842 are in StopwatchMore.h.
//
// Modes: 0 reads the CPU time-stamp counter (after cpuid, so the read is serialised), every other
// mode reads QueryPerformanceCounter. Elapsed returns the accumulated count in a unit that depends
// on the mode: modes 0 and 1 the raw low 32 bits; modes 2..6 the count times one of five float
// scale factors the startup calibration stores in game globals (so they are read from the game).
//
// Tests replace the two counters through the pointers below; the default (null) is the real counter.
#pragma pack(push, 4) // the game packs its 64-bit fields to 4
class cRZStopwatch {
public:
    static constexpr uint32_t kSize = 0x18;
    // Game globals the original's calibration code fills in: a 64-bit count of ticks treated as the
    // cost of reading the counter (an elapsed span up to it counts as 1 tick), and the five float
    // scale factors for modes 2..6.
    static constexpr uint32_t kOverheadRva = 0x10cb648;
    static constexpr uint32_t kScalesRva = 0x10cb650;

    using CounterFn = void (*)(uint64_t *out);
    static inline CounterFn sPerformanceCounterOverride = nullptr; // test hook for QueryPerformanceCounter
    static inline CounterFn sTimeStampCounterOverride = nullptr;   // test hook for cpuid + rdtsc

    // The five float factors for modes 2..6 that turn units into counter ticks (0x10cb664, written by
    // the same start-up calibration as the scales above).
    static constexpr uint32_t kInverseScalesRva = 0x10cb664;
    // A game global holding a pointer (null normally) to a 64-bit cell that MeasureFrequency reads
    // and updates.
    static constexpr uint32_t kFrequencyCacheRva = 0x10cb63c;

    static void __cdecl ReadTsc(uint64_t *out);       // 0x1c9244
    static int32_t __cdecl ReadPerformanceCounter(uint64_t *out);  // 0x1c95c4 (returns QueryPerformanceCounter's BOOL)

    // 0x1c906a (cdecl, two pointers): the start-up calibration's frequency probe; the one caller
    // passes the two 64-bit globals 0x10ff2480 and 0x10ff2488. `out` (the second) receives the
    // time-stamp counter's ticks per second, `performanceFrequency` (the first) QueryPerformance-
    // Frequency's answer.
    // *out = 0, then *out = *cache when the cache global (above) is set. Up to five times it reads the
    // performance counter and the time-stamp counter; when the time-stamp counter is within
    // (0.98, 1.02) times the performance counter it asks QueryPerformanceFrequency(out) (the two
    // counters run at the same rate) and stops. If *out is still zero it measures: QPF, one reading of
    // both counters, SleepEx(100, TRUE) and readings again until the performance counter moved by at
    // least 0.33 s, then *out = the time-stamp counter ticks per second (unsigned conversion of the
    // double). The cache cell, when set, gets *out. Last, QueryPerformanceFrequency(performanceFrequency).
    static void __cdecl MeasureFrequency(uint64_t *performanceFrequency, uint64_t *out);

    cRZStopwatch *Construct(int32_t mode);   // 0x1c9030: mode, start and accumulated count zero (the dword at +4 is left alone)
    void Start();                            // 0x1c982b: start = the counter now
    // 0x1c960f (__thiscall, ret 8): sets the accumulated count from a count in the mode's unit and
    // restarts the clock. Modes 0 and 1: accumulated = units. Modes 2..6: units as a float (int64 to
    // double to float) times the mode's inverse scale, converted to a 64-bit count (values from 2^63 up
    // are taken as unsigned: 2^63 is subtracted before the conversion and added back after, and a value
    // of 2^64 or more converts as value minus 2^63 without the add-back). Other modes leave the
    // accumulated count alone. Then start = the counter now (time-stamp counter in mode 0, else
    // performance counter).
    void SetCount(int64_t units);
    uint32_t Elapsed() const;               // 0x1c9256: the accumulated count plus the span since Start, in the mode's unit

    int32_t mMode;         // 0x00
    uint32_t mUnused;      // 0x04 never written by the constructor
    uint64_t mStart;       // 0x08 counter value at Start (0 = not started)
    uint64_t mAccumulated; // 0x10
};
#pragma pack(pop)

static_assert(sizeof(cRZStopwatch) == cRZStopwatch::kSize, "cRZStopwatch size");
static_assert(offsetof(cRZStopwatch, mStart) == 0x08, "cRZStopwatch layout");
static_assert(offsetof(cRZStopwatch, mAccumulated) == 0x10, "cRZStopwatch layout");
