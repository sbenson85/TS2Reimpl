#pragma once
// Where another thread of this process is: its instruction pointer and the return addresses on its stack (a diagnostic
// for the lockstep scheduler's stuck rule, host/shared/lockstep.h; tests: tests/host_threadstack.cpp). Win32 (x86)
// only, like the host.
//
// Capture suspends the thread, copies its registers and the top of its stack into memory allocated beforehand, and
// resumes it: nothing is allocated while it is suspended (it may hold the heap's lock). The return addresses are then
// found by scanning the copy: a value that points into executable memory just after something that encodes a call. That
// needs no symbols or frame pointers (system DLLs omit them), at the price of a stale value now and then (an old return
// address left in a slot the current frame hasn't overwritten); the first ones, nearest the top, are the reliable ones.
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace threadstack {

// A name for an address ("" when the namer doesn't know it): the host names the game image's addresses by RVA.
using Namer = std::string (*)(uintptr_t address);

struct Snapshot {
    bool ok = false;
    std::string error;                 // why not (ok false)
    uint32_t eip = 0, esp = 0, ebp = 0;
    std::vector<uint32_t> returns;     // candidate return addresses, from the top of the stack down
};

// `thread` needs THREAD_SUSPEND_RESUME, THREAD_GET_CONTEXT and THREAD_QUERY_INFORMATION; never the calling thread.
// Scans at most `stackBytes` from the stack pointer and keeps the first `maxReturns` candidates.
Snapshot Capture(HANDLE thread, size_t maxReturns = 16, size_t stackBytes = 64 * 1024);

// The same scan of the calling thread's own stack, from here up (the first candidates are this function's callers), or
// from `start` when it lies above here on this stack: a caller's return-address slot (_AddressOfReturnAddress) skips
// the frames below it, whose uninitialised locals can hold stale return addresses.
std::vector<uint32_t> Returns(size_t maxReturns = 16, size_t stackBytes = 16 * 1024, const void *start = nullptr);

// The bytes that end just before `p` encode a call (call rel32, or call through a register or memory operand).
bool LooksLikeCall(const uint8_t *p);

// The module and offset of an address ("ntdll.dll+0x7a3c"; the namer first), or the bare address.
std::string Where(uintptr_t address, Namer namer = nullptr);

// "eip <where>; returns <where>, <where>, ..." (or "not captured: <error>").
std::string Describe(const Snapshot &snapshot, Namer namer = nullptr);

// The thread's start routine as Windows records it (0 when unknown).
uintptr_t StartAddress(HANDLE thread);

// The ids of this process's threads.
std::vector<DWORD> ProcessThreads();

} // namespace threadstack
