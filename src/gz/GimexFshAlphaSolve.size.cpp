// The FSH back end's alpha end-point solver (lane MT; src/gz/GimexFshCluster.h, 0x899d55).
//
// Given up to 16 alpha values in 0..1 it finds two integer end points (0..255) and, for each value, the code of the nearest of `levels`
// evenly spaced points between them (6 or 8 levels; the codes 6 and 7, exact zero and one, are the caller's).
//
// The search works on the values minus their mean. It starts with the end points at the smallest and largest value and repeats:
//  1. cut the span between the end points into `levels` equal cells (the thresholds are the middles of the cells' borders) and give every
//     value the code of its cell;
//  2. do the least-squares fit of the end points to those codes (a two-parameter line fit written out in closed form with its
//     rounding: the (x - |x - y|) + (|y - x| + y) terms are minimum / maximum clamps built from sums, inexact, and part of the result);
//  3. when no code changed from the last pass, try moving one value at a time across a threshold (the lowest value of a cell to the cell
//     below, the highest to the cell above) and take the first move that lowers the error, going back to step 1;
//  4. stop when no code changed and no move helps, when a single cell is occupied, or after 65 passes.
// The end points are then rounded to integers (clamped to 0..255), the levels between them interpolated, every value gets the nearest
// level and the sum of the squared errors (in 0..255 units) is returned.
//
// All SSE2 double, in the association the code was compiled with (the decompiler printed several wrongly; the difference test decides).
//
// The original keeps everything in image globals (0x10ebdc8..0x10ebee8: the thresholds, the centred values, the fit's sums) that no other
// function reads (a scan of the image for those addresses finds only this function and the alpha fit's list-end constant); ours are
// locals. The decompiler also showed a double built from a stack local that is never written: that is the loop counter sharing a stack
// slot with a double, not a read of uninitialised data (see the difference test, which runs the original with the stack cleared and
// filled with different bytes and finds the same outputs and scratch bytes).
//
// Input that is read before it is written: the first pass compares the codes it assigns with `codes` as the caller passed them (all but
// the first, which is set to 0 on entry) to decide whether any code changed. A caller whose buffer already holds the first pass's
// codes goes straight to the single-move search. FitAlpha passes zeroes (ours) or whatever was on its stack (the original).
#include "gz/GimexFshCluster.h"

#include <cmath>

