#pragma once
#include "rz/cRZStopwatch.h"

#include <cstdint>

// The cRZStopwatch methods that cRZStopwatch.h leaves out (RVAs 1.18.0.187), as free functions on
// the verified layout, so that file (another group's) stays untouched. They share the "accumulated
// count plus the span since Start" total with Elapsed (0x1c9256), counting a span within the reading
// overhead as one tick.
namespace stopwatch_more {

// 0x1c9368 (__thiscall, ret 4): the total as a float, written to *out. Modes 0 and 1: the count
// itself (64-bit integer to double to float); modes 2..6: that times the mode's scale factor;
// any other mode: 0.0f.
void __fastcall ElapsedAsFloat(const cRZStopwatch *self, void *edx, float *out);

// 0x1c948d (__thiscall, ret 4): the total as a 64-bit count, written to *out. Modes 0 and 1: the
// total; modes 2..6: the scaled float truncated to 64 bits, of which only the low 32 bits are kept
// (the high half is 0); any other mode: 0.
void __fastcall ElapsedAs64(const cRZStopwatch *self, void *edx, uint64_t *out);

// 0x1c9842 (__thiscall, no arguments): adds the span since Start (less the overhead, one tick if
// within it) to the accumulated count and clears the start. Does nothing when not started.
// ORIGINAL-BUG: reads QueryPerformanceCounter whatever the mode, though a mode 0 stopwatch started
// from the time-stamp counter: the span is then the difference of two unrelated counters.
void __fastcall Stop(cRZStopwatch *self, void *edx);

} // namespace stopwatch_more
