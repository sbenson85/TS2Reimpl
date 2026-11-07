#pragma once
// Lockstep scheduling of the game's own threads (TS2HOST_FRAMECLOCK; host/control_frameclock.cpp, docs/ui-testing.md).
// A diagnostic control like the rest of host/: it decides when the game's threads run, never what they do. Built into
// the host and the difftest runner (tests/host_lockstep.cpp).
//
// With the frame clock alone the main thread's frames see fixed times, but the game's worker threads (the package
// opener, the log service, the sound timer, cRZThread users) run in real time beside it: a load that ends during frame
// N on one run ends during frame N+1 on the next, and everything after differs. In lockstep:
//   - one game thread runs at a time (it holds the token); the others are parked in their waits;
//   - a thread gives up the token only where it would wait: Sleep/SleepEx, WaitForSingleObject/MultipleObjects, a busy
//     critical section or SRW lock, SleepConditionVariableSRW, a message sent to another game thread (the image's
//     imports; the host replaces them in its import address table). A waiting thread's condition is checked again only
//     when the scheduler polls it, and conditions that take something (a mutex, an auto-reset event, a lock) are
//     checked by the waiting thread itself, so the right thread gets it;
//   - the next thread to run is the first, in creation order (the main thread is 0), whose condition holds;
//   - when none can run and the main thread is blocked, virtual time moves on to the earliest time-out: sleeps and wait
//     time-outs end by virtual time (host/shared/frameclock.h), never by real time;
//   - at each framework tick the main thread yields: every worker that can run does, until all wait, and workers' sleeps
//     and time-outs that end before the next tick's time end now, in time order (a worker that sleeps 7 ms runs about
//     five times per frame, as it would in real time; its clock reads the times it woke at). Work a frame started is
//     done before the next frame starts, whatever the machine's speed;
//   - Sleep(0) yields: the workers that can run go first, then the yielder. A worker yields at most kYieldBudget times
//     per tick ahead of a yielding main thread; while the main thread is blocked, each worker yield moves virtual time
//     on by kCreep, so a worker that spins on the clock with Sleep(0) gets there. A Sleep(0) of the main thread outside
//     the tick's yield, when no worker can run, moves virtual time on by kCreep (at most to the next worker's time-out),
//     so a main thread that spins on Sleep(0) for a sleeping worker (the sound timer's stop) gets it;
//   - APCs queued to a game thread (QueueUserAPC: cRZThread's wake-up) are held and run when it waits alertably, as
//     Windows would run them;
//   - SendMessage from one game thread to another's window is held until the receiver next calls PeekMessage (where
//     Windows would deliver it), and so are the window functions that send to the window's thread and wait
//     (SetWindowTextA, SetWindowPos, ShowWindow, MoveWindow, SetWindowLongA, UpdateWindow: OnWindowThread). The
//     caller would otherwise block outside the scheduler, holding the token, until the receiver pumps messages, which
//     it never does while parked;
//   - PulseEvent (cRZThreadSignal's Signal and Broadcast) releases the game threads waiting on the event at that moment
//     (one for an auto-reset event, all for a manual-reset one), as Windows would: a parked thread is not in a real wait,
//     so the real pulse alone would be lost;
//   - a new thread starts parked and first runs when the scheduler next picks (its creator waits or the tick ends);
//   - a thread that ends (its routine returns, ExitThread, or FreeLibraryAndExitThread: the C runtime's _endthreadex)
//     hands the token to the driver (a host thread), which picks the next one, and leaves the scheduler (what runs on it
//     while it ends gets the real functions). A token holder that ends any other way is seen by the driver, logged and
//     taken as ended (Stats::unseenExits).
// Threads the image did not create (Direct3D's, the sound driver's, the host's) are not scheduled: their calls pass
// through, and the signals they give are seen when the scheduler next polls (that part stays real-time). A watchdog (a
// host thread of its own, so nothing the scheduler blocks in stops it) ends lockstep when the thread holding the token
// makes no progress for kStuckMs of real time (no hand-over, almost no processor time) while others wait, or when no
// tick comes for kNoTickFree x kStuckMs (a report first at kNoTickReport x kStuckMs): everything runs freely from then
// on, and the host logs why, followed by a report (Report): the scheduler's state, each scheduled thread's state, last
// intercepted call and its caller, and stack, the driver's stack, then every other thread of the process with its start
// address and stack.
#define NOMINMAX
#include <windows.h>

