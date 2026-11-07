// Differential tests for lane HQ's owned XC entries (src/app/StaticInitH5.cpp; docs/subsystems/startup.md "Done in lane
// HQ"). The constructors themselves are verified by their own tests (rz_winapi_tables, stopwatch_calibration,
// startup_objects, rz_memory_report, audio_mmx_tables); here they are stood in on both sides (shape::StandIn over the
// image's function and ours: each logs the object it was called on and its argument), so what is compared is each
// entry's own work.
//
// StaticInitH5_Entries: per entry, the slot holds the image's initialiser our table names; the image's entry against
//   ours: the constructor calls (object, argument), the registrations (none, or exactly one, and ours not the image's
//   thunk); then the registered thunks, the image's against ours: the FreeLibrary calls (the import bound to a logging
//   fake; the module words junk, zero and set), the memory report's destructor 0xc69888 (stood in over the image's: our
//   thunk reaches it through OriginalFunction), the memory hook's destructor (the object window), the MMX thunk (nothing).
//   Slot 193 runs for real on both sides: the 16-byte rotation with junk 8-byte margins, bit for bit, from the file's
//   axes and half angles and from 399 random ones (axes in [-10, 10), half angles in [-4, 4)), written where the
//   image's entry reads them.
//   Slot 98 (lane II) is `jmp _tzset`: the image CRT's _tzset (0x84571e) and ours are stood in alike (tag 11).
//   The entries (slot: image initialiser): 93: 0x1382ac, 94: 0x1382c2, 98: 0x138306, 102: 0x138396, 103: 0x1383a0,
//   105: 0x1383aa, 134: 0x1388d0, 193: 0x1395b0, 754: 0x140871, 755: 0x140856, 769: 0x1409ae (each checked against the
//   XC table).
// StaticInitH5_OnImageTable: the runner over a copy of the image's XC table with only these slots: 11 ours, 0 image /
//   mismatched / unused; the same constructor calls in slot order, the same registrations, the same thunk calls when
//   each side's list runs LIFO.
#include "difftest.h"
#include "code_patch.h"
#include "app/StaticInit.h"
#include "app/StaticInitH5.h"
#include "app/WinMainStart.h"
#include "audio/MmxResampleTables.h"
#include "game/game.h"
#include "misc/StartupObjects.h"
#include "rz/WinApiTables.h"
#include "rz/cRZMemoryReport.h"
#include "rz/cRZStopwatchCalibration.h"

#include <windows.h>
#include <time.h>

#include <cstring>
#include <random>
#include <vector>

namespace {

using staticinit::H5Kind;
using staticinit::H5Static;

constexpr uint32_t kXcFirst = 0xd9a724, kXcLast = 0xd9b3a8;
constexpr uint32_t kAtExit = 0x824cbe;
constexpr int kMargin = 8;

using Thunk = void(__cdecl *)();
std::vector<Thunk> gRegistered;
int __cdecl RecordAtExit(Thunk f)
{
    gRegistered.push_back(f);
    return 0;
}

uintptr_t gBase = 0;
std::vector<uint32_t> gLog; // (tag, object RVA, argument) triples
void Log(uint32_t tag, const void *object, uint32_t arg)
{
    gLog.push_back(tag);
    gLog.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object) - gBase));
    gLog.push_back(arg);
}

template <uint32_t Tag> void *__fastcall StandCtor(void *self, void *)
{
    Log(Tag, self, 0);
    return self;
}
void *__fastcall StandHookCtor(void *self, void *, const char *text)
{
    Log(9, self, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text) - gBase));
    return self;
}
void __fastcall StandReportDtor(void *self, void *) { Log(20, self, 0); }
void __cdecl StandTzset() { Log(11, nullptr, 0); }
BOOL WINAPI FakeFreeLibrary(HMODULE module)
{
    Log(21, nullptr, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(module)));
    return TRUE;
}

struct BindImport {
    void **entry;
    void *saved;
    BindImport(const difftest::Context &ctx, uint32_t rva, const void *target) : entry(ctx.image.At<void *>(rva)), saved(*entry)
    {
        DWORD old;
        VirtualProtect(entry, 4, PAGE_READWRITE, &old);
        *entry = const_cast<void *>(target);
    }
    ~BindImport() { *entry = saved; }
};

