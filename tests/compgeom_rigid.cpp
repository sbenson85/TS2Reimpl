// Differential test for the rigid transform, the quaternion helpers, the cuboid boxes and the LU inverse (lane PH,
// src/compgeom/Rigid.h, Frames.h; RVAs 1.18.0.187): each hook table replacement against the original at its RVA on
// random inputs, outputs and returns compared bit for bit. Floats mix small integers, decimals, wide exponents,
// denormals, zeros, infinities and up to two NaNs per case (a third of the cases get two NaNs of different payloads at
// random places, some with the same payload and opposite signs: two NaNs meeting pick a payload by operand order in
// SSE and by significand in the x87, so this checks every product and sum keeps the original's order).

#include "compgeom/Frames.h"
#include "compgeom/Rigid.h"
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

// A buffer holding the case's objects at fixed float offsets (guards between them keep stray writes visible).
struct alignas(16) Buf {
    float f[48];
};

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
    const uint32_t quiet = rng() % 4 ? 0x00400000u : 0;
    for (const unsigned p : {a, b}) {
        if (tie)
            f[p] = Float((p == a ? 0u : 0x80000000u) | 0x7f800000u | 0x00400000u | payload);
        else
            f[p] = Float((rng() & 0x80000000u) | 0x7f800000u | (rng() % 4 ? 0x00400000u : 0) | (1 + rng() % 0x3ffffe));
    }
    (void)quiet;
}

void RandomFrame(AffineTransform3 &f, std::mt19937 &rng)
{
    f.mUnknown0 = rng() % 8 ? (rng() & 0x1f) : rng();
    if (rng() % 2) f.mUnknown0 &= ~0x18u; // half the frames keep the axes
    for (float &m : f.mMatrix) m = rng() % 3 ? Num(rng) : float(rng() % 2);
    if (rng() % 3 == 0) {
        const float c = Num(rng), s = Num(rng), k = rng() % 2 ? 1.0f : Num(rng);
        const float rot[9] = {c * k, -s * k, 0, s * k, c * k, 0, 0, 0, k};
        std::memcpy(f.mMatrix, rot, sizeof(rot));
    }
    for (float &o : f.mOffset) o = Num(rng);
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
            while (first < 48 && Bits(b[0].f[first]) == Bits(b[1].f[first])) ++first;
            ctx.Fail("%s case %u: differs at float %u (%08x / %08x), returns %llx / %llx", name, i, first,
                     first < 48 ? Bits(b[0].f[first]) : 0, first < 48 ? Bits(b[1].f[first]) : 0,
                     static_cast<unsigned long long>(r[0]), static_cast<unsigned long long>(r[1]));
            ++failures;
        }
    }
}

uint64_t Is(const void *got, const void *want)
{
    return got == want ? 1 : 0;
}
uint64_t DoubleBits(double d)
{
    uint64_t b;
    std::memcpy(&b, &d, 8);
    return b;
}

} // namespace

// Buffer layouts: rigid at f[0..6], vectors/points at f[8..], outputs further on.
DIFF_TEST(CompGeomRigid_Transforms, 0x19c900)
{
    std::mt19937 rng(0x19c9);
    const auto rigidSetup = [](Buf &b, std::mt19937 &rng) {
        Fill(b, rng);
        for (unsigned i = 0; i < 3; ++i) b.f[i] = Num(rng);
        RandomQuaternion(&b.f[3], rng);
        for (unsigned i = 8; i < 14; ++i) b.f[i] = Num(rng);
        if (rng() % 3 == 0) TwoNaNs(b.f, 11, rng);
    };
    using PointFn = void(__thiscall *)(const RigidTransform3 *, float *);
    using VectorFn = float *(__thiscall *)(const RigidTransform3 *, float *);
    using MatrixFn = float *(__thiscall *)(const RigidTransform3 *, float *);
    Run<PointFn>(ctx, "RigidTransformPoint", 0x19c900, rng, rigidSetup, [](PointFn fn, Buf &b) {
        fn(reinterpret_cast<RigidTransform3 *>(b.f), &b.f[8]);
        return uint64_t(1);
    });
    Run<PointFn>(ctx, "RigidInverseTransformPoint", 0x19c6d0, rng, rigidSetup, [](PointFn fn, Buf &b) {
        fn(reinterpret_cast<RigidTransform3 *>(b.f), &b.f[8]);
        return uint64_t(1);
    });
    Run<VectorFn>(ctx, "RigidInverseTransformVector", 0x19c390, rng, rigidSetup, [](VectorFn fn, Buf &b) {
        return Is(fn(reinterpret_cast<RigidTransform3 *>(b.f), &b.f[8]), &b.f[8]);
    });
    using ComposeFn = RigidTransform3 *(__thiscall *)(RigidTransform3 *, const RigidTransform3 *);
    Run<ComposeFn>(ctx, "RigidCompose", 0x19c080, rng,
                   [](Buf &b, std::mt19937 &rng) {
                       Fill(b, rng);
                       for (unsigned i = 0; i < 3; ++i) b.f[i] = Num(rng);
                       RandomQuaternion(&b.f[3], rng);
                       for (unsigned i = 8; i < 11; ++i) b.f[i] = Num(rng);
                       RandomQuaternion(&b.f[11], rng);
                       if (rng() % 3 == 0) TwoNaNs(b.f, 15, rng);
                   },
                   [](ComposeFn fn, Buf &b) {
                       return Is(fn(reinterpret_cast<RigidTransform3 *>(b.f), reinterpret_cast<RigidTransform3 *>(&b.f[8])),
                                 b.f);
                   });
    Run<MatrixFn>(ctx, "RigidToMatrix", 0x19c230, rng, rigidSetup, [](MatrixFn fn, Buf &b) {
        return Is(fn(reinterpret_cast<RigidTransform3 *>(b.f), &b.f[12]), &b.f[12]);
    });
}

