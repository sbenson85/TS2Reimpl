// Differential tests of nGZSceneGraph's locomotion helpers (src/sg/SGUtilitiesLoco.cpp, lane 261111k).
//
//   SG_UtilitiesLoco_MakePathRibbonMesh     0xae6990
//   SG_UtilitiesLoco_MakeLocomotionRequest  0xae2280  (cLocomotable vf52; its fakes are in namespace req below)
//
// Fakes: the framework (0x1ae18d stood in on both sides; slot 3 GetClassObject hands out the geometry node and the
// indexed mesh builder by class), the node (slot 32 AddPart), the builder (its own face at +0, a vbptr at +4 whose
// entry 1 is its cIGZUnknown fake; slot 44 CountPositions answers the positions added so far plus a per-seed base,
// near 2^32 at times so the corner indices wrap), and the path: slot 16 the knot count, slot 10 a knot's time, slot 18
// the point at t (three words from a per-seed table, NaN-dense, by a running call number), slot 20 the heading at t
// (likewise). Every call is logged with its arguments (points as 3 floats, names as text), so the order of the calls,
// the corners and the knot names ("part" and _itoa: 0, 9, 10 and on) are compared. Knot times step by spans on the
// 0.1 step boundaries (0.1, 0.2, 0.3, just under), zero of both signs, negatives, NaNs with payloads (both operands of
// the span NaN at times), infinities and huge times; knot counts -1, 0, 1, 2 and up to 12.
#include "sgfx.h"

#include "code_patch.h"
#include "rz/StaticGetters.h"
#include "sg/SGUtilities.h"

#include <array>
#include <bit>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kNodeClass = 0x7ba3838c, kMeshClass = 0x9bffc10d;

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

constexpr Slots<46> MeshSlots()
{
    auto r = Unsettled<46>();
    r[0] = {1, Ret::Int, true, "Reset", "b"};
    r[1] = {0, Ret::Int, true, "Finish", ""};
    r[9] = {1, Ret::Int, true, "AddPosition", "F3"};
    r[27] = {1, Ret::Int, true, "SetGroupName", "s"};
    r[28] = {0, Ret::Int, true, "MarkUnknown", ""};
    r[29] = {0, Ret::Int, true, "CommitGroup", ""};
    r[35] = {1, Ret::Int, true, "AddPositionIndex", "u"};
    r[44] = {0, Ret::Int, true, "CountPositions", ""};
    return r;
}
constexpr auto kMeshSlots = MeshSlots();
constexpr drawfx::Iface kMeshFace{"IndexedMeshBuilder", 46, kMeshSlots.data()};

// The path: slots 10 KnotTime(i), 16 KnotCount(), 18 PositionAt(out, t), 20 HeadingAt(t).
constexpr Slots<21> PathSlots()
{
    auto r = Unsettled<21>();
    r[10] = {1, Ret::Float, true, "KnotTime", "u"};
    r[16] = {0, Ret::Int, true, "KnotCount", ""};
    r[18] = {2, Ret::Int, true, "PositionAt", "af"};
    r[20] = {1, Ret::Float, true, "HeadingAt", "f"};
    return r;
}
constexpr auto kPathSlots = PathSlots();
constexpr drawfx::Iface kPath{"Path", 21, kPathSlots.data()};

struct Builder {
    void *vt;
    const int32_t *vbptr;
    int32_t vbtable[2];
};

struct World {
    Fake framework, node, meshFace, meshUnknown, path;
    Builder mesh;
    int32_t count;
    uint32_t times[16];
    uint32_t pointWords[61], headingWords[29];
    uint32_t pointCalls, headingCalls, positions, positionBase, timeCalls;
    bool scripted;
} gw;

uint32_t Create(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    Fake *counted = nullptr;
    void *made = nullptr;
    switch (a[0]) {
    case kNodeClass: counted = &gw.node, made = &gw.node; break;
    case kMeshClass: counted = &gw.meshUnknown, made = &gw.mesh; break;
    }
    if (!made) return 0;
    ++counted->refs;
    *out = made;
    return 1;
}
void *StandFramework() { return &gw.framework; }
uint32_t Quiet(Fake &, const uint32_t *) { return 0; }

