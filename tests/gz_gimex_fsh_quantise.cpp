// Differential test for the FSH back end's end-point quantiser (lane MU; src/gz/GimexFshQuantise.size.cpp, 0x89aa95). It runs alone over seeded
// input, the original (by RVA, in the mapped image) against ours, with the stack cleared before each call on both sides.
//
// The original builds its palettes, distance tables and best score in image globals (0x10e7eb0..0x10e9840) and rebuilds them on every call;
// ours are locals. So besides the results (the six end-point integers, the codes of the colours and the entries after them untouched, the
// returned double bit for bit) the test:
//  - compares ours' working doubles with what the original left in its globals (best score at 0x10e9830, the 12 palettes, the distance
//    tables for the colours and levels in use), bit for bit: the integer results hide the order the doubles were combined in;
//  - checks that every byte the original wrote to the data region 0x10e7000..0x10ee000 lies inside 0x10e7eb0..0x10e9840, and that ours writes
//    none (a scan of the image's relocated references finds only this function in that range, see the source).
//
// Input is what PlaceEndpoints passes: form 3 or 4, 1..16 distinct colours scaled by the channel weights (flat, gradient, noise, edge
// colours, colours exactly on the 8-bit grid, two clusters), counts adding to one or raw, weights general / equal / with a zero / all zero,
// real end points near colours, around the cube, outside it, huge, exactly on rounding boundaries.
#define NOMINMAX
#include "difftest.h"
#include "game/game.h"
#include "gz/GimexFshCluster.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kQuantise = 0x89aa95;
constexpr uint32_t kRegionRva = 0x10e7000;
constexpr size_t kRegionBytes = 0x7000;
constexpr uint32_t kWrittenFrom = 0x10e7eb0;
constexpr uint32_t kWrittenTo = 0x10e9840;
constexpr uint32_t kPaletteRva = 0x10e7eb0;
constexpr uint32_t kDistanceRva = 0x10e8030;
constexpr uint32_t kBestScoreRva = 0x10e9830;
constexpr int32_t kMaxColours = 16;
constexpr int32_t kSentinel = 0x7777;

using QuantiseFn = double(__cdecl *)(int32_t, int32_t, const double *, const double *, const double *, const double *, int32_t *, int32_t *);

__declspec(noinline) void ClearStack(uint8_t value = 0)
{
    volatile uint8_t buffer[0x4000];
    for (size_t i = 0; i < sizeof(buffer); ++i) buffer[i] = value;
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
    int32_t form;
    int32_t unique;
    double colours[kMaxColours * 3];
    double counts[kMaxColours];
    double weights[3];
    double endpoints[6];
};

struct Output {
    int32_t ends[8];
    int32_t codes[kMaxColours + 2];
    double error;
};