#include "frameclock.h"
#include "threadstack.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lockstep {

constexpr uint64_t kNever = ~0ull;
constexpr uint32_t kYieldBudget = 1000; // Sleep(0)s per worker and tick that go before a yielding main thread
constexpr uint64_t kCreep = 100;        // virtual time per worker yield while the main thread is blocked (10 us)
constexpr uint32_t kStuckMs = 10'000;
constexpr uint32_t kNoTickReport = 2, kNoTickFree = 6; // x the stuck time: no tick that long reports, then ends lockstep

// What happened since the last TakeStats (the host writes it per tick).
struct Stats {
    uint32_t handoffs = 0; // the token went to another thread
    uint32_t polls = 0;    // waiting threads asked to check their condition
    uint32_t advances = 0; // virtual time moved on to a time-out
    uint32_t idle = 0;     // 1 ms real waits: nothing could run until a thread outside the game did something
    uint32_t outside = 0;  // blocking calls made holding the token (a message to a window outside the game's threads)
    uint32_t apcs = 0;     // APCs held for a game thread
    uint32_t threads = 0;  // threads created
    uint32_t calls = 0;    // messages sent between game threads
    uint32_t pulses = 0;   // waiting game threads released by PulseEvent
    uint32_t unseenExits = 0; // token holders that ended by a way out the scheduler does not intercept
    uint64_t sequence = 0; // hash of which thread ran, in order, and why
};

class Scheduler {
public:
    using ClockOf = frameclock::ThreadClock &(*)(); // the calling thread's clock
    using LogFn = void (*)(const char *text);
    // A name for a new thread, from its start routine and parameter (read while the creator waits in CreateThread).
    using ThreadNamer = std::string (*)(const void *routine, const void *param);

    Scheduler(frameclock::Clock &clock, ClockOf clockOf, LogFn log = nullptr);
    ~Scheduler(); // tests: ends lockstep, waits for the threads it started (5 s each) and the driver

    // The calling thread becomes the main thread (index 0, running). stuckMs 0: never give up.
    void Start(uint32_t stuckMs = kStuckMs);
    bool Free() const { return mFree.load(); }
    void GoFree(const char *why); // lockstep ends: parked threads continue with real waits

