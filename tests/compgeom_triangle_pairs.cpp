// Differential test for the triangle-set walkers (lane PK, src/compgeom/TrianglePairs.h; RVAs 1.18.0.187): each hook table
// replacement against the original at its RVA.
// - CompGeomTrianglePairs_Cores: the Moller-Trumbore core 0x1a3240 on edges derived from random triangles and lines aimed
//   at them, the four triangle wrappers 0x1a3130 / 0x1a2db0 / 0x1a2ec0 / 0x1a2fe0 and the box span 0x19ace0. The whole
//   case buffer and the returned bool are compared bit for bit (a t written before a false return counts).
// - CompGeomTrianglePairs_Walkers: trees built over random triangle sets (every child pairing: triangle/triangle,
//   triangle/node, node/triangle, node/node; boxes exact, or perturbed so a box misses what its triangles hit and the
//   other way round); the tree walkers 0x19b670 / 0x19b8e0 / 0x19bb50 / 0x19b330 from random nodes, and the forwarders and
//   in-frame wrappers from the root. The two sides read one scene; the result, the t written and the return value are
//   compared.
//
// Float inputs follow docs/subsystems/scenegraph.md ("Float inputs and NaNs"): edge values and junk freely, but in at
// most one slot of a case a NaN, and then no infinities or values of 1e6 and above in the case. Each function must
// return both true and false in at least 2% of its cases.
// Each test runs at the harness's 53-bit x87 precision and again at the game's 24-bit.

#include "compgeom/TrianglePairs.h"
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
    0xff7fffff, 0x7f800000, 0xff800000, 0x3f000000, 0x40400000, 0x34000000, 0x2f03126f, 0xaf03126f, 0x2f031270,
    0x2f03126e,
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

// Puts at most one NaN into the slots: in a third of the cases one of them; then every slot of 1e6 and above or infinite
// is brought back to a small value, so that no second NaN can arise.
void InjectNaN(const std::vector<float *> &slots, std::mt19937 &rng)
{
    if (slots.empty() || rng() % 3 != 0) return;
    *slots[rng() % slots.size()] = NaNValue(rng);
    for (float *s : slots)
        if (!std::isnan(*s) && !(std::fabs(*s) < 1.0e6f)) *s = 0.75f;
}

const HookSpec *HookAt(difftest::Context &ctx, uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    ctx.Fail("no hook at 0x%x", rva);
    return nullptr;
}

constexpr unsigned kCases = 30000;

void CheckRate(difftest::Context &ctx, const char *name, unsigned trues, unsigned cases)
{
    // A test that never reaches both outcomes would pass for any function that returns one of them.
    if (trues < cases / 50 || trues > cases - cases / 50)
        ctx.Fail("%s: %u of %u cases returned true (the generator misses a branch)", name, trues, cases);
}

// ---- the cores ----

struct alignas(16) Buf {
    float f[64];
};
constexpr unsigned kTri = 0, kE1 = 9, kE2 = 12, kS = 15, kDir = 18, kLine = 24, kOut = 32;

