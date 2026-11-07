// Differential tests for src/rz/cRZMemoryReport.h (lane HQ): the memory hook's and report's constructors, the 4-byte
// CountFromOne and the global DbgHelp load wrapper, against the image's.
//
// cRZMemoryReport_Hook (0xc696ce): junk object with margins, random text pointers: the window, the returned pointer.
// cRZMemoryReport_CountFromOne (0x1baa70): the same.
// cRZMemoryReport_LoadGlobalDbgHelp (0x1bcc62): DbgHelpTable::Load stood in on both sides (shape::StandIn over 0x1bc8a7 and
//   ours): the table it is called on (0x10cb000) and the result (al) handed back, for answers 0, 1, 0xff and junk.
// cRZMemoryReport_Construct (0xc696d9): the Load stand-in again (logs, answers junk); the object (0x24 bytes, junk, 8-byte
//   margins) with the lock's block word compared by its CRITICAL_SECTION's state, the globals 0x1103060 (by what it points
//   at: a 4-byte block holding 1) and 0x1103064 (the object), the Load calls. Both sides' locks and blocks are freed after.
#include "difftest.h"
#include "code_patch.h"
#include "app/ReporterDump.h"
#include "game/game.h"
#include "rz/cRZMemoryReport.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr int kMargin = 8;

std::vector<uint32_t> gLoadLog;
uint32_t gLoadAnswer = 0;
uintptr_t gBase = 0;

uint32_t __fastcall StandLoad(void *table, void *)
{
    gLoadLog.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(table) - gBase));
    return gLoadAnswer;
}

uint32_t WordAt(const uint8_t *p, size_t off)
{
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}

} // namespace

DIFF_TEST(cRZMemoryReport_Hook, 0xc696ce)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, const char *)>(0xc696ce);
    std::mt19937 rng(0xc696ce);
    for (int i = 0; i < 40; ++i) {
        ++ctx.cases;
        uint8_t a[4 + 2 * kMargin], b[sizeof a];
        for (auto &x : a) x = static_cast<uint8_t>(rng());
        std::memcpy(b, a, sizeof a);
        const char *text = reinterpret_cast<const char *>(static_cast<uintptr_t>(rng()));
        void *ra = orig(a + kMargin, text);
        void *rb = reinterpret_cast<cRZMemoryHook *>(b + kMargin)->Construct(text);
        if (ra != a + kMargin || rb != b + kMargin) ctx.Fail("returned %p / %p", ra, rb);
        if (std::memcmp(a, b, sizeof a) != 0) ctx.Fail("case %d: the object differs", i);
        // The destructor (0xc69881, run by the image's exit thunk 0xd9898d) against ours on junk.
        for (auto &x : a) x = static_cast<uint8_t>(rng());
        std::memcpy(b, a, sizeof a);
        ctx.Original<void(__thiscall *)(void *)>(0xc69881)(a + kMargin);
        reinterpret_cast<cRZMemoryHook *>(b + kMargin)->Destruct();
        if (std::memcmp(a, b, sizeof a) != 0) ctx.Fail("case %d: the destroyed object differs", i);
    }
}

DIFF_TEST(cRZMemoryReport_CountFromOne, 0x1baa70)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x1baa70);
    std::mt19937 rng(0x1baa70);
    for (int i = 0; i < 40; ++i) {
        ++ctx.cases;
        uint8_t a[4 + 2 * kMargin], b[sizeof a];
        for (auto &x : a) x = static_cast<uint8_t>(rng());
        std::memcpy(b, a, sizeof a);
        void *ra = orig(a + kMargin);
        void *rb = reinterpret_cast<CountFromOne *>(b + kMargin)->Construct();
        if (ra != a + kMargin || rb != b + kMargin) ctx.Fail("returned %p / %p", ra, rb);
        if (std::memcmp(a, b, sizeof a) != 0) ctx.Fail("case %d: the object differs", i);
    }
}

DIFF_TEST(cRZMemoryReport_LoadGlobalDbgHelp, 0x1bcc62)
{
    gBase = reinterpret_cast<uintptr_t>(ctx.image.At<void>(0));
    shape::StandIn load(0x1bc8a7, &appbase::DbgHelpTable::Load, &StandLoad);
    auto orig = ctx.Original<uint8_t(__cdecl *)()>(0x1bcc62);
    std::mt19937 rng(0x1bcc62);
    for (int i = 0; i < 40; ++i) {
        ++ctx.cases;
        gLoadAnswer = i < 3 ? (i == 2 ? 0xff : i) : static_cast<uint32_t>(rng());
        gLoadLog.clear();
        const uint8_t ra = orig();
        auto logA = gLoadLog;
        gLoadLog.clear();
        const uint8_t rb = LoadGlobalDbgHelp();
        if (ra != rb) ctx.Fail("case %d: %02x vs ours %02x", i, ra, rb);
        if (logA != gLoadLog || logA.size() != 1 || logA[0] != appbase::DbgHelpTable::kGlobal)
            ctx.Fail("case %d: Load calls differ", i);
    }
}

