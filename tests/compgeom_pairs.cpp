// Differential test for the shape-pair tests (lane PI, src/compgeom/ShapePairs.h; RVAs 1.18.0.187): each hook table
// replacement against the original at its RVA. Every test and first hit of a convex shape against a line, a ray and a
// segment, in each placement, on random shapes, placements and lines aimed at the shape (so hits and misses both
// happen), with edge values, and with up to two NaNs of different payloads at random places in a third of the cases
// (two NaNs meeting pick a payload by operand order in SSE and by significand in the x87). The whole case buffer and the
// returned bool are compared bit for bit, so a float written before a false return counts.

#include "compgeom/ShapePairs.h"
#include "difftest.h"
#include "hook/hooks.h"

#include "shape.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace nGZCompGeom;

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

constexpr uint32_t kEdges[] = {
    0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x00000001, 0x80000001, 0x007fffff, 0x00800000, 0x7f7fffff,
    0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001, 0x7fa00000, 0x3f000000, 0x40400000, 0x34000000,
    0x2f03126f, 0xaf03126f, 0x2f031270, 0x2f03126e,
};
int gNaNBudget;

float Num(std::mt19937 &rng)
{
    float f;
    switch (rng() % 10) {
    case 0:
    case 1:
    case 2: f = float(int(rng() % 9) - 4); break;
    case 3:
    case 4:
    case 5: f = float(int(rng() % 20001) - 10000) / 100.0f; break;
    case 6: f = Float((rng() & 0x80000000u) | ((96 + rng() % 64) << 23) | (rng() & 0x7fffff)); break;
    case 7: f = Float(kEdges[rng() % std::size(kEdges)]); break;
    case 8: f = Float(rng()); break;
    default: f = Float(rng() & 0x807fffffu); break; // denormal
    }
    if (std::isnan(f)) {
        if (gNaNBudget <= 0) return 0.75f;
        --gNaNBudget;
    }
    return f;
}
float Unit(std::mt19937 &rng) { return float(int(rng() % 20001) - 10000) / 10000.0f; } // -1 .. 1
float Positive(std::mt19937 &rng) { return 0.2f + float(rng() % 3000) / 1000.0f; }

struct alignas(16) Buf {
    float f[64];
};
constexpr unsigned kShape = 0, kPlace = 8, kLinear = 24, kOut = 32;

void Fill(Buf &b, std::mt19937 &rng)
{
    std::memset(&b, 0x5c, sizeof(b));
    gNaNBudget = int(rng() % 4) - 1; // none, one or two NaNs
}

// Two NaNs with different payloads (some with the same payload and opposite signs) at two of `slots`.
void TwoNaNs(float *f, const std::vector<unsigned> &slots, std::mt19937 &rng)
{
    const unsigned n = unsigned(slots.size());
    const unsigned a = rng() % n;
    unsigned b = rng() % (n - 1);
    if (b >= a) ++b;
    const bool tie = rng() % 3 == 0;
    const uint32_t payload = 1 + rng() % 0x3ffffe;
    const unsigned picks[2] = {a, b};
    for (unsigned k = 0; k < 2; ++k) {
        float &dst = f[slots[picks[k]]];
        if (tie)
            dst = Float((k == 0 ? 0u : 0x80000000u) | 0x7f800000u | 0x00400000u | payload);
        else
            dst = Float((rng() & 0x80000000u) | 0x7f800000u | (rng() % 4 ? 0x00400000u : 0) | (1 + rng() % 0x3ffffe));
    }
}

const HookSpec *HookAt(difftest::Context &ctx, uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    ctx.Fail("no hook at 0x%x", rva);
    return nullptr;
}

constexpr unsigned kCases = 30000;