// The caller's out cell during a MakePathRibbonMesh call: the node's AddRef notes whether the result is stored yet.
void **gOutCell;
uint32_t NodeAddRef(Fake &self, const uint32_t *)
{
    if (gOutCell) drawfx::Note("out at AddRef", drawfx::Norm(drawfx::P(*gOutCell)));
    return uint32_t(++self.refs);
}

uint32_t AddPosition(Fake &, const uint32_t *) { return ++gw.positions, 0; }
uint32_t CountPositions(Fake &, const uint32_t *) { return gw.positionBase + gw.positions; }

uint32_t KnotCount(Fake &, const uint32_t *) { return uint32_t(gw.count); }
// Knot times by index, or (scripted) by call number: then each segment's start and end are drawn on their own, the
// start often zero so that the span is exactly the drawn one.
uint32_t KnotTime(Fake &, const uint32_t *a)
{
    if (gw.scripted) return gw.times[gw.timeCalls++ % std::size(gw.times)];
    return gw.times[a[0] % std::size(gw.times)];
}
uint32_t PositionAt(Fake &, const uint32_t *a)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (unsigned k = 0; k < 3; ++k) out[k] = gw.pointWords[(gw.pointCalls * 3 + k) % std::size(gw.pointWords)];
    ++gw.pointCalls;
    return 0;
}
uint32_t HeadingAt(Fake &, const uint32_t *)
{
    return gw.headingWords[gw.headingCalls++ % std::size(gw.headingWords)];
}

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

// A NaN with a random payload and sign.
uint32_t NaNWord(std::mt19937 &rng) { return 0x7f800001u | (rng() & 0x807fffffu); }

// Point and heading words: edges, NaNs a third of the time, small random floats.
uint32_t FloatWord(std::mt19937 &rng)
{
    static constexpr uint32_t kEdges[] = {0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000,
                                          0x00000001, 0x80400000, 0x7f7fffff, 0x40490fdb, 0xc0490fdb, 0x3fc90fdb,
                                          0x5f800000, 0x3ca3d70a, 0xbca3d70a};
    switch (rng() % 6) {
    case 0: case 1: return NaNWord(rng);
    case 2: return kEdges[rng() % std::size(kEdges)];
    case 3: return std::bit_cast<uint32_t>(float(int(rng() % 2001) - 1000) / 64.0f);
    default: return std::bit_cast<uint32_t>((float(rng() % 100000) / 50000.0f - 1.0f) * 7.0f);
    }
}

// A knot-to-knot span: on the 0.1 step boundaries and just off them, zeros, negatives, non-finite; at most ~4 long,
// so a segment has at most ~41 steps.
float Span(std::mt19937 &rng)
{
    static constexpr uint32_t kSpans[] = {
        0x3dcccccd, // 0.1
        0x3e4ccccd, // 0.2
        0x3e99999a, // 0.3
        0x3dcccccc, // just under 0.1
        0x3dccccce, // just over 0.1
        0x3d4ccccd, // 0.05
        0x00000000, 0x80000000,
        0xbe19999a, // -0.15
        0xbdcccccd, // -0.1
        0xbf800000, // -1
        0x3f800000, // 1
        0x40233333, // 2.55
        0x40800000, // 4
        0x7f800000, 0xff800000,
        0x00000001,
    };
    switch (rng() % 4) {
    case 0: case 1: return std::bit_cast<float>(kSpans[rng() % std::size(kSpans)]);
    case 2: return float(rng() % 4000) / 1000.0f;
    default: return std::bit_cast<float>(NaNWord(rng));
    }
}

void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 29u);
    gw.framework.Reset<kFw>(60);
    gw.framework.On(3, &Create);
    gw.node.Reset<kNode>(61);
    gw.node.On(1, &NodeAddRef);
    gw.meshFace.Reset<kMeshFace>(62);
    gw.meshUnknown.Reset<sgfx::kUnknown>(66);
    gw.path.Reset<kPath>(70);
    MakeBuilder(gw.mesh, gw.meshFace, gw.meshUnknown, 0x7c000000);
    gw.meshFace.On(9, &AddPosition);
    gw.meshFace.On(44, &CountPositions);
    gw.path.On(10, &KnotTime);
    gw.path.On(16, &KnotCount);
    gw.path.On(18, &PositionAt);
    gw.path.On(20, &HeadingAt);
    for (uint32_t &w : gw.pointWords) w = FloatWord(rng);
    for (uint32_t &w : gw.headingWords) w = FloatWord(rng);
    gw.pointCalls = gw.headingCalls = gw.positions = gw.timeCalls = 0;
    static constexpr uint32_t kBases[] = {0, 0, 1, 7, 0xfffffffd, 0xfffffffe, 0x7ffffffe};
    gw.positionBase = kBases[rng() % std::size(kBases)];
}

