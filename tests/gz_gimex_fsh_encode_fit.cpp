// Differential tests for the FSH back end's least-squares encoder (lane MS; src/gz/GimexFshAlphaFit.size.cpp,
// GimexFshSingleColour.size.cpp, GimexFshColourFit.size.cpp, GimexFshEncodeBlocks.size.cpp): the alpha fit 0x897b56, the single-colour
// fit 0x8990a2, the colour fit 0x897e3b and the block driver 0x8988f3. Each runs alone over seeded input, the original (by RVA, in the
// mapped image) against ours; the callees past 0x8995da are ours (lane MT), and the quantiser 0x89aa95 is ours too (lane MU).
//
// The fits keep their working arrays in image globals the original's second-half code reads. The whole data region the fits touch
// (0x10e8000..0x10ee000) is saved before the original runs and restored before ours, and compared afterwards, so each side starts from
// the same state and every global write is checked as well as the result (but for the alpha solver's and the end-point placement's private scratch, see Snapshot).
//
// Compared: results, return values (bit-exact doubles), every byte written (with sentinel margins), the globals, the source untouched.
//
// Kept out: a block mode above 2 (the original's slot table is then the previous block's, or stack garbage for the first); zero
// colour weights in the fits (a division by zero); an alpha fit with no values (the caller handles it).
#define NOMINMAX
#include "difftest.h"
#include "code_patch.h"
#include "game/game.h"
#include "gz/GimexFshCluster.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kFitAlpha = 0x897b56;
constexpr uint32_t kFitColours = 0x897e3b;
constexpr uint32_t kEncodeBlocks = 0x8988f3;
constexpr uint32_t kFitSingleColour = 0x8990a2;
constexpr uint32_t kDataRva = 0x10e8000;
constexpr size_t kDataBytes = 0x6000;

using AlphaFn = void(__cdecl *)(gimex::fsh::AlphaFit *);
using ColourFn = void(__cdecl *)(gimex::fsh::ColourFit *);
using SingleFn = double(__cdecl *)(int32_t *, int32_t, const double *, const double *, int32_t *, int32_t *);
using BlocksFn = int32_t(__cdecl *)(int32_t, int32_t, uint32_t, const uint8_t *, uint8_t *, uint32_t, const float *, int32_t, int32_t);

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

// The alpha solver 0x899d55 (ours since lane MT) did its work in the image's globals 0x10ebdc8..0x10ebee8 (the level thresholds, the
// centred values and the line fit's sums); ours keeps it in locals and leaves them alone. Nothing else reads them: a scan of the image
// for those addresses finds only the solver itself and a loop bound in the alpha fit (0x897d64, the end of another table), and
// GimexFshAlphaSolve_Solve compares the solver's results against the original with the stack cleared; GimexFshAlphaSolve_Stack shows the
// original's scratch does not depend on the stack either. So the range is not compared; it is zeroed in every snapshot.
constexpr size_t kSolverScratchFrom = 0x10ebdc8 - kDataRva;
constexpr size_t kSolverScratchTo = 0x10ebee8 - kDataRva;

// Likewise the end-point placement 0x899b1f (ours since lane MT) built its candidate end points at 0x10eb370..0x10eb3a0 and kept the two best
// errors at 0x10eb3a0 and 0x10eb3a8; ours keeps them in locals. The only other reference in the image to this range is the end bound of a
// loop over the two doubles before it (0x899417, in the single-colour search), and GimexFshPlaceEndpoints_Place compares the placement's
// results against the original with the quantiser both sides call. Zeroed in every snapshot, too.
constexpr size_t kPlaceScratchFrom = 0x10eb370 - kDataRva;
constexpr size_t kPlaceScratchTo = 0x10eb3b0 - kDataRva;