template <class Fn, class Setup, class Call>
void Run(difftest::Context &ctx, const char *name, uint32_t rva, std::mt19937 &rng, Setup setup, Call call,
         unsigned cases = kCases)
{
    const HookSpec *h = HookAt(ctx, rva);
    if (!h) return;
    const Fn fn[2] = {ctx.Original<Fn>(rva), reinterpret_cast<Fn>(h->replacement)};
    unsigned failures = 0, trues = 0;
    for (unsigned i = 0; i < cases && failures < 5; ++i) {
        Buf b[2];
        setup(b[0], rng);
        b[1] = b[0];
        uint64_t r[2];
        for (unsigned s = 0; s < 2; ++s) r[s] = call(fn[s], b[s]);
        ++ctx.cases;
        trues += r[0] ? 1 : 0;
        if (std::memcmp(&b[0], &b[1], sizeof(Buf)) != 0 || r[0] != r[1]) {
            unsigned first = 0;
            while (first < 64 && Bits(b[0].f[first]) == Bits(b[1].f[first])) ++first;
            ctx.Fail("%s case %u: differs at float %u (%08x / %08x), returns %llx / %llx", name, i, first,
                     first < 64 ? Bits(b[0].f[first]) : 0, first < 64 ? Bits(b[1].f[first]) : 0,
                     static_cast<unsigned long long>(r[0]), static_cast<unsigned long long>(r[1]));
            ++failures;
        }
    }
    // A test that never reaches both outcomes would pass for any function that returns one of them.
    if (failures == 0 && cases >= 1000 && (trues < cases / 50 || trues > cases - cases / 50))
        ctx.Fail("%s: %u of %u cases returned true (the generator misses a branch)", name, trues, cases);
}

enum Lin { kLine, kRay, kSeg };

struct Spec {
    const char *name;
    uint32_t rva;
    ShapeKind shape;
    Placement place;
    Lin lin;
    bool hit;
};

void RandomShape(Buf &b, ShapeKind s, std::mt19937 &rng, std::vector<unsigned> &used)
{
    const bool junk = rng() % 6 == 0;
    switch (s) {
    case ShapeKind::Cone:
        b.f[kShape] = junk ? Num(rng) : Positive(rng);
        b.f[kShape + 1] = junk ? Num(rng) : Positive(rng);
        used = {kShape, kShape + 1};
        break;
    case ShapeKind::Sphere:
        b.f[kShape] = junk ? Num(rng) : Positive(rng);
        used = {kShape};
        break;
    case ShapeKind::Cuboid:
        for (unsigned i = 0; i < 3; ++i) b.f[kShape + i] = junk ? Num(rng) : Positive(rng);
        used = {kShape, kShape + 1, kShape + 2};
        break;
    }
}

void RandomRotation(float *q, std::mt19937 &rng)
{
    float x = Unit(rng), y = Unit(rng), z = Unit(rng);
    const float n = std::sqrt(x * x + y * y + z * z);
    if (n < 0.01f) {
        x = 0;
        y = 0;
        z = 1;
    } else {
        x /= n;
        y /= n;
        z /= n;
    }
    const float a = Unit(rng) * 3.14159f, s = std::sin(a * 0.5f);
    q[0] = x * s;
    q[1] = y * s;
    q[2] = z * s;
    q[3] = std::cos(a * 0.5f);
}

// The placement's centre, to aim the line at.
void RandomPlace(Buf &b, Placement p, std::mt19937 &rng, std::vector<unsigned> &used, float *centre)
{
    const bool junk = rng() % 6 == 0;
    for (unsigned i = 0; i < 3; ++i) centre[i] = junk ? Num(rng) : Unit(rng) * 3.0f;
    switch (p) {
    case Placement::Oriented:
        for (unsigned i = 0; i < 3; ++i) b.f[kPlace + i] = centre[i];
        if (junk || rng() % 8 == 0)
            for (unsigned i = 0; i < 4; ++i) b.f[kPlace + 3 + i] = Num(rng);
        else
            RandomRotation(&b.f[kPlace + 3], rng);
        for (unsigned i = 0; i < 7; ++i) used.push_back(kPlace + i);
        break;
    case Placement::Translated:
        for (unsigned i = 0; i < 3; ++i) b.f[kPlace + i] = centre[i];
        for (unsigned i = 0; i < 3; ++i) used.push_back(kPlace + i);
        break;
    case Placement::InFrame: {
        // flags (kPlace), the matrix (kPlace+1..9), the offset (kPlace+10..12): a rotation (flags 2), a uniform scale
        // (flags 6), a general matrix (0x10) or the identity (0), sometimes random words.
        uint32_t flags;
        float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        switch (junk ? 9 : rng() % 6) {
        case 0: flags = 0; break;
        case 1:
        case 2: {
            flags = 2;
            float q[4];
            RandomRotation(q, rng);
            const float x = q[0], y = q[1], z = q[2], w = q[3];
            const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                                2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                                2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
            std::memcpy(m, r, sizeof(m));
            break;
        }
        case 3: {
            flags = 6;
            const float k = 0.5f + Positive(rng);
            for (unsigned i = 0; i < 9; ++i) m[i] = i % 4 == 0 ? k : 0.0f;
            break;
        }
        case 4:
            flags = 0x10;
            for (unsigned i = 0; i < 9; ++i) m[i] = (i % 4 == 0 ? 1.0f : 0.0f) + Unit(rng) * 0.4f;
            break;
        default:
            flags = rng() % 2 ? (rng() & 0x1f) : rng();
            for (float &v : m) v = Num(rng);
            break;
        }
        std::memcpy(&b.f[kPlace], &flags, 4);
        std::memcpy(&b.f[kPlace + 1], m, sizeof(m));
        for (unsigned i = 0; i < 3; ++i) b.f[kPlace + 10 + i] = centre[i];
        for (unsigned i = 0; i < 13; ++i) used.push_back(kPlace + i);
        break;
    }
    }
}

