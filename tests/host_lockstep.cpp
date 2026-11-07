// The lockstep scheduler (host/shared/lockstep.h; TS2HOST_FRAMECLOCK, host/control_frameclock.cpp) with real threads.
// Not a differential test (no original function): rva 0. The test threads call the scheduler's replacements where the
// game calls its imports. What the host relies on: the same program gives the same interleaving and the same virtual
// times whatever real time does (real sleeps injected between steps), waits end by virtual time without waiting in
// real time, held APCs and sent messages arrive where Windows would deliver them, suspended threads stay parked, a
// thread's end lets its joiner go, Sleep(0) spinners get their budget, and a stuck token holder ends lockstep.
#include "difftest.h"

#include "../host/shared/lockstep.h"
#include "../host/shared/ourcrt.h"

#include <process.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

using namespace lockstep;

frameclock::Clock *gClock = nullptr;
Scheduler *gS = nullptr;
thread_local frameclock::ThreadClock tClock;
std::vector<std::string> gLog; // written only by the thread holding the token
std::mutex gSchedLogLock;
std::vector<std::string> gSchedLog; // the scheduler's own log lines (any thread)
uint32_t gJitterSeed = 0;           // 0: no real-time jitter

frameclock::ThreadClock &ClockOf()
{
    return tClock;
}

void SchedLog(const char *text)
{
    std::lock_guard<std::mutex> guard(gSchedLogLock);
    gSchedLog.emplace_back(text);
}

uint64_t Read()
{
    return gClock->Read(tClock, 0) - frameclock::kBase;
}

void Note(const char *format, ...)
{
    char text[200];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    gLog.emplace_back(text);
}

// A real-time delay of 0-2 ms (or a short spin) between steps, different on every run: lockstep must not care.
void Jitter()
{
    if (!gJitterSeed) return;
    gJitterSeed = gJitterSeed * 1103515245u + 12345u;
    const uint32_t r = (gJitterSeed >> 16) % 7;
    if (r < 3) {
        ::Sleep(r);
    } else {
        volatile uint32_t spin = 0;
        for (uint32_t i = 0; i < r * 20000; ++i) spin = spin + i;
    }
}

struct Run {
    frameclock::Clock clock;
    Scheduler s;
    explicit Run(uint32_t stuckMs = 0) : s(clock, &ClockOf, &SchedLog)
    {
        clock.Configure(33'333, 64, 0);
        clock.OnTick(0, 0);
        tClock = {};
        gClock = &clock;
        gS = &s;
        gLog.clear();
        gSchedLog.clear();
        s.Start(stuckMs);
    }
    ~Run() { gS = nullptr; }
    void Tick(uint32_t tick)
    {
        clock.OnTick(tick, 0);
        s.TickYield();
    }
};

void Join(HANDLE *threads, DWORD count)
{
    gS->WaitForMultipleObjects(count, threads, TRUE, INFINITE);
    for (DWORD i = 0; i < count; ++i) CloseHandle(threads[i]);
}

// ---- the mixed program: sleeps, an auto-reset event with a time-out, a critical section, a mutex ----

HANDLE gEvent, gMutex;
CRITICAL_SECTION gSection;
int gShared;

DWORD WINAPI Sleeper(void *)
{
    for (int i = 0; i < 10; ++i) {
        Note("sleeper %d t=%llu", i, Read());
        Jitter();
        gS->Sleep(7);
    }
    return 1;
}

DWORD WINAPI EventWaiter(void *)
{
    for (int i = 0; i < 10; ++i) {
        const DWORD r = gS->WaitForSingleObject(gEvent, 20);
        Note("waiter %d r=%lu t=%llu", i, r, Read());
        Jitter();
    }
    return 2;
}

DWORD WINAPI SectionUser(void *)
{
    for (int i = 0; i < 8; ++i) {
        gS->EnterCriticalSection(&gSection);
        const int before = gShared;
        Note("section %d in t=%llu", i, Read());
        Jitter();
        gS->Sleep(40); // holding the section across a tick: the main thread blocks on it meanwhile
        gShared = before + 1;
        LeaveCriticalSection(&gSection);
        gS->Sleep(0);
    }
    return 3;
}

DWORD WINAPI MutexUser(void *)
{
    for (int i = 0; i < 6; ++i) {
        gS->WaitForSingleObject(gMutex, INFINITE);
        Note("mutex %d t=%llu", i, Read());
        Jitter();
        gS->Sleep(4);
        ReleaseMutex(gMutex);
        gS->Sleep(1);
    }
    return 4;
}

struct MixedResult {
    std::vector<std::string> log;
    Stats stats;
    int shared = 0;
    std::string census;
};

MixedResult RunMixed(uint32_t seed)
{
    MixedResult out;
    gJitterSeed = seed;
    gEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    gMutex = CreateMutexW(nullptr, FALSE, nullptr);
    InitializeCriticalSection(&gSection);
    gShared = 0;
    {
        Run run;
        HANDLE threads[4];
        LPTHREAD_START_ROUTINE routines[4] = {&Sleeper, &EventWaiter, &SectionUser, &MutexUser};
        for (int i = 0; i < 4; ++i) threads[i] = run.s.CreateThread(nullptr, 0, routines[i], nullptr, 0, nullptr);
        for (uint32_t tick = 1; tick <= 12; ++tick) {
            run.Tick(tick);
            Note("main tick %u t=%llu", tick, Read());
            Jitter();
            if (tick % 3 == 0) SetEvent(gEvent);
            run.s.EnterCriticalSection(&gSection);
            ++gShared;
            Note("main section t=%llu", Read());
            LeaveCriticalSection(&gSection);
            if (tick % 4 == 0) {
                run.s.WaitForSingleObject(gMutex, INFINITE);
                Note("main mutex t=%llu", Read());
                ReleaseMutex(gMutex);
            }
            run.s.Sleep(2);
        }
        Join(threads, 4);
        Note("joined t=%llu", Read());
        out.stats = run.s.TakeStats();
        out.census = run.s.Census();
    }
    out.log = gLog;
    out.shared = gShared;
    DeleteCriticalSection(&gSection);
    CloseHandle(gEvent);
    CloseHandle(gMutex);
    gJitterSeed = 0;
    return out;
}

size_t Count(const std::vector<std::string> &log, const char *prefix)
{
    size_t n = 0;
    for (const auto &line : log)
        if (line.compare(0, std::strlen(prefix), prefix) == 0) ++n;
    return n;
}

} // namespace

