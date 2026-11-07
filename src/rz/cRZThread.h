#pragma once
#include "rz/cRZCriticalSection.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// The thread classes (RVAs 1.18.0.187). cRZThread is a value member of other groups' classes (cRZCallbackThread,
// nGZLog's asynchronous output, the speculative loader, cTSPersistSystem's background thread): they build it with
// Construct() over their own memory, so it keeps the game's vtable and its slots are hooked at their RVAs (the hook
// shape; docs/subsystems/foundations.md, "F1").

// A cRZThread's shared state (0x3c bytes, reference counted under its own lock).
struct cRZThreadState {
    static constexpr uint32_t kPriorityUnset = 0x12345678;

    cRZThreadState *Construct();   // 0x1b9aec
    uint32_t AddRef();             // 0x1b9b9e: under the lock; returns the new count

    uint32_t mField0;              // 0x00 the exit code (Exit, the thread routines' end)
    uint32_t mField4;              // 0x04 the thread handle (_beginthreadex), 0 when none
    uint32_t mThreadId;            // 0x08 0 until the thread is created; cleared when it exits
    uint32_t mMagic;               // 0x0c the priority level set before the thread exists (kPriorityUnset: none)
    cRZString mName;               // 0x10
    uint32_t mRefCount;            // 0x30
    cRZCriticalSection mLock;      // 0x34
};
static_assert(sizeof(cRZThreadState) == 0x3c, "cRZThreadState size");
static_assert(offsetof(cRZThreadState, mName) == 0x10 && offsetof(cRZThreadState, mLock) == 0x34, "cRZThreadState layout");

class cRZThread;
// The blocks the two Start slots hand their thread (operator new; the thread routine frees them first).
struct cRZThreadStartableArgs { // 12 bytes, slot 1
    void *mStartable;           // cIGZThreadStartable: its slot 0 runs with the thread (this) as argument
    cRZThread *mThread;
    cRZThreadState *mState;
};
struct cRZThreadRoutineArgs {   // 16 bytes, slot 2
    uint32_t(__stdcall *mRoutine)(void *data);
    void *mData;
    cRZThread *mThread;
    cRZThreadState *mState;
};
static_assert(sizeof(cRZThreadStartableArgs) == 12 && sizeof(cRZThreadRoutineArgs) == 16, "thread argument blocks");

// The empty APC cRZThread::Wake queues (an alertable wait of the thread returns); the original's is the shared `ret 4`
// at 0x300a0. ULONG_PTR argument.
void __stdcall cRZThreadWakeApc(unsigned long data);

// The thread object (8 bytes): the vtable (17 slots, 0xda3168) and the shared state.
class cRZThread {
public:
    static constexpr uint32_t kVTable = 0xda3168;
    static constexpr int kSlotSetName = 12;
    static constexpr int kSlotIsRunning = 11;
    static constexpr int kSlotDeletingDestructor = 13;
    static constexpr uint32_t kThreadNameException = 0x406d1388;
    using Routine = uint32_t(__stdcall *)(void *data);

    cRZThread *Construct();                // 0x1b9b19: a new state block, AddRef'd