void RandomLinear(Buf &b, Lin l, std::mt19937 &rng, std::vector<unsigned> &used, const float *centre)
{
    float origin[3], dir[3];
    const bool junk = rng() % 6 == 0;
    for (unsigned i = 0; i < 3; ++i) {
        origin[i] = junk ? Num(rng) : centre[i] + Unit(rng) * 6.0f;
        const float target = centre[i] + Unit(rng) * 1.5f;
        dir[i] = junk ? Num(rng) : target - origin[i];
    }
    if (!junk) {
        static const float scales[] = {1.0f, 1.0f, 2.0f, 0.4f, 5.0f, -1.0f, 0.0f, 1.0e-3f};
        const float k = scales[rng() % std::size(scales)];
        for (float &d : dir) d *= k;
        if (rng() % 10 == 0) dir[rng() % 3] = 0.0f; // axis-parallel
    }
    for (unsigned i = 0; i < 3; ++i) {
        b.f[kLinear + i] = origin[i];
        // A segment's second point: the end.
        b.f[kLinear + 3 + i] = l == kSeg && !junk ? origin[i] + dir[i] : dir[i];
    }
    for (unsigned i = 0; i < 6; ++i) used.push_back(kLinear + i);
}

// Small lattice cases: shape sizes from {0.5, 1, 2}, whole-number centres, the identity orientation and integer lines, so
// that the parameters land exactly on 0 and 1 and the comparisons at the boundaries get decided by equality.
void SetupLattice(Buf &b, std::mt19937 &rng, const Spec &sp)
{
    Fill(b, rng);
    static const float sizes[3] = {0.5f, 1.0f, 2.0f};
    const unsigned count = sp.shape == ShapeKind::Cone ? 2 : sp.shape == ShapeKind::Sphere ? 1 : 3;
    for (unsigned i = 0; i < count; ++i) b.f[kShape + i] = sizes[rng() % 3];
    float centre[3];
    for (float &c : centre) c = float(int(rng() % 5) - 2);
    switch (sp.place) {
    case Placement::Oriented:
        for (unsigned i = 0; i < 3; ++i) b.f[kPlace + i] = centre[i];
        b.f[kPlace + 3] = b.f[kPlace + 4] = b.f[kPlace + 5] = 0.0f;
        b.f[kPlace + 6] = 1.0f;
        break;
    case Placement::Translated:
        for (unsigned i = 0; i < 3; ++i) b.f[kPlace + i] = centre[i];
        break;
    case Placement::InFrame: {
        const uint32_t flags = 0;
        std::memcpy(&b.f[kPlace], &flags, 4);
        for (unsigned i = 0; i < 9; ++i) b.f[kPlace + 1 + i] = i % 4 == 0 ? 1.0f : 0.0f;
        for (unsigned i = 0; i < 3; ++i) b.f[kPlace + 10 + i] = centre[i];
        break;
    }
    }
    for (unsigned i = 0; i < 3; ++i) {
        const float origin = float(int(rng() % 9) - 4), dir = float(int(rng() % 7) - 3);
        b.f[kLinear + i] = origin;
        b.f[kLinear + 3 + i] = sp.lin == kSeg ? origin + dir : dir;
    }
}

void SetupPair(Buf &b, std::mt19937 &rng, const Spec &sp)
{
    if (rng() % 8 == 0) return SetupLattice(b, rng, sp);
    Fill(b, rng);
    std::vector<unsigned> used;
    float centre[3];
    RandomShape(b, sp.shape, rng, used);
    RandomPlace(b, sp.place, rng, used, centre);
    RandomLinear(b, sp.lin, rng, used, centre);
    if (rng() % 3 == 0) TwoNaNs(b.f, used, rng);
}

