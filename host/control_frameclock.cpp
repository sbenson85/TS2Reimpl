// The frame clock (TS2HOST_FRAMECLOCK; docs/ui-testing.md): test control like host/control.cpp, nothing here stands in
// for the game's code. It makes the game's clocks tell time by the framework tick, and (version 2) schedules the game's
// threads in lockstep with it, so frame N sees the same time and the same work done on every run (the clock's
// arithmetic: host/shared/frameclock.h; the scheduling: host/shared/lockstep.h).
//
// TS2HOST_FRAMECLOCK=1 turns it on with a 1/30 s frame; a number from 1000 up is the frame time in microseconds
// (TS2HOST_FRAMECLOCK=16667: 1/60 s). TS2HOST_FRAMECLOCK_OPTS (comma- or space-separated) changes the defaults:
//   still=<n>        reads per tick and thread that see the same time (default 1024; 0: a step on every read, v1)
//   stall=<ms>       a tick longer than this in real time lets time follow real time at 1/8 speed (default 0: never;
//                    500 is v1, not deterministic)
//   threads=lockstep the game's threads run one at a time in a fixed order, and their sleeps and wait time-outs end by
//                    virtual time (default); threads=free: as Windows schedules them (v1)
//   timers=virtual   window timers (SetTimer with a window and no callback) fire by virtual time, posted at the tick
//                    (default); timers=real: Windows's
//   stuck=<ms>       lockstep ends when the thread holding the token makes no progress this long (default 10000; 0:
//                    never)
//   sound=off        no sound device (DirectSoundCreate fails): the sound mixer follows the device's real play cursor
//                    (default with threads=lockstep); sound=on keeps the device
//   heap=private     the image's process-heap calls go to a private heap whose block addresses repeat between runs
//                    (host/shared/privateheap.h; an experiment: the neighbourhood loads about twice as slowly and its
//                    frames still differ); heapmb=<n> its fixed part (default 512). Default heap=process
//   trace=<a>-<b>    ct/cs trace lines for ticks a to b in ts2devlog.txt; tracelocks=1 includes lock calls
// still=0,stall=500,threads=free,timers=real is the first version.
//
// Covered, on reference and replacement runs alike:
//   - the image's imports QueryPerformanceCounter, GetTickCount, timeGetTime, GetSystemTimeAsFileTime and GetLocalTime
//     (their import address table slots, the ones host/control_clocks.cpp counts; the clock census still works on top);
//   - the time-stamp counter: the 20 rdtsc instructions in the image become ud2 and a vectored handler answers them
//     (the stopwatch's ReadTsc 0x1c9244, the start-up CPU speed probe 0x1cb3b0, OpenSSL's entropy and a group of
//     small functions that pick one of four values by the counter's low bits, e.g. 0x5b7990);
//   - our cRZStopwatch, which reads the counters through its own imports: its two override hooks;
//   - our C runtime's clock() and _time64 (the speculative loader, GZCOM, nGZLog, ...): the host's own
//     QueryPerformanceCounter and GetSystemTimeAsFileTime imports, clock() answering what the image's clock() would;
//   - lockstep: the image's thread, wait, lock, APC and SendMessage/PeekMessage imports (kLockstepSlots), and the
//     host's own EnterCriticalSection (our cRZCriticalSection and GeneralAllocator call it directly), WaitForSingleObject
//     (cRZMutex), CreateThread (only for our CRT's _beginthreadex: the threads our cRZThread starts), ExitThread and
//     FreeLibraryAndExitThread (our CRT's _endthreadex) imports;
//   - timers=virtual: the image's SetTimer and KillTimer.
// QueryPerformanceFrequency is left alone (virtual counter values are in its unit). Not covered: message times,
// GetProcessTimes, other DLLs' clock reads and threads (the sound
// system's, Direct3D's, Steam's), and window messages from outside the game (input, quiet mode's window moves). The
// sound device's play cursor is not virtualised: sound=off (the lockstep default) removes the device instead.
//
// With TS2HOST_DEVLOG on, each tick also writes a "clk" line to ts2devlog.txt: what the clocks returned and how the
// threads were scheduled in that tick (tools/devlog.py compares them and names the first tick whose time differs).
#include "control.h"

#include "shared/frameclock.h"
#include "shared/lockstep.h"
#include "shared/ourcrt.h"
#include "shared/privateheap.h"
#include "shared/threadstack.h"

#include "game/game.h"
#include "hook/log.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZThread.h"

#define NOMINMAX
#include <windows.h>

#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Import address table slots (as host/control_clocks.cpp).
constexpr uint32_t kIatQpc = 0xd9a24c, kIatTickCount = 0xd9a0fc, kIatTimeGetTime = 0xd9a64c,
                   kIatSystemTimeAsFileTime = 0xd9a2a4, kIatLocalTime = 0xd9a1c8;
constexpr uint32_t kIatSetTimer = 0xd9a554, kIatKillTimer = 0xd9a558;

// The image's rdtsc instructions (0f 31), found by disassembling every function in db/functions.jsonl (1.18.0.187).
// 0x1c924c: cRZStopwatch::ReadTsc; 0x1cb464, 0x1cb49c: the CPU speed probe 0x1cb3b0; 0x116f44: OpenSSL's readtimer;
// the rest: small functions returning one of four table entries by the counter's low two bits.
constexpr uint32_t kRdtscSites[] = {0x116f44, 0x1c924c, 0x1cb464, 0x1cb49c, 0x5b7998, 0x5b79c5, 0x5b79f2,
                                    0x5b7a1f, 0x5b7a77, 0x621d11, 0x621d69, 0x621e98, 0x621ec5, 0x621ef2,
                                    0x64924a, 0x6492a2, 0x6492cf, 0x6492fc, 0x64d6db, 0x76e024};

struct Options {
    uint32_t still = frameclock::kDefaultStill;
    uint32_t stallMs = 0;
    bool lockstep = true;
    bool virtualTimers = true;
    uint32_t stuckMs = lockstep::kStuckMs;
    uint32_t traceFrom = 1, traceTo = 0; // trace=<from>-<to>: ticks whose clock reads and scheduler calls are traced
    bool traceLocks = false;             // tracelocks=1: EnterCriticalSection and AcquireSRWLockExclusive calls too
    int sound = -1;                      // sound=off|on; default: off with threads=lockstep, else on
    int heap = 0;                        // heap=private|process (default process: private loads the hood 2x slower)
    uint32_t heapMb = 512;               // heapmb=<n>: the private heap's fixed part (address space reserved at start)
};

// DSOUND.dll ordinal 1, DirectSoundCreate: its one caller is the sound library's device start 0xe9c68.
constexpr uint32_t kIatDirectSoundCreate = 0xd9a08c;
constexpr HRESULT kDsErrNoDriver = static_cast<HRESULT>(0x88780078); // DSERR_NODRIVER

// The image's thread start routines the thread namer looks through: the C runtime's thread_start (its parameter is
// {procedure, context}), cRZThread's entry 0x1b9d82 (its context is {runnable, thread, state}: the runnable's vtable
// names the work) and the plain entry 0x1b9d56 beside it (its context is {function, argument, ...}: it frees the context
// at once, so the function is read while the creator waits).
constexpr uint32_t kCrtThreadStart = 0x83a760, kRZThreadEntry = 0x1b9d82, kFunctionThreadEntry = 0x1b9d56;
constexpr uint32_t kTraceLinesPerThread = 200; // per tick: a busy-polling thread would write hundreds of thousands
// The image CRT's clock() start count (__acrt_initialize_clock 0x83a409 stores QueryPerformanceCounter's answer here,
// clock() 0x83a46e subtracts it; -1 when that read failed).
constexpr uint32_t kImageClockStart = 0x10dfcd0;

