#pragma once
// A seam for code that executes cpuid / rdtsc / emms itself (lane HQ): the function under test runs single-stepped
// (the trap flag, a vectored handler of the test's), and at each instruction boundary inside it a cpuid or rdtsc is not
// executed but answered from a script (registers set, EIP moved past the two bytes), so the original and ours see the
// same "hardware" and their results can be compared exactly. emms is recorded and left to run. The trace starts at the
// function's entry (the address called) and ends when it returns to its caller (EIP = the return address, ESP = entry
// ESP + 4); the registers a cdecl/thiscall callee must preserve are captured at both points.
//
// Usage: `cputrace::Script s; s.tsc = {...}; auto r = cputrace::Run(s, entryAddress, [&] { fn(args); });`.
// One trace at a time (globals); the handler is added for the run and removed after it.
#include <windows.h>
#include <intrin.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace cputrace {

struct Regs {
    uint32_t eax, ebx, ecx, edx;
};

enum class Op : uint8_t { Cpuid, Rdtsc, Emms };

struct Event {
    Op op;
    uint32_t eax, ecx; // as the instruction found them (cpuid: leaf, subleaf)
};

struct Script {
    std::vector<Regs> cpuid;   // answers in order; past the end the last one repeats (none: zeros)
    std::vector<uint64_t> tsc; // answers in order; past the end the last one repeats (none: 0)
};

struct Saved {
    uint32_t ebx, esi, edi, ebp, esp;
    bool operator==(const Saved &) const = default;
};

struct Result {
    std::vector<Event> events;
    bool entered = false, returned = false;
    Saved atEntry{}, atReturn{}; // atReturn.esp is compared against atEntry.esp + 4
    uint32_t steps = 0;
};

namespace detail {
inline const Script *gScript = nullptr;
inline Result *gResult = nullptr;
inline volatile bool gActive = false;
inline uint32_t gEntry = 0, gReturn = 0, gReturnEsp = 0;
inline size_t gNextCpuid = 0, gNextTsc = 0;

inline Saved Capture(const CONTEXT &c) { return {c.Ebx, c.Esi, c.Edi, c.Ebp, c.Esp}; }

// Stands in for every cpuid / rdtsc at EIP (several in a row: the instruction after a skipped one would otherwise run
// before the next trap).
inline void Emulate(CONTEXT &c)
{
    for (;;) {
        const auto *p = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(c.Eip));
        if (p[0] != 0x0f) return;
        if (p[1] == 0xa2) {
            gResult->events.push_back({Op::Cpuid, c.Eax, c.Ecx});
            Regs r{};
            if (!gScript->cpuid.empty())
                r = gScript->cpuid[gNextCpuid < gScript->cpuid.size() ? gNextCpuid : gScript->cpuid.size() - 1];
            ++gNextCpuid;
            c.Eax = r.eax;
            c.Ebx = r.ebx;
            c.Ecx = r.ecx;
            c.Edx = r.edx;
            c.Eip += 2;
        } else if (p[1] == 0x31) {
            gResult->events.push_back({Op::Rdtsc, c.Eax, c.Ecx});
            uint64_t v = 0;
            if (!gScript->tsc.empty())
                v = gScript->tsc[gNextTsc < gScript->tsc.size() ? gNextTsc : gScript->tsc.size() - 1];
            ++gNextTsc;
            c.Eax = static_cast<uint32_t>(v);
            c.Edx = static_cast<uint32_t>(v >> 32);
            c.Eip += 2;
        } else {
            if (p[1] == 0x77) gResult->events.push_back({Op::Emms, c.Eax, c.Ecx}); // runs for real
            return;
        }
    }
}

inline LONG CALLBACK Handler(EXCEPTION_POINTERS *info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT &c = *info->ContextRecord;
    if (!gActive) {
        c.EFlags &= ~0x100u;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    ++gResult->steps;
    if (!gResult->entered) {
        if (c.Eip == gEntry) {
            gResult->entered = true;
            gResult->atEntry = Capture(c);
            gReturn = *reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(c.Esp));
            gReturnEsp = c.Esp + 4;
        }
    } else if (c.Eip == gReturn && c.Esp == gReturnEsp) {
        gResult->returned = true;
        gResult->atReturn = Capture(c);
        gActive = false;
        c.EFlags &= ~0x100u;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (gResult->entered) Emulate(c);
    c.EFlags |= 0x100u;
    return EXCEPTION_CONTINUE_EXECUTION;
}
} // namespace detail

// Runs `call` (which must call `entry` once, directly) with the trace on.
inline Result Run(const Script &script, const void *entry, const std::function<void()> &call)
{
    using namespace detail;
    Result result;
    gScript = &script;
    gResult = &result;
    gNextCpuid = gNextTsc = 0;
    gEntry = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry));
    void *handler = AddVectoredExceptionHandler(1, &Handler);
    gActive = true;
    __writeeflags(__readeflags() | 0x100);
    call();
    gActive = false; // the handler clears the flag at the return; this covers a trace that never got there
    RemoveVectoredExceptionHandler(handler);
    gScript = nullptr;
    gResult = nullptr;
    return result;
}

} // namespace cputrace
