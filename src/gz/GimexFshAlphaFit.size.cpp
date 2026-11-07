// The FSH back end's alpha end-point search (lane MS; src/gz/GimexFshEncode.h, 0x897b56). It sorts the values into five lists (all,
// all, the non-zero ones, the ones that are not 1, the ones that are neither), runs the end-point solver on each enabled list and
// keeps the cheapest (the solver is ours, GimexFshAlphaSolve.size.cpp). The lists and the solver's errors live in image globals, so they
// are written there.
#include "gz/GimexFshCluster.h"

#include "game/game.h"

namespace gimex {

namespace fsh {

namespace {

constexpr int32_t kConfigs = 5;
constexpr int32_t kListValues = 16;
constexpr uint32_t kListRvas[kConfigs] = {
    0x10ebb48,
    0x10ebbc8,
    0x10ebc48,
    0x10ebcc8,
    0x10ebd48,
};
constexpr uint32_t kErrorsRva = 0x10ebb20;
constexpr int32_t kLevelsOfConfig[kConfigs] = {8, 6, 6, 6, 6};
constexpr int32_t kUsesZero[kConfigs] = {0, 0, 1, 0, 1};   // the list has no zero: the code 6 is available
constexpr int32_t kUsesOne[kConfigs] = {0, 0, 0, 1, 1};    // the list has no one: the code 7 is available
constexpr int32_t kCodeZero = 6;
constexpr int32_t kCodeOne = 7;
constexpr float kZero = 0.0f;
constexpr float kOne = 1.0f;


} // namespace

// 0x897b56
void __cdecl FitAlpha(AlphaFit *fit)
{
    double *lists[kConfigs];
    for (int32_t c = 0; c < kConfigs; ++c) lists[c] = GameAddress<double>(kListRvas[c]);
    double *errors = GameAddress<double>(kErrorsRva);

    int32_t counts[kConfigs] = {};
    for (int32_t i = 0; i < fit->count; ++i) {
        const float value = fit->values[i];
        lists[0][counts[0]++] = value;
        lists[1][counts[1]++] = value;
        if (value != kZero) lists[2][counts[2]++] = value;
        if (value != kOne) lists[3][counts[3]++] = value;
        if (value != kZero && value != kOne) lists[4][counts[4]++] = value;
    }

    const bool avoidZero = fit->allowZero != 0 && counts[0] != counts[2];
    const bool avoidOne = fit->allowOne != 0 && counts[0] != counts[3];
    int32_t enabled[kConfigs];
    enabled[0] = 1;
    enabled[1] = 1;
    enabled[2] = counts[2] != counts[1];
    enabled[3] = counts[3] != counts[1];
    enabled[4] = counts[4] != counts[1];

    // ORIGINAL-BUG: a candidate list can be empty (all the values are exactly 1, or exactly 0 or 1) and the solver then writes neither
    // end points nor codes for it, yet the candidate can still be the cheapest (its error is 0). The original reads those end points from
    // its stack, which holds what the previous call left (or, for the first, whatever was there); the end points of the alpha block
    // are then random bytes. Ours keeps them 0 (not checked against a decoder: whether the codes 6 and 7, exact 0 and 1, decode right whatever the
    // end points are has not been verified). The tests clear the stack, so the original sees zeros as well. See notes/feedback/MS.md.
    int32_t endpoints[kConfigs][2] = {};
    int32_t codes[kConfigs][kListValues] = {};
    for (int32_t c = 0; c < kConfigs; ++c) {
        if (enabled[c] == 0) continue;
        errors[c] = SolveAlpha(kLevelsOfConfig[c], counts[c], lists[c], endpoints[c], codes[c]);
        int32_t keep = 1;
        if (avoidZero && kUsesZero[c] == 0) {
            if (endpoints[c][0] != 0 && endpoints[c][1] != 0) keep = 0;
        }
        enabled[c] = keep;
        if (avoidOne && kUsesOne[c] == 0) {
            int32_t usesOne = 1;
            if (endpoints[c][0] != 1 && endpoints[c][1] != 1) usesOne = 0;
            enabled[c] = usesOne & keep;
        }
    }

    int32_t best = 0;
    while (best < kConfigs && enabled[best] == 0) ++best;
    for (int32_t c = best + 1; c < kConfigs; ++c) {
        if (enabled[c] != 0 && errors[best] > errors[c]) best = c;
    }

    fit->levels = kLevelsOfConfig[best];
    fit->endpoints[0] = endpoints[best][0];
    fit->endpoints[1] = endpoints[best][1];
    int32_t next = 0;
    for (int32_t i = 0; i < fit->count; ++i) {
        const float value = fit->values[i];
        int32_t code;
        if (value == kZero && kUsesZero[best] != 0) {
            code = kCodeZero;
        } else if (value == kOne && kUsesOne[best] != 0) {
            code = kCodeOne;
        } else {
            code = codes[best][next++];
        }
        fit->codes[i] = code;
    }
}

} // namespace fsh

} // namespace gimex