// Our C runtime (host/shared/ourcrt.h): the start routine its _beginthreadex gives CreateThread, and clock()'s start
// count and counter-read site.
const void *gCrtThreadStart = nullptr;
ourcrt::ClockStart gCrtClock;
decltype(&::CreateThread) gRealCreateThread = nullptr;

frameclock::Clock gClock;
lockstep::Scheduler *gSched = nullptr; // lives for the process
Options gOptions;
bool gOn = false;
uint64_t gStartMs = 0;
uint64_t gFrequency = 0;
uintptr_t gBase = 0;
uintptr_t gImageSize = 0;
DWORD gMainThread = 0;
thread_local frameclock::ThreadClock tClock; // this thread's reads
volatile LONG gStalled = 0;                   // some read in the current tick was decided by the stall rule
bool gAhead = false;                          // the main thread's clock ran past its tick's start (logged on change)
uint32_t gStallLines = 0;
volatile LONG gRdtscTraps = 0; // rdtsc sites answered (each costs an exception)

// Per tick, for the devlog's clk line: how many reads, and an order-free hash of what they returned.
std::atomic<uint32_t> gMainReads{0}, gOtherReads{0};
std::atomic<uint64_t> gMainSum{0}, gOtherSum{0};
uint32_t gTimersPosted = 0;
// Per tick, for the clk line's informational part: messages the game's threads took from their queues, and its reads
// of the real input state (cursor, keys, focus), each as a count and an order-dependent hash. Both come from outside the
// game: a difference here names input as the cause. The control's command messages are counted apart (ctl).
std::atomic<uint32_t> gMessages{0}, gMessageHash{0}, gInputs{0}, gInputHash{0}, gControlMessages{0};

void Fold(std::atomic<uint32_t> &count, std::atomic<uint32_t> &hash, uint32_t value)
{
    ++count;
    uint32_t h = hash.load();
    while (!hash.compare_exchange_weak(h, (h ^ value) * 16777619u)) {
    }
}
// Per tick, reads by thread (the scheduler's index; the last bucket: threads outside the lockstep), for the clk line's
// informational part.
constexpr uint32_t kReadBuckets = 17;
std::atomic<uint32_t> gReadsBy[kReadBuckets];
// Threads outside the lockstep (not the main thread) seen reading a clock: each is logged once.
std::atomic<DWORD> gOutsideReaders[32];

uint32_t RealMs()
{
    return static_cast<uint32_t>(GetTickCount64() - gStartMs);
}

// ---- names and traces (diagnostics) ----

bool InImage(const void *p)
{
    const auto address = reinterpret_cast<uintptr_t>(p);
    return gBase && address >= gBase && address < gBase + gImageSize;
}

// The game image's addresses by RVA ("image 0x1b9df1"); other modules: threadstack's module+offset.
std::string NameAddress(uintptr_t address)
{
    if (InImage(reinterpret_cast<const void *>(address))) {
        char text[32];
        std::snprintf(text, sizeof text, "image 0x%x", static_cast<unsigned>(address - gBase));
        return text;
    }
    return {};
}

bool ReadWord(uintptr_t address, uint32_t *out)
{
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), out, 4, &got) && got == 4;
}

// "start image 0x83a760 > image 0x1b9d82 runnable vtable image 0xe5c4f0": the CRT's thread_start and cRZThread's entry
// are looked through to the work they run. Our CRT's thread_start and our cRZThread's entries the same way ("start
// ts2host.exe+0x... (our CRT) > ts2host.exe+0x... (cRZThread::RunStartable) runnable vtable image 0xe61c1c").
std::string NameThread(const void *routine, const void *param)
{
    const auto r = reinterpret_cast<uintptr_t>(routine);
    std::string out = "start " + threadstack::Where(r, &NameAddress);
    uint32_t procedure = 0, context = 0, runnable = 0, vtable = 0;
    const auto p = reinterpret_cast<uintptr_t>(param);
    const bool imageCrt = gBase && r == gBase + kCrtThreadStart;
    const bool ourCrt = gCrtThreadStart && routine == gCrtThreadStart;
    if ((imageCrt || ourCrt) && ReadWord(p, &procedure) && ReadWord(p + 4, &context)) {
        if (ourCrt) out += " (our CRT)";
        out += " > " + threadstack::Where(procedure, &NameAddress);
        const auto ours = [](auto function) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(function)); };
        const bool startable = imageCrt ? procedure == gBase + kRZThreadEntry : procedure == ours(&cRZThread::RunStartable);
        const bool function = imageCrt ? procedure == gBase + kFunctionThreadEntry : procedure == ours(&cRZThread::RunRoutine);
        if (ourCrt && (startable || function))
            out += startable ? " (cRZThread::RunStartable)" : " (cRZThread::RunRoutine)";
        if (startable && ReadWord(context, &runnable) && ReadWord(runnable, &vtable))
            out += " runnable vtable " + threadstack::Where(vtable, &NameAddress);
        else if (function && ReadWord(context, &runnable))
            out += " function " + threadstack::Where(runnable, &NameAddress);
    }
    return out;
}

// The calling thread as the trace and the read counts name it: the scheduler's index, or ~0u outside the lockstep.
uint32_t ThreadIndex()
{
    if (gSched) return gSched->ThreadIndex();
    return GetCurrentThreadId() == gMainThread ? 0 : ~0u;
}

thread_local uint32_t tTraceTick = ~0u, tTraceReads = 0, tTraceCalls = 0;
thread_local bool tTracing = false; // writing a trace line: what that does (locks, the log file) isn't traced

bool Tracing()
{
    const uint32_t tick = gClock.Tick();
    return tick >= gOptions.traceFrom && tick <= gOptions.traceTo && !tTracing;
}

// A trace line for this thread in this tick, unless it has written kTraceLinesPerThread of that kind (reads, calls)
// already: a thread's many lock calls don't hide its clock reads.
bool TraceBudget(bool read)
{
    const uint32_t tick = gClock.Tick();
    if (tTraceTick != tick) tTraceTick = tick, tTraceReads = 0, tTraceCalls = 0;
    return (read ? tTraceReads++ : tTraceCalls++) < kTraceLinesPerThread;
}

// " < image 0x... < image 0x...": up to four of the image's return addresses above the reader's caller, found by
// scanning the stack (threadstack::Returns; the image's frames have no frame pointers, so a frame-chain walk stops at the
// first). The scan starts just above the hook's return-address slot (`callerSlot`), past the host's own frames, whose
// uninitialised buffers hold stale copies of earlier return addresses. Without the slot (rdtsc) it starts here and the
// image's addresses are taken from the caller on (from the first one when the caller isn't seen). A stale slot in the
// image's frames can still add an address now and then.
std::string TraceCallers(const void *caller, const void *callerSlot)
{
    const void *start = callerSlot ? static_cast<const uint8_t *>(callerSlot) + 4 : nullptr;
    const std::vector<uint32_t> returns = threadstack::Returns(48, 8 * 1024, start);
    auto from = returns.begin();
    if (!callerSlot) {
        from = std::find(returns.begin(), returns.end(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(caller)));
        from = from == returns.end() ? returns.begin() : from + 1;
    }
    std::string out;
    int found = 0;
    for (auto at = from; at != returns.end() && found < 4; ++at) {
        if (!InImage(reinterpret_cast<const void *>(static_cast<uintptr_t>(*at)))) continue;
        out += " < " + NameAddress(*at);
        ++found;
    }
    return out;
}

