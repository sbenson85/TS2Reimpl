// Differential test for the compgeom helpers the scenegraph code used to call in the game (scenegraph lane TS; RVAs
// 1.18.0.187): the rigid transform inverse 0x19cd80, the box containment test 0x19d2f0, the Euler angles to a 3x3 matrix
// 0x1a5660 and the translated cuboid shape's constructor 0x18fc10 and destructor 0x18fce0. Each replacement runs
// against the original at its RVA on random inputs; outputs, returns and untouched guard words are compared bit for bit.
// Floats mix small integers, decimals, wide exponents, denormals, zeros (both signs), infinities and up to two NaNs of
// different payloads per case (two NaNs meeting in SSE keep the first operand's payload, so this checks the operand
// order of every product and sum). Boxes also come touching, nested, degenerate (min == max) and inverted.

#include "compgeom/ConvexShapes.h"
#include "compgeom/Matrix4.h"
#include "compgeom/Rigid.h"
#include "difftest.h"
#include "hook/hooks.h"
#include "shape.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
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

struct alignas(16) Buf {
    float f[48];
};
constexpr unsigned kWords = 48;

void Fill(Buf &b, std::mt19937 &rng)
{
    std::memset(&b, 0x5c, sizeof(b));
    gNaNBudget = int(rng() % 4) - 1; // none, one or two NaNs
}

const HookSpec *HookAt(difftest::Context &ctx, uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    ctx.Fail("no hook at 0x%x", rva);
    return nullptr;
}

void ReportDiff(difftest::Context &ctx, const char *name, unsigned i, const Buf &a, const Buf &b, uint64_t ra, uint64_t rb)
{
    unsigned first = 0;
    while (first < kWords && Bits(a.f[first]) == Bits(b.f[first])) ++first;
    ctx.Fail("%s case %u: differs at float %u (%08x / %08x), returns %llx / %llx", name, i, first,
             first < kWords ? Bits(a.f[first]) : 0, first < kWords ? Bits(b.f[first]) : 0,
             static_cast<unsigned long long>(ra), static_cast<unsigned long long>(rb));
}

// Runs fn[0] (the original) and fn[1] (ours) over the same cases; `setup` fills a case, `call` runs one side.
template <class Fn, class Setup, class Call>
void Run(difftest::Context &ctx, const char *name, std::mt19937 &rng, const Fn (&fn)[2], Setup setup, Call call,
         unsigned cases = 20000)
{
    unsigned failures = 0;
    for (unsigned i = 0; i < cases && failures < 5; ++i) {
        Buf b[2];
        setup(b[0], rng);
        b[1] = b[0];
        uint64_t r[2];
        for (unsigned s = 0; s < 2; ++s) r[s] = call(fn[s], b[s]);
        ++ctx.cases;
        if (std::memcmp(&b[0], &b[1], sizeof(Buf)) != 0 || r[0] != r[1]) {
            ReportDiff(ctx, name, i, b[0], b[1], r[0], r[1]);
            ++failures;
        }
    }
}

void RandomQuaternion(float *q, std::mt19937 &rng)
{
    for (unsigned i = 0; i < 4; ++i) q[i] = Num(rng);
    switch (rng() % 4) {
    case 0: { // a unit-ish quaternion
        const float s = std::sin(Num(rng) * 0.1f), c = std::cos(Num(rng) * 0.1f);
        q[0] = s * 0.6f;
        q[1] = s * 0.8f;
        q[2] = 0;
        q[3] = c;
        break;
    }
    case 1: // the identity
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        break;
    case 2: // a quarter turn about a random axis, or a half turn
        q[0] = q[1] = q[2] = 0.0f;
        q[rng() % 3] = rng() % 2 ? 0.70710678f : 1.0f;
        q[3] = q[0] + q[1] + q[2] > 0.9f ? 0.0f : 0.70710678f;
        break;
    default: break;
    }
}

} // namespace

