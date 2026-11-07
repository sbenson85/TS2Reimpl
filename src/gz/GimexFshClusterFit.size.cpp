// The FSH back end's ordered cluster fits (lane MT; src/gz/GimexFshCluster.h, 0x89c480).
//
// Given the distinct colours of a block in axis order (each with a weight), every way of cutting that run into groups is tried: group g
// of the cut gets the value g, so the colours' group sums are V = sum(g * colour) and the weights' S1 = sum(g * weight), S2 = sum(g^2 * weight).
// The cut's score is the ratio of two sums built up incrementally while the cut points move (one colour moves from one group to the next:
// the incremental forms ((K - 2 * W) - w) * w are the changes of the sum of squares), and the cut that scores best is kept. The end points
// are then V scaled by -S1 / M and (groups - S1) / M, M = S2 - S1^2, and V is written back as the next axis.
//
// All SSE2 double, in the association the code was compiled with (the decompiler printed some of it wrongly; the difference test decides).
//
// ORIGINAL-BUG (state, harmless): the best score is kept in image globals that nothing resets, so it carries over from call to call and
// across block encodes. It cannot change a result (see bestScore below), so ours is a local. Likewise the three cut points live in
// uninitialised stack slots the original reads if no cut ever replaced the best; the first cut always does, so the slots are always
// written (ours start as 0, 0, 0 only so that the reads are defined).
#include "gz/GimexFshCluster.h"

#include <cmath>

