// Differential test of nGZSceneGraph's free helpers (src/sg/SGUtilities.h), part 6 (lane 261111c): the light bounds
// rebuild t_SetSceneFlags.
//
//   SG_Utilities_RebuildLightBounds   0xaeeea0
//
// Stood in on both sides: the framework getter 0x1ae18d, the tag helpers 0xae7900 / 0xae78f0 (ours, verified; the
// stands answer each node's drawn tag and note the tag set) and the two mesh makers 0xaf05b0 / 0xaef950 (ours, verified;
// the stands note their arguments and hand out a counted mesh fake, or null). Fakes: the scene (slot 44 fills a range
// of three iterator fakes for the filter it is given, whose two words are noted), the iterators (slot 4 the cursor at
// the end, 5 the node, 6 the step), the shape-ref nodes the first walk finds (slot 34 their parent, whose slot 46 is
// noted), the light-ref nodes the second walk finds (slot 10 writes the drawn range / cone angles into the event, whose
// vtable is noted; slot 95 the light or null; slot 45 noted), the lights (slot 36 the drawn type), and the shape-ref
// nodes and shapes the framework's slot 3 makes (slot 94 counted into slot 98's answer, which starts at a drawn word
// including negative ones; slots 34 / 41 noted). Ranges and angles are edge floats (zeros of both signs, infinities,
// NaN payloads, denormals) and random ones; flags 0, 4, 8, 0xc and others. After each call every count is noted.
#include "sgfx.h"

#include "code_patch.h"
#include "game/game.h"
#include "rz/StaticGetters.h"
#include "sg/SGUtilities.h"

#include <array>
#include <bit>
#include <cstring>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kShapeRefClass = 0x65245517, kShapeRefIid = 0xa516cd10, kShapeClass = 0xfc6eb1f7,
                   kShapeIid = 0x3c6eb1f0;
constexpr uint32_t kRange = 0x0a5622e9, kAngle = 0x29de5328, kAngle2 = 0x49de5331;

template <unsigned N> using Slots = std::array<Slot, N>;
template <unsigned N> constexpr Slots<N> Unsettled()
{
    Slots<N> r{};
    for (unsigned i = 0; i < N; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    return r;
}

constexpr Slot kFwSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {3, Ret::Int, true, "GetClassObject", "uua"}, // 3
};
constexpr drawfx::Iface kFw{"Framework", 4, kFwSlots};

// The scene: slot 44 (out, a, filter).
constexpr Slots<45> SceneSlots()
{
    auto r = Unsettled<45>();
    r[44] = {3, Ret::Int, true, "MakeIterator44", "aua"};
    return r;
}
constexpr auto kSceneSlots = SceneSlots();
constexpr drawfx::Iface kScene{"Scene", 45, kSceneSlots.data()};

constexpr Slot kIterSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {0, Ret::Int, true, "Clone", ""},           {1, Ret::Int, true, "IsEqual", "a"}, {0, Ret::Int, true, "Get", ""},
    {0, Ret::Int, true, "Next", ""},
};
constexpr drawfx::Iface kIter{"NodeIterator", 7, kIterSlots};

// A tree node: slot 34 GetParent, 46 RemoveChild (the shape-ref nodes the first walk finds and their parent).
constexpr Slots<47> TreeSlots()
{
    auto r = Unsettled<47>();
    r[34] = {0, Ret::Int, true, "GetParent", ""};
    r[46] = {1, Ret::Int, true, "RemoveChild", "a"};
    return r;
}
constexpr auto kTreeSlots = TreeSlots();
constexpr drawfx::Iface kTree{"TreeNode", 47, kTreeSlots.data()};

// A light-ref node: slot 10 GetAttribute (event, id, 0), 45 AddChild, 95 GetLight.
constexpr Slots<96> LightNodeSlots()
{
    auto r = Unsettled<96>();
    r[10] = {3, Ret::Int, true, "GetAttribute", "auu"};
    r[45] = {1, Ret::Int, true, "AddChild", "a"};
    r[95] = {0, Ret::Int, true, "GetLight", ""};
    return r;
}
constexpr auto kLightNodeSlots = LightNodeSlots();
constexpr drawfx::Iface kLightNode{"LightRefNode", 96, kLightNodeSlots.data()};

