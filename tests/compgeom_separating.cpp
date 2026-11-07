// Differential test for the separating-axis tests of triangle sets (lane PL, src/compgeom/SeparatingAxes.h; RVAs
// 1.18.0.187): each hook table replacement against the original at its RVA.
// - CompGeomSeparating_Frames: the 3x3 product 0x1a5d20 / 0x1a6650, the frame members 0x19cca0 (inverse in place) and
//   0x19be20 (composition), the context builder 0x19ffd0 (the whole 0xb8-byte context compared bit for bit) and the box
//   span forwarder 0x19f800, on random frames and matrices with up to two NaNs of different payloads per case (two NaNs
//   meeting pick a payload by operand order, so this checks every product and sum keeps the original's order).
// - CompGeomSeparating_TriangleBox: the triangle against a box 0x1a3410 on triangles placed around a box (inside, across,
//   touching, apart, lattice and junk), at most one NaN per case.
// - CompGeomSeparating_SetCuboid: trees built over random triangle sets, the tree walk 0x19f930 from random nodes with a
//   context built by the original, and the whole test 0x19f8e0, with random frames for both shapes.
//
// Float inputs follow docs/subsystems/scenegraph.md ("Float inputs and NaNs"): edge values and junk freely, but in at
// most one slot of a case a NaN (where a result is a bool) and then no infinities or values of 1e6 and above in the case.
// Each function must return both true and false in at least 2% of its cases.
// Each test runs at the harness's 53-bit x87 precision and again at the game's 24-bit.

#include "compgeom/SeparatingAxes.h"
#include "difftest.h"
#include "hook/hooks.h"

#include "shape.h"

#include <cfloat>
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
    0xff7fffff, 0x7f800000, 0xff800000, 0x3f000000, 0x40400000, 0x34000000, 0x33ffffff, 0x34000001,
};

// A number without NaNs.
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
    return std::isnan(f) ? 0.75f : f;
}
float Unit(std::mt19937 &rng) { return float(int(rng() % 20001) - 10000) / 10000.0f; } // -1 .. 1

float NaNValue(std::mt19937 &rng)
{
    return Float((rng() & 0x80000000u) | 0x7f800000u | (rng() % 3 ? 0x00400000u : 0) | (1 + rng() % 0x3ffffe));
}

// At most one NaN into the slots: in a third of the cases one of them; then every slot of 1e6 and above or infinite is
// brought back to a small value, so that no second NaN can arise.
void InjectNaN(const std::vector<float *> &slots, std::mt19937 &rng)
{
    if (slots.empty() || rng() % 3 != 0) return;
    *slots[rng() % slots.size()] = NaNValue(rng);
    for (float *s : slots)
        if (!std::isnan(*s) && !(std::fabs(*s) < 1.0e6f)) *s = 0.75f;
}

// Two NaNs with different payloads (and signs, sometimes signalling) at two random places among the slots.
void TwoNaNs(const std::vector<float *> &slots, std::mt19937 &rng)
{
    const unsigned n = unsigned(slots.size());
    const unsigned a = rng() % n;
    unsigned b = rng() % (n - 1);
    if (b >= a) ++b;
    for (const unsigned p : {a, b}) *slots[p] = NaNValue(rng);
}

const HookSpec *HookAt(difftest::Context &ctx, uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    ctx.Fail("no hook at 0x%x", rva);
    return nullptr;
}

constexpr unsigned kCases = 20000;

void CheckRate(difftest::Context &ctx, const char *name, unsigned trues, unsigned cases)
{
    // A test that never reaches both outcomes would pass for any function that returns one of them.
    if (trues < cases / 50 || trues > cases - cases / 50)
        ctx.Fail("%s: %u of %u cases returned true (the generator misses a branch)", name, trues, cases);
}

// ---- frames ----

// A frame of junk words (flags as any of the combinations the inverse tests, matrices of noise or rotation-like).
void JunkFrame(AffineTransform3 &f, std::mt19937 &rng)
{
    f.mUnknown0 = rng() % 8 ? (rng() & 0x1f) : rng();
    for (float &m : f.mMatrix) m = rng() % 3 ? Num(rng) : float(rng() % 2);
    if (rng() % 3 == 0) {
        const float c = Num(rng), s = Num(rng), k = rng() % 2 ? 1.0f : Num(rng);
        const float rot[9] = {c * k, -s * k, 0, s * k, c * k, 0, 0, 0, k};
        std::memcpy(f.mMatrix, rot, sizeof(rot));
    }
    for (float &o : f.mOffset) o = Num(rng);
}

