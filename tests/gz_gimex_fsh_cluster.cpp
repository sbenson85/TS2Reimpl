// Differential tests for the FSH back end's colour axes and orderings (lane MT; src/gz/GimexFshAxis.size.cpp): the principal axis 0x8995da,
// the farthest-pair axis 0x899a2f, the ordering along an axis 0x89d691 and the same-ordering test 0x89c42d. Each runs alone over seeded
// input, the original (by RVA, in the mapped image) against ours; doubles are compared bit for bit (memcmp), never with a tolerance.
//
// The originals keep their working arrays in image globals (the matrix at 0x10e7880.., the projections at 0x10e7930, scratch doubles at
// 0x10e7918..0x10e7928) that nothing else reads (xrefs: only these functions); each writes every one before it reads it, so ours are locals
// and the globals are not compared.
#define NOMINMAX
#include "difftest.h"
#include "gz/GimexFshCluster.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kPrincipalAxis = 0x8995da;
constexpr uint32_t kFarthestPairAxis = 0x899a2f;
constexpr uint32_t kOrderAlongAxis = 0x89d691;
constexpr uint32_t kSameOrdering = 0x89c42d;

using AxisFn = void(__cdecl *)(int32_t, const double *, double *);
using OrderFn = void(__cdecl *)(int32_t, const double *, const double *, int32_t *, int32_t);
using SameFn = int32_t(__cdecl *)(int32_t, const int32_t *, const int32_t *);

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
    // A value with all 53 mantissa bits used (Unit has 24, so sums and products of those are mostly exact and hide the order they are added in).
    double Wide()
    {
        const uint64_t high = Next() >> 5;
        const uint64_t low = Next() >> 6;
        return static_cast<double>((high << 26) | low) / 9007199254740992.0 * 2.0 - 1.0;
    }
};

// The shapes of colour sets: scattered, quantised (equal projections and equal distances), nearly collinear, with repeats, one axis only.
void MakeColours(Fill &fill, uint32_t shape, int32_t count, double (*colours)[3], bool centred)
{
    double palette[4][3];
    for (auto &p : palette)
        for (auto &c : p) c = fill.Signed();
    const double direction[3] = {fill.Wide(), fill.Wide(), fill.Wide()};
    for (int32_t i = 0; i < count; ++i) {
        const double along = fill.Signed();
        // Shape 7: every colour is a permutation of one vector, so projections are equal in exact arithmetic and the order of the
        // three products in the sum decides which are equal in doubles.
        const uint32_t rotate = fill.Next() % 3;
        const bool flip = (fill.Next() & 1) != 0;
        for (int c = 0; c < 3; ++c) {
            double v = 0;
            switch (shape) {
            case 7: v = direction[flip ? (2 - (c + rotate) % 3) : (c + rotate) % 3]; break;
            case 0: v = fill.Wide(); break;
            case 1: v = static_cast<double>(static_cast<int32_t>(fill.Next() % 9) - 4) / 8.0; break;
            case 2: v = direction[c] * along + 0.001 * fill.Signed(); break;
            case 3: v = palette[fill.Next() % 4][c]; break;
            case 4: v = c == 1 ? fill.Signed() : 0.0; break;
            case 5: v = (fill.Next() % 2) ? 1.0e-3 * fill.Signed() : 1.0e3 * fill.Signed(); break;
            default: v = direction[c] * (static_cast<double>(i) - 0.5 * count); break;
            }
            colours[i][c] = v;
        }
    }
    if (centred) {
        double mean[3] = {0, 0, 0};
        for (int32_t i = 0; i < count; ++i)
            for (int c = 0; c < 3; ++c) mean[c] += colours[i][c];
        for (int c = 0; c < 3; ++c) mean[c] /= count > 0 ? count : 1;
        for (int32_t i = 0; i < count; ++i)
            for (int c = 0; c < 3; ++c) colours[i][c] -= mean[c];
    }
}

} // namespace

DIFF_TEST(GimexFshCluster_PrincipalAxis, kPrincipalAxis)
{
    const AxisFn original = ctx.Original<AxisFn>(kPrincipalAxis);
    Fill fill(0x3001);
    for (int iter = 0; iter < 40000; ++iter) {
        const int32_t count = 1 + fill.Next() % 16;
        const uint32_t shape = fill.Next() % 8;
        double colours[16][3];
        MakeColours(fill, shape, count, colours, iter % 2 == 0);
        double expected[4];
        double actual[4];
        for (auto &e : expected) e = 123.0;
        for (auto &a : actual) a = 123.0;
        original(count, &colours[0][0], expected);
        gimex::fsh::PrincipalAxis(count, &colours[0][0], actual);
        ++ctx.cases;
        if (std::memcmp(expected, actual, sizeof expected) != 0)
            ctx.Fail("principal axis iter %d count %d shape %u: %.17g %.17g %.17g (%.17g) vs %.17g %.17g %.17g (%.17g)", iter, count, shape,
                     expected[0], expected[1], expected[2], expected[3], actual[0], actual[1], actual[2], actual[3]);
    }
}