// Buffer layout: the rigid transform at f[0..6] (translation, quaternion), guard floats after.
DIFF_TEST(CompGeomTS_RigidInvert, 0x19cd80)
{
    std::mt19937 rng(0x19cd);
    const HookSpec *h = HookAt(ctx, 0x19cd80);
    if (!h) return;
    using Fn = RigidTransform3 *(__thiscall *)(RigidTransform3 *);
    const Fn fn[2] = {ctx.Original<Fn>(0x19cd80), reinterpret_cast<Fn>(h->replacement)};
    Run(ctx, "RigidInvert", rng, fn,
        [](Buf &b, std::mt19937 &rng) {
            Fill(b, rng);
            for (unsigned i = 0; i < 3; ++i) b.f[i] = Num(rng);
            RandomQuaternion(&b.f[3], rng);
            if (rng() % 3 == 0) TwoNaNs(b.f, 7, rng);
        },
        [](Fn f, Buf &b) { return uint64_t(f(reinterpret_cast<RigidTransform3 *>(&b.f[0])) == reinterpret_cast<RigidTransform3 *>(&b.f[0])); });
}

// The box containment test is called only by ours (no hook): our member against the original body.
DIFF_TEST(CompGeomTS_BoxContains, 0x19d2f0)
{
    std::mt19937 rng(0x19d2);
    using Fn = uint32_t(__thiscall *)(const BoundBox *, const BoundBox *);
    const auto ours = [](const BoundBox *a, const BoundBox *b) -> uint32_t { return a->Contains(*b) ? 1u : 0u; };
    const Fn orig = ctx.Original<Fn>(0x19d2f0);
    unsigned failures = 0;
    for (unsigned i = 0; i < 40000 && failures < 5; ++i) {
        Buf b;
        Fill(b, rng);
        // Two boxes of random kinds: independent numbers, or the second built from the first by small moves in single
        // coordinates (touching, nested, equal, degenerate), so every comparison sits at its boundary often.
        for (unsigned k = 0; k < 12; ++k) b.f[k] = Num(rng);
        switch (rng() % 4) {
        case 0: break;
        case 1: // inner equals outer, then one coordinate nudged either way
            for (unsigned k = 0; k < 6; ++k) b.f[6 + k] = b.f[k];
            b.f[6 + rng() % 6] += (rng() % 2 ? 1.0f : -1.0f) * (rng() % 3 ? 1.0f : 1e-7f);
            break;
        case 2: // a nested pair of ordered boxes, one coordinate possibly broken
            for (unsigned a = 0; a < 3; ++a) {
                b.f[a] = -float(rng() % 5) - 1.0f;
                b.f[3 + a] = float(rng() % 5) + 1.0f;
                b.f[6 + a] = b.f[a] + float(rng() % 6);
                b.f[9 + a] = b.f[3 + a] - float(rng() % 6);
            }
            if (rng() % 2) b.f[rng() % 12] = Num(rng);
            break;
        default: // degenerate: min == max for both; signed zeros
            for (unsigned a = 0; a < 3; ++a) {
                const float v = (rng() % 3 == 0) ? (rng() % 2 ? 0.0f : -0.0f) : float(int(rng() % 3) - 1);
                b.f[a] = b.f[3 + a] = v;
                b.f[6 + a] = b.f[9 + a] = (rng() % 2) ? v : (rng() % 2 ? 0.0f : -0.0f);
            }
            break;
        }
        if (rng() % 4 == 0) TwoNaNs(b.f, 12, rng);
        Buf c = b;
        const uint32_t want = orig(reinterpret_cast<const BoundBox *>(&b.f[0]), reinterpret_cast<const BoundBox *>(&b.f[6])) & 0xffu;
        const uint32_t got = ours(reinterpret_cast<const BoundBox *>(&c.f[0]), reinterpret_cast<const BoundBox *>(&c.f[6]));
        ++ctx.cases;
        if (want != got || std::memcmp(&b, &c, sizeof(Buf)) != 0) {
            ctx.Fail("BoxContains case %u: original %u, ours %u", i, want, got);
            ++failures;
        }
    }
}

DIFF_TEST(CompGeomTS_EulerMatrix, 0x1a5660)
{
    std::mt19937 rng(0x1a56);
    const HookSpec *h = HookAt(ctx, 0x1a5660);
    if (!h) return;
    using Fn = float *(__cdecl *)(float *, float, float, float);
    const Fn fn[2] = {ctx.Original<Fn>(0x1a5660), reinterpret_cast<Fn>(h->replacement)};
    // Angles: the radians cases real callers use (multiples of pi/4), small and large decimals, and every edge value
    // (zeros, infinities, NaNs, denormals), alone or with NaNs of different payloads in two angles.
    const auto angle = [](std::mt19937 &rng) -> float {
        switch (rng() % 6) {
        case 0: return float(int(rng() % 17) - 8) * 0.7853982f;
        case 1: return float(int(rng() % 2001) - 1000) / 100.0f;
        case 2: return float(int(rng() % 20001) - 10000);
        default: return Num(rng);
        }
    };
    Run(ctx, "EulerMatrix", rng, fn,
        [&](Buf &b, std::mt19937 &rng) {
            Fill(b, rng);
            for (unsigned i = 0; i < 3; ++i) b.f[i] = angle(rng);
            // f[0..2] angles; the output at f[8..16], pre-filled with a pattern the zeroing stores must clear.
            for (unsigned i = 8; i < 17; ++i) b.f[i] = Float(0x11111111u * (i - 7));
            if (rng() % 4 == 0) TwoNaNs(b.f, 3, rng);
        },
        [](Fn f, Buf &b) { return uint64_t(f(&b.f[8], b.f[0], b.f[1], b.f[2]) == &b.f[8]); });
}