DIFF_TEST(HostLockstep_Deterministic, 0)
{
    const MixedResult a = RunMixed(0), b = RunMixed(12345), c = RunMixed(987654321);
    if (Count(a.log, "sleeper") != 10 || Count(a.log, "waiter") != 10 || Count(a.log, "section") != 8 ||
        Count(a.log, "mutex") != 6 || Count(a.log, "main tick") != 12 || Count(a.log, "joined") != 1)
        ctx.Fail("the program did not run to the end (%u lines)", static_cast<unsigned>(a.log.size()));
    if (a.shared != 8 + 12) ctx.Fail("critical section: the shared count is %d, expected 20", a.shared);
    for (const MixedResult *other : {&b, &c}) {
        if (other->log != a.log) {
            size_t i = 0;
            while (i < a.log.size() && i < other->log.size() && a.log[i] == other->log[i]) ++i;
            ctx.Fail("jitter changed the program: line %u '%s' vs '%s'", static_cast<unsigned>(i),
                     i < a.log.size() ? a.log[i].c_str() : "(end)", i < other->log.size() ? other->log[i].c_str() : "(end)");
        }
        if (other->stats.sequence != a.stats.sequence || other->stats.handoffs != a.stats.handoffs ||
            other->stats.advances != a.stats.advances)
            ctx.Fail("jitter changed the schedule: %u/%u hand-overs, %u/%u advances", a.stats.handoffs, other->stats.handoffs,
                     a.stats.advances, other->stats.advances);
    }
    if (a.stats.threads != 4 || !a.stats.handoffs || !a.stats.advances) ctx.Fail("stats: %u threads", a.stats.threads);
    if (a.census.find("thread 4 ") == std::string::npos || a.census.find("dead") == std::string::npos)
        ctx.Fail("census: %s", a.census.c_str());
    ctx.cases += 3;

    // The sleeper's times are 7 ms apart exactly (virtual), whatever happened in real time.
    std::vector<uint64_t> times;
    for (const auto &line : a.log) {
        unsigned long long t;
        int i;
        if (std::sscanf(line.c_str(), "sleeper %d t=%llu", &i, &t) == 2) times.push_back(t);
    }
    for (size_t i = 1; i < times.size(); ++i)
        if (times[i] - times[i - 1] < 7 * frameclock::kUnitsPerMs || times[i] - times[i - 1] > 7 * frameclock::kUnitsPerMs + 1000)
            ctx.Fail("sleeper step %u: %llu units", static_cast<unsigned>(i),
                     static_cast<unsigned long long>(times[i] - times[i - 1]));
    ++ctx.cases;
}

DIFF_TEST(HostLockstep_VirtualTimeouts, 0)
{
    Run run;
    HANDLE never = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const ULONGLONG real0 = GetTickCount64();
    const uint64_t t0 = Read();
    const DWORD r = run.s.WaitForSingleObject(never, 5000);
    const uint64_t t1 = Read();
    run.s.Sleep(2500);
    const uint64_t t2 = Read();
    const DWORD ex = run.s.SleepEx(1000, TRUE);
    const uint64_t t3 = Read();
    const ULONGLONG real = GetTickCount64() - real0;
    if (r != WAIT_TIMEOUT) ctx.Fail("wait result %lu", r);
    if (t1 - t0 < 5000 * frameclock::kUnitsPerMs || t1 - t0 > 5000 * frameclock::kUnitsPerMs + 100)
        ctx.Fail("a 5 s time-out took %llu units", static_cast<unsigned long long>(t1 - t0));
    if (t2 - t1 < 2500 * frameclock::kUnitsPerMs || t2 - t1 > 2500 * frameclock::kUnitsPerMs + 100)
        ctx.Fail("a 2.5 s sleep took %llu units", static_cast<unsigned long long>(t2 - t1));
    if (ex != 0 || t3 - t2 < 1000 * frameclock::kUnitsPerMs) ctx.Fail("SleepEx: %lu", ex);
    if (real > 2000) ctx.Fail("8.5 virtual seconds took %llu ms of real time", static_cast<unsigned long long>(real));
    // A signalled object returns at once, without virtual time passing.
    SetEvent(never);
    const uint64_t t4 = Read();
    if (run.s.WaitForSingleObject(never, 5000) != WAIT_OBJECT_0 || Read() - t4 > 100) ctx.Fail("signalled wait");
    CloseHandle(never);
    ctx.cases += 2;
}