double Clamp01(double v)
{
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

void Make(Fill &fill, int iter, Input &in)
{
    in.form = (fill.Next() % 2) ? 3 : 4;
    in.unique = 1 + static_cast<int32_t>(fill.Next() % kMaxColours);
    if (iter % 211 == 0) in.unique = 0;
    if (iter % 7 == 0) in.unique = kMaxColours;
    for (double &w : in.weights) w = 0.2 + 0.8 * fill.Unit();
    if (iter % 5 == 0) in.weights[0] = in.weights[1] = in.weights[2] = 1.0;
    if (iter % 13 == 0) {
        in.weights[0] = 0.299;
        in.weights[1] = 0.587;
        in.weights[2] = 0.114;
    }
    if (iter % 17 == 0) in.weights[fill.Next() % 3] = 0.0;
    if (iter % 251 == 0) in.weights[0] = in.weights[1] = in.weights[2] = 0.0;
    const uint32_t shape = fill.Next() % 7;
    double base[3];
    double direction[3];
    for (int c = 0; c < 3; ++c) {
        base[c] = fill.Unit();
        direction[c] = fill.Unit() - 0.5;
    }
    double total = 0.0;
    for (int i = 0; i < in.unique; ++i) {
        in.counts[i] = 1.0 + static_cast<double>(fill.Next() % 4);
        if (iter % 19 == 0 && i == 0) in.counts[i] = 0.0;
        total += in.counts[i];
        const double along = fill.Unit();
        const bool second = (fill.Next() % 2) != 0;
        for (int c = 0; c < 3; ++c) {
            double unit;
            switch (shape) {
            case 0: unit = fill.Unit(); break;
            case 1: unit = base[c]; break;
            case 2: unit = Clamp01(base[c] + (along - 0.5) * direction[c] * 2.0); break;
            case 3: unit = (fill.Next() % 2) ? 1.0 : 0.0; break;
            case 4: unit = static_cast<double>(fill.Next() % 256) / 255.0; break;
            case 5: unit = Clamp01((second ? 0.8 : 0.2) + (fill.Unit() - 0.5) * 0.1 + (c == 1 ? 0.05 : 0.0)); break;
            default: unit = Clamp01(static_cast<double>(fill.Next() % 256) / 255.0 + (fill.Unit() - 0.5) / 512.0); break;
            }
            in.colours[i * 3 + c] = unit * in.weights[c];
        }
    }
    if (iter % 3 != 0 && total > 0.0)
        for (int i = 0; i < in.unique; ++i) in.counts[i] = in.counts[i] / total;
    // Counts so large that no rounding beats the starting bound: the original then reads rounding 3 of every channel and, for the last channel,
    // the palette group below its own.
    if (iter % 29 == 0)
        for (int i = 0; i < in.unique; ++i) in.counts[i] *= 1e4;
    for (int e = 0; e < 2; ++e) {
        for (int c = 0; c < 3; ++c) {
            double v;
            const double w = in.weights[c];
            switch (fill.Next() % 8) {
            case 0: v = in.unique > 0 ? in.colours[(fill.Next() % static_cast<uint32_t>(in.unique)) * 3 + c] : 0.0; break;
            case 1: v = (fill.Unit() * 1.6 - 0.3) * w; break;
            case 2: v = fill.Unit() * w; break;
            case 3: v = ((static_cast<double>(fill.Next() % 256) + 0.5) / 255.0) * w; break;
            case 4: {
                const double huge[4] = {1e30, -1e30, 1e10, -5.0};
                v = huge[fill.Next() % 4];
                break;
            }
            case 5: v = (fill.Next() % 2) ? 0.0 : w; break;
            case 6: v = (static_cast<double>(fill.Next() % 256) / 255.0) * w; break;
            default: v = (fill.Unit() * 1.2 - 0.1) * w; break;
            }
            in.endpoints[e * 3 + c] = v;
        }
    }
}

void Seed(Output &out)
{
    for (int32_t &e : out.ends) e = kSentinel;
    for (int32_t &c : out.codes) c = kSentinel;
    out.error = 0.0;
}

bool Same(const Output &a, const Output &b)
{
    return std::memcmp(a.ends, b.ends, sizeof a.ends) == 0 && std::memcmp(a.codes, b.codes, sizeof a.codes) == 0 &&
           std::memcmp(&a.error, &b.error, sizeof a.error) == 0;
}

std::vector<uint8_t> Snapshot()
{
    const uint8_t *data = GameAddress<const uint8_t>(kRegionRva);
    return std::vector<uint8_t>(data, data + kRegionBytes);
}

void Restore(const std::vector<uint8_t> &snapshot)
{
    std::memcpy(GameAddress<uint8_t>(kRegionRva), snapshot.data(), kRegionBytes);
}

// What the original left in its globals, in the trace's shape (the distance tables for the colours and levels in use; the rest zero).
void ReadOriginalTrace(const Input &in, gimex::fsh::QuantiseTrace &trace)
{
    std::memset(&trace, 0, sizeof trace);
    std::memcpy(&trace.bestScore, GameAddress<const uint8_t>(kBestScoreRva), sizeof(double));
    const int32_t used = in.unique > kMaxColours ? kMaxColours : in.unique;
    for (int g = 0; g < 12; ++g) {
        const double *palette = GameAddress<const double>(kPaletteRva + g * 0x20);
        for (int l = 0; l < in.form; ++l) trace.palette[g][l] = palette[l];
        const double *table = GameAddress<const double>(kDistanceRva + g * 0x200);
        for (int k = 0; k < used; ++k)
            for (int l = 0; l < in.form; ++l) trace.distance[g / 4][g % 4][k][l] = table[k * in.form + l];
    }
}

// Ours, masked the same way (the entries past the colours and levels in use are zero on both sides).
void MaskTrace(const Input &in, gimex::fsh::QuantiseTrace &trace)
{
    const int32_t used = in.unique > kMaxColours ? kMaxColours : in.unique;
    for (int g = 0; g < 12; ++g) {
        for (int l = in.form; l < 4; ++l) trace.palette[g][l] = 0.0;
        for (int k = 0; k < kMaxColours; ++k)
            for (int l = 0; l < 4; ++l)
                if (k >= used || l >= in.form) trace.distance[g / 4][g % 4][k][l] = 0.0;
    }
}

} // namespace