template <class Fn, class Setup, class Call>
void RunBuf(difftest::Context &ctx, const char *name, uint32_t rva, std::mt19937 &rng, Setup setup, Call call)
{
    const HookSpec *h = HookAt(ctx, rva);
    if (!h) return;
    const Fn fn[2] = {ctx.Original<Fn>(rva), reinterpret_cast<Fn>(h->replacement)};
    unsigned failures = 0, trues = 0;
    for (unsigned i = 0; i < kCases && failures < 5; ++i) {
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
    if (failures == 0) CheckRate(ctx, name, trues, kCases);
}

enum Lin { kLineKind, kRayKind, kSegKind };

// A triangle (f[0..8]) and a line (f[24..29]: origin and direction, or a segment's start and end) aimed at it, with the
// edges the wrappers form in f[9..20]. Kinds of case: aimed (a point of the triangle's plane region, +- a margin), a
// lattice (integers, so the parameters land exactly on 0 and 1), the parallel boundary (a unit right triangle and a
// direction of the threshold's size), and junk.
void SetupTriangleCase(Buf &b, std::mt19937 &rng, Lin lin, bool derived)
{
    std::memset(&b, 0x5c, sizeof(b));
    float *tri = &b.f[kTri];
    float *line = &b.f[kLine];
    float dir[3];
    const unsigned kind = rng() % 16;
    if (kind < 3) { // lattice
        for (unsigned i = 0; i < 9; ++i) tri[i] = float(int(rng() % 5) - 2);
        for (unsigned i = 0; i < 3; ++i) {
            const float origin = float(int(rng() % 9) - 4);
            dir[i] = float(int(rng() % 7) - 3);
            line[i] = origin;
        }
    } else if (kind == 3) { // the parallel threshold
        static const float corners[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        std::memcpy(tri, corners, sizeof(corners));
        const uint32_t k = 0x2f03126fu + (rng() % 5) - 2; // a few ulps either side
        dir[0] = dir[1] = 0.0f;
        dir[2] = Float(k | ((rng() & 1) ? 0x80000000u : 0));
        line[0] = float(rng() % 5) * 0.25f;
        line[1] = float(rng() % 5) * 0.25f;
        line[2] = float(int(rng() % 5) - 2);
        if (rng() % 3 == 0) dir[2] = dir[2] * 1.0f; // (no-op: keeps the draw count stable)
    } else if (kind == 4) { // junk
        for (unsigned i = 0; i < 9; ++i) tri[i] = Num(rng);
        for (unsigned i = 0; i < 3; ++i) {
            line[i] = Num(rng);
            dir[i] = Num(rng);
        }
    } else { // aimed
        for (unsigned i = 0; i < 9; ++i) tri[i] = Unit(rng) * 3.0f;
        const float a = Unit(rng) * 0.7f + 0.5f, c = Unit(rng) * 0.7f + 0.5f; // barycentric, around the inside
        float target[3];
        for (unsigned i = 0; i < 3; ++i) target[i] = tri[i] + a * (tri[3 + i] - tri[i]) + c * (tri[6 + i] - tri[i]);
        float origin[3];
        for (unsigned i = 0; i < 3; ++i) {
            origin[i] = target[i] + Unit(rng) * 5.0f;
            dir[i] = target[i] - origin[i];
        }
        static const float scales[] = {1.0f, 1.0f, 2.0f, 0.4f, 5.0f, -1.0f, 0.0f, 1.0e-3f, 1.0f};
        const float k = scales[rng() % std::size(scales)];
        for (float &d : dir) d *= k;
        if (rng() % 12 == 0) dir[rng() % 3] = 0.0f;
        for (unsigned i = 0; i < 3; ++i) line[i] = origin[i];
    }
    // A segment's second slot is its end point.
    for (unsigned i = 0; i < 3; ++i) line[3 + i] = lin == kSegKind ? line[i] + dir[i] : dir[i];
    if (rng() % 12 == 0) line[rng() % 6] = Float(kEdges[rng() % std::size(kEdges)]);

    std::vector<float *> slots;
    if (!derived) {
        for (unsigned i = 0; i < 9; ++i) slots.push_back(&tri[i]);
        for (unsigned i = 0; i < 6; ++i) slots.push_back(&line[i]);
        InjectNaN(slots, rng);
    }
    // The wrappers' own edges, formed the way they form them, for the direct core.
    const float *d = lin == kSegKind ? nullptr : &line[3];
    float dirs[3];
    for (unsigned i = 0; i < 3; ++i) dirs[i] = d ? d[i] : line[3 + i] - line[i];
    for (unsigned i = 0; i < 3; ++i) {
        b.f[kE1 + i] = tri[3 + i] - tri[i];
        b.f[kE2 + i] = tri[6 + i] - tri[i];
        b.f[kS + i] = line[i] - tri[i];
        b.f[kDir + i] = dirs[i];
    }
    if (derived) {
        for (unsigned i = 0; i < 12; ++i) slots.push_back(&b.f[kE1 + i]);
        InjectNaN(slots, rng);
    }
}

} // namespace

DIFF_TEST(CompGeomTrianglePairs_Cores, 0x1a3240)
{
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
        std::mt19937 rng(0x1a32);
        using Core = bool(__cdecl *)(float *, const void *, const void *, const void *, const void *);
        RunBuf<Core>(ctx, "TriangleCore", 0x1a3240, rng,
                     [](Buf &b, std::mt19937 &r) { SetupTriangleCase(b, r, r() % 2 ? kLineKind : kSegKind, true); },
                     [](Core fn, Buf &b) {
                         return uint64_t(fn(&b.f[kOut], &b.f[kE1], &b.f[kE2], &b.f[kS], &b.f[kDir]));
                     });
        using Hit = bool(__cdecl *)(float *, const void *, const void *);
        RunBuf<Hit>(ctx, "TriangleLineHit", 0x1a3130, rng,
                    [](Buf &b, std::mt19937 &r) { SetupTriangleCase(b, r, kLineKind, false); },
                    [](Hit fn, Buf &b) { return uint64_t(fn(&b.f[kOut], &b.f[kTri], &b.f[kLine])); });
        using Test = bool(__cdecl *)(const void *, const void *);
        RunBuf<Test>(ctx, "TriangleLineTest", 0x1a2db0, rng,
                     [](Buf &b, std::mt19937 &r) { SetupTriangleCase(b, r, kLineKind, false); },
                     [](Test fn, Buf &b) { return uint64_t(fn(&b.f[kTri], &b.f[kLine])); });
        RunBuf<Test>(ctx, "TriangleRayTest", 0x1a2ec0, rng,
                     [](Buf &b, std::mt19937 &r) { SetupTriangleCase(b, r, kRayKind, false); },
                     [](Test fn, Buf &b) { return uint64_t(fn(&b.f[kTri], &b.f[kLine])); });
        RunBuf<Test>(ctx, "TriangleSegmentTest", 0x1a2fe0, rng,
                     [](Buf &b, std::mt19937 &r) { SetupTriangleCase(b, r, kSegKind, false); },
                     [](Test fn, Buf &b) { return uint64_t(fn(&b.f[kTri], &b.f[kLine])); });
        // 0x19ace0: the cuboid's half extents at f[0..2], its centre at f[3..5], the line at f[24..29]; the span in f[32..33].
        // It returns 0 or 2.
        using Span = int(__cdecl *)(float *, float *, const void *, const void *, const void *);
        RunBuf<Span>(
            ctx, "BoxLineSpan", 0x19ace0, rng,
            [](Buf &b, std::mt19937 &r) {
                std::memset(&b, 0x5c, sizeof(b));
                const bool junk = r() % 6 == 0;
                for (unsigned i = 0; i < 3; ++i) {
                    b.f[i] = junk ? Num(r) : 0.2f + float(r() % 3000) / 1000.0f;
                    b.f[3 + i] = junk ? Num(r) : Unit(r) * 3.0f;
                    b.f[kLine + i] = junk ? Num(r) : b.f[3 + i] + Unit(r) * 6.0f;
                    b.f[kLine + 3 + i] = junk ? Num(r) : (b.f[3 + i] + Unit(r) * 1.5f) - b.f[kLine + i];
                }
                if (r() % 10 == 0) b.f[kLine + 3 + r() % 3] = 0.0f;
                std::vector<float *> slots;
                for (unsigned i = 0; i < 6; ++i) slots.push_back(&b.f[i]);
                for (unsigned i = 0; i < 6; ++i) slots.push_back(&b.f[kLine + i]);
                InjectNaN(slots, r);
            },
            [](Span fn, Buf &b) { return uint64_t(uint32_t(fn(&b.f[kOut], &b.f[kOut + 1], &b.f[0], &b.f[3], &b.f[kLine])) != 0 ? 2 : 0); });
    }
}