namespace {

DWORD gApcThread;

void CALLBACK Apc(ULONG_PTR data)
{
    Note("apc %lu on %s", static_cast<unsigned long>(data), GetCurrentThreadId() == gApcThread ? "the worker" : "another thread");
}

DWORD WINAPI AlertableWorker(void *)
{
    gApcThread = GetCurrentThreadId();
    Note("worker waits");
    const DWORD r = gS->SleepEx(INFINITE, TRUE);
    Note("worker woke %lu", r);
    return 0;
}

std::atomic<int> gSpins{0};
volatile bool gStop = false;

DWORD WINAPI Spinner(void *)
{
    for (;;) {
        ++gSpins;
        if (gStop) return 0;
        gS->Sleep(0);
    }
}

DWORD WINAPI Starter(void *)
{
    Note("started");
    return 7;
}

} // namespace

DIFF_TEST(HostLockstep_Apc, 0)
{
    Run run;
    HANDLE h = run.s.CreateThread(nullptr, 0, &AlertableWorker, nullptr, 0, nullptr);
    run.Tick(1);
    if (!run.s.QueueUserAPC(&Apc, h, 42)) ctx.Fail("QueueUserAPC");
    Note("queued");
    run.Tick(2);
    Join(&h, 1);
    const std::vector<std::string> want = {"worker waits", "queued", "apc 42 on the worker", "worker woke 192"};
    if (gLog != want) ctx.Fail("order: %u lines, first '%s'", static_cast<unsigned>(gLog.size()), gLog.empty() ? "" : gLog[0].c_str());
    if (run.s.TakeStats().apcs != 1) ctx.Fail("apc count");
    ++ctx.cases;
}

DIFF_TEST(HostLockstep_StartAndJoin, 0)
{
    Run run;
    DWORD id = 0;
    HANDLE h = run.s.CreateThread(nullptr, 0, &Starter, nullptr, CREATE_SUSPENDED, &id);
    if (!h || !id) ctx.Fail("CreateThread");
    if (run.s.IndexOf(id) != 1 || run.s.IndexOf(GetCurrentThreadId()) != 0 || run.s.IndexOf(run.s.DriverThreadId()) != ~0u)
        ctx.Fail("IndexOf");
    run.Tick(1);
    run.Tick(2);
    if (!gLog.empty()) ctx.Fail("a suspended thread ran");
    if (run.s.ResumeThread(h) != 1 || run.s.ResumeThread(h) != 0) ctx.Fail("ResumeThread's counts");
    Note("resumed");
    // A new thread runs when its creator waits, not before.
    if (gLog.size() != 1) ctx.Fail("ran before the creator waited");
    // The join: the thread hands over at its end, and the wait on its handle ends.
    if (run.s.WaitForSingleObject(h, INFINITE) != WAIT_OBJECT_0) ctx.Fail("join");
    DWORD code = 0;
    GetExitCodeThread(h, &code);
    if (code != 7) ctx.Fail("exit code %lu", code);
    const std::vector<std::string> want = {"resumed", "started"};
    if (gLog != want) ctx.Fail("order");
    if (run.s.Threads() != 2) ctx.Fail("threads %u", run.s.Threads());
    CloseHandle(h);
    ++ctx.cases;
}

DIFF_TEST(HostLockstep_YieldBudget, 0)
{
    gSpins = 0;
    gStop = false;
    Run run;
    HANDLE h = run.s.CreateThread(nullptr, 0, &Spinner, nullptr, 0, nullptr);
    run.Tick(1);
    const int first = gSpins;
    run.Tick(2);
    const int second = gSpins;
    // The main thread blocked: every worker yield moves time on by kCreep, so a 1 ms sleep ends after 100 of them.
    run.s.Sleep(1);
    const int third = gSpins;
    gStop = true;
    Join(&h, 1);
    if (first != static_cast<int>(kYieldBudget) + 1) ctx.Fail("tick 1: %d spins", first);
    if (second - first != static_cast<int>(kYieldBudget)) ctx.Fail("tick 2: %d spins", second - first);
    if (third - second != static_cast<int>(frameclock::kUnitsPerMs / kCreep)) ctx.Fail("sleep: %d spins", third - second);
    ++ctx.cases;
}

namespace {

SRWLOCK gLock = SRWLOCK_INIT;
CONDITION_VARIABLE gVariable = CONDITION_VARIABLE_INIT, gQuiet = CONDITION_VARIABLE_INIT;
bool gReady = false;

DWORD WINAPI VariableWaiter(void *)
{
    gS->AcquireSRWLockExclusive(&gLock);
    while (!gReady) gS->SleepConditionVariableSRW(&gVariable, &gLock, INFINITE, 0);
    Note("waiter ready");
    const uint64_t t0 = Read();
    const BOOL ok = gS->SleepConditionVariableSRW(&gQuiet, &gLock, 30, 0);
    const DWORD error = GetLastError();
    Note("quiet %d %lu %llu", ok, error, (Read() - t0) / frameclock::kUnitsPerMs);
    ReleaseSRWLockExclusive(&gLock);
    return 0;
}

HWND gWindow;

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_USER + 1) {
        Note("proc %u on %s", static_cast<unsigned>(wparam), gS->ThreadIndex() == 0 ? "main" : "a worker");
        return static_cast<LRESULT>(wparam * 2);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

DWORD WINAPI Sender(void *)
{
    Note("worker sends");
    const LRESULT r = gS->SendMessageA(gWindow, WM_USER + 1, 21, 0);
    Note("worker got %ld", static_cast<long>(r));
    // A window function that sends to the window's thread (SetWindowTextA): run there, like a sent message.
    int arg = 5;
    const uintptr_t w = gS->OnWindowThread(
        gWindow,
        [](void *c) -> uintptr_t {
            Note("window call on %s", gS->ThreadIndex() == 0 ? "main" : "a worker");
            return static_cast<uintptr_t>(*static_cast<int *>(c) * 3);
        },
        &arg);
    Note("worker got %u", static_cast<unsigned>(w));
    return 0;
}

HANDLE gReleased;

DWORD WINAPI Gate(void *arg)
{
    gS->WaitForSingleObject(static_cast<HANDLE>(arg), INFINITE);
    SetEvent(gReleased);
    return 0;
}

} // namespace

