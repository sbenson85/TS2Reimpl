#pragma once
// The stopwatch's start-up calibration (lane HQ): the static object at 0x10cb640 (XC slot 105, entry 0x1383aa:
// `mov ecx, 0x10cb640; jmp 0x1c8d8e`; no destructor). Its constructor measures both counters' rates with the process at
// real-time priority and fills the two timebases cRZStopwatch reads: the performance counter's (cRZStopwatch::kOverheadRva,
// kScalesRva, kInverseScalesRva; every mode, 0 included, subtracts that overhead) and the time-stamp counter's, which
// is written only: nothing in the image reads 0x10cb678 (lane FA, xrefs).
#include "game/owned.h"

#include <cstddef>
#include <cstdint>

#pragma pack(push, 4) // the game packs its 64-bit fields to 4
// One counter's figures (0x30 bytes): the cost of a read (the least of eight spans between two back-to-back reads;
// cRZStopwatch::Elapsed takes spans up to it as one tick) and per unit (nanoseconds, microseconds, milliseconds, seconds,
// 60ths of a second) units per tick (unitsPerSecond / frequency) and its reciprocal, ticks per unit.
struct cRZStopwatchTimebase {
    uint64_t overhead;   // +0x00
    float scale[5];      // +0x08
    float inverse[5];    // +0x1c
};
#pragma pack(pop)
static_assert(sizeof(cRZStopwatchTimebase) == 0x30, "cRZStopwatchTimebase size");

class cRZStopwatchCalibration {
public:
    static constexpr uint32_t kRva = 0x10cb640;
    static constexpr uint32_t kPerformanceTimebaseRva = 0x10cb648; // = cRZStopwatch::kOverheadRva
    static constexpr uint32_t kStampTimebaseRva = 0x10cb678;
    // cRZStopwatch::MeasureFrequency's two outputs: QueryPerformanceFrequency's answer and the time-stamp counter's rate.
    static constexpr uint32_t kPerformanceFrequencyRva = 0xff2480;
    static constexpr uint32_t kStampFrequencyRva = 0xff2488;

    // 0x1c8d8e (thiscall, returns this; the object itself is never written). The process priority class and the
    // thread priority are saved, set to REALTIME / TIME_CRITICAL around cRZStopwatch::MeasureFrequency, and restored.
    // Then per counter: the timebase from its frequency (int64 to double to float, each unit's count divided by it in
    // float, the reciprocals of those), then the overhead from eight pairs of reads.
    cRZStopwatchCalibration *Construct();

    // Fills `timebase` (but its overhead) from a frequency, as the constructor does for each counter.
    static void SetRates(cRZStopwatchTimebase *timebase, uint64_t frequency);
    // The least span between two back-to-back reads of a counter over eight tries (from all ones; unsigned): the
    // performance counter (cRZStopwatch::ReadPerformanceCounter) or the time-stamp counter (cRZStopwatch::ReadTsc).
    static uint64_t MeasureOverhead(bool stampCounter);
};

// Owned statics (game/owned.h; lane FA). The image-free program defines them and constructs the calibration object
// from its start-up (standalone/owned_statics.cpp, the XC slot 105 order); a hooked game's own initialiser constructs
// the image's, so nothing of ours runs it there.
TS2_OWNED_STATIC(cRZStopwatchCalibration, 10cb640);  // kRva
TS2_OWNED_STATIC(cRZStopwatchTimebase, 10cb648);     // the performance counter's (modes 1..6)
TS2_OWNED_STATIC(cRZStopwatchTimebase, 10cb678);     // the time-stamp counter's (written, never read)
TS2_OWNED_STATIC(uint64_t, ff2480);                  // QueryPerformanceFrequency's answer
TS2_OWNED_STATIC(uint64_t, ff2488);                  // the time-stamp counter's rate
// cRZStopwatch::MeasureFrequency's cache: the view of the cRZTimer section (the 8-byte per-process named mapping
// "cRZTimer%08x", XC slot 104, its object at 0x10cb638 being { mapping, view }). Null in an image-free program until
// that section is ours: no cache, which for the one calibration a process runs gives what a fresh, zeroed view does.
TS2_OWNED_STATIC(uint64_t *, 10cb63c);
