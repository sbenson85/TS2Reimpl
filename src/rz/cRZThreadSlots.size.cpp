// cRZThread's slots and thread routines (lane LO, foundations F1; src/rz/cRZThread.h). Size profile, as the original's.
// Win32 calls go through the game's import slots (ImportFunction); _beginthreadex / _endthreadex and operator new /
// delete are library code (our CRT's).
#include "rz/cRZThread.h"
#include "rz/cRZThreadRelease.h"

#include "game/game.h"
#include "game/import.h"
#include "game/vtable.h"

#define NOMINMAX
#include <windows.h>
#include <malloc.h>
#include <process.h>

#include <new>

namespace {

constexpr uint32_t kIatSuspendThread = 0xd9a1a4, kIatResumeThread = 0xd9a1a8, kIatSleepEx = 0xd9a188,
                   kIatQueueUserAPC = 0xd9a18c, kIatGetCurrentThreadId = 0xd9a194, kIatWaitForSingleObject = 0xd9a184,
                   kIatGetThreadPriority = 0xd9a19c, kIatSetThreadPriority = 0xd9a198,
                   kIatGetExitCodeThread = 0xd9a1a0, kIatCloseHandle = 0xd9a17c;

HANDLE ThreadHandle(const cRZThreadState *state) { return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(state->mField4)); }

// The Win32 priority of each level (the original's table 0xda31b0).
constexpr int32_t kWin32Priority[7] = {THREAD_PRIORITY_IDLE, THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL,
                                       THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL, THREAD_PRIORITY_HIGHEST,
                                       THREAD_PRIORITY_TIME_CRITICAL};

uint32_t SleepEx(uint32_t microseconds)
{
    // (us + 999) / 1000 in 32 bits, as the original (the sum wraps for the largest values).
    const uint32_t milliseconds = (microseconds + 999u) / 1000u;
    return ImportFunction<decltype(&::SleepEx), kIatSleepEx>()(milliseconds, TRUE);
}

// Calls a slot-2 routine whatever its convention: the game hands the slot both __stdcall routines (the directory
// watcher's 0x9c1a90) and __cdecl ones (the speculative loader's 0x1f9860), and the original calls them as __stdcall
// (a cdecl routine leaves its argument on the stack, harmless there because _endthreadex never returns). The _alloca
// gives this function a frame whose epilogue restores esp from ebp, so either kind returns here cleanly.
__declspec(noinline) uint32_t CallRoutine(cRZThread::Routine routine, void *data)
{
    void *volatile anchor = _alloca(sizeof(void *));
    (void)anchor;
    return routine(data);
}

} // namespace

// The empty APC slot 7 queues (the original's is the shared `ret 4` at 0x300a0).
void __stdcall cRZThreadWakeApc(ULONG_PTR) {}

uint32_t __fastcall cRZThread::Release(cRZThread *self, void *)
{
    if (self) VirtualCall<cRZThread *(__thiscall *)(void *, uint32_t), kSlotDeletingDestructor>(self, 1);
    return 0;
}

uint32_t cRZThread::Start(void *startable)
{
    cRZThreadState *state = mState;
    VirtualCall<uint32_t(__thiscall *)(void *), kSlotIsRunning>(this);
    if (state->mField4 != 0) return 0;
    auto *args = static_cast<cRZThreadStartableArgs *>(::operator new(sizeof(cRZThreadStartableArgs)));
    args->mStartable = startable;
    args->mThread = this;
    args->mState = state;
    state->AddRef();
    state->mField4 = static_cast<uint32_t>(_beginthreadex(nullptr, 0, reinterpret_cast<_beginthreadex_proc_type>(&RunStartable), args, 0,
                                                          reinterpret_cast<unsigned *>(&state->mThreadId)));
    if (state->mField4 != 0) return 1;
    appdebugger::ReleaseThreadState(state, nullptr);
    ::operator delete(args);
    return 0;
}

uint32_t cRZThread::Start(Routine routine, void *data)
{
    cRZThreadState *state = mState;
    VirtualCall<uint32_t(__thiscall *)(void *), kSlotIsRunning>(this);
    if (state->mField4 != 0) return 0;
    auto *args = static_cast<cRZThreadRoutineArgs *>(::operator new(sizeof(cRZThreadRoutineArgs)));
    args->mRoutine = routine;
    args->mData = data;
    args->mThread = this;
    args->mState = state;
    state->AddRef();
    state->mField4 = static_cast<uint32_t>(_beginthreadex(nullptr, 0, reinterpret_cast<_beginthreadex_proc_type>(&RunRoutine), args, 0,
                                                          reinterpret_cast<unsigned *>(&state->mThreadId)));
    if (state->mField4 != 0) return 1;
    appdebugger::ReleaseThreadState(state, nullptr);
    ::operator delete(args);
    return 0;
}

void cRZThread::Exit(uint32_t exitCode) { ExitWith(mState, exitCode); }

uint32_t cRZThread::Suspend()
{
    cRZThreadState *state = mState;
    if (state->mField4 == 0) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(state));
    return ImportFunction<decltype(&::SuspendThread), kIatSuspendThread>()(ThreadHandle(state));
}