using TestFn = bool(__cdecl *)(const void *, const void *, const void *);
using HitFn = bool(__cdecl *)(float *, const void *, const void *, const void *);

void RunSpec(difftest::Context &ctx, const Spec &sp, std::mt19937 &rng)
{
    const auto setup = [&sp](Buf &b, std::mt19937 &r) { SetupPair(b, r, sp); };
    if (sp.hit)
        Run<HitFn>(ctx, sp.name, sp.rva, rng, setup, [](HitFn fn, Buf &b) {
            return uint64_t(fn(&b.f[kOut], &b.f[kShape], &b.f[kPlace], &b.f[kLinear]));
        });
    else
        Run<TestFn>(ctx, sp.name, sp.rva, rng, setup, [](TestFn fn, Buf &b) {
            return uint64_t(fn(&b.f[kShape], &b.f[kPlace], &b.f[kLinear]));
        });
}

// One shape's three placements by three linear kinds, tests then first hits.
void RunShape(difftest::Context &ctx, std::mt19937 &rng, ShapeKind shape, const char *label, const uint32_t (&tests)[9],
              const uint32_t (&hits)[9])
{
    static const Placement places[3] = {Placement::Oriented, Placement::Translated, Placement::InFrame};
    static const char *placeNames[3] = {"Oriented", "Translated", "InFrame"};
    static const char *linNames[3] = {"Line", "Ray", "Segment"};
    char names[2][9][64];
    for (unsigned h = 0; h < 2; ++h)
        for (unsigned p = 0; p < 3; ++p)
            for (unsigned l = 0; l < 3; ++l) {
                char *name = names[h][p * 3 + l];
                std::snprintf(name, 64, "%s%s%s%s", h ? "Hit" : "Test", label, placeNames[p], linNames[l]);
                if ((h ? hits : tests)[p * 3 + l] == 0) continue; // the sphere has no oriented placement
                RunSpec(ctx, {name, (h ? hits : tests)[p * 3 + l], shape, places[p], Lin(l), h != 0}, rng);
            }
}

} // namespace

DIFF_TEST(CompGeomPairs_Cone, 0x197d70)
{
    // Each test runs at the harness's 53-bit x87 precision and again at the game's 24-bit.
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
    std::mt19937 rng(0x197d);
    // RVAs by placement (Oriented, Translated, InFrame) then Line, Ray, Segment.
    static const uint32_t tests[9] = {0x197fa0, 0x198080, 0x198170, 0x198270, 0x198320,
                                      0x1983f0, 0x197d70, 0x197e10, 0x197ed0};
    static const uint32_t hits[9] = {0x198750, 0x198820, 0x198930, 0x198a50, 0x198b00,
                                     0x198be0, 0x1984f0, 0x198590, 0x198660};
    RunShape(ctx, rng, ShapeKind::Cone, "Cone", tests, hits);
    }
}

DIFF_TEST(CompGeomPairs_ConeCores, 0x1978a0)
{
    // Each test runs at the harness's 53-bit x87 precision and again at the game's 24-bit.
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
    std::mt19937 rng(0x1978);
    using Core = bool(__stdcall *)(float *, float *, const void *, const void *);
    // Buffer: the cone (f[0..1]), the line (f[24..29]), the span (f[32], f[33]).
    const auto setup = [](Buf &b, std::mt19937 &rng) {
        Fill(b, rng);
        std::vector<unsigned> used;
        float centre[3] = {0, 0, 0};
        RandomShape(b, ShapeKind::Cone, rng, used);
        RandomLinear(b, rng() % 2 ? kSeg : kLine, rng, used, centre);
        if (rng() % 3 == 0) TwoNaNs(b.f, used, rng);
    };
    Run<Core>(ctx, "ConeLineSpan", 0x1978a0, rng, setup, [](Core fn, Buf &b) {
        return uint64_t(fn(&b.f[kOut], &b.f[kOut + 1], &b.f[kShape], &b.f[kLinear]));
    });
    Run<Core>(ctx, "ConeSegmentSpan", 0x197b00, rng, setup, [](Core fn, Buf &b) {
        return uint64_t(fn(&b.f[kOut], &b.f[kOut + 1], &b.f[kShape], &b.f[kLinear]));
    });
    }
}

