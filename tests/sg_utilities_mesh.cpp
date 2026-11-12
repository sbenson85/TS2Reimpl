// Differential tests of nGZSceneGraph's free helpers (src/sg/SGUtilities.h), part 5 (lane 261111c): the light bound
// meshes t_SetSceneFlags (0xaeeea0) builds, and the shadow's quad.
//
//   SG_Utilities_MakeConeMesh     0xaef950
//   SG_Utilities_MakeSphereMesh   0xaf05b0
//   SG_Utilities_MakeQuadMesh     0xaf16f0   (centre, size and texture corners: every word an edge, random or NaN float)
//   SG_Utilities_MakeBoxMesh      0xaef5b0   (lane 261111d; the two corners alike: their words are copied as bits)
//   SG_Utilities_MakeBillboardQuadMesh 0xaf1420, SG_Utilities_MakeQuadMeshXZ 0xaf1ad0, SG_Utilities_MakePlainQuadMesh
//   0xaf1eb0 (lane 261111e; NaN words a quarter of the time, the flag's upper bytes junk)
//   SG_Utilities_MakePoolWaterRectMesh 0xaf09e0 (lane 261111n; lattice sizes -2..6, morph counts around the squares,
//   the three flag bytes with junk upper bytes, every mode 0..3 and out of range; the old-style part 0x5ce7e026 has its
//   vbptr at +0 and may fail too; cRZString::Sprintf stood in on both sides; the out cell noted at each
//   GetClassObject, AddPart and Finish)
//
// Fakes: the framework (0x1ae18d stood in on both sides; slot 3 GetClassObject hands out the node, the mesh builder,
// the bounding volume, compactor and tangent space builders by class, the bounding volume and compactor failing (null)
// by a per-seed draw), the geometry node (slot 32 AddPart, its argument the builder's face), and the builders as
// objects with their own face at +0 (the mesh builder's 46 slots, the tangent space builder's slot 0) and a vbptr at +4
// whose entry 1 is their cIGZUnknown fake. Every builder call is logged with what its pointers point at (positions and
// normals as 3 floats, texture coordinates as 2, names as text); after each call the result, *out and every count
// are noted. Radii and lengths come from a set of edge floats (zeros of both signs, infinities, NaNs with payloads,
// denormals) and random ones; segment counts 0-9 and the game's 32.
#include "sgfx.h"

#include "code_patch.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/SGUtilities.h"

#include <array>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <random>
#include <string>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kNodeClass = 0x7ba3838c, kMeshClass = 0x9bffc10d, kBoundsClass = 0x1cfeceb8,
                   kCompactorClass = 0xdcdda078, kTangentClass = 0x5d054225;

constexpr Slot kFwSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {3, Ret::Int, true, "GetClassObject", "uua"}, // 3
};
constexpr drawfx::Iface kFw{"Framework", 4, kFwSlots};

template <unsigned N> using Slots = std::array<Slot, N>;
template <unsigned N> constexpr Slots<N> Unsettled()
{
    Slots<N> r{};
    for (unsigned i = 0; i < N; ++i) r[i] = {0, Ret::Int, false, "", ""};
    return r;
}