DIFF_TEST(GimexFshCluster_FarthestPairAxis, kFarthestPairAxis)
{
    const AxisFn original = ctx.Original<AxisFn>(kFarthestPairAxis);
    Fill fill(0x3002);
    for (int iter = 0; iter < 40000; ++iter) {
        const int32_t count = 1 + fill.Next() % 16;
        const uint32_t shape = fill.Next() % 8;
        double colours[16][3];
        MakeColours(fill, shape, count, colours, false);
        double expected[4];
        double actual[4];
        for (auto &e : expected) e = 123.0;
        for (auto &a : actual) a = 123.0;
        original(count, &colours[0][0], expected);
        gimex::fsh::FarthestPairAxis(count, &colours[0][0], actual);
        ++ctx.cases;
        if (std::memcmp(expected, actual, sizeof expected) != 0)
            ctx.Fail("farthest pair iter %d count %d shape %u: %.17g %.17g %.17g vs %.17g %.17g %.17g", iter, count, shape, expected[0], expected[1],
                     expected[2], actual[0], actual[1], actual[2]);
    }
}

DIFF_TEST(GimexFshCluster_OrderAlongAxis, kOrderAlongAxis)
{
    const OrderFn original = ctx.Original<OrderFn>(kOrderAlongAxis);
    Fill fill(0x3003);
    for (int iter = 0; iter < 60000; ++iter) {
        const int32_t count = fill.Next() % 17;
        const uint32_t shape = fill.Next() % 8;
        double colours[16][3];
        MakeColours(fill, shape, count, colours, false);
        double axis[3];
        if (iter % 3 == 0) {
            // A whole-number axis on whole-number-eighths colours: many equal projections, so the tie rule decides the order.
            for (auto &a : axis) a = static_cast<double>(static_cast<int32_t>(fill.Next() % 3) - 1);
        } else {
            for (auto &a : axis) a = fill.Wide();
        }
        // Permuted colours (shape 7) on an axis with equal components: the exact projections are equal, the doubles differ by how the sum rounds.
        if (shape == 7) axis[1] = axis[2] = axis[0];
        const int32_t direction = static_cast<int32_t>(fill.Next() % 3) == 0 ? static_cast<int32_t>(fill.Next() | 1) : 0;
        int32_t expected[20];
        int32_t actual[20];
        std::memset(expected, 0x5a, sizeof expected);
        std::memset(actual, 0x5a, sizeof actual);
        original(count, &colours[0][0], axis, expected, direction);
        gimex::fsh::OrderAlongAxis(count, &colours[0][0], axis, actual, direction);
        ++ctx.cases;
        if (std::memcmp(expected, actual, sizeof expected) != 0)
            ctx.Fail("order iter %d count %d shape %u direction %d: first %d %d %d vs %d %d %d", iter, count, shape, direction, expected[0], expected[1],
                     expected[2], actual[0], actual[1], actual[2]);
    }
}

DIFF_TEST(GimexFshCluster_SameOrdering, kSameOrdering)
{
    const SameFn original = ctx.Original<SameFn>(kSameOrdering);
    Fill fill(0x3004);
    for (int iter = 0; iter < 60000; ++iter) {
        const int32_t count = static_cast<int32_t>(fill.Next() % 19) - 1;
        int32_t a[20];
        int32_t b[20];
        for (int i = 0; i < 20; ++i) a[i] = static_cast<int32_t>(fill.Next() % 16);
        const uint32_t relation = fill.Next() % 6;
        for (int i = 0; i < 20; ++i) {
            switch (relation) {
            case 0: b[i] = a[i]; break;                                   // the same
            case 1: b[i] = count > 0 && i < count ? a[count - 1 - i] : a[i]; break; // reversed
            case 2: b[i] = static_cast<int32_t>(fill.Next() % 16); break; // unrelated
            case 3: b[i] = a[i]; break;                                   // the same but for one entry (below)
            case 4: b[i] = count > 0 && i < count ? a[count - 1 - i] : a[i]; break; // reversed but for one entry (below)
            default: b[i] = a[i] ^ static_cast<int32_t>(fill.Next() % 2); break;
            }
        }
        if ((relation == 3 || relation == 4) && count > 0) b[fill.Next() % count] ^= 1;
        const int32_t expected = original(count, a, b);
        const int32_t actual = gimex::fsh::SameOrdering(count, a, b);
        ++ctx.cases;
        if (expected != actual) ctx.Fail("same ordering iter %d count %d relation %u: %d vs %d", iter, count, relation, expected, actual);
    }
}