    // Slot 0, 0x1b9ebb: deletes the thread object through slot 13 (a null this does nothing); returns 0.
    static uint32_t __fastcall Release(cRZThread *self, void *edx);
    // Slot 1, 0x1b9c32 (ret 4): when no thread runs (IsRunning first, which closes a finished one's handle), starts
    // one that runs startable's slot 0 with this; 1 if a thread was created, else 0.
    uint32_t Start(void *startable);
    // Slot 2, 0x1b9bc1 (ret 8): the same with routine(data).
    uint32_t Start(Routine routine, void *data);
    // Slot 3, 0x1b9c9c (ret 4): ends the calling thread with this exit code (ExitWith).
    void Exit(uint32_t exitCode);
    // Slots 4 / 5, 0x1b9fb1 / 0x1b9ec9: SuspendThread / ResumeThread on the thread when there is one; their result,
    // else (no thread) the state block's address, what eax held (ORIGINAL-BUG: garbage result without a thread).
    uint32_t Suspend();
    uint32_t Resume();
    // Slot 6, 0x1b9f77 (ret 4): SleepEx(microseconds rounded up to milliseconds, alertable).
    uint32_t Sleep(uint32_t microseconds);
    // Slot 7, 0x1b9ff1: wakes an alertable wait of the thread with an empty APC (0x300a0); QueueUserAPC's result,
    // else the state block's address (as Suspend).
    uint32_t Wake();
    // Slot 8, 0x1b9df1 (ret 4): waits for the thread to end (always INFINITE: the argument is ignored); 0 on the
    // thread itself or a failed wait, 2 on a timeout, else 1.
    uint32_t Join(uint32_t timeoutIgnored);
    // Slot 9, 0x1b9cd2: the priority level 0..6 (idle, lowest, below normal, normal, above normal, highest, time
    // critical) from GetThreadPriority, or the level set before the thread exists (3 when none).
    int32_t GetPriority();
    // Slot 10, 0x1b9edc (ret 4): SetThreadPriority with the level's Win32 priority (level 0..6 indexes a table;
    // ORIGINAL-BUG: other levels read past it, the original's stack), or remembers the level when no thread exists.
    bool SetPriority(int32_t level);
    // Slot 11, 0x1b9db2: 1 while the thread's exit code is STILL_ACTIVE; a finished thread's handle is closed and
    // cleared (0); no thread or a failed query: 0.
    uint32_t IsRunning();
    // Slot 13, 0x1b9b7f (ret 4): the destructor (0x1b9b71), then the 8 bytes freed when bit 0 of flags is set.
    cRZThread *DeletingDestructor(uint32_t flags);
    // Slot 14, 0x1b9cba (ret 4): *out = the exit code; true when the thread is not running (slot 11).
    bool GetExitCode(uint32_t *out);
    // Slot 15, 0x1b9d42 (ret 4): *out = the thread id; slot 11's result.
    uint32_t GetThreadId(uint32_t *out);
    // Slot 16, 0x1b9d30 (ret 4): *out = the thread handle; true.
    bool GetHandle(void **out);

    // 0x1b9f95 (cdecl): SleepEx(microseconds rounded up to milliseconds, alertable), the static form of slot 6.
    static uint32_t __cdecl SleepMicroseconds(uint32_t microseconds);
    // 0x1b9fd2 (cdecl): the end of a thread: the exit code stored, the thread id cleared, the state released, then
    // _endthreadex(exitCode).
    static void __cdecl ExitWith(cRZThreadState *state, uint32_t exitCode);
    // 0x1b9d82 / 0x1b9d56 (stdcall): the routines _beginthreadex runs for slots 1 and 2: free the block, run, ExitWith.
    static uint32_t __stdcall RunStartable(cRZThreadStartableArgs *args);
    static uint32_t __stdcall RunRoutine(cRZThreadRoutineArgs *args);
    // Slot 12, 0x1b9f30: sets the state's name (null clears it); with a thread id already assigned
    // it also announces the name to a debugger and returns true, else false.
    bool SetName(const char *name);
    // 0x1b9e26 (cdecl): the MSVC "SetThreadName" exception (0x406d1388, four arguments) inside a
    // handler that continues execution.
    static void __cdecl AnnounceThreadName(const char *name, uint32_t threadId);

    const void *mVTable;     // 0x00
    cRZThreadState *mState;  // 0x04
};
static_assert(sizeof(cRZThread) == 8, "cRZThread size");

// The callback thread (0x50 bytes): runs queued callbacks for cRZCallbackSystemService. vtables at
// +0 (cIGZCallbackThread, 9 slots, 0xe61bf4) and +4 (cIGZThreadStartable, 1 slot, 0xe61c1c).
class cRZCallbackThread {
public:
    static constexpr uint32_t kVTable = 0xe61bf4;
    static constexpr uint32_t kStartableVTable = 0xe61c1c;
    static constexpr uint32_t kNameRva = 0xe61c20; // the name string passed to SetName
    static constexpr uint32_t kSize = 0x50;

    cRZCallbackThread *Construct();        // 0x9cfa5d
    // 0x9cfac2 (lane HT, src/rz/cRZCallbackThreadDestroy.size.cpp): the destructor body. Both vtables stored; under
    // the lock (its slots 1 / 2, through its vtable) the running flag cleared, the queue (+0x10, a vector of 28-byte
    // entries) emptied, and when the thread runs (its slot 11) it is resumed (5), woken (7) and waited for (8, -1);
    // then the lock, the thread (0x1b9b71), the stopwatch (nothing) and the queue's buffer are destroyed.
    void Destruct();
    // 0x9cfb3e, slot 8: Destruct, then the 0x50 bytes freed when bit 0 of `flags` is set. Returns this.
    cRZCallbackThread *DeletingDestruct(uint32_t flags);