struct TraceScope {
    TraceScope() { tTracing = true; }
    ~TraceScope() { tTracing = false; }
};

std::string ThreadLabel()
{
    const uint32_t i = ThreadIndex();
    char text[24];
    if (i != ~0u) std::snprintf(text, sizeof text, "t%u", i);
    else std::snprintf(text, sizeof text, "x%lu", GetCurrentThreadId());
    return text;
}

// "ct <tick> <thread> <clock> <caller> <time in us since tick 0, 0.1 us precision>"
void TraceRead(char clock, const void *caller, const void *callerSlot, uint64_t value)
{
    const TraceScope scope;
    if (!TraceBudget(true)) return;
    const uint64_t t = value - frameclock::kBase;
    char text[256];
    std::snprintf(text, sizeof text, "ct %u %s %c %s %llu.%u%s", gClock.Tick(), ThreadLabel().c_str(), clock,
                  threadstack::Where(reinterpret_cast<uintptr_t>(caller), &NameAddress).c_str(),
                  static_cast<unsigned long long>(t / 10), static_cast<unsigned>(t % 10), TraceCallers(caller, callerSlot).c_str());
    DeviceLogNote(text);
}

// "cs <tick> <thread> <import>(<arguments>) <caller> -> <result>[ (from tick N)]"
void TraceCall(uint32_t entryTick, const char *import, const void *caller, const char *arguments, unsigned long result)
{
    const TraceScope scope;
    // Lock calls (most of them, and they rarely wait) only with tracelocks.
    if (!gOptions.traceLocks && (!std::strcmp(import, "EnterCriticalSection") || !std::strcmp(import, "AcquireSRWLockExclusive")))
        return;
    if (!TraceBudget(false)) return;
    char text[256];
    const uint32_t tick = gClock.Tick();
    char from[24] = "";
    if (entryTick != tick) std::snprintf(from, sizeof from, " (from tick %u)", entryTick);
    std::snprintf(text, sizeof text, "cs %u %s %s(%s) %s -> %#lx%s", tick, ThreadLabel().c_str(), import, arguments,
                  threadstack::Where(reinterpret_cast<uintptr_t>(caller), &NameAddress).c_str(), result, from);
    DeviceLogNote(text);
}

void NoteOutsideReader(char clock, const void *caller)
{
    if (tTracing) return;
    const TraceScope scope;
    const DWORD id = GetCurrentThreadId();
    for (std::atomic<DWORD> &slot : gOutsideReaders) {
        DWORD seen = slot.load();
        if (seen == id) return;
        if (seen) continue;
        if (!slot.compare_exchange_strong(seen, id)) {
            if (seen == id) return;
            continue;
        }
        Log("frameclock: thread id %lu (start %s), outside the lockstep, reads the clock: %c from %s at frame %u", id,
            threadstack::Where(threadstack::StartAddress(GetCurrentThread()), &NameAddress).c_str(), clock,
            threadstack::Where(reinterpret_cast<uintptr_t>(caller), &NameAddress).c_str(), gClock.Tick());
        return;
    }
}

uint64_t Scramble(uint64_t v)
{
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdull;
    v ^= v >> 33;
    v *= 0xc4ceb9fe1a85ec53ull;
    return v ^ (v >> 33);
}

// A read of the virtual clock: `clock` names the function (q QueryPerformanceCounter, t GetTickCount, m timeGetTime,
// f GetSystemTimeAsFileTime, l GetLocalTime, r rdtsc, s/S the stopwatch's counter/time-stamp overrides), `caller`
// where it was called from (the trace and the outside-reader log say).
uint64_t Now(char clock, const void *caller, const void *callerSlot = nullptr)
{
    bool stalled = false;
    const uint64_t v = gClock.Read(tClock, RealMs(), &stalled);
    if (stalled) gStalled = 1;
    if (GetCurrentThreadId() == gMainThread) {
        gMainReads.fetch_add(1, std::memory_order_relaxed);
        gMainSum.fetch_add(Scramble(v), std::memory_order_relaxed);
    } else {
        gOtherReads.fetch_add(1, std::memory_order_relaxed);
        gOtherSum.fetch_add(Scramble(v), std::memory_order_relaxed);
    }
    const uint32_t index = ThreadIndex();
    gReadsBy[index < kReadBuckets - 1 ? index : kReadBuckets - 1].fetch_add(1, std::memory_order_relaxed);
    if (index == ~0u) NoteOutsideReader(clock, caller);
    if (Tracing()) TraceRead(clock, caller, callerSlot, v);
    return v;
}

frameclock::ThreadClock &ThisClock()
{
    return tClock;
}

BOOL WINAPI FrameQpc(LARGE_INTEGER *out)
{
    out->QuadPart = static_cast<LONGLONG>(frameclock::ToCounter(Now('q', _ReturnAddress(), _AddressOfReturnAddress()), gFrequency));
    return TRUE;
}

DWORD WINAPI FrameTickCount()
{
    return frameclock::ToMilliseconds(Now('t', _ReturnAddress(), _AddressOfReturnAddress()));
}

DWORD WINAPI FrameTimeGetTime()
{
    return frameclock::ToMilliseconds(Now('m', _ReturnAddress(), _AddressOfReturnAddress()));
}

void FileTimeNow(FILETIME *out, char clock, const void *caller, const void *callerSlot)
{
    const uint64_t t = frameclock::ToFileTime(Now(clock, caller, callerSlot));
    out->dwLowDateTime = static_cast<DWORD>(t);
    out->dwHighDateTime = static_cast<DWORD>(t >> 32);
}

void WINAPI FrameSystemTimeAsFileTime(FILETIME *out)
{
    FileTimeNow(out, 'f', _ReturnAddress(), _AddressOfReturnAddress());
}

// Local time is the virtual UTC time (no time zone), so it is the same on every machine.
void WINAPI FrameLocalTime(SYSTEMTIME *out)
{
    FILETIME ft;
    FileTimeNow(&ft, 'l', _ReturnAddress(), _AddressOfReturnAddress());
    FileTimeToSystemTime(&ft, out);
}

// cRZStopwatch's overrides (src/rz/cRZStopwatch.h): our stopwatch reads the counters through our toolchain's imports.
void FrameCounter(uint64_t *out)
{
    *out = frameclock::ToCounter(Now('s', _ReturnAddress(), _AddressOfReturnAddress()), gFrequency);
}

void FrameTsc(uint64_t *out)
{
    *out = frameclock::ToTsc(Now('S', _ReturnAddress(), _AddressOfReturnAddress()));
}

// The host's own QueryPerformanceCounter import (our toolchain's reads; GetSystemTimeAsFileTime, our CRT's _time64,
// goes to FrameSystemTimeAsFileTime). A read from our CRT's clock() (`c`; by its call site, host/shared/ourcrt.h) answers
// what makes our clock() return what the image's clock() would at this virtual time: its own start count plus the
// virtual counter's distance from the image CRT's start count (both clock()s scale the same way, at the same frequency).
// So our clock() starts where the image's did, whatever the real counter was when the host started; and it is -1 when the
// image's is. Any other read (`q`) gets the virtual counter, as through the image's import.
BOOL WINAPI HostQpc(LARGE_INTEGER *out)
{
    const void *caller = _ReturnAddress();
    const bool crtClock = gCrtClock.found && caller == gCrtClock.site;
    const auto counter = static_cast<int64_t>(
        frameclock::ToCounter(Now(crtClock ? 'c' : 'q', caller, _AddressOfReturnAddress()), gFrequency));
    if (!crtClock) {
        out->QuadPart = counter;
        return TRUE;
    }
    out->QuadPart = ourcrt::ClockCounter(gCrtClock, counter, *GameAddress<volatile int64_t>(kImageClockStart));
    return TRUE;
}