constexpr Slots<37> LightSlots()
{
    auto r = Unsettled<37>();
    r[36] = {0, Ret::Int, true, "GetLightType", ""};
    return r;
}
constexpr auto kLightSlots = LightSlots();
constexpr drawfx::Iface kLight{"Light", 37, kLightSlots.data()};

// A shape-ref node the rebuild makes: slot 94 AddShape, 98 ShapeCount.
constexpr Slots<99> HolderSlots()
{
    auto r = Unsettled<99>();
    r[94] = {1, Ret::Int, true, "AddShape", "a"};
    r[98] = {0, Ret::Int, true, "ShapeCount", ""};
    return r;
}
constexpr auto kHolderSlots = HolderSlots();
constexpr drawfx::Iface kHolder{"ShapeRefNode", 99, kHolderSlots.data()};

// A shape: slot 34 BindMaterial (material, subset), 41 AddPart (object, lod).
constexpr Slots<42> ShapeSlots()
{
    auto r = Unsettled<42>();
    r[34] = {2, Ret::Int, true, "BindMaterial", "ss"};
    r[41] = {2, Ret::Int, true, "AddPart", "au"};
    return r;
}
constexpr auto kShapeSlots = ShapeSlots();
constexpr drawfx::Iface kShape{"Shape", 42, kShapeSlots.data()};

constexpr unsigned kTreeNodes = 5, kLights = 8, kMade = 18;

struct World {
    Fake framework, scene, parent;
    Fake cursors[2], begins[2], ends[2];
    Fake tree[kTreeNodes], lightNodes[kLights], lights[kLights];
    Fake holders[kMade], shapes[kMade], meshes[kMade];
    const char *tags[kTreeNodes];
    unsigned treeCount, lightCount;
    unsigned holdersMade, shapesMade, meshesMade;
    uint32_t holderCounts[kMade];
    bool meshFails[kMade];
} gw;

// The cursors: value[0] the position, value[1] which list (0 the shape-ref nodes, 1 the light nodes).
uint32_t IterAtEnd(Fake &self, const uint32_t *)
{
    return self.value[0] >= (self.value[1] ? gw.lightCount : gw.treeCount);
}
uint32_t IterGet(Fake &self, const uint32_t *)
{
    const unsigned i = self.value[0];
    if (self.value[1]) return i < gw.lightCount ? drawfx::P(&gw.lightNodes[i]) : 0;
    return i < gw.treeCount ? drawfx::P(&gw.tree[i]) : 0;
}
uint32_t IterNext(Fake &self, const uint32_t *) { return ++self.value[0], 0; }

uint32_t SceneRange(Fake &, const uint32_t *a)
{
    auto *const out = reinterpret_cast<Fake **>(uintptr_t(a[0]));
    const auto *const filter = reinterpret_cast<const uint32_t *>(uintptr_t(a[2]));
    drawfx::Note("filter", {uint32_t(filter[0] == drawfx::P(GameAddress<const void>(0xe7cf54))), filter[1]});
    const unsigned which = filter[1] == kShapeRefIid ? 0 : 1;
    Fake &cursor = gw.cursors[which];
    cursor.value[0] = 0;
    cursor.value[1] = which;
    out[0] = &cursor, out[1] = &gw.begins[which], out[2] = &gw.ends[which];
    ++cursor.refs, ++gw.begins[which].refs, ++gw.ends[which].refs;
    return drawfx::P(out);
}

// The light node's attribute: value[0..2] the range and the two cone angles.
uint32_t LightAttribute(Fake &self, const uint32_t *a)
{
    auto *const event = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    drawfx::Note("event vtable", uint32_t(event[0] == drawfx::P(GameAddress<const void>(0xdb07f4))));
    switch (a[1]) {
    case kRange: event[1] = self.value[0]; break;
    case kAngle: event[1] = self.value[1]; break;
    case kAngle2: event[1] = self.value[2]; break;
    }
    return 0;
}

uint32_t HolderAddShape(Fake &self, const uint32_t *) { return ++self.ret[98], 0; }