void NoteState(void *made)
{
    drawfx::Note("out", drawfx::Norm(drawfx::P(made)));
    for (const Fake *f : {&gw.node, &gw.meshUnknown}) drawfx::Note("refs", {f->id, uint32_t(f->refs)});
    drawfx::Note("calls", {gw.pointCalls, gw.headingCalls, gw.positions});
}

} // namespace

DIFF_TEST(SG_UtilitiesLoco_MakePathRibbonMesh, 0xae6990)
{
    using Fn = void(__cdecl *)(void *, void **);
    const Fn original = ctx.Original<Fn>(0xae6990);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "MakePathRibbonMesh", 400, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         // Knot counts: none, one (no segment), negative, a few, and twelve (knot names past 9 and 10).
                         static constexpr int32_t kCounts[] = {-1, 0, 1, 2, 2, 3, 4, 5, 12, int32_t(0x80000001)};
                         gw.count = kCounts[rng() % std::size(kCounts)];
                         // The first time: zero, an edge, a NaN, a huge one (the steps then add nothing), random.
                         switch (rng() % 5) {
                         case 0: gw.times[0] = 0; break;
                         case 1: gw.times[0] = NaNWord(rng); break;
                         case 2: gw.times[0] = rng() % 2 ? 0x5f000000u : 0xc61c4000u; break; // 2^63, -10000
                         default: gw.times[0] = std::bit_cast<uint32_t>(float(int(rng() % 2000) - 1000) / 8.0f); break;
                         }
                         for (unsigned i = 1; i < std::size(gw.times); ++i)
                             gw.times[i] = rng() % 10 == 0 ? NaNWord(rng)
                                                           : std::bit_cast<uint32_t>(
                                                                 std::bit_cast<float>(gw.times[i - 1]) + Span(rng));
                         // A third of the time the times come by call: start and end drawn per segment, the start
                         // mostly zero and the end a multiple of 0.1 or one ulp either side, where span / 0.1 and
                         // span * 10 truncate differently.
                         gw.scripted = rng() % 3 == 0;
                         if (gw.scripted)
                             for (unsigned i = 0; i < std::size(gw.times); i += 2) {
                                 gw.times[i] = rng() % 4 ? 0u : std::bit_cast<uint32_t>(float(rng() % 8) / 4.0f);
                                 const uint32_t end = std::bit_cast<uint32_t>(
                                     std::bit_cast<float>(gw.times[i]) + float(rng() % 40 + 1) * 0.1f);
                                 gw.times[i + 1] = end + uint32_t(int32_t(rng() % 3) - 1);
                             }
                         drawfx::Note("count", {uint32_t(gw.count), uint32_t(gw.scripted)});
                         drawfx::Note("times", {gw.times[0], gw.times[1], gw.times[2], gw.times[3], gw.times[4]});
                         void *made = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                         gOutCell = &made;
                         if (isOriginal)
                             original(&gw.path, &made);
                         else
                             MakePathRibbonMesh(&gw.path, reinterpret_cast<cIGeometryNode **>(&made));
                         gOutCell = nullptr;
                         NoteState(made);
                     });
}

// ---- MakeLocomotionRequest ----------------------------------------------------------------------------------------