// A frame that places things sensibly: the identity, a rotation, a uniform scale or a general matrix, with an offset.
void SensibleFrame(AffineTransform3 &f, std::mt19937 &rng, bool lattice)
{
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(f.mMatrix, identity, sizeof(identity));
    f.mUnknown0 = 0;
    for (float &o : f.mOffset) o = lattice ? float(int(rng() % 5) - 2) : Unit(rng) * 2.0f;
    const unsigned variant = lattice ? 0 : rng() % 8;
    if (variant == 1 || variant == 2) {
        f.mUnknown0 = 2;
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
        const float a = Unit(rng) * 3.14159f, sn = std::sin(a * 0.5f), w = std::cos(a * 0.5f);
        x *= sn;
        y *= sn;
        z *= sn;
        const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                            2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                            2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
        std::memcpy(f.mMatrix, r, sizeof(r));
    } else if (variant == 3) {
        f.mUnknown0 = 6;
        const float k = 0.5f + float(rng() % 3000) / 1000.0f;
        for (unsigned i = 0; i < 9; ++i) f.mMatrix[i] = i % 4 == 0 ? k : 0.0f;
    } else if (variant == 4) {
        f.mUnknown0 = 0x10;
        for (unsigned i = 0; i < 9; ++i) f.mMatrix[i] = (i % 4 == 0 ? 1.0f : 0.0f) + Unit(rng) * 0.4f;
    }
}

void FrameSlots(AffineTransform3 &f, std::vector<float *> &slots)
{
    for (float &m : f.mMatrix) slots.push_back(&m);
    for (float &o : f.mOffset) slots.push_back(&o);
}

struct alignas(16) FrameCase {
    AffineTransform3 a;
    AffineTransform3 b;
    float matrixOut[9];
    float guard[7];
};

template <class T> bool SameBytes(const T &x, const T &y) { return std::memcmp(&x, &y, sizeof(T)) == 0; }

} // namespace

