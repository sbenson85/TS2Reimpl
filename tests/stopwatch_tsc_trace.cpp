// cRZStopwatch::ReadTsc (0x1c9244) through the cpuid / rdtsc seam of tests/cpu_trace.h (lane HQ): both sides run
// single-stepped, and the cpuid and rdtsc they execute are answered from one script, so the value each stores is
// compared exactly (tests/rz_stopwatch.cpp only compares the real counter back to back).
//
// Compared per case: the 8 bytes stored at `out` and 8 canary bytes on each side of it; the sequence of
// cpuid / rdtsc / emms executed (exactly cpuid, then rdtsc: cpuid is what serialises the read); the callee-saved
// registers (ebx, esi, edi, ebp) and esp at the return against the entry (cpuid's scripted ebx is junk, so a side that
// did not restore ebx shows).
// Not compared: the cpuid leaf (eax) and subleaf (ecx) the instruction finds. The original executes cpuid with whatever
// eax its caller left (0x1c924a, no load of eax before it), ours with leaf 0 (__cpuid). Neither reads cpuid's answer:
// rdtsc at 0x1c924c overwrites eax and edx, ebx is restored by the pop at 0x1c9254, and ecx is a caller-saved scratch
// register under cdecl; cpuid serialises the stream whatever the leaf.
#include "difftest.h"
#include "cpu_trace.h"
#include "rz/cRZStopwatch.h"

#include <cstring>
#include <random>

namespace {

constexpr uint32_t kReadTsc = 0x1c9244;

struct Out {
    uint8_t before[8];
    uint64_t value;
    uint8_t after[8];
};

const char *OpName(cputrace::Op op)
{
    return op == cputrace::Op::Cpuid ? "cpuid" : op == cputrace::Op::Rdtsc ? "rdtsc" : "emms";
}

bool SameOps(const cputrace::Result &a, const cputrace::Result &b)
{
    if (a.events.size() != b.events.size()) return false;
    for (size_t i = 0; i < a.events.size(); ++i)
        if (a.events[i].op != b.events[i].op) return false;
    return true;
}

} // namespace

DIFF_TEST(cRZStopwatch_ReadTscTrace, 0x1c9244)
{
    using Fn = void(__cdecl *)(uint64_t *);
    Fn orig = ctx.Original<Fn>(kReadTsc);
    Fn ours = &cRZStopwatch::ReadTsc;
    cRZStopwatch::sTimeStampCounterOverride = nullptr; // ours executes the real instructions, which the trace answers
    std::mt19937_64 rng(0x1c9244);
    const uint64_t edges[] = {0, 1, 0xffffffffull, 0x100000000ull, 0x7fffffffffffffffull, 0x8000000000000000ull,
                              ~0ull, 0x0123456789abcdefull};
    for (int i = 0; i < 300; ++i) {
        ++ctx.cases;
        cputrace::Script script;
        script.tsc = {i < 8 ? edges[i] : rng()};
        uint32_t junk = static_cast<uint32_t>(rng());
        script.cpuid = {{junk, junk ^ 0x5a5a5a5a, junk + 1, junk - 1}};
        Out a, b;
        std::memset(&a, 0xa5 ^ (i & 0xff), sizeof a);
        std::memcpy(&b, &a, sizeof a);
        cputrace::Result ra = cputrace::Run(script, reinterpret_cast<const void *>(orig), [&] { orig(&a.value); });
        cputrace::Result rb = cputrace::Run(script, reinterpret_cast<const void *>(ours), [&] { ours(&b.value); });
        if (!ra.entered || !ra.returned || !rb.entered || !rb.returned) {
            ctx.Fail("case %d: trace incomplete (orig entered %d returned %d, ours %d %d)", i, ra.entered, ra.returned,
                     rb.entered, rb.returned);
            return;
        }
        if (std::memcmp(&a, &b, sizeof a) != 0)
            ctx.Fail("case %d: out %016llx vs ours %016llx (script %016llx) or canaries differ", i, a.value, b.value,
                     script.tsc[0]);
        if (a.value != script.tsc[0]) ctx.Fail("case %d: the original stored %016llx, not the script's", i, a.value);
        if (!SameOps(ra, rb) || ra.events.size() != 2 || ra.events[0].op != cputrace::Op::Cpuid ||
            ra.events[1].op != cputrace::Op::Rdtsc) {
            ctx.Fail("case %d: instructions differ: original %zu, ours %zu", i, ra.events.size(), rb.events.size());
            for (const auto &e : ra.events) ctx.Fail("  original %s", OpName(e.op));
            for (const auto &e : rb.events) ctx.Fail("  ours %s", OpName(e.op));
            return;
        }
        for (const cputrace::Result *r : {&ra, &rb}) {
            const char *side = r == &ra ? "original" : "ours";
            cputrace::Saved expect = r->atEntry;
            expect.esp += 4;
            if (!(r->atReturn == expect))
                ctx.Fail("case %d: %s did not preserve ebx/esi/edi/ebp/esp (ebx %08x -> %08x)", i, side, r->atEntry.ebx,
                         r->atReturn.ebx);
        }
    }
}