namespace gimex {

namespace fsh {

namespace {

constexpr int32_t kMaxValues = 16;
constexpr int32_t kMaxLevels = 8;
constexpr int32_t kPasses = 0x40;
constexpr int32_t kInterpolateFrom = 3;       // from this many levels the levels between the end points are interpolated
constexpr double kZero = 0.0;
constexpr double kHalf = 0.5;
constexpr double kOne = 1.0;
constexpr double kMinusOne = -1.0;
constexpr double kScale = 255.0;
constexpr int32_t kMaxCode = 0xff;
constexpr int32_t kNone = -1;

// ((a - |a - q|) + (|b - q| + b)) * 0.5: the middle of a, b and q written with sums, the way the original clamps (inexact, kept).
double Middle(double q, double a, double b)
{
    return ((a - std::fabs(a - q)) + (std::fabs(b - q) + b)) * kHalf;
}

} // namespace

// 0x899d55
double __cdecl SolveAlpha(int32_t levels, int32_t count, const double *values, int32_t *endpoints, int32_t *codes)
{
    return SolveAlphaTraced(levels, count, values, endpoints, codes, nullptr);
}

double __cdecl SolveAlphaTraced(int32_t levels, int32_t count, const double *values, int32_t *endpoints, int32_t *codes, AlphaTrace *trace)
{
    if (count == 0) return kZero;

    const double n = static_cast<double>(count);
    const double invN = kOne / n;
    double low = values[0];
    double high = values[0];
    double total = values[0];
    for (int32_t i = 1; i < count; ++i) {
        const double v = values[i];
        if (low > v) low = v;
        if (v > high) high = v;
        codes[0] = 0;
        total = total + v;
    }
    const double mean = total * invN;
    double centred[kMaxValues];
    for (int32_t i = 0; i < count; ++i) centred[i] = values[i] - mean;
    double lower = low - mean;
    double upper = high - mean;

    const int32_t cuts = levels - 1;
    const double top = static_cast<double>(levels) - kOne;
    int32_t used[kMaxLevels];            // how many values have each code
    int32_t occupied = 0;                // how many codes are used
    // The line fit of the last pass (its outputs are read by the single-move search).
    double sumK = 0.0;
    double sumKC = 0.0;
    double meanK = 0.0;
    double spread = 0.0;                 // V: the codes' sum of squares about their mean
    double fitCost = 0.0;
    double fitScore = 0.0;
    bool unchanged = false;
    bool fitted = false;
    int32_t passes = kPasses;

    for (;;) {
        unchanged = true;
        double threshold[kMaxLevels] = {};
        for (int32_t i = 0; i < kMaxLevels; ++i) used[i] = 0;
        if (cuts > 0) {
            const double cell = (upper - lower) / static_cast<double>(cuts);
            for (int32_t i = 0; i < cuts; ++i) threshold[i] = (static_cast<double>(i) + kHalf) * cell + lower;
        }
        used[levels - 1] = 0;
        for (int32_t i = 0; i < count; ++i) {
            int32_t code = 0;
            while (code < cuts) {
                if (threshold[code] > centred[i]) break;
                ++code;
            }
            ++used[code];
            const int32_t before = codes[i];
            codes[i] = code;
            unchanged = before == code ? unchanged : false;
        }
        occupied = 0;
        for (int32_t k = 0; k < levels; ++k) occupied += used[k] != 0;
        if (occupied == 1) break;

        sumK = 0.0;
        sumKC = 0.0;
        double sumKK = 0.0;
        for (int32_t i = 0; i < count; ++i) {
            const double k = static_cast<double>(codes[i]);
            sumK = k + sumK;
            sumKC = k * centred[i] + sumKC;
            sumKK = k * k + sumKK;
        }
        meanK = sumK * invN;
        const double negMeanK = -meanK;
        spread = sumKK - (meanK * n) * meanK;
        const double a = (n * negMeanK) * negMeanK + spread;
        const double d = top - meanK;
        const double b = (n * negMeanK) * d + spread;
        const double c = (n * d) * d + spread;
        const double ca = c * a;
        const double z = (((spread * ca) * ca) * top) * top;
        const double oneMinusMean = kOne - mean;
        const double negMean = -mean;
        const double p1 = spread * oneMinusMean - sumKC * negMeanK;
        const double p2 = spread * negMean - sumKC * negMeanK;
        const double p3 = spread * negMean - sumKC * d;
        const double p4 = spread * oneMinusMean - sumKC * d;
        const double p3a = p3 * a;
        const double p4a = p4 * a;
        const double p1ca = p1 * ca;
        const double p2ca = p2 * ca;
        const double q = Middle(kZero, p1, p2) * b;
        const double r = Middle(q, p4a, p3a);
        const double rc = r * c;
        const double rb = r * b;
        const double y = Middle(rb, p1ca, p2ca);
        const double zd = rc * negMeanK - y * d;
        const double change = y - rc;
        fitCost = ((n * zd) * zd + (spread * change) * change) - (sumKC * sumKC) * z;
        fitScore = spread * z;
        const double lowerNext = (sumKC * negMeanK + y / ca) / spread;
        const double upperNext = (sumKC * d + rc / ca) / spread;
        lower = lowerNext;
        upper = upperNext;
        fitted = true;

        if (passes-- == 0) break;
        if (!unchanged) continue;

        // The lowest and the highest value of each code.
        int32_t firstOf[kMaxLevels];
        int32_t lastOf[kMaxLevels];
        for (int32_t k = 0; k < levels; ++k) firstOf[k] = kNone;
        for (int32_t i = 0; i < count; ++i) {
            const int32_t code = codes[i];
            if (firstOf[code] == kNone) {
                firstOf[code] = i;
                lastOf[code] = i;
            } else {
                const double value = centred[i];
                if (centred[firstOf[code]] > value) firstOf[code] = i;
                if (value > centred[lastOf[code]]) lastOf[code] = i;
            }
        }

        const double widened = spread + kOne;
        bool moved = false;
        for (int32_t side = 0; side < 2 && !moved; ++side) {
            const double sign = side == 0 ? kOne : kMinusOne;
            const double sign2 = sign + sign;
            const double sumShift = invN * sign + meanK;
            const double base = (widened - sign2 * meanK) - invN;
            for (int32_t j = side; j < cuts + side; ++j) {
                if (used[j] == 0) continue;
                if (!(used[j] > 1 || occupied > 2)) continue;
                const double e = top - sumShift;
                const int32_t moving = side == 0 ? lastOf[j] : firstOf[j];
                const double shifted = sign * centred[moving] + sumKC;
                const double u = -sumShift;
                const double t = static_cast<double>(j) * sign2 + base;
                const double a2 = (e * n) * e + t;
                const double b2 = (u * n) * e + t;
                const double c2 = (u * n) * u + t;
                const double ca2 = a2 * c2;
                const double z2 = (((ca2 * t) * ca2) * top) * top;
                const double negMeanT = negMean * t;
                const double eb = e * shifted;
                const double h1 = (negMeanT - eb) * c2;
                const double ub = u * shifted;
                const double oneMinusMeanT = oneMinusMean * t;
                const double d15 = negMeanT - ub;
                const double g = oneMinusMeanT - ub;
                const double h2 = (oneMinusMeanT - eb) * c2;
                const double gz = g * ca2;
                const double q2 = Middle(kZero, g, d15) * b2;
                const double d15z = d15 * ca2;
                const double r2 = Middle(q2, h2, h1);
                const double l2 = r2 * a2;
                const double rb2 = r2 * b2;
                const double r3 = Middle(rb2, gz, d15z);
                const double zd2 = l2 * u - r3 * e;
                const double step = r3 - l2;
                const double cost2 = ((zd2 * n) * zd2 + (step * t) * step) - (shifted * shifted) * z2;
                const double score2 = z2 * t;
                if (score2 * fitCost > cost2 * fitScore) {
                    upper = (l2 / ca2 + eb) / t;
                    lower = (r3 / ca2 + ub) / t;
                    moved = true;
                    break;
                }
            }
        }
        if (moved) continue;
        break;
    }

    // Round the end points to integers, interpolate the levels between them, take the nearest level for every value.
    int32_t level[kMaxLevels];
    const double ends[2] = {lower, upper};
    if (trace) {
        trace->lower = lower;
        trace->upper = upper;
        trace->cost = fitCost;
        trace->score = fitScore;
        trace->fitted = fitted;
    }
    for (int32_t k = 0; k < 2; ++k) {
        const double rounded = std::floor((mean + ends[k]) * kScale + kHalf);
        if (trace) trace->rounded[k] = rounded;
        // The original truncates with cvttsd2si; the value is finite and tiny here (the values are 0..1), so a plain conversion is the same.
        int32_t v = static_cast<int32_t>(rounded);
        if (v < 0) {
            v = 0;
        } else if (v > kMaxCode) {
            v = kMaxCode;
        }
        level[k] = v;
    }
    if (levels >= kInterpolateFrom) {
        int32_t weight = levels - 2;
        const int32_t half = (levels - 2) / 2;
        for (int32_t k = 2; k < levels; ++k) {
            level[k] = (weight * level[0] + (k - 1) * level[1] + half) / cuts;
            --weight;
        }
    }
    endpoints[0] = level[0];
    endpoints[1] = level[1];

    double error = 0.0;
    for (int32_t i = 0; i < count; ++i) {
        const double scaled = values[i] * kScale;
        int32_t best = 0;
        for (int32_t k = 1; k < levels; ++k) {
            if (std::fabs(static_cast<double>(level[best]) - scaled) > std::fabs(static_cast<double>(level[k]) - scaled)) best = k;
        }
        codes[i] = best;
        const double off = static_cast<double>(level[best]) - values[i] * kScale;
        error = error + off * off;
    }
    return error;
}

} // namespace fsh

} // namespace gimex