namespace {

// ---- the scenes ----

constexpr unsigned kMaxTriangles = 9, kMaxVertices = 16, kMaxNodes = kMaxTriangles;

struct Scene {
    uint32_t block[10]; // the set: vertices at +0, indices at +0xc, the tree's vtable +0x18 and nodes +0x1c (begin, end, cap)
    Vec3 vertices[kMaxVertices];
    uint16_t indices[kMaxTriangles * 3];
    AABBTreeNode nodes[kMaxNodes];
    float line[6]; // origin and direction, or start and end
    float frame[13];
    float lo;
    unsigned triangles, vertexCount, nodeCount;
};

struct Builder {
    Scene &scene;
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

// Builds a scene: a set of triangles with its tree, a linear shape aimed at it (in the set's own frame, or, with
// `frame`, in the world through a frame), a lower bound for the hits.
void MakeScene(Scene &s, std::mt19937 &rng, Lin lin, bool frame)
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
    Builder{s, rng}.Build(all);

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

    // The linear shape, local: aimed at a point near a random vertex or the middle of the set.
    float target[3], origin[3], dir[3];
    const Vec3 &pivot = s.vertices[s.indices[rng() % (s.triangles * 3)]];
    const bool junk = !lattice && rng() % 10 == 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (lattice) {
            origin[i] = float(int(rng() % 9) - 4);
            dir[i] = float(int(rng() % 7) - 3);
            continue;
        }
        const float p[3] = {pivot.x, pivot.y, pivot.z};
        target[i] = rng() % 4 ? p[i] + Unit(rng) * 0.5f : Unit(rng);
        origin[i] = Unit(rng) * 6.0f;
        dir[i] = target[i] - origin[i];
    }
    if (!lattice) {
        static const float scales[] = {1.0f, 1.0f, 2.0f, 0.4f, 5.0f, -1.0f, 0.0f, 1.0e-3f, 1.0f, 1.0f};
        const float k = scales[rng() % std::size(scales)];
        for (float &d : dir) d *= k;
        if (rng() % 12 == 0) dir[rng() % 3] = 0.0f;
    }
    if (junk)
        for (unsigned i = 0; i < 3; ++i) {
            origin[i] = Num(rng);
            dir[i] = Num(rng);
        }