DIFF_TEST(CompGeomSeparating_Frames, 0x19cca0)
{
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
        std::mt19937 rng(0x19cc);
        unsigned failures = 0;

        // 0x1a5d20 (out, a, b) and 0x1a6650 (this = a, b).
        {
            using Product = float *(__cdecl *)(float *, const float *, const float *);
            using MultiplyBy = void *(__thiscall *)(void *, const void *);
            const HookSpec *h1 = HookAt(ctx, 0x1a5d20), *h2 = HookAt(ctx, 0x1a6650);
            if (!h1 || !h2) return;
            const Product p[2] = {ctx.Original<Product>(0x1a5d20), reinterpret_cast<Product>(h1->replacement)};
            const MultiplyBy mb[2] = {ctx.Original<MultiplyBy>(0x1a6650), reinterpret_cast<MultiplyBy>(h2->replacement)};
            for (unsigned i = 0; i < kCases && failures < 5; ++i) {
                FrameCase c[2];
                std::memset(&c[0], 0x5c, sizeof(FrameCase));
                for (unsigned k = 0; k < 9; ++k) {
                    c[0].a.mMatrix[k] = Num(rng);
                    c[0].b.mMatrix[k] = Num(rng);
                }
                std::vector<float *> slots;
                for (unsigned k = 0; k < 9; ++k) {
                    slots.push_back(&c[0].a.mMatrix[k]);
                    slots.push_back(&c[0].b.mMatrix[k]);
                }
                if (rng() % 3 == 0) TwoNaNs(slots, rng);
                c[1] = c[0];
                float *r[2];
                for (unsigned s = 0; s < 2; ++s) r[s] = p[s](c[s].matrixOut, c[s].a.mMatrix, c[s].b.mMatrix);
                ++ctx.cases;
                if (!SameBytes(c[0], c[1]) || r[0] != c[0].matrixOut || r[1] != c[1].matrixOut) {
                    ctx.Fail("MatrixProduct case %u differs", i);
                    ++failures;
                }
                // And in place: a = a * b.
                c[1] = c[0];
                void *q[2];
                for (unsigned s = 0; s < 2; ++s) q[s] = mb[s](c[s].a.mMatrix, c[s].b.mMatrix);
                if (!SameBytes(c[0], c[1]) || q[0] != c[0].a.mMatrix || q[1] != c[1].a.mMatrix) {
                    ctx.Fail("Matrix3x3::MultiplyBy case %u differs", i);
                    ++failures;
                }
            }
        }

        // 0x19cca0 (this) and 0x19be20 (this, other).
        {
            using Invert = void *(__thiscall *)(void *);
            using Compose = void *(__thiscall *)(void *, const void *);
            const HookSpec *h1 = HookAt(ctx, 0x19cca0), *h2 = HookAt(ctx, 0x19be20);
            if (!h1 || !h2) return;
            const Invert inv[2] = {ctx.Original<Invert>(0x19cca0), reinterpret_cast<Invert>(h1->replacement)};
            const Compose comp[2] = {ctx.Original<Compose>(0x19be20), reinterpret_cast<Compose>(h2->replacement)};
            for (unsigned i = 0; i < kCases && failures < 5; ++i) {
                FrameCase c[2];
                std::memset(&c[0], 0x5c, sizeof(FrameCase));
                if (rng() % 2) {
                    SensibleFrame(c[0].a, rng, false);
                    SensibleFrame(c[0].b, rng, false);
                } else {
                    JunkFrame(c[0].a, rng);
                    JunkFrame(c[0].b, rng);
                }
                std::vector<float *> slots;
                FrameSlots(c[0].a, slots);
                FrameSlots(c[0].b, slots);
                if (rng() % 3 == 0) TwoNaNs(slots, rng);
                c[1] = c[0];
                void *r[2];
                for (unsigned s = 0; s < 2; ++s) r[s] = inv[s](&c[s].a);
                ++ctx.cases;
                if (!SameBytes(c[0], c[1]) || r[0] != &c[0].a || r[1] != &c[1].a) {
                    ctx.Fail("Invert case %u (flags %08x) differs: offset %08x %08x %08x / %08x %08x %08x", i,
                             c[0].a.mUnknown0, Bits(c[0].a.mOffset[0]), Bits(c[0].a.mOffset[1]), Bits(c[0].a.mOffset[2]),
                             Bits(c[1].a.mOffset[0]), Bits(c[1].a.mOffset[1]), Bits(c[1].a.mOffset[2]));
                    ++failures;
                }
                for (unsigned s = 0; s < 2; ++s) r[s] = comp[s](&c[s].a, &c[s].b);
                if (!SameBytes(c[0], c[1]) || r[0] != &c[0].a || r[1] != &c[1].a) {
                    ctx.Fail("Compose case %u (flags %08x %08x) differs", i, c[0].a.mUnknown0, c[0].b.mUnknown0);
                    ++failures;
                }
            }
        }

        // 0x19ffd0: the context (ecx), then (first, firstFrame, second, secondFrame).
        {
            using Build = void(__thiscall *)(void *, const void *, const void *, const void *, const void *);
            const HookSpec *h = HookAt(ctx, 0x19ffd0);
            if (!h) return;
            const Build fn[2] = {ctx.Original<Build>(0x19ffd0), reinterpret_cast<Build>(h->replacement)};
            for (unsigned i = 0; i < kCases && failures < 5; ++i) {
                FrameCase in;
                std::memset(&in, 0x5c, sizeof(in));
                if (rng() % 2) {
                    SensibleFrame(in.a, rng, rng() % 6 == 0);
                    SensibleFrame(in.b, rng, rng() % 6 == 0);
                } else {
                    JunkFrame(in.a, rng);
                    JunkFrame(in.b, rng);
                }
                std::vector<float *> slots;
                FrameSlots(in.a, slots);
                FrameSlots(in.b, slots);
                if (rng() % 3 == 0) TwoNaNs(slots, rng);
                alignas(16) uint8_t buffer[2][sizeof(SeparationContext) + 16];
                for (unsigned s = 0; s < 2; ++s) {
                    std::memset(buffer[s], 0x5c, sizeof(buffer[s]));
                    fn[s](buffer[s], &in, &in.a, &in.matrixOut, &in.b);
                }
                ++ctx.cases;
                if (std::memcmp(buffer[0], buffer[1], sizeof(buffer[0])) != 0) {
                    unsigned first = 0;
                    while (buffer[0][first] == buffer[1][first]) ++first;
                    ctx.Fail("Build case %u (flags %08x %08x): context differs at byte 0x%x", i, in.a.mUnknown0,
                             in.b.mUnknown0, first);
                    ++failures;
                }
            }
        }

        // 0x19f800: the tree node's box (half extents, centre) against a line.
        {
            using Span = int(__cdecl *)(float *, float *, const void *, const void *);
            const HookSpec *h = HookAt(ctx, 0x19f800);
            if (!h) return;
            const Span fn[2] = {ctx.Original<Span>(0x19f800), reinterpret_cast<Span>(h->replacement)};
            unsigned trues = 0, cases = 0;
            for (unsigned i = 0; i < kCases && failures < 5; ++i) {
                struct alignas(16) Case {
                    float node[8]; // half extents, centre, two words
                    float line[6];
                    float t[2];
                    float guard[6];
                } c[2];
                std::memset(&c[0], 0x5c, sizeof(Case));
                const bool junk = rng() % 6 == 0;
                for (unsigned k = 0; k < 3; ++k) {
                    c[0].node[k] = junk ? Num(rng) : 0.2f + float(rng() % 3000) / 1000.0f;
                    c[0].node[3 + k] = junk ? Num(rng) : Unit(rng) * 3.0f;
                    c[0].line[k] = junk ? Num(rng) : c[0].node[3 + k] + Unit(rng) * 6.0f;
                    c[0].line[3 + k] = junk ? Num(rng) : (c[0].node[3 + k] + Unit(rng) * 1.5f) - c[0].line[k];
                }
                std::vector<float *> slots;
                for (unsigned k = 0; k < 6; ++k) slots.push_back(&c[0].node[k]);
                for (unsigned k = 0; k < 6; ++k) slots.push_back(&c[0].line[k]);
                InjectNaN(slots, rng);
                c[1] = c[0];
                int r[2];
                for (unsigned s = 0; s < 2; ++s) r[s] = fn[s](&c[s].t[0], &c[s].t[1], c[s].node, c[s].line) != 0;
                ++cases;
                trues += r[0];
                ++ctx.cases;
                if (!SameBytes(c[0], c[1]) || r[0] != r[1]) {
                    ctx.Fail("TreeBoxLineSpan case %u differs (returns %d / %d)", i, r[0], r[1]);
                    ++failures;
                }
            }
            if (!failures) CheckRate(ctx, "TreeBoxLineSpan", trues, cases);
        }
    }
}

namespace {

constexpr unsigned kBoxCases = 60000;

struct alignas(16) BoxCase {
    float triangle[9];
    float box[3];
    float guard[4];
};

} // namespace

