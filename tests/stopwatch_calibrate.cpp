// Differential tests for two more cRZStopwatch functions: SetCount (0x1c960f) and the start-up
// calibration's frequency probe MeasureFrequency (0x1c906a).
//
// Every clock they touch is scripted. The original reads QueryPerformanceCounter, QueryPerformance-
// Frequency and SleepEx through its import slots, which are pointed at fakes; ours reads the same
// slots. The time-stamp counter helper at 0x1c9244 (cpuid, rdtsc) is reached by direct calls: its
// first five bytes in the mapped original are replaced by a jump to a fake for the life of a test,
// and ours reads it through cRZStopwatch::sTimeStampCounterOverride. Both sides get the same random
// script (a generator reseeded for each side) and every fake logs what it was asked and gave, so a
// different order or number of reads shows in the log.
#define NOMINMAX
#include "difftest.h"
#include "rz/cRZStopwatch.h"

#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kIatQpc = 0xd9a24c, kIatQpf = 0xd9a250, kIatSleepEx = 0xd9a188;
constexpr uint32_t kReadTsc = 0x1c9244, kSetCount = 0x1c960f, kMeasure = 0x1c906a, kCtor = 0x1c9030;

std::mt19937_64 gRng;
std::vector<std::string> gLog;
uint64_t gQpc = 0, gTsc = 0, gQpf = 0, gQpcStep = 50;
int gTscMode = 0; // 0: a ratio of the performance counter; 1: free-running steps
const double kRatios[] = {0.0, 0.5, 0.97, 0.9799, 0.98, 0.9801, 1.0, 1.0199, 1.02, 1.0201, 1.1, 3.0};
int gRatioPick = -1; // >= 0: always this ratio (or the exact boundaries below)

void Log(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.emplace_back(buf);
}

std::string Joined()
{
    std::string out;
    for (const std::string &s : gLog) out += s + "|";
    return out;
}

void AdvanceQpc()
{
    gQpc += gQpcStep * (1 + gRng() % 20);
}

BOOL WINAPI FakeQpc(uint64_t *out)
{
    AdvanceQpc();
    *out = gQpc;
    Log("qpc %llx", gQpc);
    return TRUE;
}
BOOL WINAPI FakeQpf(uint64_t *out)
{
    *out = gQpf;
    Log("qpf %llx", gQpf);
    return TRUE;
}
DWORD WINAPI FakeSleepEx(DWORD ms, BOOL alertable)
{
    gQpc += gQpcStep * (1 + gRng() % 20) * (1 + gRng() % 3);
    Log("sleep %u %d -> qpc %llx", ms, alertable, gQpc);
    return 0;
}
void __cdecl FakeTsc(uint64_t *out)
{
    if (gTscMode == 0) {
        // Exact boundary ratios need the counter to be a multiple of 50 (it is: the steps are).
        double r = kRatios[gRng() % (sizeof(kRatios) / sizeof(kRatios[0]))];
        uint64_t t;
        if (r == 0.98) t = gQpc / 50 * 49;
        else if (r == 1.02) t = gQpc / 50 * 51;
        else t = static_cast<uint64_t>(static_cast<double>(static_cast<int64_t>(gQpc)) * r);
        gTsc = t;
    } else {
        gTsc += (gRng() % 5 == 0 ? (1ull << 62) : (1ull << 40)) * (1 + gRng() % 3) + gRng() % 1000;
    }
    *out = gTsc;
    Log("tsc %llx", gTsc);
}

