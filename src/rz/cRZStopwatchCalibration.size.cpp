#include "rz/cRZStopwatchCalibration.h"

#include "game/import_slots.h"
#include "rz/cRZStopwatch.h"

#include <windows.h>

namespace {
constexpr IatSlot kIatGetCurrentThread = &ts2_rva_d9a190;
constexpr IatSlot kIatSetThreadPriority = &ts2_rva_d9a198;
constexpr IatSlot kIatGetThreadPriority = &ts2_rva_d9a19c;
constexpr IatSlot kIatGetCurrentProcess = &ts2_rva_d9a1b8;
constexpr IatSlot kIatSetPriorityClass = &ts2_rva_d9a254;
constexpr IatSlot kIatGetPriorityClass = &ts2_rva_d9a258;

// Units per second of the stopwatch's modes 2..6: nanoseconds, microseconds, milliseconds, seconds, 60ths of a second.
constexpr float kUnitsPerSecond[5] = {1e9f, 1e6f, 1000.0f, 1.0f, 1.0f / 60.0f};
} // namespace

void cRZStopwatchCalibration::SetRates(cRZStopwatchTimebase *timebase, uint64_t frequency)
{
    // int64 to double (the CRT's _ltod3), then to float.
    const float ticksPerSecond = static_cast<float>(static_cast<double>(static_cast<int64_t>(frequency)));
    for (int i = 0; i < 5; ++i) timebase->scale[i] = kUnitsPerSecond[i] / ticksPerSecond;
    for (int i = 0; i < 5; ++i) timebase->inverse[i] = 1.0f / timebase->scale[i];
}

uint64_t cRZStopwatchCalibration::MeasureOverhead(bool stampCounter)
{
    uint64_t least = ~0ull;
    for (int i = 0; i < 8; ++i) {
        uint64_t first, second;
        if (stampCounter) {
            cRZStopwatch::ReadTsc(&first);
            cRZStopwatch::ReadTsc(&second);
        } else {
            cRZStopwatch::ReadPerformanceCounter(&first);
            cRZStopwatch::ReadPerformanceCounter(&second);
        }
        if (second - first < least) least = second - first;
    }
    return least;
}

cRZStopwatchCalibration *cRZStopwatchCalibration::Construct()
{
    HANDLE process = ImportSlot<HANDLE(WINAPI *)(), kIatGetCurrentProcess>()();
    const DWORD priorityClass = ImportSlot<DWORD(WINAPI *)(HANDLE), kIatGetPriorityClass>()(process);
    HANDLE thread = ImportSlot<HANDLE(WINAPI *)(), kIatGetCurrentThread>()();
    const int threadPriority = ImportSlot<int(WINAPI *)(HANDLE), kIatGetThreadPriority>()(thread);
    auto setPriorityClass = ImportSlot<BOOL(WINAPI *)(HANDLE, DWORD), kIatSetPriorityClass>();
    auto setThreadPriority = ImportSlot<BOOL(WINAPI *)(HANDLE, int), kIatSetThreadPriority>();
    setPriorityClass(process, REALTIME_PRIORITY_CLASS);
    setThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
    uint64_t *performanceFrequency = &ts2_rva_ff2480;
    uint64_t *stampFrequency = &ts2_rva_ff2488;
    cRZStopwatch::MeasureFrequency(performanceFrequency, stampFrequency);
    setPriorityClass(process, priorityClass);
    setThreadPriority(thread, threadPriority);

    auto *performance = &ts2_rva_10cb648;
    SetRates(performance, *performanceFrequency);
    performance->overhead = MeasureOverhead(false);
    auto *stamp = &ts2_rva_10cb678;
    SetRates(stamp, *stampFrequency);
    stamp->overhead = MeasureOverhead(true);
    return this;
}