DIFF_TEST(CompGeomSeparating_TriangleBox, 0x1a3410)
{
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
        std::mt19937 rng(0x1a34);
        using Fn = bool(__cdecl *)(const float *, const float *);
        const HookSpec *h = HookAt(ctx, 0x1a3410);
        if (!h) return;
        const Fn fn[2] = {ctx.Original<Fn>(0x1a3410), reinterpret_cast<Fn>(h->replacement)};
        unsigned failures = 0, trues = 0;
        for (unsigned i = 0; i < kBoxCases && failures < 5; ++i) {
            BoxCase c[2];
            std::memset(&c[0], 0x5c, sizeof(BoxCase));
            const unsigned kind = rng() % 14;
            if (kind < 3) { // lattice: integers, so the sums land exactly on the boundaries
                for (float &v : c[0].triangle) v = float(int(rng() % 7) - 3);
                for (float &b : c[0].box) b = float(1 + rng() % 2);
            } else if (kind == 3) { // junk
                for (float &v : c[0].triangle) v = Num(rng);
                for (float &b : c[0].box) b = Num(rng);
            } else if (kind == 4) { // a corner exactly on the box
                for (float &b : c[0].box) b = 0.5f + float(rng() % 4) * 0.5f;
                for (unsigned k = 0; k < 9; ++k) c[0].triangle[k] = Unit(rng) * 2.5f;
                const unsigned corner = rng() % 3;
                for (unsigned k = 0; k < 3; ++k)
                    c[0].triangle[corner * 3 + k] = (rng() % 2 ? 1.0f : -1.0f) * c[0].box[k];
            } else if (kind >= 12) { // a large triangle whose diagonal plane passes close to a box corner
                for (float &b : c[0].box) b = 0.5f + float(rng() % 1500) / 1000.0f;
                float n[3];
                for (float &v : n) v = (rng() % 2 ? 1.0f : -1.0f) * (0.6f + Unit(rng) * 0.4f);
                const float reach = std::fabs(n[0]) * c[0].box[0] + std::fabs(n[1]) * c[0].box[1] + std::fabs(n[2]) * c[0].box[2];
                const float d = reach + Unit(rng) * 0.3f;
                const float len2 = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
                float base[3], u[3], w[3];
                for (unsigned k = 0; k < 3; ++k) base[k] = n[k] * d / len2;
                u[0] = n[1] - n[2]; u[1] = n[2] - n[0]; u[2] = n[0] - n[1];
                w[0] = n[1] * u[2] - n[2] * u[1]; w[1] = n[2] * u[0] - n[0] * u[2]; w[2] = n[0] * u[1] - n[1] * u[0];
                for (unsigned v = 0; v < 3; ++v) {
                    const float a = (v == 0 ? 6.0f : v == 1 ? -5.0f : 1.0f) * (1.0f + Unit(rng) * 0.2f);
                    const float b = (v == 0 ? -3.0f : v == 1 ? -4.0f : 7.0f) * (1.0f + Unit(rng) * 0.2f);
                    for (unsigned k = 0; k < 3; ++k) c[0].triangle[v * 3 + k] = base[k] + a * u[k] + b * w[k];
                }
            } else { // random around the box
                for (float &b : c[0].box) b = 0.2f + float(rng() % 2000) / 1000.0f;
                const float spread = rng() % 3 ? 2.5f : 0.7f;
                for (float &v : c[0].triangle) v = Unit(rng) * spread;
                if (rng() % 8 == 0) // a thin triangle
                    for (unsigned k = 0; k < 3; ++k) c[0].triangle[6 + k] = c[0].triangle[3 + k] + Unit(rng) * 0.01f;
            }
            std::vector<float *> slots;
            for (float &v : c[0].triangle) slots.push_back(&v);
            for (float &b : c[0].box) slots.push_back(&b);
            InjectNaN(slots, rng);
            c[1] = c[0];
            bool r[2];
            for (unsigned s = 0; s < 2; ++s) r[s] = fn[s](c[s].triangle, c[s].box);
            ++ctx.cases;
            trues += r[0];
            if (r[0] != r[1] || !SameBytes(c[0], c[1])) {
                ctx.Fail("TriangleMeetsBox case %u (kind %u): returns %d / %d", i, kind, r[0], r[1]);
                ++failures;
            }
        }
        if (!failures) CheckRate(ctx, "TriangleMeetsBox", trues, kBoxCases);
    }
}