bool IsRdtscSite(uint32_t rva)
{
    for (uint32_t site : kRdtscSites)
        if (site == rva) return true;
    return false;
}

// The ud2 written over each rdtsc: answer it as rdtsc would (edx:eax) and continue after it.
LONG CALLBACK RdtscHandler(EXCEPTION_POINTERS *info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ILLEGAL_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = info->ContextRecord;
    if (c->Eip < gBase || !IsRdtscSite(static_cast<uint32_t>(c->Eip - gBase))) return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&gRdtscTraps);
    const uint64_t tsc = frameclock::ToTsc(Now('r', reinterpret_cast<const void *>(static_cast<uintptr_t>(c->Eip))));
    c->Eax = static_cast<DWORD>(tsc);
    c->Edx = static_cast<DWORD>(tsc >> 32);
    c->Eip += 2;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// ---- lockstep: the image's imports ----

// Each records the call (the census and the stuck report name the last one and its caller) and, in traced ticks, writes
// a "cs" line when it returns. ExitThread and FreeLibraryAndExitThread don't return: their lines are written before.
#define LS_ENTER(name)                                                                                                     \
    const void *caller = _ReturnAddress();                                                                                 \
    const uint32_t entryTick = gClock.Tick();                                                                              \
    const char *const import = name;                                                                                       \
    gSched->Called(import, caller)
// Only calls from the image: the host's own EnterCriticalSection calls include the log file's and the allocator's.
#define LS_TRACE(result, ...)                                                                                              \
    if (InImage(caller) && Tracing()) {                                                                                    \
        char arguments[96];                                                                                                \
        std::snprintf(arguments, sizeof arguments, __VA_ARGS__);                                                           \
        TraceCall(entryTick, import, caller, arguments, static_cast<unsigned long>(result));                               \
    }

// A thread argument as a trace line names it: the scheduler's index ("thread 3"), the same on every run, or the
// Windows id of a thread outside the lockstep ("tid 1234").
std::string ThreadArg(HANDLE thread)
{
    const DWORD id = GetThreadId(thread);
    const uint32_t index = gSched->IndexOf(id);
    char text[32];
    if (index != ~0u) std::snprintf(text, sizeof text, "thread %u", index);
    else std::snprintf(text, sizeof text, "tid %lu", id);
    return text;
}

HANDLE WINAPI LsCreateThread(LPSECURITY_ATTRIBUTES a, SIZE_T b, LPTHREAD_START_ROUTINE c, LPVOID d, DWORD e, LPDWORD f)
{
    LS_ENTER("CreateThread");
    HANDLE h = gSched->CreateThread(a, b, c, d, e, f);
    // The new thread by index (its handle's value differs between runs); the result: whether there is one.
    LS_TRACE(h ? 1 : 0, "%s, %#lx: %s", threadstack::Where(reinterpret_cast<uintptr_t>(c), &NameAddress).c_str(), e,
             h ? ThreadArg(h).c_str() : "-");
    return h;
}
// The host's own CreateThread import. Our CRT's _beginthreadex (its thread_start: the threads our replacements start,
// cRZThread's) goes to the scheduler, as the image CRT's does through the image's import: a game thread that starts
// one gets a scheduled thread (index in creation order, parked until picked). Everything else (the host's own threads:
// the control's, coverage, quiet mode) gets the real function.
HANDLE WINAPI LsHostCreateThread(LPSECURITY_ATTRIBUTES a, SIZE_T b, LPTHREAD_START_ROUTINE c, LPVOID d, DWORD e, LPDWORD f)
{
    if (!gCrtThreadStart || reinterpret_cast<const void *>(c) != gCrtThreadStart) return gRealCreateThread(a, b, c, d, e, f);
    LS_ENTER("CreateThread");
    (void)entryTick; // not traced: the caller is our CRT, not the image
    return gSched->CreateThread(a, b, c, d, e, f);
}
DWORD WINAPI LsSuspendThread(HANDLE t)
{
    LS_ENTER("SuspendThread");
    const DWORD r = gSched->SuspendThread(t);
    LS_TRACE(r, "%s", ThreadArg(t).c_str());
    return r;
}
DWORD WINAPI LsResumeThread(HANDLE t)
{
    LS_ENTER("ResumeThread");
    const DWORD r = gSched->ResumeThread(t);
    LS_TRACE(r, "%s", ThreadArg(t).c_str());
    return r;
}
void WINAPI LsExitThread(DWORD code)
{
    LS_ENTER("ExitThread");
    LS_TRACE(0, "%lu", code);
    gSched->ExitThread(code);
}
void WINAPI LsFreeLibraryAndExitThread(HMODULE module, DWORD code)
{
    LS_ENTER("FreeLibraryAndExitThread");
    LS_TRACE(0, "%lu", code);
    gSched->FreeLibraryAndExitThread(module, code);
}
void WINAPI LsSleep(DWORD ms)
{
    LS_ENTER("Sleep");
    gSched->Sleep(ms);
    LS_TRACE(0, "%lu", ms);
}
DWORD WINAPI LsSleepEx(DWORD ms, BOOL alertable)
{
    LS_ENTER("SleepEx");
    const DWORD r = gSched->SleepEx(ms, alertable);
    LS_TRACE(r, "%lu, %d", ms, alertable);
    return r;
}
DWORD WINAPI LsWaitForSingleObject(HANDLE h, DWORD ms)
{
    LS_ENTER("WaitForSingleObject");
    const DWORD r = gSched->WaitForSingleObject(h, ms);
    LS_TRACE(r, "%p, %lu", h, ms);
    return r;
}
DWORD WINAPI LsWaitForMultipleObjects(DWORD n, const HANDLE *h, BOOL all, DWORD ms)
{
    LS_ENTER("WaitForMultipleObjects");
    const DWORD r = gSched->WaitForMultipleObjects(n, h, all, ms);
    LS_TRACE(r, "%lu: %p.., %d, %lu", n, n ? h[0] : nullptr, all, ms);
    return r;
}
void WINAPI LsEnterCriticalSection(CRITICAL_SECTION *s)
{
    LS_ENTER("EnterCriticalSection");
    gSched->EnterCriticalSection(s);
    LS_TRACE(0, "%p", static_cast<void *>(s));
}
void WINAPI LsAcquireSRWLockExclusive(SRWLOCK *l)
{
    LS_ENTER("AcquireSRWLockExclusive");
    gSched->AcquireSRWLockExclusive(l);
    LS_TRACE(0, "%p", static_cast<void *>(l));
}
BOOL WINAPI LsSleepConditionVariableSRW(CONDITION_VARIABLE *v, SRWLOCK *l, DWORD ms, ULONG flags)
{
    LS_ENTER("SleepConditionVariableSRW");
    const BOOL r = gSched->SleepConditionVariableSRW(v, l, ms, flags);
    LS_TRACE(r, "%p, %p, %lu, %lu", static_cast<void *>(v), static_cast<void *>(l), ms, flags);
    return r;
}
void WINAPI LsWakeAllConditionVariable(CONDITION_VARIABLE *v)
{
    LS_ENTER("WakeAllConditionVariable");
    gSched->WakeAllConditionVariable(v);
    LS_TRACE(0, "%p", static_cast<void *>(v));
}
DWORD WINAPI LsQueueUserAPC(PAPCFUNC f, HANDLE t, ULONG_PTR d)
{
    LS_ENTER("QueueUserAPC");
    const DWORD r = gSched->QueueUserAPC(f, t, d);
    LS_TRACE(r, "%s, %s", threadstack::Where(reinterpret_cast<uintptr_t>(f), &NameAddress).c_str(), ThreadArg(t).c_str());
    return r;
}
LRESULT WINAPI LsSendMessageA(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    LS_ENTER("SendMessageA");
    const LRESULT r = gSched->SendMessageA(w, m, wp, lp);
    LS_TRACE(r, "%p, %#x", static_cast<void *>(w), m);
    return r;
}
BOOL WINAPI LsPeekMessageA(MSG *msg, HWND w, UINT a, UINT b, UINT remove)
{
    LS_ENTER("PeekMessageA");
    const BOOL r = gSched->PeekMessageA(msg, w, a, b, remove);
    if (r && (remove & PM_REMOVE) && msg->message == kControlMessage) ++gControlMessages;
    else if (r && (remove & PM_REMOVE)) {
        // The message and, for mouse messages, where (window handles in other parameters differ between runs). The
        // control's own command messages arrive when the scenario writes its lines (real time): counted apart.
        const bool mouse = msg->message >= WM_MOUSEFIRST && msg->message <= WM_MOUSELAST;
        Fold(gMessages, gMessageHash, msg->message * 31u + (mouse ? static_cast<uint32_t>(msg->lParam) : 0u));
    }
    LS_TRACE(r, "%#x %#lx", r ? msg->message : 0u, r ? static_cast<unsigned long>(msg->lParam) : 0ul);
    return r;
}