// Every constructor stood in on both sides, the image's atexit and ours captured, FreeLibrary and the report's
// destructor logged.
struct Fixture {
    shape::StandIn tryEnter, shell, timeZone, files, dirs, calibration, pool, report, hook, mmx;
    shape::CodePatch reportDtor, atExit;
    BindImport freeLibrary;
    staticinit::RegisterAtExit savedAtExit;
    explicit Fixture(difftest::Context &ctx)
        : tryEnter(0x1af60a, &rz::TryEnterCriticalSectionLoader::Construct, &StandCtor<1>),
          shell(0x1afb32, &rz::ShellExecuteLoader::Construct, &StandCtor<2>),
          timeZone(0x84571e, &_tzset, &StandTzset), // the image CRT's _tzset and ours (slot 98)
          files(0x1c0d9b, &rz::WideFileFunctions::Construct, &StandCtor<3>),
          dirs(0x1c4830, &rz::WideDirectoryFunctions::Construct, &StandCtor<4>),
          calibration(0x1c8d8e, &cRZStopwatchCalibration::Construct, &StandCtor<5>),
          pool(0x26bbb0, &startupobj::RecordPoolReset::Construct, &StandCtor<6>),
          report(0xc696d9, &cRZMemoryReport::Construct, &StandCtor<8>),
          hook(0xc696ce, &cRZMemoryHook::Construct, &StandHookCtor),
          mmx(0xc94c78, &MmxResampleTables::Construct, &StandCtor<10>),
          reportDtor(0xc69888, reinterpret_cast<const void *>(&StandReportDtor)),
          atExit(kAtExit, reinterpret_cast<const void *>(&RecordAtExit)),
          freeLibrary(ctx, rz::winapi::kIatFreeLibrary, reinterpret_cast<const void *>(&FakeFreeLibrary)),
          savedAtExit(staticinit::BoundAtExit())
    {
        gBase = reinterpret_cast<uintptr_t>(ctx.image.At<void>(0));
        staticinit::BindAtExit(&RecordAtExit);
    }
    ~Fixture() { staticinit::BindAtExit(savedAtExit); }
};

// The memory the entries and thunks touch outside the stood-in constructors: the loaders' module words, the hook object,
// the rotation; with margins.
struct Area {
    uint32_t first, size;
};
const Area kAreas[] = {
    {rz::TryEnterCriticalSectionLoader::kProcRva - kMargin, 0x18 + 2 * kMargin}, // 0x10caea0..0x10caeb8 (both loaders)
    {cRZMemoryHook::kStatic - kMargin, 4 + 2 * kMargin},
    {0x10d9bbc - kMargin, 16 + 2 * kMargin},
};

std::vector<uint8_t> Snapshot(const difftest::Context &ctx)
{
    std::vector<uint8_t> all;
    for (const Area &a : kAreas) {
        const uint8_t *p = ctx.image.At<uint8_t>(a.first);
        all.insert(all.end(), p, p + a.size);
    }
    return all;
}
void Restore(const difftest::Context &ctx, const std::vector<uint8_t> &all)
{
    size_t off = 0;
    for (const Area &a : kAreas) {
        std::memcpy(ctx.image.At<uint8_t>(a.first), all.data() + off, a.size);
        off += a.size;
    }
}

// Slot 193's inputs (two axes in .data, two half angles in .rdata): the file's values, or random ones for later rounds
// (both sides read the same), restored at the end.
struct RotationInputs {
    float *axes[2];
    double *angles[2];
    float savedAxes[2][3];
    double savedAngles[2];
    explicit RotationInputs(const difftest::Context &ctx)
        : axes{ctx.image.At<float>(staticinit::kRotationAxisA), ctx.image.At<float>(staticinit::kRotationAxisB)},
          angles{ctx.image.At<double>(staticinit::kRotationHalfAngleA), ctx.image.At<double>(staticinit::kRotationHalfAngleB)}
    {
        for (int k = 0; k < 2; ++k) {
            std::memcpy(savedAxes[k], axes[k], sizeof savedAxes[k]);
            savedAngles[k] = *angles[k];
            DWORD old;
            VirtualProtect(angles[k], sizeof(double), PAGE_READWRITE, &old);
        }
    }
    void Randomise(std::mt19937 &rng)
    {
        std::uniform_real_distribution<float> axis(-10.0f, 10.0f);
        std::uniform_real_distribution<double> angle(-4.0, 4.0);
        for (int k = 0; k < 2; ++k) {
            for (int c = 0; c < 3; ++c) axes[k][c] = axis(rng);
            *angles[k] = angle(rng);
        }
    }
    void Restore()
    {
        for (int k = 0; k < 2; ++k) {
            std::memcpy(axes[k], savedAxes[k], sizeof savedAxes[k]);
            *angles[k] = savedAngles[k];
        }
    }
    ~RotationInputs() { Restore(); }
};

bool SlotHolds(difftest::Context &ctx, uint32_t slot, uint32_t rva)
{
    uint32_t held = *ctx.image.At<uint32_t>(kXcFirst + 4 * slot) - static_cast<uint32_t>(ctx.image.Base());
    if (held == rva) return true;
    ctx.Fail("slot %u holds %08x, our table says %08x", slot, held, rva);
    return false;
}

} // namespace

