// Differential test for the 4x4 matrices and the frame / rigid members that build them (scenegraph lane TR,
// src/compgeom/Matrix4.h; RVAs 1.18.0.187): each hook table replacement against the original at its RVA on random
// inputs, outputs, returns and untouched guard words compared bit for bit. Floats mix small integers, decimals, wide
// exponents, denormals, zeros, infinities and up to two NaNs per case (a third of the cases get two NaNs of different
// payloads at random places, some with the same payload and opposite signs: two NaNs meeting pick a payload by operand
// order in SSE, so this checks every product and sum keeps the original's order). Matrices also come singular,
// permuted, near the identity and with zero pivots, and the product and inverse run with `out` aliasing an input.

#include "compgeom/Frames.h"
#include "compgeom/Matrix4.h"
#include "compgeom/Rigid.h"
#include "compgeom/SeparatingAxes.h"
#include "difftest.h"
#include "hook/hooks.h"

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

// A buffer holding the case's objects at fixed float offsets; the rest stays 0x5c bytes so stray writes show.
struct alignas(16) Buf {
    float f[96];
};
constexpr unsigned kWords = 96;

void Fill(Buf &b, std::mt19937 &rng)
{
    std::memset(&b, 0x5c, sizeof(b));
    gNaNBudget = int(rng() % 4) - 1; // none, one or two NaNs
}

// Two NaNs with different payloads (and signs, sometimes signalling; a third with the same payload and opposite signs)
// at two random places among the first n floats.
void TwoNaNs(float *f, unsigned n, std::mt19937 &rng)
{
    const unsigned a = rng() % n;
    unsigned b = rng() % (n - 1);
    if (b >= a) ++b;
    const bool tie = rng() % 3 == 0;
    const uint32_t payload = 1 + rng() % 0x3ffffe;
    for (const unsigned p : {a, b}) {
        if (tie)
            f[p] = Float((p == a ? 0u : 0x80000000u) | 0x7f800000u | 0x00400000u | payload);
        else
            f[p] = Float((rng() & 0x80000000u) | 0x7f800000u | (rng() % 4 ? 0x00400000u : 0) | (1 + rng() % 0x3ffffe));
    }
}

// A 4x4 matrix of a random kind: random floats, small integers (exact zeros: singular matrices and zero pivots turn
// up), a permutation, the identity with a few elements changed, or one with a repeated or zero row or column.
void RandomMatrix4(float *m, std::mt19937 &rng)
{
    switch (rng() % 7) {
    case 0:
    case 1:
        for (unsigned i = 0; i < 16; ++i) m[i] = Num(rng);
        break;
    case 2:
        for (unsigned i = 0; i < 16; ++i) m[i] = float(int(rng() % 5) - 2);
        break;
    case 3: {
        unsigned p[4] = {0, 1, 2, 3};
        for (unsigned i = 3; i > 0; --i) std::swap(p[i], p[rng() % (i + 1)]);
        for (unsigned i = 0; i < 16; ++i) m[i] = 0;
        for (unsigned i = 0; i < 4; ++i) m[4 * i + p[i]] = rng() % 2 ? 1.0f : Num(rng);
        break;
    }
    case 4:
        for (unsigned i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        for (unsigned n = rng() % 4; n > 0; --n) m[rng() % 16] = Num(rng);
        break;
    case 5: {
        for (unsigned i = 0; i < 16; ++i) m[i] = float(int(rng() % 9) - 4);
        const unsigned from = rng() % 4, to = rng() % 4;
        if (rng() % 2)
            for (unsigned c = 0; c < 4; ++c) m[4 * to + c] = m[4 * from + c] * float(int(rng() % 3) - 1);
        else
            for (unsigned r = 0; r < 4; ++r) m[4 * r + to] = m[4 * r + from] * float(int(rng() % 3) - 1);
        break;
    }
    default: // a rigid-ish matrix: rotation block, translation column, last row 0 0 0 1
        for (unsigned i = 0; i < 16; ++i) m[i] = 0;
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 3; ++c) m[4 * r + c] = float(int(rng() % 7) - 3) / 2.0f;
        for (unsigned r = 0; r < 3; ++r) m[4 * r + 3] = Num(rng);
        m[15] = 1.0f;
        break;
    }
}

