// Lockstep scheduling of the game's threads: the rules are in lockstep.h.
//
// The token: mRunner is the thread allowed to run game code. Only the token holder schedules (or the driver, a host
// thread, when nobody holds it). A thread that would wait records its condition in its slot, picks the next thread
// (Schedule) and parks on its own event (Park). A parked thread is woken with a command: run (the scheduler checked its
// condition), poll (check your condition yourself and answer; if it holds you take the token) or free.
#include "lockstep.h"

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <utility>

namespace lockstep {

namespace {

enum : LONG { kNone = 0, kPoll = 1, kRun = 2, kFree = 3 };
enum : LONG { kNoReply = 0, kIdle = 1, kRan = 2 };

enum class State : uint8_t { Running, Waiting, Dead };
enum class Kind : uint8_t { Start, Sleep, Yield, Objects, Section, Srw, Variable, Call };

const char *const kStateNames[] = {"running", "waiting", "dead"};
const char *const kKindNames[] = {"start", "sleep", "yield", "objects", "section", "srw", "variable", "call"};

// A call held for the thread that owns a window (OnWindowThread): run by it at its next PeekMessageA.
struct SentCall {
    uintptr_t (*call)(void *context);
    void *context;
    uintptr_t result = 0;
    volatile LONG done = 0;
};

// The real functions, by address from their DLLs: the host may point its own import of EnterCriticalSection at the
// scheduler, and the scheduler's real calls must not come back to it.
struct Real {
    decltype(&::CreateThread) CreateThread;
    decltype(&::SuspendThread) SuspendThread;
    decltype(&::ResumeThread) ResumeThread;
    decltype(&::ExitThread) ExitThread;
    decltype(&::FreeLibraryAndExitThread) FreeLibraryAndExitThread;
    decltype(&::SleepEx) SleepEx;
    decltype(&::WaitForSingleObject) WaitForSingleObject;
    decltype(&::WaitForMultipleObjects) WaitForMultipleObjects;
    decltype(&::EnterCriticalSection) EnterCriticalSection;
    decltype(&::AcquireSRWLockExclusive) AcquireSRWLockExclusive;
    decltype(&::SleepConditionVariableSRW) SleepConditionVariableSRW;
    decltype(&::WakeAllConditionVariable) WakeAllConditionVariable;
    decltype(&::QueueUserAPC) QueueUserAPC;
    decltype(&::SendMessageA) SendMessageA;
    decltype(&::PeekMessageA) PeekMessageA;
    decltype(&::PulseEvent) PulseEvent;
    LONG(NTAPI *NtQueryEvent)(HANDLE, ULONG, void *, ULONG, ULONG *);
};

template <typename T> void Bind(T &out, HMODULE module, const char *name)
{
    out = reinterpret_cast<T>(reinterpret_cast<void *>(GetProcAddress(module, name)));
}

const Real &R()
{
    static const Real real = [] {
        Real r{};
        HMODULE k = GetModuleHandleW(L"kernel32.dll");
        HMODULE u = LoadLibraryW(L"user32.dll");
        Bind(r.CreateThread, k, "CreateThread");
        Bind(r.SuspendThread, k, "SuspendThread");
        Bind(r.ResumeThread, k, "ResumeThread");
        Bind(r.ExitThread, k, "ExitThread");
        Bind(r.FreeLibraryAndExitThread, k, "FreeLibraryAndExitThread");
        Bind(r.SleepEx, k, "SleepEx");
        Bind(r.WaitForSingleObject, k, "WaitForSingleObject");
        Bind(r.WaitForMultipleObjects, k, "WaitForMultipleObjects");
        Bind(r.EnterCriticalSection, k, "EnterCriticalSection");
        Bind(r.AcquireSRWLockExclusive, k, "AcquireSRWLockExclusive");
        Bind(r.SleepConditionVariableSRW, k, "SleepConditionVariableSRW");
        Bind(r.WakeAllConditionVariable, k, "WakeAllConditionVariable");
        Bind(r.QueueUserAPC, k, "QueueUserAPC");
        Bind(r.SendMessageA, u, "SendMessageA");
        Bind(r.PeekMessageA, u, "PeekMessageA");
        Bind(r.PulseEvent, k, "PulseEvent");
        Bind(r.NtQueryEvent, GetModuleHandleW(L"ntdll.dll"), "NtQueryEvent");
        return r;
    }();
    return real;
}

thread_local Scheduler::Slot *tSlot = nullptr;
thread_local Scheduler *tOwner = nullptr;
thread_local int tInside = 0; // the scheduler's own code: its calls (the C runtime's locks) are real

struct Inside {
    Inside() { ++tInside; }
    ~Inside() { --tInside; }
};

// The calling thread's last error survives the scheduler's own calls.
struct KeepError {
    DWORD error = GetLastError();
    ~KeepError() { SetLastError(error); }
};

uint64_t Mix(uint64_t h, uint64_t v)
{
    return (h ^ v) * 0x100000001b3ull;
}

struct StartArgs {
    Scheduler *owner;
    Scheduler::Slot *slot;
    LPTHREAD_START_ROUTINE start;
    void *param;
};

} // namespace

struct Scheduler::Slot {
    uint32_t index = 0;
    DWORD id = 0;
    HANDLE thread = nullptr; // our own handle (query, APC delivery once free, joins in tests)
    HANDLE wake = nullptr;   // auto-reset
    volatile LONG command = kNone;
    volatile LONG reply = kNoReply;
    State state = State::Running;
    uint32_t suspends = 0;
    // The wait (written by the thread itself before it gives up the token).
    Kind kind = Kind::Start;
    bool alertable = false;
    uint64_t deadline = kNever;
    DWORD result = 0;
    DWORD count = 0;
    HANDLE objects[MAXIMUM_WAIT_OBJECTS] = {};
    bool all = false;
    CRITICAL_SECTION *section = nullptr;
    SRWLOCK *srw = nullptr;
    void *variable = nullptr;
    uint64_t generation = 0;
    bool woken = false, timedOut = false;
    SentCall *call = nullptr;
    // Held for the thread (under mLock).
    std::deque<std::pair<PAPCFUNC, ULONG_PTR>> apcs;
    std::deque<SentCall *> calls;
    DWORD pulsed = 0; // 1 + the index of the object a PulseEvent released this wait on (0: none)
    const void *routine = nullptr;
    std::string name;
    uint32_t yields = 0, yieldTick = ~0u;
    uint32_t runs = 0, waits = 0, timeouts = 0;
    // The last intercepted call (Called): import name and the caller's return address.
    const char *lastImport = nullptr;
    uintptr_t lastCaller = 0;
    uint32_t intercepted = 0;
};

Scheduler::Scheduler(frameclock::Clock &clock, ClockOf clockOf, LogFn log) : mClock(clock), mClockOf(clockOf), mLog(log)
{
    R();
}

Scheduler::~Scheduler()
{
    if (mStop) {
        bool live = false;
        for (Slot *s : mSlots)
            if (s->index && s->state != State::Dead) live = true;
        if (live) GoFree("the scheduler is destroyed");
        for (Slot *s : mSlots)
            if (s->index && s->thread) R().WaitForSingleObject(s->thread, 5000);
        SetEvent(mStop);
        R().WaitForSingleObject(mDriver, 5000);
        CloseHandle(mDriver);
        if (mWatchdog) {
            R().WaitForSingleObject(mWatchdog, 5000);
            CloseHandle(mWatchdog);
        }
        CloseHandle(mStop);
        CloseHandle(mWake);
        CloseHandle(mReply);
    }
    for (Slot *s : mSlots) {
        if (s->thread) CloseHandle(s->thread);
        if (s->wake) CloseHandle(s->wake);
        delete s;
    }
    delete mDriverSlot;
    if (tOwner == this) tOwner = nullptr, tSlot = nullptr;
}

Scheduler::Slot *Scheduler::NewSlot(const void *routine)
{
    auto *s = new Slot;
    s->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    s->routine = routine;
    ::AcquireSRWLockExclusive(&mLock); // the real one: the scheduler's own lock is never patched
    s->index = static_cast<uint32_t>(mSlots.size());
    mSlots.push_back(s);
    ReleaseSRWLockExclusive(&mLock);
    return s;
}

void Scheduler::Start(uint32_t stuckMs)
{
    Inside inside;
    mStuckMs = stuckMs;
    mReply = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    mWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    mStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Slot *main = NewSlot(nullptr);
    main->id = GetCurrentThreadId();
    main->thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION | THREAD_SET_CONTEXT | SYNCHRONIZE, FALSE, main->id);
    main->state = State::Running;
    main->name = "main";
    ::AcquireSRWLockExclusive(&mLock);
    mById[main->id] = main;
    ReleaseSRWLockExclusive(&mLock);
    mDriverSlot = new Slot;
    mDriverSlot->index = ~0u;
    tSlot = main;
    tOwner = this;
    mRunner = main;
    mLastRan = main;
    mDriver = R().CreateThread(nullptr, 0, &Driver, this, 0, &mDriverId);
    if (stuckMs) mWatchdog = R().CreateThread(nullptr, 0, &Watchdog, this, 0, &mWatchdogId);
}