DIFF_TEST(StaticInitH5_Entries, 0x1382ac)
{
    Fixture fixture(ctx);
    size_t n = 0;
    const H5Static *entries = staticinit::H5Statics(&n);
    const auto saved = Snapshot(ctx);
    RotationInputs rotation(ctx);
    std::mt19937 rng(0x1382ac);
    for (size_t i = 0; i < n; ++i) {
        const H5Static &e = entries[i];
        ++ctx.cases;
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        const int rounds = e.kind == H5Kind::Rotation ? 400 : 4;
        for (int round = 0; round < rounds; ++round) {
            ++ctx.cases;
            if (e.kind == H5Kind::Rotation) {
                if (round == 0) rotation.Restore();
                else rotation.Randomise(rng);
            }
            std::vector<uint8_t> start(saved.size());
            for (auto &x : start) x = static_cast<uint8_t>(rng());
            if (round == 1) { // loaders: no module (the thunk calls nothing)
                std::memset(&start[kMargin], 0, 0x18);
            }
            std::vector<uint8_t> after[2], afterThunk[2];
            std::vector<uint32_t> log[2], thunkLog[2];
            std::vector<Thunk> registered[2];
            for (int side = 0; side < 2; ++side) {
                Restore(ctx, start);
                gLog.clear();
                gRegistered.clear();
                if (side == 0) ctx.Original<void(__cdecl *)()>(e.imageRva)();
                else staticinit::InitH5(&e);
                after[side] = Snapshot(ctx);
                log[side] = gLog;
                registered[side] = gRegistered;
                gLog.clear();
                for (Thunk t : gRegistered) t();
                afterThunk[side] = Snapshot(ctx);
                thunkLog[side] = gLog;
            }
            if (log[0] != log[1] || (e.kind != H5Kind::Rotation && log[0].size() != 3))
                ctx.Fail("slot %u: constructor calls differ (%zu, %zu)", e.slot, log[0].size(), log[1].size());
            if (after[0] != after[1]) ctx.Fail("slot %u round %d: memory after the entry differs", e.slot, round);
            const size_t expect = e.thunk ? 1 : 0;
            if (registered[0].size() != expect || registered[1].size() != expect)
                ctx.Fail("slot %u: %zu / %zu registrations (%zu)", e.slot, registered[0].size(), registered[1].size(), expect);
            else if (expect && (reinterpret_cast<uintptr_t>(registered[0][0]) - gBase != e.thunk ||
                                registered[1][0] == registered[0][0]))
                ctx.Fail("slot %u: the registered thunks", e.slot);
            if (thunkLog[0] != thunkLog[1]) ctx.Fail("slot %u round %d: thunk calls differ (%zu, %zu)", e.slot, round, thunkLog[0].size(), thunkLog[1].size());
            if (afterThunk[0] != afterThunk[1]) ctx.Fail("slot %u round %d: memory after the thunk differs", e.slot, round);
        }
    }
    Restore(ctx, saved);
}

DIFF_TEST(StaticInitH5_OnImageTable, 0x1395b0)
{
    Fixture fixture(ctx);
    size_t ownedCount = 0;
    const winmainstart::OwnedInitialiser *owned = staticinit::OwnedH5(&ownedCount);
    const uint32_t slots = (kXcLast - kXcFirst) / 4;
    std::vector<winmainstart::Initialiser> table(slots, nullptr);
    for (size_t i = 0; i < ownedCount; ++i)
        if (owned[i].slot < slots) table[owned[i].slot] = ctx.image.At<winmainstart::Initialiser>(kXcFirst)[owned[i].slot];
    const auto saved = Snapshot(ctx);
    std::vector<uint32_t> log[2], exitLog[2];
    std::vector<uint8_t> after[2];
    size_t registrations[2];
    winmainstart::XcRunCounts c{};
    for (int side = 0; side < 2; ++side) {
        Restore(ctx, saved);
        gLog.clear();
        gRegistered.clear();
        if (side == 0) {
            for (uint32_t s = 0; s < slots; ++s)
                if (table[s]) table[s]();
        } else {
            c = winmainstart::RunInitialisersOwned(table.data(), table.data() + slots, ctx.image.Base(), owned, ownedCount);
        }
        log[side] = gLog;
        after[side] = Snapshot(ctx);
        registrations[side] = gRegistered.size();
        gLog.clear();
        for (auto it = gRegistered.rbegin(); it != gRegistered.rend(); ++it) (*it)();
        exitLog[side] = gLog;
    }
    Restore(ctx, saved);
    ++ctx.cases;
    if (c.owned != 11 || ownedCount != 11 || c.image || c.mismatched || c.unused)
        ctx.Fail("counts: %d ours, %d image, %d mismatched, %d unused", c.owned, c.image, c.mismatched, c.unused);
    if (log[0] != log[1]) ctx.Fail("constructor calls differ (%zu, %zu)", log[0].size(), log[1].size());
    if (after[0] != after[1]) ctx.Fail("memory differs after the run");
    if (registrations[0] != registrations[1] || registrations[0] != 5) ctx.Fail("registrations %zu / %zu (5)", registrations[0], registrations[1]);
    if (exitLog[0] != exitLog[1]) ctx.Fail("exit calls differ (%zu, %zu)", exitLog[0].size(), exitLog[1].size());
}