DIFF_TEST(HostLockstep_ConditionVariable, 0)
{
    gReady = false;
    Run run;
    HANDLE h = run.s.CreateThread(nullptr, 0, &VariableWaiter, nullptr, 0, nullptr);
    run.Tick(1); // the waiter sleeps on the variable
    run.s.AcquireSRWLockExclusive(&gLock);
    gReady = true;
    run.s.WakeAllConditionVariable(&gVariable);
    Note("main woke it");
    run.Tick(2); // the waiter can't have the lock yet
    ReleaseSRWLockExclusive(&gLock);
    run.Tick(3);
    Join(&h, 1);
    const std::vector<std::string> want = {"main woke it", "waiter ready", "quiet 0 1460 30"};
    if (gLog != want) ctx.Fail("order: %s / %s", gLog.size() > 0 ? gLog[0].c_str() : "", gLog.size() > 2 ? gLog[2].c_str() : "");
    ++ctx.cases;
}

DIFF_TEST(HostLockstep_SendMessage, 0)
{
    Run run;
    WNDCLASSW wc{};
    wc.lpfnWndProc = &Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TS2LockstepTest";
    RegisterClassW(&wc);
    gWindow = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!gWindow) ctx.Fail("window");
    HANDLE h = run.s.CreateThread(nullptr, 0, &Sender, nullptr, 0, nullptr);
    run.Tick(1);
    Note("main before peek");
    MSG msg;
    run.s.PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE);
    Note("main after peek");
    run.Tick(2);
    Note("main before second peek");
    run.s.PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE);
    run.Tick(3);
    Join(&h, 1);
    // The main thread's own window: the call runs at once, here.
    int arg = 7;
    const uintptr_t own = run.s.OnWindowThread(
        gWindow, [](void *c) -> uintptr_t { return static_cast<uintptr_t>(*static_cast<int *>(c) + 1); }, &arg);
    DestroyWindow(gWindow);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    const std::vector<std::string> want = {"worker sends",        "main before peek",        "proc 21 on main",
                                           "main after peek",     "worker got 42",           "main before second peek",
                                           "window call on main", "worker got 15"};
    if (gLog != want)
        ctx.Fail("order: %u lines, second '%s', sixth '%s'", static_cast<unsigned>(gLog.size()), gLog.size() > 1 ? gLog[1].c_str() : "",
                 gLog.size() > 6 ? gLog[6].c_str() : "");
    if (run.s.TakeStats().calls != 2) ctx.Fail("calls");
    if (own != 8) ctx.Fail("own window's call: %u", static_cast<unsigned>(own));
    ++ctx.cases;
}

namespace {

HANDLE gOutsideGo;

DWORD WINAPI OutsideWaiter(void *)
{
    ::WaitForSingleObject(gOutsideGo, INFINITE);
    return 0;
}

std::string NameThread(const void *routine, const void *param)
{
    return routine == reinterpret_cast<const void *>(&Gate) ? (param ? "gate with an event" : "gate") : "?";
}

std::string NameAddress(uintptr_t address)
{
    if (address == reinterpret_cast<uintptr_t>(&OutsideWaiter)) return "OutsideWaiter";
    if (address == 0x1234) return "caller 0x1234";
    return "";
}

bool Logged(const char *text)
{
    std::lock_guard<std::mutex> guard(gSchedLogLock);
    for (const auto &line : gSchedLog)
        if (line.find(text) != std::string::npos) return true;
    return false;
}

std::string LoggedLine(const char *text)
{
    std::lock_guard<std::mutex> guard(gSchedLogLock);
    for (const auto &line : gSchedLog)
        if (line.find(text) != std::string::npos) return line;
    return "";
}

} // namespace