void Scheduler::Called(const char *import, const void *caller)
{
    if (Slot *me = Self()) {
        me->lastImport = import;
        me->lastCaller = reinterpret_cast<uintptr_t>(caller);
        ++me->intercepted;
    }
}

Scheduler::Slot *Scheduler::Self()
{
    return tOwner == this ? tSlot : nullptr;
}

Scheduler::Slot *Scheduler::SlotOf(DWORD threadId)
{
    if (!threadId) return nullptr;
    AcquireSRWLockShared(&mLock);
    auto it = mById.find(threadId);
    Slot *s = it == mById.end() ? nullptr : it->second;
    ReleaseSRWLockShared(&mLock);
    return s;
}

bool Scheduler::Scheduled()
{
    return Self() && !mFree;
}

uint32_t Scheduler::ThreadIndex()
{
    Slot *me = Self();
    return me ? me->index : ~0u;
}

uint32_t Scheduler::IndexOf(DWORD threadId)
{
    const Slot *s = SlotOf(threadId);
    return s ? s->index : ~0u;
}

uint32_t Scheduler::Threads()
{
    AcquireSRWLockShared(&mLock);
    const auto n = static_cast<uint32_t>(mSlots.size());
    ReleaseSRWLockShared(&mLock);
    return n;
}

void Scheduler::Log(const char *format, ...)
{
    if (!mLog) return;
    Inside inside;
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    mLog(text);
}

// ---- conditions ----

bool Scheduler::Due(const Slot *s) const
{
    return s->deadline != kNever && Reached() >= s->deadline;
}

bool Scheduler::HasApcs(Slot *s)
{
    AcquireSRWLockShared(&mLock);
    const bool any = !s->apcs.empty();
    ReleaseSRWLockShared(&mLock);
    return any;
}