namespace {
namespace req {

constexpr uint32_t kRequestClass = 0x9c1d008c, kShapeClass = 0xfc6eb1f7, kRefNodeClass = 0x65245517;

// The request's own face: setters 0..16.
constexpr Slots<17> RequestSlots()
{
    auto r = Unsettled<17>();
    r[0] = {1, Ret::Int, true, "SetPath", "a"};
    r[1] = {1, Ret::Int, true, "SetA5", "f"};
    r[2] = {1, Ret::Int, true, "SetA6", "f"};
    r[3] = {1, Ret::Int, true, "SetA3", "f"};
    r[5] = {1, Ret::Int, true, "SetA7", "f"};
    r[7] = {1, Ret::Int, true, "SetA8", "a"};
    r[8] = {1, Ret::Int, true, "SetA13", "f"};
    r[9] = {1, Ret::Int, true, "SetA14", "f"};
    r[10] = {1, Ret::Int, true, "SetA15", "f"};
    r[13] = {1, Ret::Int, true, "SetA16", "f"};
    r[14] = {2, Ret::Int, true, "SetCurve", "uf"};
    r[15] = {1, Ret::Int, true, "SetA27", "u"};
    r[16] = {1, Ret::Int, true, "SetDisplay", "a"};
    return r;
}
constexpr auto kRequestSlots = RequestSlots();
constexpr drawfx::Iface kRequestFace{"LocoRequest", 17, kRequestSlots.data()};

// Its settings (virtual base 2): setters 10..26 and 51, the transform.
constexpr Slots<52> SettingsSlots()
{
    auto r = Unsettled<52>();
    r[10] = {1, Ret::Int, true, "SetA10", "u"};
    r[11] = {1, Ret::Int, true, "SetA11", "f"};
    r[12] = {1, Ret::Int, true, "SetA12", "f"};
    r[13] = {1, Ret::Int, true, "SetA17", "u"};
    r[14] = {1, Ret::Int, true, "SetA18", "u"};
    r[15] = {1, Ret::Int, true, "SetA19", "f"};
    r[16] = {1, Ret::Int, true, "SetA20", "a"};
    r[17] = {1, Ret::Int, true, "SetA22", "u"};
    r[18] = {1, Ret::Int, true, "SetA23", "u"};
    r[19] = {1, Ret::Int, true, "SetA24", "f"};
    r[21] = {2, Ret::Int, true, "SetA25", "au"};
    r[24] = {1, Ret::Int, true, "SetA28", "u"};
    r[25] = {1, Ret::Int, true, "SetA30", "u"};
    r[26] = {1, Ret::Int, true, "SetBlend", "f"};
    r[51] = {0, Ret::Int, true, "Transform", ""};
    return r;
}
constexpr auto kSettingsSlots = SettingsSlots();
constexpr drawfx::Iface kSettings{"LocoSettings", 52, kSettingsSlots.data()};

// The node (a2): slot 10 gets the settings.
constexpr Slots<11> NodeFaceSlots()
{
    auto r = Unsettled<11>();
    r[10] = {1, Ret::Int, true, "SetSettings", "a"};
    return r;
}
constexpr auto kNodeFaceSlots = NodeFaceSlots();
constexpr drawfx::Iface kNodeFace{"LocoNode", 11, kNodeFaceSlots.data()};

// The node's cIExtension (virtual base 1): slot 18 GetOwner.
constexpr Slots<19> ExtensionSlots()
{
    auto r = Unsettled<19>();
    r[18] = {0, Ret::Int, true, "GetOwner", ""};
    return r;
}
constexpr auto kExtensionSlots = ExtensionSlots();
constexpr drawfx::Iface kExtension{"Extension", 19, kExtensionSlots.data()};

// The owner, the scene (a cITransformNode: 34 GetParent, 45 AddChild) and its parent.
constexpr Slots<46> SceneSlots()
{
    auto r = Unsettled<46>();
    r[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    r[34] = {0, Ret::Int, true, "GetParent", ""};
    r[45] = {1, Ret::Int, true, "AddChild", "a"};
    return r;
}
constexpr auto kSceneSlots = SceneSlots();
constexpr drawfx::Iface kScene{"TransformNode", 46, kSceneSlots.data()};

// The shape-ref nodes: 45 AddChild, 63 SetTransform (7 floats), 64 SetTranslation, 65 SetRotation, 94 AddShape.
constexpr Slots<95> RefNodeSlots()
{
    auto r = Unsettled<95>();
    r[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    r[45] = {1, Ret::Int, true, "AddChild", "a"};
    r[63] = {1, Ret::Int, true, "SetTransform", "F7"};
    r[64] = {1, Ret::Int, true, "SetTranslation", "F3"};
    r[65] = {1, Ret::Int, true, "SetRotation", "F4"};
    r[94] = {1, Ret::Int, true, "AddShape", "a"};
    return r;
}
constexpr auto kRefNodeSlots = RefNodeSlots();
constexpr drawfx::Iface kRefNode{"ShapeRefNode", 95, kRefNodeSlots.data()};

// The shapes: 34 BindMaterial(material, subset), 41 AddPart(object, lod).
constexpr Slots<42> ShapeSlots()
{
    auto r = Unsettled<42>();
    r[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    r[34] = {2, Ret::Int, true, "BindMaterial", "ss"};
    r[41] = {2, Ret::Int, true, "AddPart", "au"};
    return r;
}
constexpr auto kShapeSlots = ShapeSlots();
constexpr drawfx::Iface kShape{"Shape", 42, kShapeSlots.data()};

// The paths: 9 PointAt(out, knot) -> a point, 10 KnotTime, 16 KnotCount, 20 HeadingAt, 22 Length; a8 and a20: 19.
constexpr Slots<23> LocoPathSlots()
{
    auto r = Unsettled<23>();
    r[9] = {2, Ret::Int, true, "PointAt", "au"};
    r[10] = {1, Ret::Float, true, "KnotTime", "u"};
    r[16] = {0, Ret::Int, true, "KnotCount", ""};
    r[19] = {1, Ret::Float, true, "ValueAt", "f"};
    r[20] = {1, Ret::Float, true, "HeadingAt", "f"};
    r[22] = {0, Ret::Float, true, "Length", ""};
    return r;
}
constexpr auto kLocoPathSlots = LocoPathSlots();
constexpr drawfx::Iface kLocoPath{"LocoPath", 23, kLocoPathSlots.data()};

struct Embedded {
    void *vt;
    const int32_t *vbptr;
    int32_t vbtable[3];
};

struct World {
    Fake framework, requestFace, requestUnknown, settings, nodeFace, extension, owner, scene, parent, a25;
    Fake paths[3], timed[2];
    Fake refNodes[7], shapes[9], ribbons[3], boxes[6];
    Embedded request, node;
    unsigned refNodesMade, shapesMade, ribbonsMade, boxesMade;
    int32_t counts[3];
    uint32_t knotTimes[7], headings[11], pointWords[17], lengthWord, timedWords[2], transform[7];
    uint32_t ownerMode, queryMode, parentMode, pointMode, ribbonNull, boxNull;
    uint32_t pointCalls, headingCalls, timeCalls;
} gr;

void Link(Embedded &e, Fake &face, Fake *base1, Fake *base2, uint32_t tag)
{
    face.Embed(&e);
    e.vbptr = e.vbtable;
    e.vbtable[0] = -4;
    e.vbtable[1] = base1 ? int32_t(drawfx::P(base1) - drawfx::P(&e.vbptr)) : 0;
    e.vbtable[2] = base2 ? int32_t(drawfx::P(base2) - drawfx::P(&e.vbptr)) : 0;
    drawfx::AddRange(&e, sizeof(e), tag);
}

uint32_t Create(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    void *made = nullptr;
    Fake *counted = nullptr;
    switch (a[0]) {
    case kRequestClass: made = &gr.request, counted = &gr.requestUnknown; break;
    case kRefNodeClass:
        if (gr.refNodesMade < std::size(gr.refNodes)) made = counted = &gr.refNodes[gr.refNodesMade++];
        break;
    case kShapeClass:
        if (gr.shapesMade < std::size(gr.shapes)) made = counted = &gr.shapes[gr.shapesMade++];
        break;
    }
    drawfx::Note("create", {a[0], a[1], counted ? counted->id : 0});
    if (!made) return 0;
    ++counted->refs;
    *out = made;
    return 1;
}
void *StandFramework() { return &gr.framework; }

uint32_t GetOwner(Fake &, const uint32_t *) { return gr.ownerMode ? uint32_t(uintptr_t(&gr.owner)) : 0; }
// The owner's QueryInterface: no, yes, yes but false, or the reference stored with 0x100 (al clear) returned.
uint32_t OwnerQuery(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    if (gr.queryMode == 0) return 0;
    ++gr.scene.refs;
    *out = &gr.scene;
    return gr.queryMode == 1 ? 1u : gr.queryMode == 2 ? 0u : 0x100u;
}
uint32_t GetParent(Fake &, const uint32_t *) { return gr.parentMode ? uint32_t(uintptr_t(&gr.parent)) : 0; }
uint32_t Transform(Fake &, const uint32_t *) { return uint32_t(uintptr_t(gr.transform)); }

int PathIndex(const Fake &self) { return int(&self - gr.paths); }
uint32_t KnotCount(Fake &self, const uint32_t *) { return uint32_t(gr.counts[PathIndex(self)]); }
uint32_t KnotTime(Fake &, const uint32_t *) { return gr.knotTimes[gr.timeCalls++ % std::size(gr.knotTimes)]; }
uint32_t HeadingAt(Fake &, const uint32_t *) { return gr.headings[gr.headingCalls++ % std::size(gr.headings)]; }
uint32_t Length(Fake &, const uint32_t *) { return gr.lengthWord; }
uint32_t TimedValue(Fake &self, const uint32_t *) { return gr.timedWords[&self - gr.timed]; }
// PointAt: fills the caller's buffer, and returns it or (pointMode) a point of its own, so that the translation is the
// one returned.
uint32_t PointAt(Fake &, const uint32_t *a)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    static uint32_t own[3];
    const unsigned at = gr.pointCalls++ * 3;
    for (unsigned k = 0; k < 3; ++k) {
        out[k] = gr.pointWords[(at + k) % std::size(gr.pointWords)];
        own[k] = gr.pointWords[(at + k + 5) % std::size(gr.pointWords)];
    }
    return gr.pointMode ? uint32_t(uintptr_t(own)) : uint32_t(uintptr_t(out));
}

// The stand-ins of the ribbon, the box mesh and the axis-angle quaternion (all ours, verified on their own): each notes
// its arguments, and the ribbon and the box hand out a fake (or null) with one reference.
void StandRibbon(void *path, cIGeometryNode **out)
{
    drawfx::Note("ribbon", drawfx::Norm(drawfx::P(path)));
    Fake *made = nullptr;
    if (!((gr.ribbonNull >> gr.ribbonsMade) & 1) && gr.ribbonsMade < std::size(gr.ribbons))
        made = &gr.ribbons[gr.ribbonsMade];
    ++gr.ribbonsMade;
    if (made) ++made->refs;
    *out = reinterpret_cast<cIGeometryNode *>(made);
}
cIGeometryNode **StandBox(cIGeometryNode **out, const float *low, const float *high)
{
    const auto *l = reinterpret_cast<const uint32_t *>(low), *h = reinterpret_cast<const uint32_t *>(high);
    drawfx::Note("box", {l[0], l[1], l[2], h[0], h[1], h[2]});
    Fake *made = nullptr;
    if (!((gr.boxNull >> gr.boxesMade) & 1) && gr.boxesMade < std::size(gr.boxes)) made = &gr.boxes[gr.boxesMade];
    ++gr.boxesMade;
    if (made) ++made->refs;
    *out = reinterpret_cast<cIGeometryNode *>(made);
    return out;
}
float *StandQuat(float *out, const float *axis, int32_t stride, float angle)
{
    const auto *x = reinterpret_cast<const uint32_t *>(axis);
    const uint32_t a = std::bit_cast<uint32_t>(angle);
    drawfx::Note("quat", {x[0], x[stride], x[2 * stride], uint32_t(stride), a});
    for (unsigned k = 0; k < 4; ++k) reinterpret_cast<uint32_t *>(out)[k] = a ^ (k * 0x01010101u);
    return out;
}

// Float words for the setters and the fakes' answers: NaN-dense, edges, small values.
uint32_t Word(std::mt19937 &rng) { return FloatWord(rng); }

void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 2246822519u + 7u);
    gr.framework.Reset<kFw>(80);
    gr.framework.On(3, &Create);
    gr.requestFace.Reset<kRequestFace>(81);
    gr.requestUnknown.Reset<sgfx::kUnknown>(82);
    gr.settings.Reset<kSettings>(83);
    gr.settings.On(51, &Transform);
    Link(gr.request, gr.requestFace, &gr.requestUnknown, &gr.settings, 0x7d000000);
    gr.nodeFace.Reset<kNodeFace>(84);
    gr.extension.Reset<kExtension>(85);
    gr.extension.On(18, &GetOwner);
    Link(gr.node, gr.nodeFace, &gr.extension, nullptr, 0x7e000000);
    gr.owner.Reset<kScene>(86);
    gr.owner.On(0, &OwnerQuery);
    gr.scene.Reset<kScene>(87);
    gr.scene.On(34, &GetParent);
    gr.parent.Reset<kScene>(88);
    gr.a25.Reset<sgfx::kUnknown>(89);
    for (unsigned i = 0; i < 3; ++i) {
        gr.paths[i].Reset<kLocoPath>(71 + i);
        gr.paths[i].On(9, &PointAt);
        gr.paths[i].On(10, &KnotTime);
        gr.paths[i].On(16, &KnotCount);
        gr.paths[i].On(20, &HeadingAt);
        gr.paths[i].On(22, &Length);
    }
    for (unsigned i = 0; i < 2; ++i) {
        gr.timed[i].Reset<kLocoPath>(74 + i);
        gr.timed[i].On(19, &TimedValue);
    }
    for (unsigned i = 0; i < std::size(gr.refNodes); ++i) gr.refNodes[i].Reset<kRefNode>(100 + i);
    for (unsigned i = 0; i < std::size(gr.shapes); ++i) gr.shapes[i].Reset<kShape>(110 + i);
    for (unsigned i = 0; i < std::size(gr.ribbons); ++i) gr.ribbons[i].Reset<sgfx::kUnknown>(120 + i);
    for (unsigned i = 0; i < std::size(gr.boxes); ++i) gr.boxes[i].Reset<sgfx::kUnknown>(130 + i);
    gr.refNodesMade = gr.shapesMade = gr.ribbonsMade = gr.boxesMade = 0;
    gr.pointCalls = gr.headingCalls = gr.timeCalls = 0;
    for (uint32_t &w : gr.knotTimes) w = Word(rng);
    for (uint32_t &w : gr.headings) w = Word(rng);
    for (uint32_t &w : gr.pointWords) w = Word(rng);
    for (uint32_t &w : gr.transform) w = Word(rng);
    for (uint32_t &w : gr.timedWords) w = Word(rng);
    gr.lengthWord = Word(rng);
}

void NoteState()
{
    for (const Fake *f : {&gr.requestUnknown, &gr.scene, &gr.owner, &gr.parent}) drawfx::Note("refs", {f->id, uint32_t(f->refs)});
    for (const Fake &f : gr.refNodes) drawfx::Note("refs", {f.id, uint32_t(f.refs)});
    for (const Fake &f : gr.shapes) drawfx::Note("refs", {f.id, uint32_t(f.refs)});
    for (const Fake &f : gr.ribbons) drawfx::Note("refs", {f.id, uint32_t(f.refs)});
    for (const Fake &f : gr.boxes) drawfx::Note("refs", {f.id, uint32_t(f.refs)});
    drawfx::Note("made", {gr.refNodesMade, gr.shapesMade, gr.ribbonsMade, gr.boxesMade, gr.pointCalls,
                          gr.headingCalls, gr.timeCalls});
}

} // namespace req
} // namespace

