// Our C runtime's thread start routine and clock() start count, found by probing (ourcrt.h).
#include "ourcrt.h"

#include <intrin.h>
#include <process.h>

#include <cerrno>
#include <cstring>
#include <ctime>

namespace ourcrt {

bool PatchImport(HMODULE module, const char *dll, const char *name, void *function, void **previous)
{
    auto *base = reinterpret_cast<uint8_t *>(module);
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    for (auto *d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name; ++d) {
        if (_stricmp(reinterpret_cast<const char *>(base + d->Name), dll) != 0 || !d->OriginalFirstThunk) continue;
        auto *names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + d->OriginalFirstThunk);
        auto *slots = reinterpret_cast<IMAGE_THUNK_DATA *>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto *byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char *>(byName->Name), name) != 0) continue;
            DWORD old;
            VirtualProtect(&slots->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            void *was = InterlockedExchangePointer(reinterpret_cast<void **>(&slots->u1.Function), function);
            VirtualProtect(&slots->u1.Function, sizeof(void *), old, &old);
            if (previous) *previous = was;
            return true;
        }
    }
    return false;
}

namespace {

// The probes: only the probing thread gets the probe's answer; any other thread calling the import meanwhile gets the
// real function (by address from kernel32, never through the patched slot).
thread_local bool tProbing = false;
thread_local const void *tStart = nullptr; // FindThreadStart: the routine CreateThread was given
thread_local int64_t tCounter = 0;          // FindClockStart: what QueryPerformanceCounter answers
thread_local const void *tSite = nullptr;   // and where it was called from

decltype(&::CreateThread) RealCreateThread()
{
    return reinterpret_cast<decltype(&::CreateThread)>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateThread")));
}

decltype(&::QueryPerformanceCounter) RealQpc()
{
    return reinterpret_cast<decltype(&::QueryPerformanceCounter)>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueryPerformanceCounter")));
}

HANDLE WINAPI ProbeCreateThread(LPSECURITY_ATTRIBUTES security, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID param,
                                DWORD flags, LPDWORD id)
{
    if (!tProbing) return RealCreateThread()(security, stack, start, param, flags, id);
    tStart = reinterpret_cast<const void *>(start);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
}

BOOL WINAPI ProbeQpc(LARGE_INTEGER *out)
{
    if (!tProbing) return RealQpc()(out);
    tSite = _ReturnAddress();
    out->QuadPart = tCounter;
    return TRUE;
}

unsigned __stdcall NeverRuns(void *)
{
    return 0;
}

// Restores errno and the last error (the probes' failures set them).
struct KeepErrors {
    int error = errno;
    DWORD last = GetLastError();
    ~KeepErrors()
    {
        errno = error;
        SetLastError(last);
    }
};

} // namespace

const void *FindThreadStart(HMODULE module)
{
    const KeepErrors keep;
    void *previous = nullptr;
    if (!PatchImport(module, "KERNEL32.dll", "CreateThread", reinterpret_cast<void *>(&ProbeCreateThread), &previous))
        return nullptr;
    tProbing = true;
    tStart = nullptr;
    const uintptr_t made = _beginthreadex(nullptr, 0, &NeverRuns, nullptr, 0, nullptr);
    tProbing = false;
    PatchImport(module, "KERNEL32.dll", "CreateThread", previous);
    if (made) { // the import isn't the one _beginthreadex calls: a real thread (it returns at once)
        CloseHandle(reinterpret_cast<HANDLE>(made));
        return nullptr;
    }
    return tStart;
}

ClockStart FindClockStart(HMODULE module)
{
    ClockStart result;
    LARGE_INTEGER now;
    if (!RealQpc()(&now)) return result;
    void *previous = nullptr;
    if (!PatchImport(module, "KERNEL32.dll", "QueryPerformanceCounter", reinterpret_cast<void *>(&ProbeQpc), &previous))
        return result;
    tProbing = true;
    tSite = nullptr;
    auto clockAt = [&](int64_t counter) {
        tCounter = counter;
        ++result.calls;
        return std::clock();
    };
    // clock() is -1 below the start count and counts up from 0 at it: the start lies in (lo, hi].
    int64_t lo = 0, hi = now.QuadPart;
    if (clockAt(hi) != -1 && clockAt(lo) == -1 && tSite) {
        while (hi - lo > 1) {
            const int64_t mid = lo + (hi - lo) / 2;
            if (clockAt(mid) == -1) lo = mid;
            else hi = mid;
        }
        result.found = true;
        result.start = hi;
        result.site = tSite;
    }
    tProbing = false;
    PatchImport(module, "KERNEL32.dll", "QueryPerformanceCounter", previous);
    return result;
}

} // namespace ourcrt