uint64_t Scheduler::Generation(void *variable)
{
    AcquireSRWLockShared(&mLock);
    auto it = mGenerations.find(variable);
    const uint64_t g = it == mGenerations.end() ? 0 : it->second;
    ReleaseSRWLockShared(&mLock);
    return g;
}

// A wait on a thread that has ended in the lockstep (it handed the token on) but not yet in Windows: it is on its way
// out, so wait for that here and the wait's result does not depend on how fast it gets there.
void Scheduler::JoinDead(Slot *me)
{
    for (DWORD i = 0; i < me->count; ++i) {
        const DWORD id = GetThreadId(me->objects[i]);
        if (!id) continue;
        Slot *s = SlotOf(id);
        if (s && s->state == State::Dead) R().WaitForSingleObject(me->objects[i], 5000);
    }
}

bool Scheduler::Evaluate(Slot *s)
{
    switch (s->kind) {
    case Kind::Start:
    case Kind::Yield:
        s->result = 0;
        return true;
    case Kind::Sleep:
        if (s->alertable && HasApcs(s)) {
            s->result = WAIT_IO_COMPLETION;
            return true;
        }
        s->result = 0;
        return Due(s);
    case Kind::Call:
        s->result = 0;
        return s->call->done != 0;
    case Kind::Objects: {
        if (s->alertable && HasApcs(s)) {
            s->result = WAIT_IO_COMPLETION;
            return true;
        }
        if (s->pulsed) { // released by a PulseEvent while it waited
            s->result = WAIT_OBJECT_0 + s->pulsed - 1;
            s->pulsed = 0;
            return true;
        }
        JoinDead(s);
        const DWORD r = s->count == 1 ? R().WaitForSingleObject(s->objects[0], 0)
                                      : R().WaitForMultipleObjects(s->count, s->objects, s->all, 0);
        if (r != WAIT_TIMEOUT) {
            s->result = r;
            return true;
        }
        if (Due(s)) {
            s->result = WAIT_TIMEOUT;
            ++s->timeouts;
            return true;
        }
        return false;
    }
    case Kind::Section:
        return TryEnterCriticalSection(s->section) != FALSE;
    case Kind::Srw:
        return TryAcquireSRWLockExclusive(s->srw) != FALSE;
    case Kind::Variable:
        if (!s->woken) {
            if (Generation(s->variable) != s->generation) {
                s->woken = true;
            } else if (Due(s)) {
                s->woken = s->timedOut = true;
                ++s->timeouts;
            }
        }
        if (!s->woken || !TryAcquireSRWLockExclusive(s->srw)) return false;
        s->result = s->timedOut ? FALSE : TRUE;
        return true;
    }
    return false;
}

// Conditions that take something are checked by the waiting thread itself.
static bool NeedsOwner(const Scheduler::Slot *s)
{
    return s->kind == Kind::Objects || s->kind == Kind::Section || s->kind == Kind::Srw || s->kind == Kind::Variable;
}

bool Scheduler::MainBlocked()
{
    Slot *main = mSlots[0];
    return main->state == State::Dead || (main->state == State::Waiting && main->kind != Kind::Yield);
}

uint64_t Scheduler::MinDeadline()
{
    const uint64_t now = Reached();
    uint64_t best = kNever;
    for (Slot *s : mSlots)
        if (s->state == State::Waiting && !s->suspends && s->deadline != kNever && s->deadline > now &&
            !(s->kind == Kind::Variable && s->woken) && s->deadline < best)
            best = s->deadline;
    return best;
}

// ---- hand-over ----

void Scheduler::Note(Slot *picked)
{
    ++mProgress;
    ++picked->runs;
    if (picked != mLastRan) ++mStats.handoffs;
    mLastRan = picked;
    mStats.sequence = Mix(mStats.sequence, static_cast<uint64_t>(picked->index) << 8 | static_cast<uint8_t>(picked->kind));
}

void Scheduler::Hand(Slot *s)
{
    Note(s);
    s->state = State::Running;
    mRunner = s;
    InterlockedCompareExchange(&s->command, kRun, kNone);
    SetEvent(s->wake);
}

bool Scheduler::Poll(Slot *s)
{
    ++mStats.polls;
    InterlockedExchange(&s->reply, kNoReply);
    if (InterlockedCompareExchange(&s->command, kPoll, kNone) != kNone) return false; // free
    SetEvent(s->wake);
    for (;;) {
        R().WaitForSingleObject(mReply, 100);
        const LONG reply = s->reply;
        if (reply != kNoReply) return reply == kRan; // kRan: s noted itself and holds the token
        if (mFree) return false;
    }
}

Scheduler::Outcome Scheduler::Park(Slot *me)
{
    Inside inside;
    for (;;) {
        R().WaitForSingleObject(me->wake, INFINITE);
        const LONG command = InterlockedExchange(&me->command, kNone);
        if (command == kFree) return Outcome::Free;
        if (command == kRun) return Outcome::Ran;
        if (command != kPoll) continue;
        const bool ok = Evaluate(me);
        if (ok) {
            Note(me); // here, before answering: once answered, the poller no longer touches the shared state
            me->state = State::Running;
            mRunner = me;
        }
        InterlockedExchange(&me->reply, ok ? kRan : kIdle);
        SetEvent(mReply);
        if (ok) return Outcome::Ran;
    }
}