struct Fakes {
    difftest::Context &ctx;
    uint8_t *site;
    uint8_t saved[5];
    void *savedSlots[3];
    explicit Fakes(difftest::Context &c) : ctx(c), site(c.image.At<uint8_t>(kReadTsc))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&FakeTsc) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
        const uint32_t slots[3] = {kIatQpc, kIatQpf, kIatSleepEx};
        void *fakes[3] = {reinterpret_cast<void *>(&FakeQpc), reinterpret_cast<void *>(&FakeQpf), reinterpret_cast<void *>(&FakeSleepEx)};
        for (int i = 0; i < 3; ++i) {
            void **slot = ctx.image.At<void *>(slots[i]);
            savedSlots[i] = *slot;
            *slot = fakes[i];
        }
        cRZStopwatch::sTimeStampCounterOverride = &FakeTsc;
        cRZStopwatch::sPerformanceCounterOverride = [](uint64_t *out) { FakeQpc(out); };
    }
    ~Fakes()
    {
        std::memcpy(site, saved, 5);
        const uint32_t slots[3] = {kIatQpc, kIatQpf, kIatSleepEx};
        for (int i = 0; i < 3; ++i) *ctx.image.At<void *>(slots[i]) = savedSlots[i];
        cRZStopwatch::sTimeStampCounterOverride = nullptr;
        cRZStopwatch::sPerformanceCounterOverride = nullptr;
    }
};

} // namespace

DIFF_TEST(cRZStopwatch_SetCount, kSetCount)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, int64_t)>(kSetCount);
    auto ctor = ctx.Original<cRZStopwatch *(__thiscall *)(void *, int32_t)>(kCtor);
    Fakes fakes(ctx);
    std::mt19937_64 rng(0x1c960f);
    float savedScales[5];
    std::memcpy(savedScales, ctx.image.At<float>(cRZStopwatch::kInverseScalesRva), sizeof(savedScales));
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const float k2p63 = 9223372036854775808.0f;
    for (int run = 0; run < 20000; ++run) {
        int32_t mode = run % 10; // 0..9
        if (run % 37 == 5) mode = -1 - run % 4;
        float scales[5];
        for (float &s : scales) {
            switch (rng() % 12) {
            case 0: s = 0.0f; break;
            case 1: s = 1.0f; break;
            case 2: s = nan; break;
            case 3: s = inf; break;
            case 4: s = -1.0f; break;
            case 5: s = 1e-3f * (1 + rng() % 5000); break;
            case 6: s = 1e10f; break;
            case 7: s = 3e18f * (1 + rng() % 7); break;
            case 8: s = 0.5f; break;
            default: s = 1e-6f * (1 + rng() % 4000000); break;
            }
        }
        int64_t units;
        switch (rng() % 8) {
        case 0: units = 0; break;
        case 1: units = std::numeric_limits<int64_t>::min(); break;
        case 2: units = std::numeric_limits<int64_t>::max(); break;
        case 3: units = static_cast<int64_t>(rng() >> (1 + rng() % 63)); break;
        case 4: units = -static_cast<int64_t>(rng() >> (1 + rng() % 63)); break;
        case 5: units = 1 + static_cast<int64_t>(rng() % 100); break;
        default: units = static_cast<int64_t>(rng() >> (1 + rng() % 40)); break;
        }
        if (run % 5 == 2 && units != 0 && mode >= 2 && mode <= 6) {
            // Land the scaled value on and around 2^63 and 2^64: scale = bound / units (in float).
            float bound = rng() % 2 ? k2p63 : 2.0f * k2p63;
            float s = bound / static_cast<float>(static_cast<double>(units));
            for (int step = static_cast<int>(rng() % 3); step > 0; --step) s = std::nextafterf(s, rng() % 2 ? inf : -inf);
            scales[mode - 2] = s;
        }
        std::memcpy(ctx.image.At<float>(cRZStopwatch::kInverseScalesRva), scales, sizeof(scales));
        // The word after the five scales is something else: nonzero, so a mode that reads it would show.
        ctx.image.At<float>(cRZStopwatch::kInverseScalesRva)[5] = 7.0f;
        cRZStopwatch a, b;
        ctor(&a, mode);
        b.Construct(mode);
        a.mUnused = b.mUnused = 0x2468;
        a.mAccumulated = b.mAccumulated = rng();
        a.mStart = b.mStart = rng();
        const uint64_t startQpc = 0x1000 + (rng() & 0xffffff);
        gQpc = startQpc;
        gTscMode = 0;
        gRng.seed(run);
        gLog.clear();
        orig(&a, units);
        std::string logA = Joined();
        gLog.clear();
        gQpc = startQpc;
        gRng.seed(run);
        b.SetCount(units);
        std::string logB = Joined();
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0 || logA != logB || logA.empty()) {
            ctx.Fail("mode %d units %lld scale %g: accumulated %llx/%llx start %llx/%llx log %s / %s", mode, static_cast<long long>(units),
                     mode >= 2 && mode <= 6 ? scales[mode - 2] : 0.0f, a.mAccumulated, b.mAccumulated, a.mStart, b.mStart, logA.c_str(), logB.c_str());
        }
    }
    std::memcpy(ctx.image.At<float>(cRZStopwatch::kInverseScalesRva), savedScales, sizeof(savedScales));
}