    // Replacements of the image's imports. Threads the scheduler didn't start, and everything once free, get the real
    // function.
    HANDLE CreateThread(LPSECURITY_ATTRIBUTES security, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID param, DWORD flags,
                        LPDWORD id);
    DWORD SuspendThread(HANDLE thread);
    DWORD ResumeThread(HANDLE thread);
    [[noreturn]] void ExitThread(DWORD code);
    [[noreturn]] void FreeLibraryAndExitThread(HMODULE module, DWORD code);
    void Sleep(DWORD ms);
    DWORD SleepEx(DWORD ms, BOOL alertable);
    DWORD WaitForSingleObject(HANDLE object, DWORD ms);
    DWORD WaitForMultipleObjects(DWORD count, const HANDLE *objects, BOOL all, DWORD ms);
    void EnterCriticalSection(CRITICAL_SECTION *section);
    void AcquireSRWLockExclusive(SRWLOCK *lock);
    BOOL SleepConditionVariableSRW(CONDITION_VARIABLE *variable, SRWLOCK *lock, DWORD ms, ULONG flags);
    void WakeAllConditionVariable(CONDITION_VARIABLE *variable);
    DWORD QueueUserAPC(PAPCFUNC function, HANDLE thread, ULONG_PTR data);
    LRESULT SendMessageA(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    BOOL PeekMessageA(MSG *message, HWND window, UINT first, UINT last, UINT remove);
    BOOL PulseEvent(HANDLE event);
    // A window function that Windows runs as a message sent to the window's thread and waits for (SetWindowTextA on
    // another thread's window sends WM_SETTEXT): when `window` belongs to another game thread, `function(context)` is
    // held for it and runs at its next PeekMessageA while the caller waits (as SendMessageA, which uses it); otherwise it
    // runs here. Returns its result.
    uintptr_t OnWindowThread(HWND window, uintptr_t (*function)(void *context), void *context);

    // The caller (a replacement of an import) records what the calling thread called and from where, for the census and
    // the stuck report. Any thread; nothing for threads the scheduler doesn't own.
    void Called(const char *import, const void *caller);

    // The main thread at a tick (Yield is a windows.h macro): the workers that can run do, until all wait.
    void TickYield();

    Stats TakeStats();
    // One line per thread: index, id, name, state, runs, waits, time-outs, its last intercepted call and the caller.
    std::string Census();
    // The census, then for each live scheduled thread its stack, then every other thread of the process (not the
    // driver, not the caller) with its start address and stack: one line each. The stuck rule logs it (from the
    // driver); any thread may ask (it leaves itself out).
    std::string Report();
    // The calling thread is one of the scheduler's (and lockstep is on).
    bool Scheduled();
    uint32_t ThreadIndex(); // the calling thread's index, or ~0u
    uint32_t IndexOf(DWORD threadId); // a thread's index (the same on every run), or ~0u: not one of the scheduler's
    uint32_t Threads();     // threads known (the main thread included)
    DWORD DriverThreadId() const { return mDriverId; }

    // Names: threads by their start routine and parameter (default: the routine's module and offset), addresses in
    // stacks and callers (default: module and offset).
    void SetNamers(ThreadNamer threads, threadstack::Namer addresses)
    {
        mThreadNamer = threads;
        mAddressNamer = addresses;
    }

    struct Slot;

private:
    enum class Outcome { Ran, Handed, Free, Untoken };

    Slot *Self();
    Slot *SlotOf(DWORD threadId);
    Slot *NewSlot(const void *routine);
    bool Block(Slot *me);
    bool Evaluate(Slot *s);
    bool Due(const Slot *s) const;
    bool HasApcs(Slot *s);
    void JoinDead(Slot *me);
    bool Poll(Slot *s);
    void Hand(Slot *s);
    Outcome Park(Slot *me);
    Outcome Schedule(Slot *self);
    Slot *PickYielder(Slot *self);
    bool MainBlocked();
    uint64_t MinDeadline();
    void Finish(Slot *me);
    bool Ended(Slot *runner);
    void RunApcs(Slot *me);
    void RunCalls(Slot *me);
    void Note(Slot *picked);
    uint64_t Generation(void *variable);
    void Log(const char *format, ...);
    void Drive();
    void Watch();
    void LogReport(const char *prefix, const std::string &report);
    static DWORD WINAPI Watchdog(void *arg);
    static DWORD WINAPI Trampoline(void *arg);
    static DWORD WINAPI Driver(void *arg);

    frameclock::Clock &mClock;
    ClockOf mClockOf;
    LogFn mLog;
    ThreadNamer mThreadNamer = nullptr;
    threadstack::Namer mAddressNamer = nullptr;
    DWORD mDriverId = 0;
    SRWLOCK mLock = SRWLOCK_INIT; // the slot list, the thread map, held APCs and messages, condition generations
    std::vector<Slot *> mSlots;
    std::unordered_map<DWORD, Slot *> mById;
    std::unordered_map<void *, uint64_t> mGenerations; // condition variables: WakeAll count
    Slot *mDriverSlot = nullptr;
    HANDLE mReply = nullptr; // auto-reset: a polled thread answered
    HANDLE mWake = nullptr;  // auto-reset: the driver should look (the token is free)
    HANDLE mStop = nullptr;  // manual-reset: the driver ends
    HANDLE mDriver = nullptr;
    HANDLE mWatchdog = nullptr;
    DWORD mWatchdogId = 0;
    std::atomic<uint32_t> mTicks{0}; // TickYields so far (the watchdog's no-tick rule)
    std::atomic<bool> mFree{false};
    std::atomic<Slot *> mRunner{nullptr}; // the thread holding the token (null: nobody, the driver picks)
    std::atomic<uint64_t> mProgress{0};   // hand-overs so far
    uint32_t mLastYielder = 0;
    Slot *mLastRan = nullptr;
    Stats mStats;
    uint32_t mStuckMs = 0;
    // The tick's yield: workers' waits that end before the next tick (mHorizon) end in deadline order; mCursor is how far
    // they got. Their clocks are then ahead of now (the main thread's frame time), which moves on at the next tick.
    bool mAtTick = false;
    uint64_t mHorizon = 0, mCursor = 0;
    uint64_t Reached() const
    {
        const uint64_t now = mClock.Now();
        return now > mCursor ? now : mCursor;
    }
};

} // namespace lockstep