Scheduler::Slot *Scheduler::PickYielder(Slot *self)
{
    (void)self;
    const uint32_t tick = mClock.Tick();
    const bool mainBlocked = MainBlocked();
    const auto n = static_cast<uint32_t>(mSlots.size());
    for (uint32_t k = 1; k <= n; ++k) {
        const uint32_t i = (mLastYielder + k) % n;
        Slot *s = mSlots[i];
        if (!i || s->state != State::Waiting || s->suspends || s->kind != Kind::Yield) continue;
        if (mainBlocked) {
            mClock.AdvanceTo(mClock.Now() + kCreep);
        } else {
            if (s->yieldTick != tick) s->yieldTick = tick, s->yields = 0;
            if (s->yields >= kYieldBudget) continue;
            ++s->yields;
        }
        mLastYielder = i;
        s->result = 0;
        return s;
    }
    return nullptr; // (a yielding main thread goes last: Schedule)
}

Scheduler::Outcome Scheduler::Schedule(Slot *self)
{
    Inside inside;
    bool crept = false;
    for (;;) {
        if (mFree) return Outcome::Free;
        AcquireSRWLockShared(&mLock);
        const size_t n = mSlots.size(); // only the token holder adds threads
        ReleaseSRWLockShared(&mLock);
        for (size_t i = 0; i < n; ++i) {
            Slot *s = mSlots[i];
            if (s->state != State::Waiting || s->suspends || s->kind == Kind::Yield) continue;
            if (s == self) {
                if (Evaluate(self)) {
                    Note(self);
                    return Outcome::Ran;
                }
            } else if (NeedsOwner(s)) {
                if (Poll(s)) return Outcome::Handed;
                if (mFree) return Outcome::Free;
            } else if (Evaluate(s)) {
                Hand(s);
                return Outcome::Handed;
            }
        }
        if (Slot *y = PickYielder(self)) {
            if (y == self) {
                Note(self);
                return Outcome::Ran;
            }
            Hand(y);
            return Outcome::Handed;
        }
        if (MainBlocked()) {
            const uint64_t deadline = MinDeadline();
            if (deadline != kNever) {
                mClock.AdvanceTo(deadline);
                ++mStats.advances;
                continue;
            }
        } else if (mAtTick) {
            // The tick's yield: the workers' sleeps and time-outs that end before the next tick do, in order.
            const uint64_t deadline = MinDeadline();
            if (deadline < mHorizon) {
                mCursor = deadline;
                ++mStats.advances;
                continue;
            }
        }
        Slot *main = mSlots[0];
        if (main == self && !mAtTick && !crept && main->kind == Kind::Yield) {
            // The main thread's Sleep(0) outside the tick's yield, and no worker can run: time moves on by kCreep, as a
            // real Sleep(0) takes some, so a main thread that spins on Sleep(0) for a sleeping worker (the sound timer's
            // stop, 0xe9780) gets it. Once per Sleep(0).
            const uint64_t deadline = MinDeadline();
            if (deadline != kNever) {
                const uint64_t to = Reached() + kCreep;
                mClock.AdvanceTo(to < deadline ? to : deadline);
                ++mStats.advances;
                crept = true;
                continue;
            }
        }
        if (main->state == State::Waiting && !main->suspends && main->kind == Kind::Yield) {
            main->result = 0;
            if (main == self) {
                Note(self);
                return Outcome::Ran;
            }
            Hand(main);
            return Outcome::Handed;
        }
        if (self->state != State::Waiting) {
            mRunner = nullptr;
            return Outcome::Untoken;
        }
        ++mStats.idle;
        R().SleepEx(1, FALSE);
    }
}

// The calling thread's wait, described in its slot. True: the condition held (result in the slot); false: lockstep is
// free, make the real call.
bool Scheduler::Block(Slot *me)
{
    ++me->waits;
    if (me->kind != Kind::Yield) {
        Inside inside;
        if (Evaluate(me)) return true;
    }
    me->state = State::Waiting;
    Outcome o = Schedule(me);
    if (o == Outcome::Handed) o = Park(me); // (Untoken is only for a thread that isn't waiting)
    me->state = State::Running;
    return o == Outcome::Ran;
}

// The calling thread ends: it hands the token to the driver and leaves the scheduler, so what runs on it from here on
// (fiber-local and TLS destructors, such as the C runtime's, which take its locks) gets the real functions.
void Scheduler::Finish(Slot *me)
{
    Inside inside;
    me->state = State::Dead;
    if (Self() == me) tOwner = nullptr, tSlot = nullptr;
    Slot *expected = me;
    mRunner.compare_exchange_strong(expected, nullptr);
    SetEvent(mWake);
}

// The token holder's thread ended by a way out the scheduler doesn't intercept (it would otherwise hold the token for
// ever): it is dead from here on and the driver picks the next thread. True if it had.
bool Scheduler::Ended(Slot *runner)
{
    if (!runner || runner == mDriverSlot || !runner->thread || R().WaitForSingleObject(runner->thread, 0) != WAIT_OBJECT_0)
        return false;
    ++mStats.unseenExits;
    Log("lockstep: thread %u (%s) ended without the scheduler seeing it; last %s from %s", runner->index,
        runner->name.c_str(), runner->lastImport ? runner->lastImport : "-",
        threadstack::Where(runner->lastCaller, mAddressNamer).c_str());
    runner->state = State::Dead;
    Slot *expected = runner;
    mRunner.compare_exchange_strong(expected, nullptr);
    return true;
}

void Scheduler::RunApcs(Slot *me)
{
    for (;;) {
        ::AcquireSRWLockExclusive(&mLock);
        if (me->apcs.empty()) {
            ReleaseSRWLockExclusive(&mLock);
            return;
        }
        const auto apc = me->apcs.front();
        me->apcs.pop_front();
        ReleaseSRWLockExclusive(&mLock);
        apc.first(apc.second);
    }
}