DIFF_TEST(CompGeomRigid_Quaternion, 0x1a77f0)
{
    std::mt19937 rng(0x1a77);
    const auto setup = [](Buf &b, std::mt19937 &rng) {
        Fill(b, rng);
        RandomQuaternion(&b.f[0], rng);
        for (unsigned i = 8; i < 11; ++i) b.f[i] = Num(rng);
        if (rng() % 3 == 0) TwoNaNs(b.f, 11, rng);
    };
    using Fn = float *(__cdecl *)(float *, const float *);
    Run<Fn>(ctx, "QuaternionToMatrix", 0x1a77f0, rng, setup,
            [](Fn fn, Buf &b) { return Is(fn(&b.f[16], &b.f[0]), &b.f[16]); });
    Run<Fn>(ctx, "RotateByInverseQuaternion", 0x1a6ad0, rng, setup,
            [](Fn fn, Buf &b) { return Is(fn(&b.f[8], &b.f[0]), &b.f[8]); });
    using RotFn = void(__thiscall *)(AffineTransform3 *, const float *);
    Run<RotFn>(ctx, "SetRotation", 0x19d0d0, rng,
               [](Buf &b, std::mt19937 &rng) {
                   Fill(b, rng);
                   RandomFrame(*reinterpret_cast<AffineTransform3 *>(&b.f[16]), rng);
                   RandomQuaternion(&b.f[0], rng);
                   if (rng() % 3 == 0) TwoNaNs(b.f, 4, rng);
               },
               [](RotFn fn, Buf &b) {
                   fn(reinterpret_cast<AffineTransform3 *>(&b.f[16]), &b.f[0]);
                   return uint64_t(1);
               });
}

// Dot87 is x87 arithmetic whose rounding follows the thread's precision control: the cases run at the harness's
// 53-bit and again at the game's 24-bit (D3D9 without FPU_PRESERVE, notes/plan.md "x87"), where the products and sums
// round. Nearby floats (a = b + a few ulps) make products whose low bits matter at 24-bit.
DIFF_TEST(CompGeomRigid_Dot87, 0x18fbf0)
{
    std::mt19937 rng(0x18fb);
    using Fn = double(__cdecl *)(const float *, const float *);
    const auto setup = [](Buf &b, std::mt19937 &rng) {
        Fill(b, rng);
        for (unsigned i = 0; i < 6; ++i) b.f[i] = Num(rng);
        if (rng() % 4 == 0)
            for (unsigned i = 0; i < 3; ++i) b.f[i + 3] = Float(Bits(b.f[i]) + rng() % 8);
        if (rng() % 2 == 0) TwoNaNs(b.f, 6, rng);
    };
    const auto call = [](Fn fn, Buf &b) { return DoubleBits(fn(&b.f[0], &b.f[3])); };
    Run<Fn>(ctx, "Dot87", 0x18fbf0, rng, setup, call);
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    Run<Fn>(ctx, "Dot87 at 24-bit", 0x18fbf0, rng, setup, call);
}