// The geometry node: cIGZUnknown's three, then slot 32 AddPart (a builder's cIGeometryBuilder face).
constexpr Slots<33> NodeSlots()
{
    auto r = Unsettled<33>();
    r[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    r[32] = {1, Ret::Int, true, "AddPart", "a"};
    return r;
}
constexpr auto kNodeSlots = NodeSlots();
constexpr drawfx::Iface kNode{"GeometryNode", 33, kNodeSlots.data()};

// cIIndexedMeshBuilder's own face (sg/cIndexedMeshBuilder.h): the slots the two makers call.
constexpr Slots<46> MeshSlots()
{
    auto r = Unsettled<46>();
    r[0] = {1, Ret::Int, true, "Reset", "b"};
    r[1] = {0, Ret::Int, true, "Finish", ""};
    r[9] = {1, Ret::Int, true, "AddPosition", "F3"};
    r[10] = {1, Ret::Int, true, "AddNormal", "F3"};
    r[11] = {2, Ret::Int, true, "AddTexCoord", "F2u"};
    r[17] = {1, Ret::Int, true, "SetMorphTargetCount", "u"};   // 0xa83740 ret 4
    r[18] = {3, Ret::Int, true, "SetMorphNames", "uss"};       // 0xa81a73 ret 0xc
    r[23] = {3, Ret::Int, true, "AddMorphPosition", "uuF3"};   // 0xa7d1e0 ret 0xc
    r[24] = {3, Ret::Int, true, "AddMorphNormal", "uuF3"};     // 0xa7caf0 ret 0xc
    r[27] = {1, Ret::Int, true, "SetGroupName", "s"};
    r[28] = {0, Ret::Int, true, "MarkUnknown", ""};
    r[29] = {0, Ret::Int, true, "CommitGroup", ""};
    r[30] = {3, Ret::Int, true, "AddPositionIndices", "uW4u"}; // MakeBoxMesh's (always 4 corners)
    r[35] = {1, Ret::Int, true, "AddPositionIndex", "u"};
    r[36] = {1, Ret::Int, true, "AddNormalIndex", "u"};
    r[39] = {2, Ret::Int, true, "AddIndex", "bu"};
    r[45] = {0, Ret::Int, true, "CountNormals", ""};
    return r;
}
constexpr auto kMeshSlots = MeshSlots();
constexpr drawfx::Iface kMeshFace{"IndexedMeshBuilder", 46, kMeshSlots.data()};

// cITangentSpaceBuilder's own face: slot 0 AddName.
constexpr Slots<3> TangentSlots()
{
    auto r = Unsettled<3>();
    r[0] = {1, Ret::Int, true, "AddName", "s"};
    return r;
}
constexpr auto kTangentSlots = TangentSlots();
constexpr drawfx::Iface kTangentFace{"TangentSpaceBuilder", 3, kTangentSlots.data()};

// The bounding volume and compactor builders' own faces: never called.
constexpr auto kSilentSlots = Unsettled<1>();
constexpr drawfx::Iface kSilentFace{"Builder", 1, kSilentSlots.data()};

// A builder: its own face at +0, a vbptr at +4 whose entry 1 leads to its cIGZUnknown (the cIGeometryBuilder face).
struct Builder {
    void *vt;
    const int32_t *vbptr;
    int32_t vbtable[2];
};

// The old-style part 0x5ce7e026 (MakePoolWaterRectMesh's fourth): no face of its own, its vbptr at +0 (entry 1 its
// cIGZUnknown fake).
struct VbaseOnly {
    const int32_t *vbptr;
    int32_t vbtable[2];
};

constexpr uint32_t kOldPartClass = 0x5ce7e026;

struct World {
    Fake framework, node;
    Fake meshFace, boundsFace, compactorFace, tangentFace;
    Fake meshUnknown, boundsUnknown, compactorUnknown, tangentUnknown, oldPartUnknown;
    Builder mesh, bounds, compactor, tangent;
    VbaseOnly oldPart;
    bool noBounds, noCompactor, noOldPart;
    void **out; // the caller's out cell (MakePoolWaterRectMesh): noted at each GetClassObject, AddPart and Finish
} gw;

void NoteOutCell(const char *label)
{
    if (gw.out) drawfx::Note(label, drawfx::Norm(drawfx::P(*gw.out)));
}

uint32_t Create(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    if (gw.out) drawfx::Note("cell asked for", drawfx::Norm(drawfx::P(*out))); // each cell cleared before it is asked
    NoteOutCell("out at GetClassObject");
    Fake *counted = nullptr;
    void *made = nullptr;
    switch (a[0]) {
    case kOldPartClass:
        if (!gw.noOldPart) counted = &gw.oldPartUnknown, made = &gw.oldPart;
        break;
    case kNodeClass: counted = &gw.node, made = &gw.node; break;
    case kMeshClass: counted = &gw.meshUnknown, made = &gw.mesh; break;
    case kBoundsClass:
        if (!gw.noBounds) counted = &gw.boundsUnknown, made = &gw.bounds;
        break;
    case kCompactorClass:
        if (!gw.noCompactor) counted = &gw.compactorUnknown, made = &gw.compactor;
        break;
    case kTangentClass: counted = &gw.tangentUnknown, made = &gw.tangent; break;
    }
    if (!made) return 0;
    ++counted->refs;
    *out = made;
    return 1;
}
void *StandFramework() { return &gw.framework; }
uint32_t Quiet(Fake &, const uint32_t *) { return 0; }

void MakeBuilder(Builder &b, Fake &face, Fake &unknown, uint32_t tag)
{
    face.Embed(&b);
    face.On(1, &Quiet);
    face.On(2, &Quiet);
    b.vbptr = b.vbtable;
    b.vbtable[0] = -4;
    b.vbtable[1] = int32_t(drawfx::P(&unknown) - drawfx::P(&b.vbptr));
    drawfx::AddRange(&b, sizeof(b), tag);
}

void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 11u);
    gw.framework.Reset<kFw>(60);
    gw.framework.On(3, &Create);
    gw.node.Reset<kNode>(61);
    gw.meshFace.Reset<kMeshFace>(62);
    gw.boundsFace.Reset<kSilentFace>(63);
    gw.compactorFace.Reset<kSilentFace>(64);
    gw.tangentFace.Reset<kTangentFace>(65);
    gw.meshUnknown.Reset<sgfx::kUnknown>(66);
    gw.boundsUnknown.Reset<sgfx::kUnknown>(67);
    gw.compactorUnknown.Reset<sgfx::kUnknown>(68);
    gw.tangentUnknown.Reset<sgfx::kUnknown>(69);
    MakeBuilder(gw.mesh, gw.meshFace, gw.meshUnknown, 0x7c000000);
    MakeBuilder(gw.bounds, gw.boundsFace, gw.boundsUnknown, 0x7c100000);
    MakeBuilder(gw.compactor, gw.compactorFace, gw.compactorUnknown, 0x7c200000);
    MakeBuilder(gw.tangent, gw.tangentFace, gw.tangentUnknown, 0x7c300000);
    gw.noBounds = rng() % 4 == 0;
    gw.noCompactor = rng() % 4 == 0;
    gw.oldPartUnknown.Reset<sgfx::kUnknown>(70);
    gw.oldPart.vbptr = gw.oldPart.vbtable;
    gw.oldPart.vbtable[0] = 0;
    gw.oldPart.vbtable[1] = int32_t(drawfx::P(&gw.oldPartUnknown) - drawfx::P(&gw.oldPart));
    drawfx::AddRange(&gw.oldPart, sizeof(gw.oldPart), 0x7c400000);
    gw.noOldPart = rng() % 4 == 0;
    gw.out = nullptr;
}

