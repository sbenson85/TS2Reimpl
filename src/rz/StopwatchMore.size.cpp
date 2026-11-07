#include "rz/StopwatchMore.h"

#include "rz/cRZStopwatchCalibration.h"

namespace stopwatch_more {
namespace {

// The accumulated count plus the span since Start (the reading is the time-stamp counter in mode 0,
// the performance counter otherwise).
uint64_t Total(const cRZStopwatch *self)
{
    uint64_t total = self->mAccumulated;
    if (self->mStart != 0) {
        uint64_t now = 0;
        if (self->mMode == 0) cRZStopwatch::ReadTsc(&now);
        else cRZStopwatch::ReadPerformanceCounter(&now);
        uint64_t span = now - self->mStart;
        uint64_t overhead = ts2_rva_10cb648.overhead;
        total += span > overhead ? span - overhead : 1;
    }
    return total;
}

// 64-bit integer to double, to float (the CRT's _ltod3, then cvtsd2ss).
float ToFloat(uint64_t total)
{
    return static_cast<float>(static_cast<double>(static_cast<int64_t>(total)));
}

float Scale(int32_t mode)
{
    return ts2_rva_10cb648.scale[mode - 2];
}

} // namespace

void __fastcall ElapsedAsFloat(const cRZStopwatch *self, void *, float *out)
{
    uint64_t total = Total(self);
    float result;
    if (self->mMode == 0 || self->mMode == 1) result = ToFloat(total);
    else if (self->mMode >= 2 && self->mMode <= 6) result = ToFloat(total) * Scale(self->mMode);
    else result = 0.0f;
    *out = result;
}

void __fastcall ElapsedAs64(const cRZStopwatch *self, void *, uint64_t *out)
{
    uint64_t total = Total(self);
    uint64_t result;
    if (self->mMode == 0 || self->mMode == 1) {
        result = total;
    } else if (self->mMode >= 2 && self->mMode <= 6) {
        float scaled = ToFloat(total) * Scale(self->mMode);
        // _ftol3 gives a 64-bit integer; only the low half is kept.
        result = static_cast<uint32_t>(static_cast<int64_t>(scaled));
    } else {
        result = 0;
    }
    *out = result;
}

void __fastcall Stop(cRZStopwatch *self, void *)
{
    if (self->mStart == 0) return;
    uint64_t now = 0;
    cRZStopwatch::ReadPerformanceCounter(&now);
    uint64_t span = now - self->mStart;
    uint64_t overhead = ts2_rva_10cb648.overhead;
    self->mAccumulated += span > overhead ? span - overhead : 1;
    self->mStart = 0;
}

} // namespace stopwatch_more