void RandomQuaternion(float *q, std::mt19937 &rng)
{
    for (unsigned i = 0; i < 4; ++i) q[i] = Num(rng);
    if (rng() % 3 == 0) { // a unit-ish quaternion
        const float s = std::sin(Num(rng) * 0.1f), c = std::cos(Num(rng) * 0.1f);
        q[0] = s * 0.6f;
        q[1] = s * 0.8f;
        q[2] = 0;
        q[3] = c;
    }
}

// A frame at f[0..12]: the flags word (any bits), nine matrix floats, three offset floats.
void RandomFrame(AffineTransform3 &f, std::mt19937 &rng)
{
    f.mUnknown0 = rng();
    for (float &m : f.mMatrix) m = rng() % 3 ? Num(rng) : float(rng() % 2);
    for (float &o : f.mOffset) o = Num(rng);
}

const HookSpec *HookAt(difftest::Context &ctx, uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    ctx.Fail("no hook at 0x%x", rva);
    return nullptr;
}

constexpr unsigned kCases = 20000;

// Runs one function: `setup(Buf&, rng)` fills a case, `call(fn, Buf&)` runs it and returns a value that must agree
// between the two sides (1 for "returned what it should", or the bits of a float result).
template <class Fn, class Setup, class Call>
void Run(difftest::Context &ctx, const char *name, uint32_t rva, std::mt19937 &rng, Setup setup, Call call,
         unsigned cases = kCases)
{
    const HookSpec *h = HookAt(ctx, rva);
    if (!h) return;
    const Fn fn[2] = {ctx.Original<Fn>(rva), reinterpret_cast<Fn>(h->replacement)};
    unsigned failures = 0;
    for (unsigned i = 0; i < cases && failures < 5; ++i) {
        Buf b[2];
        setup(b[0], rng);
        b[1] = b[0];
        uint64_t r[2];
        for (unsigned s = 0; s < 2; ++s) r[s] = call(fn[s], b[s]);
        ++ctx.cases;
        if (std::memcmp(&b[0], &b[1], sizeof(Buf)) != 0 || r[0] != r[1]) {
            unsigned first = 0;
            while (first < kWords && Bits(b[0].f[first]) == Bits(b[1].f[first])) ++first;
            ctx.Fail("%s case %u: differs at float %u (%08x / %08x), returns %llx / %llx", name, i, first,
                     first < kWords ? Bits(b[0].f[first]) : 0, first < kWords ? Bits(b[1].f[first]) : 0,
                     static_cast<unsigned long long>(r[0]), static_cast<unsigned long long>(r[1]));
            ++failures;
        }
    }
}

uint64_t Is(const void *got, const void *want)
{
    return got == want ? 1 : 0;
}

} // namespace

// Buffer layouts: operands first, outputs further on (the guard floats between them keep their 0x5c bytes).
DIFF_TEST(CompGeomMatrix4_Multiply, 0x1a5eb0)
{
    std::mt19937 rng(0x1a5e);
    using Fn = float *(__cdecl *)(float *, const float *, const float *);
    const auto setup = [](Buf &b, std::mt19937 &rng) {
        Fill(b, rng);
        RandomMatrix4(&b.f[0], rng);
        RandomMatrix4(&b.f[16], rng);
        if (rng() % 3 == 0) TwoNaNs(b.f, 32, rng);
    };
    Run<Fn>(ctx, "Matrix4Multiply", 0x1a5eb0, rng, setup,
            [](Fn fn, Buf &b) { return Is(fn(&b.f[40], &b.f[0], &b.f[16]), &b.f[40]); });
    Run<Fn>(ctx, "Matrix4Multiply out is a", 0x1a5eb0, rng, setup,
            [](Fn fn, Buf &b) { return Is(fn(&b.f[0], &b.f[0], &b.f[16]), &b.f[0]); });
    Run<Fn>(ctx, "Matrix4Multiply out is b", 0x1a5eb0, rng, setup,
            [](Fn fn, Buf &b) { return Is(fn(&b.f[16], &b.f[0], &b.f[16]), &b.f[16]); });
    Run<Fn>(ctx, "Matrix4Multiply a is b", 0x1a5eb0, rng, setup,
            [](Fn fn, Buf &b) { return Is(fn(&b.f[40], &b.f[0], &b.f[0]), &b.f[40]); });
}