namespace {
// The report as a comparable form: the lock's block word by its CRITICAL_SECTION's state, the rest as it is.
std::vector<uint32_t> ReportForm(const uint8_t *w)
{
    std::vector<uint32_t> out;
    for (size_t off = 0; off < 0x24 + 2 * kMargin; off += 4) out.push_back(WordAt(w, off));
    const uint32_t block = WordAt(w, kMargin + 0xc);
    out[(kMargin + 0xc) / 4] = block != 0;
    if (block) {
        const auto *cs = reinterpret_cast<const CRITICAL_SECTION *>(static_cast<uintptr_t>(block));
        out.push_back(static_cast<uint32_t>(cs->LockCount));
        out.push_back(static_cast<uint32_t>(cs->RecursionCount));
        out.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cs->OwningThread)));
        out.push_back(WordAt(reinterpret_cast<const uint8_t *>(cs), 0x18));
    }
    return out;
}
} // namespace

DIFF_TEST(cRZMemoryReport_Construct, 0xc696d9)
{
    gBase = reinterpret_cast<uintptr_t>(ctx.image.At<void>(0));
    shape::StandIn load(0x1bc8a7, &appbase::DbgHelpTable::Load, &StandLoad);
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xc696d9);
    const uint32_t savedCount = *ctx.image.At<uint32_t>(cRZMemoryReport::kCountRva);
    const uint32_t savedReport = *ctx.image.At<uint32_t>(cRZMemoryReport::kReportRva);
    std::mt19937 rng(0xc696d9);
    for (int i = 0; i < 40; ++i) {
        ++ctx.cases;
        alignas(8) uint8_t w[2][0x24 + 2 * kMargin];
        for (auto &x : w[0]) x = static_cast<uint8_t>(rng());
        std::memcpy(w[1], w[0], sizeof w[0]);
        gLoadAnswer = static_cast<uint32_t>(rng());
        std::vector<uint32_t> form[2], log[2];
        uint32_t count[2], report[2];
        void *returned[2];
        for (int side = 0; side < 2; ++side) {
            *ctx.image.At<uint32_t>(cRZMemoryReport::kCountRva) = static_cast<uint32_t>(rng());
            *ctx.image.At<uint32_t>(cRZMemoryReport::kReportRva) = static_cast<uint32_t>(rng());
            gLoadLog.clear();
            uint8_t *object = w[side] + kMargin;
            returned[side] = side == 0 ? orig(object) : reinterpret_cast<cRZMemoryReport *>(object)->Construct();
            form[side] = ReportForm(w[side]);
            log[side] = gLoadLog;
            const uint32_t c = *ctx.image.At<uint32_t>(cRZMemoryReport::kCountRva);
            auto *countObject = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(c));
            count[side] = c ? *countObject | static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, countObject)) << 8 : 0;
            report[side] = *ctx.image.At<uint32_t>(cRZMemoryReport::kReportRva) -
                           static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
            reinterpret_cast<cRZMemoryReport *>(object)->lock.Destruct();
            HeapFree(GetProcessHeap(), 0, countObject);
            if (returned[side] != object) ctx.Fail("case %d side %d: returned %p", i, side, returned[side]);
        }
        if (form[0] != form[1]) ctx.Fail("case %d: the object differs", i);
        if (count[0] != count[1] || count[0] != (1u | 4u << 8)) ctx.Fail("case %d: the count block %08x vs ours %08x", i, count[0], count[1]);
        if (report[0] != 0 || report[1] != 0) ctx.Fail("case %d: the report global", i);
        if (log[0] != log[1] || log[0].size() != 1 || log[0][0] != appbase::DbgHelpTable::kGlobal) ctx.Fail("case %d: Load calls", i);
    }
    *ctx.image.At<uint32_t>(cRZMemoryReport::kCountRva) = savedCount;
    *ctx.image.At<uint32_t>(cRZMemoryReport::kReportRva) = savedReport;
}