// The real input state, read through (recorded for the clk line and the trace).
BOOL WINAPI LsGetCursorPos(POINT *p)
{
    LS_ENTER("GetCursorPos");
    const BOOL r = GetCursorPos(p);
    Fold(gInputs, gInputHash, r ? static_cast<uint32_t>(p->x) * 65599u + static_cast<uint32_t>(p->y) : 0u);
    LS_TRACE(r, "%ld,%ld", r ? p->x : 0l, r ? p->y : 0l);
    return r;
}
SHORT WINAPI LsGetAsyncKeyState(int key)
{
    LS_ENTER("GetAsyncKeyState");
    const SHORT r = GetAsyncKeyState(key);
    Fold(gInputs, gInputHash, static_cast<uint32_t>(key) << 16 | static_cast<uint16_t>(r));
    LS_TRACE(static_cast<uint16_t>(r), "%d", key);
    return r;
}
SHORT WINAPI LsGetKeyState(int key)
{
    LS_ENTER("GetKeyState");
    const SHORT r = GetKeyState(key);
    Fold(gInputs, gInputHash, static_cast<uint32_t>(key) << 16 | static_cast<uint16_t>(r));
    LS_TRACE(static_cast<uint16_t>(r), "%d", key);
    return r;
}
HWND WINAPI LsGetForegroundWindow()
{
    LS_ENTER("GetForegroundWindow");
    const HWND r = GetForegroundWindow();
    DWORD pid = 0;
    if (r) GetWindowThreadProcessId(r, &pid);
    const uint32_t ours = r ? (pid == GetCurrentProcessId() ? 2u : 1u) : 0u; // none, another process's, ours
    Fold(gInputs, gInputHash, 0x100000u | ours);
    LS_TRACE(ours, "%s", "");
    return r;
}
HWND WINAPI LsGetActiveWindow()
{
    LS_ENTER("GetActiveWindow");
    const HWND r = GetActiveWindow();
    Fold(gInputs, gInputHash, 0x200000u | (r ? 1u : 0u));
    LS_TRACE(r ? 1u : 0u, "%s", "");
    return r;
}
BOOL WINAPI LsPulseEvent(HANDLE h)
{
    LS_ENTER("PulseEvent");
    const BOOL r = gSched->PulseEvent(h);
    LS_TRACE(r, "%p", h);
    return r;
}

// Window functions that send to the window's thread and wait: on another game thread's window they run on that thread
// (lockstep.h, OnWindowThread). The real functions are the host's own imports, which nothing patches.
BOOL WINAPI LsSetWindowTextA(HWND w, LPCSTR text)
{
    LS_ENTER("SetWindowTextA");
    struct A {
        HWND w;
        LPCSTR text;
    } a{w, text};
    const auto r = static_cast<BOOL>(gSched->OnWindowThread(
        w, [](void *c) -> uintptr_t { const auto *x = static_cast<A *>(c); return ::SetWindowTextA(x->w, x->text); }, &a));
    LS_TRACE(r, "%p", static_cast<void *>(w));
    return r;
}
BOOL WINAPI LsSetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    LS_ENTER("SetWindowPos");
    struct A {
        HWND w, after;
        int x, y, cx, cy;
        UINT flags;
    } a{w, after, x, y, cx, cy, flags};
    const auto r = static_cast<BOOL>(gSched->OnWindowThread(
        w,
        [](void *c) -> uintptr_t {
            const auto *v = static_cast<A *>(c);
            return ::SetWindowPos(v->w, v->after, v->x, v->y, v->cx, v->cy, v->flags);
        },
        &a));
    LS_TRACE(r, "%p, %#x", static_cast<void *>(w), flags);
    return r;
}
BOOL WINAPI LsShowWindow(HWND w, int show)
{
    LS_ENTER("ShowWindow");
    struct A {
        HWND w;
        int show;
    } a{w, show};
    const auto r = static_cast<BOOL>(gSched->OnWindowThread(
        w, [](void *c) -> uintptr_t { const auto *v = static_cast<A *>(c); return ::ShowWindow(v->w, v->show); }, &a));
    LS_TRACE(r, "%p, %d", static_cast<void *>(w), show);
    return r;
}
BOOL WINAPI LsMoveWindow(HWND w, int x, int y, int cx, int cy, BOOL repaint)
{
    LS_ENTER("MoveWindow");
    struct A {
        HWND w;
        int x, y, cx, cy;
        BOOL repaint;
    } a{w, x, y, cx, cy, repaint};
    const auto r = static_cast<BOOL>(gSched->OnWindowThread(
        w,
        [](void *c) -> uintptr_t {
            const auto *v = static_cast<A *>(c);
            return ::MoveWindow(v->w, v->x, v->y, v->cx, v->cy, v->repaint);
        },
        &a));
    LS_TRACE(r, "%p", static_cast<void *>(w));
    return r;
}
LONG WINAPI LsSetWindowLongA(HWND w, int index, LONG value)
{
    LS_ENTER("SetWindowLongA");
    struct A {
        HWND w;
        int index;
        LONG value;
    } a{w, index, value};
    const auto r = static_cast<LONG>(gSched->OnWindowThread(
        w,
        [](void *c) -> uintptr_t {
            const auto *v = static_cast<A *>(c);
            return static_cast<uintptr_t>(::SetWindowLongA(v->w, v->index, v->value));
        },
        &a));
    LS_TRACE(r, "%p, %d", static_cast<void *>(w), index);
    return r;
}
BOOL WINAPI LsUpdateWindow(HWND w)
{
    LS_ENTER("UpdateWindow");
    const auto r = static_cast<BOOL>(gSched->OnWindowThread(
        w, [](void *c) -> uintptr_t { return ::UpdateWindow(static_cast<HWND>(c)); }, w));
    LS_TRACE(r, "%p", static_cast<void *>(w));
    return r;
}

