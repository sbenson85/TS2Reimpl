// Differential tests of lane 261111d's leaves (src/compgeom/Dot4.cpp, src/sg/SGUtilitiesProject.cpp,
// src/sg/ShapeMaterialHandle.cpp): original vs ours, each called directly.
//
//   SG_Utilities_Dot4                  0x1a6840   nGZCompGeom::Dot4: 4-element products summed in SSE (the order shows in
//       NaN payloads of both signs, infinities, 0 * inf, overflow and the rounding of mixed magnitudes); the result's bits.
//   SG_Utilities_BoxProjection         0xaf25a0   BoxProjection::Project on a 0x2c block of junk words (byte 0 the
//       orthographic flag: 0, 1, 0x80 or junk). Boxes: random, inverted, flat, near the w = 1e-5 plane, NaN payloads of both
//       signs, infinities and zeros of both signs in an element. Matrices: random, w = z (perspective: corners on both
//       sides), w = 1, all corners in front, all behind, w nearly constant around 1e-5 (the edge's small |dw| arm), and an
//       element replaced by a special word. Noted: al and all 11 words of the block. Each case at the default x87
//       precision and at 24 bits (the all-front reciprocal goes through a double).
//   SG_Utilities_ShapeMaterialHandle   0xae8820 / 0xae8840 / 0xae8800 / 0xae88a0 / 0xae8a00 / 0xae8870: the handle's
//       constructors (index, name, copy), Assign (a cell onto itself too), Release and the destructor on three handle
//       cells, against a manager fake in GetCachedQueriedObject's cell 0x10f8fcc whose slots 19 (FindOrLoad), 20
//       (AddRefIndex) and 21 (ReleaseIndex) are logged with their arguments and note the three cells at the call, so the
//       order of the store and the manager's calls shows. Noted: the cells after each call, and the constructors' and
//       Assign's eax.
#include "sgfx.h"

#include "compgeom/Rigid.h"
#include "sg/SGUtilities.h"
#include "sg/cShape.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <utility>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }
float Float(uint32_t w) { return std::bit_cast<float>(w); }

// A word that is special now and then: NaN payloads of both signs (quiet and signalling), infinities, zeros, the largest
// and smallest magnitudes; otherwise `normal`.
uint32_t Special(std::mt19937 &rng)
{
    static const uint32_t kWords[] = {0x7fc00000u, 0xffc00000u, 0x7fc12345u, 0xffd54321u, 0x7f800001u, 0xff812345u,
                                      0x7f800000u, 0xff800000u, 0x00000000u, 0x80000000u, 0x7f7fffffu, 0xff7fffffu,
                                      0x00000001u, 0x80400000u, 0x3f800000u, 0xbf800000u};
    return kWords[rng() % (sizeof(kWords) / sizeof(kWords[0]))];
}
float Draw(std::mt19937 &rng, float span) { return (float(rng() % 2000001) / 1000000.0f - 1.0f) * span; }

// ---- Dot4 ----

DIFF_TEST(SG_Utilities_Dot4, 0x1a6840)
{
    using Fn = float(__cdecl *)(const float *, const float *);
    const Fn original = ctx.Original<Fn>(0x1a6840);
    sgfx::CheckSides(ctx, "Dot4", 512, [](uint32_t, drawfx::Cells &) {},
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned call = 0; call < 8; ++call) {
                             float a[4], b[4];
                             const unsigned mode = rng() % 4;
                             for (unsigned k = 0; k < 4; ++k) {
                                 a[k] = Draw(rng, mode == 1 ? 1e30f : 10.0f);
                                 b[k] = Draw(rng, mode == 2 ? 1e-30f : 10.0f);
                                 if (mode == 3 && rng() % 3 == 0) a[k] = Float(Special(rng));
                                 if (mode == 3 && rng() % 3 == 0) b[k] = Float(Special(rng));
                             }
                             // Mixed magnitudes: one large term that the small ones round against, in either place.
                             if (rng() % 4 == 0) a[rng() & 3] = 3e7f;
                             const float r = isOriginal ? original(a, b) : nGZCompGeom::Dot4(a, b);
                             drawfx::Note("dot", Bits(r));
                         }
                     });
}

// ---- the projector ----

struct ProjectCase {
    float box[6];
    float matrix[16];
    uint32_t block[11];
};

