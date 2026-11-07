// Differential tests for src/audio/MmxResampleTables.h (lane HQ).
//
// MmxResampleTables_DetectMmx (0xc94db1): both sides single-stepped through the cpuid seam of tests/cpu_trace.h, cpuid
//   answered from a script (EDX with and without bit 23, the other bits junk): the result (al), the instructions met in
//   order with cpuid's leaf and subleaf (1, 0; ebx is zeroed before it too in the original, unobserved here), emms only
//   with MMX, and the callee-saved registers at the return. Not covered: a CPU without cpuid or MMX state (the guarded
//   faults), which no CPU Windows runs on has.
// MmxResampleTables_BuildTables (0xc98e00): the 0x4000-byte table area from junk with 8-byte margins: every byte.
// MmxResampleTables_Construct (0xc94c78): DetectMmx and BuildTables stood in on both sides (shape::StandIn: scripted
//   answers 0, 1 and junk in the high bytes of eax, calls logged): the flag dword with margins, the calls, the return.
//   And the image's exit thunk 0xd98a7e on junk flags: it writes nothing (ours registers a thunk that does nothing).
#include "difftest.h"
#include "code_patch.h"
#include "cpu_trace.h"
#include "audio/MmxResampleTables.h"
#include "game/game.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr int kMargin = 8;

std::vector<uint32_t> gLog;
uint32_t gDetectAnswer = 0;

uint32_t __cdecl StandDetect()
{
    gLog.push_back(1);
    return gDetectAnswer;
}
void __cdecl StandBuild() { gLog.push_back(2); }

std::vector<uint8_t> Read(const difftest::Context &ctx, uint32_t first, uint32_t size)
{
    const uint8_t *p = ctx.image.At<uint8_t>(first);
    return {p, p + size};
}
void Write(const difftest::Context &ctx, uint32_t first, const std::vector<uint8_t> &v)
{
    std::memcpy(ctx.image.At<uint8_t>(first), v.data(), v.size());
}

} // namespace

DIFF_TEST(MmxResampleTables_DetectMmx, 0xc94db1)
{
    using Fn = bool(__cdecl *)();
    Fn orig = ctx.Original<Fn>(0xc94db1);
    Fn ours = &MmxResampleTables::DetectMmx;
    std::mt19937 rng(0xc94db1);
    for (int i = 0; i < 200; ++i) {
        ++ctx.cases;
        cputrace::Script script;
        uint32_t edx = static_cast<uint32_t>(rng());
        if (i < 2) edx = i ? 0x800000 : ~0x800000u;
        script.cpuid = {{static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), edx}};
        uint8_t ra = 0, rb = 0;
        cputrace::Result ta = cputrace::Run(script, reinterpret_cast<const void *>(orig), [&] { ra = orig(); });
        cputrace::Result tb = cputrace::Run(script, reinterpret_cast<const void *>(ours), [&] { rb = ours(); });
        if (!ta.returned || !tb.returned) {
            ctx.Fail("case %d: trace incomplete (%d, %d)", i, ta.returned, tb.returned);
            return;
        }
        const bool mmx = (edx & 0x800000) != 0;
        if (ra != rb || ra != mmx) ctx.Fail("case %d (edx %08x): %d vs ours %d", i, edx, ra, rb);
        bool same = ta.events.size() == tb.events.size() && ta.events.size() == (mmx ? 2u : 1u);
        for (size_t k = 0; same && k < ta.events.size(); ++k) {
            const auto &a = ta.events[k], &b = tb.events[k];
            same = a.op == b.op && (a.op != cputrace::Op::Cpuid || (a.eax == 1 && b.eax == 1 && a.ecx == 0 && b.ecx == 0));
        }
        if (!same || ta.events[0].op != cputrace::Op::Cpuid || (mmx && ta.events[1].op != cputrace::Op::Emms))
            ctx.Fail("case %d: instructions differ (%zu, %zu)", i, ta.events.size(), tb.events.size());
        for (const cputrace::Result *r : {&ta, &tb}) {
            cputrace::Saved expect = r->atEntry;
            expect.esp += 4;
            if (!(r->atReturn == expect)) ctx.Fail("case %d: %s did not preserve the registers", i, r == &ta ? "original" : "ours");
        }
    }
}

DIFF_TEST(MmxResampleTables_BuildTables, 0xc98e00)
{
    auto orig = ctx.Original<void(__cdecl *)()>(0xc98e00);
    constexpr uint32_t first = MmxResampleTables::kTables - kMargin, size = MmxResampleTables::kTableBytes + 2 * kMargin;
    const auto saved = Read(ctx, first, size);
    std::mt19937 rng(0xc98e00);
    for (int i = 0; i < 4; ++i) {
        ++ctx.cases;
        std::vector<uint8_t> start(size);
        for (auto &x : start) x = static_cast<uint8_t>(rng());
        Write(ctx, first, start);
        orig();
        const auto a = Read(ctx, first, size);
        Write(ctx, first, start);
        MmxResampleTables::BuildTables();
        const auto b = Read(ctx, first, size);
        for (uint32_t k = 0; k < size; ++k)
            if (a[k] != b[k]) {
                ctx.Fail("case %d: byte %08x is %02x, ours %02x", i, first + k, a[k], b[k]);
                break;
            }
        if (a == start) ctx.Fail("the original wrote nothing");
    }
    Write(ctx, first, saved);
}

DIFF_TEST(MmxResampleTables_Construct, 0xc94c78)
{
    shape::StandIn detect(0xc94db1, &MmxResampleTables::DetectMmx, &StandDetect);
    shape::StandIn build(0xc98e00, &MmxResampleTables::BuildTables, &StandBuild);
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xc94c78);
    auto thunk = ctx.Original<void(__cdecl *)()>(0xd98a7e);
    constexpr uint32_t first = MmxResampleTables::kHaveMmx - kMargin, size = 8 + 2 * kMargin; // the flag and the object
    void *object = ctx.image.At<void>(MmxResampleTables::kStatic);
    const auto saved = Read(ctx, first, size);
    std::mt19937 rng(0xc94c78);
    for (int i = 0; i < 40; ++i) {
        ++ctx.cases;
        gDetectAnswer = i < 2 ? i : (static_cast<uint32_t>(rng()) & ~0xffu) | (rng() & 1);
        std::vector<uint8_t> start(size);
        for (auto &x : start) x = static_cast<uint8_t>(rng());
        Write(ctx, first, start);
        gLog.clear();
        void *ra = orig(object);
        const auto a = Read(ctx, first, size);
        const auto logA = gLog;
        Write(ctx, first, start);
        gLog.clear();
        void *rb = GameAddress<MmxResampleTables>(MmxResampleTables::kStatic)->Construct();
        const auto b = Read(ctx, first, size);
        if (ra != object || rb != object) ctx.Fail("case %d: returned %p / %p", i, ra, rb);
        if (a != b) ctx.Fail("case %d (answer %08x): the flag differs", i, gDetectAnswer);
        if (logA != gLog) ctx.Fail("case %d: calls differ (%zu, %zu)", i, logA.size(), gLog.size());
        // The exit thunk on what the constructor left, and on junk: nothing written.
        for (int k = 0; k < 2; ++k) {
            if (k) {
                for (auto &x : start) x = static_cast<uint8_t>(rng());
                Write(ctx, first, start);
            }
            const auto before = Read(ctx, first, size);
            thunk();
            if (Read(ctx, first, size) != before) ctx.Fail("case %d: the image's exit thunk wrote", i);
        }
    }
    Write(ctx, first, saved);
}