DIFF_TEST(HostLockstep_Stuck, 0)
{
    // The main thread blocks in a call the scheduler doesn't see (as our toolchain's own imports would) on a parked
    // worker: after the stuck time lockstep ends and the worker runs freely. The report says where everyone was: the
    // census with the last intercepted call and its caller, the scheduled threads' stacks, and the threads outside.
    gOutsideGo = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD outsideId = 0;
    HANDLE outside = ::CreateThread(nullptr, 0, &OutsideWaiter, nullptr, 0, &outsideId);
    Run run(1000);
    run.s.SetNamers(&NameThread, &NameAddress);
    HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    gReleased = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD workerId = 0;
    HANDLE h = run.s.CreateThread(nullptr, 0, &Gate, gate, 0, &workerId);
    run.Tick(1); // the worker parks on the gate
    run.s.Called("WaitForSingleObject", reinterpret_cast<const void *>(0x1234));
    SetEvent(gate);
    const ULONGLONG real0 = GetTickCount64();
    const DWORD r = ::WaitForSingleObject(gReleased, 15000);
    const ULONGLONG real = GetTickCount64() - real0;
    if (r != WAIT_OBJECT_0) ctx.Fail("the worker never ran (%lu)", r);
    if (real > 6000) ctx.Fail("freed after %llu ms, stuck time 1000 ms", real);
    if (!run.s.Free()) ctx.Fail("not free");
    if (!Logged("free from here on: thread 0 ")) ctx.Fail("no log line");
    ++ctx.cases;
    // The report, logged after that line, one "lockstep: stuck: " line each. Wait for its OutsideWaiter line: outside
    // threads are listed in the system's order, so the first outside line can come before it. (Under load the stuck
    // rule can take two seconds, and the no-tick report, at twice the stuck time, may come first: also fine.)
    char want[64];
    std::snprintf(want, sizeof want, "stuck: outside thread id %lu (start OutsideWaiter): eip ntdll", outsideId);
    for (int i = 0; i < 300 && !Logged(want); ++i) ::Sleep(10);
    if (!Logged("lockstep: stuck: token thread 0, ") || !Logged("lockstep: stuck: driver stack: eip "))
        ctx.Fail("scheduler state or driver stack: %s", LoggedLine("stuck: token").c_str());
    if (!Logged("lockstep: stuck: thread 0 (id ") || !Logged(", main): running ") ||
        !Logged("1 calls, last WaitForSingleObject from caller 0x1234"))
        ctx.Fail("census of the main thread: %s", LoggedLine("stuck: thread 0 (").c_str());
    if (!Logged("lockstep: stuck: thread 1 (id ") || !Logged(", gate with an event): waiting objects"))
        ctx.Fail("census of the worker: %s", LoggedLine("stuck: thread 1 (").c_str());
    const std::string mainStack = LoggedLine("stuck: thread 0 stack: ");
    if (mainStack.find("eip ntdll.dll+0x") == std::string::npos || mainStack.find("difftest.exe+0x") == std::string::npos)
        ctx.Fail("main thread's stack: %s", mainStack.c_str());
    if (!Logged("stuck: thread 1 stack: eip ntdll.dll+0x")) ctx.Fail("worker's stack: %s", LoggedLine("thread 1 stack").c_str());
    if (!Logged(want)) ctx.Fail("outside thread: %s", LoggedLine("stuck: outside thread").c_str());
    std::snprintf(want, sizeof want, "outside thread id %lu ", run.s.DriverThreadId());
    if (Logged(want) || !run.s.DriverThreadId()) ctx.Fail("the driver is in the report as an outside thread");
    std::snprintf(want, sizeof want, "outside thread id %lu ", workerId);
    if (Logged(want)) ctx.Fail("a scheduled thread is in the report as an outside thread");
    // Asked from another thread (here the main thread), the report leaves the driver out too.
    std::snprintf(want, sizeof want, "outside thread id %lu ", run.s.DriverThreadId());
    const std::string report = run.s.Report();
    if (report.find(want) != std::string::npos || report.find("thread 0 stack: ") != std::string::npos ||
        report.find("(start OutsideWaiter): eip ") == std::string::npos)
        ctx.Fail("report from the main thread: %s", report.c_str());
    ++ctx.cases;
    // Free: the replacements are the real functions.
    if (run.s.WaitForSingleObject(h, 5000) != WAIT_OBJECT_0) ctx.Fail("join after free");
    SetEvent(gOutsideGo);
    ::WaitForSingleObject(outside, 5000);
    CloseHandle(outside);
    CloseHandle(gOutsideGo);
    CloseHandle(h);
    CloseHandle(gate);
    CloseHandle(gReleased);
}

namespace {

HANDLE gPulsed;

DWORD WINAPI PulseWaiter(void *arg)
{
    const DWORD r = gS->WaitForSingleObject(gPulsed, 1000);
    Note("waiter %d r=%lu t=%llu", static_cast<int>(reinterpret_cast<intptr_t>(arg)), r, Read() / frameclock::kUnitsPerMs);
    return 0;
}

std::vector<std::string> RunPulses(bool manual)
{
    gPulsed = CreateEventW(nullptr, manual, FALSE, nullptr);
    {
        Run run;
        HANDLE threads[2];
        for (int i = 0; i < 2; ++i)
            threads[i] = run.s.CreateThread(nullptr, 0, &PulseWaiter, reinterpret_cast<void *>(static_cast<intptr_t>(i)), 0, nullptr);
        run.Tick(1); // both wait (1 s time-out)
        Note("pulse 1");
        run.s.PulseEvent(gPulsed);
        run.Tick(2);
        Note("pulse 2");
        run.s.PulseEvent(gPulsed);
        run.Tick(3);
        Join(threads, 2);
        Note("pulses %u", run.s.TakeStats().pulses);
        // Nobody waits: the pulse is lost, as in Windows (a wait afterwards times out).
        run.s.PulseEvent(gPulsed);
        Note("after r=%lu", run.s.WaitForSingleObject(gPulsed, 10));
    }
    CloseHandle(gPulsed);
    return gLog;
}

} // namespace

DIFF_TEST(HostLockstep_PulseEvent, 0)
{
    // cRZThreadSignal's Signal and Broadcast: an auto-reset event's pulse releases one waiter (the first in creation
    // order), a manual-reset event's all of them, at the time of the pulse. Without the scheduler's pulse the parked
    // waiters would only see their time-outs (1 s).
    const std::vector<std::string> one = RunPulses(false);
    // A released waiter runs when the pulsing thread next waits (here the next tick's yield), at that tick's time.
    const std::vector<std::string> wantOne = {"pulse 1", "waiter 0 r=0 t=66", "pulse 2", "waiter 1 r=0 t=99", "pulses 2",
                                              "after r=258"};
    if (one != wantOne)
        ctx.Fail("auto-reset: %u lines: %s / %s / %s", static_cast<unsigned>(one.size()), one.size() > 1 ? one[1].c_str() : "",
                 one.size() > 3 ? one[3].c_str() : "", one.size() > 5 ? one[5].c_str() : "");
    const std::vector<std::string> all = RunPulses(true);
    const std::vector<std::string> wantAll = {"pulse 1", "waiter 0 r=0 t=66", "waiter 1 r=0 t=66", "pulse 2", "pulses 2",
                                              "after r=258"};
    if (all != wantAll)
        ctx.Fail("manual-reset: %u lines: %s / %s / %s", static_cast<unsigned>(all.size()), all.size() > 1 ? all[1].c_str() : "",
                 all.size() > 2 ? all[2].c_str() : "", all.size() > 4 ? all[4].c_str() : "");
    ctx.cases += 2;
}