namespace {

constexpr unsigned kMaxTriangles = 9, kMaxVertices = 16, kMaxNodes = kMaxTriangles;

struct alignas(16) SetScene {
    uint32_t block[10]; // the set: vertices at +0, indices at +0xc, the tree's vtable +0x18 and nodes +0x1c (begin, end, cap)
    Vec3 vertices[kMaxVertices];
    uint16_t indices[kMaxTriangles * 3];
    AABBTreeNode nodes[kMaxNodes];
    AffineTransform3 setFrame;
    AffineTransform3 cuboidFrame;
    float cuboid[3];
    unsigned triangles, vertexCount, nodeCount;
};

struct SetBuilder {
    SetScene &scene;
    std::mt19937 &rng;
    uint32_t Build(std::vector<unsigned> list)
    {
        if (list.size() == 1) return list[0] * 2 + 1;
        const unsigned index = scene.nodeCount++;
        for (size_t i = list.size(); i > 1; --i) std::swap(list[i - 1], list[rng() % i]);
        const size_t split = 1 + rng() % (list.size() - 1);
        float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (unsigned t : list)
            for (unsigned c = 0; c < 3; ++c) {
                const Vec3 &v = scene.vertices[scene.indices[t * 3 + c]];
                const float p[3] = {v.x, v.y, v.z};
                for (unsigned k = 0; k < 3; ++k) {
                    if (p[k] < lo[k]) lo[k] = p[k];
                    if (p[k] > hi[k]) hi[k] = p[k];
                }
            }
        AABBTreeNode &n = scene.nodes[index];
        n.box.half = {(hi[0] - lo[0]) * 0.5f, (hi[1] - lo[1]) * 0.5f, (hi[2] - lo[2]) * 0.5f};
        n.centre = {(hi[0] + lo[0]) * 0.5f, (hi[1] + lo[1]) * 0.5f, (hi[2] + lo[2]) * 0.5f};
        const uint32_t left = Build(std::vector<unsigned>(list.begin(), list.begin() + split));
        const uint32_t right = Build(std::vector<unsigned>(list.begin() + split, list.end()));
        scene.nodes[index].left = left;
        scene.nodes[index].right = right;
        return index * 2;
    }
};

void MakeSetScene(SetScene &s, std::mt19937 &rng)
{
    std::memset(&s, 0, sizeof(s));
    for (AABBTreeNode &n : s.nodes) n = AABBTreeNode();
    const bool lattice = rng() % 6 == 0;
    s.triangles = 2 + rng() % (kMaxTriangles - 1);
    s.vertexCount = 4 + rng() % (kMaxVertices - 3);
    for (unsigned v = 0; v < s.vertexCount; ++v) {
        if (lattice)
            s.vertices[v] = {float(int(rng() % 5) - 2), float(int(rng() % 5) - 2), float(int(rng() % 5) - 2)};
        else
            s.vertices[v] = {Unit(rng) * 3.0f, Unit(rng) * 3.0f, Unit(rng) * 3.0f};
    }
    for (unsigned t = 0; t < s.triangles; ++t)
        for (unsigned c = 0; c < 3; ++c) s.indices[t * 3 + c] = uint16_t(rng() % s.vertexCount);
    std::vector<unsigned> all;
    for (unsigned t = 0; t < s.triangles; ++t) all.push_back(t);
    SetBuilder{s, rng}.Build(all);

    // Boxes: sometimes shrunk, grown or shifted so they and the triangles disagree.
    if (rng() % 5 == 0) {
        for (unsigned n = 0; n < s.nodeCount; ++n) {
            if (rng() % 3 != 0) continue;
            const float k = rng() % 2 ? 0.2f + Unit(rng) * 0.1f : 1.0f + (rng() % 3) * 0.5f;
            s.nodes[n].box.half.x *= k;
            s.nodes[n].box.half.y *= k;
            s.nodes[n].box.half.z *= k;
            if (rng() % 3 == 0) s.nodes[n].centre.x += Unit(rng);
        }
    }
    for (float &c : s.cuboid) c = lattice ? float(1 + rng() % 2) * 0.5f : 0.3f + float(rng() % 1700) / 1000.0f;
    if (rng() % 12 == 0) s.cuboid[rng() % 3] = Num(rng);
    SensibleFrame(s.setFrame, rng, lattice);
    SensibleFrame(s.cuboidFrame, rng, lattice);
    if (rng() % 15 == 0) JunkFrame(rng() % 2 ? s.setFrame : s.cuboidFrame, rng);

    s.block[0] = shape::P(s.vertices);
    s.block[1] = s.block[2] = shape::P(s.vertices + s.vertexCount);
    s.block[3] = shape::P(s.indices);
    s.block[4] = s.block[5] = shape::P(s.indices + s.triangles * 3);
    s.block[6] = 0;
    s.block[7] = shape::P(s.nodes);
    s.block[8] = s.block[9] = shape::P(s.nodes + s.nodeCount);

    std::vector<float *> slots;
    for (unsigned v = 0; v < s.vertexCount; ++v) {
        slots.push_back(&s.vertices[v].x);
        slots.push_back(&s.vertices[v].y);
        slots.push_back(&s.vertices[v].z);
    }
    for (unsigned n = 0; n < s.nodeCount; ++n) {
        float *w = reinterpret_cast<float *>(&s.nodes[n]);
        for (unsigned k = 0; k < 6; ++k) slots.push_back(&w[k]);
    }
    FrameSlots(s.setFrame, slots);
    FrameSlots(s.cuboidFrame, slots);
    for (float &c : s.cuboid) slots.push_back(&c);
    InjectNaN(slots, rng);
}

} // namespace

