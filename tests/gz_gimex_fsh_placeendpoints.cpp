// Differential test for the FSH back end's end-point placement (lane MT; src/gz/GimexFshPlaceEndpoints.size.cpp, 0x899b1f). It runs alone over
// seeded input, the original against ours, with the stack cleared before each call on both sides. The original's placement calls the original
// quantiser 0x89aa95, ours calls ours (lane MU; GimexFshQuantise_Quantise tests the quantiser alone). Compared bit for bit: the
// form left in *levels, the six end-point integers, the codes of the colours (and that the entries after them are untouched) and the returned
// double.
//
// The inputs are what the colour search hands over: colours scaled by the channel weights (so in 0..weight), counts that add up to one, the
// real end points near or outside the colour cube.
// Levels start at 2 (two colours), 3 or 4; the allowed form (maxLevels) is 2, 3 or 4.
#define NOMINMAX
#include "difftest.h"
#include "game/game.h"
#include "gz/GimexFshCluster.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kPlace = 0x899b1f;
constexpr int32_t kMaxColours = 16;
constexpr int32_t kSentinel = 0x7777;

using PlaceFn = double(__cdecl *)(int32_t *, int32_t, int32_t, const double *, const double *, const double *, const double *, int32_t *, int32_t *);

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
};

struct Input {
    int32_t levels;
    int32_t maxLevels;
    int32_t unique;
    double colours[kMaxColours * 3];
    double counts[kMaxColours];
    double weights[3];
    double endpoints[6];
};

struct Output {
    int32_t levels;
    int32_t ends[6];
    int32_t codes[kMaxColours + 1];
    double error;
};

void Make(Fill &fill, int iter, Input &in)
{
    in.levels = 2 + static_cast<int32_t>(fill.Next() % 3);
    in.maxLevels = 2 + static_cast<int32_t>(fill.Next() % 3);
    if (iter % 4 == 0) in.levels = 2;
    in.unique = 2 + static_cast<int32_t>(fill.Next() % 15);
    for (double &w : in.weights) w = 0.2 + 0.8 * fill.Unit();
    if (iter % 5 == 0) in.weights[0] = in.weights[1] = in.weights[2] = 1.0;
    if (iter % 17 == 0) in.weights[fill.Next() % 3] = 0.0;
    double total = 0.0;
    for (int i = 0; i < in.unique; ++i) {
        in.counts[i] = 1.0 + static_cast<double>(fill.Next() % 4);
        total += in.counts[i];
        for (int c = 0; c < 3; ++c) in.colours[i * 3 + c] = fill.Unit() * in.weights[c];
    }
    for (int i = 0; i < in.unique; ++i) in.counts[i] = in.counts[i] / total;
    // The real end points: near two of the colours, or anywhere around the cube (outside it too).
    for (int e = 0; e < 2; ++e) {
        for (int c = 0; c < 3; ++c) {
            double v;
            switch (fill.Next() % 3) {
            case 0: v = in.colours[(fill.Next() % static_cast<uint32_t>(in.unique)) * 3 + c]; break;
            case 1: v = (fill.Unit() * 1.6 - 0.3) * in.weights[c]; break;
            default: v = fill.Unit() * in.weights[c]; break;
            }
            in.endpoints[e * 3 + c] = v;
        }
    }
}

void Seed(Output &out, int32_t levels)
{
    out.levels = levels;
    for (int32_t &e : out.ends) e = kSentinel;
    for (int32_t &c : out.codes) c = kSentinel;
    out.error = 0.0;
}

bool Same(const Output &a, const Output &b)
{
    return a.levels == b.levels && std::memcmp(a.ends, b.ends, sizeof a.ends) == 0 && std::memcmp(a.codes, b.codes, sizeof a.codes) == 0 &&
           std::memcmp(&a.error, &b.error, sizeof a.error) == 0;
}

} // namespace

DIFF_TEST(GimexFshPlaceEndpoints_Place, kPlace)
{
    const PlaceFn original = ctx.Original<PlaceFn>(kPlace);
    Fill fill(0x5101);
    for (int iter = 0; iter < 20000; ++iter) {
        Input in;
        Make(fill, iter, in);
        Output expected;
        Output actual;
        Seed(expected, in.levels);
        Seed(actual, in.levels);
        ClearStack();
        expected.error = original(&expected.levels, in.maxLevels, in.unique, in.colours, in.counts, in.weights, in.endpoints, expected.ends, expected.codes);
        ClearStack();
        actual.error = gimex::fsh::PlaceEndpoints(&actual.levels, in.maxLevels, in.unique, in.colours, in.counts, in.weights, in.endpoints, actual.ends,
                                                  actual.codes);
        ++ctx.cases;
        if (!Same(expected, actual))
            ctx.Fail("place iter %d levels %d max %d unique %d: form %d vs %d, error %.17g vs %.17g, ends %d %d %d %d %d %d vs %d %d %d %d %d %d, codes %d %d vs %d %d",
                     iter, in.levels, in.maxLevels, in.unique, expected.levels, actual.levels, expected.error, actual.error, expected.ends[0],
                     expected.ends[1], expected.ends[2], expected.ends[3], expected.ends[4], expected.ends[5], actual.ends[0], actual.ends[1],
                     actual.ends[2], actual.ends[3], actual.ends[4], actual.ends[5], expected.codes[0], expected.codes[1], actual.codes[0],
                     actual.codes[1]);
    }
}