struct SlotPatch {
    uint32_t rva;
    void *function;
};
const SlotPatch kLockstepSlots[] = {
    {0xd9a2e8, reinterpret_cast<void *>(&LsCreateThread)},
    {0xd9a1a4, reinterpret_cast<void *>(&LsSuspendThread)},
    {0xd9a1a8, reinterpret_cast<void *>(&LsResumeThread)},
    {0xd9a2ec, reinterpret_cast<void *>(&LsExitThread)},
    {0xd9a2f0, reinterpret_cast<void *>(&LsFreeLibraryAndExitThread)},
    {0xd9a144, reinterpret_cast<void *>(&LsSleep)},
    {0xd9a188, reinterpret_cast<void *>(&LsSleepEx)},
    {0xd9a184, reinterpret_cast<void *>(&LsWaitForSingleObject)},
    {0xd9a3d4, reinterpret_cast<void *>(&LsWaitForMultipleObjects)},
    {0xd9a158, reinterpret_cast<void *>(&LsEnterCriticalSection)},
    {0xd9a294, reinterpret_cast<void *>(&LsAcquireSRWLockExclusive)},
    {0xd9a29c, reinterpret_cast<void *>(&LsSleepConditionVariableSRW)},
    {0xd9a298, reinterpret_cast<void *>(&LsWakeAllConditionVariable)},
    {0xd9a18c, reinterpret_cast<void *>(&LsQueueUserAPC)},
    {0xd9a498, reinterpret_cast<void *>(&LsSendMessageA)},
    {0xd9a578, reinterpret_cast<void *>(&LsPeekMessageA)},
    {0xd9a3dc, reinterpret_cast<void *>(&LsPulseEvent)},
    {0xd9a598, reinterpret_cast<void *>(&LsSetWindowTextA)},
    {0xd9a510, reinterpret_cast<void *>(&LsSetWindowPos)},
    {0xd9a538, reinterpret_cast<void *>(&LsShowWindow)},
    {0xd9a53c, reinterpret_cast<void *>(&LsMoveWindow)},
    {0xd9a5d4, reinterpret_cast<void *>(&LsSetWindowLongA)},
    {0xd9a55c, reinterpret_cast<void *>(&LsUpdateWindow)},
    // Not scheduling: input state, recorded (in=, ci trace lines).
    {0xd9a4d0, reinterpret_cast<void *>(&LsGetCursorPos)},
    {0xd9a548, reinterpret_cast<void *>(&LsGetAsyncKeyState)},
    {0xd9a5a4, reinterpret_cast<void *>(&LsGetKeyState)},
    {0xd9a4ac, reinterpret_cast<void *>(&LsGetForegroundWindow)},
    {0xd9a544, reinterpret_cast<void *>(&LsGetActiveWindow)},
};

// ---- virtual window timers ----

struct Timer {
    HWND window;
    UINT_PTR id;
    uint64_t period; // units
    uint64_t next;
};
std::vector<Timer> gTimers; // main thread only

UINT_PTR WINAPI FrameSetTimer(HWND window, UINT_PTR id, UINT elapse, TIMERPROC proc)
{
    if (!window || proc || GetCurrentThreadId() != gMainThread || GetWindowThreadProcessId(window, nullptr) != gMainThread)
        return SetTimer(window, id, elapse, proc);
    if (elapse < USER_TIMER_MINIMUM) elapse = USER_TIMER_MINIMUM;
    if (elapse > USER_TIMER_MAXIMUM) elapse = USER_TIMER_MAXIMUM;
    const uint64_t period = frameclock::FromMilliseconds(elapse);
    const uint64_t next = gClock.ThreadNow(tClock) + period;
    for (Timer &t : gTimers)
        if (t.window == window && t.id == id) {
            t.period = period;
            t.next = next;
            return id ? id : 1;
        }
    gTimers.push_back({window, id, period, next});
    return id ? id : 1;
}

BOOL WINAPI FrameKillTimer(HWND window, UINT_PTR id)
{
    for (auto it = gTimers.begin(); it != gTimers.end(); ++it)
        if (it->window == window && it->id == id) {
            gTimers.erase(it);
            return TRUE;
        }
    return KillTimer(window, id);
}

// At the tick: each due timer posts one WM_TIMER (Windows never queues more than one per timer either).
void FireTimers()
{
    const uint64_t now = gClock.Now();
    for (auto it = gTimers.begin(); it != gTimers.end();) {
        if (!IsWindow(it->window)) { // a destroyed window's timers end with it
            it = gTimers.erase(it);
            continue;
        }
        if (it->next <= now) {
            PostMessageA(it->window, WM_TIMER, it->id, 0);
            ++gTimersPosted;
            it->next += it->period;
            if (it->next <= now) it->next = now + it->period;
        }
        ++it;
    }
}

// ---- patching ----

// sound=off: no sound device. The sound library's mixer (0xe9e0d, on its timer thread 0xea0dd) mixes as far as the
// device's play cursor has moved, a real-time input the frame clock can't fix; without a device the library runs no
// timer thread and mixes nothing (0xe9c68 takes the failure; the game plays silently).
HRESULT WINAPI NoDirectSound(const GUID *, void **sound, void *)
{
    if (sound) *sound = nullptr;
    return kDsErrNoDriver;
}

// heap=private: the image's process-heap calls go to a private heap whose addresses repeat from run to run
// (host/shared/privateheap.h: the process heap's low-fragmentation heap places blocks at random, and the game orders some
// containers by address). The image's heap handle is GetProcessHeap's, so these four slots are all it takes.
constexpr uint32_t kIatHeapAlloc = 0xd9a1d8, kIatHeapFree = 0xd9a1dc, kIatHeapReAlloc = 0xd9a380, kIatHeapSize = 0xd9a3a8;
privateheap::Heap gHeap;

void *WINAPI PrivateHeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes)
{
    return gHeap.Alloc(heap, flags, bytes);
}
BOOL WINAPI PrivateHeapFree(HANDLE heap, DWORD flags, void *block)
{
    return gHeap.Free(heap, flags, block);
}
void *WINAPI PrivateHeapReAlloc(HANDLE heap, DWORD flags, void *block, SIZE_T bytes)
{
    return gHeap.ReAlloc(heap, flags, block, bytes);
}
SIZE_T WINAPI PrivateHeapSize(HANDLE heap, DWORD flags, const void *block)
{
    return gHeap.Size(heap, flags, block);
}

void PatchSlot(uint32_t rva, void *function)
{
    auto *slot = GameAddress<void *>(rva);
    DWORD old;
    VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old);
    InterlockedExchangePointer(slot, function);
    VirtualProtect(slot, sizeof(void *), old, &old);
}

// The host's own import of `name` from `dll` (our toolchain's calls) now goes to `function`. False: not imported.
bool PatchHostImport(const char *dll, const char *name, void *function)
{
    return ourcrt::PatchImport(GetModuleHandleW(nullptr), dll, name, function);
}

// The host's own imports the lockstep takes (our toolchain's calls; host/shared/ourcrt.h): CreateThread only for our
// CRT's _beginthreadex, the rest for the scheduled threads (the scheduler gives every other thread the real function).
struct HostPatch {
    const char *name;
    void *function;
};
const HostPatch kLockstepHostImports[] = {
    {"EnterCriticalSection", reinterpret_cast<void *>(&LsEnterCriticalSection)}, // cRZCriticalSection, GeneralAllocator
    {"WaitForSingleObject", reinterpret_cast<void *>(&LsWaitForSingleObject)},   // cRZMutex
    {"CreateThread", reinterpret_cast<void *>(&LsHostCreateThread)},             // our CRT's _beginthreadex: cRZThread
    {"ExitThread", reinterpret_cast<void *>(&LsExitThread)},                     // our CRT's _endthreadex
    {"FreeLibraryAndExitThread", reinterpret_cast<void *>(&LsFreeLibraryAndExitThread)}, // the same
};