// And the quantiser 0x89aa95 (ours since lane MU) rebuilt its palettes (0x10e7eb0..0x10e8030, the last of them lie in this region) and its
// distance tables at 0x10e8030..0x10e9830 and kept the best score and a scratch double at 0x10e9830 / 0x10e9838; ours keeps them in locals. A scan of the image's
// relocated references to 0x10e7eb0..0x10e9840 finds only the quantiser itself (the next global, 0x10e9840, is the single-colour fit's, which
// stays compared), and the tables are rebuilt in full on every call, so no state carries over. GimexFshQuantise_Quantise compares ours' tables and
// score with the original's globals bit for bit. Zeroed in every snapshot, too.
constexpr size_t kQuantiseTablesFrom = 0x10e8000 - kDataRva;   // the region starts inside the palettes
constexpr size_t kQuantiseTablesTo = 0x10e9840 - kDataRva;

std::vector<uint8_t> Snapshot()
{
    const uint8_t *data = GameAddress<const uint8_t>(kDataRva);
    std::vector<uint8_t> snapshot(data, data + kDataBytes);
    std::fill(snapshot.begin() + kQuantiseTablesFrom, snapshot.begin() + kQuantiseTablesTo, uint8_t{0});
    std::fill(snapshot.begin() + kSolverScratchFrom, snapshot.begin() + kSolverScratchTo, uint8_t{0});
    std::fill(snapshot.begin() + kPlaceScratchFrom, snapshot.begin() + kPlaceScratchTo, uint8_t{0});
    return snapshot;
}

void Restore(const std::vector<uint8_t> &snapshot)
{
    std::memcpy(GameAddress<uint8_t>(kDataRva), snapshot.data(), kDataBytes);
}

// The first differing byte of the data region, or -1.
int FirstDifference(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return static_cast<int>(i);
    return -1;
}

} // namespace

DIFF_TEST(GimexFshEncodeFit_Alpha, kFitAlpha)
{
    const AlphaFn original = ctx.Original<AlphaFn>(kFitAlpha);
    Fill fill(0x2001);
    for (int iter = 0; iter < 30000; ++iter) {
        gimex::fsh::AlphaFit fit;
        std::memset(&fit, 0xa5, sizeof fit);
        fit.count = 1 + fill.Next() % 16;
        const uint32_t shape = fill.Next() % 6;
        for (int i = 0; i < 16; ++i) {
            float v;
            switch (shape) {
            case 0: v = static_cast<float>(fill.Next() % 256) / 255.0f; break;
            case 1: v = (fill.Next() % 3 == 0) ? 0.0f : ((fill.Next() % 2) ? 1.0f : static_cast<float>(fill.Unit())); break;
            case 2: v = static_cast<float>(fill.Next() % 3) * 0.5f; break;
            case 3: v = (fill.Next() % 2) ? 1.0f : static_cast<float>(fill.Next() % 8) / 7.0f; break;
            case 4: v = static_cast<float>(fill.Unit()); break;
            default: v = (fill.Next() % 2) ? 0.0f : static_cast<float>(fill.Next() % 16) / 15.0f; break;
            }
            fit.values[i] = v;
        }
        fit.allowZero = (fill.Next() % 2) ? static_cast<int32_t>(fill.Next() | 1) : 0;
        fit.allowOne = (fill.Next() % 2) ? 0x20000 : 0;
        const std::vector<uint8_t> before = Snapshot();
        gimex::fsh::AlphaFit expected = fit;
        gimex::fsh::AlphaFit actual = fit;
        ClearStack();
        original(&expected);
        const std::vector<uint8_t> afterOriginal = Snapshot();
        Restore(before);
        ClearStack();
        gimex::fsh::FitAlpha(&actual);
        const std::vector<uint8_t> afterOurs = Snapshot();
        ++ctx.cases;
        const int diff = FirstDifference(afterOriginal, afterOurs);
        if (std::memcmp(&expected, &actual, sizeof expected) != 0 || diff >= 0)
            ctx.Fail("alpha fit iter %d count %d shape %u zero %d one %d: levels %d vs %d ends %d %d vs %d %d, global diff at %d", iter, fit.count, shape,
                     fit.allowZero, fit.allowOne, expected.levels, actual.levels, expected.endpoints[0], expected.endpoints[1], actual.endpoints[0],
                     actual.endpoints[1], diff);
    }
}

