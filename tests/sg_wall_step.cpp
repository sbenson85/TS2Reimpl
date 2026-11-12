// Differential tests of row LD2j (lane 261110f; src/sg/cWallDiffusionManager.h): the maths under Step 0x40c2e0, one
// light on a wall run. Light 0x40c040 projects the light's point onto the run (Project 0x40d5a0), weighs it and paints the
// colour both ways from the projected side (PaintBack 0x411500, PaintAhead 0x411700). Records come from
// tests/sg_wall_diffusion.h (wallfx) alike on both sides; every case runs at the x87's 53-bit and 24-bit precisions (the
// game's is 24-bit: Light's dot product 0x19d250 is x87, still the game's, called by both sides).
//
// Light calls the CRT's cos, sin and pow on floats widened to doubles (cvtps2pd, the routine, cvtsd2ss), through the
// SSE2 entries __libm_sse2_cos_precise / sin / pow (xmm0 (and xmm1) in, xmm0 out). The image's are byte-identical
// library matches of our libucrt.lib's objects (db/library-matches.jsonl: 0x8309c0, 0x830b80, 0x8380c0) and our object
// calls the same entries (no CPU dispatch: the compiler names the SSE2 entry); the first three tests here sweep them
// over the inputs Light can hand them (every float, strided; pow over its bases and exponents) for the result's bits and
// errno, the evidence that Light may call ours (tests/crt_equivalents.cpp's CRT_sin and CRT_pow sweep doubles).
#include "sg_wall_diffusion.h"

#include <cerrno>
#include <climits>
#include <cmath>
#include <mmintrin.h>

using namespace wallfx;
using nTSSG::WallLight;

namespace {

// ---- the CRT's routines -----------------------------------------------------------------------------------------------

void *gLibm; // the image's routine the shims call

// One double in xmm0, the result in xmm0 (the SSE2 libm's convention), returned in st0 for the C++ side.
__declspec(naked) double __cdecl ImageUnary(double)
{
    __asm {
        movsd xmm0, qword ptr [esp + 4]
        call dword ptr [gLibm]
        movsd qword ptr [esp + 4], xmm0
        fld qword ptr [esp + 4]
        ret
    }
}

// Two doubles in xmm0 and xmm1.
__declspec(naked) double __cdecl ImageBinary(double, double)
{
    __asm {
        movsd xmm0, qword ptr [esp + 4]
        movsd xmm1, qword ptr [esp + 12]
        call dword ptr [gLibm]
        movsd qword ptr [esp + 4], xmm0
        fld qword ptr [esp + 4]
        ret
    }
}

uint64_t Bits(double d)
{
    uint64_t b;
    std::memcpy(&b, &d, 8);
    return b;
}

int *ImageErrno(const difftest::Context &ctx) { return ctx.Original<int *(__cdecl *)()>(0x831b17)(); }

// The float sweep's stride: every 509th float pattern (8.4 million a precision), from a start that differs by precision.
constexpr uint32_t kFloatStride = 509;

// The image's unary routine at `rva` against `ours` on floats widened to doubles: the double's bits and errno, at both
// precisions (the routines are SSE2; their x87 fallbacks for huge arguments set their own control word, 0x27f).
template <class Ours> void FloatSweep(difftest::Context &ctx, uint32_t rva, const char *what, Ours ours)
{
    gLibm = ctx.image.At<void>(rva);
    for (int precision = 0; precision < 2; ++precision) {
        std::unique_ptr<difftest::X87Precision> single;
        if (precision) single = std::make_unique<difftest::X87Precision>(difftest::X87Pc::Bits24);
        uint32_t failed = 0;
        for (uint64_t b = precision ? kFloatStride / 2 : 0; b < 0x100000000ull; b += kFloatStride) {
            const double x = static_cast<double>(Fl(static_cast<uint32_t>(b)));
            *ImageErrno(ctx) = 0;
            errno = 0;
            const double image = ImageUnary(x);
            const int imageErrno = *ImageErrno(ctx);
            const double mine = ours(x);
            const int myErrno = errno;
            ++ctx.cases;
            if (Bits(image) != Bits(mine) || imageErrno != myErrno) {
                if (++failed <= 10)
                    ctx.Fail("%s(float %08x) at %s: %016llx errno %d vs %016llx errno %d", what, uint32_t(b),
                             precision ? "24-bit" : "53-bit", Bits(image), imageErrno, Bits(mine), myErrno);
            }
        }
    }
}

} // namespace

