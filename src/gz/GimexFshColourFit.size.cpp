// The FSH back end's colour end-point search (lane MS; src/gz/GimexFshEncode.h, 0x897e3b). Given up to 16 colours (three doubles each)
// and three weights it
//  - scales the colours by the weights and takes their mean (the arrays live in image globals, the callees past 0x8995da read them),
//  - if all the colours lie within 1/2048 of the first, hands them to the single-colour search (FitSingleColour),
//  - merges equal colours (a count and an accumulated offset from the mean each), and with exactly two distinct colours lets the
//    end-point placement (PlaceEndpoints, 0x899b1f) place them,
//  - otherwise searches the orderings of the distinct colours along an axis (0x8995da, 0x89d691, 0x89c480, 0x89c42d, 0x899a2f,
//    0x89cacf, all original calls for now): two passes of up to 17 tries each, the ordering being changed until it repeats an earlier
//    one, then, when an end point falls outside the range, up to nine more tries with the bounded solver, and finally the solver again
//    for the winning pass.
// The orderings table is a 16 x 16 int array on the original's stack; the tries read only entries they wrote.
#include "gz/GimexFshCluster.h"

#include "game/game.h"

#include <cstring>

namespace gimex {

namespace fsh {

namespace {

constexpr uint32_t kScaledRva = 0x10eb3b0;      // [16][3] the weighted colours
constexpr uint32_t kCentredRva = 0x10eb530;     // [16][3] the weighted colours less the mean
constexpr uint32_t kMeanRva = 0x10eb6b0;        // [3]
constexpr uint32_t kUniqueRva = 0x10eb6c8;      // [16][3] the distinct weighted colours
constexpr uint32_t kAccumulatedRva = 0x10eb848; // [16][3] the sum, then the average, of the centred colours of each distinct colour
constexpr uint32_t kCountsRva = 0x10eb9c8;      // [16] how many colours each distinct colour stands for, then the fraction
constexpr uint32_t kWeightsRva = 0x10eba48;     // [3] the weights, at least 0
constexpr uint32_t kLowerRva = 0x10eba60;       // [3] the lowest value an end point may take (less the mean)
constexpr uint32_t kUpperRva = 0x10eba78;       // [3]
constexpr uint32_t kAxisRva = 0x10eba90;        // [3]
constexpr uint32_t kErrorsRva = 0x10ebaa8;      // [2] the error of each pass
constexpr uint32_t kResultsRva = 0x10ebab8;     // [2][2][3] the end points each pass found
constexpr uint32_t kInverseCountRva = 0x10ebb18;

constexpr int32_t kChannels = 3;
constexpr int32_t kMaxColours = 16;
constexpr int32_t kPasses = 2;
constexpr int32_t kTries = 16;                  // the orderings table has this many rows
constexpr int32_t kPassTries = 16;              // tries after the first, per pass
constexpr int32_t kBoundedTries = 8;
constexpr int32_t kSpecialTry = 17;
constexpr int32_t kResultDoubles = 6;
constexpr double kOne = 1.0;
constexpr double kSameColour = 0.00048828125;
constexpr int32_t kTwoColours = 2;

struct Tables {
    double (*scaled)[kChannels];
    double (*centred)[kChannels];
    double *mean;
    double (*unique)[kChannels];
    double (*accumulated)[kChannels];
    double *counts;
    double *weights;
    double *lower;
    double *upper;
    double *axis;
    double *errors;
    double *results;
};

Tables TablesOf()
{
    Tables t;
    t.scaled = reinterpret_cast<double (*)[kChannels]>(GameAddress<double>(kScaledRva));
    t.centred = reinterpret_cast<double (*)[kChannels]>(GameAddress<double>(kCentredRva));
    t.mean = GameAddress<double>(kMeanRva);
    t.unique = reinterpret_cast<double (*)[kChannels]>(GameAddress<double>(kUniqueRva));
    t.accumulated = reinterpret_cast<double (*)[kChannels]>(GameAddress<double>(kAccumulatedRva));
    t.counts = GameAddress<double>(kCountsRva);
    t.weights = GameAddress<double>(kWeightsRva);
    t.lower = GameAddress<double>(kLowerRva);
    t.upper = GameAddress<double>(kUpperRva);
    t.axis = GameAddress<double>(kAxisRva);
    t.errors = GameAddress<double>(kErrorsRva);
    t.results = GameAddress<double>(kResultsRva);
    return t;
}

struct Search {
    int32_t orders[kTries][kTries];
    int32_t unique;
    int32_t found;      // the last comparison's result; it carries over between tries and passes
};

// One try's change of ordering: re-sorts the row `row` (for each of the two directions) until it equals an earlier row. `wrapped` says
// the rows after `row` have been written as well. `tryLeft` is the try counter of the first search (-1 in the second, which has no stop
// at the last try).
void NextOrdering(Search &search, const Tables &t, int32_t row, bool wrapped, int32_t tryLeft)
{
    int32_t *order = search.orders[row];
    const int32_t before = row - 1;
    int32_t direction = 0;
    do {
        OrderAlongAxis(search.unique, &t.unique[0][0], t.axis, order, direction);
        bool matched = false;
        if (before >= 0) {
            for (int32_t q = before; q >= 0; --q) {
                search.found = SameOrdering(search.unique, search.orders[q], order);
                if (search.found != 0) {
                    matched = true;
                    break;
                }
            }
        }
        if (!matched && search.found == 0) {
            if (!wrapped || row + 1 >= kTries) break;
            for (int32_t q = row + 1; q < kTries; ++q) {
                search.found = SameOrdering(search.unique, search.orders[q], order);
                if (search.found != 0) {
                    matched = true;
                    break;
                }
            }
            if (!matched) break;
        }
        if (tryLeft == 0) break;
        ++direction;
    } while (direction < 2);
}

} // namespace

// 0x897e3b
void __cdecl FitColours(ColourFit *fit)
{
    int32_t lastPass = 0;
    Search search = {};
    if (fit == nullptr) return;
    const int32_t count = fit->count;
    if (count == 0) return;

    const Tables t = TablesOf();
    const double inverseCount = kOne / static_cast<double>(count);
    *GameAddress<double>(kInverseCountRva) = inverseCount;
    for (int32_t i = 0; i < kChannels; ++i) {
        const double weight = fit->weights[i];
        t.weights[i] = 0.0 > weight ? 0.0 : weight;
    }

    int32_t allClose = 1;
    for (int32_t channel = 0; channel < kChannels; ++channel) {
        t.mean[channel] = 0.0;
        double sum = 0.0;
        for (int32_t i = 0; i < count; ++i) {
            const double colour = fit->colours[i][channel];
            const double difference = colour - fit->colours[0][channel];
            const double distance = difference < 0.0 ? -difference : difference;
            const double scaled = t.weights[channel] * colour;
            allClose &= kSameColour > distance ? 1 : 0;
            sum += scaled;
            t.scaled[i][channel] = scaled;
            t.mean[channel] = sum;
        }
    }

    if (allClose != 0) {
        int32_t levels = fit->maxLevels;
        FitSingleColour(&levels, count, &t.scaled[0][0], t.weights, &fit->endpoints[0][0], fit->indices);
        fit->levels = levels;
        return;
    }

    for (int32_t channel = 0; channel < kChannels; ++channel) {
        const double mean = t.mean[channel] / static_cast<double>(count);
        t.mean[channel] = mean;
        for (int32_t i = 0; i < count; ++i) t.centred[i][channel] = t.scaled[i][channel] - mean;
        t.lower[channel] = -mean;
        t.upper[channel] = t.weights[channel] - mean;
    }

    // The distinct colours, in order of first appearance.
    int32_t uniqueOf[kMaxColours] = {};
    int32_t unique = 0;
    for (int32_t i = 0; i < count; ++i) {
        int32_t j = 0;
        while (j < unique && !(t.unique[j][0] == t.scaled[i][0] && t.unique[j][1] == t.scaled[i][1] && t.unique[j][2] == t.scaled[i][2])) ++j;
        if (j == unique) {
            t.unique[j][0] = t.scaled[i][0];
            t.unique[j][1] = t.scaled[i][1];
            t.unique[j][2] = t.scaled[i][2];
            t.accumulated[j][0] = t.centred[i][0];
            t.accumulated[j][1] = t.centred[i][1];
            t.accumulated[j][2] = t.centred[i][2];
            t.counts[j] = kOne;
            ++unique;
        } else {
            t.accumulated[j][0] += t.centred[i][0];
            t.accumulated[j][1] += t.centred[i][1];
            t.accumulated[j][2] += t.centred[i][2];
            t.counts[j] += kOne;
        }
        uniqueOf[i] = j;
    }
    for (int32_t j = 0; j < unique; ++j) {
        t.counts[j] = inverseCount * t.counts[j];
        t.accumulated[j][0] = t.accumulated[j][0] * inverseCount;
        t.accumulated[j][1] = t.accumulated[j][1] * inverseCount;
        t.accumulated[j][2] = t.accumulated[j][2] * inverseCount;
    }

    int32_t codes[kMaxColours] = {};
    if (unique == kTwoColours) {
        fit->levels = kTwoColours;
        PlaceEndpoints(&fit->levels, fit->maxLevels, kTwoColours, &t.unique[0][0], t.counts, t.weights,
                                              &t.unique[0][0], &fit->endpoints[0][0], codes);
        for (int32_t i = 0; i < count; ++i) fit->indices[i] = codes[uniqueOf[i]];
        return;
    }

    int32_t levelsOfPass[kPasses] = {};
    int32_t candidates[kPasses][kTries] = {};
    search.unique = unique;
    int32_t row = 0;
    bool wrapped = false;
    PrincipalAxis(count, &t.centred[0][0], t.axis);
    OrderAlongAxis(unique, &t.unique[0][0], t.axis, search.orders[0], 0);
    for (int32_t pass = 0; pass < kPasses; ++pass) {
        int32_t triesLeft = kPassTries;
        for (;;) {
            levelsOfPass[pass] = fit->maxLevels - 1;
            if (triesLeft + pass == kSpecialTry) {
                FarthestPairAxis(unique, &t.unique[0][0], t.axis);
            } else {
                const double error = ClusterFit(&levelsOfPass[pass], unique, &t.accumulated[0][0], t.counts, search.orders[row],
                                                t.results + pass * kResultDoubles, t.axis);
                lastPass = pass;
                t.errors[pass] = error;
            }
            row = (row + 1) & (kTries - 1);
            if (row == 0) wrapped = true;
            NextOrdering(search, t, row, wrapped, triesLeft);
            const int32_t old = triesLeft;
            --triesLeft;
            if (old == 0 || search.found != 0) break;
        }
        if (unique > 0) std::memcpy(candidates[pass], search.orders[row], static_cast<size_t>(unique) * sizeof(int32_t));
    }

    if (lastPass == 1) lastPass = !(t.errors[1] > t.errors[0]) ? 1 : 0;

    bool outside = false;
    for (int32_t end = 0; end < 2; ++end) {
        for (int32_t channel = 0; channel < kChannels; ++channel) {
            if (outside) continue;
            const double value = t.results[(end + lastPass * 2) * kChannels + channel];
            outside = t.lower[channel] > value || value > t.upper[channel];
        }
    }

    if (outside) {
        if (unique > 0) std::memcpy(search.orders[0], candidates[lastPass], static_cast<size_t>(unique) * sizeof(int32_t));
        int32_t k = 0;
        bool boundedWrapped = false;
        int32_t triesLeft = kBoundedTries;
        for (;;) {
            levelsOfPass[lastPass] = fit->maxLevels - 1;
            const double error = BoundedClusterFit(&levelsOfPass[lastPass], unique, &t.accumulated[0][0], t.counts, search.orders[k], t.lower,
                                                   t.results + lastPass * kResultDoubles, t.axis);
            t.errors[lastPass] = error;
            k = (k + 1) & (kTries - 1);
            if (k == 0) boundedWrapped = true;
            NextOrdering(search, t, k, boundedWrapped, -1);
            const int32_t old = triesLeft;
            --triesLeft;
            if (old == 0 || search.found != 0) break;
        }
    }

    double *source = t.results + lastPass * kResultDoubles;
    double *target = t.results + (kPasses - 1 - lastPass) * kResultDoubles;
    for (int32_t end = 0; end < 2; ++end)
        for (int32_t channel = 0; channel < kChannels; ++channel) target[end * kChannels + channel] = source[end * kChannels + channel] + t.mean[channel];
    fit->levels = levelsOfPass[lastPass] + 1;
    PlaceEndpoints(&fit->levels, fit->maxLevels, unique, &t.unique[0][0], t.counts, t.weights, target, &fit->endpoints[0][0],
                                          codes);
    for (int32_t i = 0; i < count; ++i) fit->indices[i] = codes[uniqueOf[i]];
}

} // namespace fsh

} // namespace gimex
