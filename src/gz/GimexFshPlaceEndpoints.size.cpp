// The FSH back end's end-point placement (lane MT; src/gz/GimexFshCluster.h, 0x899b1f).
//
// The colour search (0x897e3b) ends with two real end points in the colour cube. This turns them into 5-6-5 integer end points and
// the codes of the colours: it asks the quantiser (0x89aa95, ours since lane MU) for the best integer end points near the real
// ones for the 3-level or 4-level form that was asked for, then for each alternative form (3 or 4 levels again) and each of the
// ways the real end points could be stretched or shrunk (a few fixed linear combinations of them: 2*e0 - e1 and e1, e0 and 2*e1 - e0,
// ...) asks it again, and keeps the result with the least error. Writes the winner's six end-point integers and the colours' codes,
// leaves in *levels the form it used (3 or 4), and returns its error.
//
// The original builds the candidate end points in image globals (0x10eb370..0x10eb3a0) and keeps the two best errors at 0x10eb3a0 and
// 0x10eb3a8; ours are locals. Nothing else reads them.
#include "gz/GimexFshCluster.h"

#include <cstring>

namespace gimex {

namespace fsh {

namespace {

constexpr int32_t kChannels = 3;
constexpr int32_t kMaxColours = 16;
constexpr int32_t kEndsPerCandidate = 6;
constexpr int32_t kBaseForms = 3;              // the quantiser's first form: 3 levels (4 for form 1)
constexpr int32_t kFourLevels = 4;
constexpr int32_t kKeyStride = 3;              // the candidate counts are indexed by levels - 2 + 3 * form
constexpr int32_t kCombinationsPerCandidate = 2;   // one per end point

// How a candidate's end point is made from the real ones: first * e0 + second * e1.
struct Combination {
    double first;
    double second;
};

// Row levels - 2 = 0, form 0 (the colours are only 2): the real end points stretched past either side.
constexpr Combination kStretch[2][kCombinationsPerCandidate] = {
    {{2.0, -1.0}, {0.0, 1.0}},
    {{1.0, 0.0}, {-1.0, 2.0}},
};

// Row 2 (levels - 2 = 0, form 1): six candidates between and beyond the real end points.
constexpr Combination kSixWays[6][kCombinationsPerCandidate] = {
    {{1.0, 0.0}, {0.0, 1.0}},
    {{1.0, 0.0}, {-2.0, 3.0}},
    {{3.0, -2.0}, {0.0, 1.0}},
    {{2.0, -1.0}, {-1.0, 2.0}},
    {{1.5, -0.5}, {0.0, 1.0}},
    {{1.0, 0.0}, {-0.5, 1.5}},
};

// Row 3 (levels - 2 = 1, form 1): the end points moved a half step.
constexpr Combination kHalfStep[2][kCombinationsPerCandidate] = {
    {{1.5, -0.5}, {0.0, 1.0}},
    {{1.0, 0.0}, {-0.5, 1.5}},
};

// How many candidates there are for levels - 2 + 3 * form (the original's table has seven entries; the others are zero).
int32_t CandidateCount(int32_t key)
{
    switch (key) {
    case 0: return 2;
    case 3: return 6;
    case 4: return 2;
    default: return 0;
    }
}

const Combination *CandidatesOf(int32_t key)
{
    switch (key) {
    case 0: return &kStretch[0][0];
    case 3: return &kSixWays[0][0];
    default: return &kHalfStep[0][0];
    }
}

} // namespace

// 0x899b1f
double __cdecl PlaceEndpoints(int32_t *levels, int32_t maxLevels, int32_t unique, const double *colours, const double *counts, const double *weights,
                              const double *endpoints, int32_t *fitEndpoints, int32_t *codes)
{
    // Two slots hold the best result so far and the one being tried.
    int32_t ends[2][kEndsPerCandidate];
    int32_t indices[2][kMaxColours];
    int32_t formOf[2] = {};
    double errors[2] = {};

    formOf[0] = *levels == kFourLevels ? 1 : 0;
    errors[0] = QuantiseEndpoints(formOf[0] + kBaseForms, unique, colours, counts, weights, endpoints, ends[0], indices[0]);

    *levels = *levels - 2;
    int32_t writing = 1;       // the slot the next candidate goes in; the other holds the best
    for (int32_t form = 0; form < maxLevels - 2; ++form) {
        const int32_t key = *levels + kKeyStride * form;
        for (int32_t candidate = 0; candidate < CandidateCount(key); ++candidate) {
            double tried[kEndsPerCandidate];
            const Combination *pair = CandidatesOf(key) + candidate * kCombinationsPerCandidate;
            for (int32_t end = 0; end < kCombinationsPerCandidate; ++end) {
                for (int32_t channel = 0; channel < kChannels; ++channel)
                    tried[end * kChannels + channel] = pair[end].second * endpoints[kChannels + channel] + pair[end].first * endpoints[channel];
            }
            formOf[writing] = form;
            errors[writing] = QuantiseEndpoints(form + kBaseForms, unique, colours, counts, weights, tried, ends[writing], indices[writing]);
            if (errors[writing] < errors[1 - writing]) writing = 1 - writing;
        }
    }

    const int32_t best = 1 - writing;
    for (int32_t i = 0; i < unique; ++i) codes[i] = indices[best][i];
    std::memcpy(fitEndpoints, ends[best], sizeof ends[best]);
    *levels = formOf[best] + kBaseForms;
    return errors[best];
}

} // namespace fsh

} // namespace gimex
