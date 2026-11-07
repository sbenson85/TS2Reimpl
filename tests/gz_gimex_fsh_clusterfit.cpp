// Differential tests for the FSH back end's cluster searches (lane MT; src/gz/GimexFshClusterFit.size.cpp): the ordered cluster fit
// 0x89c480 and its bounded variant 0x89cacf. Each runs alone over seeded input, the original (by RVA, in the mapped image) against ours,
// the stack cleared before each call on both sides; every written byte (the end points, the axis, *levels) and the returned double are
// compared bit for bit.
//
// The originals keep their best score in image globals (0x105de90 and 0x10e7c28; 0x105de98 and 0x10e7ea8) that nothing resets, and
// their scratch tables (0x10e79b0..0x10e7c28, 0x10e7c30..0x10e7ea8) are read by no other function (xrefs). Ours have no carried state
// (the first split of every call replaces it, see the source), so the tests set the globals to a different finite pair before each
// call - the initial pair, the previous call's result, extreme pairs - and the results must still agree.
#define NOMINMAX
#include "difftest.h"
#include "game/game.h"
#include "gz/GimexFshCluster.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kClusterFit = 0x89c480;
constexpr uint32_t kBoundedClusterFit = 0x89cacf;
constexpr uint32_t kBestScoreRva = 0x105de90;
constexpr uint32_t kBestCostRva = 0x10e7c28;
constexpr uint32_t kBoundedBestScoreRva = 0x105de98;
constexpr uint32_t kBoundedBestCostRva = 0x10e7ea8;

using ClusterFn = double(__cdecl *)(int32_t *, int32_t, const double *, const double *, const int32_t *, double *, double *);
using BoundedFn = double(__cdecl *)(int32_t *, int32_t, const double *, const double *, const int32_t *, const double *, double *, double *);

__declspec(noinline) void ClearStack()
{
    volatile uint8_t buffer[0x4000];
    for (size_t i = 0; i < sizeof(buffer); ++i) buffer[i] = 0;
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
    double Signed() { return Unit() * 2.0 - 1.0; }
    // All 53 mantissa bits used (Unit has 24, so sums and products of those are mostly exact and hide the order they are added in).
    double Wide()
    {
        const uint64_t high = Next() >> 5;
        const uint64_t low = Next() >> 6;
        return static_cast<double>((high << 26) | low) / 9007199254740992.0 * 2.0 - 1.0;
    }
};

struct Case {
    int32_t count;
    uint32_t shape;
    double accumulated[16][3];
    double weights[16];
    int32_t order[16];
    int32_t levels;
};

void MakeCase(Fill &fill, int iter, Case &c)
{
    c.count = 1 + static_cast<int32_t>(fill.Next() % 16);
    c.shape = fill.Next() % 6;
    for (int i = 0; i < 16; ++i) {
        for (int k = 0; k < 3; ++k) {
            switch (c.shape) {
            case 0: c.accumulated[i][k] = fill.Wide() * 0.5; break;
            case 1: c.accumulated[i][k] = static_cast<double>(static_cast<int32_t>(fill.Next() % 9) - 4) / 16.0; break;
            case 2: c.accumulated[i][k] = fill.Wide() * 0.5 * (static_cast<double>(i) - 8.0) / 8.0; break;
            case 3: c.accumulated[i][k] = (i < 8 ? -1 : 1) * 0.25 + 0.01 * fill.Wide(); break;
            case 4: c.accumulated[i][k] = k == 0 ? fill.Wide() * 0.5 : 0.0; break;
            default: c.accumulated[i][k] = fill.Signed() * (fill.Next() % 2 ? 1.0e-3 : 1.0); break;
            }
        }
    }
    // The weights are the colours' shares (they add to 1 in the encoder), here also any positive numbers.
    double total = 0.0;
    for (int i = 0; i < 16; ++i) {
        c.weights[i] = (iter % 3 == 0) ? 1.0 : 0.05 + fill.Unit();
        if (i < c.count) total += c.weights[i];
    }
    if (iter % 2 == 0)
        for (int i = 0; i < 16; ++i) c.weights[i] = c.weights[i] / total;
    for (int i = 0; i < 16; ++i) c.order[i] = i;
    const uint32_t arrangement = fill.Next() % 3;
    if (arrangement == 1) {
        // Sorted along a random axis, as the encoder does.
        double axis[3] = {fill.Wide(), fill.Wide(), fill.Wide()};
        gimex::fsh::OrderAlongAxis(c.count, &c.accumulated[0][0], axis, c.order, static_cast<int32_t>(fill.Next() & 1));
    } else if (arrangement == 2) {
        for (int i = c.count - 1; i > 0; --i) {
            const int32_t j = static_cast<int32_t>(fill.Next() % (i + 1));
            const int32_t t = c.order[i];
            c.order[i] = c.order[j];
            c.order[j] = t;
        }
    }
    c.levels = (fill.Next() % 4 == 0) ? 2 + static_cast<int32_t>(fill.Next() % 3) : 3 + static_cast<int32_t>(fill.Next() % 2);
}

// The image's carried best: the initial pair, the previous result, or extremes.
void SetCarried(Fill &fill, int iter, double *score, double *cost)
{
    switch (iter % 4) {
    case 0: *score = 1.0; *cost = 0.0; break;
    case 1: break;
    case 2: *score = 1.0e-9 * fill.Unit(); *cost = -1.0e9 * (fill.Unit() + 0.01); break;
    default: *score = 1.0e9 * fill.Unit(); *cost = -1.0e-9 * fill.Unit(); break;
    }
}

} // namespace

