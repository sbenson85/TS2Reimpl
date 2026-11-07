// Differential test for nGZNumerics' frame members (lane PC, src/compgeom/Frames.h): each hook table replacement against
// the original at its RVA, on random frames (every combination of the flag bits the members test, plus random words),
// vectors and matrices; outputs and returns compared bit for bit, with `out` apart from and equal to the input (the
// members' store order shows only then). Floats mix small integers, decimals, wide exponents, denormals, zeros,
// infinities and up to two NaNs per case (more from the rotation-like matrices; a quarter of the cases also get two
// NaNs of different payloads at random places): two NaNs meeting pick a payload by operand order, so this checks that
// every product, sum and difference keeps the original's order.

#include "compgeom/Frames.h"
#include "difftest.h"
#include "hook/hooks.h"

#include "shape.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

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

struct alignas(16) Case {
    AffineTransform3 frame;
    float v[12]; // the input (and, for the in-place runs, the output) at v[4]; guards around it
};

void RandomFrame(AffineTransform3 &f, std::mt19937 &rng)
{
    f.mUnknown0 = rng() % 8 ? (rng() & 0x1f) : rng();
    for (float &m : f.mMatrix) m = rng() % 3 ? Num(rng) : float(rng() % 2);
    // Rotation-like and scaled matrices too (the inverse branches see sensible data as well as noise).
    if (rng() % 3 == 0) {
        const float c = Num(rng), s = Num(rng), k = rng() % 2 ? 1.0f : Num(rng);
        const float rot[9] = {c * k, -s * k, 0, s * k, c * k, 0, 0, 0, k};
        std::memcpy(f.mMatrix, rot, sizeof(rot));
    }
    for (float &o : f.mOffset) o = Num(rng);
}

// Two NaNs with different payloads (and signs, sometimes signalling) at two random places among the frame's matrix and
// offset and the extra floats given: where both reach one product, sum or difference, the operand order shows.
void TwoNaNs(AffineTransform3 &f, float *extra, unsigned extraCount, std::mt19937 &rng)
{
    float *places[15];
    unsigned n = 0;
    for (float &m : f.mMatrix) places[n++] = &m;
    for (float &o : f.mOffset) places[n++] = &o;
    for (unsigned i = 0; i < extraCount; ++i) places[n++] = &extra[i];
    const unsigned a = rng() % n;
    unsigned b = rng() % (n - 1);
    if (b >= a) ++b;
    for (const unsigned p : {a, b}) {
        const uint32_t quiet = rng() % 4 ? 0x00400000u : 0;
        *places[p] = Float((rng() & 0x80000000u) | 0x7f800000u | quiet | (1 + rng() % 0x3ffffe));
    }
}

const HookSpec *HookAt(difftest::Context &ctx, uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    ctx.Fail("no hook at 0x%x", rva);
    return nullptr;
}

using VecFn = uint32_t(__thiscall *)(const AffineTransform3 *, float *, const float *);
using MatrixFn = uint32_t(__thiscall *)(const AffineTransform3 *, float *);
using ScaleFn = float(__thiscall *)(const AffineTransform3 *);
using TransposeFn = uint32_t(__cdecl *)(float *, const float *);

constexpr unsigned kCases = 20000;

struct Totals {
    unsigned branch[4] = {};
};

// Which branch of the inverse a frame takes: identity, rotation (transpose), uniform scale, general.
unsigned Branch(uint32_t flags)
{
    if (!(flags & 0x1e)) return 0;
    if (!(flags & 0x10)) {
        if (!(flags & 4)) return 1;
        if (!(flags & 8)) return 2;
    }
    return 3;
}

} // namespace

