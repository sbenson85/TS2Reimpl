// The FSH back end's colour axes and orderings (lane MT; src/gz/GimexFshCluster.h, 0x8995da, 0x899a2f, 0x89d691, 0x89c42d).
//
// The original keeps the working arrays of these in image globals (the second-moment matrix at 0x10e7880.., the projections at 0x10e7930,
// two scratch doubles); nothing reads them but the functions themselves, each rewrites what it reads first, so ours are locals.
// All SSE2 double, evaluated in the order written (the sums keep the original's association).
#include "gz/GimexFshCluster.h"

#include <cmath>

namespace gimex {

namespace fsh {

namespace {

constexpr int32_t kChannels = 3;
constexpr int32_t kMaxColours = 16;
constexpr double kTrace = 3.5;                  // the second-moment matrix is scaled to this trace before each round of powers
constexpr double kOne = 1.0;
constexpr int32_t kRounds = 3;                  // scale, then four squarings of the squared matrix
constexpr int32_t kSquarings = 4;

} // namespace

// 0x8995da
void __cdecl PrincipalAxis(int32_t count, const double *colours, double *axis)
{
    // The symmetric matrix [[xx, xy, xz], [xy, yy, yz], [xz, yz, zz]] of the colours' second moments.
    double xx = 0.0;
    double xy = 0.0;
    double xz = 0.0;
    double yy = 0.0;
    double yz = 0.0;
    double zz = 0.0;
    for (int32_t i = 0; i < count; ++i) {
        const double x = colours[i * kChannels];
        const double y = colours[i * kChannels + 1];
        const double z = colours[i * kChannels + 2];
        xy = x * y + xy;
        xx = x * x + xx;
        xz = xz + x * z;
        yy = yy + y * y;
        yz = yz + y * z;
        zz = z * z + zz;
    }

    // The square of the matrix before its last squaring is what the axis is read from: [p00 p01 p02; . p11 p12; . . p22].
    double p00 = 0.0;
    double p01 = 0.0;
    double p02 = 0.0;
    double p11 = 0.0;
    double p12 = 0.0;
    double p22 = 0.0;
    for (int32_t round = 0; round < kRounds; ++round) {
        const double scale = kTrace / ((xx + yy) + zz);
        xx = scale * xx;
        xy = scale * xy;
        xz = scale * xz;
        yy = scale * yy;
        yz = scale * yz;
        zz = scale * zz;
        for (int32_t step = 0; step < kSquarings; ++step) {
            p00 = (xx * xx + xy * xy) + xz * xz;
            p01 = (xx * xy + xy * yy) + xz * yz;
            p02 = (xx * xz + xy * yz) + xz * zz;
            p11 = (yy * yy + xy * xy) + yz * yz;
            p12 = (xy * xz + yy * yz) + yz * zz;
            p22 = (yz * yz + xz * xz) + zz * zz;
            xx = (p01 * p01 + p00 * p00) + p02 * p02;
            xy = (p01 * p11 + p01 * p00) + p12 * p02;
            xz = (p01 * p12 + p02 * p00) + p02 * p22;
            yy = (p11 * p11 + p01 * p01) + p12 * p12;
            yz = (p02 * p01 + p12 * p11) + p12 * p22;
            zz = (p02 * p02 + p12 * p12) + p22 * p22;
        }
    }

    // The channel with the largest diagonal term (ties and unordered values go the way the original's compares send them).
    int32_t big;
    if (xx > yy) {
        big = xx > zz ? 0 : 2;
    } else {
        big = !(yy > zz) ? 2 : 1;
    }
    const double diagonal = big == 0 ? xx : (big == 1 ? yy : zz);
    const double inverseLength = kOne / std::sqrt(diagonal);

    const double matrix[kChannels][kChannels] = {{p00, p01, p02}, {0.0, p11, p12}, {0.0, 0.0, p22}};
    int32_t k = 0;
    for (; k < big; ++k) axis[k] = inverseLength * matrix[k][big];
    for (; k < kChannels; ++k) axis[k] = inverseLength * matrix[big][k];
}

// 0x899a2f
void __cdecl FarthestPairAxis(int32_t count, const double *colours, double *axis)
{
    double farthest = 0.0;
    int32_t first = 0;
    int32_t second = 0;
    for (int32_t i = 0; i < count; ++i) {
        for (int32_t j = i; j < count; ++j) {
            const double dy = colours[i * kChannels + 1] - colours[j * kChannels + 1];
            const double dx = colours[i * kChannels] - colours[j * kChannels];
            const double dz = colours[i * kChannels + 2] - colours[j * kChannels + 2];
            const double distance = (dy * dy + dx * dx) + dz * dz;
            if (distance > farthest) {
                farthest = distance;
                first = i;
                second = j;
            }
        }
    }
    const double inverseLength = kOne / std::sqrt(farthest);
    for (int32_t k = 0; k < kChannels; ++k) axis[k] = (colours[first * kChannels + k] - colours[second * kChannels + k]) * inverseLength;
}

// 0x89d691
__declspec(noinline) void __cdecl OrderAlongAxis(int32_t count, const double *colours, const double *axis, int32_t *order, int32_t direction)
{
    int32_t pending[kMaxColours];
    double projection[kMaxColours];
    for (int32_t i = 0; i < count; ++i) {
        pending[i] = 1;
        projection[i] = (axis[1] * colours[i * kChannels + 1] + axis[0] * colours[i * kChannels]) + axis[2] * colours[i * kChannels + 2];
    }
    for (int32_t n = 0; n < count; ++n) {
        int32_t best = 0;
        while (best < count && pending[best] == 0) ++best;
        for (int32_t j = best + 1; j < count; ++j) {
            if (pending[j] == 0) continue;
            const bool takes = direction == 0 ? projection[j] < projection[best] : projection[j] <= projection[best];
            if (takes) best = j;
        }
        pending[best] = 0;
        order[n] = best;
    }
}

// 0x89c42d
__declspec(noinline) int32_t __cdecl SameOrdering(int32_t count, const int32_t *a, const int32_t *b)
{
    int32_t matched = 0;
    if (count > 0) {
        while (matched < count && a[matched] == b[matched]) ++matched;
        if (matched < count) {
            matched = 0;
            while (matched < count && a[matched] == b[count - 1 - matched]) ++matched;
        }
    }
    return matched == count;
}

} // namespace fsh

} // namespace gimex