DIFF_TEST(GimexFshEncodeFit_SingleColour, kFitSingleColour)
{
    const SingleFn original = ctx.Original<SingleFn>(kFitSingleColour);
    Fill fill(0x2002);
    for (int iter = 0; iter < 20000; ++iter) {
        const int32_t count = 1 + fill.Next() % 16;
        double weights[3];
        for (auto &w : weights) w = 0.2 + 0.8 * fill.Unit();
        if (iter % 11 == 0) weights[0] = weights[1] = weights[2] = 1.0;
        double colours[16 * 3];
        const double base[3] = {fill.Unit(), fill.Unit(), fill.Unit()};
        // Shape 4: every colour exactly between two 8-bit values (weights 1), so the rounding of the wanted value and the tie between
        // the nearest values above and below (decided by the wanted value's parity) are exercised.
        const uint32_t shape = fill.Next() % 5;
        if (shape == 4) weights[0] = weights[1] = weights[2] = 1.0;
        for (int i = 0; i < count; ++i) {
            for (int c = 0; c < 3; ++c) {
                double unit = base[c];
                if (shape == 4) unit = (std::floor(base[c] * 254.0) + 0.5) / 255.0;
                if (shape == 1) unit += (fill.Unit() - 0.5) / 2048.0;
                if (shape == 2) unit = fill.Unit();
                if (shape == 3) unit = (fill.Next() % 2) ? 0.0 : 1.0;
                if (unit < 0.0) unit = 0.0;
                if (unit > 1.0) unit = 1.0;
                colours[i * 3 + c] = unit * weights[c];
            }
        }
        const int32_t levels0 = (fill.Next() % 2) ? 3 : 4;
        int32_t levelsA = levels0;
        int32_t levelsB = levels0;
        int32_t endpointsA[8];
        int32_t endpointsB[8];
        int32_t indicesA[20];
        int32_t indicesB[20];
        std::memset(endpointsA, 0x55, sizeof endpointsA);
        std::memset(endpointsB, 0x55, sizeof endpointsB);
        std::memset(indicesA, 0x66, sizeof indicesA);
        std::memset(indicesB, 0x66, sizeof indicesB);
        std::vector<uint8_t> before = Snapshot();
        if (iter % 50 == 0) {
            // A cold table: the flag clear and the table zero (the original builds it on first use).
            std::memset(before.data() + (0x10eb358 - kDataRva), 0, 4);
            std::memset(before.data() + (0x10e9858 - kDataRva), 0, 0x1b00);
            Restore(before);
        }
        const double errorA = original(&levelsA, count, colours, weights, endpointsA, indicesA);
        const std::vector<uint8_t> afterOriginal = Snapshot();
        Restore(before);
        const double errorB = gimex::fsh::FitSingleColour(&levelsB, count, colours, weights, endpointsB, indicesB);
        const std::vector<uint8_t> afterOurs = Snapshot();
        ++ctx.cases;
        const int diff = FirstDifference(afterOriginal, afterOurs);
        if (levelsA != levelsB || std::memcmp(endpointsA, endpointsB, sizeof endpointsA) != 0 || std::memcmp(indicesA, indicesB, sizeof indicesA) != 0 ||
            std::memcmp(&errorA, &errorB, sizeof errorA) != 0 || diff >= 0)
            ctx.Fail("single colour iter %d count %d shape %u levels %d: levels %d vs %d error %g vs %g, global diff at %d", iter, count, shape, levels0,
                     levelsA, levelsB, errorA, errorB, diff);
    }
}

