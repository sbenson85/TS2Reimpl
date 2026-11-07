// Clock census (diagnostic, test control like host/control.cpp; nothing here stands in for the game's
// code). The "clocks" control command replaces the game's import slots for the wall-clock and timer
// functions with wrappers that call the real function and count each call by (function, caller,
// main thread or not); later "clocks" commands log the counts that changed since the previous one.
// Run it on reference runs: our replacements call through the same slots, so their callers would be
// our code. rdtsc read inline is not seen here.
#include "control.h"

#include "game/game.h"
#include "hook/log.h"

#define NOMINMAX
#include <windows.h>
#include <intrin.h>

#include <cstdint>
#include <cstring>

#pragma intrinsic(_ReturnAddress, _AddressOfReturnAddress)

namespace {

enum Clock : uint32_t { kQpc, kTickCount, kTimeGetTime, kSystemTimeAsFileTime, kLocalTime, kClockCount };
const char *const kClockNames[kClockCount] = {"QueryPerformanceCounter", "GetTickCount", "timeGetTime",
                                              "GetSystemTimeAsFileTime", "GetLocalTime"};
// Import address table slots in the unpacked image.
constexpr uint32_t kIat[kClockCount] = {0xd9a24c, 0xd9a0fc, 0xd9a64c, 0xd9a2a4, 0xd9a1c8};

void *gReal[kClockCount];
DWORD gMainThread;
bool gInstalled;
SRWLOCK gLock = SRWLOCK_INIT;

struct Key {
    uint32_t clock, caller, user, worker;
};
constexpr uint32_t kMaxKeys = 2048;
Key gKeys[kMaxKeys];
uint32_t gCounts[kMaxKeys], gShown[kMaxKeys], gUsed;
volatile LONG gOverflow;

// Most game reads go through the stopwatch (cRZStopwatch, 0x1c9000..0x1c9a00), so the caller alone
// says little: the stack above is scanned for the first return address into game code outside the
// stopwatch, i.e. a game address just after a call instruction (a heuristic: a stale value on the
// stack can match).
constexpr uint32_t kStopwatchBegin = 0x1c9000, kStopwatchEnd = 0x1c9a00, kTextEnd = 0xd9a000;

bool AfterCall(const uint8_t *p)
{
    return p[-5] == 0xE8 || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) || (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) ||
           (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) || (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10);
}

uint32_t FindUser(const uint32_t *stack, uint32_t base)
{
    __try {
        for (int i = 0; i < 48; ++i) {
            uint32_t rva = stack[i] - base;
            if (rva < 0x1000 || rva >= kTextEnd || (rva >= kStopwatchBegin && rva < kStopwatchEnd)) continue;
            if (AfterCall(reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(stack[i])))) return rva;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0;
}

void Count(uint32_t clock, void *returnAddress, const uint32_t *stack)
{
    uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(0)));
    uint32_t ra = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(returnAddress));
    uint32_t caller = ra - base < 0x2000000 ? ra - base : ra;
    uint32_t user = caller >= kStopwatchBegin && caller < kStopwatchEnd ? FindUser(stack + 1, base) : 0;
    Key key{clock, caller, user, GetCurrentThreadId() != gMainThread};
    AcquireSRWLockExclusive(&gLock);
    uint32_t i = 0;
    while (i < gUsed && std::memcmp(&gKeys[i], &key, sizeof(key)) != 0) ++i;
    if (i == gUsed && gUsed < kMaxKeys) gKeys[gUsed++] = key;
    if (i < kMaxKeys) ++gCounts[i];
    else InterlockedIncrement(&gOverflow);
    ReleaseSRWLockExclusive(&gLock);
}

BOOL WINAPI CountedQpc(LARGE_INTEGER *out)
{
    Count(kQpc, _ReturnAddress(), static_cast<const uint32_t *>(_AddressOfReturnAddress()));
    return reinterpret_cast<BOOL(WINAPI *)(LARGE_INTEGER *)>(gReal[kQpc])(out);
}

DWORD WINAPI CountedTickCount()
{
    Count(kTickCount, _ReturnAddress(), static_cast<const uint32_t *>(_AddressOfReturnAddress()));
    return reinterpret_cast<DWORD(WINAPI *)()>(gReal[kTickCount])();
}

DWORD WINAPI CountedTimeGetTime()
{
    Count(kTimeGetTime, _ReturnAddress(), static_cast<const uint32_t *>(_AddressOfReturnAddress()));
    return reinterpret_cast<DWORD(WINAPI *)()>(gReal[kTimeGetTime])();
}

void WINAPI CountedSystemTimeAsFileTime(FILETIME *out)
{
    Count(kSystemTimeAsFileTime, _ReturnAddress(), static_cast<const uint32_t *>(_AddressOfReturnAddress()));
    reinterpret_cast<void(WINAPI *)(FILETIME *)>(gReal[kSystemTimeAsFileTime])(out);
}

void WINAPI CountedLocalTime(SYSTEMTIME *out)
{
    Count(kLocalTime, _ReturnAddress(), static_cast<const uint32_t *>(_AddressOfReturnAddress()));
    reinterpret_cast<void(WINAPI *)(SYSTEMTIME *)>(gReal[kLocalTime])(out);
}

void *const kWrappers[kClockCount] = {reinterpret_cast<void *>(&CountedQpc), reinterpret_cast<void *>(&CountedTickCount),
                                      reinterpret_cast<void *>(&CountedTimeGetTime),
                                      reinterpret_cast<void *>(&CountedSystemTimeAsFileTime),
                                      reinterpret_cast<void *>(&CountedLocalTime)};

void Install()
{
    gMainThread = GetCurrentThreadId();
    for (uint32_t i = 0; i < kClockCount; ++i) {
        auto *slot = GameAddress<void *>(kIat[i]);
        gReal[i] = *slot;
        DWORD old;
        VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old);
        InterlockedExchangePointer(slot, kWrappers[i]);
        VirtualProtect(slot, sizeof(void *), old, &old);
    }
    gInstalled = true;
    Log("clocks: counting calls to %u clock functions", static_cast<unsigned>(kClockCount));
}

} // namespace

void LogClocks()
{
    if (!gInstalled) {
        Install();
        return;
    }
    AcquireSRWLockExclusive(&gLock);
    uint32_t changed = 0;
    for (uint32_t i = 0; i < gUsed; ++i) {
        if (gCounts[i] == gShown[i]) continue;
        const Key &k = gKeys[i];
        Log("clocks: %s from 0x%x (user 0x%x)%s: %u (+%u)", kClockNames[k.clock], k.caller, k.user,
            k.worker ? " (worker)" : "", gCounts[i], gCounts[i] - gShown[i]);
        gShown[i] = gCounts[i];
        ++changed;
    }
    Log("clocks: %u entries changed, %u keys, %ld overflowed", changed, gUsed, gOverflow);
    ReleaseSRWLockExclusive(&gLock);
}