namespace {

DWORD gFls = FLS_OUT_OF_INDEXES;
std::atomic<uint32_t> gFlsSawIndex{0};

// A fiber-local destructor (the C runtime's frees its per-thread data and takes its locks): it runs while the thread
// ends, after the thread left the scheduler.
void NTAPI FlsDone(void *)
{
    gFlsSawIndex = gS->ThreadIndex();
    gS->Sleep(1); // the real Sleep: an ending thread doesn't wait in the lockstep
}

// How the worker ends: 0 its routine returns, 1 FreeLibraryAndExitThread (the C runtime's _endthreadex), 2 a way out the
// scheduler doesn't see (the real ExitThread).
DWORD WINAPI Ender(void *arg)
{
    const int how = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    if (how != 2) FlsSetValue(gFls, &gFls);
    Note("worker %d ends", how);
    if (how == 1) gS->FreeLibraryAndExitThread(LoadLibraryW(L"kernel32.dll"), 9);
    if (how == 2) ::ExitThread(9);
    return 9;
}

struct Ending {
    std::vector<std::string> log;
    uint32_t flsSawIndex = 0;
    uint32_t unseen = 0;
    bool free = false;
    DWORD code = 0;
    ULONGLONG ms = 0;
};

Ending RunEnding(int how)
{
    Ending e;
    gFlsSawIndex = 0xdead;
    gFls = FlsAlloc(&FlsDone);
    {
        Run run;
        HANDLE h = run.s.CreateThread(nullptr, 0, &Ender, reinterpret_cast<void *>(static_cast<intptr_t>(how)), 0, nullptr);
        const ULONGLONG real0 = GetTickCount64();
        run.Tick(1); // the worker runs and ends
        Note("main at tick 2");
        run.Tick(2);
        Note("join r=%lu", run.s.WaitForSingleObject(h, 5000));
        e.ms = GetTickCount64() - real0;
        GetExitCodeThread(h, &e.code);
        e.unseen = run.s.TakeStats().unseenExits;
        e.free = run.s.Free();
        CloseHandle(h);
    }
    FlsFree(gFls);
    e.flsSawIndex = gFlsSawIndex;
    e.log = gLog;
    return e;
}

} // namespace

DIFF_TEST(HostLockstep_ThreadEnds, 0)
{
    // However a game thread ends, the token goes on and lockstep goes on: the C runtime's _endthreadex ends the image's
    // threads with FreeLibraryAndExitThread (in the 2026-10-19 runs thread 5 ended so, unseen, and held the token until
    // the stuck rule ended lockstep).
    for (int how = 0; how < 2; ++how) {
        const Ending e = RunEnding(how);
        char first[32];
        std::snprintf(first, sizeof first, "worker %d ends", how);
        const std::vector<std::string> want = {first, "main at tick 2", "join r=0"};
        if (e.log != want)
            ctx.Fail("end %d: %u lines: %s / %s", how, static_cast<unsigned>(e.log.size()), e.log.empty() ? "" : e.log[0].c_str(),
                     e.log.size() > 2 ? e.log[2].c_str() : "");
        if (e.code != 9 || e.free || e.unseen) ctx.Fail("end %d: code %lu, free %d, unseen %u", how, e.code, e.free, e.unseen);
        // What runs on the thread after it ended in the lockstep is outside it.
        if (e.flsSawIndex != ~0u) ctx.Fail("end %d: the fiber-local destructor ran as thread %u", how, e.flsSawIndex);
        if (e.ms > 3000) ctx.Fail("end %d: %llu ms", how, e.ms);
        ++ctx.cases;
    }
    // A way out the scheduler doesn't see: the driver finds the token holder gone, logs it and goes on (not free).
    const Ending e = RunEnding(2);
    const std::vector<std::string> want = {"worker 2 ends", "main at tick 2", "join r=0"};
    if (e.log != want) ctx.Fail("unseen end: %u lines", static_cast<unsigned>(e.log.size()));
    if (e.code != 9 || e.free || e.unseen != 1) ctx.Fail("unseen end: code %lu, free %d, unseen %u", e.code, e.free, e.unseen);
    if (!Logged("lockstep: thread 1 (") || !Logged("ended without the scheduler seeing it; last "))
        ctx.Fail("no log line for the unseen end");
    if (e.ms > 3000) ctx.Fail("unseen end: %llu ms", e.ms);
    ++ctx.cases;
}

namespace {

volatile LONG gTimerRunning = 0;
HANDLE gNever;

// The sound timer thread (0xea0dd): sleeps, then sees it should stop and says so.
DWORD WINAPI LateWorker(void *)
{
    gS->Sleep(50);
    Note("worker woke t=%llu", Read() / frameclock::kUnitsPerMs);
    InterlockedExchange(&gTimerRunning, 0);
    return 0;
}

// The main thread stops it as the game does (0xe9780): Sleep(0) until the flag drops.
std::vector<std::string> RunMainSpin()
{
    gTimerRunning = 1;
    Run run;
    HANDLE h = run.s.CreateThread(nullptr, 0, &LateWorker, nullptr, 0, nullptr);
    run.Tick(1); // the worker sleeps past the next tick
    uint32_t spins = 0;
    while (gTimerRunning && spins < 100000) {
        run.s.Sleep(0);
        ++spins;
    }
    Note("main spun %u t=%llu", spins, Read() / frameclock::kUnitsPerMs);
    Join(&h, 1);
    return gLog;
}

DWORD WINAPI NeverWoken(void *)
{
    gS->WaitForSingleObject(gNever, INFINITE);
    return 0;
}

} // namespace