    // The frame: the identity, a rotation, a uniform scale or a general matrix, with an offset; the local shape is carried
    // to the world through it (so that the set ends up under the line again).
    float matrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float offset[3] = {0, 0, 0};
    uint32_t flags = 0;
    if (frame) {
        for (unsigned i = 0; i < 3; ++i) offset[i] = lattice ? float(int(rng() % 5) - 2) : Unit(rng) * 3.0f;
        const unsigned variant = lattice ? 0 : rng() % 8;
        if (variant == 1 || variant == 2) {
            flags = 2;
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
            std::memcpy(matrix, r, sizeof(matrix));
        } else if (variant == 3) {
            flags = 6;
            const float k = 0.5f + float(rng() % 3000) / 1000.0f;
            for (unsigned i = 0; i < 9; ++i) matrix[i] = i % 4 == 0 ? k : 0.0f;
        } else if (variant == 4) {
            flags = 0x10;
            for (unsigned i = 0; i < 9; ++i) matrix[i] = (i % 4 == 0 ? 1.0f : 0.0f) + Unit(rng) * 0.4f;
        } else if (variant == 5) {
            flags = rng() % 2 ? (rng() & 0x1f) : rng();
            for (float &v : matrix) v = Num(rng);
        }
        // world = matrix * local + offset (row major); whichever way the game reads the matrix, the identity, the
        // translations and the scales aim true, and a rotation lands near the set often enough.
        const auto carry = [&](const float *v, float *out, bool point) {
            for (unsigned r = 0; r < 3; ++r)
                out[r] = matrix[r * 3] * v[0] + matrix[r * 3 + 1] * v[1] + matrix[r * 3 + 2] * v[2] + (point ? offset[r] : 0.0f);
        };
        float po[3], pd[3];
        carry(origin, po, true);
        if (lin == kSegKind) {
            const float end[3] = {origin[0] + dir[0], origin[1] + dir[1], origin[2] + dir[2]};
            carry(end, pd, true);
        } else {
            carry(dir, pd, false);
        }
        for (unsigned i = 0; i < 3; ++i) {
            origin[i] = po[i];
            dir[i] = pd[i];
        }
        std::memcpy(&s.frame[0], &flags, 4);
        std::memcpy(&s.frame[1], matrix, sizeof(matrix));
        std::memcpy(&s.frame[10], offset, sizeof(offset));
    }
    for (unsigned i = 0; i < 3; ++i) {
        s.line[i] = origin[i];
        // A segment's second slot is the end point (carried already in a frame, else formed here).
        s.line[3 + i] = lin == kSegKind ? (frame ? dir[i] : origin[i] + dir[i]) : dir[i];
    }
    if (rng() % 15 == 0) s.line[rng() % 6] = Float(kEdges[rng() % std::size(kEdges)]);