DIFF_TEST(GimexFshQuantise_Quantise, kQuantise)
{
    const QuantiseFn original = ctx.Original<QuantiseFn>(kQuantise);
    Fill fill(0x6001);
    int touched = 0;
    for (int iter = 0; iter < 40000; ++iter) {
        Input in;
        Make(fill, iter, in);
        // Odd cases at 24-bit x87 precision, as the game's D3D9 thread may run: the original sums in SSE2, so ours must not
        // depend on it (lane PJ: MSVC had put our Distance sums on the x87; docs/lane/test.md "x87 precision").
        difftest::X87Precision precision(iter % 2 ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
        Output expected;
        Output actual;
        Seed(expected);
        Seed(actual);
        const std::vector<uint8_t> before = Snapshot();
        ClearStack();
        expected.error = original(in.form, in.unique, in.colours, in.counts, in.weights, in.endpoints, expected.ends, expected.codes);
        gimex::fsh::QuantiseTrace expectedTrace;
        ReadOriginalTrace(in, expectedTrace);
        const std::vector<uint8_t> afterOriginal = Snapshot();
        Restore(before);
        gimex::fsh::QuantiseTrace actualTrace;
        std::memset(&actualTrace, 0, sizeof actualTrace);
        ClearStack();
        actual.error = gimex::fsh::QuantiseEndpointsTraced(in.form, in.unique, in.colours, in.counts, in.weights, in.endpoints, actual.ends, actual.codes,
                                                           &actualTrace);
        MaskTrace(in, actualTrace);
        const std::vector<uint8_t> afterOurs = Snapshot();
        ++ctx.cases;
        // The original writes only inside its tables; ours writes nothing.
        int outside = -1;
        for (size_t i = 0; i < kRegionBytes; ++i) {
            const uint32_t rva = kRegionRva + static_cast<uint32_t>(i);
            if (before[i] != afterOriginal[i]) {
                ++touched;
                if (rva < kWrittenFrom || rva >= kWrittenTo) outside = static_cast<int>(rva);
            }
        }
        const bool oursClean = std::memcmp(before.data(), afterOurs.data(), kRegionBytes) == 0;
        if (!Same(expected, actual) || std::memcmp(&expectedTrace, &actualTrace, sizeof expectedTrace) != 0 || outside >= 0 || !oursClean)
            ctx.Fail("quantise iter %d form %d unique %d: error %.17g vs %.17g, ends %d %d %d %d %d %d vs %d %d %d %d %d %d, codes %d %d vs %d %d, trace %s, "
                     "original wrote outside at %#x, ours %s",
                     iter, in.form, in.unique, expected.error, actual.error, expected.ends[0], expected.ends[1], expected.ends[2], expected.ends[3],
                     expected.ends[4], expected.ends[5], actual.ends[0], actual.ends[1], actual.ends[2], actual.ends[3], actual.ends[4], actual.ends[5],
                     expected.codes[0], expected.codes[1], actual.codes[0], actual.codes[1],
                     std::memcmp(&expectedTrace, &actualTrace, sizeof expectedTrace) == 0 ? "same" : "differs", outside, oursClean ? "clean" : "wrote");
    }
    if (touched == 0) ctx.Fail("the original wrote nothing to its tables: the test is not looking at them");
}

// The original does not read the stack it does not set: the same input with the stack filled with 0x00 and 0xff gives the same results.
DIFF_TEST(GimexFshQuantise_Stack, kQuantise)
{
    const QuantiseFn original = ctx.Original<QuantiseFn>(kQuantise);
    Fill fill(0x6002);
    for (int iter = 0; iter < 5000; ++iter) {
        Input in;
        Make(fill, iter, in);
        Output zero;
        Output ones;
        Seed(zero);
        Seed(ones);
        ClearStack(0x00);
        zero.error = original(in.form, in.unique, in.colours, in.counts, in.weights, in.endpoints, zero.ends, zero.codes);
        ClearStack(0xff);
        ones.error = original(in.form, in.unique, in.colours, in.counts, in.weights, in.endpoints, ones.ends, ones.codes);
        ++ctx.cases;
        if (!Same(zero, ones)) ctx.Fail("quantise stack iter %d form %d unique %d: the original depends on the stack", iter, in.form, in.unique);
    }
}