DIFF_TEST(SG_UtilitiesLoco_MakeLocomotionRequest, 0xae2280)
{
    using W = uint32_t;
    using Fn = uint32_t(__cdecl *)(W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
                                   W, W, W, W);
    const Fn original = ctx.Original<Fn>(0xae2280);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &req::StandFramework};
    shape::StandIn ribbon{0xae6990, &MakePathRibbonMesh, &req::StandRibbon};
    shape::StandIn box{0xaef5b0, &MakeBoxMesh, &req::StandBox};
    shape::StandIn quat{0xc63510, &QuatFromAxisAngle, &req::StandQuat};
    sgfx::CheckSides(
        ctx, "MakeLocomotionRequest", 400, [](uint32_t seed, drawfx::Cells &) { req::Build(seed); },
        [&](bool isOriginal, std::mt19937 &rng) {
            using namespace req;
            // The display: no node, no flag, no owner, the owner's answers, no parent, or all of it.
            const unsigned mode = rng() % 8;
            const bool hasNode = mode != 0;
            const uint32_t display = mode == 1 ? (rng() % 2 ? 0x100u : 0u) : (rng() % 2 ? 1u : 0xffffff01u);
            gr.ownerMode = mode == 2 ? 0 : 1;
            gr.queryMode = mode == 3 ? rng() % 4 : 1;
            gr.parentMode = mode == 4 ? 0 : 1;
            gr.pointMode = rng() % 2;
            gr.ribbonNull = rng() % 3 == 0 ? rng() & 7 : 0;
            gr.boxNull = rng() % 3 == 0 ? rng() & 0x3f : 0;
            // Knot counts: the first path's waypoints (at most 6), the reference paths' parts (names past 9 and 10).
            static constexpr int32_t kFirst[] = {-1, 0, 1, 2, 3, 6, int32_t(0x80000001)};
            static constexpr int32_t kOthers[] = {-1, 0, 1, 2, 12};
            gr.counts[0] = kFirst[rng() % std::size(kFirst)];
            gr.counts[1] = kOthers[rng() % std::size(kOthers)];
            gr.counts[2] = kOthers[rng() % std::size(kOthers)];
            void *const ref1 = rng() % 3 ? &gr.paths[1] : rng() % 2 ? &gr.paths[0] : nullptr;
            void *const ref2 = rng() % 3 ? &gr.paths[2] : nullptr;
            // The times: a9 0x80000100 (no time) or about it, a10, and their difference wrapping.
            static constexpr uint32_t kTimes[] = {0x80000100, 0x80000100, 0x800000ff, 0x80000101, 0, 1, 10,
                                                  0x7fffffff, 0x80000000, 0xffffffff, 1000};
            const uint32_t a9 = rng() % 4 ? kTimes[rng() % std::size(kTimes)] : rng();
            const uint32_t a10 = rng() % 3 ? kTimes[rng() % std::size(kTimes)] : rng();
            void *const a8 = rng() % 4 ? &gr.timed[0] : nullptr;
            void *const a20 = rng() % 4 ? &gr.timed[1] : nullptr;
            // a24: below zero or not (a NaN is not).
            static constexpr uint32_t kSigns[] = {0x80000000, 0, 0xbf800000, 0x3f800000, 0x80000001, 0xff800000,
                                                  0xffc00000, 0x7fc00001};
            const uint32_t a24 = rng() % 2 ? kSigns[rng() % std::size(kSigns)] : Word(rng);
            // a11: the blend weight (1 - a11 when a24 < 0).
            static constexpr uint32_t kWeights[] = {0x3f800000, 0, 0x3f000000, 0x7f800000, 0x7fc00000, 0xff812345};
            const uint32_t a11 = rng() % 2 ? kWeights[rng() % std::size(kWeights)] : Word(rng);
            static uint32_t curve[20];
            for (uint32_t &w : curve) w = Word(rng);
            const uint32_t *const a21 = rng() % 3 ? curve : nullptr;
            void *const a25 = rng() % 2 ? &gr.a25 : nullptr;
            W w[33] = {};
            for (unsigned i = 1; i <= 32; ++i) w[i] = Word(rng);
            void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
            w[1] = W(uintptr_t(&out));
            w[2] = hasNode ? W(uintptr_t(&gr.node)) : 0;
            w[4] = W(uintptr_t(&gr.paths[0]));
            w[8] = W(uintptr_t(a8));
            w[9] = a9;
            w[10] = a10;
            w[11] = a11;
            w[17] = rng();
            w[18] = rng();
            w[20] = W(uintptr_t(a20));
            w[21] = W(uintptr_t(a21));
            w[22] = rng();
            w[23] = rng();
            w[24] = a24;
            w[25] = W(uintptr_t(a25));
            w[26] = rng();
            w[27] = rng();
            w[28] = rng();
            w[29] = display;
            w[30] = rng();
            w[31] = W(uintptr_t(ref1));
            w[32] = W(uintptr_t(ref2));
            drawfx::Note("args", {mode, gr.counts[0] < 0 ? 0u : W(gr.counts[0]), W(gr.counts[1]), W(gr.counts[2]), a9,
                                  a10, a11, a24});
            uint32_t result;
            if (isOriginal) {
                result = original(w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], w[12], w[13],
                                  w[14], w[15], w[16], w[17], w[18], w[19], w[20], w[21], w[22], w[23], w[24], w[25],
                                  w[26], w[27], w[28], w[29], w[30], w[31], w[32]);
            } else {
                auto f = [&](unsigned i) { return std::bit_cast<float>(w[i]); };
                auto p = [&](unsigned i) { return reinterpret_cast<void *>(uintptr_t(w[i])); };
                result = MakeLocomotionRequest(reinterpret_cast<void **>(p(1)), p(2), f(3), p(4), f(5), f(6), f(7),
                                               p(8), int32_t(w[9]), int32_t(w[10]), f(11), f(12), f(13), f(14), f(15),
                                               f(16), w[17], w[18], f(19), p(20),
                                               reinterpret_cast<const float *>(p(21)), w[22], w[23], f(24), p(25),
                                               w[26], w[27], w[28], w[29], w[30], p(31), p(32));
            }
            drawfx::Note("result", result & 0xff);
            drawfx::Note("out", drawfx::Norm(drawfx::P(out)));
            NoteState();
        });
}
