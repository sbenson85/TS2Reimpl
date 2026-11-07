// cRZStopwatchCalibration::Construct (0x1c8d8e; src/rz/cRZStopwatchCalibration.h, lane HQ): the stopwatch's start-up
// calibration against the image's.
//
// Stood in on both sides (shape::StandIn: the image's function and our replacement of it): MeasureFrequency (writes a
// scripted QueryPerformanceFrequency answer and time-stamp rate, and logs the two pointers it got), ReadPerformanceCounter
// and ReadTsc (scripted sequences, logged); the priority imports (GetCurrentProcess, GetPriorityClass, GetCurrentThread,
// GetThreadPriority, SetPriorityClass, SetThreadPriority) are bound in the image's import table, which ours calls through
// too, to logging fakes with scripted answers. Per case: frequencies (zero, one, the usual 10 MHz / 3.58 MHz, CPU rates,
// past 2^31, past 2^63, all ones) and counter sequences (equal reads, small and large spans, wrapped spans: second below
// first); the timebases and frequency globals start as junk with 8-byte margins. Compared: the window over both timebases
// and the object (0x10cb638..0x10cb6b0), the frequency globals with margins, the returned pointer, the full call log.
#include "difftest.h"
#include "code_patch.h"
#include "game/game.h"
#include "game/import_slots.h"
#include "impfx.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZStopwatchCalibration.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <vector>

namespace {

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

std::vector<uint64_t> gLog; // (code << 56) | value
uint64_t gQpf = 0, gTscRate = 0;
std::vector<uint64_t> gQpc, gTsc;
size_t gNextQpc = 0, gNextTsc = 0;
uint32_t gPriorityClass = 0;
int gThreadPriority = 0;
uintptr_t gImageBase = 0;

void Log(uint64_t code, uint64_t value) { gLog.push_back(code << 56 ^ value); }
uint64_t Rva(const void *p) { return reinterpret_cast<uintptr_t>(p) - gImageBase; }

void __cdecl StandMeasure(uint64_t *performanceFrequency, uint64_t *out)
{
    Log(1, Rva(performanceFrequency));
    Log(2, Rva(out));
    *out = gTscRate;
    *performanceFrequency = gQpf;
}
int32_t __cdecl StandQpc(uint64_t *out)
{
    *out = gQpc[gNextQpc++ % gQpc.size()];
    Log(3, *out);
    return TRUE;
}
void __cdecl StandTsc(uint64_t *out)
{
    *out = gTsc[gNextTsc++ % gTsc.size()];
    Log(4, *out);
}
HANDLE WINAPI FakeGetCurrentProcess()
{
    Log(5, 0);
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));
}
DWORD WINAPI FakeGetPriorityClass(HANDLE h)
{
    Log(6, reinterpret_cast<uintptr_t>(h));
    return gPriorityClass;
}
HANDLE WINAPI FakeGetCurrentThread()
{
    Log(7, 0);
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2));
}
int WINAPI FakeGetThreadPriority(HANDLE h)
{
    Log(8, reinterpret_cast<uintptr_t>(h));
    return gThreadPriority;
}
BOOL WINAPI FakeSetPriorityClass(HANDLE h, DWORD c)
{
    Log(9, reinterpret_cast<uintptr_t>(h));
    Log(10, c);
    return TRUE;
}
BOOL WINAPI FakeSetThreadPriority(HANDLE h, int p)
{
    Log(11, reinterpret_cast<uintptr_t>(h));
    Log(12, static_cast<uint32_t>(p));
    return TRUE;
}

constexpr uint32_t kWindowFirst = 0x10cb638, kWindowEnd = 0x10cb6b0;
constexpr uint32_t kFreqFirst = 0xff2478, kFreqEnd = 0xff2498;

std::vector<uint8_t> Read(const difftest::Context &ctx, uint32_t first, uint32_t end)
{
    const uint8_t *p = ctx.image.At<uint8_t>(first);
    return {p, p + (end - first)};
}
void Write(const difftest::Context &ctx, uint32_t first, const std::vector<uint8_t> &v)
{
    std::memcpy(ctx.image.At<uint8_t>(first), v.data(), v.size());
}

} // namespace