DIFF_TEST(CompGeomRigid_Boxes, 0x18ff90)
{
    std::mt19937 rng(0x18ff);
    using InFrameFn = BoundBox *(__thiscall *)(const Cuboid *, BoundBox *, const AffineTransform3 *);
    using InRigidFn = BoundBox *(__thiscall *)(const Cuboid *, BoundBox *, const RigidTransform3 *);
    using TransformFn = BoundBox *(__thiscall *)(BoundBox *, const AffineTransform3 *);
    using CornerFn = Vec3 *(__thiscall *)(const BoundBox *, Vec3 *, uint32_t);
    // A box that is sometimes sane (min <= max), sometimes any floats.
    const auto fillBox = [](float *box, std::mt19937 &rng) {
        for (unsigned i = 0; i < 6; ++i) box[i] = Num(rng);
        if (rng() % 2) {
            for (unsigned i = 0; i < 3; ++i)
                if (box[i] > box[i + 3]) std::swap(box[i], box[i + 3]);
        }
    };
    // The three users of Dot87 run at the harness's 53-bit and again at the game's 24-bit.
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
        Run<InFrameFn>(ctx, single ? "CuboidBoxInFrame at 24-bit" : "CuboidBoxInFrame", 0x18ff90, rng,
                       [](Buf &b, std::mt19937 &rng) {
                           Fill(b, rng);
                           for (unsigned i = 0; i < 3; ++i) b.f[i] = rng() % 4 ? std::fabs(Num(rng)) : Num(rng);
                           RandomFrame(*reinterpret_cast<AffineTransform3 *>(&b.f[16]), rng);
                           if (rng() % 3 == 0) TwoNaNs(&b.f[16], 29, rng);
                       },
                       [](InFrameFn fn, Buf &b) {
                           return Is(fn(reinterpret_cast<Cuboid *>(&b.f[0]), reinterpret_cast<BoundBox *>(&b.f[4]),
                                        reinterpret_cast<AffineTransform3 *>(&b.f[16])),
                                     &b.f[4]);
                       });
        Run<InRigidFn>(ctx, single ? "CuboidBoxInRigid at 24-bit" : "CuboidBoxInRigid", 0x190170, rng,
                       [](Buf &b, std::mt19937 &rng) {
                           Fill(b, rng);
                           for (unsigned i = 0; i < 3; ++i) b.f[i] = rng() % 4 ? std::fabs(Num(rng)) : Num(rng);
                           for (unsigned i = 16; i < 19; ++i) b.f[i] = Num(rng);
                           RandomQuaternion(&b.f[19], rng);
                           if (rng() % 3 == 0) TwoNaNs(b.f, 24, rng);
                       },
                       [](InRigidFn fn, Buf &b) {
                           return Is(fn(reinterpret_cast<Cuboid *>(&b.f[0]), reinterpret_cast<BoundBox *>(&b.f[4]),
                                        reinterpret_cast<RigidTransform3 *>(&b.f[16])),
                                     &b.f[4]);
                       });
        Run<TransformFn>(ctx, single ? "BoundBoxTransform at 24-bit" : "BoundBoxTransform", 0x19d730, rng,
                         [&fillBox](Buf &b, std::mt19937 &rng) {
                             Fill(b, rng);
                             fillBox(b.f, rng);
                             RandomFrame(*reinterpret_cast<AffineTransform3 *>(&b.f[16]), rng);
                             if (rng() % 3 == 0) TwoNaNs(b.f, 29, rng);
                         },
                         [](TransformFn fn, Buf &b) {
                             return Is(fn(reinterpret_cast<BoundBox *>(&b.f[0]),
                                          reinterpret_cast<AffineTransform3 *>(&b.f[16])),
                                       &b.f[0]);
                         });
    }
    Run<CornerFn>(ctx, "BoundBoxCorner", 0x19d390, rng,
                  [&fillBox](Buf &b, std::mt19937 &rng) {
                      Fill(b, rng);
                      fillBox(b.f, rng);
                      b.f[16] = Float(rng() % 16); // the index (any low bits)
                  },
                  [](CornerFn fn, Buf &b) {
                      return Is(fn(reinterpret_cast<BoundBox *>(&b.f[0]), reinterpret_cast<Vec3 *>(&b.f[8]),
                                   Bits(b.f[16])),
                                &b.f[8]);
                  });
}

