// Differential test for cGZRect's intersection 0x373520 (lane 261108s, src/gz/cGZRect.cpp): random rectangles built
// to touch, overlap by one, nest, invert and sit at the int32 edges, the result written into a separate rectangle or
// into either input; the written words, the canary words around them and the low byte of the answer compared.
#include "difftest.h"

#include "gz/cGZRect.h"
#include "gz/cIGZBuffer.h"

#include <climits>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <random>
#include <utility>

namespace {

using Fn = uint32_t(__thiscall *)(cGZRect *, const cGZRect *, const cGZRect *);

// An edge near the other rectangle's (equal, one either side), an edge of the int32 range, or a small number.
int32_t Edge(std::mt19937 &rng, int32_t near)
{
    static const int32_t kSteps[] = {0, 0, 0, 1, -1, 2, -2};
    static const int32_t kEdges[] = {INT_MIN, INT_MIN + 1, INT_MAX, INT_MAX - 1, 0, -1, 1};
    const unsigned how = rng() % 8;
    if (how < 5) return int32_t(uint32_t(near) + uint32_t(kSteps[rng() % std::size(kSteps)]));
    if (how == 5) return kEdges[rng() % std::size(kEdges)];
    if (how == 6) return int32_t(rng());
    return int32_t(rng() % 64) - 16;
}

void Pick(std::mt19937 &rng, cGZRect &a, cGZRect &b)
{
    a.left = int32_t(rng() % 64) - 16;
    a.top = int32_t(rng() % 64) - 16;
    a.right = a.left + int32_t(rng() % 40) - 4;
    a.bottom = a.top + int32_t(rng() % 40) - 4;
    if (rng() % 10 == 0) a.left = Edge(rng, a.left);
    if (rng() % 10 == 0) a.right = Edge(rng, a.right);
    // b's edges near a's (either of a's edges on the axis), so ties on every compare come up.
    const int32_t xs[] = {a.left, a.right}, ys[] = {a.top, a.bottom};
    b.left = Edge(rng, xs[rng() % 2]);
    b.right = Edge(rng, xs[rng() % 2]);
    b.top = Edge(rng, ys[rng() % 2]);
    b.bottom = Edge(rng, ys[rng() % 2]);
}

} // namespace

// Verifies: cGZRect's intersection 0x373520.
DIFF_TEST(SG_GZRectIntersect, 0x373520)
{
    const Fn original = ctx.Original<Fn>(0x373520);
    std::mt19937 rng(0x373520);
    unsigned hits = 0, misses = 0, ties = 0;
    for (unsigned i = 0; i < 400000 && ctx.failures.size() < 8; ++i) {
        cGZRect a, b;
        Pick(rng, a, b);
        if (rng() % 2) std::swap(a, b);
        // Three slots (out, a, b) between canaries; `out` is the first slot, or a's, or b's.
        cGZRect want[5], got[5];
        const cGZRect canary{int32_t(0xa5a5a5a5u), int32_t(0x5a5a5a5au), int32_t(rng()), int32_t(rng())};
        want[0] = want[4] = canary;
        want[1] = canary;
        if (rng() % 4 == 0) std::memset(&want[1], 0, sizeof want[1]);
        want[2] = a;
        want[3] = b;
        std::memcpy(got, want, sizeof want);
        const unsigned outAt = rng() % 3 == 0 ? 2 + rng() % 2 : 1;
        const uint8_t wantAnswer = uint8_t(original(&want[outAt], &want[2], &want[3]));
        const uint8_t gotAnswer = uint8_t(GZRectIntersect(&got[outAt], &got[2], &got[3]));
        ++ctx.cases;
        (wantAnswer ? hits : misses)++;
        if (a.right == b.left || a.left == b.right || a.bottom == b.top || a.top == b.bottom) ++ties;
        if (wantAnswer != gotAnswer || std::memcmp(want, got, sizeof want) != 0)
            ctx.Fail("case %u (out at %u): a (%d %d %d %d) b (%d %d %d %d): original %u (%d %d %d %d), ours %u (%d %d %d %d)", i,
                     outAt, a.left, a.top, a.right, a.bottom, b.left, b.top, b.right, b.bottom, wantAnswer,
                     want[outAt].left, want[outAt].top, want[outAt].right, want[outAt].bottom, gotAnswer, got[outAt].left,
                     got[outAt].top, got[outAt].right, got[outAt].bottom);
    }
    if (hits < 10000 || misses < 10000 || ties < 10000)
        ctx.Fail("coverage: %u overlapping, %u apart, %u touching", hits, misses, ties);
}