DIFF_TEST(GimexFshEncodeFit_Colours, kFitColours)
{
    const ColourFn original = ctx.Original<ColourFn>(kFitColours);
    Fill fill(0x2003);
    for (int iter = 0; iter < 20000; ++iter) {
        gimex::fsh::ColourFit fit;
        std::memset(&fit, 0, sizeof fit);
        fit.count = 1 + fill.Next() % 16;
        const uint32_t shape = fill.Next() % 8;
        double palette[8][3];
        for (auto &p : palette)
            for (auto &c : p) c = fill.Unit();
        const double base[3] = {fill.Unit(), fill.Unit(), fill.Unit()};
        const uint32_t distinct = 1 + fill.Next() % 6;
        for (int i = 0; i < fit.count; ++i) {
            for (int c = 0; c < 3; ++c) {
                double v = 0;
                switch (shape) {
                case 0: v = fill.Unit(); break;
                case 1: v = palette[fill.Next() % distinct][c]; break;
                case 2: v = base[c] + (fill.Unit() - 0.5) / 4096.0; break;
                case 3: v = palette[fill.Next() % 2][c]; break;
                case 4: v = (fill.Next() % 2) ? 0.0 : 1.0; break;
                case 5: v = base[c] * static_cast<double>(i) / 15.0; break;
                case 6: v = static_cast<double>(fill.Next() % 32) / 31.0; break;
                default: v = palette[fill.Next() % 3][c] * 0.5 + 0.25 * fill.Unit(); break;
                }
                fit.colours[i][c] = v;
            }
        }
        for (auto &w : fit.weights) w = 0.2 + 0.8 * fill.Unit();
        if (iter % 7 == 0) fit.weights[0] = fit.weights[1] = fit.weights[2] = 1.0;
        fit.maxLevels = (fill.Next() % 2) ? 3 : 4;
        std::memset(fit.indices, 0x33, sizeof fit.indices);
        std::memset(fit.endpoints, 0x44, sizeof fit.endpoints);
        fit.levels = 0x1234;
        const std::vector<uint8_t> before = Snapshot();
        gimex::fsh::ColourFit expected = fit;
        gimex::fsh::ColourFit actual = fit;
        ClearStack();
        original(&expected);
        const std::vector<uint8_t> afterOriginal = Snapshot();
        Restore(before);
        ClearStack();
        gimex::fsh::FitColours(&actual);
        const std::vector<uint8_t> afterOurs = Snapshot();
        ++ctx.cases;
        const int diff = FirstDifference(afterOriginal, afterOurs);
        if (std::memcmp(&expected, &actual, sizeof expected) != 0 || diff >= 0)
            ctx.Fail("colour fit iter %d count %d shape %u max %d: levels %d vs %d, global diff at %d (rva 0x%x)", iter, fit.count, shape, fit.maxLevels,
                     expected.levels, actual.levels, diff, diff >= 0 ? kDataRva + diff : 0);
    }
}

namespace {
// The ordering search stops when a new ordering repeats an earlier one; over seeded input the longest search seen (3,000,000 cases of
// 8..16 colours) took 11 tries of the 17 allowed, so the cap is not reachable with random colours. This test stands a function that
// never reports a repeat in for the image's same-order test 0x89c42d, on both sides (the original fit and ours both call the image's
// function), so every search runs to its cap: 17 tries in each pass, 9 in the bounded retry.
// Also the reorder 0x89d691 is stood in for: with the real one the orderings reach a fixed point before the cap, so the last tries repeat
// the earlier ones' results and a search stopping one try early would give the same answer. The stand-in gives each call a different
// rotation of the colours (counter reset before each side), so every try's result differs.
int32_t __cdecl NeverSameOrder(int32_t, const int32_t *, const int32_t *)
{
    return 0;
}

int32_t gRotation = 0;

void __cdecl RotatingOrder(int32_t unique, const double *, const double *, int32_t *order, int32_t)
{
    for (int32_t i = 0; i < unique; ++i) order[i] = (i + gRotation) % unique;
    ++gRotation;
}
} // namespace