DIFF_TEST(CompGeomRigid_LuInverse, 0x1a62e0)
{
    std::mt19937 rng(0x1a62);
    // Matrices from small integers (exact zeros, pivot ties, singular ones), decimals, and noise.
    const auto matrix = [](float *m, std::mt19937 &rng) {
        const unsigned kind = rng() % 4;
        for (unsigned i = 0; i < 9; ++i) {
            if (kind == 0)
                m[i] = float(int(rng() % 5) - 2);
            else if (kind == 1)
                m[i] = float(int(rng() % 2001) - 1000) / 100.0f;
            else
                m[i] = Num(rng);
        }
        if (rng() % 6 == 0) { // a repeated row: singular
            const unsigned a = rng() % 3, c = rng() % 3;
            for (unsigned j = 0; j < 3; ++j) m[3 * a + j] = m[3 * c + j];
        }
        if (rng() % 8 == 0) { // a zero column
            const unsigned c = rng() % 3;
            for (unsigned j = 0; j < 3; ++j) m[3 * j + c] = 0;
        }
    };
    using InvFn = float *(__cdecl *)(float *, const float *);
    Run<InvFn>(ctx, "InverseMatrix3", 0x1a62e0, rng,
               [&matrix](Buf &b, std::mt19937 &rng) {
                   Fill(b, rng);
                   matrix(b.f, rng);
                   if (rng() % 3 == 0) TwoNaNs(b.f, 9, rng);
               },
               [](InvFn fn, Buf &b) { return Is(fn(&b.f[16], &b.f[0]), &b.f[16]); });
    using DecFn = LuDecomposition3 *(__thiscall *)(LuDecomposition3 *, const float *);
    Run<DecFn>(ctx, "LuDecompose", 0x1a7aa0, rng,
               [&matrix](Buf &b, std::mt19937 &rng) {
                   Fill(b, rng);
                   matrix(b.f, rng);
                   if (rng() % 3 == 0) TwoNaNs(b.f, 9, rng);
               },
               [](DecFn fn, Buf &b) {
                   return Is(fn(reinterpret_cast<LuDecomposition3 *>(&b.f[16]), &b.f[0]), &b.f[16]);
               });
    using SolveFn = float *(__thiscall *)(const LuDecomposition3 *, float *, const float *);
    Run<SolveFn>(ctx, "LuSolve", 0x1a7e50, rng,
                 [&matrix](Buf &b, std::mt19937 &rng) {
                     Fill(b, rng);
                     float m[9];
                     matrix(m, rng);
                     reinterpret_cast<LuDecomposition3 *>(&b.f[16])->Decompose(m);
                     for (unsigned i = 0; i < 3; ++i) b.f[i] = Num(rng);
                     if (rng() % 3 == 0) TwoNaNs(&b.f[0], 3, rng);
                     if (rng() % 4 == 0) { // NaNs inside the factorisation too
                         float *lu = &b.f[16];
                         const unsigned a = rng() % 9;
                         lu[a] = Float(0x7fc00000u | (1 + rng() % 0x3ffffe) | (rng() & 0x80000000u));
                     }
                 },
                 [](SolveFn fn, Buf &b) {
                     return Is(fn(reinterpret_cast<LuDecomposition3 *>(&b.f[16]), &b.f[8], &b.f[0]), &b.f[8]);
                 });
}

// Lane VS: RigidTransform3::TransformPlane 0x19ca50 (a plane at +8 carried through the rigid transform at +0) and
// BoundBox::Extend 0x19d4a0 (the box at +0 grown by a point at +8, or, in an eighth of the cases, by a point inside the
// box object itself: the max side re-reads the point after the min side is stored).
DIFF_TEST(CompGeomRigid_PlaneAndExtend, 0x19ca50)
{
    std::mt19937 rng(0x19ca);
    using PlaneFn = void(__thiscall *)(const RigidTransform3 *, float *);
    Run<PlaneFn>(ctx, "RigidTransformPlane", 0x19ca50, rng,
                 [](Buf &b, std::mt19937 &rng) {
                     Fill(b, rng);
                     for (unsigned i = 0; i < 3; ++i) b.f[i] = Num(rng);
                     RandomQuaternion(&b.f[3], rng);
                     for (unsigned i = 8; i < 12; ++i) b.f[i] = Num(rng);
                     if (rng() % 3 == 0) TwoNaNs(b.f, 12, rng);
                     if (rng() % 4 == 0) {
                         // A huge normal (the rotation may overflow one component to inf - inf alone) against a
                         // NaN translation: which NaN the products keep shows each multiply's operand order.
                         for (unsigned i = 8; i < 11; ++i)
                             b.f[i] = (rng() % 2 ? 1.0f : -1.0f) * (1.0e38f + float(rng() % 2400) * 1.0e35f);
                         b.f[rng() % 3] = Float(0x7fc00000u | (1 + rng() % 0x3ffffe));
                     }
                 },
                 [](PlaneFn fn, Buf &b) {
                     fn(reinterpret_cast<RigidTransform3 *>(b.f), &b.f[8]);
                     return uint64_t(1);
                 });
    using ExtendFn = void(__thiscall *)(BoundBox *, const Vec3 *);
    Run<ExtendFn>(ctx, "BoundBoxExtend", 0x19d4a0, rng,
                  [](Buf &b, std::mt19937 &rng) {
                      Fill(b, rng);
                      gNaNBudget = int(rng() % 4); // up to three NaNs among the box and the point
                      for (unsigned i = 0; i < 6; ++i) b.f[i] = Num(rng);
                      for (unsigned i = 8; i < 11; ++i) b.f[i] = Num(rng);
                      if (rng() % 4 == 0) // a degenerate box (min = max) or one the point already lies in
                          for (unsigned i = 0; i < 3; ++i) b.f[3 + i] = b.f[i];
                      b.f[47] = rng() % 8 == 0 ? float(rng() % 4) : 8.0f; // where the point is (8: its own place)
                  },
                  [](ExtendFn fn, Buf &b) {
                      const unsigned at = unsigned(b.f[47]);
                      fn(reinterpret_cast<BoundBox *>(b.f), reinterpret_cast<const Vec3 *>(&b.f[at]));
                      return uint64_t(1);
                  });
}