DIFF_TEST(CompGeomSeparating_SetCuboid, 0x19f8e0)
{
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
        std::mt19937 rng(0x19f8);
        using Whole = bool(__cdecl *)(const void *, const void *, const void *, const void *);
        using Walk = bool(__cdecl *)(const void *, const void *, const void *);
        using Build = void(__thiscall *)(void *, const void *, const void *, const void *, const void *);
        const HookSpec *hw = HookAt(ctx, 0x19f8e0), *hk = HookAt(ctx, 0x19f930);
        if (!hw || !hk) return;
        const Whole whole[2] = {ctx.Original<Whole>(0x19f8e0), reinterpret_cast<Whole>(hw->replacement)};
        const Walk walk[2] = {ctx.Original<Walk>(0x19f930), reinterpret_cast<Walk>(hk->replacement)};
        const Build build = ctx.Original<Build>(0x19ffd0);
        unsigned failures = 0, wholeTrues = 0, walkTrues = 0, walkCases = 0;
        SetScene *scene = new SetScene;
        for (unsigned i = 0; i < kCases && failures < 5; ++i) {
            MakeSetScene(*scene, rng);
            bool r[2];
            for (unsigned s = 0; s < 2; ++s)
                r[s] = whole[s](scene->block, &scene->setFrame, scene->cuboid, &scene->cuboidFrame);
            ++ctx.cases;
            wholeTrues += r[0];
            if (r[0] != r[1]) {
                ctx.Fail("SetMeetsCuboid case %u: returns %d / %d", i, r[0], r[1]);
                ++failures;
            }
            // The walk from a random node, with a context the original builds.
            alignas(16) uint8_t context[sizeof(SeparationContext) + 16];
            std::memset(context, 0x5c, sizeof(context));
            build(context, scene->block, &scene->setFrame, scene->cuboid, &scene->cuboidFrame);
            const unsigned pick = rng() % scene->nodeCount;
            for (unsigned s = 0; s < 2; ++s) r[s] = walk[s](scene->nodes, &scene->nodes[pick], context);
            ++walkCases;
            walkTrues += r[0];
            if (r[0] != r[1]) {
                ctx.Fail("TreeMeetsCuboid case %u (node %u): returns %d / %d", i, pick, r[0], r[1]);
                ++failures;
            }
        }
        delete scene;
        if (!failures) {
            CheckRate(ctx, "SetMeetsCuboid", wholeTrues, kCases);
            CheckRate(ctx, "TreeMeetsCuboid", walkTrues, walkCases);
        }
    }
}

// ---- triangle against triangle: 0x1a4620, 0x1a42d0, 0x1a4ea0 ----

namespace {

constexpr unsigned kPairCases = 80000;

struct alignas(16) PairCase {
    float a[9];
    float b[9];
    float guard[4];
};

// Two triangles (a, b) of one kind: lattice, exactly coplanar (axis planes and tilted ones), generic, sharing a corner or
// an edge, nearly coplanar (within and beyond the plane epsilon), thin, junk.
void MakePair(PairCase &c, std::mt19937 &rng, unsigned kind)
{
    float *a = c.a, *b = c.b;
    auto lattice = [&](float *t, int range) {
        for (unsigned k = 0; k < 9; ++k) t[k] = float(int(rng() % (2 * range + 1)) - range);
    };
    if (kind < 4) {
        lattice(a, 2);
        lattice(b, 2);
    } else if (kind < 6) { // coplanar in an axis plane
        lattice(a, 3);
        lattice(b, 3);
        const unsigned axis = rng() % 3;
        const float level = float(int(rng() % 3) - 1);
        for (unsigned v = 0; v < 3; ++v) a[v * 3 + axis] = b[v * 3 + axis] = level;
    } else if (kind < 8) { // coplanar in a tilted plane (integer combinations of two vectors)
        int u[3], w[3];
        for (unsigned k = 0; k < 3; ++k) {
            u[k] = int(rng() % 5) - 2;
            w[k] = int(rng() % 5) - 2;
        }
        for (unsigned t = 0; t < 2; ++t)
            for (unsigned v = 0; v < 3; ++v) {
                const int p = int(rng() % 7) - 3, q = int(rng() % 7) - 3;
                for (unsigned k = 0; k < 3; ++k) (t ? b : a)[v * 3 + k] = float(p * u[k] + q * w[k]);
            }
    } else if (kind < 11) { // generic
        for (unsigned k = 0; k < 9; ++k) {
            a[k] = Unit(rng) * 2.0f;
            b[k] = Unit(rng) * 2.0f;
        }
    } else if (kind == 11) { // junk
        for (unsigned k = 0; k < 9; ++k) {
            a[k] = Num(rng);
            b[k] = Num(rng);
        }
    } else if (kind == 12 || kind == 13) { // b shares a corner (or an edge) with a
        for (unsigned k = 0; k < 9; ++k) {
            a[k] = Unit(rng) * 2.0f;
            b[k] = Unit(rng) * 2.0f;
        }
        const unsigned va = rng() % 3, vb = rng() % 3;
        for (unsigned k = 0; k < 3; ++k) b[vb * 3 + k] = a[va * 3 + k];
        if (kind == 13) {
            const unsigned va2 = (va + 1) % 3, vb2 = (vb + 1) % 3;
            for (unsigned k = 0; k < 3; ++k) b[vb2 * 3 + k] = a[va2 * 3 + k];
        }
    } else if (kind == 14 || kind == 15) { // nearly coplanar: the axis plane, then offsets around the plane epsilon
        lattice(a, 3);
        lattice(b, 3);
        const unsigned axis = rng() % 3;
        for (unsigned v = 0; v < 3; ++v) a[v * 3 + axis] = 0.0f;
        static const float kOff[] = {0.0f, 1.0e-11f, 1.1e-10f, 1.1920929e-10f, 1.2e-10f, 1.0e-9f, 1.0e-6f, 1.0e-3f};
        for (unsigned v = 0; v < 3; ++v) b[v * 3 + axis] = kOff[rng() % std::size(kOff)] * (rng() % 2 ? 1.0f : -1.0f);
    } else if (kind == 16) { // b's corners at heights 0..2 over a's plane
        lattice(a, 2);
        lattice(b, 2);
        for (unsigned v = 0; v < 3; ++v) a[v * 3 + 2] = 0.0f;
        for (unsigned v = 0; v < 3; ++v) b[v * 3 + 2] = float(int(rng() % 3));
    } else if (kind == 17) { // thin triangles
        for (unsigned k = 0; k < 9; ++k) {
            a[k] = Unit(rng) * 2.0f;
            b[k] = Unit(rng) * 2.0f;
        }
        for (unsigned k = 0; k < 3; ++k) {
            a[6 + k] = a[3 + k] + Unit(rng) * 0.001f;
            if (rng() % 2) b[6 + k] = b[3 + k] + Unit(rng) * 0.001f;
        }
    } else { // lattice with b standing on a corner of a's plane
        lattice(a, 2);
        lattice(b, 2);
        for (unsigned v = 0; v < 3; ++v) b[v * 3 + 2] = float(int(rng() % 3) - 1) * (v == 0 ? 0.0f : 1.0f);
    }
}

void PairSlots(PairCase &c, std::vector<float *> &slots)
{
    for (float &v : c.a) slots.push_back(&v);
    for (float &v : c.b) slots.push_back(&v);
}

} // namespace