DIFF_TEST(CompGeomPairs_Sphere, 0x198fc0)
{
    // Each test runs at the harness's 53-bit x87 precision and again at the game's 24-bit.
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
    std::mt19937 rng(0x198f);
    // Translated and in-frame only (the sphere has no oriented placement).
    static const uint32_t tests[9] = {0, 0, 0, 0x199230, 0x199310, 0x1993f0, 0x198fc0, 0x199090, 0x199160};
    static const uint32_t hits[9] = {0, 0, 0, 0x199740, 0x1997f0, 0x1998d0, 0x1994e0, 0x199580, 0x199650};
    RunShape(ctx, rng, ShapeKind::Sphere, "Sphere", tests, hits);
    }
}

DIFF_TEST(CompGeomPairs_Cuboid, 0x199d60)
{
    // Each test runs at the harness's 53-bit x87 precision and again at the game's 24-bit.
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
    std::mt19937 rng(0x199d);
    static const uint32_t tests[9] = {0x199f90, 0x19a070, 0x19a160, 0x19a260, 0x19a310,
                                      0x19a3e0, 0x199d60, 0x199e00, 0x199ec0};
    static const uint32_t hits[9] = {0x19a740, 0x19a810, 0x19a920, 0x19aa40, 0x19aaf0,
                                     0x19abd0, 0x19a4e0, 0x19a580, 0x19a650};
    RunShape(ctx, rng, ShapeKind::Cuboid, "Cuboid", tests, hits);
    }
}

DIFF_TEST(CompGeomPairs_SphereCuboidCores, 0x198cf0)
{
    // Each test runs at the harness's 53-bit x87 precision and again at the game's 24-bit.
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
    std::mt19937 rng(0x198c);
    using Core = bool(__stdcall *)(float *, float *, const void *, const void *);
    const auto setupFor = [](ShapeKind kind) {
        return [kind](Buf &b, std::mt19937 &rng) {
            Fill(b, rng);
            std::vector<unsigned> used;
            float centre[3] = {0, 0, 0};
            RandomShape(b, kind, rng, used);
            RandomLinear(b, rng() % 2 ? kSeg : kLine, rng, used, centre);
            if (rng() % 3 == 0) TwoNaNs(b.f, used, rng);
        };
    };
    const auto call = [](Core fn, Buf &b) { return uint64_t(fn(&b.f[kOut], &b.f[kOut + 1], &b.f[kShape], &b.f[kLinear])); };
    Run<Core>(ctx, "SphereLineSpan", 0x198cf0, rng, setupFor(ShapeKind::Sphere), call);
    Run<Core>(ctx, "SphereSegmentSpan", 0x198e50, rng, setupFor(ShapeKind::Sphere), call);
    Run<Core>(ctx, "CuboidLineSpan", 0x1999e0, rng, setupFor(ShapeKind::Cuboid), call);
    Run<Core>(ctx, "CuboidSegmentSpan", 0x199b90, rng, setupFor(ShapeKind::Cuboid), call);
    }
}

DIFF_TEST(CompGeomPairs_Distances, 0x1a2950)
{
    // Each test runs at the harness's 53-bit x87 precision and again at the game's 24-bit.
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
    std::mt19937 rng(0x1a29);
    // The point at f[0..2], the line at f[24..29]; the result's bits go to f[34..35] and the test returns whether it is under 3.
    using Dist = double(__cdecl *)(const void *, const void *);
    const auto setupFor = [](Lin kind) {
        return [kind](Buf &b, std::mt19937 &rng) {
            Fill(b, rng);
            std::vector<unsigned> used = {kShape, kShape + 1, kShape + 2};
            float point[3];
            const bool junk = rng() % 6 == 0;
            for (unsigned i = 0; i < 3; ++i) b.f[kShape + i] = point[i] = junk ? Num(rng) : Unit(rng) * 3.0f;
            RandomLinear(b, kind, rng, used, point);
            if (rng() % 3 == 0) TwoNaNs(b.f, used, rng);
        };
    };
    const auto call = [](Dist fn, Buf &b) {
        const double d = fn(&b.f[kShape], &b.f[kLinear]);
        std::memcpy(&b.f[34], &d, 8);
        return uint64_t(d < 3.0);
    };
    Run<Dist>(ctx, "PointLineDistanceSquared", 0x1a2950, rng, setupFor(kLine), call);
    Run<Dist>(ctx, "PointRayDistanceSquared", 0x1a2a80, rng, setupFor(kRay), call);
    Run<Dist>(ctx, "PointSegmentDistanceSquared", 0x1a2be0, rng, setupFor(kSeg), call);
    }
}