DIFF_TEST(cRZStopwatch_Calibration, 0x1c8d8e)
{
    gImageBase = reinterpret_cast<uintptr_t>(ctx.image.At<void>(0));
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x1c8d8e);
    shape::StandIn measure(0x1c906a, &cRZStopwatch::MeasureFrequency, &StandMeasure);
    shape::StandIn qpc(0x1c95c4, &cRZStopwatch::ReadPerformanceCounter, &StandQpc);
    shape::StandIn tsc(0x1c9244, &cRZStopwatch::ReadTsc, &StandTsc);
    BindImport b1(ctx, 0xd9a1b8, reinterpret_cast<const void *>(&FakeGetCurrentProcess));
    BindImport b2(ctx, 0xd9a258, reinterpret_cast<const void *>(&FakeGetPriorityClass));
    BindImport b3(ctx, 0xd9a190, reinterpret_cast<const void *>(&FakeGetCurrentThread));
    BindImport b4(ctx, 0xd9a19c, reinterpret_cast<const void *>(&FakeGetThreadPriority));
    BindImport b5(ctx, 0xd9a254, reinterpret_cast<const void *>(&FakeSetPriorityClass));
    BindImport b6(ctx, 0xd9a198, reinterpret_cast<const void *>(&FakeSetThreadPriority));
    const auto savedWindow = Read(ctx, kWindowFirst, kWindowEnd);
    const auto savedFreq = Read(ctx, kFreqFirst, kFreqEnd);
    void *object = ctx.image.At<void>(cRZStopwatchCalibration::kRva);

    const uint64_t rates[] = {0, 1, 3579545, 10000000, 1000000000, 2400000000ull, 0x80000000ull, 0x7fffffffffffffffull,
                              0x8000000000000000ull, ~0ull, 16777217, 0x20000000000001ull};
    std::mt19937_64 rng(0x1c8d8e);
    for (int c = 0; c < 400; ++c) {
        ++ctx.cases;
        gQpf = c < 12 ? rates[c] : (c & 1 ? rng() >> (rng() % 64) : rates[rng() % std::size(rates)]);
        gTscRate = c < 12 ? rates[11 - c] : (c & 2 ? rng() >> (rng() % 64) : rates[rng() % std::size(rates)]);
        gPriorityClass = static_cast<uint32_t>(rng());
        gThreadPriority = static_cast<int>(rng());
        for (auto *seq : {&gQpc, &gTsc}) {
            seq->assign(16, 0);
            uint64_t base = rng();
            for (size_t i = 0; i < 16; i += 2) {
                (*seq)[i] = base + rng() % 1000;
                switch (rng() % 5) {
                case 0: (*seq)[i + 1] = (*seq)[i]; break;                        // no span
                case 1: (*seq)[i + 1] = (*seq)[i] - 1 - rng() % 100; break;      // wrapped (huge unsigned span)
                case 2: (*seq)[i + 1] = (*seq)[i] + rng(); break;                // large
                default: (*seq)[i + 1] = (*seq)[i] + rng() % 300; break;         // small
                }
                if (c % 7 == 0) (*seq)[i + 1] = (*seq)[i] + 0x100000000ull + rng() % 3; // spans past 32 bits
            }
        }
        std::vector<uint8_t> window(kWindowEnd - kWindowFirst), freq(kFreqEnd - kFreqFirst);
        for (auto &b : window) b = static_cast<uint8_t>(rng());
        for (auto &b : freq) b = static_cast<uint8_t>(rng());

        Write(ctx, kWindowFirst, window);
        Write(ctx, kFreqFirst, freq);
        gLog.clear();
        gNextQpc = gNextTsc = 0;
        void *a = orig(object);
        const auto windowA = Read(ctx, kWindowFirst, kWindowEnd), freqA = Read(ctx, kFreqFirst, kFreqEnd);
        const auto logA = gLog;

        Write(ctx, kWindowFirst, window);
        Write(ctx, kFreqFirst, freq);
        gLog.clear();
        gNextQpc = gNextTsc = 0;
        void *b = GameAddress<cRZStopwatchCalibration>(cRZStopwatchCalibration::kRva)->Construct();
        const auto windowB = Read(ctx, kWindowFirst, kWindowEnd), freqB = Read(ctx, kFreqFirst, kFreqEnd);

        if (a != b || a != object) ctx.Fail("case %d: returned %p, ours %p", c, a, b);
        for (size_t i = 0; i < windowA.size(); ++i)
            if (windowA[i] != windowB[i]) {
                ctx.Fail("case %d (qpf %llx tsc %llx): byte %08zx is %02x, ours %02x", c, gQpf, gTscRate,
                         kWindowFirst + i, windowA[i], windowB[i]);
                break;
            }
        if (freqA != freqB) ctx.Fail("case %d: the frequency globals differ", c);
        if (logA != gLog) {
            ctx.Fail("case %d: call logs differ (%zu vs %zu)", c, logA.size(), gLog.size());
            for (size_t i = 0; i < logA.size() || i < gLog.size(); ++i)
                ctx.Fail("  %016llx | %016llx", i < logA.size() ? logA[i] : 0ull, i < gLog.size() ? gLog[i] : 0ull);
            break;
        }
    }
    Write(ctx, kWindowFirst, savedWindow);
    Write(ctx, kFreqFirst, savedFreq);
}