DIFF_TEST(GimexFshEncodeFit_ColoursSearchCap, kFitColours)
{
    const ColourFn original = ctx.Original<ColourFn>(kFitColours);
    shape::StandIn neverSame(0x89c42d, &gimex::fsh::SameOrdering, &NeverSameOrder);
    shape::StandIn rotating(0x89d691, &gimex::fsh::OrderAlongAxis, &RotatingOrder);
    Fill fill(0x2077);
    for (int iter = 0; iter < 3000; ++iter) {
        gimex::fsh::ColourFit fit;
        std::memset(&fit, 0, sizeof fit);
        fit.count = 3 + fill.Next() % 14;
        for (int i = 0; i < fit.count; ++i)
            for (int c = 0; c < 3; ++c) fit.colours[i][c] = (iter % 3 == 0) ? static_cast<double>(fill.Next() % 32) / 31.0 : fill.Unit();
        for (auto &w : fit.weights) w = 0.2 + 0.8 * fill.Unit();
        fit.maxLevels = (fill.Next() % 2) ? 3 : 4;
        std::memset(fit.indices, 0x33, sizeof fit.indices);
        std::memset(fit.endpoints, 0x44, sizeof fit.endpoints);
        fit.levels = 0x1234;
        const std::vector<uint8_t> before = Snapshot();
        gimex::fsh::ColourFit expected = fit;
        gimex::fsh::ColourFit actual = fit;
        ClearStack();
        gRotation = 0;
        original(&expected);
        const std::vector<uint8_t> afterOriginal = Snapshot();
        Restore(before);
        ClearStack();
        gRotation = 0;
        gimex::fsh::FitColours(&actual);
        const std::vector<uint8_t> afterOurs = Snapshot();
        ++ctx.cases;
        const int diff = FirstDifference(afterOriginal, afterOurs);
        if (std::memcmp(&expected, &actual, sizeof expected) != 0 || diff >= 0)
            ctx.Fail("colour fit (no repeats) iter %d count %d max %d: levels %d vs %d, global diff at %d (rva 0x%x)", iter, fit.count, fit.maxLevels,
                     expected.levels, actual.levels, diff, diff >= 0 ? kDataRva + diff : 0);
    }
}

namespace {

enum class Look { Flat, GradientX, GradientY, Noise, Edges, FewColours, TwoColours, Translucent, Count };

void MakeImage(Fill &fill, Look look, std::vector<uint8_t> &bytes, int32_t width, int32_t rows, uint32_t pitch)
{
    bytes.assign(static_cast<size_t>(pitch) * (rows + 1) + 64, 0xde);
    uint32_t palette[8];
    for (auto &p : palette) p = fill.Next();
    const uint32_t base = fill.Next();
    for (int32_t y = 0; y < rows; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            uint32_t pixel = 0;
            switch (look) {
            case Look::Flat: pixel = base | 0xff000000u; break;
            case Look::GradientX: pixel = ((x * 0x0b0a09u + base) & 0xffffffu) | 0xff000000u; break;
            case Look::GradientY: pixel = ((y * 0x101010u + x * 0x030201u) ^ (base & 0xe0e0e0u)) | (y * 17u << 24); break;
            case Look::Noise: pixel = fill.Next(); break;
            case Look::Edges: {
                static const uint32_t kEdges[] = {0xff000000, 0xffffffff, 0xff0000ff, 0xffff0000, 0xff00ff00, 0x00000000, 0x40ffffff, 0x80808080};
                pixel = kEdges[fill.Next() % 8];
                break;
            }
            case Look::FewColours: pixel = palette[fill.Next() % (3 + base % 4)]; break;
            case Look::TwoColours: pixel = palette[fill.Next() % 2]; break;
            case Look::Translucent: pixel = (palette[fill.Next() % 5] & 0xffffffu) | ((fill.Next() % 4) * 0x55u << 24); break;
            default: break;
            }
            std::memcpy(&bytes[static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4], &pixel, 4);
        }
    }
}

} // namespace

