// Differential test for the matrix-to-quaternion helper 0x1a7570 (lane QL, nGZSceneGraph::indexedmesh::
// QuaternionFromMatrix, src/sg/cIndexedMeshBuilder.cpp): our hook-table replacement against the original at its RVA,
// outputs (four floats and the guard words after them) and the returned pointer compared bit for bit, on rotation
// matrices of both branches (positive trace, then each of the three axes), matrices that are no rotation, and floats
// with zeros, ties between the diagonal elements, infinities and NaNs (a negative root takes the CRT's path).
// RVAs these tests reach: 0x1a7570 (and the CRT root 0x82fc60 it calls).

#include "difftest.h"
#include "hook/hooks.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>

namespace {

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float Float(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

constexpr uint32_t kEdges[] = {0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x00000001, 0x007fffff, 0x7f7fffff,
                               0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001, 0x3f000000, 0xbf000000, 0x34000000};

float Any(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return float(int(rng() % 5) - 2);
    case 1: return float(int(rng() % 2001) - 1000) / 1000.0f;
    case 2: return Float(kEdges[rng() % std::size(kEdges)]);
    case 3: return Float(rng() & 0x807fffffu);
    case 4: return Float(rng());
    default: return float(int(rng() % 201) - 100) / 64.0f;
    }
}

// A rotation matrix (row-major, as the helper reads it) from an axis and an angle; the angle sometimes a half turn or
// near it so the trace is not positive.
void Rotation(float *m, std::mt19937 &rng)
{
    double ax = double(int(rng() % 7) - 3), ay = double(int(rng() % 7) - 3), az = double(int(rng() % 7) - 3);
    if (ax == 0 && ay == 0 && az == 0) az = 1;
    const double n = std::sqrt(ax * ax + ay * ay + az * az);
    ax /= n, ay /= n, az /= n;
    double angle;
    switch (rng() % 4) {
    case 0: angle = 3.14159265358979; break;
    case 1: angle = 3.14159265358979 - double(rng() % 1000) * 1e-4; break;
    default: angle = double(rng() % 62832) * 1e-4; break;
    }
    const double c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    const double r[9] = {t * ax * ax + c, t * ax * ay - s * az, t * ax * az + s * ay,
                         t * ax * ay + s * az, t * ay * ay + c, t * ay * az - s * ax,
                         t * ax * az - s * ay, t * ay * az + s * ax, t * az * az + c};
    for (int i = 0; i < 9; ++i) m[i] = float(r[i]);
    if (rng() % 2) // the transpose
        std::swap(m[1], m[3]), std::swap(m[2], m[6]), std::swap(m[5], m[7]);
}

struct alignas(16) Buf {
    float m[12];   // the matrix at 0
    float out[12]; // the result at 0, guards after it
};

} // namespace

DIFF_TEST(sg_matrix_to_quaternion, 0x1a7570)
{
    using Fn = float *(__cdecl *)(float *, const float *);
    const HookSpec *hook = nullptr;
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == 0x1a7570) hook = h;
    if (!hook) {
        ctx.Fail("no hook at 0x1a7570");
        return;
    }
    const Fn fn[2] = {ctx.Original<Fn>(0x1a7570), reinterpret_cast<Fn>(hook->replacement)};
    std::mt19937 rng(0x1a75);
    unsigned failures = 0;
    for (unsigned i = 0; i < 40000 && failures < 5; ++i) {
        Buf b[2];
        std::memset(&b[0], 0x5c, sizeof(Buf));
        switch (rng() % 6) {
        case 0:
        case 1:
        case 2: Rotation(b[0].m, rng); break;
        case 3: // a rotation with one element changed
            Rotation(b[0].m, rng);
            b[0].m[rng() % 9] = Any(rng);
            break;
        case 4: // ties on the diagonal
            for (float &x : b[0].m) x = float(int(rng() % 3) - 1) * 0.5f;
            break;
        default:
            for (int k = 0; k < 9; ++k) b[0].m[k] = Any(rng);
            break;
        }
        b[1] = b[0];
        float *r[2];
        for (unsigned s = 0; s < 2; ++s) r[s] = fn[s](b[s].out, b[s].m);
        ++ctx.cases;
        if (r[0] != b[0].out || r[1] != b[1].out || std::memcmp(&b[0], &b[1], sizeof(Buf)) != 0) {
            unsigned first = 0;
            while (first < 12 && Bits(b[0].out[first]) == Bits(b[1].out[first])) ++first;
            ctx.Fail("case %u: differs at output float %u (%08x / %08x), returns %s / %s", i, first,
                     first < 12 ? Bits(b[0].out[first]) : 0, first < 12 ? Bits(b[1].out[first]) : 0,
                     r[0] == b[0].out ? "out" : "other", r[1] == b[1].out ? "out" : "other");
            ++failures;
        }
    }
}
