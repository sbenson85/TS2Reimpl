#pragma once
// Tearing down the game's general allocator (the image's object at 0x10cb098) after a test built it.
//
// GeneralAllocator::Shutdown never frees the first core (CreateCore marks it canFree 0), and hands each later one back with
// VirtualFree(core, reserved, MEM_RELEASE), which fails (MEM_RELEASE takes size 0; ORIGINAL-BUG in
// src/rz/GeneralAllocator.size.cpp FreeCore), so every core stays reserved. The game shuts its allocator down once, at exit;
// a test that builds and shuts it down leaks every core it made (16 MB or more of address space each), and a full run of
// the suite used to run out of address space after a couple of hundred such tests: VirtualAlloc
// then returns null and the original's MallocInternal, which has no out-of-memory callback, puts the null core into SetTop
// and crashes (null+4 / null+0xc). The harness fails a test that leaves the allocator's bytes changed (harness/main.cpp,
// kWatchedGlobals) and one that leaves a core it made reserved (CheckCoreLeaks).
#include "rz/GeneralAllocator.h"

#include <windows.h>

#include <cstring>

namespace difftest_alloc {

// Shuts `allocator` down, releases the cores Shutdown leaves reserved and zeroes the object (the state the fixtures check for
// before they build it).
inline void ShutdownAndRelease(GeneralAllocator *allocator)
{
    // Every core the allocator made itself (no free function; the fixtures give it no core of their own): their headers sit
    // at the base of their reservation (CreateCore commits from the base it reserved; OsAlloc's allocation is its own
    // base). Collected first: Shutdown unlinks them.
    constexpr int kMaxCores = 64;
    void *cores[kMaxCores];
    int count = 0;
    GeneralAllocatorCore *head = &allocator->mCores;
    for (GeneralAllocatorCore *core = head->next; core && core != head && count < kMaxCores; core = core->next)
        if (!core->freeFunction) cores[count++] = core;
    allocator->Shutdown();
    for (int i = 0; i < count; ++i) {
        MEMORY_BASIC_INFORMATION info;
        // Released only if still reserved at that base (Shutdown's own call succeeds for a core whose `reserved` is 0).
        if (VirtualQuery(cores[i], &info, sizeof(info)) == sizeof(info) && info.State != MEM_FREE && info.AllocationBase == cores[i])
            VirtualFree(cores[i], 0, MEM_RELEASE);
    }
    std::memset(static_cast<void *>(allocator), 0, sizeof(GeneralAllocator));
}

} // namespace difftest_alloc