// Edge floats: zeros, ones, infinities, NaN payloads, denormals, the game's sizes; otherwise random ones of either sign.
float Pick(std::mt19937 &rng)
{
    static constexpr uint32_t kEdges[] = {
        0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x3f000000, 0x40000000, 0x7f800000, 0xff800000,
        0x7fc12345, 0xffc00001, 0x7fa00042, 0x00000001, 0x80400000, 0x41f00000, 0x43695555, 0x7f7fffff,
    };
    switch (rng() % 4) {
    case 0: return std::bit_cast<float>(kEdges[rng() % std::size(kEdges)]);
    case 1: return float(int(rng() % 9) - 3) * 0.5f;
    case 2: return float(rng() % 100000) / 64.0f;
    default: return std::bit_cast<float>(rng());
    }
}
uint32_t PickWord(std::mt19937 &rng) { return std::bit_cast<uint32_t>(Pick(rng)); }
uint32_t PickSegments(std::mt19937 &rng) { return rng() % 8 == 0 ? 32 : rng() % 10; }

void NoteState(void *result, void *made, void **out)
{
    drawfx::Note("result is out", uint32_t(result == out));
    drawfx::Note("out", drawfx::Norm(drawfx::P(made)));
    for (const Fake *f :
         {&gw.node, &gw.meshUnknown, &gw.boundsUnknown, &gw.compactorUnknown, &gw.tangentUnknown, &gw.oldPartUnknown})
        drawfx::Note("refs", {f->id, uint32_t(f->refs)});
}

constexpr unsigned kSeeds = 240;

// MakePoolWaterRectMesh's handlers: AddPart and Finish note the caller's out cell where they are called.
uint32_t OutAtAddPart(Fake &, const uint32_t *)
{
    NoteOutCell("out at AddPart");
    return 0;
}
uint32_t OutAtFinish(Fake &, const uint32_t *)
{
    NoteOutCell("out at Finish");
    return 0;
}