void BuildProjectCase(ProjectCase &c, std::mt19937 &rng)
{
    for (unsigned k = 0; k < 3; ++k) {
        const float a = Draw(rng, 50.0f), b = Draw(rng, 50.0f);
        c.box[k] = std::fmin(a, b);
        c.box[3 + k] = std::fmax(a, b);
    }
    switch (rng() % 8) {
    case 0: std::swap(c.box[rng() % 3], c.box[3 + rng() % 3]); break; // inverted in some axis
    case 1: c.box[3 + rng() % 3] = c.box[rng() % 3]; break;           // flat
    case 2: c.box[rng() % 6] = Float(Special(rng)); break;
    case 3: // a small box near the origin, so w lands near 1e-5
        for (unsigned k = 0; k < 3; ++k) c.box[k] = -Draw(rng, 1e-5f) - 1e-6f, c.box[3 + k] = Draw(rng, 1e-5f) + 2e-6f;
        break;
    default: break;
    }
    for (float &m : c.matrix) m = Draw(rng, 2.0f);
    float *const row3 = c.matrix + 12;
    switch (rng() % 11) {
    case 0: std::memcpy(row3, c.matrix + 8, 16); break; // w = z
    case 1: row3[0] = row3[1] = row3[2] = 0.0f, row3[3] = 1.0f; break;
    case 2: row3[3] = 1000.0f; break;  // all in front
    case 3: row3[3] = -1000.0f; break; // all behind
    case 4: // w nearly constant around the plane: the edges' |dw| under or near 1e-5
        for (unsigned k = 0; k < 3; ++k) row3[k] = Draw(rng, 1e-7f);
        row3[3] = 1e-5f + Draw(rng, 2e-6f);
        break;
    case 5: c.matrix[rng() % 16] = Float(Special(rng)); break;
    case 6: // perspective as a camera makes it: w = -z, the box straddling z = 0
        std::memset(row3, 0, 16);
        row3[2] = -1.0f;
        break;
    case 9: // w exactly the near threshold 1e-5 at x = 0 and twice it at x = 1: on the plane counts as behind
        c.box[0] = 0.0f, c.box[3] = 1.0f;
        c.box[1] = Draw(rng, 5.0f), c.box[4] = Draw(rng, 5.0f), c.box[2] = Draw(rng, 5.0f), c.box[5] = Draw(rng, 5.0f);
        row3[0] = 1e-5f, row3[1] = row3[2] = 0.0f, row3[3] = 1e-5f;
        break;
    case 10: // w exactly 0.5e-5 or 1.5e-5 along x: the crossing edges' |dw| is exactly the threshold
        c.box[0] = 0.0f, c.box[3] = 1.0f;
        c.box[1] = Draw(rng, 5.0f), c.box[4] = Draw(rng, 5.0f), c.box[2] = Draw(rng, 5.0f), c.box[5] = Draw(rng, 5.0f);
        row3[0] = 1e-5f, row3[1] = row3[2] = 0.0f, row3[3] = 0.5e-5f;
        break;
    default: break;
    }
    for (uint32_t &w : c.block) w = rng();
    switch (rng() % 4) {
    case 0: c.block[0] &= ~0xffu; break;
    case 1: c.block[0] = (c.block[0] & ~0xffu) | 1u; break;
    case 2: c.block[0] = (c.block[0] & ~0xffu) | 0x80u; break;
    default: break;
    }
}

void CheckProjection(difftest::Context &ctx, const char *what)
{
    using Fn = bool(__thiscall *)(void *, const void *, const float *);
    const Fn original = ctx.Original<Fn>(0xaf25a0);
    static_assert(sizeof(BoxProjection) == sizeof(ProjectCase::block));
    sgfx::CheckSides(ctx, what, 1024, [](uint32_t, drawfx::Cells &) {},
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned call = 0; call < 4; ++call) {
                             ProjectCase c;
                             BuildProjectCase(c, rng);
                             const bool ok = isOriginal ? original(c.block, c.box, c.matrix)
                                                        : reinterpret_cast<BoxProjection *>(c.block)->Project(c.box, c.matrix);
                             drawfx::Note("ok", uint32_t(ok));
                             for (uint32_t w : c.block) drawfx::Note("word", w);
                         }
                     });
}