DIFF_TEST(CompGeomFrames_Members, 0x19c3b0)
{
    struct Vec {
        const char *name;
        uint32_t rva;
        bool returnsOut;
    };
    const Vec vecs[] = {{"VectorTo", 0x19c770, true}, {"InverseVectorTo", 0x19c250, false},
                        {"InversePointTo", 0x19c3b0, true}};
    std::mt19937 rng(0x19c3);
    Totals totals;
    unsigned failures = 0;
    for (const Vec &f : vecs) {
        const HookSpec *h = HookAt(ctx, f.rva);
        if (!h) return;
        const VecFn fn[2] = {ctx.Original<VecFn>(f.rva), reinterpret_cast<VecFn>(h->replacement)};
        for (unsigned i = 0; i < kCases && failures < 10; ++i) {
            gNaNBudget = int(rng() % 4) - 1; // none, one or two NaNs
            Case c[2];
            std::memset(&c[0], 0x5c, sizeof(Case));
            RandomFrame(c[0].frame, rng);
            for (float &x : c[0].v) x = Num(rng);
            if (rng() % 4 == 0) TwoNaNs(c[0].frame, &c[0].v[4], 3, rng);
            c[1] = c[0];
            const bool inPlace = rng() % 4 == 0;
            uint32_t r[2];
            for (unsigned s = 0; s < 2; ++s) {
                float *out = inPlace ? &c[s].v[4] : &c[s].v[0];
                r[s] = fn[s](&c[s].frame, out, &c[s].v[4]);
                r[s] = r[s] == shape::P(out) ? 1u : 0u;
            }
            ++ctx.cases;
            ++totals.branch[Branch(c[0].frame.mUnknown0)];
            const bool same = std::memcmp(&c[0], &c[1], sizeof(Case)) == 0 && (!f.returnsOut || (r[0] && r[1]));
            if (!same) {
                const unsigned o = inPlace ? 4 : 0;
                ctx.Fail("%s case %u (flags %08x, %s): %08x %08x %08x / %08x %08x %08x (returns %u %u)", f.name, i,
                         c[0].frame.mUnknown0, inPlace ? "in place" : "apart", Bits(c[0].v[o]), Bits(c[0].v[o + 1]),
                         Bits(c[0].v[o + 2]), Bits(c[1].v[o]), Bits(c[1].v[o + 1]), Bits(c[1].v[o + 2]), r[0], r[1]);
                ++failures;
            }
        }
    }
    {
        const HookSpec *h = HookAt(ctx, 0x19cb30);
        if (!h) return;
        const MatrixFn fn[2] = {ctx.Original<MatrixFn>(0x19cb30), reinterpret_cast<MatrixFn>(h->replacement)};
        for (unsigned i = 0; i < kCases && failures < 10; ++i) {
            gNaNBudget = int(rng() % 4) - 1; // none, one or two NaNs
            Case c[2];
            std::memset(&c[0], 0x5c, sizeof(Case));
            RandomFrame(c[0].frame, rng);
            if (rng() % 4 == 0) TwoNaNs(c[0].frame, nullptr, 0, rng);
            c[1] = c[0];
            uint32_t r[2];
            for (unsigned s = 0; s < 2; ++s) r[s] = fn[s](&c[s].frame, &c[s].v[1]) == shape::P(&c[s].v[1]);
            ++ctx.cases;
            ++totals.branch[Branch(c[0].frame.mUnknown0)];
            if (std::memcmp(&c[0], &c[1], sizeof(Case)) != 0 || !r[0] || !r[1]) {
                ctx.Fail("InverseMatrix case %u (flags %08x): differs", i, c[0].frame.mUnknown0);
                ++failures;
            }
        }
    }
    {
        const HookSpec *h = HookAt(ctx, 0x19cf10);
        if (!h) return;
        const ScaleFn fn[2] = {ctx.Original<ScaleFn>(0x19cf10), reinterpret_cast<ScaleFn>(h->replacement)};
        for (unsigned i = 0; i < kCases && failures < 10; ++i) {
            gNaNBudget = int(rng() % 4) - 1; // none, one or two NaNs
            AffineTransform3 frame;
            RandomFrame(frame, rng);
            if (rng() % 4 == 0) TwoNaNs(frame, nullptr, 0, rng);
            const float a = fn[0](&frame), b = fn[1](&frame);
            ++ctx.cases;
            if (Bits(a) != Bits(b)) {
                ctx.Fail("Scale case %u (flags %08x): %08x / %08x", i, frame.mUnknown0, Bits(a), Bits(b));
                ++failures;
            }
        }
    }
    {
        const HookSpec *h = HookAt(ctx, 0x1a6590);
        if (!h) return;
        const TransposeFn fn[2] = {ctx.Original<TransposeFn>(0x1a6590), reinterpret_cast<TransposeFn>(h->replacement)};
        for (unsigned i = 0; i < 2000 && failures < 10; ++i) {
            float m[2][12];
            for (float &x : m[0]) x = Float(rng()); // any bits: a copy keeps them all
            std::memcpy(m[1], m[0], sizeof(m[0]));
            const bool inPlace = i % 2 == 1;
            uint32_t r[2];
            for (unsigned s = 0; s < 2; ++s) {
                float *out = inPlace ? &m[s][1] : &m[s][0];
                r[s] = fn[s](out, inPlace ? &m[s][1] : &m[s][3]) == shape::P(out);
            }
            ++ctx.cases;
            if (std::memcmp(m[0], m[1], sizeof(m[0])) != 0 || !r[0] || !r[1]) {
                ctx.Fail("TransposeMatrix case %u (%s): differs", i, inPlace ? "in place" : "apart");
                ++failures;
            }
        }
    }
    std::printf("  inverse branches: identity %u, rotation %u, scale %u, general %u\n", totals.branch[0],
                totals.branch[1], totals.branch[2], totals.branch[3]);
    for (unsigned b = 0; b < 4; ++b)
        if (totals.branch[b] < 1000) ctx.Fail("branch %u: too few cases", b);
}