// cRZString::Sprintf (ours, verified): the format and the text, logged; the string gets the text.
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    {
        difftest::NotCounted bookkeeping;
        drawfx::Call c;
        c.note = "sprintf";
        c.text = std::string(fmt) + '\x1f' + text + '\x1f';
        drawfx::gLog.push_back(std::move(c));
    }
    self->mString = text;
    return self;
}

// Lattice sizes: 0, 1 and 2 (the divisor edges), negatives, odd and even, a few larger.
int32_t PickCount(std::mt19937 &rng)
{
    static constexpr int32_t kCounts[] = {-2, -1, 0, 1, 2, 3, 4, 5};
    return rng() % 3 == 0 ? int32_t(rng() % 7) : kCounts[rng() % std::size(kCounts)];
}

} // namespace

DIFF_TEST(SG_Utilities_MakeConeMesh, 0xaef950)
{
    using Fn = void **(__cdecl *)(void **, uint32_t, uint32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaef950);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "MakeConeMesh", kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 2; ++k) {
                             const uint32_t radius = PickWord(rng), length = PickWord(rng), segments = PickSegments(rng);
                             // The cap flag is a byte: the high bytes are a register's leftovers.
                             static constexpr uint32_t kCaps[] = {0, 1, 0x100, 0xffffff01, 0x80};
                             const uint32_t cap = kCaps[rng() % std::size(kCaps)];
                             gw.meshFace.Returns(45, rng() % 4 ? rng() % 1000 : 0);
                             drawfx::Note("args", {radius, length, cap, segments});
                             void *made = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                             void **result;
                             if (isOriginal)
                                 result = original(&made, radius, length, cap, segments);
                             else
                                 result = reinterpret_cast<void **>(
                                     MakeConeMesh(reinterpret_cast<cIGeometryNode **>(&made), std::bit_cast<float>(radius),
                                                  std::bit_cast<float>(length), (cap & 0xff) != 0, segments));
                             NoteState(result, made, &made);
                         }
                     });
}

DIFF_TEST(SG_Utilities_MakeSphereMesh, 0xaf05b0)
{
    using Fn = void **(__cdecl *)(void **, uint32_t, uint32_t, const char *, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaf05b0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "MakeSphereMesh", kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 2; ++k) {
                             const uint32_t radius = PickWord(rng), segments = PickSegments(rng);
                             static constexpr const char *kNames[] = {"subset0", "", "Bound name"};
                             const char *const name = kNames[rng() % std::size(kNames)];
                             static constexpr uint32_t kFlags[] = {0, 1, 0x100, 0xffffff01, 0x80};
                             const uint32_t tangents = kFlags[rng() % std::size(kFlags)];
                             drawfx::Note("args", {radius, segments, tangents});
                             void *made = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                             void **result;
                             if (isOriginal)
                                 result = original(&made, radius, segments, name, tangents);
                             else
                                 result = reinterpret_cast<void **>(
                                     MakeSphereMesh(reinterpret_cast<cIGeometryNode **>(&made),
                                                    std::bit_cast<float>(radius), segments, name, (tangents & 0xff) != 0));
                             NoteState(result, made, &made);
                         }
                     });
}

DIFF_TEST(SG_Utilities_MakeQuadMesh, 0xaf16f0)
{
    using Fn = void **(__cdecl *)(void **, const uint32_t *, const uint32_t *, const uint32_t *, const uint32_t *);
    const Fn original = ctx.Original<Fn>(0xaf16f0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "MakeQuadMesh", kSeeds * 2, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 2; ++k) {
                             // The centre, the size and the two texture corners, each word an edge or random float,
                             // or a NaN with a random payload and sign a quarter of the time (so that both operands
                             // of a sum are NaNs often enough to tell their order).
                             const auto pick = [&] { return rng() % 4 ? PickWord(rng) : 0x7f800001u | (rng() & 0x807fffffu); };
                             uint32_t center[3], size[2], low[2], high[2];
                             for (uint32_t &w : center) w = pick();
                             for (uint32_t &w : size) w = pick();
                             for (uint32_t &w : low) w = pick();
                             for (uint32_t &w : high) w = pick();
                             drawfx::Note("centre", {center[0], center[1], center[2]});
                             drawfx::Note("size and texture corners", {size[0], size[1], low[0], low[1], high[0], high[1]});
                             void *made = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                             void **result;
                             if (isOriginal)
                                 result = original(&made, center, size, low, high);
                             else
                                 result = reinterpret_cast<void **>(MakeQuadMesh(
                                     reinterpret_cast<cIGeometryNode **>(&made), reinterpret_cast<const float *>(center),
                                     reinterpret_cast<const float *>(size), reinterpret_cast<const float *>(low),
                                     reinterpret_cast<const float *>(high)));
                             NoteState(result, made, &made);
                         }
                     });
}