DIFF_TEST(sg_wall_step_libm_cos, 0x8309c0)
{
    FloatSweep(ctx, 0x8309c0, "cos", [](double x) { return std::cos(x); });
}

DIFF_TEST(sg_wall_step_libm_sin, 0x830b80)
{
    FloatSweep(ctx, 0x830b80, "sin", [](double x) { return std::sin(x); });
}

namespace {

// A pow base as Light makes it, 1 / (spread + 1) in floats, for a spread from the families around -1 and 0.
float LightBase(std::mt19937 &gen)
{
    float spread;
    switch (gen() % 6) {
    case 0: spread = std::nextafter(-1.0f, gen() % 2 ? 0.0f : -2.0f); break;
    case 1: spread = Wide(gen, 4.0f); break;
    case 2: spread = Fl(gen()); break;
    default: spread = float(gen() % 100000) / 1000.0f; break;
    }
    return 1.0f / (spread + 1.0f);
}

// A pow exponent as Light makes it, max(1, 2 distance factor), from a distance and factor drawn.
float LightExponent(std::mt19937 &gen)
{
    switch (gen() % 6) {
    case 0: return float(1 + gen() % 64);
    case 1: return Fl(gen());
    case 2: return std::nextafter(1.0f, 2.0f);
    default: {
        const float d = gen() % 2 ? Wide(gen, 64.0f) : float(gen() % 100000) / 100.0f;
        const float f = gen() % 3 ? 1.0f : (6.0f - 2.0f * Nice(gen, 1.0f)) / (2.0f * Nice(gen, 1.0f) + 6.0f);
        const float s = d * 2.0f * f;
        return 1.0f > s ? 1.0f : s;
    }
    }
}

} // namespace

// pow on float pairs: Light's bases and exponents, any float patterns, and the edges' cross product (zeros, ones,
// infinities, NaNs of their own payloads, denormals, negative bases with integer and half exponents), at both
// precisions: the double's bits and errno.
DIFF_TEST(sg_wall_step_libm_pow, 0x8380c0)
{
    gLibm = ctx.image.At<void>(0x8380c0);
    static const float kEdges[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.0f, -2.0f, 1.5f, 3.0f, -3.0f, 16777216.0f,
                                   16777217.0f * 2.0f, 1e-38f, 1e-45f, -1e-45f, 1e38f, -1e38f, 0.999999f, 1.000001f,
                                   Fl(0x7f800000u), Fl(0xff800000u), Fl(0x7fc00000u), Fl(0xffc00123u), Fl(0x7f800777u),
                                   Fl(0x007fffffu), 127.0f, 128.0f, 1024.0f, 1075.0f, -1075.0f, 149.0f, 150.0f};
    std::vector<std::pair<float, float>> inputs;
    for (float a : kEdges)
        for (float b : kEdges) inputs.push_back({a, b});
    std::mt19937 gen(0x8380c0u);
    for (int i = 0; i < 1500000; ++i) inputs.push_back({LightBase(gen), LightExponent(gen)});
    for (int i = 0; i < 1500000; ++i) inputs.push_back({Fl(gen()), Fl(gen())});
    for (int i = 0; i < 300000; ++i) inputs.push_back({-std::fabs(Wide(gen, 8.0f)), float(int(gen() % 81) - 40) / (gen() % 2 ? 1.0f : 2.0f)});
    for (int precision = 0; precision < 2; ++precision) {
        std::unique_ptr<difftest::X87Precision> single;
        if (precision) single = std::make_unique<difftest::X87Precision>(difftest::X87Pc::Bits24);
        uint32_t failed = 0;
        for (const auto &[a, b] : inputs) {
            *ImageErrno(ctx) = 0;
            errno = 0;
            const double image = ImageBinary(a, b);
            const int imageErrno = *ImageErrno(ctx);
            const double mine = std::pow(static_cast<double>(a), static_cast<double>(b));
            const int myErrno = errno;
            ++ctx.cases;
            if (Bits(image) != Bits(mine) || imageErrno != myErrno) {
                if (++failed <= 10)
                    ctx.Fail("pow(float %08x, float %08x) at %s: %016llx errno %d vs %016llx errno %d", U(a), U(b),
                             precision ? "24-bit" : "53-bit", Bits(image), imageErrno, Bits(mine), myErrno);
            }
        }
    }
}