DIFF_TEST(CompGeomMatrix4_Inverse, 0x1a6410)
{
    std::mt19937 rng(0x1a64);
    using Fn = float *(__cdecl *)(float *, const float *);
    const auto setup = [](Buf &b, std::mt19937 &rng) {
        Fill(b, rng);
        RandomMatrix4(&b.f[0], rng);
        if (rng() % 3 == 0) TwoNaNs(b.f, 16, rng);
    };
    Run<Fn>(ctx, "Matrix4Inverse", 0x1a6410, rng, setup,
            [](Fn fn, Buf &b) { return Is(fn(&b.f[24], &b.f[0]), &b.f[24]); });
    Run<Fn>(ctx, "Matrix4Inverse in place", 0x1a6410, rng, setup,
            [](Fn fn, Buf &b) { return Is(fn(&b.f[0], &b.f[0]), &b.f[0]); });
}

// The decomposition and the solve are called only by the inverse in the image, but they are hooked on their own.
DIFF_TEST(CompGeomMatrix4_Lu, 0x1a7c50)
{
    std::mt19937 rng(0x1a7c);
    using DecomposeFn = void *(__thiscall *)(LuDecomposition4 *, const float *);
    Run<DecomposeFn>(ctx, "LuDecompose4", 0x1a7c50, rng,
                     [](Buf &b, std::mt19937 &rng) {
                         Fill(b, rng);
                         RandomMatrix4(&b.f[24], rng);
                         if (rng() % 3 == 0) TwoNaNs(&b.f[24], 16, rng);
                     },
                     [](DecomposeFn fn, Buf &b) {
                         return Is(fn(reinterpret_cast<LuDecomposition4 *>(&b.f[0]), &b.f[24]), &b.f[0]);
                     });
    using SolveFn = float *(__thiscall *)(const LuDecomposition4 *, float *, const float *);
    Run<SolveFn>(ctx, "LuSolve4", 0x1a8030, rng,
                 [](Buf &b, std::mt19937 &rng) {
                     Fill(b, rng);
                     for (unsigned i = 0; i < 16; ++i) b.f[i] = Num(rng);
                     int32_t *rows = reinterpret_cast<int32_t *>(&b.f[16]);
                     for (unsigned i = 0; i < 4; ++i) rows[i] = rng() % 4;
                     if (rng() % 2) { // a permutation, as the decomposition leaves it
                         for (unsigned i = 0; i < 4; ++i) rows[i] = i;
                         for (unsigned i = 3; i > 0; --i) std::swap(rows[i], rows[rng() % (i + 1)]);
                     }
                     for (unsigned i = 0; i < 4; ++i) b.f[28 + i] = Num(rng);
                     if (rng() % 3 == 0) TwoNaNs(b.f, 16, rng);
                 },
                 [](SolveFn fn, Buf &b) {
                     return Is(fn(reinterpret_cast<const LuDecomposition4 *>(&b.f[0]), &b.f[24], &b.f[28]), &b.f[24]);
                 });
}

DIFF_TEST(CompGeomMatrix4_Translation, 0x1a5870)
{
    std::mt19937 rng(0x1a58);
    using Fn = float *(__cdecl *)(float *, const float *);
    Run<Fn>(ctx, "Matrix4Translation", 0x1a5870, rng,
            [](Buf &b, std::mt19937 &rng) {
                Fill(b, rng);
                for (unsigned i = 0; i < 3; ++i) b.f[i] = Num(rng);
                if (rng() % 3 == 0) TwoNaNs(b.f, 3, rng);
            },
            [](Fn fn, Buf &b) { return Is(fn(&b.f[8], &b.f[0]), &b.f[8]); });
}