uint32_t Create(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    Fake *made = nullptr;
    if (a[0] == kShapeRefClass && a[1] == kShapeRefIid && gw.holdersMade < kMade) made = &gw.holders[gw.holdersMade++];
    if (a[0] == kShapeClass && a[1] == kShapeIid && gw.shapesMade < kMade) made = &gw.shapes[gw.shapesMade++];
    if (!made) return 0;
    ++made->refs;
    *out = made;
    return 1;
}
void *StandFramework() { return &gw.framework; }

// The tag helpers (ours, verified) stood in: each tree node answers its drawn tag.
const char *StandGetTag(cICompositionTreeNode *node)
{
    for (unsigned i = 0; i < kTreeNodes; ++i)
        if (node == reinterpret_cast<cICompositionTreeNode *>(&gw.tree[i])) {
            drawfx::Note("get tag", gw.tree[i].id);
            return gw.tags[i];
        }
    drawfx::Note("get tag of an unknown node", drawfx::Norm(drawfx::P(node)));
    return nullptr;
}
void StandSetTag(cICompositionTreeNode *node, const char *tag)
{
    drawfx::Note("set tag", drawfx::Norm(drawfx::P(node)));
    sgfx::NoteText("tag", tag);
}

// The mesh makers (ours, verified) stood in: the arguments noted, a counted mesh fake (or null) handed out.
cIGeometryNode **HandOutMesh(cIGeometryNode **out)
{
    Fake *mesh = nullptr;
    if (gw.meshesMade < kMade) {
        const unsigned k = gw.meshesMade++;
        if (!gw.meshFails[k]) mesh = &gw.meshes[k], ++mesh->refs;
    }
    *out = reinterpret_cast<cIGeometryNode *>(mesh);
    return out;
}
cIGeometryNode **StandSphere(cIGeometryNode **out, float radius, uint32_t segments, const char *name, bool tangents)
{
    drawfx::Note("sphere", {std::bit_cast<uint32_t>(radius), segments, uint32_t(tangents)});
    sgfx::NoteText("sphere name", name);
    return HandOutMesh(out);
}
cIGeometryNode **StandCone(cIGeometryNode **out, float radius, float length, bool cap, uint32_t segments)
{
    drawfx::Note("cone", {std::bit_cast<uint32_t>(radius), std::bit_cast<uint32_t>(length), uint32_t(cap), segments});
    return HandOutMesh(out);
}

// Edge floats: zeros, ones, infinities, NaN payloads, denormals, angles; otherwise random ones of either sign.
float Pick(std::mt19937 &rng)
{
    static constexpr uint32_t kEdges[] = {
        0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x3f000000, 0x40000000, 0x7f800000, 0xff800000,
        0x7fc12345, 0xffc00001, 0x7fa00042, 0x00000001, 0x80400000, 0x41f00000, 0x3fc90fdb, 0x7f7fffff,
    };
    switch (rng() % 4) {
    case 0: return std::bit_cast<float>(kEdges[rng() % std::size(kEdges)]);
    case 1: return float(int(rng() % 9) - 3) * 0.25f;
    case 2: return float(rng() % 100000) / 4096.0f;
    default: return std::bit_cast<float>(rng());
    }
}
uint32_t PickWord(std::mt19937 &rng) { return std::bit_cast<uint32_t>(Pick(rng)); }