// ---- the run and the point --------------------------------------------------------------------------------------------

namespace {

// A packed word in one of a case's modes (as tests/sg_wall_propagate.cpp's): 0 any word, 1 the case's one word, 2 mostly
// 0 with a bright byte now and then, 3 bytes from the edges (0, 1, 15-17, 128, 254, 255).
uint32_t PackedWord(std::mt19937 &gen, uint32_t mode, uint32_t uniform)
{
    static const uint8_t kBytes[] = {0, 1, 15, 16, 17, 128, 254, 255};
    switch (mode) {
    case 0: return gen();
    case 1: return uniform;
    case 2: return gen() % 5 ? 0u : (gen() % 2 ? 0xffu : gen() & 0xffu) << (8 * (gen() % 4));
    default: {
        uint32_t w = 0;
        for (int i = 0; i < 4; ++i) w = w << 8 | kBytes[gen() % std::size(kBytes)];
        return w;
    }
    }
}

// A step's run, alike on both sides from the seed: 1-6 segments (Project reads the first unchecked), FillRun's fields
// (NaNs among the floats now and then), the packed words in one mode, and the first segment's line mostly a tile's step
// in one of the eight directions from a grid point (mDiagonal mostly matching, else any flag).
struct StepRun {
    WallDiffusionRun run;
    StepRun(uint32_t seed)
    {
        std::mt19937 gen(seed * 0x9e3779b9u + 0x40c040u);
        Shape shape;
        shape.minSegments = 1;
        shape.maxSegments = gen() % 4 ? 3 : 6;
        shape.nans = gen() % 4 == 0;
        FillRun(run, gen, shape, 1);
        const uint32_t mode = gen() % 4, uniform = gen();
        for (WallDiffusionSegment &s : run.mSegments)
            for (uint32_t &w : s.mPacked) w = PackedWord(gen, mode, uniform);
        if (gen() % 5 == 0) return; // the line as FillSegment drew it
        WallDiffusionSegment &first = run.mSegments[0];
        static const int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1}, kDy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        const uint32_t k = gen() % 8;
        for (int i = 0; i < 2; ++i) first.mStart[i] = float(int(gen() % 41) - 20) + (gen() % 4 ? 0.0f : 0.5f);
        first.mEnd[0] = first.mStart[0] + float(kDx[k]);
        first.mEnd[1] = first.mStart[1] + float(kDy[k]);
        run.mDiagonal = gen() % 5 ? uint8_t(k % 2) : Flag(gen);
    }
};