namespace gimex {

namespace fsh {

namespace {

constexpr int32_t kChannels = 3;
constexpr int32_t kMaxColours = 16;
constexpr int32_t kGroups = 4;
constexpr int32_t kTwoCutsOnly = 3;           // with fewer levels than this only the first two cut points move
constexpr double kInnerBase = 5.0;
constexpr double kMiddleBase = 3.0;
constexpr double kOuterBase = 1.0;
constexpr double kTwo = 2.0;
constexpr double kInitialScore = 1.0;
constexpr double kInitialCost = 0.0;



// The running sums of one level of the search: the weight so far, the score so far and the colour sum.
struct Level {
    double weight = 0.0;
    double score = 0.0;
    double sum[kChannels] = {0.0, 0.0, 0.0};
};

} // namespace

// 0x89c480
double __cdecl ClusterFit(int32_t *levels, int32_t count, const double *accumulated, const double *weights, const int32_t *order, double *endpoints,
                          double *axis)
{
    double colour[kMaxColours][kChannels];
    double weight[kMaxColours];
    for (int32_t i = 0; i < count; ++i) {
        const int32_t index = order[i];
        for (int32_t k = 0; k < kChannels; ++k) colour[i][k] = accumulated[index * kChannels + k];
        weight[i] = weights[index];
    }

    // The cut points: group 0 is [0, cut[0]), group 1 [cut[0], cut[1]), group 2 [cut[1], cut[2]), group 3 the rest.
    int32_t cut[kGroups] = {0, 0, 0, count};

    // The best split so far. The original keeps it in image globals (0x105de90, 0x10e7c28, starting 1.0 and 0.0) that nothing resets; the
    // first split scored below always replaces it whatever it holds (score 0 and cost -0: 0 * cost >= score * -0 for every finite pair),
    // so the carried value never matters and every call may start from the initial pair.
    double bestScore = kInitialScore;
    double bestCost = kInitialCost;

    Level outer;
    Level middle;
    Level inner;
    int32_t a = count;
    for (;;) {
        int32_t b = count;
        for (;;) {
            int32_t c = count;
            for (;;) {
                const double cost = ((-inner.sum[0]) * inner.sum[0] - inner.sum[1] * inner.sum[1]) - inner.sum[2] * inner.sum[2];
                if (inner.score * bestCost >= bestScore * cost) {
                    bestCost = cost;
                    bestScore = inner.score;
                    cut[0] = a;
                    cut[1] = b;
                    cut[2] = c;
                }
                --c;
                if (c < b || *levels < kTwoCutsOnly) break;
                for (int32_t k = 0; k < kChannels; ++k) inner.sum[k] = colour[c][k] + inner.sum[k];
                const double w = weight[c];
                const double twice = inner.weight + inner.weight;
                inner.weight = inner.weight + w;
                inner.score = inner.score + ((kInnerBase - twice) - w) * w;
            }
            --b;
            if (b < a) break;
            for (int32_t k = 0; k < kChannels; ++k) middle.sum[k] = middle.sum[k] + colour[b][k];
            const double w = weight[b];
            const double doubled = middle.weight * kTwo;
            middle.score = ((kMiddleBase - doubled) - w) * w + middle.score;
            middle.weight = w + middle.weight;
            inner = middle;
        }
        --a;
        if (a < 1) break;
        for (int32_t k = 0; k < kChannels; ++k) outer.sum[k] = outer.sum[k] + colour[a][k];
        const double w = weight[a];
        const double twice = outer.weight + outer.weight;
        outer.score = ((kOuterBase - twice) - w) * w + outer.score;
        outer.weight = w + outer.weight;
        middle = outer;
        inner = outer;
    }

    // The chosen cut: with exactly one distinct run the two middle points coincide (the second group is empty).
    const int32_t distinct = (cut[2] != count) + (cut[1] != cut[2]) + (cut[0] != cut[1]);
    if (distinct == 1) cut[1] = cut[0];

    double s2 = 0.0;
    double s1 = 0.0;
    double sum[kChannels] = {0.0, 0.0, 0.0};
    int32_t used = 0;
    int32_t i = 0;
    for (int32_t g = 0; g < kGroups; ++g) {
        if (i < cut[g]) {
            const double value = static_cast<double>(g);
            const double square = value * value;
            do {
                const double w = weight[i];
                s2 = s2 + w * square;
                s1 = s1 + w * value;
                sum[0] = sum[0] + colour[i][0] * value;
                sum[1] = sum[1] + colour[i][1] * value;
                sum[2] = sum[2] + value * colour[i][2];
                ++i;
            } while (i < cut[g]);
        }
        used += cut[g] != count;
    }

    const double spread = s2 - s1 * s1;
    const double numerator = ((-sum[0]) * sum[0] - sum[1] * sum[1]) - sum[2] * sum[2];
    const double lowFactor = -s1;
    const double highFactor = static_cast<double>(used) - s1;
    for (int32_t k = 0; k < kChannels; ++k) {
        endpoints[k] = (sum[k] * lowFactor) / spread;
        endpoints[kChannels + k] = (sum[k] * highFactor) / spread;
        axis[k] = sum[k];
    }
    *levels = distinct == 1 ? 1 : used;
    return numerator / spread;
}

namespace {

constexpr double kHalf = 0.5;
constexpr double kZero = 0.0;

// What the bounded fit derives from the group weights alone: q is the spread (the sum of squares less the square of the sum), s the sum of
// the weighted group values, k the value of the last group.
struct BoundedTerms {
    double negS;      // -s
    double e;         // k - s
    double a;         // negS^2 + q
    double b;         // e * negS + q
    double c;         // e^2 + q
    double d;         // c * a
    double z;         // q * d * d * k * k
};

BoundedTerms TermsOf(double q, double s, double k)
{
    BoundedTerms t;
    t.negS = -s;
    t.e = k - s;
    t.a = t.negS * t.negS + q;
    t.b = t.e * t.negS + q;
    t.c = t.e * t.e + q;
    t.d = t.c * t.a;
    t.z = q * t.d * t.d * k * k;
    return t;
}

// One channel: the two end points (before scaling) with the bounds low and high folded in by the (x + y) - |y - x| minimum and maximum,
// whose rounding is part of the result. `v` is the channel of the group sum V.
struct BoundedChannel {
    double m2c;       // the second clamped value times c
    double m3;        // the third clamped value
    double u;
    double r;
    double vNegS;     // v * -s
    double vE;        // v * e
};

BoundedChannel ChannelOf(const BoundedTerms &t, double q, double v, double low, double high)
{
    BoundedChannel ch;
    ch.vE = v * t.e;
    ch.vNegS = v * t.negS;
    const double p1 = q * high - ch.vNegS;
    const double p2 = q * low - ch.vNegS;
    const double p3 = q * high - ch.vE;
    const double p4 = q * low - ch.vE;
    const double p3a = p3 * t.a;
    const double p4a = p4 * t.a;
    const double p1d = p1 * t.d;
    const double p2d = p2 * t.d;
    const double m1 = (((p1 - std::fabs(p1 - kZero)) + (std::fabs(p2 - kZero) + p2)) * kHalf) * t.b;
    const double m2 = ((p3a - std::fabs(p3a - m1)) + (std::fabs(p4a - m1) + p4a)) * kHalf;
    ch.m2c = m2 * t.c;
    const double m2b = m2 * t.b;
    ch.m3 = ((p1d - std::fabs(p1d - m2b)) + (std::fabs(p2d - m2b) + p2d)) * kHalf;
    ch.r = ch.m3 - ch.m2c;
    ch.u = ch.m2c * t.negS - ch.m3 * t.e;
    return ch;
}

} // namespace

// 0x89cacf
double __cdecl BoundedClusterFit(int32_t *levels, int32_t count, const double *accumulated, const double *weights, const int32_t *order,
                                 const double *bounds, double *endpoints, double *axis)
{
    double colour[kMaxColours][kChannels];
    double weight[kMaxColours];
    for (int32_t i = 0; i < count; ++i) {
        const int32_t index = order[i];
        for (int32_t k = 0; k < kChannels; ++k) colour[i][k] = accumulated[index * kChannels + k];
        weight[i] = weights[index];
    }
    const double *lower = bounds;
    const double *upper = bounds + kChannels;

    int32_t cut[kGroups] = {0, 0, 0, count};

    // The best split so far: carried in image globals (0x105de98, 0x10e7ea8) by the original, and never mattering, as in ClusterFit.
    double bestQuality = kInitialScore;
    double bestCost = kInitialCost;

    Level outer;
    Level middle;
    Level inner;
    int32_t value = 0;      // the value of the group the last step moved a colour into
    int32_t a = count;
    for (;;) {
        int32_t b = count;
        for (;;) {
            int32_t c = count;
            for (;;) {
                const double k = static_cast<double>(value);
                const BoundedTerms t = TermsOf(inner.score, inner.weight, k);
                double accumulatedError = 0.0;
                for (int32_t ch = 0; ch < kChannels; ++ch) {
                    const double v = inner.sum[ch];
                    const BoundedChannel bc = ChannelOf(t, inner.score, v, lower[ch], upper[ch]);
                    accumulatedError = ((bc.u * bc.u + (inner.score * bc.r) * bc.r) - (v * v) * t.z) + accumulatedError;
                }
                const double candidate = inner.score * t.z;
                if (bestCost * candidate >= bestQuality * accumulatedError) {
                    bestCost = accumulatedError;
                    bestQuality = candidate;
                    cut[0] = a;
                    cut[1] = b;
                    cut[2] = c;
                }
                --c;
                if (c < b || *levels < kTwoCutsOnly) break;
                value = 3;
                for (int32_t ch = 0; ch < kChannels; ++ch) inner.sum[ch] = colour[c][ch] + inner.sum[ch];
                const double w = weight[c];
                const double twice = inner.weight + inner.weight;
                inner.score = inner.score + ((kInnerBase - twice) - w) * w;
                inner.weight = w + inner.weight;
            }
            --b;
            if (b < a) break;
            value = 2;
            for (int32_t ch = 0; ch < kChannels; ++ch) middle.sum[ch] = middle.sum[ch] + colour[b][ch];
            const double w = weight[b];
            const double doubled = middle.weight * kTwo;
            middle.score = ((kMiddleBase - doubled) - w) * w + middle.score;
            middle.weight = w + middle.weight;
            inner = middle;
        }
        --a;
        if (a < 1) break;
        value = 1;
        for (int32_t ch = 0; ch < kChannels; ++ch) outer.sum[ch] = outer.sum[ch] + colour[a][ch];
        const double w = weight[a];
        const double twice = outer.weight + outer.weight;
        outer.score = ((kOuterBase - twice) - w) * w + outer.score;
        outer.weight = w + outer.weight;
        middle = outer;
        inner = outer;
    }

    const int32_t distinct = (cut[2] != count) + (cut[1] != cut[2]) + (cut[0] != cut[1]);
    if (distinct == 1) cut[1] = cut[0];

    double s1 = 0.0;
    double s2 = 0.0;
    double sum[kChannels] = {0.0, 0.0, 0.0};
    int32_t used = 0;
    int32_t i = 0;
    for (int32_t g = 0; g < kGroups; ++g) {
        if (i < cut[g]) {
            const double groupValue = static_cast<double>(g);
            const double square = groupValue * groupValue;
            do {
                const double w = weight[i];
                s1 = s1 + w * groupValue;
                s2 = s2 + square * w;
                sum[0] = sum[0] + colour[i][0] * groupValue;
                sum[1] = sum[1] + colour[i][1] * groupValue;
                sum[2] = sum[2] + groupValue * colour[i][2];
                ++i;
            } while (i < cut[g]);
        }
        used += cut[g] != count;
    }

    const double spread = s2 - s1 * s1;
    const BoundedTerms t = TermsOf(spread, s1, static_cast<double>(used));
    double error = 0.0;
    for (int32_t ch = 0; ch < kChannels; ++ch) {
        const double v = sum[ch];
        const BoundedChannel bc = ChannelOf(t, spread, v, lower[ch], upper[ch]);
        error = ((bc.u * bc.u + (bc.r * spread) * bc.r) - (v * v) * t.z) + error;
        endpoints[ch] = (bc.m3 / t.d + bc.vNegS) / spread;
        endpoints[kChannels + ch] = (bc.m2c / t.d + bc.vE) / spread;
        axis[ch] = v;
    }
    *levels = distinct == 1 ? 1 : used;
    return error / (t.z * spread);
}

} // namespace fsh

} // namespace gimex
