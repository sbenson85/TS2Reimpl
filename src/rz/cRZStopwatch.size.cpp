#include "rz/cRZStopwatch.h"

#include "game/game.h"
#include "game/import_slots.h"
#include "rz/cRZStopwatchCalibration.h"

#include <windows.h>
#include <intrin.h>

void __cdecl cRZStopwatch::ReadTsc(uint64_t *out)
{
    if (sTimeStampCounterOverride) {
        sTimeStampCounterOverride(out);
        return;
    }
    // cpuid serialises the instruction stream before the read, as in the original.
    int regs[4];
    __cpuid(regs, 0);
    *out = __rdtsc();
}

int32_t __cdecl cRZStopwatch::ReadPerformanceCounter(uint64_t *out)
{
    if (sPerformanceCounterOverride) {
        sPerformanceCounterOverride(out);
        return TRUE;
    }
    return ImportSlot<BOOL(WINAPI *)(LARGE_INTEGER *), &ts2_rva_d9a24c>()(reinterpret_cast<LARGE_INTEGER *>(out));
}

cRZStopwatch *cRZStopwatch::Construct(int32_t mode)
{
    mMode = mode;
    mStart = 0;
    mAccumulated = 0;
    return this;
}

void cRZStopwatch::Start()
{
    if (mMode == 0) ReadTsc(&mStart);
    else ReadPerformanceCounter(&mStart);
}

uint32_t cRZStopwatch::Elapsed() const
{
    uint64_t total = mAccumulated;
    if (mStart != 0) {
        uint64_t now = 0;
        if (mMode == 0) ReadTsc(&now);
        else ReadPerformanceCounter(&now);
        uint64_t span = now - mStart;
        uint64_t overhead = ts2_rva_10cb648.overhead;
        // A span within the reading overhead still counts as one tick.
        total += span > overhead ? span - overhead : 1;
    }
    if (mMode == 0 || mMode == 1) return static_cast<uint32_t>(total);
    if (mMode < 2 || mMode > 6) return 0;
    float scale = ts2_rva_10cb648.scale[mMode - 2];
    // 64-bit integer to double, to float, scaled in float, then back through a 64-bit integer
    // (the CRT's _ltod3 and _ftol3); the low 32 bits are the result.
    float scaled = static_cast<float>(static_cast<double>(static_cast<int64_t>(total))) * scale;
    return static_cast<uint32_t>(static_cast<int64_t>(scaled));
}

namespace {
constexpr IatSlot kIatQueryPerformanceCounter = &ts2_rva_d9a24c;
constexpr IatSlot kIatQueryPerformanceFrequency = &ts2_rva_d9a250;
constexpr IatSlot kIatSleepEx = &ts2_rva_d9a188;

// 64-bit integer to double (the CRT's _ltod3: signed).
double ToDouble(uint64_t value)
{
    return static_cast<double>(static_cast<int64_t>(value));
}
} // namespace

void cRZStopwatch::SetCount(int64_t units)
{
    if (mMode == 0 || mMode == 1) {
        mAccumulated = static_cast<uint64_t>(units);
    } else if (mMode >= 2 && mMode <= 6) {
        float scaled = static_cast<float>(static_cast<double>(units)) * ts2_rva_10cb648.inverse[mMode - 2];
        const float k2p63 = 9223372036854775808.0f;
        uint64_t bias = 0;
        if (scaled >= k2p63) {
            scaled -= k2p63;
            if (k2p63 > scaled) bias = 0x8000000000000000ull;
        }
        mAccumulated = static_cast<uint64_t>(static_cast<int64_t>(scaled)) + bias;
    }
    if (mMode == 0) ReadTsc(&mStart);
    else ReadPerformanceCounter(&mStart);
}

void __cdecl cRZStopwatch::MeasureFrequency(uint64_t *performanceFrequency, uint64_t *out)
{
    using CounterRead = BOOL(__stdcall *)(uint64_t *);
    *out = 0;
    uint64_t *cache = ts2_rva_10cb63c;
    if (cache) *out = *cache;
    CounterRead queryCounter = ImportSlot<CounterRead, kIatQueryPerformanceCounter>();
    for (int attempt = 0; attempt < 5; ++attempt) {
        uint64_t performance, stamp;
        queryCounter(&performance);
        ReadTsc(&stamp);
        double ratio = ToDouble(stamp) / ToDouble(performance);
        if (ratio > 0.98 && 1.02 > ratio) {
            ImportSlot<CounterRead, kIatQueryPerformanceFrequency>()(out);
            break;
        }
    }
    if (*out == 0) {
        uint64_t frequency, performanceStart, stampStart, performanceNow, stampNow;
        ImportSlot<CounterRead, kIatQueryPerformanceFrequency>()(&frequency);
        queryCounter(&performanceStart);
        ReadTsc(&stampStart);
        double seconds;
        do {
            ImportSlot<unsigned long(__stdcall *)(unsigned long, int), kIatSleepEx>()(100, 1);
            queryCounter(&performanceNow);
            ReadTsc(&stampNow);
            seconds = ToDouble(performanceNow - performanceStart) / ToDouble(frequency);
        } while (0.33 > seconds);
        double rate = ToDouble(stampNow - stampStart) / seconds;
        const double k2p63 = 9223372036854775808.0;
        uint64_t bias = 0;
        if (rate >= k2p63) {
            rate -= k2p63;
            if (k2p63 > rate) bias = 0x8000000000000000ull;
        }
        *out = static_cast<uint64_t>(static_cast<int64_t>(rate)) + bias;
    }
    uint64_t value = *out;
    if (cache) *cache = value;
    ImportSlot<CounterRead, kIatQueryPerformanceFrequency>()(performanceFrequency);
}