// A point for Project: on the first segment's line at a fraction t of the run (t = j / 2n for j in -1..2n + 1, the
// sides' bounds, a few ulps either way; or any t in [-0.5, 1.5]), offset across it (0, +-1e-4 and around, a few tiles);
// or any Value / Wide pair, NaNs of their own payloads, infinities.
void Point(float *p, std::mt19937 &gen, const WallDiffusionRun &run)
{
    const WallDiffusionSegment &s = run.mSegments[0];
    switch (gen() % 8) {
    case 0: p[0] = Wide(gen, 30.0f), p[1] = Wide(gen, 30.0f); return;
    case 1: p[0] = Nice(gen, 30.0f), p[1] = Nice(gen, 30.0f); return;
    default: break;
    }
    const float ex = s.mEnd[0] - s.mStart[0], ey = s.mEnd[1] - s.mStart[1];
    const float l = std::sqrt(ex * ex + ey * ey);
    const float ux = ex / l, uy = ey / l;
    const float n = float(run.mSegments.size());
    const float len = run.Length();
    float t;
    if (gen() % 3) {
        t = float(int(gen() % uint32_t(2 * n + 3)) - 1) / (2.0f * n);
        for (uint32_t i = gen() % 5; i > 0 && i < 3; --i) t = std::nextafter(t, gen() % 2 ? 2.0f : -1.0f);
    } else
        t = float(gen() % 2001) / 1000.0f - 0.5f;
    float off;
    switch (gen() % 6) {
    case 0: off = 0.0f; break;
    case 1: off = gen() % 2 ? 1e-4f : -1e-4f; break;
    case 2: off = std::nextafter(gen() % 2 ? 1e-4f : -1e-4f, 0.0f); break;
    case 3: off = std::nextafter(0.0f, gen() % 2 ? 1.0f : -1.0f); break;
    default: off = Nice(gen, 4.0f); break;
    }
    p[0] = s.mStart[0] + ux * t * len - uy * off;
    p[1] = s.mStart[1] + uy * t * len + ux * off;
    if (gen() % 12 == 0) p[gen() % 2] = NaN(gen);
}

// A painter's distance: the falloff's edges (0.0625 / (1 + d (times 1.4)) in doubles, then Fixed16): 0 and -0, small and
// wide positives, near -1 / 1.4 and -1 (the division's pole), below -1, infinities, NaNs of their own payloads.
float PaintDistance(std::mt19937 &gen)
{
    auto ulps = [&](float f) {
        switch (gen() % 4) {
        case 0: return f;
        case 1: return std::nextafter(f, 1e30f);
        case 2: return std::nextafter(f, -1e30f);
        default: return std::nextafter(std::nextafter(f, -1e30f), -1e30f);
        }
    };
    switch (gen() % 12) {
    case 0: return gen() % 2 ? 0.0f : -0.0f;
    case 1: return ulps(-1.0f / 1.4f);
    case 2: return ulps(-1.0f);
    case 3: return -float(gen() % 1000) / 1000.0f;
    case 4: return Fl(0x7f800000u | (gen() & 0x80000000u));
    case 5: return NaN(gen);
    case 6: return Wide(gen, 100.0f);
    case 7: return -1.0f - float(gen() % 1000) / 100.0f;
    default: return float(gen() % 2000) / 100.0f;
    }
}

void Append(Blob &o, const WallDiffusionRun &r) { DumpRun(o, r, false); }

} // namespace

// ---- Project ----------------------------------------------------------------------------------------------------------

// Project 0x40d5a0 (thiscall, ret 0x14; int in eax) with Point's points. Each out pointer (distance, t, nearest) is a
// slot holding a marker, or null where the original checks it: t and nearest now and then; distance only where the
// point lies outside 0 <= t < 1 (the original stores the cross there unchecked), found by a first call of the original
// with every slot (made by both sides alike). eax whole, the slots and the run.
DIFF_TEST(sg_wall_step_project, 0x40d5a0)
{
    using ProjectFn = int32_t(__thiscall *)(void *, float, float, float *, float *, float *);
    auto project = ctx.Original<ProjectFn>(0x40d5a0);
    SidesAtBothPrecisions(ctx, "project", 6000, false, [&](uint32_t seed, int side) {
        StepRun sr(seed);
        WallDiffusionRun &run = sr.run;
        std::mt19937 gen(seed ^ 0x40d5a0u);
        float p[2];
        Point(p, gen, run);
        float probe[4];
        project(&run, p[0], p[1], &probe[0], &probe[1], &probe[2]);
        const bool inside = !(0.0f > probe[1]) && 1.0f > probe[1];
        float slots[4];
        for (int i = 0; i < 4; ++i) slots[i] = Fl(0x7fbad000u + uint32_t(i));
        float *distance = inside || gen() % 4 ? &slots[0] : nullptr;
        float *t = gen() % 4 ? &slots[1] : nullptr;
        float *nearest = gen() % 4 ? &slots[2] : nullptr;
        const int32_t eax = side == 0 ? project(&run, p[0], p[1], distance, t, nearest)
                                      : run.Project(p[0], p[1], distance, t, nearest);
        Blob o{U(p[0]), U(p[1]), uint32_t(eax), uint32_t(!distance), uint32_t(!t), uint32_t(!nearest)};
        for (float f : slots) o.push_back(U(f)); // into the outcome: gLog (LF) is not compared here
        Append(o, run);
        return o;
    });
}

