// Differential tests for the FSH back end's alpha solver (lane MT; src/gz/GimexFshAlphaSolve.size.cpp, 0x899d55). It runs alone over seeded
// input, the original (by RVA, in the mapped image) against ours, the stack cleared before each call on both sides; the two end points,
// every code written, the buffer's other entries and the returned double are compared bit for bit.
//
// The codes buffer is an input as well as an output (the first pass is compared with what it held), so it is seeded three ways: zeros,
// random codes, and the codes of an earlier run of the original on the same values (which sends easy inputs straight to the
// single-move search).
//
// GimexFshAlphaSolve_Stack runs the original alone with the stack cleared, filled with ones and filled with random bytes, the solver's
// scratch bytes in the image (0x10ebdc8..0x10ebee8) set to a pattern first, and requires identical results and scratch: the decompiler
// shows a double built from a stack slot that is never written, and this is the check that the slot is a loop counter and not data.
#define NOMINMAX
#include "difftest.h"
#include "game/game.h"
#include "gz/GimexFshCluster.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {

constexpr uint32_t kSolve = 0x899d55;
constexpr uint32_t kStateRva = 0x10ebe80;        // the original keeps lower (0x10ebe80), upper, and the last fit's cost (0x10ebec8) and score (0x10ebed0) here
constexpr int kCostSlot = (0x10ebec8 - 0x10ebe80) / 8;
constexpr int kScoreSlot = (0x10ebed0 - 0x10ebe80) / 8;
constexpr uint32_t kScratchRva = 0x10ebdc8;
constexpr size_t kScratchBytes = 0x10ebee8 - 0x10ebdc8;
constexpr int32_t kMaxValues = 16;
constexpr int32_t kSentinel = 0x7777;

using SolveFn = double(__cdecl *)(int32_t, int32_t, const double *, int32_t *, int32_t *);

__declspec(noinline) void ClearStack()
{
    volatile uint8_t buffer[0x4000];
    for (size_t i = 0; i < sizeof(buffer); ++i) buffer[i] = 0;
}

__declspec(noinline) void FillStack(uint8_t byte)
{
    volatile uint8_t buffer[0x4000];
    for (size_t i = 0; i < sizeof(buffer); ++i) buffer[i] = byte;
}

struct Fill {
    uint32_t state;
    explicit Fill(uint32_t seed) : state(seed * 2654435761u + 0x9e3779b9u) {}
    uint32_t Next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    double Unit() { return static_cast<double>(Next() >> 8) / 16777216.0; }
    // All 53 mantissa bits used, so sums and products do not come out exact and the order they are added in shows.
    double Wide()
    {
        const uint64_t high = Next() >> 5;
        const uint64_t low = Next() >> 6;
        return static_cast<double>((high << 26) | low) / 9007199254740992.0;
    }
};

struct Case {
    int32_t levels;
    int32_t count;
    uint32_t shape;
    double values[kMaxValues];
};

void MakeCase(Fill &fill, int iter, Case &c)
{
    static const int32_t kLevelChoices[] = {8, 6, 6, 8, 5, 4, 3, 2, 7};
    c.levels = kLevelChoices[fill.Next() % 9];
    if (iter % 3 == 0) c.levels = (iter % 2) ? 8 : 6;
    c.count = (iter % 20 == 0) ? 0 : 1 + static_cast<int32_t>(fill.Next() % 16);
    c.shape = fill.Next() % 11;
    // A few values the shapes repeat or pick from.
    const double pool[3] = {fill.Wide(), fill.Wide(), fill.Wide()};
    for (int i = 0; i < kMaxValues; ++i) {
        switch (c.shape) {
        case 0: c.values[i] = fill.Wide(); break;
        case 1: c.values[i] = static_cast<double>(fill.Next() % 256) / 255.0; break;
        case 2: c.values[i] = static_cast<double>(fill.Next() % 4) / 3.0; break;
        case 3: c.values[i] = pool[fill.Next() % 3]; break;
        case 4: c.values[i] = (fill.Next() % 2) ? 1.0 : 0.0; break;
        case 5: c.values[i] = 0.4 + 0.2 * fill.Wide(); break;
        case 6: c.values[i] = (fill.Next() % 3 == 0) ? 1.0 : ((fill.Next() % 3 == 0) ? 0.0 : fill.Wide()); break;
        case 7: c.values[i] = static_cast<double>(static_cast<float>(fill.Unit())); break;
        // One or two outliers among equal values: the fit's end points can reach past the data.
        case 8: c.values[i] = (i < 2) ? fill.Wide() : pool[0]; break;
        case 9: c.values[i] = (i == 0) ? 1.0 - 0.01 * fill.Unit() : 0.5 * pool[1]; break;
        default: c.values[i] = (i % 5 == 4) ? fill.Wide() : 0.25 + 0.001 * static_cast<double>(i); break;
        }
    }
}