    static const float los[] = {-FLT_MAX, -FLT_MAX, 0.0f, 0.0f, -1.0f, 0.5f, 1.0f, 0.25f};
    s.lo = rng() % 4 ? los[rng() % std::size(los)] : Unit(rng) * 2.0f;

    // The fake set: a raw block laid out like the set (three vectors of begin, end, capacity words).
    s.block[0] = shape::P(s.vertices);
    s.block[1] = s.block[2] = shape::P(s.vertices + s.vertexCount);
    s.block[3] = shape::P(s.indices);
    s.block[4] = s.block[5] = shape::P(s.indices + s.triangles * 3);
    s.block[6] = 0;
    s.block[7] = shape::P(s.nodes);
    s.block[8] = s.block[9] = shape::P(s.nodes + s.nodeCount);

    // Up to one NaN anywhere in the scene's floats.
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
    for (float &f : s.line) slots.push_back(&f);
    if (frame)
        for (unsigned k = 1; k < 13; ++k) slots.push_back(&s.frame[k]);
    slots.push_back(&s.lo);
    InjectNaN(slots, rng);
}

struct Result {
    uint32_t ret, t;
    bool operator==(const Result &o) const { return ret == o.ret && t == o.t; }
};

template <class Fn, class Call>
void RunScenes(difftest::Context &ctx, const char *name, uint32_t rva, std::mt19937 &rng, Lin lin, bool frame, Call call)
{
    const HookSpec *h = HookAt(ctx, rva);
    if (!h) return;
    const Fn fn[2] = {ctx.Original<Fn>(rva), reinterpret_cast<Fn>(h->replacement)};
    unsigned failures = 0, trues = 0;
    Scene *scene = new Scene;
    for (unsigned i = 0; i < kCases && failures < 5; ++i) {
        MakeScene(*scene, rng, lin, frame);
        const unsigned pick = rng() % scene->nodeCount; // the node a walker starts from
        const uint32_t pattern = 0x5c5c5c5c;
        const uint32_t tInit = rng() % 3 == 0 ? Bits(Num(rng) > 0 ? 1.0f : 0.0f) : (rng() % 2 ? Bits(FLT_MAX) : pattern);
        Result r[2];
        for (unsigned side = 0; side < 2; ++side) {
            float t;
            std::memcpy(&t, &tInit, 4);
            const bool ret = call(fn[side], *scene, &t, pick);
            r[side] = {uint32_t(ret), Bits(t)};
        }
        ++ctx.cases;
        trues += r[0].ret;
        if (!(r[0] == r[1])) {
            ctx.Fail("%s case %u: returns %u / %u, t %08x / %08x", name, i, r[0].ret, r[1].ret, r[0].t, r[1].t);
            ++failures;
        }
    }
    delete scene;
    if (failures == 0) CheckRate(ctx, name, trues, kCases);
}

const void *SetOf(const Scene &s) { return s.block; }
const void *NodeOf(const Scene &s, unsigned n) { return &s.nodes[n]; }

