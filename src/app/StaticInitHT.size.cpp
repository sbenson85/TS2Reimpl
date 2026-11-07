// Lane HT's size-profile bodies (src/app/StaticInitHT.h): the worker threads' shutdown (0x99936e) and the DbgHelp
// table's unload (0x1bd611), the exit thunks of XC slots 529 and 99.
#include "app/ReporterDump.h"
#include "app/StaticInitHT.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZThread.h"
#include "rz/nGZLog.h"

#include <time.h>
#include <windows.h>

namespace staticinit {

namespace {

constexpr int kThreadSlotResume = 5, kThreadSlotIsRunning = 11, kThreadSlotDelete = 13;
constexpr uint32_t kWorkerCount = 3;
constexpr uint32_t kIatFreeLibrary = 0xd9a120, kIatCloseHandle = 0xd9a17c;

// The image's import slots (as the original calls them).
template <typename Fn> Fn Import(uint32_t iatRva) { return *GameAddress<Fn>(iatRva); }

} // namespace

uint32_t __cdecl WorkerThreads::Shutdown(int32_t seconds)
{
    void **threads = GameAddress<void *>(kThreads);
    uint8_t *suspended = GameAddress<uint8_t>(kSuspended);
    uint8_t *busy = GameAddress<uint8_t>(kBusy);
    cRZCriticalSection *lock = GameAddress<cRZCriticalSection>(kLock);
    // Only the low dword of the time is kept for the deadline, as in the original.
    uint32_t deadline = 0xffffffff;
    if (seconds != -1) deadline = static_cast<uint32_t>(_time64(nullptr)) + static_cast<uint32_t>(seconds);
    uint32_t deleted = 0;
    busy[0] = busy[1] = busy[2] = 1;
    for (;;) {
        lock->Lock();
        uint32_t found = 0;
        for (uint32_t i = 0; i < kWorkerCount; ++i) {
            if (!threads[i]) continue;
            ++found;
            if (suspended[i]) {
                suspended[i] = 0;
                VirtualCall<uint32_t(__thiscall *)(void *), kThreadSlotResume>(threads[i]);
            }
            if (VirtualCall<uint32_t(__thiscall *)(void *), kThreadSlotIsRunning>(threads[i])) continue;
            if (threads[i]) VirtualCall<void *(__thiscall *)(void *, uint32_t), kThreadSlotDelete>(threads[i], 1u);
            ++deleted;
            threads[i] = nullptr;
        }
        cRZThread::SleepMicroseconds(kSleepMicroseconds);
        lock->Unlock();
        if (!found) break;
        const int64_t now = _time64(nullptr);
        const int32_t high = static_cast<int32_t>(static_cast<uint64_t>(now) >> 32);
        if (high > 0 || (high == 0 && static_cast<uint32_t>(now) > deadline)) break;
    }
    busy[0] = busy[1] = busy[2] = 0;
    return deleted;
}

} // namespace staticinit

namespace appbase {

uint8_t DbgHelpTable::Unload()
{
    uint8_t *flags = GameAddress<uint8_t>(kDoneFlag);
    if (!flags[0]) return 1;
    flags[0] = 0;
    flags[1] = 0;
    if (symCleanup) reinterpret_cast<BOOL(WINAPI *)(HANDLE)>(symCleanup)(process);
    for (void *module : {dbghelp, ntdll, kernel32, tlhelp32, psapi}) {
        if (module) staticinit::Import<BOOL(WINAPI *)(void *)>(staticinit::kIatFreeLibrary)(module);
    }
    staticinit::Import<BOOL(WINAPI *)(void *)>(staticinit::kIatCloseHandle)(process);
    process = nullptr;
    return 1;
}

} // namespace appbase