DIFF_TEST(CompGeomSeparating_TrianglePair, 0x1a4620)
{
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
        std::mt19937 rng(0x1a46);
        using Fn = bool(__cdecl *)(const float *, const float *);
        const HookSpec *h = HookAt(ctx, 0x1a4620);
        if (!h) return;
        const Fn fn[2] = {ctx.Original<Fn>(0x1a4620), reinterpret_cast<Fn>(h->replacement)};
        unsigned failures = 0, trues = 0;
        for (unsigned i = 0; i < kPairCases && failures < 5; ++i) {
            PairCase c[2];
            std::memset(&c[0], 0x5c, sizeof(PairCase));
            const unsigned kind = rng() % 19;
            MakePair(c[0], rng, kind);
            std::vector<float *> slots;
            PairSlots(c[0], slots);
            InjectNaN(slots, rng);
            c[1] = c[0];
            bool r[2];
            for (unsigned s = 0; s < 2; ++s) r[s] = fn[s](c[s].a, c[s].b);
            ++ctx.cases;
            trues += r[0];
            if (r[0] != r[1] || !SameBytes(c[0], c[1])) {
                ctx.Fail("TriangleMeetsTriangle case %u (kind %u): returns %d / %d", i, kind, r[0], r[1]);
                ++failures;
            }
        }
        if (!failures) CheckRate(ctx, "TriangleMeetsTriangle", trues, kPairCases);
    }
}

DIFF_TEST(CompGeomSeparating_Coplanar, 0x1a42d0)
{
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
        std::mt19937 rng(0x1a42);
        using Fn = bool(__cdecl *)(const float *, const float *);
        const HookSpec *h = HookAt(ctx, 0x1a42d0);
        if (!h) return;
        const Fn fn[2] = {ctx.Original<Fn>(0x1a42d0), reinterpret_cast<Fn>(h->replacement)};
        unsigned failures = 0, trues = 0;
        for (unsigned i = 0; i < kPairCases && failures < 5; ++i) {
            PairCase c[2];
            std::memset(&c[0], 0x5c, sizeof(PairCase));
            const unsigned kind = rng() % 12 < 9 ? rng() % 8 : (rng() % 3 == 0 ? 14 : rng() % 19);
            MakePair(c[0], rng, kind);
            std::vector<float *> slots;
            PairSlots(c[0], slots);
            InjectNaN(slots, rng);
            c[1] = c[0];
            bool r[2];
            for (unsigned s = 0; s < 2; ++s) r[s] = fn[s](c[s].a, c[s].b);
            ++ctx.cases;
            trues += r[0];
            if (r[0] != r[1] || !SameBytes(c[0], c[1])) {
                ctx.Fail("CoplanarTrianglesMeet case %u (kind %u): returns %d / %d", i, kind, r[0], r[1]);
                ++failures;
            }
        }
        if (!failures) CheckRate(ctx, "CoplanarTrianglesMeet", trues, kPairCases);
    }
}

