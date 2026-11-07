#pragma once
// The FSH back end's block encoders, second half (lane MT, 0x8995da..0x89d7b1; src/gz/GimexFshAxis.size.cpp, GimexFshClusterFit.size.cpp,
// GimexFshAlphaSolve.size.cpp, GimexFshPlaceEndpoints.size.cpp, GimexFshRecordEncode.size.cpp). RVAs 1.18.0.187. Names are tentative.
//
// This is the machinery under the least-squares block encoder (GimexFshEncode.h): given up to 16 colours (three doubles each), or up to
// 16 alpha values, find the two end points of a line whose evenly spaced points (3 or 4 for colours, 6 or 8 for alpha) come nearest.
//  - The colour search orders the distinct colours along an axis (0x89d691), splits the ordered run into three or four groups in every
//    possible way and keeps the split with the least error (0x89c480; 0x89cacf is the same search with the end points held inside the
//    colour cube), then takes the group sums as the next axis and orders again, until an ordering repeats (the caller, 0x897e3b).
//  - The first axis is the principal axis of the colours (0x8995da, power iteration) or the line between the two colours farthest
//    apart (0x899a2f).
//  - The end points the search finds are real numbers; 0x899b1f turns them into 5-6-5 integer end points: the quantiser (0x89aa95)
//    is asked at the real pair and at a few stretched or shrunk pairs, and the least error wins.
//  - The alpha solver 0x899d55 finds integer end points and codes for the values by the same kind of search over a single channel.
//
// Uninitialised memory and state that persists between calls (found by lane MT): see GimexFshClusterFit.size.cpp (the two cluster
// searches keep their best result in image globals that nothing resets) and GimexFshAlphaSolve.size.cpp.
#include "gz/GimexFshEncode.h"

#include <cstdint>