DIFF_TEST(cRZStopwatch_MeasureFrequency, kMeasure)
{
    auto orig = ctx.Original<void(__cdecl *)(uint64_t *, uint64_t *)>(kMeasure);
    Fakes fakes(ctx);
    std::mt19937_64 rng(0x1c906a);
    uint64_t **cacheGlobal = ctx.image.At<uint64_t *>(cRZStopwatch::kFrequencyCacheRva);
    uint64_t *savedCache = *cacheGlobal;
    for (int run = 0; run < 6000; ++run) {
        switch (rng() % 8) {
        case 0: gQpf = 0; break;
        case 1: gQpf = 1; break;
        case 2: gQpf = 50; break;
        case 3: gQpf = 10000000; break;
        case 4: gQpf = 1ull << 32; break;
        case 5: gQpf = rng() >> 24; break;
        case 6: gQpf = rng() >> (1 + rng() % 63); break;
        default: gQpf = 3000000 + rng() % 100; break;
        }
        gQpcStep = 50 * (1 + rng() % (gQpf / 200 + 2));
        gTscMode = rng() % 4 == 0 ? 1 : 0;
        uint64_t startQpc = (rng() >> (rng() % 40)) / 50 * 50;
        uint64_t startTsc = rng() >> (rng() % 40);
        int cacheKind = static_cast<int>(rng() % 3); // 0: no cache; 1: cache holding zero; 2: cache holding a value
        uint64_t cacheValue = cacheKind == 2 ? (rng() >> (rng() % 40)) | 1 : 0;
        uint64_t results[2], frequencies[2], caches[2];
        std::string logs[2];
        for (int side = 0; side < 2; ++side) {
            uint64_t cache = cacheValue;
            *cacheGlobal = cacheKind ? &cache : nullptr;
            gQpc = startQpc;
            gTsc = startTsc;
            gRng.seed(run);
            gLog.clear();
            // Two cells with canaries after each.
            alignas(8) uint64_t performance[2] = {0x1111111111111111ull, 0x3333333333333333ull};
            alignas(8) uint64_t out[2] = {0x2222222222222222ull, 0x4444444444444444ull};
            if (side == 0) orig(performance, out);
            else cRZStopwatch::MeasureFrequency(performance, out);
            results[side] = out[0];
            frequencies[side] = performance[0];
            caches[side] = cache;
            logs[side] = Joined();
            if (out[1] != 0x4444444444444444ull || performance[1] != 0x3333333333333333ull) logs[side] += "overrun|";
        }
        *cacheGlobal = nullptr;
        ++ctx.cases;
        if (results[0] != results[1] || frequencies[0] != frequencies[1] || caches[0] != caches[1] || logs[0] != logs[1]) {
            ctx.Fail("run %d qpf %llx cache %d: out %llx/%llx freq %llx/%llx cache %llx/%llx log %s / %s", run, gQpf, cacheKind, results[0], results[1],
                     frequencies[0], frequencies[1], caches[0], caches[1], logs[0].c_str(), logs[1].c_str());
        }
    }
    *cacheGlobal = savedCache;
}