DIFF_TEST(CompGeomSeparating_PointInTriangle, 0x1a4ea0)
{
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
        std::mt19937 rng(0x1a4e);
        using Fn = bool(__cdecl *)(const float *, const float *, int, int);
        const HookSpec *h = HookAt(ctx, 0x1a4ea0);
        if (!h) return;
        const Fn fn[2] = {ctx.Original<Fn>(0x1a4ea0), reinterpret_cast<Fn>(h->replacement)};
        unsigned failures = 0, trues = 0;
        for (unsigned i = 0; i < kPairCases && failures < 5; ++i) {
            PairCase c[2];
            std::memset(&c[0], 0x5c, sizeof(PairCase));
            const unsigned kind = rng() % 5;
            for (unsigned k = 0; k < 9; ++k) c[0].a[k] = kind < 2 ? float(int(rng() % 7) - 3) : Unit(rng) * 2.0f; // t
            for (unsigned k = 0; k < 3; ++k) c[0].b[k] = kind < 2 ? float(int(rng() % 7) - 3) : Unit(rng) * 2.0f; // p
            if (kind == 4)
                for (unsigned k = 0; k < 3; ++k) c[0].b[k] = (c[0].a[k] + c[0].a[3 + k] + c[0].a[6 + k]) / 3.0f;
            if (kind == 3) // p on a corner
                for (unsigned k = 0, v = rng() % 3; k < 3; ++k) c[0].b[k] = c[0].a[3 * v + k];
            std::vector<float *> slots;
            PairSlots(c[0], slots);
            InjectNaN(slots, rng);
            c[1] = c[0];
            const int d = int(rng() % 3), s2 = (d + 1 + int(rng() % 2)) % 3;
            bool r[2];
            for (unsigned s = 0; s < 2; ++s) r[s] = fn[s](c[s].b, c[s].a, d, s2);
            ++ctx.cases;
            trues += r[0];
            if (r[0] != r[1] || !SameBytes(c[0], c[1])) {
                ctx.Fail("PointInTriangleProjected case %u (kind %u, %d %d): returns %d / %d", i, kind, d, s2, r[0], r[1]);
                ++failures;
            }
        }
        if (!failures) CheckRate(ctx, "PointInTriangleProjected", trues, kPairCases);
    }
}

// ---- set against set: 0x1a01c0 and its walk 0x1a0210 ----

namespace {

// The second scene a copy of the first (its block pointing into itself), moved a little.
void CopySceneShifted(SetScene &to, const SetScene &from, std::mt19937 &rng)
{
    to = from;
    to.block[0] = shape::P(to.vertices);
    to.block[1] = to.block[2] = shape::P(to.vertices + to.vertexCount);
    to.block[3] = shape::P(to.indices);
    to.block[4] = to.block[5] = shape::P(to.indices + to.triangles * 3);
    to.block[7] = shape::P(to.nodes);
    to.block[8] = to.block[9] = shape::P(to.nodes + to.nodeCount);
    to.setFrame.mOffset[0] += float(int(rng() % 3) - 1) * 0.5f;
}

} // namespace

DIFF_TEST(CompGeomSeparating_SetSet, 0x1a01c0)
{
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
        std::mt19937 rng(0x1a01);
        using Whole = bool(__cdecl *)(const void *, const void *, const void *, const void *);
        using Walk = bool(__cdecl *)(const void *, const void *, const void *, const void *, const void *);
        using Build = void(__thiscall *)(void *, const void *, const void *, const void *, const void *);
        const HookSpec *hw = HookAt(ctx, 0x1a01c0), *hk = HookAt(ctx, 0x1a0210);
        if (!hw || !hk) return;
        const Whole whole[2] = {ctx.Original<Whole>(0x1a01c0), reinterpret_cast<Whole>(hw->replacement)};
        const Walk walk[2] = {ctx.Original<Walk>(0x1a0210), reinterpret_cast<Walk>(hk->replacement)};
        const Build build = ctx.Original<Build>(0x19ffd0);
        unsigned failures = 0, wholeTrues = 0, walkTrues = 0, walkCases = 0;
        SetScene *one = new SetScene, *two = new SetScene;
        for (unsigned i = 0; i < kCases && failures < 5; ++i) {
            MakeSetScene(*one, rng);
            MakeSetScene(*two, rng);
            if (rng() % 4 == 0) CopySceneShifted(*two, *one, rng);
            bool r[2];
            for (unsigned s = 0; s < 2; ++s) r[s] = whole[s](one->block, &one->setFrame, two->block, &two->setFrame);
            ++ctx.cases;
            wholeTrues += r[0];
            if (r[0] != r[1]) {
                ctx.Fail("SetMeetsSet case %u: returns %d / %d", i, r[0], r[1]);
                ++failures;
            }
            // The walk from random nodes, with a context the original builds.
            alignas(16) uint8_t context[sizeof(SeparationContext) + 16];
            std::memset(context, 0x5c, sizeof(context));
            build(context, one->block, &one->setFrame, two->block, &two->setFrame);
            const unsigned p1 = rng() % one->nodeCount, p2 = rng() % two->nodeCount;
            bool w[2];
            for (unsigned s = 0; s < 2; ++s)
                w[s] = walk[s](one->nodes, two->nodes, &one->nodes[p1], &two->nodes[p2], context);
            ++walkCases;
            walkTrues += w[0] != 0;
            if (w[0] != w[1]) {
                ctx.Fail("NodesMeet case %u (nodes %u, %u): returns %d / %d", i, p1, p2, w[0], w[1]);
                ++failures;
            }
        }
        delete one;
        delete two;
        if (!failures) {
            CheckRate(ctx, "SetMeetsSet", wholeTrues, kCases);
            CheckRate(ctx, "NodesMeet", walkTrues, walkCases);
        }
    }
}