void Scheduler::RunCalls(Slot *me)
{
    for (;;) {
        ::AcquireSRWLockExclusive(&mLock);
        if (me->calls.empty()) {
            ReleaseSRWLockExclusive(&mLock);
            return;
        }
        SentCall *c = me->calls.front();
        me->calls.pop_front();
        ReleaseSRWLockExclusive(&mLock);
        c->result = c->call(c->context); // on the window's own thread: a direct call
        InterlockedExchange(&c->done, 1);
    }
}

// ---- the driver ----

DWORD WINAPI Scheduler::Driver(void *arg)
{
    static_cast<Scheduler *>(arg)->Drive();
    return 0;
}

// Picks the next thread when nobody holds the token (a thread ended, or ended unseen).
void Scheduler::Drive()
{
    Inside inside;
    HANDLE handles[] = {mStop, mWake};
    for (;;) {
        if (R().WaitForMultipleObjects(2, handles, FALSE, 1) == WAIT_OBJECT_0) return;
        if (mFree) continue;
        Slot *expected = nullptr;
        if (mRunner.compare_exchange_strong(expected, mDriverSlot)) {
            const Outcome o = Schedule(mDriverSlot);
            (void)o; // Handed: the picked thread holds the token; Untoken: nobody can run yet
            continue;
        }
        Ended(mRunner);
    }
}

DWORD WINAPI Scheduler::Watchdog(void *arg)
{
    static_cast<Scheduler *>(arg)->Watch();
    return 0;
}

void Scheduler::LogReport(const char *prefix, const std::string &report)
{
    if (!mLog) return;
    size_t at = 0;
    while (at < report.size()) {
        const size_t end = report.find('\n', at);
        mLog((prefix + report.substr(at, end == std::string::npos ? end : end - at)).c_str());
        if (end == std::string::npos) break;
        at = end + 1;
    }
}

// A host thread of its own, so that nothing the scheduler (the driver included) blocks in can keep it from looking.
// Once a second it checks two rules:
//   - stuck: the same token holder, no hand-over, under 1% of a 3 GHz processor second, others waiting, for mStuckMs:
//     the holder waits in something the scheduler doesn't see;
//   - no tick: no TickYield for kNoTickReport x mStuckMs (a report, once) or kNoTickFree x mStuckMs: the main thread
//     spins or something blocks that the first rule can't see (a busy holder, a blocked driver).
// Either logs the report (Report), and lockstep ends.
void Scheduler::Watch()
{
    Inside inside;
    Slot *watched = nullptr;
    uint64_t progress = 0, cycles = 0, ticks = mTicks;
    uint32_t stuck = 0;
    ULONGLONG lastTick = GetTickCount64();
    bool reported = false;
    while (R().WaitForSingleObject(mStop, 1000) == WAIT_TIMEOUT) {
        if (mFree) continue;
        const ULONGLONG now = GetTickCount64();
        if (mTicks != ticks) ticks = mTicks, lastTick = now, reported = false;
        const ULONGLONG noTick = now - lastTick;
        if (noTick >= static_cast<ULONGLONG>(kNoTickReport) * mStuckMs && !reported) {
            reported = true;
            Log("lockstep: no tick for %llu s (tick %u)", noTick / 1000, mClock.Tick());
            LogReport("lockstep: no tick: ", Report());
        }
        if (noTick >= static_cast<ULONGLONG>(kNoTickFree) * mStuckMs) {
            char why[96];
            std::snprintf(why, sizeof why, "no tick for %llu s", noTick / 1000);
            const std::string report = Report();
            GoFree(why);
            LogReport("lockstep: stuck: ", report);
            continue;
        }
        Slot *runner = mRunner;
        ULONG64 c = 0;
        if (runner && runner->thread) QueryThreadCycleTime(runner->thread, &c);
        bool waiting = false;
        AcquireSRWLockShared(&mLock);
        for (const Slot *s : mSlots)
            if (s != runner && s->state == State::Waiting) waiting = true;
        ReleaseSRWLockShared(&mLock);
        if (runner == watched && mProgress == progress && c - cycles < 30'000'000 && waiting) {
            if (++stuck * 1000 >= mStuckMs) {
                char why[160];
                std::snprintf(why, sizeof why, "thread %u (%s) made no progress for %u s while others waited",
                              runner ? runner->index : ~0u, runner ? kKindNames[static_cast<int>(runner->kind)] : "-",
                              stuck);
                const std::string report = Report(); // where everyone is, before they move on
                GoFree(why);
                LogReport("lockstep: stuck: ", report);
            }
        } else {
            stuck = 0;
        }
        watched = runner;
        progress = mProgress;
        cycles = c;
    }
}

void Scheduler::GoFree(const char *why)
{
    if (mFree.exchange(true)) return;
    Log("lockstep: free from here on: %s", why);
    ::AcquireSRWLockExclusive(&mLock);
    for (Slot *s : mSlots) {
        // Held APCs go to Windows, to run at the thread's next real alertable wait.
        for (const auto &apc : s->apcs)
            if (s->thread) R().QueueUserAPC(apc.first, s->thread, apc.second);
        s->apcs.clear();
        InterlockedExchange(&s->command, kFree);
        SetEvent(s->wake);
    }
    ReleaseSRWLockExclusive(&mLock);
    SetEvent(mReply);
}

// ---- threads ----