int PatchRdtscSites()
{
    int patched = 0;
    for (uint32_t rva : kRdtscSites) {
        auto *p = GameAddress<uint8_t>(rva);
        if (p[0] != 0x0f || p[1] != 0x31) {
            Log("frameclock: no rdtsc at 0x%x (%02x %02x); left alone", rva, p[0], p[1]);
            continue;
        }
        DWORD old;
        VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &old);
        p[1] = 0x0b; // 0f 0b: ud2
        VirtualProtect(p, 2, old, &old);
        ++patched;
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    return patched;
}

Options ReadOptions()
{
    Options o;
    char value[256];
    const DWORD n = GetEnvironmentVariableA("TS2HOST_FRAMECLOCK_OPTS", value, sizeof(value));
    if (!n || n >= sizeof(value)) return o;
    char *context = nullptr;
    for (char *token = strtok_s(value, ", ", &context); token; token = strtok_s(nullptr, ", ", &context)) {
        char *eq = std::strchr(token, '=');
        if (!eq) {
            Log("frameclock: option \"%s\" ignored (expected name=value)", token);
            continue;
        }
        *eq = 0;
        const char *v = eq + 1;
        if (!std::strcmp(token, "still")) o.still = std::strtoul(v, nullptr, 10);
        else if (!std::strcmp(token, "stall")) o.stallMs = std::strtoul(v, nullptr, 10);
        else if (!std::strcmp(token, "stuck")) o.stuckMs = std::strtoul(v, nullptr, 10);
        else if (!std::strcmp(token, "trace")) {
            char *end = nullptr;
            o.traceFrom = std::strtoul(v, &end, 10);
            o.traceTo = *end == '-' ? std::strtoul(end + 1, nullptr, 10) : o.traceFrom;
        }
        else if (!std::strcmp(token, "tracelocks")) o.traceLocks = std::strtoul(v, nullptr, 10) != 0;
        else if (!std::strcmp(token, "sound") && (!std::strcmp(v, "off") || !std::strcmp(v, "on")))
            o.sound = !std::strcmp(v, "on");
        else if (!std::strcmp(token, "heap") && (!std::strcmp(v, "private") || !std::strcmp(v, "process")))
            o.heap = !std::strcmp(v, "private");
        else if (!std::strcmp(token, "heapmb") && std::strtoul(v, nullptr, 10) >= 16)
            o.heapMb = std::strtoul(v, nullptr, 10);
        else if (!std::strcmp(token, "threads") && (!std::strcmp(v, "lockstep") || !std::strcmp(v, "free")))
            o.lockstep = !std::strcmp(v, "lockstep");
        else if (!std::strcmp(token, "timers") && (!std::strcmp(v, "virtual") || !std::strcmp(v, "real")))
            o.virtualTimers = !std::strcmp(v, "virtual");
        else
            Log("frameclock: option %s=%s ignored", token, v);
    }
    return o;
}

void SchedulerLog(const char *text)
{
    Log("frameclock: %s", text);
}

// The clk line of the tick that just ended (devlog.py: the part before " |" must match between runs).
void WriteClockLine(uint32_t frame)
{
    char line[512];
    const lockstep::Stats s = gSched ? gSched->TakeStats() : lockstep::Stats{};
    const uint64_t start = gClock.TickStart() - frameclock::kBase;
    int n = std::snprintf(line, sizeof line,
                          "clk %u t=%llu main=%u:%016llx other=%u:%016llx seq=%016llx hand=%u adv=%u threads=%u apcs=%u "
                          "calls=%u timers=%u | polls=%u idle=%u outside=%u pulses=%u unseen=%u msgs=%u:%08x in=%u:%08x ctl=%u by=",
                          frame, static_cast<unsigned long long>(start / 10), gMainReads.exchange(0),
                          static_cast<unsigned long long>(gMainSum.exchange(0)), gOtherReads.exchange(0),
                          static_cast<unsigned long long>(gOtherSum.exchange(0)), static_cast<unsigned long long>(s.sequence),
                          s.handoffs, s.advances, s.threads, s.apcs, s.calls, gTimersPosted, s.polls, s.idle, s.outside,
                          s.pulses, s.unseenExits, gMessages.exchange(0), gMessageHash.exchange(0), gInputs.exchange(0),
                          gInputHash.exchange(0), gControlMessages.exchange(0));
    // Reads by thread: "0:3,1:2,x:4" (x: threads outside the lockstep, or past the 16th).
    bool first = true;
    for (uint32_t i = 0; i < kReadBuckets && n > 0 && n < static_cast<int>(sizeof line) - 24; ++i) {
        const uint32_t count = gReadsBy[i].exchange(0);
        if (!count) continue;
        if (i == kReadBuckets - 1) n += std::snprintf(line + n, sizeof line - n, "%sx:%u", first ? "" : ",", count);
        else n += std::snprintf(line + n, sizeof line - n, "%s%u:%u", first ? "" : ",", i, count);
        first = false;
    }
    gTimersPosted = 0;
    DeviceLogNote(line);
}

} // namespace