DIFF_TEST(GimexFshEncodeFit_Blocks, kEncodeBlocks)
{
    const BlocksFn original = ctx.Original<BlocksFn>(kEncodeBlocks);
    Fill fill(0x2004);
    std::vector<uint8_t> image;
    static const uint32_t kKinds[] = {0, 0x100, 0x200, 0x300};
    for (int iter = 0; iter < 6000; ++iter) {
        // Half the cases are one block (the stack is clear for it); the others are several, where the original's fits see the stack the
        // previous block left (see the header). For those, the fitted alpha kind gets alpha values strictly between 0 and 255, so that
        // no candidate list of the alpha fit is empty (an empty list's end points are the stack's, whichever candidate wins).
        const bool single = iter % 2 == 0;
        const int32_t span = single ? 4 : 14;
        const int32_t width = static_cast<int32_t>(fill.Next() % span) + (single ? 1 : 0) - (iter % 40 == 0 ? 1 : 0);
        const int32_t rows = static_cast<int32_t>(fill.Next() % span) + (single ? 1 : 0) - (iter % 45 == 0 ? 1 : 0);
        const uint32_t pitch = static_cast<uint32_t>(std::max(width, 0) * 4 + (fill.Next() % 3) * 4);
        const Look look = static_cast<Look>(fill.Next() % static_cast<uint32_t>(Look::Count));
        uint32_t flags = kKinds[fill.Next() % 4] | (fill.Next() % 3);
        if (fill.Next() % 2) flags |= 0x10000;
        if (fill.Next() % 2) flags |= 0x20000;
        MakeImage(fill, look, image, width < 0 ? 0 : width, rows < 0 ? 0 : rows, pitch);
        if (!single && (flags & 0xff00) == 0x200) {
            for (int32_t y = 0; y < rows; ++y)
                for (int32_t x = 0; x < width; ++x) {
                    uint8_t &a = image[static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4 + 3];
                    a = static_cast<uint8_t>(1 + a % 254);
                }
        }
        const std::vector<uint8_t> imageBefore = image;
        float weights[3];
        for (auto &w : weights) w = 0.1f + 0.9f * static_cast<float>(fill.Unit());
        if (iter % 5 == 0) weights[0] = weights[1] = weights[2] = 1.0f;
        const int32_t form4444 = fill.Next() % 3 == 0;
        const int32_t bigEndian = fill.Next() % 3 == 0;
        const size_t bytes = static_cast<size_t>(((std::max(width, 0) + 3) / 4) * ((std::max(rows, 0) + 3) / 4)) * 16 + 64;
        std::vector<uint8_t> expected(bytes, 0xcc);
        std::vector<uint8_t> actual(bytes, 0xcc);
        const std::vector<uint8_t> before = Snapshot();
        ClearStack();
        const int32_t expectedSize = original(width, rows, pitch, image.data(), expected.data(), flags, weights, form4444, bigEndian);
        const std::vector<uint8_t> afterOriginal = Snapshot();
        Restore(before);
        ClearStack();
        const int32_t actualSize = gimex::fsh::EncodeBlocks(width, rows, pitch, image.data(), actual.data(), flags, weights, form4444, bigEndian);
        const std::vector<uint8_t> afterOurs = Snapshot();
        ++ctx.cases;
        const int diff = FirstDifference(afterOriginal, afterOurs);
        if (diff >= 0) {
            char list[400] = {};
            int used = 0;
            int shown = 0;
            for (size_t i = static_cast<size_t>(diff); i < afterOriginal.size() && shown < 24; ++i) {
                if (afterOriginal[i] == afterOurs[i]) continue;
                used += std::snprintf(list + used, sizeof list - static_cast<size_t>(used), " +%zx:%02x/%02x", i, afterOriginal[i], afterOurs[i]);
                ++shown;
            }
            ctx.Fail("blocks iter %d diffs:%s", iter, list);
        }
        if (expectedSize != actualSize || expected != actual || image != imageBefore || diff >= 0)
            ctx.Fail("blocks iter %d %dx%d pitch %u look %d flags %x form %d be %d: size %d vs %d, output %s, global diff at %d (%02x vs %02x)", iter,
                     width, rows, pitch, static_cast<int>(look), flags, form4444, bigEndian, expectedSize, actualSize,
                     expected == actual ? "same" : "differs", diff, diff >= 0 ? afterOriginal[diff] : 0, diff >= 0 ? afterOurs[diff] : 0);
    }
}