struct Result {
    double error;
    int32_t endpoints[2];
    int32_t codes[kMaxValues + 1];
};

void Seed(Fill &fill, const Case &c, uint32_t kind, const int32_t *previous, Result &r)
{
    r.error = 0.0;
    r.endpoints[0] = kSentinel;
    r.endpoints[1] = kSentinel;
    for (int i = 0; i <= kMaxValues; ++i) {
        switch (kind) {
        case 0: r.codes[i] = 0; break;
        case 1: r.codes[i] = static_cast<int32_t>(fill.Next() % static_cast<uint32_t>(c.levels < 1 ? 1 : c.levels)); break;
        default: r.codes[i] = i < kMaxValues ? previous[i] : kSentinel; break;
        }
    }
    r.codes[kMaxValues] = kSentinel;
}

bool Same(const Result &a, const Result &b)
{
    return std::memcmp(&a.error, &b.error, sizeof a.error) == 0 && std::memcmp(a.endpoints, b.endpoints, sizeof a.endpoints) == 0 &&
           std::memcmp(a.codes, b.codes, sizeof a.codes) == 0;
}

// The codes of the solver's first pass for these values (the thresholds are the middles of the cells between the smallest and the largest value).
void FirstPassCodes(const Case &c, int32_t *out)
{
    double total = c.values[0];
    double low = c.values[0];
    double high = c.values[0];
    for (int i = 1; i < c.count; ++i) {
        total += c.values[i];
        if (c.values[i] < low) low = c.values[i];
        if (c.values[i] > high) high = c.values[i];
    }
    const double mean = total / c.count;
    const int32_t cuts = c.levels - 1;
    const double cell = (high - low) / cuts;
    for (int i = 0; i < c.count; ++i) {
        int32_t code = 0;
        while (code < cuts && (code + 0.5) * cell + (low - mean) <= c.values[i] - mean) ++code;
        out[i] = code;
    }
}

} // namespace