DIFF_TEST(CompGeomMatrix4_RotationScaleTranslation, 0x1a7200)
{
    std::mt19937 rng(0x1a72);
    using Fn = void(__cdecl *)(const float *, const float *, const float *, float *);
    Run<Fn>(ctx, "Matrix4FromRotationScaleTranslation", 0x1a7200, rng,
            [](Buf &b, std::mt19937 &rng) {
                Fill(b, rng);
                for (unsigned i = 0; i < 15; ++i) b.f[i] = Num(rng);
                if (rng() % 3 == 0) TwoNaNs(b.f, 15, rng);
            },
            [](Fn fn, Buf &b) {
                fn(&b.f[0], &b.f[9], &b.f[12], &b.f[24]);
                return uint64_t(1);
            });
}

DIFF_TEST(CompGeomMatrix4_Frames, 0x19c0e0)
{
    std::mt19937 rng(0x19c0);
    using ToMatrixFn = float *(__thiscall *)(const FrameMath *, float *);
    Run<ToMatrixFn>(ctx, "FrameToMatrix4", 0x19c0e0, rng,
                    [](Buf &b, std::mt19937 &rng) {
                        Fill(b, rng);
                        RandomFrame(*reinterpret_cast<AffineTransform3 *>(&b.f[0]), rng);
                        if (rng() % 3 == 0) TwoNaNs(&b.f[1], 12, rng);
                    },
                    [](ToMatrixFn fn, Buf &b) {
                        return Is(fn(reinterpret_cast<const FrameMath *>(&b.f[0]), &b.f[16]), &b.f[16]);
                    });
    using ComposeFn = FramePlacement *(__thiscall *)(FramePlacement *, const RigidTransform3 *);
    Run<ComposeFn>(ctx, "FrameComposeRigid", 0x19be70, rng,
                   [](Buf &b, std::mt19937 &rng) {
                       Fill(b, rng);
                       RandomFrame(*reinterpret_cast<AffineTransform3 *>(&b.f[0]), rng);
                       for (unsigned i = 0; i < 3; ++i) b.f[16 + i] = Num(rng);
                       RandomQuaternion(&b.f[19], rng);
                       if (rng() % 3 == 0) {
                           float pool[24];
                           std::memcpy(pool, &b.f[1], 12 * sizeof(float));
                           std::memcpy(pool + 12, &b.f[16], 7 * sizeof(float));
                           TwoNaNs(pool, 19, rng);
                           std::memcpy(&b.f[1], pool, 12 * sizeof(float));
                           std::memcpy(&b.f[16], pool + 12, 7 * sizeof(float));
                       }
                   },
                   [](ComposeFn fn, Buf &b) {
                       return Is(fn(reinterpret_cast<FramePlacement *>(&b.f[0]),
                                    reinterpret_cast<const RigidTransform3 *>(&b.f[16])),
                                 &b.f[0]);
                   });
}

DIFF_TEST(CompGeomMatrix4_Rigid, 0x19c1c0)
{
    std::mt19937 rng(0x19c1);
    const auto setup = [](Buf &b, std::mt19937 &rng) {
        Fill(b, rng);
        for (unsigned i = 0; i < 3; ++i) b.f[i] = Num(rng);
        RandomQuaternion(&b.f[3], rng);
        for (unsigned i = 8; i < 11; ++i) b.f[i] = Num(rng);
        if (rng() % 3 == 0) TwoNaNs(b.f, 11, rng);
    };
    using Fn = float *(__thiscall *)(const RigidTransform3 *, float *);
    Run<Fn>(ctx, "RigidToMatrix4", 0x19c1c0, rng, setup, [](Fn fn, Buf &b) {
        return Is(fn(reinterpret_cast<const RigidTransform3 *>(&b.f[0]), &b.f[16]), &b.f[16]);
    });
    Run<Fn>(ctx, "RigidRotateVector", 0x19c800, rng, setup, [](Fn fn, Buf &b) {
        return Is(fn(reinterpret_cast<const RigidTransform3 *>(&b.f[0]), &b.f[8]), &b.f[8]);
    });
}