DWORD WINAPI Scheduler::Trampoline(void *arg)
{
    const StartArgs a = *static_cast<StartArgs *>(arg);
    delete static_cast<StartArgs *>(arg);
    tOwner = a.owner;
    tSlot = a.slot;
    // Waiting (Kind::Start) since the creator made the slot: the first hand-over may come before this thread gets here,
    // and the event keeps it.
    if (a.owner->Park(a.slot) == Outcome::Free) a.slot->state = State::Running;
    const DWORD code = a.start(a.param);
    a.owner->Finish(a.slot);
    return code;
}

HANDLE Scheduler::CreateThread(LPSECURITY_ATTRIBUTES security, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID param,
                               DWORD flags, LPDWORD id)
{
    Slot *me = Self();
    if (!me || mFree || tInside) return R().CreateThread(security, stack, start, param, flags, id);
    Inside inside;
    Slot *s = NewSlot(reinterpret_cast<const void *>(start));
    s->name = mThreadNamer ? mThreadNamer(reinterpret_cast<const void *>(start), param)
                           : threadstack::Where(reinterpret_cast<uintptr_t>(start), mAddressNamer);
    s->state = State::Waiting;
    s->kind = Kind::Start;
    s->suspends = (flags & CREATE_SUSPENDED) ? 1 : 0;
    auto *args = new StartArgs{this, s, start, param};
    DWORD tid = 0;
    // Always started: it parks at once, and our suspend count holds it.
    HANDLE h = R().CreateThread(security, stack, &Trampoline, args, flags & ~static_cast<DWORD>(CREATE_SUSPENDED), &tid);
    if (!h) {
        const KeepError keep;
        s->state = State::Dead;
        delete args;
        return nullptr;
    }
    s->id = tid;
    s->thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION | THREAD_SET_CONTEXT | SYNCHRONIZE, FALSE, tid);
    ::AcquireSRWLockExclusive(&mLock);
    mById[tid] = s;
    ReleaseSRWLockExclusive(&mLock);
    ++mStats.threads;
    if (id) *id = tid;
    return h;
}

DWORD Scheduler::SuspendThread(HANDLE thread)
{
    Slot *s = mFree ? nullptr : SlotOf(GetThreadId(thread));
    if (!s || s == Self() || s->state == State::Running) return R().SuspendThread(thread);
    return s->suspends++;
}

DWORD Scheduler::ResumeThread(HANDLE thread)
{
    Slot *s = mFree ? nullptr : SlotOf(GetThreadId(thread));
    if (!s || s->state == State::Running) return R().ResumeThread(thread);
    const DWORD previous = s->suspends;
    if (previous) --s->suspends;
    return previous;
}

void Scheduler::ExitThread(DWORD code)
{
    if (Slot *me = Self()) Finish(me);
    R().ExitThread(code);
    for (;;) {
    }
}

// The C runtime's _endthreadex ends a thread with this when the thread's procedure is in a module it could pin (the
// image: common_end_thread 0x83a815).
void Scheduler::FreeLibraryAndExitThread(HMODULE module, DWORD code)
{
    if (Slot *me = Self()) Finish(me);
    R().FreeLibraryAndExitThread(module, code);
    for (;;) {
    }
}

// ---- waits ----

void Scheduler::Sleep(DWORD ms)
{
    SleepEx(ms, FALSE);
}

DWORD Scheduler::SleepEx(DWORD ms, BOOL alertable)
{
    Slot *me = Self();
    if (!me || mFree || tInside) return R().SleepEx(ms, alertable);
    {
        const KeepError keep;
        frameclock::ThreadClock &clock = mClockOf();
        me->alertable = alertable != FALSE;
        if (me->alertable && HasApcs(me)) {
            RunApcs(me);
            return WAIT_IO_COMPLETION;
        }
        me->kind = ms ? Kind::Sleep : Kind::Yield;
        me->deadline = ms && ms != INFINITE ? mClock.ThreadNow(clock) + frameclock::FromMilliseconds(ms) : kNever;
        if (Block(me)) {
            if (me->result == WAIT_IO_COMPLETION) {
                RunApcs(me);
                return WAIT_IO_COMPLETION;
            }
            if (me->deadline != kNever) mClock.Slept(clock, me->deadline);
            return 0;
        }
    }
    return R().SleepEx(ms, alertable);
}

DWORD Scheduler::WaitForSingleObject(HANDLE object, DWORD ms)
{
    return WaitForMultipleObjects(1, &object, TRUE, ms);
}

DWORD Scheduler::WaitForMultipleObjects(DWORD count, const HANDLE *objects, BOOL all, DWORD ms)
{
    Slot *me = Self();
    if (!me || mFree || tInside || !ms || !count || count > MAXIMUM_WAIT_OBJECTS)
        return count == 1 ? R().WaitForSingleObject(objects[0], ms) : R().WaitForMultipleObjects(count, objects, all, ms);
    {
        KeepError keep;
        frameclock::ThreadClock &clock = mClockOf();
        me->kind = Kind::Objects;
        me->alertable = false;
        me->pulsed = 0;
        me->count = count;
        for (DWORD i = 0; i < count; ++i) me->objects[i] = objects[i];
        me->all = all != FALSE;
        me->deadline = ms != INFINITE ? mClock.ThreadNow(clock) + frameclock::FromMilliseconds(ms) : kNever;
        if (Block(me)) {
            if (me->result == WAIT_TIMEOUT) mClock.Slept(clock, me->deadline);
            if (me->result == WAIT_FAILED) keep.error = GetLastError();
            return me->result;
        }
    }
    return count == 1 ? R().WaitForSingleObject(objects[0], ms) : R().WaitForMultipleObjects(count, objects, all, ms);
}