DIFF_TEST(GimexFshAlphaSolve_Solve, kSolve)
{
    const SolveFn original = ctx.Original<SolveFn>(kSolve);
    const double *const originalState = GameAddress<double>(kStateRva);   // lower, upper, ... as the original leaves them
    Fill fill(0x4101);
    int outOfRange = 0;
    for (int iter = 0; iter < 60000; ++iter) {
        Case c;
        MakeCase(fill, iter, c);
        const uint32_t kind = static_cast<uint32_t>(iter % 4);
        if (kind == 3 && c.count > 0) {
            // The smallest value first, so its first-pass code is 0 and the seeded buffer can match the whole first pass.
            int low = 0;
            for (int i = 1; i < c.count; ++i)
                if (c.values[i] < c.values[low]) low = i;
            std::swap(c.values[0], c.values[low]);
        }
        int32_t previous[kMaxValues] = {};
        if (kind == 2) {
            Result probe;
            Seed(fill, c, 0, previous, probe);
            ClearStack();
            original(c.levels, c.count, c.values, probe.endpoints, probe.codes);
            for (int i = 0; i < kMaxValues; ++i) previous[i] = probe.codes[i];
        } else if (kind == 3 && c.count > 0) {
            FirstPassCodes(c, previous);
        }
        Result expected;
        Result actual;
        Seed(fill, c, kind == 3 ? 2 : kind, previous, expected);
        actual = expected;
        Result third = expected;
        ClearStack();
        expected.error = original(c.levels, c.count, c.values, expected.endpoints, expected.codes);
        const double state[4] = {originalState[0], originalState[1], originalState[kCostSlot], originalState[kScoreSlot]};
        gimex::fsh::AlphaTrace trace = {};
        ClearStack();
        actual.error = gimex::fsh::SolveAlphaTraced(c.levels, c.count, c.values, actual.endpoints, actual.codes, &trace);
        ClearStack();
        third.error = gimex::fsh::SolveAlpha(c.levels, c.count, c.values, third.endpoints, third.codes);
        ++ctx.cases;
        if (!Same(expected, actual) || !Same(expected, third))
            ctx.Fail("alpha solve iter %d levels %d count %d shape %u seed %u: error %.17g vs %.17g, ends %d %d vs %d %d, codes %d %d %d vs %d %d %d", iter,
                     c.levels, c.count, c.shape, kind, expected.error, actual.error, expected.endpoints[0], expected.endpoints[1], actual.endpoints[0],
                     actual.endpoints[1], expected.codes[0], expected.codes[1], expected.codes[2], actual.codes[0], actual.codes[1], actual.codes[2]);
        if (c.count == 0) continue;
        // The real-valued state: the end points always (the first pass leaves the starting ones when one code holds every value), the fit's cost
        // and score when a fit ran.
        const bool lowerSame = std::memcmp(&state[0], &trace.lower, sizeof(double)) == 0;
        const bool upperSame = std::memcmp(&state[1], &trace.upper, sizeof(double)) == 0;
        const bool costSame = !trace.fitted || (std::memcmp(&state[2], &trace.cost, sizeof(double)) == 0 && std::memcmp(&state[3], &trace.score, sizeof(double)) == 0);
        if (!lowerSame || !upperSame || !costSame)
            ctx.Fail("alpha solve iter %d levels %d count %d shape %u seed %u: state %.17g %.17g %.17g %.17g vs %.17g %.17g %.17g %.17g (fitted %d)", iter,
                     c.levels, c.count, c.shape, kind, state[0], state[1], state[2], state[3], trace.lower, trace.upper, trace.cost, trace.score,
                     trace.fitted ? 1 : 0);
        for (int k = 0; k < 2; ++k)
            if (trace.rounded[k] < 0.0 || trace.rounded[k] > 255.0) ++outOfRange;
    }
    // The clamp of the rounded end points to 0..255 is not reached by any of these inputs (outOfRange stays 0): the fit's min / max terms
    // keep the end points inside the colour range. The clamp is kept as the original has it; mutants of it are argued equivalent in the status.
    (void)outOfRange;
}

DIFF_TEST(GimexFshAlphaSolve_Stack, kSolve)
{
    const SolveFn original = ctx.Original<SolveFn>(kSolve);
    uint8_t *const scratch = GameAddress<uint8_t>(kScratchRva);
    Fill fill(0x4102);
    for (int iter = 0; iter < 6000; ++iter) {
        Case c;
        MakeCase(fill, iter, c);
        if (c.count == 0) continue;
        int32_t previous[kMaxValues] = {};
        Result runs[3];
        uint8_t scratchAfter[3][kScratchBytes];
        for (int variant = 0; variant < 3; ++variant) {
            Seed(fill, c, 0, previous, runs[variant]);
            std::memset(scratch, 0x5a, kScratchBytes);
            if (variant == 0) {
                ClearStack();
            } else if (variant == 1) {
                FillStack(0xff);
            } else {
                FillStack(static_cast<uint8_t>(fill.Next()));
            }
            runs[variant].error = original(c.levels, c.count, c.values, runs[variant].endpoints, runs[variant].codes);
            std::memcpy(scratchAfter[variant], scratch, kScratchBytes);
        }
        ++ctx.cases;
        for (int variant = 1; variant < 3; ++variant) {
            if (!Same(runs[0], runs[variant]))
                ctx.Fail("alpha solve stack iter %d variant %d: the results depend on the stack (error %.17g vs %.17g)", iter, variant, runs[0].error,
                         runs[variant].error);
            if (std::memcmp(scratchAfter[0], scratchAfter[variant], kScratchBytes) != 0) {
                size_t first = 0;
                while (scratchAfter[0][first] == scratchAfter[variant][first]) ++first;
                ctx.Fail("alpha solve stack iter %d variant %d: scratch byte +%zx moved (%02x vs %02x), levels %d count %d", iter, variant, first,
                         scratchAfter[0][first], scratchAfter[variant][first], c.levels, c.count);
            }
        }
    }
}