struct Linear {
    const char *name;
    Lin lin;
};
constexpr Linear kLinears[3] = {{"Line", kLineKind}, {"Ray", kRayKind}, {"Segment", kSegKind}};

} // namespace

DIFF_TEST(CompGeomTrianglePairs_Walkers, 0x19b330)
{
    for (const bool single : {false, true}) {
        difftest::X87Precision precision(single ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53);
        std::mt19937 rng(0x19b3);
        char name[64];

        // The tree walkers: (node, set, linear), starting at a random node.
        using Walk = bool(__cdecl *)(const void *, const void *, const void *);
        static const uint32_t walkers[3] = {0x19b670, 0x19b8e0, 0x19bb50};
        for (unsigned l = 0; l < 3; ++l) {
            std::snprintf(name, sizeof(name), "TestTreeWalk%s", kLinears[l].name);
            RunScenes<Walk>(ctx, name, walkers[l], rng, kLinears[l].lin, false,
                            [](Walk fn, Scene &s, float *, unsigned node) { return fn(NodeOf(s, node), SetOf(s), s.line); });
        }
        // The hit walker: (t, lo, node, set, line) on a line (a segment's walk is given the same Line3 layout).
        using HitWalk = bool(__cdecl *)(float *, float, const void *, const void *, const void *);
        for (unsigned l = 0; l < 2; ++l) {
            std::snprintf(name, sizeof(name), "HitTreeWalk%s", kLinears[l].name);
            RunScenes<HitWalk>(ctx, name, 0x19b330, rng, kLinears[l].lin, false,
                               [](HitWalk fn, Scene &s, float *t, unsigned node) {
                                   return fn(t, s.lo, NodeOf(s, node), SetOf(s), s.line);
                               });
        }

        // The forwarders (set, linear), the hits (t, set, linear), and their in-frame wrappers.
        using SetTest = bool(__cdecl *)(const void *, const void *);
        static const uint32_t setTests[3] = {0x19ada0, 0x19adc0, 0x19ade0};
        static const uint32_t frameTests[3] = {0x19ae00, 0x19aea0, 0x19af40};
        using FrameTest = bool(__cdecl *)(const void *, const void *, const void *);
        using SetHit = bool(__cdecl *)(float *, const void *, const void *);
        static const uint32_t setHits[3] = {0x19afe0, 0x19b030, 0x19b0b0};
        static const uint32_t frameHits[3] = {0x19b150, 0x19b1f0, 0x19b290};
        using FrameHit = bool(__cdecl *)(float *, const void *, const void *, const void *);
        for (unsigned l = 0; l < 3; ++l) {
            std::snprintf(name, sizeof(name), "TestTriangleSet%s", kLinears[l].name);
            RunScenes<SetTest>(ctx, name, setTests[l], rng, kLinears[l].lin, false,
                               [](SetTest fn, Scene &s, float *, unsigned) { return fn(SetOf(s), s.line); });
            std::snprintf(name, sizeof(name), "TestTriangleSetInFrame%s", kLinears[l].name);
            RunScenes<FrameTest>(ctx, name, frameTests[l], rng, kLinears[l].lin, true,
                                 [](FrameTest fn, Scene &s, float *, unsigned) { return fn(SetOf(s), s.frame, s.line); });
            std::snprintf(name, sizeof(name), "HitTriangleSet%s", kLinears[l].name);
            RunScenes<SetHit>(ctx, name, setHits[l], rng, kLinears[l].lin, false,
                              [](SetHit fn, Scene &s, float *t, unsigned) { return fn(t, SetOf(s), s.line); });
            std::snprintf(name, sizeof(name), "HitTriangleSetInFrame%s", kLinears[l].name);
            RunScenes<FrameHit>(ctx, name, frameHits[l], rng, kLinears[l].lin, true,
                                [](FrameHit fn, Scene &s, float *t, unsigned) { return fn(t, SetOf(s), s.frame, s.line); });
        }
    }
}