DIFF_TEST(SG_Utilities_MakeBoxMesh, 0xaef5b0)
{
    using Fn = void **(__cdecl *)(void **, const uint32_t *, const uint32_t *);
    const Fn original = ctx.Original<Fn>(0xaef5b0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "MakeBoxMesh", kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 2; ++k) {
                             // The corners' words, each an edge or random float or a NaN with a random payload and sign
                             // (copied as bits: a NaN's payload must come through).
                             const auto pick = [&] { return rng() % 4 ? PickWord(rng) : 0x7f800001u | (rng() & 0x807fffffu); };
                             uint32_t low[3], high[3];
                             for (uint32_t &w : low) w = pick();
                             for (uint32_t &w : high) w = pick();
                             drawfx::Note("corners", {low[0], low[1], low[2], high[0], high[1], high[2]});
                             void *made = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                             void **result;
                             if (isOriginal)
                                 result = original(&made, low, high);
                             else
                                 result = reinterpret_cast<void **>(MakeBoxMesh(reinterpret_cast<cIGeometryNode **>(&made),
                                                                                reinterpret_cast<const float *>(low),
                                                                                reinterpret_cast<const float *>(high)));
                             NoteState(result, made, &made);
                         }
                     });
}

// Lane 261111e: the other three "layer" quads.
DIFF_TEST(SG_Utilities_MakeBillboardQuadMesh, 0xaf1420)
{
    using Fn = void **(__cdecl *)(void **, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaf1420);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "MakeBillboardQuadMesh", kSeeds * 2, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 2; ++k) {
                             const auto pick = [&] { return rng() % 4 ? PickWord(rng) : 0x7f800001u | (rng() & 0x807fffffu); };
                             uint32_t w[5];
                             for (uint32_t &word : w) word = pick();
                             drawfx::Note("args", {w[0], w[1], w[2], w[3], w[4]});
                             void *made = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                             void **result;
                             if (isOriginal)
                                 result = original(&made, w[0], w[1], w[2], w[3], w[4]);
                             else
                                 result = reinterpret_cast<void **>(MakeBillboardQuadMesh(
                                     reinterpret_cast<cIGeometryNode **>(&made), std::bit_cast<float>(w[0]),
                                     std::bit_cast<float>(w[1]), std::bit_cast<float>(w[2]), std::bit_cast<float>(w[3]),
                                     std::bit_cast<float>(w[4])));
                             NoteState(result, made, &made);
                         }
                     });
}

DIFF_TEST(SG_Utilities_MakeQuadMeshXZ, 0xaf1ad0)
{
    using Fn = void **(__cdecl *)(void **, const uint32_t *, const uint32_t *, const uint32_t *, const uint32_t *);
    const Fn original = ctx.Original<Fn>(0xaf1ad0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "MakeQuadMeshXZ", kSeeds * 2, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 2; ++k) {
                             const auto pick = [&] { return rng() % 4 ? PickWord(rng) : 0x7f800001u | (rng() & 0x807fffffu); };
                             uint32_t center[3], size[2], low[2], high[2];
                             for (uint32_t &w : center) w = pick();
                             for (uint32_t &w : size) w = pick();
                             for (uint32_t &w : low) w = pick();
                             for (uint32_t &w : high) w = pick();
                             drawfx::Note("centre", {center[0], center[1], center[2]});
                             drawfx::Note("size and texture corners", {size[0], size[1], low[0], low[1], high[0], high[1]});
                             void *made = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                             void **result;
                             if (isOriginal)
                                 result = original(&made, center, size, low, high);
                             else
                                 result = reinterpret_cast<void **>(MakeQuadMeshXZ(
                                     reinterpret_cast<cIGeometryNode **>(&made), reinterpret_cast<const float *>(center),
                                     reinterpret_cast<const float *>(size), reinterpret_cast<const float *>(low),
                                     reinterpret_cast<const float *>(high)));
                             NoteState(result, made, &made);
                         }
                     });
}