void StartFrameClock(uintptr_t imageBase)
{
    wchar_t value[32];
    if (!GetEnvironmentVariableW(L"TS2HOST_FRAMECLOCK", value, 32)) return;
    const unsigned long n = std::wcstoul(value, nullptr, 10);
    const uint64_t frameUs = n >= 1000 ? n : frameclock::kDefaultFrameUs;
    gOptions = ReadOptions();
    gBase = imageBase;
    gImageSize = 18'935'808; // 1.18.0.187's size of image, unless the mapped headers say otherwise
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(imageBase);
    if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(imageBase + dos->e_lfanew);
        if (nt->Signature == IMAGE_NT_SIGNATURE) gImageSize = nt->OptionalHeader.SizeOfImage;
    }
    gMainThread = GetCurrentThreadId(); // the host's thread runs the image's entry point: the game's main thread
    gStartMs = GetTickCount64();
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    gFrequency = static_cast<uint64_t>(f.QuadPart);
    gClock.Configure(frameUs, gOptions.still, gOptions.stallMs);
    gClock.OnTick(ControlFrame(), RealMs());
    // Ahead of the fault log (host/main.cpp starts it first), which would log every rdtsc as an exception.
    AddVectoredExceptionHandler(1, &RdtscHandler);
    const int sites = PatchRdtscSites();
    PatchSlot(kIatQpc, reinterpret_cast<void *>(&FrameQpc));
    PatchSlot(kIatTickCount, reinterpret_cast<void *>(&FrameTickCount));
    PatchSlot(kIatTimeGetTime, reinterpret_cast<void *>(&FrameTimeGetTime));
    PatchSlot(kIatSystemTimeAsFileTime, reinterpret_cast<void *>(&FrameSystemTimeAsFileTime));
    PatchSlot(kIatLocalTime, reinterpret_cast<void *>(&FrameLocalTime));
    cRZStopwatch::sPerformanceCounterOverride = &FrameCounter;
    cRZStopwatch::sTimeStampCounterOverride = &FrameTsc;
    // Our CRT's clock() and _time64 (host/shared/ourcrt.h): clock()'s start count first, with the real counter.
    gCrtClock = ourcrt::FindClockStart(GetModuleHandleW(nullptr));
    const bool hostQpc = PatchHostImport("KERNEL32.dll", "QueryPerformanceCounter", reinterpret_cast<void *>(&HostQpc));
    const bool hostFileTime =
        PatchHostImport("KERNEL32.dll", "GetSystemTimeAsFileTime", reinterpret_cast<void *>(&FrameSystemTimeAsFileTime));
    if (!gCrtClock.found || !hostQpc || !hostFileTime)
        Log("frameclock: our CRT's clock: start count %s, QueryPerformanceCounter import %s, GetSystemTimeAsFileTime "
            "import %s: our clock() or _time64 read real time",
            gCrtClock.found ? "found" : "NOT FOUND", hostQpc ? "patched" : "NOT PATCHED", hostFileTime ? "patched" : "NOT PATCHED");
    unsigned hostImports = 0;
    if (gOptions.lockstep) {
        gSched = new lockstep::Scheduler(gClock, &ThisClock, &SchedulerLog);
        gSched->SetNamers(&NameThread, &NameAddress);
        gSched->Start(gOptions.stuckMs);
        for (const SlotPatch &p : kLockstepSlots) PatchSlot(p.rva, p.function);
        // Our CRT's thread start routine first (the probe uses the CreateThread import itself), then the host's imports.
        gRealCreateThread = reinterpret_cast<decltype(&::CreateThread)>(
            reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateThread")));
        gCrtThreadStart = ourcrt::FindThreadStart(GetModuleHandleW(nullptr));
        for (const HostPatch &p : kLockstepHostImports)
            if (PatchHostImport("KERNEL32.dll", p.name, p.function)) ++hostImports;
            else Log("frameclock: the host doesn't import %s: our code's calls of it are not scheduled", p.name);
        if (!gCrtThreadStart)
            Log("frameclock: our CRT's thread start routine NOT FOUND: the threads our code starts run outside the lockstep");
    }
    if (gOptions.virtualTimers) {
        PatchSlot(kIatSetTimer, reinterpret_cast<void *>(&FrameSetTimer));
        PatchSlot(kIatKillTimer, reinterpret_cast<void *>(&FrameKillTimer));
    }
    if (gOptions.sound < 0) gOptions.sound = gOptions.lockstep ? 0 : 1;
    if (!gOptions.sound) {
        PatchSlot(kIatDirectSoundCreate, reinterpret_cast<void *>(&NoDirectSound));
        Log("frameclock: sound off (DirectSoundCreate answers DSERR_NODRIVER; sound=on keeps it)");
    }

    if (gOptions.heap == 1) {
        if (gHeap.Create(GetProcessHeap(), static_cast<SIZE_T>(gOptions.heapMb) << 20)) {
            PatchSlot(kIatHeapAlloc, reinterpret_cast<void *>(&PrivateHeapAlloc));
            PatchSlot(kIatHeapFree, reinterpret_cast<void *>(&PrivateHeapFree));
            PatchSlot(kIatHeapReAlloc, reinterpret_cast<void *>(&PrivateHeapReAlloc));
            PatchSlot(kIatHeapSize, reinterpret_cast<void *>(&PrivateHeapSize));
            Log("frameclock: private heap (%u MB fixed at %p, then overflow; an experiment, the default is heap=process)",
                gOptions.heapMb, gHeap.Handle());
        } else {
            Log("frameclock: private heap not created (%lu): the process heap stays", GetLastError());
        }
    }
    gOn = true;
    Log("frameclock: on, %llu us per frame, still %u, stall %u ms, threads %s (%u import slots and %u of the host's "
        "imports), timers %s (5 clock imports, %d of %u rdtsc sites, the stopwatch overrides, our CRT's clock() and "
        "_time64: %u of 2 host imports)",
        static_cast<unsigned long long>(frameUs), gOptions.still, gOptions.stallMs, gOptions.lockstep ? "lockstep" : "free",
        gOptions.lockstep ? static_cast<unsigned>(sizeof(kLockstepSlots) / sizeof(kLockstepSlots[0])) : 0u, hostImports,
        gOptions.virtualTimers ? "virtual" : "real", sites, static_cast<unsigned>(sizeof(kRdtscSites) / sizeof(kRdtscSites[0])),
        (hostQpc ? 1u : 0u) + (hostFileTime ? 1u : 0u));
    if (gOptions.lockstep)
        Log("frameclock: our CRT: thread start %s (its _beginthreadex threads are scheduled), clock() start count %s",
            gCrtThreadStart ? threadstack::Where(reinterpret_cast<uintptr_t>(gCrtThreadStart), &NameAddress).c_str() : "-",
            gCrtClock.found ? "found" : "-");
    if (gOptions.traceTo >= gOptions.traceFrom)
        Log("frameclock: tracing clock reads and scheduler calls in ticks %u-%u (ct and cs lines in ts2devlog.txt; at most "
            "%u per thread and tick)", gOptions.traceFrom, gOptions.traceTo, kTraceLinesPerThread);
}

void FrameClockOnFrame(uint32_t frame)
{
    if (!gOn) return;
    WriteClockLine(frame - 1);
    gClock.OnTick(frame, RealMs());
    if (gOptions.virtualTimers) FireTimers();
    // The workers run what this tick's time lets them, before the main thread's frame (host/shared/lockstep.h).
    if (gSched) gSched->TickYield();
    if (InterlockedExchange(&gStalled, 0)) {
        if (gStallLines < 20)
            Log("frameclock: frame %u: the previous tick lasted over %u ms real, so time followed real time at 1/%u speed "
                "(not deterministic)", frame, gOptions.stallMs, frameclock::kStallDivisor);
        else if (gStallLines == 20)
            Log("frameclock: further stalled ticks not logged");
        ++gStallLines;
    }
    // The main thread (this one) ran past the new tick's start: its reads in this tick continue from there.
    const bool ahead = tClock.last > gClock.TickStart();
    if (ahead != gAhead) {
        if (ahead)
            Log("frameclock: frame %u: the main thread's clock is %llu us ahead of the frame time; frames are "
                "deterministic again once it is caught up", frame,
                static_cast<unsigned long long>((tClock.last - gClock.TickStart()) / 10));
        else
            Log("frameclock: frame %u: caught up (the main thread's clock is the frame time again)", frame);
        gAhead = ahead;
    }
}

void LogFrameClock()
{
    if (!gOn) {
        Log("control: frameclock is off (TS2HOST_FRAMECLOCK not set)");
        return;
    }
    Log("control: frameclock: frame %u, %llu us per frame, tick start +%llu us, main thread %s, %u stalled ticks, %ld "
        "rdtsc reads answered, %u virtual timers, lockstep %s",
        gClock.Tick(), static_cast<unsigned long long>(gClock.FrameUnits() / 10),
        static_cast<unsigned long long>((gClock.TickStart() - frameclock::kBase) / 10), gAhead ? "ahead" : "on time",
        gStallLines, gRdtscTraps, static_cast<unsigned>(gTimers.size()),
        !gSched ? "off" : gSched->Free() ? "ended (free)" : "on");
    if (gHeap.Handle()) {
        const privateheap::Stats h = gHeap.GetStats();
        Log("control: frameclock: private heap: %llu allocations, %llu frees, %llu resizes, %llu from the overflow heap, "
            "%llu calls for process-heap blocks",
            h.allocs, h.frees, h.reallocs, h.overflow, h.foreign);
    }
    if (!gSched) return;
    const std::string census = gSched->Census();
    size_t at = 0;
    while (at < census.size()) {
        const size_t end = census.find('\n', at);
        Log("control: frameclock: %s", census.substr(at, end - at).c_str());
        if (end == std::string::npos) break;
        at = end + 1;
    }
}