// ---- the calibration whole (lane FA): Construct with the real MeasureFrequency and counter reads on both sides ----
// The Win32 side is impfx's (tests/impfx.h): Clock() answers QueryPerformanceCounter / Frequency from one counter that
// each read advances by perfStep, Threads() gives SleepEx (it advances the counter by ms * frequency / 1000) and the
// thread priority calls, and the process priority calls and GetCurrentThread are plain recording fakes. Both sides call
// through the image's import table (ours through its owned slots, which the harness resolves to the image's). Only the
// time-stamp counter read (0x1c9244, cpuid / rdtsc) is stood in on both sides: it answers from impfx's counter, scaled
// (rate num / den: equal, inside and outside MeasureFrequency's 0.98..1.02 window) plus a per-read step, so both of
// MeasureFrequency's paths run (the ratio test taking QueryPerformanceFrequency, or the SleepEx loop measuring the rate).
// Per case also the frequency cache (the cRZTimer view at 0x10cb63c): none, a zeroed or a nonzero cell. Compared: the
// bits of both timebases and the object (0x10cb638..0x10cb6b0, junk-filled first, so x87 and SSE rounding differences
// show as bits, not tolerances), the frequency cells with margins, the cache cell, the returned pointer and the whole
// call log. First, the owned statics' identity: each is the image's cell.

namespace {

uint64_t gRateNum = 1, gRateDen = 1, gStampOffset = 0, gStampStep = 0, gStampReads = 0;

void __cdecl ScaledTsc(uint64_t *out)
{
    const uint64_t perf = impfx::gState.perf;
    *out = gStampOffset + perf / gRateDen * gRateNum + perf % gRateDen * gRateNum / gRateDen + gStampReads++ * gStampStep;
    drawfx::Note("tsc", {uint32_t(*out), uint32_t(*out >> 32)});
}

bool SameCell(const difftest::Context &ctx, const void *owned, uint32_t rva) { return owned == ctx.image.At<void>(rva); }

} // namespace