// Lane VS: RigidNearlyEqual 0xae79e0 (the transforms at +0 and +8). Most cases put the second transform near the first:
// the translation moved by about 0.01 (the distance test's edge) and the rotation nudged by about 0.01, or negated (the
// dot product's edge near +-1); the rest are random, with NaNs and the edge values.
DIFF_TEST(CompGeomRigid_NearlyEqual, 0xae79e0)
{
    std::mt19937 rng(0xae79);
    using EqualFn = bool(__cdecl *)(const RigidTransform3 *, const RigidTransform3 *);
    Run<EqualFn>(ctx, "RigidNearlyEqual", 0xae79e0, rng,
                 [](Buf &b, std::mt19937 &rng) {
                     Fill(b, rng);
                     for (unsigned i = 0; i < 3; ++i) b.f[i] = Num(rng);
                     RandomQuaternion(&b.f[3], rng);
                     for (unsigned i = 8; i < 15; ++i) b.f[i] = Num(rng);
                     if (rng() % 4 != 0) {
                         // A unit quaternion first, then the second transform near it.
                         float q[4], norm = 0;
                         for (float &c : q) c = float(int(rng() % 2001) - 1000) / 1000.0f;
                         for (float c : q) norm += c * c;
                         norm = norm > 0 ? std::sqrt(norm) : 1.0f;
                         for (unsigned i = 0; i < 4; ++i) b.f[3 + i] = q[i] / norm;
                         const float reach = 0.004f + float(rng() % 1000) * 0.000008f; // 0.004 to 0.012
                         for (unsigned i = 0; i < 3; ++i)
                             b.f[8 + i] = b.f[i] + reach * (float(int(rng() % 2001) - 1000) / 1000.0f);
                         const float sign = rng() % 2 ? 1.0f : -1.0f;
                         const float nudge = float(rng() % 30) * 0.001f;
                         for (unsigned i = 0; i < 4; ++i)
                             b.f[11 + i] = sign * (b.f[3 + i] + nudge * (float(int(rng() % 2001) - 1000) / 1000.0f));
                         if (rng() % 3 == 0) // the same translation: only the rotation decides
                             for (unsigned i = 0; i < 3; ++i) b.f[8 + i] = b.f[i];
                         if (rng() % 3 == 0) {
                             // The dot product's edge itself: the second rotation turned so that its dot with the first
                             // lands within a few float steps of 1 - 1e-4, where the summation's rounding decides.
                             float p[4], along = 0, pn = 0;
                             for (float &c : p) c = float(int(rng() % 2001) - 1000) / 1000.0f;
                             for (unsigned i = 0; i < 4; ++i) along += p[i] * b.f[3 + i];
                             for (unsigned i = 0; i < 4; ++i) p[i] -= along * b.f[3 + i];
                             for (float c : p) pn += c * c;
                             pn = pn > 0 ? std::sqrt(pn) : 1.0f;
                             const double c = 1.0 - 1e-4 + double(int(rng() % 17) - 8) * 6e-8;
                             const double s = std::sqrt(1.0 - c * c);
                             for (unsigned i = 0; i < 4; ++i)
                                 b.f[11 + i] = sign * float(c * b.f[3 + i] + s * p[i] / pn);
                         }
                     }
                     if (rng() % 8 == 0) b.f[rng() % 15 < 7 ? rng() % 7 : 8 + rng() % 7] = Float(0x7fc00000u);
                 },
                 [](EqualFn fn, Buf &b) {
                     return uint64_t(fn(reinterpret_cast<RigidTransform3 *>(b.f),
                                        reinterpret_cast<RigidTransform3 *>(&b.f[8])));
                 });
}