DIFF_TEST(GimexFshCluster_ClusterFit, kClusterFit)
{
    const ClusterFn original = ctx.Original<ClusterFn>(kClusterFit);
    double *const bestScore = GameAddress<double>(kBestScoreRva);
    double *const bestCost = GameAddress<double>(kBestCostRva);
    Fill fill(0x3101);
    for (int iter = 0; iter < 40000; ++iter) {
        Case c;
        MakeCase(fill, iter, c);
        SetCarried(fill, iter, bestScore, bestCost);
        int32_t expectedLevels = c.levels;
        int32_t actualLevels = c.levels;
        double expectedEnds[8];
        double actualEnds[8];
        double expectedAxis[5];
        double actualAxis[5];
        for (auto &e : expectedEnds) e = 123.0;
        for (auto &e : actualEnds) e = 123.0;
        for (auto &e : expectedAxis) e = 123.0;
        for (auto &e : actualAxis) e = 123.0;
        ClearStack();
        const double expected = original(&expectedLevels, c.count, &c.accumulated[0][0], c.weights, c.order, expectedEnds, expectedAxis);
        ClearStack();
        const double actual = gimex::fsh::ClusterFit(&actualLevels, c.count, &c.accumulated[0][0], c.weights, c.order, actualEnds, actualAxis);
        ++ctx.cases;
        if (expectedLevels != actualLevels || std::memcmp(&expected, &actual, sizeof expected) != 0 ||
            std::memcmp(expectedEnds, actualEnds, sizeof expectedEnds) != 0 || std::memcmp(expectedAxis, actualAxis, sizeof expectedAxis) != 0)
            ctx.Fail("cluster fit iter %d count %d shape %u levels %d: levels %d vs %d, error %.17g vs %.17g, ends %.17g %.17g vs %.17g %.17g, axis %.17g vs %.17g",
                     iter, c.count, c.shape, c.levels, expectedLevels, actualLevels, expected, actual, expectedEnds[0], expectedEnds[3], actualEnds[0],
                     actualEnds[3], expectedAxis[0], actualAxis[0]);
    }
    *bestScore = 1.0;
    *bestCost = 0.0;
}

DIFF_TEST(GimexFshCluster_BoundedClusterFit, kBoundedClusterFit)
{
    const BoundedFn original = ctx.Original<BoundedFn>(kBoundedClusterFit);
    double *const bestScore = GameAddress<double>(kBoundedBestScoreRva);
    double *const bestCost = GameAddress<double>(kBoundedBestCostRva);
    Fill fill(0x3102);
    for (int iter = 0; iter < 40000; ++iter) {
        Case c;
        MakeCase(fill, iter, c);
        SetCarried(fill, iter, bestScore, bestCost);
        // The bounds: lower and upper per channel; wide (never reached), tight (reached at one end or both), or inverted-looking and empty.
        double bounds[6];
        const uint32_t kind = fill.Next() % 5;
        for (int k = 0; k < 3; ++k) {
            const double low = -0.1 - 0.5 * fill.Unit();
            switch (kind) {
            case 0: bounds[k] = -10.0; bounds[3 + k] = 10.0; break;
            case 1: bounds[k] = low; bounds[3 + k] = low + 0.5 + fill.Unit(); break;
            case 2: bounds[k] = low * 0.1; bounds[3 + k] = low * 0.1 + 0.05 * fill.Unit(); break;
            case 3: bounds[k] = 0.0; bounds[3 + k] = 1.0 * fill.Unit(); break;
            default: bounds[k] = fill.Wide() * 0.3; bounds[3 + k] = fill.Wide() * 0.3; break;
            }
        }
        int32_t expectedLevels = c.levels;
        int32_t actualLevels = c.levels;
        double expectedEnds[8];
        double actualEnds[8];
        double expectedAxis[5];
        double actualAxis[5];
        for (auto &e : expectedEnds) e = 123.0;
        for (auto &e : actualEnds) e = 123.0;
        for (auto &e : expectedAxis) e = 123.0;
        for (auto &e : actualAxis) e = 123.0;
        ClearStack();
        const double expected = original(&expectedLevels, c.count, &c.accumulated[0][0], c.weights, c.order, bounds, expectedEnds, expectedAxis);
        ClearStack();
        const double actual =
            gimex::fsh::BoundedClusterFit(&actualLevels, c.count, &c.accumulated[0][0], c.weights, c.order, bounds, actualEnds, actualAxis);
        ++ctx.cases;
        if (expectedLevels != actualLevels || std::memcmp(&expected, &actual, sizeof expected) != 0 ||
            std::memcmp(expectedEnds, actualEnds, sizeof expectedEnds) != 0 || std::memcmp(expectedAxis, actualAxis, sizeof expectedAxis) != 0)
            ctx.Fail("bounded fit iter %d count %d shape %u bounds %u levels %d: levels %d vs %d, error %.17g vs %.17g, ends %.17g %.17g vs %.17g %.17g, axis %.17g vs %.17g",
                     iter, c.count, c.shape, kind, c.levels, expectedLevels, actualLevels, expected, actual, expectedEnds[0], expectedEnds[3],
                     actualEnds[0], actualEnds[3], expectedAxis[0], actualAxis[0]);
    }
    *bestScore = 1.0;
    *bestCost = 0.0;
}