DIFF_TEST(cRZStopwatch_CalibrationWhole, 0x1c8d8e)
{
    struct Owned {
        const void *cell;
        uint32_t rva;
    } owned[] = {{&ts2_rva_10cb640, 0x10cb640}, {&ts2_rva_10cb648, 0x10cb648}, {&ts2_rva_10cb678, 0x10cb678},
                 {&ts2_rva_ff2480, 0xff2480},   {&ts2_rva_ff2488, 0xff2488},   {&ts2_rva_10cb63c, 0x10cb63c},
                 {&ts2_rva_d9a188, 0xd9a188},   {&ts2_rva_d9a190, 0xd9a190},   {&ts2_rva_d9a198, 0xd9a198},
                 {&ts2_rva_d9a19c, 0xd9a19c},   {&ts2_rva_d9a1b8, 0xd9a1b8},   {&ts2_rva_d9a24c, 0xd9a24c},
                 {&ts2_rva_d9a250, 0xd9a250},   {&ts2_rva_d9a254, 0xd9a254},   {&ts2_rva_d9a258, 0xd9a258}};
    for (const Owned &o : owned)
        if (!SameCell(ctx, o.cell, o.rva)) ctx.Fail("ts2_rva_%x is not the image's cell", o.rva);

    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x1c8d8e);
    impfx::Imports imp(ctx);
    imp.Clock();
    imp.Threads();
    IMPFX_ADD(imp, GetCurrentProcess).Returns(0xffffffff);
    IMPFX_ADD(imp, GetCurrentThread).Returns(0xfffffffe);
    IMPFX_ADD(imp, GetPriorityClass);
    IMPFX_ADD(imp, SetPriorityClass).Returns(TRUE);
    shape::StandIn tsc(0x1c9244, &cRZStopwatch::ReadTsc, &ScaledTsc);
    const auto savedWindow = Read(ctx, kWindowFirst, kWindowEnd);
    const auto savedFreq = Read(ctx, kFreqFirst, kFreqEnd);
    void *object = ctx.image.At<void>(cRZStopwatchCalibration::kRva);
    uint64_t **cacheCell = ctx.image.At<uint64_t *>(cRZStopwatch::kFrequencyCacheRva);
    uint64_t cache = 0;

    const uint64_t frequencies[] = {3579545, 10000000, 1000, 1193182, 2400000000ull, 1ull << 40, 24000000, 14318180};
    const uint64_t rates[][2] = {{1, 1}, {99, 100}, {101, 100}, {49, 50}, {51, 50}, {98, 100}, {102, 100},
                                 {240, 1}, {3, 1}, {1, 3}, {1000, 1}, {7, 5}};
    std::mt19937_64 rng(0xfa1c8d8e);
    for (int c = 0; c < 400; ++c) {
        auto &s = imp.settings();
        s.perfFrequency = frequencies[c % std::size(frequencies)];
        s.perfCount = c % 5 == 0 ? 0 : rng() >> (1 + rng() % 30);
        s.perfStep = c % 4 == 0 ? 0 : rng() % 400;
        const auto &rate = rates[(c / 3) % std::size(rates)];
        gRateNum = rate[0];
        gRateDen = rate[1];
        const uint64_t offset = c % 3 == 0 ? 0 : rng() >> (rng() % 64);
        const uint64_t step = c % 6 == 0 ? 0 : rng() % 1000;
        const uint32_t priorityClass = c % 2 ? NORMAL_PRIORITY_CLASS : static_cast<uint32_t>(rng());
        const int cacheKind = c % 3; // none, zero, nonzero
        const uint64_t cacheValue = cacheKind == 2 ? rng() : 0;

        std::vector<uint8_t> window(kWindowEnd - kWindowFirst), freq(kFreqEnd - kFreqFirst);
        for (auto &b : window) b = static_cast<uint8_t>(rng());
        for (auto &b : freq) b = static_cast<uint8_t>(rng());

        std::vector<uint8_t> windowSide[2], freqSide[2];
        uint64_t cacheSide[2] = {};
        void *result[2] = {};
        drawfx::Log logs[2];
        std::vector<std::string> errors[2];
        for (int side = 0; side < 2; ++side) {
            auto r = drawfx::Side(
                [&] {
                    imp.BeginSide();
                    imp.Get("GetPriorityClass").Returns(priorityClass);
                    gStampOffset = offset;
                    gStampStep = step;
                    gStampReads = 0;
                    Write(ctx, kWindowFirst, window);
                    Write(ctx, kFreqFirst, freq);
                    cache = cacheValue;
                    *cacheCell = cacheKind == 0 ? nullptr : &cache;
                },
                [&] {
                    result[side] = side == 0 ? orig(object) : ts2_rva_10cb640.Construct();
                    cacheSide[side] = cache;
                    windowSide[side] = Read(ctx, kWindowFirst, kWindowEnd);
                    freqSide[side] = Read(ctx, kFreqFirst, kFreqEnd);
                });
            logs[side] = std::move(r.first);
            errors[side] = std::move(r.second);
        }
        ++ctx.cases;
        if (result[0] != result[1] || result[0] != object) ctx.Fail("case %d: returned %p, ours %p", c, result[0], result[1]);
        for (size_t i = 0; i < windowSide[0].size(); ++i)
            if (windowSide[0][i] != windowSide[1][i]) {
                ctx.Fail("case %d (qpf %llx rate %llu/%llu): byte %08zx is %02x, ours %02x", c, s.perfFrequency, gRateNum,
                         gRateDen, kWindowFirst + i, windowSide[0][i], windowSide[1][i]);
                break;
            }
        if (freqSide[0] != freqSide[1]) ctx.Fail("case %d: the frequency cells differ", c);
        if (cacheSide[0] != cacheSide[1]) ctx.Fail("case %d: the cache %llx, ours %llx", c, cacheSide[0], cacheSide[1]);
        if (!drawfx::Same(ctx, logs[0], logs[1], "calibration", c, errors[1].empty() ? errors[0] : errors[1])) break;
    }
    *cacheCell = nullptr;
    Write(ctx, kWindowFirst, savedWindow);
    Write(ctx, kFreqFirst, savedFreq);
}