DIFF_TEST(SG_Utilities_BoxProjection, 0xaf25a0)
{
    CheckProjection(ctx, "BoxProjection");
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    CheckProjection(ctx, "BoxProjection at 24-bit");
}

// ---- the material handle ----

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}

// nGZSceneGraph::cIMaterialManager: 19 a material's index by name, 20 / 21 an index's reference taken / dropped.
constexpr At kMaterialsAt[] = {{19, {1, Ret::Int, true, "FindOrLoad", "s"}},
                               {20, {1, Ret::Int, true, "AddRefIndex", "u"}},
                               {21, {1, Ret::Int, true, "ReleaseIndex", "u"}}};
constexpr auto kMaterialsSlots = Table<22>(kMaterialsAt);
constexpr drawfx::Iface kMaterials{"MaterialManager", 22, kMaterialsSlots.data(), false, drawfx::kTestTable};

constexpr uint32_t kMaterialsCell = 0x10f8fcc; // GetCachedQueriedObject's

struct HandleWorld {
    Fake materials;
    uint32_t cells[3];
    uint32_t found; // FindOrLoad's next answer
} gh;

void NoteCells() { drawfx::Note("cells", {gh.cells[0], gh.cells[1], gh.cells[2]}); }
uint32_t MaterialsFind(Fake &, const uint32_t *)
{
    NoteCells();
    return gh.found;
}
uint32_t MaterialsNote(Fake &, const uint32_t *)
{
    NoteCells();
    return 0;
}

DIFF_TEST(SG_Utilities_ShapeMaterialHandle, 0xae8820)
{
    using Index = void *(__thiscall *)(void *, int32_t);
    using Name = void *(__thiscall *)(void *, const char *);
    using Pair = void *(__thiscall *)(void *, const void *);
    using Self = void(__thiscall *)(void *);
    const Index index = ctx.Original<Index>(0xae8820);
    const Name byName = ctx.Original<Name>(0xae8840);
    const Pair copy = ctx.Original<Pair>(0xae8800), assign = ctx.Original<Pair>(0xae88a0);
    const Self release = ctx.Original<Self>(0xae8a00), destruct = ctx.Original<Self>(0xae8870);
    static const char *const kNames[] = {"", "white", "a_long_material_name_for_the_handle", "#shader"};
    sgfx::CheckSides(
        ctx, "ShapeMaterialHandle", 256,
        [](uint32_t, drawfx::Cells &cells) {
            gh.materials.Reset<kMaterials>(701);
            gh.materials.On(19, &MaterialsFind);
            gh.materials.On(20, &MaterialsNote);
            gh.materials.On(21, &MaterialsNote);
            cells.Set(kMaterialsCell, &gh.materials);
        },
        [&](bool isOriginal, std::mt19937 &rng) {
            for (uint32_t &c : gh.cells) c = rng() % 4 == 0 ? ~0u : rng() % 8;
            for (unsigned step = 0; step < 12; ++step) {
                const unsigned a = rng() % 3, b = rng() % 3, op = rng() % 6;
                void *const self = &gh.cells[a];
                auto *const ours = reinterpret_cast<ShapeMaterialHandle *>(self);
                const auto *const other = reinterpret_cast<const ShapeMaterialHandle *>(&gh.cells[b]);
                drawfx::Note("step", {op, a, b});
                void *result = nullptr;
                switch (op) {
                case 0: {
                    const int32_t i = rng() % 4 == 0 ? -1 : int32_t(rng() % 16);
                    result = isOriginal ? index(self, i) : ours->ConstructIndex(i);
                    break;
                }
                case 1: {
                    const char *const name = kNames[rng() % 4];
                    gh.found = rng() % 4 == 0 ? ~0u : rng() % 16;
                    result = isOriginal ? byName(self, name) : ours->ConstructByName(name);
                    break;
                }
                case 2: result = isOriginal ? copy(self, other) : ours->ConstructCopy(*other); break;
                case 3: result = isOriginal ? assign(self, other) : &ours->Assign(*other); break;
                case 4:
                    if (isOriginal) release(self);
                    else ours->Release();
                    break;
                default:
                    if (isOriginal) destruct(self);
                    else ours->Destruct();
                    break;
                }
                if (op < 4) drawfx::Note("eax", uint32_t(result == self));
                NoteCells();
            }
        });
}

} // namespace