uint32_t cRZThread::Resume()
{
    cRZThreadState *state = mState;
    if (state->mField4 == 0) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(state));
    return ImportFunction<decltype(&::ResumeThread), kIatResumeThread>()(ThreadHandle(state));
}

uint32_t cRZThread::Sleep(uint32_t microseconds) { return SleepEx(microseconds); }

uint32_t cRZThread::Wake()
{
    cRZThreadState *state = mState;
    if (state->mField4 == 0) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(state));
    return ImportFunction<decltype(&::QueueUserAPC), kIatQueueUserAPC>()(&cRZThreadWakeApc, ThreadHandle(state), 0);
}

uint32_t cRZThread::Join(uint32_t)
{
    cRZThreadState *state = mState;
    if (ImportFunction<decltype(&::GetCurrentThreadId), kIatGetCurrentThreadId>()() == state->mThreadId) return 0;
    const DWORD result = ImportFunction<decltype(&::WaitForSingleObject), kIatWaitForSingleObject>()(ThreadHandle(state), INFINITE);
    if (result == WAIT_FAILED) return 0;
    return result == WAIT_TIMEOUT ? 2 : 1;
}

int32_t cRZThread::GetPriority()
{
    cRZThreadState *state = mState;
    if (state->mField4 == 0) return state->mMagic == cRZThreadState::kPriorityUnset ? 3 : static_cast<int32_t>(state->mMagic);
    switch (ImportFunction<decltype(&::GetThreadPriority), kIatGetThreadPriority>()(ThreadHandle(state))) {
    case THREAD_PRIORITY_IDLE: return 0;
    case THREAD_PRIORITY_LOWEST: return 1;
    case THREAD_PRIORITY_BELOW_NORMAL: return 2;
    case THREAD_PRIORITY_ABOVE_NORMAL: return 4;
    case THREAD_PRIORITY_HIGHEST: return 5;
    case THREAD_PRIORITY_TIME_CRITICAL: return 6;
    default: return 3; // normal, and anything else (THREAD_PRIORITY_ERROR_RETURN)
    }
}

bool cRZThread::SetPriority(int32_t level)
{
    cRZThreadState *state = mState;
    if (state->mField4 == 0) {
        state->mMagic = static_cast<uint32_t>(level);
        return true;
    }
    return ImportFunction<decltype(&::SetThreadPriority), kIatSetThreadPriority>()(ThreadHandle(state), kWin32Priority[level]) != 0;
}

uint32_t cRZThread::IsRunning()
{
    cRZThreadState *state = mState;
    if (state->mField4 == 0) return 0;
    DWORD exitCode;
    if (!ImportFunction<decltype(&::GetExitCodeThread), kIatGetExitCodeThread>()(ThreadHandle(state), &exitCode)) return 0;
    if (exitCode == STILL_ACTIVE) return 1;
    ImportFunction<decltype(&::CloseHandle), kIatCloseHandle>()(ThreadHandle(state));
    state->mField4 = 0;
    return 0;
}

cRZThread *cRZThread::DeletingDestructor(uint32_t flags)
{
    appdebugger::DestroyThread(this, nullptr);
    if (flags & 1) ::operator delete(this, sizeof(cRZThread));
    return this;
}

bool cRZThread::GetExitCode(uint32_t *out)
{
    *out = mState->mField0;
    return VirtualCall<uint32_t(__thiscall *)(void *), kSlotIsRunning>(this) == 0;
}

uint32_t cRZThread::GetThreadId(uint32_t *out)
{
    *out = mState->mThreadId;
    return VirtualCall<uint32_t(__thiscall *)(void *), kSlotIsRunning>(this);
}

bool cRZThread::GetHandle(void **out)
{
    *out = ThreadHandle(mState);
    return true;
}

uint32_t __cdecl cRZThread::SleepMicroseconds(uint32_t microseconds) { return SleepEx(microseconds); }

void __cdecl cRZThread::ExitWith(cRZThreadState *state, uint32_t exitCode)
{
    state->mThreadId = 0;
    state->mField0 = exitCode;
    appdebugger::ReleaseThreadState(state, nullptr);
    _endthreadex(exitCode);
}

uint32_t __stdcall cRZThread::RunStartable(cRZThreadStartableArgs *args)
{
    void *startable = args->mStartable;
    cRZThread *thread = args->mThread;
    cRZThreadState *state = args->mState;
    ::operator delete(args);
    const uint32_t exitCode = VirtualCall<uint32_t(__thiscall *)(void *, cRZThread *), 0>(startable, thread);
    ExitWith(state, exitCode);
    return exitCode;
}

uint32_t __stdcall cRZThread::RunRoutine(cRZThreadRoutineArgs *args)
{
    const Routine routine = args->mRoutine;
    void *data = args->mData;
    cRZThreadState *state = args->mState;
    ::operator delete(args);
    const uint32_t exitCode = CallRoutine(routine, data);
    ExitWith(state, exitCode);
    return exitCode;
}