void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 17u);
    gw.framework.Reset<kFw>(70);
    gw.framework.On(3, &Create);
    gw.scene.Reset<kScene>(71);
    gw.scene.On(44, &SceneRange);
    gw.parent.Reset<kTree>(72);
    for (unsigned w = 0; w < 2; ++w) {
        gw.cursors[w].Reset<kIter>(73 + w);
        gw.cursors[w].On(4, &IterAtEnd);
        gw.cursors[w].On(5, &IterGet);
        gw.cursors[w].On(6, &IterNext);
        gw.begins[w].Reset<kIter>(75 + w);
        gw.ends[w].Reset<kIter>(77 + w);
    }
    static constexpr const char *kTags[] = {nullptr, "@@BoundingLights@@", "@@boundinglights@@", "@@BoundingLights@@ ",
                                            "", "@@BoundingLight", "@@BoundingLights@@"};
    gw.treeCount = rng() % (kTreeNodes + 1);
    for (unsigned i = 0; i < kTreeNodes; ++i) {
        gw.tree[i].Reset<kTree>(80 + i);
        gw.tree[i].Returns(34, &gw.parent);
        gw.tags[i] = kTags[rng() % std::size(kTags)];
    }
    gw.lightCount = rng() % (kLights + 1);
    static constexpr uint32_t kTypes[] = {2, 2, 3, 3, 3, 3, 1, 0, 4};
    for (unsigned i = 0; i < kLights; ++i) {
        gw.lightNodes[i].Reset<kLightNode>(90 + i);
        gw.lightNodes[i].On(10, &LightAttribute);
        gw.lights[i].Reset<kLight>(100 + i);
        gw.lights[i].Returns(36, kTypes[rng() % std::size(kTypes)]);
        if (rng() % 4) gw.lightNodes[i].Returns(95, &gw.lights[i]);
        // The range positive half the time.
        gw.lightNodes[i].value[0] = rng() % 2 ? std::bit_cast<uint32_t>(float(rng() % 100000) / 256.0f + 0.5f) : PickWord(rng);
        // The cone angles NaN (random payload and sign) a quarter of the time, so both are NaN in some cones.
        for (unsigned a = 1; a <= 2; ++a) gw.lightNodes[i].value[a] = rng() % 4 ? PickWord(rng) : 0x7f800001u | (rng() & 0x807fffffu);
    }
    static constexpr uint32_t kCounts[] = {0, 0, 0, 1, 2, 0xffffffff, 0x80000000};
    for (unsigned k = 0; k < kMade; ++k) {
        gw.holders[k].Reset<kHolder>(110 + k);
        gw.holders[k].On(94, &HolderAddShape);
        gw.holders[k].Returns(98, kCounts[rng() % std::size(kCounts)]);
        gw.shapes[k].Reset<kShape>(130 + k);
        gw.meshes[k].Reset<sgfx::kUnknown>(150 + k);
        gw.meshFails[k] = rng() % 6 == 0;
    }
    gw.holdersMade = gw.shapesMade = gw.meshesMade = 0;
}

void NoteCounts()
{
    for (const Fake *f : {&gw.cursors[0], &gw.cursors[1], &gw.begins[0], &gw.begins[1], &gw.ends[0], &gw.ends[1]})
        drawfx::Note("refs", {f->id, uint32_t(f->refs)});
    for (unsigned k = 0; k < kMade; ++k)
        drawfx::Note("made refs", {uint32_t(gw.holders[k].refs), uint32_t(gw.shapes[k].refs), uint32_t(gw.meshes[k].refs)});
    drawfx::Note("made", {gw.holdersMade, gw.shapesMade, gw.meshesMade});
}

constexpr unsigned kSeeds = 1000;

} // namespace

DIFF_TEST(SG_Utilities_RebuildLightBounds, 0xaeeea0)
{
    using Fn = void(__cdecl *)(void *, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaeeea0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    shape::StandIn getTag{0xae7900, &GetNodeTag, &StandGetTag};
    shape::StandIn setTag{0xae78f0, &SetNodeTag, &StandSetTag};
    shape::StandIn sphere{0xaf05b0, &MakeSphereMesh, &StandSphere};
    shape::StandIn cone{0xaef950, &MakeConeMesh, &StandCone};
    sgfx::CheckSides(ctx, "RebuildLightBounds", kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 2; ++k) {
                             static constexpr uint32_t kFlags[] = {0, 4, 8, 0xc, 0xc, 0xffffffff, 3, 0x10, 0x104};
                             const uint32_t flags = rng() % 8 ? kFlags[rng() % std::size(kFlags)] : rng();
                             drawfx::Note("flags", flags);
                             if (isOriginal)
                                 original(&gw.scene, flags);
                             else
                                 RebuildLightBounds(reinterpret_cast<cICompositionTreeNode *>(&gw.scene), flags);
                             NoteCounts();
                         }
                     });
}