DIFF_TEST(SG_Utilities_MakePlainQuadMesh, 0xaf1eb0)
{
    using Fn = void **(__cdecl *)(void **, const uint32_t *, const uint32_t *, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaf1eb0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "MakePlainQuadMesh", kSeeds * 2, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 2; ++k) {
                             const auto pick = [&] { return rng() % 4 ? PickWord(rng) : 0x7f800001u | (rng() & 0x807fffffu); };
                             uint32_t center[3], size[2];
                             for (uint32_t &w : center) w = pick();
                             for (uint32_t &w : size) w = pick();
                             // The flag is a byte: the high bytes are a register's leftovers.
                             static constexpr uint32_t kFlags[] = {0, 1, 0x100, 0xffffff01, 0x80, 0xffffff00};
                             const uint32_t flag = kFlags[rng() % std::size(kFlags)];
                             drawfx::Note("centre", {center[0], center[1], center[2]});
                             drawfx::Note("size and flag", {size[0], size[1], flag});
                             void *made = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                             void **result;
                             if (isOriginal)
                                 result = original(&made, center, size, flag);
                             else
                                 result = reinterpret_cast<void **>(MakePlainQuadMesh(
                                     reinterpret_cast<cIGeometryNode **>(&made), reinterpret_cast<const float *>(center),
                                     reinterpret_cast<const float *>(size), (flag & 0xff) != 0));
                             NoteState(result, made, &made);
                         }
                     });
}

DIFF_TEST(SG_Utilities_MakePoolWaterRectMesh, 0xaf09e0)
{
    using Fn = void **(__cdecl *)(void **, int32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t, int32_t);
    const Fn original = ctx.Original<Fn>(0xaf09e0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    shape::StandIn sprintfStand(0x1ad6f7u, &cRZString::Sprintf, &StandSprintf);
    sgfx::CheckSides(ctx, "MakePoolWaterRectMesh", kSeeds * 2,
                     [](uint32_t seed, drawfx::Cells &) {
                         Build(seed);
                         gw.node.On(32, &OutAtAddPart);
                         gw.meshFace.On(1, &OutAtFinish);
                     },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         const int32_t columns = PickCount(rng), rows = PickCount(rng);
                         // Morph counts around the squares (the lattice side is the ceiling of the root), and none.
                         static constexpr int32_t kMorphs[] = {-2, -1, 0, 1, 2, 3, 4, 5, 8, 9, 10, 15, 16, 17};
                         const int32_t morphs = kMorphs[rng() % std::size(kMorphs)];
                         // The flags are bytes: the high bytes are a register's leftovers; 0x80 and 0xff make the
                         // index kind's byte*2+5 carry past the low byte.
                         static constexpr uint32_t kFlags[] = {0, 1, 0x100, 0xffffff01, 0x80, 0xff, 2};
                         const uint32_t morphNormals = kFlags[rng() % std::size(kFlags)];
                         const uint32_t normalised = kFlags[rng() % std::size(kFlags)];
                         const uint32_t centred = kFlags[rng() % std::size(kFlags)];
                         static constexpr int32_t kModes[] = {0, 1, 2, 3, 4, -1, 0x100, int32_t(0x80000003)};
                         const int32_t mode = kModes[rng() % std::size(kModes)];
                         drawfx::Note("args", {uint32_t(columns), uint32_t(rows), uint32_t(morphs), morphNormals,
                                               normalised, centred, uint32_t(mode)});
                         void *made = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                         gw.out = &made;
                         void **result;
                         if (isOriginal)
                             result = original(&made, columns, rows, morphs, morphNormals, normalised, centred, mode);
                         else
                             result = reinterpret_cast<void **>(MakePoolWaterRectMesh(
                                 reinterpret_cast<cIGeometryNode **>(&made), columns, rows, morphs,
                                 uint8_t(morphNormals & 0xff), uint8_t(normalised & 0xff), uint8_t(centred & 0xff), mode));
                         gw.out = nullptr;
                         NoteState(result, made, &made);
                     });
}
