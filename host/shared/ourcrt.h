#pragma once
// Our toolchain's C runtime (the static UCRT linked into the host and the difftest runner) as the frame clock needs it
// (host/control_frameclock.cpp; docs/ui-testing.md, "Frame clock"). Our replacements create threads through the CRT's
// _beginthreadex (cRZThread's Start slots) and read the time through its clock() and _time64 (the speculative loader,
// GZCOM, nGZLog, ...). Those call the host's own imports (CreateThread, ExitThread, FreeLibraryAndExitThread,
// QueryPerformanceCounter, GetSystemTimeAsFileTime), not the image's, so the frame clock points the host's imports at
// itself too. Two facts about the CRT it needs are not exported; they are found here by calling the CRT once with one of
// the module's imports pointed at a probe (the probe answers only the probing thread: every other thread calling the
// import meanwhile gets the real function).
#define NOMINMAX
#include <windows.h>

#include <cstdint>

namespace ourcrt {

// `module`'s import of `name` from `dll` now goes to `function`; *previous (when given) gets what the slot held. False:
// the module doesn't import it by name.
bool PatchImport(HMODULE module, const char *dll, const char *name, void *function, void **previous = nullptr);

// The start routine our CRT's _beginthreadex hands CreateThread (its thread_start<_beginthreadex_proc_type, true>; the
// parameter it passes starts {procedure, context}, as the image CRT's does). Found by one _beginthreadex call with the
// module's CreateThread import pointed at a probe that records the routine and fails, so no thread is made (the CRT
// frees its parameter block; errno and the last error are restored). Null: not found.
const void *FindThreadStart(HMODULE module);

// Our CRT's clock(): (QueryPerformanceCounter - start) in milliseconds, -1 while the counter is below start. `start` is
// the counter the CRT read when it initialised (a static of its own); `site` is the return address of clock()'s one
// counter read (the frame clock tells clock()'s reads from other reads by it). Found by calling clock() with the
// module's QueryPerformanceCounter import answering probe values: start is the lowest value clock() doesn't answer -1
// for (a binary search between 0 and the real counter now). Not found: the CRT's start-up read failed (clock() is -1
// for ever) or the module doesn't import QueryPerformanceCounter.
struct ClockStart {
    bool found = false;
    int64_t start = 0;
    const void *site = nullptr;
    uint32_t calls = 0; // clock() calls the search took
};
ClockStart FindClockStart(HMODULE module);

// What clock()'s counter read should answer so that our clock() returns what the image CRT's clock() (0x83a46e, the same
// UCRT code) returns for the counter `counter`, given the image CRT's start count `imageStart` (-1: its start-up read
// failed, and its clock() is -1 for ever). Both scale (counter - start) by QueryPerformanceFrequency, so our clock() is
// fed our start plus the image's distance: start - 1 (below our start: -1) when imageStart is -1.
inline int64_t ClockCounter(const ClockStart &clock, int64_t counter, int64_t imageStart)
{
    return imageStart == -1 ? clock.start - 1 : clock.start + (counter - imageStart);
}

} // namespace ourcrt
