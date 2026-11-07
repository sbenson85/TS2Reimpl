// The FSH back end's end-point quantiser (lane MU; src/gz/GimexFshCluster.h, 0x89aa95).
//
// The colour search hands over two real end points in the colour cube (each channel scaled by its weight). A block stores them as 5-6-5
// integers, and the colours in between are the evenly spaced points of the line (3 or 4 of them). This finds the integer end points whose
// line is nearest the colours:
//  1. Each channel of each end point is scaled to 0..255 and rounded down and up to the nearest value a 5 (or 6) bit field can hold. That
//     gives four ways to round the pair in each channel (low/low, low/high, high/low, high/high), kept as the 8-bit values the decoder
//     expands them to (the field with its top bits repeated below).
//  2. For every way of each channel the in-between values are made (the middle for 3 levels, the thirds for 4), and for each colour the
//     squared distance, weighted by the colour's count, to each of the level points is tabled per channel.
//  3. The 64 ways to pick one rounding per channel are scored: for every colour the least of its distances summed over the channels,
//     over all colours. The least score wins.
//  4. The winner's end points are written out as field values, and each colour gets the code of its nearest level.
// The minimum of several values is taken as (x + y) - |y - x| (twice the minimum, rounded as written), which is part of the result.
//
// The original builds its palettes (0x10e7eb0..0x10e8030), the per-colour distance tables (0x10e8030..0x10e9830) and keeps the best score
// so far and a scratch double at 0x10e9830 / 0x10e9838, in image globals, rebuilt in full on every call (there is no cached state). A scan
// of the image's relocated references to 0x10e7eb0..0x10e9840 finds only this function (the next global, 0x10e9840, is the single-colour
// fit's), so ours are locals.
//
// Out of range: more than 16 colours overflow the original's tables; ours ignore the colours past the sixteenth in the search and still
// give them codes. A form other than 3 or 4 makes the original read an uninitialised stack slot; ours takes the "no winner" path.
#include "gz/GimexFshCluster.h"

#include <emmintrin.h>

#include <cmath>
#include <cstring>