// The translated cuboid shape on the stack: constructor 0x18fc10 (extents, centre) and destructor 0x18fce0.
DIFF_TEST(CompGeomTS_TranslatedCuboidShape, 0x18fc10)
{
    std::mt19937 rng(0x18fc);
    using Ctor = void *(__thiscall *)(void *, const Vec3 *, const Vec3 *);
    using Dtor = void(__thiscall *)(void *);
    const Ctor ctor = ctx.Original<Ctor>(0x18fc10);
    const Dtor dtor = ctx.Original<Dtor>(0x18fce0);
    // Our vtables: the shape's and the bound's (what its destructor leaves).
    alignas(16) static uint8_t probe[0x20];
    auto *p = ::new (probe) cTranslatedCuboidShape;
    void *const ourShapeVt = *reinterpret_cast<void **>(p);
    p->~cTranslatedCuboidShape();
    auto *q = ::new (probe) cTranslatedCuboidBound;
    void *const ourBoundVt = *reinterpret_cast<void **>(q);
    q->~cTranslatedCuboidBound();
    void *const gameShapeVt = shape::GameVTable(0xda1640);
    void *const gameBoundVt = shape::GameVTable(0xda1584);
    unsigned failures = 0;
    for (unsigned i = 0; i < 20000 && failures < 5; ++i) {
        gNaNBudget = int(rng() % 4) - 1;
        Vec3 extents = {Num(rng), Num(rng), Num(rng)};
        Vec3 centre = {Num(rng), Num(rng), Num(rng)};
        if (rng() % 3 == 0) {
            float all[6] = {extents.x, extents.y, extents.z, centre.x, centre.y, centre.z};
            TwoNaNs(all, 6, rng);
            std::memcpy(&extents, all, sizeof extents);
            std::memcpy(&centre, all + 3, sizeof centre);
        }
        alignas(16) uint8_t a[0x20], b[0x20];
        std::memset(a, 0x5c, sizeof a);
        std::memset(b, 0x5c, sizeof b);
        void *const ra = ctor(a, &extents, &centre);
        auto *ours = ::new (b) cTranslatedCuboidShape(&extents, &centre);
        ++ctx.cases;
        uint32_t wa[8], wb[8];
        std::memcpy(wa, a, 0x20);
        std::memcpy(wb, b, 0x20);
        bool bad = ra != a;
        for (unsigned k = 1; k < 8; ++k) bad |= wa[k] != wb[k];
        bad |= std::memcmp(&wa[0], &gameShapeVt, 4) != 0 || std::memcmp(&wb[0], &ourShapeVt, 4) != 0;
        // Destructors: both leave the base vtable and touch nothing else.
        dtor(a);
        ours->~cTranslatedCuboidShape();
        std::memcpy(wa, a, 0x20);
        std::memcpy(wb, b, 0x20);
        for (unsigned k = 1; k < 8; ++k) bad |= wa[k] != wb[k];
        // The original's destructor leaves the bound's vtable. Ours leaves whichever base the compiler's last store names: a
        // destroyed object's vtable word is not observable, so only the data words are compared on our side.
        bad |= std::memcmp(&wa[0], &gameBoundVt, 4) != 0;
        if (bad) {
            ctx.Fail("TranslatedCuboidShape case %u: differs (vtables after the destructors %08x / %08x, wanted %08x / %08x; shape %08x / %08x; ret %d)", i,
                     wa[0], wb[0], uint32_t(uintptr_t(gameBoundVt)), uint32_t(uintptr_t(ourBoundVt)), uint32_t(uintptr_t(gameShapeVt)), uint32_t(uintptr_t(ourShapeVt)), int(ra == a));
            ++failures;
        }
    }
}