namespace gimex {
namespace fsh {

// ---- the axes and the orderings (src/gz/GimexFshAxis.size.cpp) ----

// 0x8995da: the unit vector along the principal axis of `count` centred colours (three doubles each; the second-moment matrix is
// scaled to trace 3.5 and raised to the 256th power three times over, then the column of its largest diagonal is normalised).
void __cdecl PrincipalAxis(int32_t count, const double *colours, double *axis);
// 0x899a2f: the unit vector from the colour of the farthest pair to the other.
void __cdecl FarthestPairAxis(int32_t count, const double *colours, double *axis);
// 0x89d691: writes into `order` the indices of the `count` (at most 16) colours sorted by their projection on the axis (a selection
// sort; among equal projections the first comes first for direction 0 and the last first otherwise).
void __cdecl OrderAlongAxis(int32_t count, const double *colours, const double *axis, int32_t *order, int32_t direction);
// 0x89c42d: 1 when the two orderings of `count` indices are the same or each other's reverse, else 0.
int32_t __cdecl SameOrdering(int32_t count, const int32_t *a, const int32_t *b);

// ---- the cluster searches (src/gz/GimexFshClusterFit.size.cpp) ----

// The best split so far of 0x89c480 / 0x89cacf is kept by the originals in image globals that nothing resets (0x105de90 and 0x10e7c28 for
// 0x89c480, 0x105de98 and 0x10e7ea8 for 0x89cacf), so it carries over from call to call. It never matters: the first split a call
// scores (every colour in the last group) has score 0 and cost -0, and 0 * cost >= quality * -0 holds for every finite carried pair, so
// that split always replaces it; ours start each call from the image's initial pair (quality 1.0, cost 0.0). Only infinities or NaNs
// in the carried pair (they cannot arise from finite input) would keep a stale split.
// 0x89c480: ordered cluster fit. `order` lists the `count` (1..16) distinct colours (`accumulated`, three doubles each; `weights`, one each)
// in axis order; every way of cutting the run into up to four groups is tried (only two cut points while *levels < 3). Writes the two end
// points (3 + 3 doubles) and the vector V of the group sums into `axis`, sets *levels to the number of groups used, returns the error.
double __cdecl ClusterFit(int32_t *levels, int32_t count, const double *accumulated, const double *weights, const int32_t *order, double *endpoints,
                          double *axis);

// 0x89cacf: the same search with each end point held between `bounds` [0..2] (lower) and [3..5] (upper) channel by channel (clamps built
// from (x + y) - |y - x|, whose rounding is part of the result). Same outputs; returns the error as a ratio to the cut's spread.
double __cdecl BoundedClusterFit(int32_t *levels, int32_t count, const double *accumulated, const double *weights, const int32_t *order,
                                 const double *bounds, double *endpoints, double *axis);

// ---- the alpha solver (src/gz/GimexFshAlphaSolve.size.cpp) ----

// 0x899d55: end points and codes for `count` (0..16) alpha values in 0..1 against `levels` (2..8) evenly spaced points. Writes the two
// integer end points (0..255) and the nearest level of every value into `codes`; returns the sum of the squared errors in 0..255 units. With
// no values nothing is written and 0 is returned. `codes` is also an input: all but the first entry are compared with the first pass's
// codes (see the source). The original does its work in image globals 0x10ebdc8..0x10ebee8 that nothing else reads; ours does not touch them.
double __cdecl SolveAlpha(int32_t levels, int32_t count, const double *values, int32_t *endpoints, int32_t *codes);

// The real-valued state SolveAlpha ends with, for the difference test (the integer results hide the order the doubles were combined in; the
// original leaves these in its image globals): the end points about the mean, the cost and score of the last fit or move, the end points'
// rounded values before the clamp, and whether any fit ran (with one occupied code from the start none does).
struct AlphaTrace {
    double lower;
    double upper;
    double cost;
    double score;
    double rounded[2];
    bool fitted;
};
double __cdecl SolveAlphaTraced(int32_t levels, int32_t count, const double *values, int32_t *endpoints, int32_t *codes, AlphaTrace *trace);

// ---- the quantiser (src/gz/GimexFshQuantise.size.cpp) ----

// 0x89aa95: the 5-6-5 integer end points nearest the two real end points `endpoints` (3 + 3 doubles) for `unique` colours (three doubles
// each, scaled by `weights`, with `counts` as their weights), for the 3-level or 4-level form (`form` 3 or 4). Writes the six field values
// to `fitEndpoints` (red, green, blue of the first, then of the second) and the nearest level of every colour to `codes`, and returns the
// sum of the weighted squared distances. The original rebuilds its palettes and distance tables at 0x10e7eb0..0x10e9840 on every call and
// nothing else reads them; ours are locals.
double __cdecl QuantiseEndpoints(int32_t form, int32_t unique, const double *colours, const double *counts, const double *weights, const double *endpoints,
                                 int32_t *fitEndpoints, int32_t *codes);

// The real-valued state the quantiser works with, for the difference test (integer results hide the order the doubles were combined in; the
// original leaves these in its image globals): the best score (at 0x10e9830), the 12 palettes (0x10e7eb0, four doubles each) and the
// distance tables ([channel][rounding][colour][level], 0x10e8030).
struct QuantiseTrace {
    double bestScore;
    double palette[12][4];
    double distance[3][4][16][4];
};
double __cdecl QuantiseEndpointsTraced(int32_t form, int32_t unique, const double *colours, const double *counts, const double *weights,
                                       const double *endpoints, int32_t *fitEndpoints, int32_t *codes, QuantiseTrace *trace);

// 0x899b1f: turns the colour search's two real end points into integer end points and codes. `*levels` (2, 3 or 4 on entry) picks the
// first form tried; the quantiser (0x89aa95) is asked for the 3-level or 4-level form (up to `maxLevels`) at the real
// end points and at a few stretched or shrunk pairs, and the result with the least error wins. Writes its six end-point integers to
// `fitEndpoints` and `unique` codes to `codes`, leaves 3 or 4 in `*levels` and returns the error.
double __cdecl PlaceEndpoints(int32_t *levels, int32_t maxLevels, int32_t unique, const double *colours, const double *counts, const double *weights,
                              const double *endpoints, int32_t *fitEndpoints, int32_t *codes);

// 0x89d7b1 (stdcall, 4 arguments): one block-compressed FSH record. Writes the 8-byte header and the 4x4 blocks of `image` (`byteCount` / 16 rows of
// 16 bytes) at `out`; `*flags` (may be null) picks the form: 0x100 / 0x200 / 8 the header codes, 4 the 4:4:4:4 colours. Returns the bytes written.
int32_t __stdcall EncodeRecord(uint8_t *out, const uint8_t *image, int32_t byteCount, uint32_t *flags);

} // namespace fsh
} // namespace gimex