namespace gimex {

namespace fsh {

namespace {

constexpr int32_t kChannels = 3;
constexpr int32_t kRoundings = 4;           // per channel: low/low, low/high, high/low, high/high for the two end points
constexpr int32_t kMaxColours = 16;
constexpr int32_t kMaxLevels = 4;
constexpr int32_t kCombinations = 64;       // one rounding per channel
constexpr int32_t kThreeLevels = 3;
constexpr int32_t kFourLevels = 4;
constexpr int32_t kNoChoice = -1;
constexpr int32_t kByteLimit = 0x100;
constexpr int32_t kChannelBits[kChannels] = {5, 6, 5};   // the table at 0xe4c3f0
constexpr double kByteScale = 255.0;
constexpr double kThresholdScale = 16.0;
constexpr int32_t kThirdsDivisor = 3;

// Rounding r of a channel: bit 0 picks the second end point's rounding, bit 1 the first's.
constexpr int32_t kLowLow = 0;
constexpr int32_t kLowHigh = 1;
constexpr int32_t kHighLow = 2;
constexpr int32_t kHighHigh = 3;

struct Tables {
    int32_t values[kChannels][kRoundings][kMaxLevels];            // 8-bit values: end point, end point, then the in-between ones
    double palette[kChannels * kRoundings][kMaxLevels];           // the same as fractions of the channel weight (flat: ch 2 reads below its start)
    double distance[kChannels][kRoundings][kMaxColours][kMaxLevels];
};

double Abs(double x)
{
    return std::fabs(x);
}

int32_t TruncateToInt(double x)
{
    return _mm_cvttsd_si32(_mm_set_sd(x));
}

// Step 1: the four roundings of each channel's end-point pair.
void RoundEndpoints(Tables &tables, const double *weights, const double *endpoints)
{
    for (int32_t channel = 0; channel < kChannels; ++channel) {
        const int32_t bits = kChannelBits[channel];
        const int32_t step = 1 << (8 - bits);
        const int32_t mask = kByteLimit - step;
        int32_t low[2];
        int32_t high[2];
        for (int32_t end = 0; end < 2; ++end) {
            const double weight = weights[channel];
            double wanted = 0.0;
            if (weight != 0.0) wanted = (endpoints[end * kChannels + channel] / weight) * kByteScale;
            const int32_t floored = TruncateToInt(std::floor(wanted));
            int32_t value = mask;
            if (floored < kByteLimit) value = floored;
            if (floored < 0) value = 0;
            value &= mask;
            // Round down to the value below when the field value's expansion lies above the wanted number.
            if (static_cast<double>((value >> bits) + value) > wanted) {
                const int32_t below = value - step;
                if (below >= 0) value = below;
            }
            low[end] = (value >> bits) + value;
            const int32_t above = value + step;
            if (above < kByteLimit) value = above;
            high[end] = (value >> bits) + value;
        }
        tables.values[channel][kLowLow][0] = low[0];
        tables.values[channel][kLowLow][1] = low[1];
        tables.values[channel][kLowHigh][0] = low[0];
        tables.values[channel][kLowHigh][1] = high[1];
        tables.values[channel][kHighLow][0] = high[0];
        tables.values[channel][kHighLow][1] = low[1];
        tables.values[channel][kHighHigh][0] = high[0];
        tables.values[channel][kHighHigh][1] = high[1];
    }
}

// Step 2: the in-between values, the palettes and the distance tables.
void BuildTables(Tables &tables, int32_t levels, int32_t colourCount, const double *colours, const double *counts, const double *weights)
{
    for (int32_t channel = 0; channel < kChannels; ++channel) {
        for (int32_t rounding = 0; rounding < kRoundings; ++rounding) {
            int32_t *values = tables.values[channel][rounding];
            if (levels == kThreeLevels) {
                values[2] = (values[0] + values[1]) / 2;
            } else {
                values[2] = (values[0] * 2 + 1 + values[1]) / kThirdsDivisor;
                values[3] = (values[1] * 2 + 1 + values[0]) / kThirdsDivisor;
            }
            double *palette = tables.palette[channel * kRoundings + rounding];
            for (int32_t level = 0; level < levels; ++level)
                palette[level] = (static_cast<double>(values[level]) * weights[channel]) / kByteScale;
            for (int32_t colour = 0; colour < colourCount; ++colour) {
                for (int32_t level = 0; level < levels; ++level) {
                    const double away = colours[colour * kChannels + channel] - palette[level];
                    tables.distance[channel][rounding][colour][level] = (away * counts[colour]) * away;
                }
            }
        }
    }
}

// A colour's distance to a level under one rounding per channel (the channels are added green, red, blue).
// Inlined: as a function of its own, /O1 adds the returned sum on the x87 (fld, fadd, fadd), which rounds to 24 bits
// under the game's D3D9 precision; the original adds in SSE2 (lane PJ, src/misc/X87.h).
__forceinline double Distance(const Tables &tables, const int32_t (&rounding)[kChannels], int32_t colour, int32_t level)
{
    return (tables.distance[1][rounding[1]][colour][level] + tables.distance[0][rounding[0]][colour][level]) +
           tables.distance[2][rounding[2]][colour][level];
}

// Four times (rounded as written) the least of the colour's distances to the three levels.
double LeastOfThree(const Tables &tables, const int32_t (&rounding)[kChannels], int32_t colour)
{
    const double second = Distance(tables, rounding, colour, 1);
    const double first = Distance(tables, rounding, colour, 0);
    const double ends = (second + first) - Abs(first - second);
    double middle = Distance(tables, rounding, colour, 2);
    middle = middle + middle;
    return (ends + middle) - Abs(middle - ends);
}

double LeastOfFour(const Tables &tables, const int32_t (&rounding)[kChannels], int32_t colour)
{
    const double second = Distance(tables, rounding, colour, 1);
    const double first = Distance(tables, rounding, colour, 0);
    const double third = Distance(tables, rounding, colour, 2);
    const double fourth = Distance(tables, rounding, colour, 3);
    const double lower = (second + first) - Abs(first - second);
    const double upper = (fourth + third) - Abs(third - fourth);
    return (lower + upper) - Abs(upper - lower);
}

// Step 3: the combination of roundings with the least total score, or kNoChoice when none beats the starting bound (twice the weights'
// squared length times 16: only when the weights are all zero).
int32_t ChooseRoundings(const Tables &tables, int32_t levels, int32_t colourCount, const double *weights, double &bound)
{
    bound = ((weights[0] * weights[0] + weights[1] * weights[1]) + weights[2] * weights[2]);
    bound = bound + bound;
    bound = bound * kThresholdScale;
    int32_t best = kNoChoice;
    for (int32_t combination = 0; combination < kCombinations; ++combination) {
        const int32_t rounding[kChannels] = {combination & 3, (combination >> 2) & 3, combination >> 4};
        double total = 0.0;
        // Highest colour first, as the original's fall-through switch adds them.
        for (int32_t colour = colourCount - 1; colour >= 0; --colour)
            total += levels == kThreeLevels ? LeastOfThree(tables, rounding, colour) : LeastOfFour(tables, rounding, colour);
        if (bound > total) {
            best = combination;
            bound = total;
        }
    }
    return best;
}

} // namespace

// 0x89aa95
double __cdecl QuantiseEndpoints(int32_t form, int32_t unique, const double *colours, const double *counts, const double *weights, const double *endpoints,
                                 int32_t *fitEndpoints, int32_t *codes)
{
    return QuantiseEndpointsTraced(form, unique, colours, counts, weights, endpoints, fitEndpoints, codes, nullptr);
}

double __cdecl QuantiseEndpointsTraced(int32_t form, int32_t unique, const double *colours, const double *counts, const double *weights,
                                       const double *endpoints, int32_t *fitEndpoints, int32_t *codes, QuantiseTrace *trace)
{
    Tables tables;
    std::memset(&tables, 0, sizeof tables);
    const bool supported = form == kThreeLevels || form == kFourLevels;
    const int32_t searched = unique < 0 ? 0 : (unique > kMaxColours ? kMaxColours : unique);

    int32_t best = kNoChoice;
    double bestScore = 0.0;
    if (supported) {
        RoundEndpoints(tables, weights, endpoints);
        BuildTables(tables, form, searched, colours, counts, weights);
        best = ChooseRoundings(tables, form, searched, weights, bestScore);
    }
    if (trace) {
        trace->bestScore = bestScore;
        static_assert(sizeof trace->palette == sizeof tables.palette, "trace layout");
        static_assert(sizeof trace->distance == sizeof tables.distance, "trace layout");
        std::memcpy(trace->palette, tables.palette, sizeof tables.palette);
        std::memcpy(trace->distance, tables.distance, sizeof tables.distance);
    }

    // Step 4. The roundings are read as the original does for no winner (-1): rounding 3 for every channel, and for the last channel the
    // palette group below its own, which is the middle channel's last one.
    for (int32_t channel = 0; channel < kChannels; ++channel) {
        const int32_t rounding = (best >> (channel * 2)) & 3;
        const int32_t shift = 8 - kChannelBits[channel];
        fitEndpoints[channel] = tables.values[channel][rounding][0] >> shift;
        fitEndpoints[channel + kChannels] = tables.values[channel][rounding][1] >> shift;
    }
    const double *palettes[kChannels] = {
        tables.palette[best & 3],
        tables.palette[kRoundings + ((best >> 2) & 3)],
        tables.palette[2 * kRoundings + (best >> 4)],
    };

    double total = 0.0;
    for (int32_t colour = 0; colour < unique; ++colour) {
        double nearest = (weights[0] * weights[0] + weights[1] * weights[1]) + weights[2] * weights[2];
        nearest = nearest + nearest;
        int32_t code = kNoChoice;
        for (int32_t level = 0; level < (supported ? form : 0); ++level) {
            const double green = colours[colour * kChannels + 1] - palettes[1][level];
            const double red = colours[colour * kChannels] - palettes[0][level];
            const double blue = colours[colour * kChannels + 2] - palettes[2][level];
            const double cost = ((green * green + red * red) + blue * blue) * counts[colour];
            if (nearest > cost) {
                code = level;
                nearest = cost;
            }
        }
        total = total + nearest;
        codes[colour] = code;
    }
    return total;
}

} // namespace fsh

} // namespace gimex