// ---- the painters -----------------------------------------------------------------------------------------------------

// A painter (PaintBack 0x411500 or PaintAhead 0x411700: thiscall, ret 0xc, bool in al) on StepRun's run: the colour,
// the distance drawn, the side -1..2n - 1 (Light's range: Project's 0..2n - 1, and -1), for PaintBack also INT_MIN and
// other negatives (its loop never starts). al only (setl / setg), and the run whole. The originals end with emms.
template <class Ours> void PaintCases(difftest::Context &ctx, const char *what, uint32_t rva, bool back, Ours ours)
{
    using PaintFn = uint32_t(__thiscall *)(void *, const float *, float, int32_t);
    auto paint = ctx.Original<PaintFn>(rva);
    SidesAtBothPrecisions(ctx, what, 6000, false, [&](uint32_t seed, int side) {
        StepRun sr(seed);
        WallDiffusionRun &run = sr.run;
        std::mt19937 gen(seed ^ rva);
        float rgb[3];
        Color(rgb, gen);
        const float d = PaintDistance(gen);
        const int32_t sides = int32_t(run.mSegments.size() * 2);
        int32_t at = int32_t(gen() % uint32_t(sides + 1)) - 1;
        if (back && gen() % 10 == 0) at = gen() % 2 ? INT_MIN : -2 - int32_t(gen() % 5);
        const uint32_t al = side == 0 ? paint(&run, rgb, d, at) : uint32_t(ours(run, rgb, d, at));
        Blob o{U(rgb[0]), U(rgb[1]), U(rgb[2]), U(d), uint32_t(at), al & 0xffu};
        Append(o, run);
        return o;
    });
}

DIFF_TEST(sg_wall_step_paint_back, 0x411500)
{
    PaintCases(ctx, "paint back", 0x411500u, true,
               [](WallDiffusionRun &r, const float *rgb, float d, int32_t at) { return r.PaintBack(rgb, d, at); });
}

DIFF_TEST(sg_wall_step_paint_ahead, 0x411700)
{
    PaintCases(ctx, "paint ahead", 0x411700u, false,
               [](WallDiffusionRun &r, const float *rgb, float d, int32_t at) { return r.PaintAhead(rgb, d, at); });
}

// ---- Light ------------------------------------------------------------------------------------------------------------

// Light 0x40c040 (thiscall, ret 0x10, bool in al) on StepRun's run: the intensity, the spread, Point's point and a
// FillLight record (tests/sg_wall_diffusion.h; the rest of its 0x6c bytes junk). al only (Step tests al, 0x40c4f9), and the run whole (the painters' work).
DIFF_TEST(sg_wall_step_light, 0x40c040)
{
    using LightFn = uint32_t(__thiscall *)(void *, float, float, const float *, const void *);
    auto light = ctx.Original<LightFn>(0x40c040);
    SidesAtBothPrecisions(ctx, "light", 8000, false, [&](uint32_t seed, int side) {
        StepRun sr(seed);
        WallDiffusionRun &run = sr.run;
        std::mt19937 gen(seed ^ 0x40c040u);
        float p[2];
        Point(p, gen, run);
        Block<WallLight> lb(seed);
        WallLight &l = *lb.Get();
        FillLight(l, gen);
        const float intensity = Intensity(gen), spread = Spread(gen);
        const uint32_t al = side == 0 ? light(&run, intensity, spread, p, &l)
                                      : uint32_t(run.Light(intensity, spread, p, &l));
        Blob o{U(p[0]), U(p[1]), U(intensity), U(spread), al & 0xffu};
        Words(o, lb.bytes, sizeof lb.bytes);
        Append(o, run);
        return o;
    });
}