namespace {

// A stand-in for the end-point solver 0x899d55 that returns the 6-level end points in the other order (e0 > e1), which the real solver
// never does (300,000 random alpha fits: every 6- and 8-level result had e0 <= e1). The block driver swaps a 6-level pair that is in that
// order and maps the codes through `7 - code` (codes 2..5), a path no real input reaches, so it is checked here with the solver replaced
// on both sides (a StandIn: the original driver calls the image's solver, ours calls ours, both land on the stand-in). The 6-level candidates get the lower error so they win.
double __cdecl ReversedSolver(int32_t levels, int32_t count, const double *values, int32_t *endpoints, int32_t *codes)
{
    if (count == 0) return 0.0;
    double lo = values[0];
    double hi = values[0];
    for (int32_t i = 1; i < count; ++i) {
        lo = values[i] < lo ? values[i] : lo;
        hi = values[i] > hi ? values[i] : hi;
    }
    const int32_t low = static_cast<int32_t>(lo * 255.0 + 0.5);
    const int32_t high = static_cast<int32_t>(hi * 255.0 + 0.5);
    endpoints[0] = levels == 6 ? high : low;
    endpoints[1] = levels == 6 ? low : high;
    const double steps = static_cast<double>(levels - 1);
    for (int32_t i = 0; i < count; ++i) {
        const double t = hi > lo ? (values[i] - lo) / (hi - lo) : 0.0;
        codes[i] = static_cast<int32_t>(t * steps + 0.5);
    }
    return levels == 6 ? 0.25 : 1.0;
}

} // namespace

DIFF_TEST(GimexFshEncodeFit_BlocksAlphaSwap, kEncodeBlocks)
{
    const BlocksFn original = ctx.Original<BlocksFn>(kEncodeBlocks);
    shape::StandIn reversed(0x899d55, &gimex::fsh::SolveAlpha, &ReversedSolver);
    Fill fill(0x2088);
    std::vector<uint8_t> image;
    for (int iter = 0; iter < 1500; ++iter) {
        const int32_t width = 4 + static_cast<int32_t>(fill.Next() % 9);
        const int32_t rows = 4 + static_cast<int32_t>(fill.Next() % 9);
        const uint32_t pitch = static_cast<uint32_t>(width * 4);
        MakeImage(fill, (iter % 2) ? Look::Noise : Look::GradientY, image, width, rows, pitch);
        // Alpha strictly between 0 and 255, so that no candidate list is empty and every list is non-trivial.
        for (int32_t y = 0; y < rows; ++y)
            for (int32_t x = 0; x < width; ++x) {
                uint8_t &a = image[static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4 + 3];
                a = static_cast<uint8_t>(2 + a % 252);
            }
        const uint32_t flags = 0x200 | (fill.Next() % 3);
        const std::vector<uint8_t> imageBefore = image;
        float weights[3] = {1.0f, 1.0f, 1.0f};
        const size_t bytes = static_cast<size_t>(((width + 3) / 4) * ((rows + 3) / 4)) * 16 + 64;
        std::vector<uint8_t> expected(bytes, 0xcc);
        std::vector<uint8_t> actual(bytes, 0xcc);
        const std::vector<uint8_t> before = Snapshot();
        ClearStack();
        const int32_t expectedSize = original(width, rows, pitch, image.data(), expected.data(), flags, weights, 0, 0);
        const std::vector<uint8_t> afterOriginal = Snapshot();
        Restore(before);
        ClearStack();
        const int32_t actualSize = gimex::fsh::EncodeBlocks(width, rows, pitch, image.data(), actual.data(), flags, weights, 0, 0);
        const std::vector<uint8_t> afterOurs = Snapshot();
        ++ctx.cases;
        const int diff = FirstDifference(afterOriginal, afterOurs);
        if (expectedSize != actualSize || expected != actual || image != imageBefore || diff >= 0)
            ctx.Fail("alpha swap iter %d %dx%d flags %x: size %d vs %d, output %s, global diff at %d", iter, width, rows, flags, expectedSize,
                     actualSize, expected == actual ? "same" : "differs", diff);
    }
}