DIFF_TEST(HostLockstep_MainSpin, 0)
{
    // A Sleep(0) of the main thread outside the tick's yield moves virtual time on by kCreep when no worker can run:
    // otherwise the sleeping worker never wakes and the main thread spins for ever (the 2026-10-19 11:44 runs, frame 5).
    const std::vector<std::string> a = RunMainSpin();
    const std::vector<std::string> b = RunMainSpin();
    const std::vector<std::string> want = {"worker woke t=83", "main spun 5000 t=83"};
    if (a != want || b != a)
        ctx.Fail("%u lines: %s / %s; again: %s", static_cast<unsigned>(a.size()), a.empty() ? "" : a[0].c_str(),
                 a.size() > 1 ? a[1].c_str() : "", b.size() > 1 ? b[1].c_str() : "");
    ++ctx.cases;
}

DIFF_TEST(HostLockstep_NoTick, 0)
{
    // The main thread spins on Sleep(0) (a hand-over each time, so the stuck rule sees progress) and no tick comes, while
    // a worker waits for ever: the watchdog reports at 2 x the stuck time, then ends lockstep at 6 x.
    gNever = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Run run(500);
    HANDLE h = run.s.CreateThread(nullptr, 0, &NeverWoken, nullptr, 0, nullptr);
    // Ticks 300 ms apart for 2.4 s: no report (each tick restarts the count).
    for (uint32_t tick = 1; tick <= 8; ++tick) {
        run.Tick(tick);
        ::Sleep(300);
    }
    if (Logged("no tick")) ctx.Fail("a no-tick report while ticks came");
    const ULONGLONG real0 = GetTickCount64();
    ULONGLONG reportedAt = 0;
    while (!run.s.Free() && GetTickCount64() - real0 < 10000) {
        run.s.Sleep(0);
        if (!reportedAt && Logged("lockstep: no tick for ")) reportedAt = GetTickCount64() - real0;
    }
    const ULONGLONG freedAt = GetTickCount64() - real0;
    // The free line and the report are logged just after the flag goes up.
    for (int i = 0; i < 200 && !Logged("lockstep: stuck: driver stack: "); ++i) ::Sleep(10);
    if (!run.s.Free()) ctx.Fail("not free after 10 s");
    if (!reportedAt || reportedAt < 400 || reportedAt > 2500) ctx.Fail("report at %llu ms (want about 1000)", reportedAt);
    if (freedAt < 2400 || freedAt > 5000) ctx.Fail("free at %llu ms (want about 3000)", freedAt);
    if (!Logged("free from here on: no tick for 3 s")) ctx.Fail("free line: %s", LoggedLine("free from here on").c_str());
    ++ctx.cases;
    // The report: the scheduler's state, the census, the driver's stack, from both the report and the free (logged just
    // after the free line).
    for (int i = 0; i < 200 && !Logged("lockstep: stuck: driver stack: "); ++i) ::Sleep(10);
    if (!Logged("lockstep: no tick: token thread 0, ") || !Logged("lockstep: no tick: thread 1 (id ") ||
        !Logged("lockstep: no tick: driver stack: eip ") || !Logged("lockstep: stuck: driver stack: eip "))
        ctx.Fail("report: %s / %s", LoggedLine("no tick: token").c_str(), LoggedLine("driver stack").c_str());
    ++ctx.cases;
    SetEvent(gNever);
    run.s.WaitForSingleObject(h, 5000);
    CloseHandle(h);
    CloseHandle(gNever);
}

// ---- threads our code starts through our CRT (host/shared/ourcrt.h) ----
//
// The host points its own CreateThread import at the scheduler for our CRT's _beginthreadex only (by its thread start
// routine, which ourcrt::FindThreadStart finds), and its ExitThread / FreeLibraryAndExitThread imports (our CRT's
// _endthreadex) at the scheduler (control_frameclock.cpp, LsHostCreateThread and kLockstepHostImports). The same routing
// here on the runner's imports (the runner links the same CRT): cRZThread's threads are then scheduled like the image's,
// and end seen by the scheduler whether their routine returns or calls _endthreadex.

namespace {

const void *gCrtStart = nullptr;
DWORD gHostThreadIndex = 0;

template <typename Fn> Fn Kernel32(const char *name)
{
    return reinterpret_cast<Fn>(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name)));
}

HANDLE WINAPI RoutedCreateThread(LPSECURITY_ATTRIBUTES a, SIZE_T b, LPTHREAD_START_ROUTINE c, LPVOID d, DWORD e, LPDWORD f)
{
    if (gS && gCrtStart && reinterpret_cast<const void *>(c) == gCrtStart) return gS->CreateThread(a, b, c, d, e, f);
    return Kernel32<decltype(&::CreateThread)>("CreateThread")(a, b, c, d, e, f);
}
void WINAPI RoutedExitThread(DWORD code)
{
    if (gS) gS->ExitThread(code);
    Kernel32<decltype(&::ExitThread)>("ExitThread")(code);
}
void WINAPI RoutedFreeLibraryAndExitThread(HMODULE module, DWORD code)
{
    if (gS) gS->FreeLibraryAndExitThread(module, code);
    Kernel32<decltype(&::FreeLibraryAndExitThread)>("FreeLibraryAndExitThread")(module, code);
}