void Scheduler::EnterCriticalSection(CRITICAL_SECTION *section)
{
    Slot *me = Self();
    if (!me || mFree || tInside || TryEnterCriticalSection(section)) {
        if (!me || mFree || tInside) R().EnterCriticalSection(section);
        return;
    }
    {
        const KeepError keep;
        // Held by a thread outside the lockstep: it lets go by itself, so wait for it here (holding the token).
        Slot *owner = SlotOf(static_cast<DWORD>(reinterpret_cast<uintptr_t>(section->OwningThread)));
        if (owner && owner != me) {
            me->kind = Kind::Section;
            me->section = section;
            me->alertable = false;
            me->deadline = kNever;
            if (Block(me)) return;
        }
    }
    R().EnterCriticalSection(section);
}

void Scheduler::AcquireSRWLockExclusive(SRWLOCK *lock)
{
    Slot *me = Self();
    if (me && !mFree && !tInside) {
        if (TryAcquireSRWLockExclusive(lock)) return;
        const KeepError keep;
        me->kind = Kind::Srw;
        me->srw = lock;
        me->alertable = false;
        me->deadline = kNever;
        if (Block(me)) return;
    }
    R().AcquireSRWLockExclusive(lock);
}

BOOL Scheduler::SleepConditionVariableSRW(CONDITION_VARIABLE *variable, SRWLOCK *lock, DWORD ms, ULONG flags)
{
    Slot *me = Self();
    if (!me || mFree || tInside || (flags & CONDITION_VARIABLE_LOCKMODE_SHARED) || !ms)
        return R().SleepConditionVariableSRW(variable, lock, ms, flags);
    frameclock::ThreadClock &clock = mClockOf();
    {
        const KeepError keep;
        me->kind = Kind::Variable;
        me->variable = variable;
        me->srw = lock;
        me->generation = Generation(variable);
        me->woken = me->timedOut = false;
        me->alertable = false;
        me->deadline = ms != INFINITE ? mClock.ThreadNow(clock) + frameclock::FromMilliseconds(ms) : kNever;
    }
    ReleaseSRWLockExclusive(lock);
    bool ok;
    {
        const KeepError keep;
        ok = Block(me);
    }
    if (!ok) {
        // Free: take the lock back; a condition variable may always wake without a cause.
        R().AcquireSRWLockExclusive(lock);
        return TRUE;
    }
    if (me->result == FALSE) {
        mClock.Slept(clock, me->deadline);
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    return TRUE;
}

void Scheduler::WakeAllConditionVariable(CONDITION_VARIABLE *variable)
{
    ::AcquireSRWLockExclusive(&mLock);
    ++mGenerations[variable];
    ReleaseSRWLockExclusive(&mLock);
    R().WakeAllConditionVariable(variable);
}

DWORD Scheduler::QueueUserAPC(PAPCFUNC function, HANDLE thread, ULONG_PTR data)
{
    Slot *s = mFree ? nullptr : SlotOf(GetThreadId(thread));
    if (!s || s->state == State::Dead) return R().QueueUserAPC(function, thread, data);
    ::AcquireSRWLockExclusive(&mLock);
    const bool held = !mFree;
    if (held) {
        s->apcs.emplace_back(function, data);
        ++mStats.apcs;
    }
    ReleaseSRWLockExclusive(&mLock);
    return held ? 1 : R().QueueUserAPC(function, thread, data);
}

LRESULT Scheduler::SendMessageA(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    struct Message {
        HWND window;
        UINT message;
        WPARAM wparam;
        LPARAM lparam;
    } m{window, message, wparam, lparam};
    return static_cast<LRESULT>(OnWindowThread(
        window,
        [](void *context) -> uintptr_t {
            const auto *a = static_cast<Message *>(context);
            return static_cast<uintptr_t>(R().SendMessageA(a->window, a->message, a->wparam, a->lparam));
        },
        &m));
}

uintptr_t Scheduler::OnWindowThread(HWND window, uintptr_t (*function)(void *context), void *context)
{
    Slot *me = Self();
    if (!me || mFree || tInside) return function(context);
    const DWORD target = GetWindowThreadProcessId(window, nullptr);
    if (target == me->id) return function(context);
    Slot *t = SlotOf(target);
    if (!t || t->state == State::Dead) {
        ++mStats.outside;
        return function(context);
    }
    SentCall call{function, context};
    ::AcquireSRWLockExclusive(&mLock);
    t->calls.push_back(&call);
    ++mStats.calls;
    ReleaseSRWLockExclusive(&mLock);
    me->kind = Kind::Call;
    me->call = &call;
    me->alertable = false;
    me->deadline = kNever;
    if (Block(me)) return call.result;
    // Free: take the message back if the receiver hasn't, else wait until it has answered.
    bool mine = false;
    ::AcquireSRWLockExclusive(&mLock);
    for (auto it = t->calls.begin(); it != t->calls.end(); ++it)
        if (*it == &call) {
            t->calls.erase(it);
            mine = true;
            break;
        }
    ReleaseSRWLockExclusive(&mLock);
    if (mine) return function(context);
    while (!call.done) R().SleepEx(1, FALSE);
    return call.result;
}

BOOL Scheduler::PeekMessageA(MSG *message, HWND window, UINT first, UINT last, UINT remove)
{
    if (Slot *me = Self()) RunCalls(me); // Windows delivers sent messages here too, ahead of posted ones
    return R().PeekMessageA(message, window, first, last, remove);
}

// An auto-reset ("synchronization") event wakes one waiter per pulse; a manual-reset one wakes them all.
static bool AutoReset(HANDLE event)
{
    struct {
        int type; // EVENT_TYPE: 0 notification (manual reset), 1 synchronization (auto reset)
        LONG state;
    } info{};
    return R().NtQueryEvent && R().NtQueryEvent(event, 0 /* EventBasicInformation */, &info, sizeof info, nullptr) >= 0 &&
           info.type == 1;
}

BOOL Scheduler::PulseEvent(HANDLE event)
{
    Slot *me = Self();
    if (me && !mFree && !tInside) {
        Inside inside;
        const bool one = AutoReset(event);
        // The token holder runs: every other game thread is parked, its wait written in its slot. The first waiters in
        // creation order are the ones released (Windows: the longest waiting; any fixed order is deterministic).
        ::AcquireSRWLockExclusive(&mLock);
        for (Slot *s : mSlots) {
            // A wait for all of several objects is left alone (Windows releases it only if the others are signalled at
            // that moment; the game makes no such waits).
            if (s == me || s->state != State::Waiting || s->kind != Kind::Objects || (s->all && s->count > 1) || s->pulsed)
                continue;
            for (DWORD i = 0; i < s->count; ++i)
                if (s->objects[i] == event) {
                    s->pulsed = i + 1;
                    ++mStats.pulses;
                    break;
                }
            if (s->pulsed && one) break;
        }
        ReleaseSRWLockExclusive(&mLock);
    }
    return R().PulseEvent(event); // threads outside the lockstep that really wait on it
}

void Scheduler::TickYield()
{
    Slot *me = Self();
    if (!me || mFree || tInside) return;
    const KeepError keep;
    ++mTicks;
    me->kind = Kind::Yield;
    me->alertable = false;
    me->deadline = kNever;
    mHorizon = mClock.TickStart() + mClock.FrameUnits();
    mAtTick = true;
    Block(me);
    mAtTick = false;
}

// ---- reports ----

Stats Scheduler::TakeStats()
{
    const Stats s = mStats;
    mStats = Stats{};
    return s;
}

std::string Scheduler::Census()
{
    std::string out;
    char line[400];
    AcquireSRWLockShared(&mLock);
    for (Slot *s : mSlots) {
        std::snprintf(line, sizeof line, "thread %u (id %lu, %s): %s %s, %u runs, %u waits, %u time-outs%s", s->index, s->id,
                      s->name.c_str(), kStateNames[static_cast<int>(s->state)], kKindNames[static_cast<int>(s->kind)], s->runs,
                      s->waits, s->timeouts, s->suspends ? ", suspended" : "");
        out += line;
        if (s->lastImport) {
            std::snprintf(line, sizeof line, ", %u calls, last %s from ", s->intercepted, s->lastImport);
            out += line + threadstack::Where(s->lastCaller, mAddressNamer);
        }
        out += '\n';
    }
    ReleaseSRWLockShared(&mLock);
    return out;
}

std::string Scheduler::Report()
{
    Inside inside;
    char line[200];
    const Slot *runner = mRunner;
    std::snprintf(line, sizeof line, "token %s %u, %llu hand-overs, %u ticks, tick %u, now %llu, cursor %llu, %s\n",
                  runner == mDriverSlot ? "driver" : runner ? "thread" : "nobody", runner && runner != mDriverSlot ? runner->index : 0,
                  static_cast<unsigned long long>(mProgress.load()), mTicks.load(), mClock.Tick(),
                  static_cast<unsigned long long>(mClock.Now()), static_cast<unsigned long long>(mCursor),
                  mAtTick ? "at the tick's yield" : "in the frame");
    std::string out = line + Census();
    std::vector<DWORD> ours;
    AcquireSRWLockShared(&mLock);
    for (auto &entry : mById) ours.push_back(entry.first);
    std::vector<std::pair<uint32_t, DWORD>> live;
    for (Slot *s : mSlots)
        if (s->state != State::Dead && s->id) live.emplace_back(s->index, s->id);
    ReleaseSRWLockShared(&mLock);
    const DWORD self = GetCurrentThreadId();
    const DWORD access = THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION;
    for (const auto &[index, id] : live) {
        if (id == self) continue;
        HANDLE h = OpenThread(access, FALSE, id);
        std::snprintf(line, sizeof line, "thread %u stack: ", index);
        out += line + (h ? threadstack::Describe(threadstack::Capture(h), mAddressNamer) : std::string("not opened")) + '\n';
        if (h) CloseHandle(h);
    }
    // The driver: where it is when it should be picking (a poll that never gets an answer, a blocked condition check).
    if (mDriverId && mDriverId != self) {
        HANDLE h = OpenThread(access, FALSE, mDriverId);
        out += "driver stack: " + (h ? threadstack::Describe(threadstack::Capture(h), mAddressNamer) : std::string("not opened")) + '\n';
        if (h) CloseHandle(h);
    }
    for (DWORD id : threadstack::ProcessThreads()) {
        if (id == self || id == mDriverId || id == mWatchdogId) continue;
        bool known = false;
        for (DWORD o : ours) known = known || o == id;
        if (known) continue;
        HANDLE h = OpenThread(access, FALSE, id);
        if (!h) continue;
        std::snprintf(line, sizeof line, "outside thread id %lu (start ", id);
        out += line + threadstack::Where(threadstack::StartAddress(h), mAddressNamer) + "): " +
               threadstack::Describe(threadstack::Capture(h), mAddressNamer) + '\n';
        CloseHandle(h);
    }
    return out;
}

} // namespace lockstep