    // The slots (lane LO, src/rz/cRZCallbackThreadSlots.size.cpp). The lock and the thread are called through their
    // vtables (slots 1 / 2 of the lock; the thread's slots as named), as the original does.
    static constexpr uint32_t kIid = 0x276c77c8;     // cIGZCallbackThread
    using Callback = void(__cdecl *)(void *data);
    // A queued callback (28 bytes, the queue at +0x10 is a std::vector of them, in the order added).
    struct Entry {
        uint32_t mId;          // 0x00 from mField40, counting up
        Callback mCallback;    // 0x04 called with mData on the thread, outside the lock
        void *mData;           // 0x08
        uint8_t mOnce;         // 0x0c removed once it has run; else it repeats every mInterval
        uint8_t mPad0d[3];     //      never written (the original's stack bytes)
        uint32_t mInterval;    // 0x10 the delay, and a repeating entry's period (stopwatch units: milliseconds)
        uint32_t mDue;         // 0x14 the stopwatch time it is due at (0xffffffff: never, after a wrap)
        uint8_t mFixedRate;    // 0x18 a repeat is due mInterval after its last due time, else after the run
        uint8_t mPad19[3];     //      never written
    };
    // Slot 0, 0x9cfc6f (ret 8): IIDs 1 and kIid: *out = this, AddRef through slot 1, true; else false.
    bool QueryInterface(uint32_t iid, void **out);
    // Slot 1, 0x9cfc40: with mOwnedByCaller the count (mField0c) incremented and returned; else 1.
    uint32_t AddRef();
    // Slot 2, 0x9cfc93: with mOwnedByCaller the count decremented and returned while above 1, else slot 8 (flags 1)
    // and 0; without it, 1.
    uint32_t Release();
    // Slot 3, 0x9cfb5d (ret 0x14): queues callback(data) due `delay` from now and returns its id (0 when not running).
    // Under the lock: the id is mField40++; into an empty queue the due time saturates at 0xffffffff and the thread is
    // started when not running (slot 1 with the startable face), then resumed (5); a later entry's due time wraps.
    // Then the thread is woken (7).
    uint32_t AddCallback(Callback callback, void *data, uint32_t delay, bool once, bool fixedRate);
    // Slot 4, 0x9cfcb4 (ret 4): removes the entry with this id under the lock; whether one was (false when not running).
    bool RemoveCallback(uint32_t id);
    // Slots 5 / 6, 0x9cfd09 / 0x9cfc67: the thread's slots 10 / 9 (SetPriority, ret 4 / GetPriority).
    bool SetPriority(int32_t level);
    int32_t GetPriority();
    // Slot 7, 0x9cfc52: the thread's id (the thread's slot 15).
    uint32_t GetThreadId();
    // The startable face's slot 0, 0x9cfd11 (this = the +4 face, ret 4; the thread argument unused): the thread body.
    // While running: under the lock, collect every entry due by now (a copy, in queue order; once-entries removed,
    // repeating ones get their next due time) and the time to the next due entry; outside it, run the collected
    // callbacks; then sleep that long less the time spent (10 ms when that much has passed already) through 0x1b9f95.
    // An empty queue suspends the thread (slot 4) until AddCallback resumes it. Returns 0.
    static uint32_t __fastcall Run(void *startable, void *edx, void *thread);

    const void *mVTable;            // 0x00
    const void *mStartableVTable;   // 0x04
    uint8_t mRunning;               // 0x08 cleared, then set to 1 once the stopwatch has started; cleared to stop
    uint8_t mOwnedByCaller;         // 0x09 cleared (the service sets it, 0x4ea470): the count below is used
    uint8_t mPad0a[2];
    uint32_t mField0c;              // 0x0c the reference count (AddRef / Release, with mOwnedByCaller)
    uint32_t mField10;              // 0x10 the queue (std::vector<Entry>): first
    uint32_t mField14;              // 0x14 last
    uint32_t mField18;              // 0x18 end of storage
    uint32_t mField1c;              // 0x1c never written
    cRZStopwatch mStopwatch;        // 0x20 mode 4, started
    cRZThread mThread;              // 0x38
    uint32_t mField40;              // 0x40 set to 1: the next callback id
    cRZCriticalSection mLock;       // 0x44
    uint32_t mField4c;              // 0x4c never written
};
static_assert(sizeof(cRZCallbackThread) == cRZCallbackThread::kSize, "cRZCallbackThread size");
static_assert(sizeof(cRZCallbackThread::Entry) == 28 && offsetof(cRZCallbackThread::Entry, mDue) == 0x14 &&
                  offsetof(cRZCallbackThread::Entry, mFixedRate) == 0x18,
              "cRZCallbackThread entry layout");
static_assert(offsetof(cRZCallbackThread, mStopwatch) == 0x20 && offsetof(cRZCallbackThread, mThread) == 0x38 && offsetof(cRZCallbackThread, mLock) == 0x44, "cRZCallbackThread layout");