struct RoutedImports {
    struct Patch {
        const char *name;
        void *function;
        void *previous = nullptr;
        bool patched = false;
    };
    Patch patches[3] = {{"CreateThread", reinterpret_cast<void *>(&RoutedCreateThread)},
                        {"ExitThread", reinterpret_cast<void *>(&RoutedExitThread)},
                        {"FreeLibraryAndExitThread", reinterpret_cast<void *>(&RoutedFreeLibraryAndExitThread)}};
    RoutedImports()
    {
        for (Patch &p : patches)
            p.patched = ourcrt::PatchImport(GetModuleHandleW(nullptr), "KERNEL32.dll", p.name, p.function, &p.previous);
    }
    ~RoutedImports()
    {
        for (Patch &p : patches)
            if (p.patched) ourcrt::PatchImport(GetModuleHandleW(nullptr), "KERNEL32.dll", p.name, p.previous);
    }
    bool All() const { return patches[0].patched && patches[1].patched && patches[2].patched; }
};

// cRZThread's two kinds of end: ExitWith's _endthreadex (a), and a routine that returns (b: thread_start's _endthreadex).
unsigned __stdcall CrtWorkerA(void *)
{
    Note("crt a thread %u t=%llu", gS->ThreadIndex(), Read());
    Jitter();
    gS->SleepEx(5, TRUE);
    Note("crt a woke t=%llu", Read());
    Jitter();
    _endthreadex(7);
    return 0;
}
unsigned __stdcall CrtWorkerB(void *)
{
    Note("crt b thread %u t=%llu", gS->ThreadIndex(), Read());
    Jitter();
    return 3;
}
// A host thread (plain CreateThread): not scheduled.
DWORD WINAPI HostThread(void *)
{
    gHostThreadIndex = gS->ThreadIndex();
    return 0;
}

struct CrtRun {
    std::vector<std::string> log;
    Stats stats;
    DWORD codes[2] = {};
    uint32_t threads = 0;
    bool free = false, routed = false;
};

CrtRun RunCrtThreads(uint32_t seed)
{
    CrtRun out;
    gJitterSeed = seed;
    gHostThreadIndex = 0;
    {
        Run run;
        RoutedImports routed;
        out.routed = routed.All();
        auto a = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &CrtWorkerA, nullptr, 0, nullptr));
        Jitter();
        auto b = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &CrtWorkerB, nullptr, 0, nullptr));
        HANDLE host = CreateThread(nullptr, 0, &HostThread, nullptr, 0, nullptr);
        Note("main started them, t=%llu", Read());
        run.Tick(1); // a runs and sleeps, b runs and ends, a wakes (5 ms, before tick 2) and ends
        Note("main tick 1 t=%llu", Read());
        HANDLE both[2] = {a, b};
        Note("joined r=%lu", run.s.WaitForMultipleObjects(2, both, TRUE, 5000));
        WaitForSingleObject(host, 5000);
        CloseHandle(host);
        GetExitCodeThread(a, &out.codes[0]);
        GetExitCodeThread(b, &out.codes[1]);
        CloseHandle(a);
        CloseHandle(b);
        out.stats = run.s.TakeStats();
        out.threads = run.s.Threads();
        out.free = run.s.Free();
    }
    gJitterSeed = 0;
    out.log = gLog;
    return out;
}

} // namespace

DIFF_TEST(HostLockstep_OurCrtThreads, 0)
{
    gCrtStart = ourcrt::FindThreadStart(GetModuleHandleW(nullptr));
    if (!gCrtStart) {
        ctx.Fail("our CRT's thread start routine not found");
        return;
    }
    const CrtRun r = RunCrtThreads(0);
    if (!r.routed) ctx.Fail("the runner's imports were not patched");
    // Both started parked (nothing ran before the main thread's tick), scheduled as threads 1 and 2 in creation order,
    // in virtual time (100 ns units: tick 1 at 33.333 ms, a's 5 ms sleep ends inside it), and seen to end however they
    // ended.
    const std::vector<std::string> want = {"main started them, t=0", "crt a thread 1 t=333330", "crt b thread 2 t=333330",
                                           "crt a woke t=383330", "main tick 1 t=333330", "joined r=0"};
    if (r.log != want) {
        std::string got;
        for (const auto &line : r.log) got += line + " / ";
        ctx.Fail("log: %s", got.c_str());
    }
    if (r.codes[0] != 7 || r.codes[1] != 3) ctx.Fail("exit codes %lu, %lu", r.codes[0], r.codes[1]);
    if (r.stats.threads != 2 || r.threads != 3 || r.stats.unseenExits || r.free)
        ctx.Fail("stats: %u threads created, %u known, %u unseen ends, free %d", r.stats.threads, r.threads,
                 r.stats.unseenExits, r.free);
    if (gHostThreadIndex != ~0u) ctx.Fail("the host's own thread is scheduled (index %lu)", gHostThreadIndex);
    ctx.cases += 3;
    // Real-time jitter changes nothing.
    for (uint32_t seed : {777u, 4242u}) {
        const CrtRun j = RunCrtThreads(seed);
        if (j.log != r.log || j.stats.sequence != r.stats.sequence || j.stats.handoffs != r.stats.handoffs)
            ctx.Fail("jitter %u changed the run (%u lines)", seed, static_cast<unsigned>(j.log.size()));
        ++ctx.cases;
    }
    gCrtStart = nullptr;
}
