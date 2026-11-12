// Differential tests of nGZSceneGraph's free helpers (lane 261111b; src/sg/SGUtilities.h), part 2: the tree and
// extension helpers, on a random tree of node fakes (the sg_tree_code.cpp pattern).
//
//   SG_Utilities_SetNodeTag / GetNodeTag    0xae78f0 / 0xae7900
//   SG_Utilities_FindNodeByName             0xae7490
//   SG_Utilities_FindNodeOfKindByName       0xae7820
//   SG_Utilities_SubtreeHasFlag             0xaead10
//   SG_Utilities_SetLayersByFlag            0xaeb0e0
//   SG_Utilities_ForEachShapeBind           0xaed420
//   SG_Utilities_AnimatedFaceOf             0xae1ed0
//   SG_Utilities_FindDataList               0xaeb5b0
//   SG_Utilities_GetOrAddDataList           0xaeb3f0 (framework 0x1ae18d stood in on both sides)
//   SG_Utilities_GrowByShapeBounds          0xae7d00 (each node also has a world transform, slot 54, a box, slot 71,
//                                           in the frame passed, logged whole, and slot 70 / 97 answers)
//   SG_Utilities_ChannelOwnerOf             0xae42c0 (an animated fake, slot 59, and a channel extension with two
//                                           virtual bases, slot 12 on the second; scripted answers, counts noted)
//
// Lane 261111l, on the same tree:
//   SG_Utilities_ChannelSourceOf            0xae43a0 (each owner face's slot 8 answers a marked word)
//   SG_Utilities_SwapSidePrefix             0xae68a0 (a name object whose slot 25 logs and replaces; prefixes in mixed
//                                           case, one-character and empty names)
//   SG_Utilities_IsAncestorOf               0xaeaae0 (parent chains through slot 33, some looping back)
//   SG_Utilities_NodeKindName               0xaeab10 (kinds 0-9, boundaries and random words; the texts compared)
//   SG_Utilities_SetFlagInTree              0xaeac50 (slot 29 logs the raw words)
//   SG_Utilities_CollectTextureNames        0xae8090 (shapes' subset bindings, a material manager planted in its cache
//                                           0x10f8fcc, materials with 0-3 texture names; the vector pre-filled and
//                                           sometimes reserved; size, capacity, cells and their counts noted)
//
// Each seed builds 1-10 node fakes in a tree, each with random flags (10 bits), a tag from a pool (mixed case, empty,
// null), the kinds it is (slot 25 IsKindOf) and, separately, the interfaces its QueryInterface answers (itself,
// counted) over four ids (the transform and shape-ref ids among them), 0-3 shape fakes (slot 93's range; each shape's
// slot 34 logs its arguments) and 0-5 extensions (slot 24 FindExtension walks them from `start`, or the first, for the
// id asked, answering the extension's face counted on the extension; some nodes also write a counted face when they
// answer false). An extension is a data list (a face with a vbptr to the extension fake: slot 0 SetListName, 12
// GetListName, names from the pool), an animated face (0x1aff3bd4) or another kind. Every slot call is logged; after
// each call the result (normalised), every node's and extension's count, tags and list names are noted.
#include "sgfx.h"

#include "code_patch.h"
#include "rz/StaticGetters.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h"
#include "sg/cDataListExtension.h"

#include <array>
#include <cmath>
#include <cstring>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

constexpr unsigned kTreeSlotCount = 98;
constexpr std::array<Slot, kTreeSlotCount> TreeSlots()
{
    std::array<Slot, kTreeSlotCount> r{};
    for (unsigned i = 0; i < 61; ++i) r[i] = sgfx::kNodeSlots[i];
    for (unsigned i = 61; i < kTreeSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[70] = {0, Ret::Int, true, "IsEverywhere", ""};
    r[71] = {2, Ret::Int, true, "GetBoundBoxIn", "iF13"};
    r[97] = {0, Ret::Int, true, "AnyShapeSlot52", ""};
    r[77] = {1, Ret::Int, true, "AddLayer", "s"};
    r[79] = {1, Ret::Int, true, "RemoveLayer", "s"};
    r[93] = {1, Ret::Int, true, "GetShapes", "i"};
    r[66] = {1, Ret::Int, true, "SetBoundFlag", "u"}; // lane 261111f: the raw word
    r[78] = {1, Ret::Int, true, "AddLayerBits", "u"};
    r[80] = {1, Ret::Int, true, "RemoveLayerBits", "u"};
    r[81] = {1, Ret::Int, true, "SetLayerTag", "u"};
    r[29] = {2, Ret::Int, true, "SetFlag", "uu"}; // lane 261111l: SetFlagInTree passes both whole words
    r[92] = {1, Ret::Int, true, "GetShapesConst", "i"};
    return r;
}
constexpr auto kTreeSlots = TreeSlots();
constexpr drawfx::Iface kTreeNode{"TreeNode", kTreeSlotCount, kTreeSlots.data()};

constexpr unsigned kShapeSlotCount = 55;
constexpr std::array<Slot, kShapeSlotCount> ShapeSlots()
{
    std::array<Slot, kShapeSlotCount> r{};
    for (unsigned i = 0; i < kShapeSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    r[34] = {2, Ret::Int, true, "Bind", "is"};
    r[45] = {1, Ret::Int, true, "Slot45", "u"}; // lane 261111f
    r[54] = {2, Ret::Int, true, "GetSubsetBindings", "iu"}; // lane 261111l
    return r;
}
constexpr auto kShapeSlots = ShapeSlots();
constexpr drawfx::Iface kShape{"Shape", kShapeSlotCount, kShapeSlots.data()};

// The data-list face (cIDataListExtension's own slots 0-12; nothing here calls 1-11).
constexpr unsigned kListFaceSlotCount = 13;
constexpr std::array<Slot, kListFaceSlotCount> ListFaceSlots()
{
    std::array<Slot, kListFaceSlotCount> r{};
    for (unsigned i = 0; i < kListFaceSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[0] = {1, Ret::Int, true, "SetListName", "s"};
    r[12] = {0, Ret::Int, true, "GetListName", ""};
    r[8] = {0, Ret::Int, true, "Source", ""}; // lane 261111l: ChannelSourceOf asks the owner
    return r;
}
constexpr auto kListFaceSlots = ListFaceSlots();
constexpr drawfx::Iface kListFace{"ListFace", kListFaceSlotCount, kListFaceSlots.data()};

constexpr Slot kFwSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {3, Ret::Int, true, "CreateInstance", "uui"}, // 3
};
constexpr drawfx::Iface kFw{"Framework", 4, kFwSlots};

constexpr uint32_t kIids[4] = {0xe51c10f5, 0xa516cd10, 0x11110001, 0x11110002}; // transform, shape-ref, two others
constexpr uint32_t kAnimatedIid = 0x1aff3bd4, kListIid = 0x6a836d51, kOtherExtIid = 0x22220003;
const char *const kTags[] = {"Bone", "bone", "BONE", "root", "r00t", "", nullptr};
const char *const kSought[] = {"bone", "ROOT", "none", ""};

enum : unsigned { kFlags, kFirstChild, kNext, kLastChild, kTag, kKinds, kQi, kExtHead };
constexpr uint32_t kDirtyMiss = 0x100; // in value[kKinds]: FindExtension's misses still hand out a counted face

constexpr unsigned kMaxNodes = 10, kMaxShapes = 3, kMaxExts = 5, kSpares = 4;

// An extension: the base fake (sgfx::MakeExtension: its next in kExtNextWord), its face and the face's fake.
enum : unsigned { kExtFaceWord = 5, kExtKindWord = 6 };
struct Ext {
    Fake base;
    sgfx::TagFace face; // {vt, vbptr, vbtable}: the same shape as a data-list face
    Fake faceFake;
};

struct World {
    Fake nodes[kMaxNodes];
    Fake shapes[kMaxNodes][kMaxShapes];
    uint32_t shapeList[kMaxNodes][kMaxShapes];
    unsigned shapeCount[kMaxNodes];
    Ext exts[kMaxNodes][kMaxExts];
    unsigned extCount[kMaxNodes];
    Ext spares[kSpares];
    unsigned sparesUsed;
    Fake framework;
    Fake terrain;
    unsigned count;
    // GrowByShapeBounds: each node's world transform (slot 54), box (slot 71) and slot 70 / 97 answers.
    float world[kMaxNodes][7];
    float bounds[kMaxNodes][6];
    bool everywhere[kMaxNodes], anyShape[kMaxNodes];
} gw;

unsigned IndexOf(const Fake &f) { return f.id - 100; }

uint32_t TestFlag(Fake &f, const uint32_t *a) { return (f.value[kFlags] >> (a[0] & 31)) & 1; }
template <unsigned W> uint32_t Word(Fake &f, const uint32_t *) { return f.value[W]; }
uint32_t SetTag(Fake &f, const uint32_t *a) { return f.value[kTag] = a[0], 0; }
uint32_t Children(Fake &f, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = out[1] = f.value[kFirstChild];
    out[2] = 0;
    return a[0];
}
int IidIndex(uint32_t iid)
{
    for (int i = 0; i < 4; ++i)
        if (kIids[i] == iid) return i;
    return -1;
}
uint32_t IsKindOf(Fake &f, const uint32_t *a)
{
    const int i = IidIndex(a[0]);
    return i >= 0 && ((f.value[kKinds] >> i) & 1);
}
uint32_t Query(Fake &f, const uint32_t *a)
{
    const int i = IidIndex(a[0]);
    if (i < 0 || !((f.value[kQi] >> i) & 1)) return 0;
    ++f.refs;
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = drawfx::P(&f);
    return 1;
}
// Slot 93 (hidden out): {first, first, end} over the node's shapes.
uint32_t Shapes(Fake &f, const uint32_t *a)
{
    const unsigned n = IndexOf(f);
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = out[1] = drawfx::P(&gw.shapeList[n][0]);
    out[2] = drawfx::P(&gw.shapeList[n][gw.shapeCount[n]]);
    return a[0];
}

// Slot 54 (hidden out): the node's world transform.
uint32_t WorldTransform(Fake &f, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gw.world[IndexOf(f)], sizeof gw.world[0]);
    return a[0];
}
// Slot 71 (hidden out, frame): the node's box moved by the frame's offset (words 10-12) and scaled by its first matrix
// word, so the box depends on the frame the caller built (the frame is logged whole too).
uint32_t BoundBoxIn(Fake &f, const uint32_t *a)
{
    const float *frame = reinterpret_cast<const float *>(uintptr_t(a[1]));
    float *out = reinterpret_cast<float *>(uintptr_t(a[0]));
    const float *b = gw.bounds[IndexOf(f)];
    for (int i = 0; i < 6; ++i) out[i] = b[i] * frame[1] + frame[10 + i % 3];
    return a[0];
}
uint32_t Everywhere(Fake &f, const uint32_t *) { return gw.everywhere[IndexOf(f)]; }
uint32_t AnyShape(Fake &f, const uint32_t *) { return gw.anyShape[IndexOf(f)]; }

Ext *ExtOfBase(uint32_t base)
{
    for (unsigned n = 0; n < kMaxNodes; ++n)
        for (unsigned k = 0; k < gw.extCount[n]; ++k)
            if (drawfx::P(&gw.exts[n][k].base) == base) return &gw.exts[n][k];
    for (unsigned k = 0; k < gw.sparesUsed; ++k)
        if (drawfx::P(&gw.spares[k].base) == base) return &gw.spares[k];
    return nullptr;
}

// Slot 22 AddExtension(base): appended to the node's chain (null ignored).
uint32_t AddExtension(Fake &f, const uint32_t *a)
{
    Ext *const added = ExtOfBase(a[0]);
    if (!added) return 0;
    ++added->base.refs;
    if (!f.value[kExtHead]) return f.value[kExtHead] = a[0], 0;
    Ext *e = ExtOfBase(f.value[kExtHead]);
    while (e->base.value[sgfx::kExtNextWord]) e = ExtOfBase(e->base.value[sgfx::kExtNextWord]);
    e->base.value[sgfx::kExtNextWord] = a[0];
    return 0;
}

// Slot 24 (iid, out, start): from `start` (an extension's base) or the first, the first of kind `iid`.
uint32_t FindExtension(Fake &f, const uint32_t *a)
{
    const uint32_t at = a[2] ? a[2] : f.value[kExtHead];
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    Ext *last = nullptr;
    for (Ext *e = ExtOfBase(at); e; e = ExtOfBase(e->base.value[sgfx::kExtNextWord])) {
        last = e;
        if (e->base.value[kExtKindWord] != a[0]) continue;
        ++e->base.refs;
        *out = &e->face;
        return 1;
    }
    if ((f.value[kKinds] & kDirtyMiss) && last) { // a miss that still hands out a counted face
        ++last->base.refs;
        *out = &last->face;
    }
    return 0;
}

uint32_t ListName(Fake &f, const uint32_t *) { return f.value[0]; }
uint32_t SetListName(Fake &f, const uint32_t *a) { return f.value[0] = a[0], 0; }

void MakeExt(Ext &e, uint32_t id, uint32_t kind, const char *name)
{
    sgfx::MakeExtension(e.base, id);
    e.base.value[kExtKindWord] = kind;
    e.base.value[kExtFaceWord] = drawfx::P(&e.face);
    e.faceFake.Reset<kListFace>(id + 1000);
    e.face.vbptr = e.face.vbtable;
    e.face.vbtable[0] = -4;
    e.face.vbtable[1] = int32_t(drawfx::P(&e.base) - drawfx::P(&e.face.vbptr));
    e.faceFake.Embed(&e.face);
    e.faceFake.value[0] = drawfx::P(name);
    e.faceFake.On(0, &SetListName);
    e.faceFake.On(12, &ListName);
    drawfx::AddRange(&e.face, sizeof(e.face), 0x7e000000 | (id << 8));
}

// Slot 3 (class, iid, out): the next spare as a data list, counted; one call in four answers false (still writing).
uint32_t CreateInstance(Fake &f, const uint32_t *a)
{
    if (gw.sparesUsed == kSpares) return 0;
    Ext &e = gw.spares[gw.sparesUsed++];
    ++e.base.refs;
    *reinterpret_cast<uint32_t *>(uintptr_t(a[2])) = drawfx::P(&e.face);
    return (f.value[0]++ % 4) != 3;
}
void *StandFramework() { return &gw.framework; }

// ---- ChannelOwnerOf: an animated face (slot 59 finds its extensions), the channel extension (a face with two virtual
// bases: the cIExtension, counted, and a second whose slot 12 answers the owner) and owner faces (data-list shaped).
constexpr unsigned kAnimatedSlotCount = 60;
constexpr std::array<Slot, kAnimatedSlotCount> AnimatedSlots()
{
    std::array<Slot, kAnimatedSlotCount> r{};
    for (unsigned i = 0; i < kAnimatedSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[59] = {3, Ret::Int, true, "FindExtension", "uii"};
    return r;
}
constexpr auto kAnimatedSlots = AnimatedSlots();
constexpr drawfx::Iface kAnimated{"Animated", kAnimatedSlotCount, kAnimatedSlots.data()};
constexpr unsigned kChannelSlotCount = 13;
constexpr std::array<Slot, kChannelSlotCount> ChannelSlots()
{
    std::array<Slot, kChannelSlotCount> r{};
    for (unsigned i = 0; i < kChannelSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[12] = {3, Ret::Int, true, "Query", "uii"};
    return r;
}
constexpr auto kChannelSlots = ChannelSlots();
constexpr drawfx::Iface kChannel{"Channel", kChannelSlotCount, kChannelSlots.data()};

struct ChannelFace {
    void *vt;
    const int32_t *vbptr;
    int32_t vbtable[3];
};
constexpr unsigned kOwners = 2;
struct Channels {
    Fake animated;
    Fake base;   // the cIExtension (counted)
    Fake second; // the second virtual base (slot 12)
    ChannelFace face;
    Ext owners[kOwners];
    unsigned findMode, queryMode, owner; // the scripted answers of this call
} gc;

// Slot 59 (iid, out, start): 0 false; 1 false, still writing the counted face; 2 and 3 true with it (true with nothing
// written would have both sides read through null).
uint32_t AnimatedFind(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    if (gc.findMode == 0) return 0;
    ++gc.base.refs;
    *out = &gc.face;
    return gc.findMode >= 2;
}
// Slot 12 (iid, out, start): 0 false; 1 false, still writing a counted owner; 2 true with it; 3 true, nothing.
uint32_t ChannelQuery(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    if (gc.queryMode == 0) return 0;
    if (gc.queryMode != 3) {
        Ext &o = gc.owners[gc.owner];
        ++o.base.refs;
        *out = &o.face;
    }
    return gc.queryMode >= 2;
}

void BuildChannels()
{
    gc.animated.Reset<kAnimated>(60);
    gc.animated.On(59, &AnimatedFind);
    sgfx::MakeExtension(gc.base, 61);
    gc.second.Reset<kChannel>(62);
    gc.second.On(12, &ChannelQuery);
    gc.face.vt = nullptr; // nothing calls the face's own slots
    gc.face.vbptr = gc.face.vbtable;
    gc.face.vbtable[0] = -4;
    gc.face.vbtable[1] = int32_t(drawfx::P(&gc.base) - drawfx::P(&gc.face.vbptr));
    gc.face.vbtable[2] = int32_t(drawfx::P(&gc.second) - drawfx::P(&gc.face.vbptr));
    for (unsigned k = 0; k < kOwners; ++k) MakeExt(gc.owners[k], 70 + k, kOtherExtIid, "owner");
}
void NoteChannels()
{
    drawfx::Note("channel", {uint32_t(gc.base.refs), uint32_t(gc.owners[0].base.refs), uint32_t(gc.owners[1].base.refs)});
}

void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 7u);
    gw.count = 1 + rng() % kMaxNodes;
    gw.sparesUsed = 0;
    gw.framework.Reset<kFw>(80);
    gw.framework.On(3, &CreateInstance);
    gw.terrain.Reset<kShape>(81);
    for (unsigned i = 0; i < kSpares; ++i) MakeExt(gw.spares[i], 900 + i, kListIid, nullptr);
    for (unsigned i = 0; i < kMaxNodes; ++i) gw.extCount[i] = gw.shapeCount[i] = 0;
    for (unsigned i = 0; i < gw.count; ++i) {
        Fake &n = gw.nodes[i];
        n.Reset<kTreeNode>(100 + i);
        n.On(0, &Query);
        n.On(19, &SetTag);
        n.On(22, &AddExtension);
        n.On(24, &FindExtension);
        n.On(25, &IsKindOf);
        n.On(26, &Word<kTag>);
        n.On(37, &Word<kNext>);
        n.On(38, &Word<kNext>);
        n.On(41, &Children);
        n.On(42, &Children);
        n.On(48, &TestFlag);
        n.On(49, &Word<kFlags>);
        n.On(54, &WorldTransform);
        n.On(70, &Everywhere);
        n.On(71, &BoundBoxIn);
        n.On(93, &Shapes);
        n.On(97, &AnyShape);
        n.value[kFlags] = rng() & 0x3ff;
        n.value[kTag] = drawfx::P(kTags[rng() % 7]);
        n.value[kKinds] = rng() & 15;
        n.value[kQi] = rng() % 4 ? n.value[kKinds] : rng() & 15;
        gw.shapeCount[i] = rng() % (kMaxShapes + 1);
        for (unsigned k = 0; k < gw.shapeCount[i]; ++k) {
            gw.shapes[i][k].Reset<kShape>(200 + i * 10 + k);
            gw.shapeList[i][k] = drawfx::P(&gw.shapes[i][k]);
        }
        gw.extCount[i] = rng() % (kMaxExts + 1);
        for (unsigned k = 0; k < gw.extCount[i]; ++k) {
            const unsigned kind = rng() % 4;
            // List names never null (_stricmp would end the process on either side).
            MakeExt(gw.exts[i][k], 300 + i * 10 + k, kind < 2 ? kListIid : kind == 2 ? kAnimatedIid : kOtherExtIid,
                    kTags[rng() % 6]);
            if (k) gw.exts[i][k - 1].base.value[sgfx::kExtNextWord] = drawfx::P(&gw.exts[i][k].base);
        }
        n.value[kExtHead] = gw.extCount[i] ? drawfx::P(&gw.exts[i][0].base) : 0;
        if (rng() % 4 == 0) n.value[kKinds] |= kDirtyMiss;
        if (i == 0) continue;
        Fake &parent = gw.nodes[rng() % i];
        if (parent.value[kLastChild])
            reinterpret_cast<Fake *>(uintptr_t(parent.value[kLastChild]))->value[kNext] = drawfx::P(&n);
        else
            parent.value[kFirstChild] = drawfx::P(&n);
        parent.value[kLastChild] = drawfx::P(&n);
    }
    // The bounds data from a second stream (the first one's draws, and so the other tests' trees, unchanged).
    std::mt19937 more(seed * 40503u + 11u);
    std::uniform_real_distribution<float> coord(-50.0f, 50.0f), size(0.0f, 8.0f), unit(-1.0f, 1.0f);
    for (unsigned i = 0; i < gw.count; ++i) {
        float *w = gw.world[i];
        for (int k = 0; k < 3; ++k) w[k] = coord(more);
        float q[4], len = 0.0f;
        do {
            len = 0.0f;
            for (float &c : q) c = unit(more), len += c * c;
        } while (len < 0.01f);
        for (int k = 0; k < 4; ++k) w[3 + k] = q[k] / std::sqrt(len);
        float *b = gw.bounds[i];
        for (int k = 0; k < 3; ++k) b[k] = coord(more), b[3 + k] = b[k] + size(more);
        gw.everywhere[i] = more() % 4 == 0;
        gw.anyShape[i] = more() % 4 != 0;
    }
}

void NoteExt(const Ext &e)
{
    drawfx::Note("ext", {e.base.id, uint32_t(e.base.refs)});
    sgfx::NoteText("list", reinterpret_cast<const char *>(uintptr_t(e.faceFake.value[0])));
}

// Every count, tag and list name.
void NoteState()
{
    for (unsigned i = 0; i < gw.count; ++i) {
        drawfx::Note("node", {gw.nodes[i].id, uint32_t(gw.nodes[i].refs)});
        sgfx::NoteText("tag", reinterpret_cast<const char *>(uintptr_t(gw.nodes[i].value[kTag])));
        for (unsigned k = 0; k < gw.extCount[i]; ++k) NoteExt(gw.exts[i][k]);
    }
    for (unsigned k = 0; k < gw.sparesUsed; ++k) NoteExt(gw.spares[k]);
}

cICompositionTreeNode *AsNode(Fake &f) { return reinterpret_cast<cICompositionTreeNode *>(&f); }
Fake &AnyNode(std::mt19937 &rng) { return gw.nodes[rng() % gw.count]; }
int32_t SkipFlag(std::mt19937 &rng) { return rng() % 3 ? int32_t(rng() % 10) : -int32_t(1 + rng() % 3); }

constexpr unsigned kSeeds = 64;

// `call(isOriginal, rng)` 1-6 times per seed, the state noted after each.
template <class Call> void Check(difftest::Context &ctx, const char *what, Call &&call)
{
    sgfx::CheckSides(ctx, what, kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         const unsigned calls = 1 + rng() % 6;
                         for (unsigned k = 0; k < calls; ++k) {
                             call(isOriginal, rng);
                             NoteState();
                         }
                     });
}

void NoteFound(const char *label, const void *p) { drawfx::Note(label, drawfx::Norm(drawfx::P(p))); }

} // namespace

DIFF_TEST(SG_Utilities_SetNodeTag, 0xae78f0)
{
    using Fn = void(__cdecl *)(void *, const char *);
    const Fn original = ctx.Original<Fn>(0xae78f0);
    Check(ctx, "SetNodeTag", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const char *tag = kTags[rng() % 7];
        drawfx::Note("call", node.id);
        if (isOriginal)
            original(&node, tag);
        else
            SetNodeTag(AsNode(node), tag);
    });
}

DIFF_TEST(SG_Utilities_GetNodeTag, 0xae7900)
{
    using Fn = const char *(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xae7900);
    Check(ctx, "GetNodeTag", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        drawfx::Note("call", node.id);
        const char *tag = isOriginal ? original(&node) : GetNodeTag(AsNode(node));
        drawfx::Note("same", tag == reinterpret_cast<const char *>(uintptr_t(node.value[kTag])));
    });
}

DIFF_TEST(SG_Utilities_FindNodeByName, 0xae7490)
{
    using Fn = void *(__cdecl *)(const char *, void *, int32_t);
    const Fn original = ctx.Original<Fn>(0xae7490);
    Check(ctx, "FindNodeByName", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const char *name = kSought[rng() % 4];
        const int32_t skip = SkipFlag(rng);
        drawfx::Note("call", {node.id, uint32_t(skip)});
        NoteFound("found", isOriginal ? original(name, &node, skip) : FindNodeByName(name, AsNode(node), skip));
    });
}

DIFF_TEST(SG_Utilities_FindNodeOfKindByName, 0xae7820)
{
    using Fn = uint8_t(__cdecl *)(const char *, void *, int32_t, uint32_t, void **);
    const Fn original = ctx.Original<Fn>(0xae7820);
    Check(ctx, "FindNodeOfKindByName", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const char *name = kSought[rng() % 4];
        const int32_t skip = SkipFlag(rng);
        const uint32_t iid = rng() % 8 ? kIids[rng() % 4] : 0x33330000u;
        void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5au));
        drawfx::Note("call", {node.id, uint32_t(skip), iid});
        const uint8_t al = isOriginal ? original(name, &node, skip, iid, &out)
                                      : uint8_t(FindNodeOfKindByName(name, AsNode(node), skip, iid, &out));
        drawfx::Note("al", al);
        NoteFound("out", out);
    });
}

DIFF_TEST(SG_Utilities_SubtreeHasFlag, 0xaead10)
{
    using Fn = uint8_t(__cdecl *)(void *, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaead10);
    Check(ctx, "SubtreeHasFlag", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const uint32_t bit = rng() % 12;
        drawfx::Note("call", {node.id, bit});
        drawfx::Note("al", isOriginal ? original(&node, bit) : uint8_t(SubtreeHasFlag(AsNode(node), bit)));
    });
}

DIFF_TEST(SG_Utilities_SetLayersByFlag, 0xaeb0e0)
{
    using Fn = void(__cdecl *)(void *, uint32_t, const char *, const char *, int32_t);
    const Fn original = ctx.Original<Fn>(0xaeb0e0);
    static const char *const kLayers[] = {"shadow", "reflection", nullptr};
    Check(ctx, "SetLayersByFlag", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const uint32_t bit = rng() % 10;
        const char *add = kLayers[rng() % 3], *remove = kLayers[rng() % 3];
        const int32_t skip = SkipFlag(rng);
        drawfx::Note("call", {node.id, bit, uint32_t(skip)});
        if (isOriginal)
            original(&node, bit, add, remove, skip);
        else
            SetLayersByFlag(AsNode(node), bit, add, remove, skip);
    });
}

DIFF_TEST(SG_Utilities_ForEachShapeBind, 0xaed420)
{
    using Fn = void(__cdecl *)(void *, void *, const char *);
    const Fn original = ctx.Original<Fn>(0xaed420);
    Check(ctx, "ForEachShapeBind", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const char *b = rng() & 1 ? "terrain" : "water";
        drawfx::Note("call", node.id);
        if (isOriginal)
            original(&node, &gw.terrain, b);
        else
            ForEachShapeBind(AsNode(node), &gw.terrain, b);
    });
}

DIFF_TEST(SG_Utilities_AnimatedFaceOf, 0xae1ed0)
{
    using Fn = void *(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xae1ed0);
    Check(ctx, "AnimatedFaceOf", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        drawfx::Note("call", node.id);
        NoteFound("face", isOriginal ? original(&node) : AnimatedFaceOf(AsNode(node)));
    });
}

DIFF_TEST(SG_Utilities_FindDataList, 0xaeb5b0)
{
    using Fn = void *(__cdecl *)(void *, const char *, void *);
    const Fn original = ctx.Original<Fn>(0xaeb5b0);
    Check(ctx, "FindDataList", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const unsigned n = IndexOf(node);
        const char *name = kSought[rng() % 4];
        // After: none, or one of the node's data lists.
        void *after = nullptr;
        if (gw.extCount[n] && rng() % 3 == 0) {
            Ext &e = gw.exts[n][rng() % gw.extCount[n]];
            if (e.base.value[kExtKindWord] == kListIid) after = &e.face;
        }
        drawfx::Note("call", node.id);
        NoteFound("after", after);
        NoteFound("found", isOriginal ? original(&node, name, after)
                                      : FindDataList(AsNode(node), name, static_cast<cIDataListExtension *>(after)));
    });
}

DIFF_TEST(SG_Utilities_GetOrAddDataList, 0xaeb3f0)
{
    using Fn = void *(__cdecl *)(void *, const char *, bool);
    const Fn original = ctx.Original<Fn>(0xaeb3f0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    Check(ctx, "GetOrAddDataList", [&](bool isOriginal, std::mt19937 &rng) {
        if (gw.sparesUsed == kSpares) return; // creation would fail (the original names it through null)
        Fake &node = AnyNode(rng);
        const char *name = kSought[rng() % 4];
        const bool reuse = rng() % 3 != 0;
        drawfx::Note("call", {node.id, reuse});
        NoteFound("list", isOriginal ? original(&node, name, reuse) : GetOrAddDataList(AsNode(node), name, reuse));
    });
}

DIFF_TEST(SG_Utilities_GrowByShapeBounds, 0xae7d00)
{
    using Fn = void(__cdecl *)(float *, int32_t, void *, void *);
    const Fn original = ctx.Original<Fn>(0xae7d00);
    Check(ctx, "GrowByShapeBounds", [&](bool isOriginal, std::mt19937 &rng) {
        float box[6];
        if (rng() % 4 == 0) {
            for (int k = 0; k < 3; ++k) box[k] = 3.4e38f, box[3 + k] = -3.4e38f; // empty: any box grows it
        } else {
            for (int k = 0; k < 3; ++k) {
                box[k] = float(int(rng() % 200) - 100);
                box[3 + k] = box[k] + float(rng() % 40);
            }
        }
        const int32_t skip = SkipFlag(rng);
        Fake &frameNode = AnyNode(rng);
        Fake &node = AnyNode(rng);
        drawfx::Note("call", {frameNode.id, node.id, uint32_t(skip)});
        if (isOriginal)
            original(box, skip, &frameNode, &node);
        else
            GrowByShapeBounds(box, skip, AsNode(frameNode), AsNode(node));
        uint32_t bits[6];
        std::memcpy(bits, box, sizeof bits);
        drawfx::Note("box", {bits[0], bits[1], bits[2], bits[3], bits[4], bits[5]});
    });
}

DIFF_TEST(SG_Utilities_ChannelOwnerOf, 0xae42c0)
{
    using Fn = void *(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xae42c0);
    sgfx::CheckSides(ctx, "ChannelOwnerOf", kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed), BuildChannels(); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         const unsigned calls = 1 + rng() % 6;
                         for (unsigned k = 0; k < calls; ++k) {
                             gc.findMode = rng() % 4;
                             gc.queryMode = rng() % 4;
                             gc.owner = rng() % kOwners;
                             drawfx::Note("call", {gc.findMode, gc.queryMode, gc.owner});
                             NoteFound("owner", isOriginal ? original(&gc.animated) : ChannelOwnerOf(&gc.animated));
                             NoteChannels();
                         }
                     });
}

// ---- lane 261111f: tree walks and lookups (src/sg/SGUtilitiesTree.cpp) ----

DIFF_TEST(SG_Utilities_NameCrc, 0xae6870)
{
    using Fn = uint32_t(__cdecl *)(const char *);
    const Fn original = ctx.Original<Fn>(0xae6870);
    static const char *const kNames[] = {nullptr, "", "a", "A", "Bone", "bone", "BONE_01", "x y z", "\x7f\x80\xff"};
    sgfx::CheckSides(ctx, "NameCrc", 16, [](uint32_t, drawfx::Cells &) {}, [&](bool isOriginal, std::mt19937 &rng) {
        for (int k = 0; k < 12; ++k) {
            const char *name = kNames[rng() % 9];
            drawfx::Note("crc", isOriginal ? original(name) : NameCrc(name));
        }
    });
}

namespace {
constexpr unsigned kSlot77Count = 78;
constexpr std::array<Slot, kSlot77Count> Slot77Slots()
{
    std::array<Slot, kSlot77Count> r{};
    for (unsigned i = 0; i < kSlot77Count; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[77] = {0, Ret::Int, true, "Slot77", ""};
    return r;
}
constexpr auto kSlot77Table = Slot77Slots();
constexpr drawfx::Iface kSlot77Iface{"Slot77", kSlot77Count, kSlot77Table.data()};
Fake gSlot77;
} // namespace

DIFF_TEST(SG_Utilities_Slot77IsNotDefault, 0xae4470)
{
    using Fn = uint8_t(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xae4470);
    static const uint32_t kAnswers[] = {0x2fa18737, 0x2fa18736, 0x2fa18737u ^ 0x80000000u, 0, 0xffffffff, 0x2fa18737 + 0x100};
    sgfx::CheckSides(ctx, "Slot77IsNotDefault", 16, [](uint32_t, drawfx::Cells &) { gSlot77.Reset<kSlot77Iface>(500); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (int k = 0; k < 8; ++k) {
                             gSlot77.value[0] = kAnswers[rng() % 6];
                             gSlot77.On(77, &Word<0>);
                             drawfx::Note("al", isOriginal ? original(&gSlot77) : uint8_t(Slot77IsNotDefault(&gSlot77)));
                         }
                     });
}

DIFF_TEST(SG_Utilities_RemoveDataLists, 0xaeb6a0)
{
    using Fn = void(__cdecl *)(void *, const char *);
    const Fn original = ctx.Original<Fn>(0xaeb6a0);
    Check(ctx, "RemoveDataLists", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const char *name = kSought[rng() % 4];
        drawfx::Note("call", node.id);
        if (isOriginal)
            original(&node, name);
        else
            RemoveDataLists(AsNode(node), name);
    });
}

DIFF_TEST(SG_Utilities_FindDataListInTree, 0xaeb520)
{
    using Fn = void *(__cdecl *)(void *, const char *, int32_t);
    const Fn original = ctx.Original<Fn>(0xaeb520);
    Check(ctx, "FindDataListInTree", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const char *name = kSought[rng() % 4];
        const int32_t skip = SkipFlag(rng);
        drawfx::Note("call", {node.id, uint32_t(skip)});
        NoteFound("found", isOriginal ? original(&node, name, skip) : FindDataListInTree(AsNode(node), name, skip));
    });
}

DIFF_TEST(SG_Utilities_SetLayerTagInTree, 0xaeb070)
{
    using Fn = void(__cdecl *)(void *, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaeb070);
    Check(ctx, "SetLayerTagInTree", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const uint32_t tag = rng() % 4 == 0 ? 999 : rng();
        drawfx::Note("call", {node.id, tag});
        if (isOriginal)
            original(&node, tag);
        else
            SetLayerTagInTree(AsNode(node), tag);
    });
}

DIFF_TEST(SG_Utilities_SetLayerBitsInTree, 0xaeb190)
{
    using Fn = void(__cdecl *)(void *, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaeb190);
    Check(ctx, "SetLayerBitsInTree", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const uint32_t add = rng(), remove = rng();
        drawfx::Note("call", {node.id, add, remove});
        if (isOriginal)
            original(&node, add, remove);
        else
            SetLayerBitsInTree(AsNode(node), add, remove);
    });
}

DIFF_TEST(SG_Utilities_SetBoundFlagInTree, 0xaeb210)
{
    using Fn = void(__cdecl *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaeb210);
    Check(ctx, "SetBoundFlagInTree", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const uint32_t value = rng() % 3 == 0 ? 0 : rng();
        // Masks of 0-3 bits of the 10 the flags use, so every branch (and its overlaps) is reached.
        auto mask = [&] {
            uint32_t m = 0;
            for (unsigned k = rng() % 4; k; --k) m |= 1u << (rng() % 10);
            return m;
        };
        const uint32_t a = mask(), b = mask(), c = mask(), d = mask();
        drawfx::Note("call", {node.id, value, a, b, c, d});
        if (isOriginal)
            original(&node, value, a, b, c, d);
        else
            SetBoundFlagInTree(AsNode(node), value, a, b, c, d);
    });
}

DIFF_TEST(SG_Utilities_ResetShapesInTree, 0xae7e60)
{
    using Fn = void(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xae7e60);
    Check(ctx, "ResetShapesInTree", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        drawfx::Note("call", node.id);
        if (isOriginal)
            original(&node);
        else
            ResetShapesInTree(AsNode(node));
    });
}

// ---- lane 261111f, second part: the QueryInterface-cell walks, slot 50 / 39, and the builder items ----

namespace {

// A QueryInterface that also writes a counted pointer into the answer cell on some misses (the nodes with
// kDirtyMiss), the way the original's callees leave a held face behind: a hit (true, pointer), a dirty miss (false,
// pointer) and a clean miss (false, cell untouched).
uint32_t DirtyQuery(Fake &f, const uint32_t *a)
{
    const int i = IidIndex(a[0]);
    const bool hit = i >= 0 && ((f.value[kQi] >> i) & 1);
    if (hit || (f.value[kKinds] & kDirtyMiss)) {
        ++f.refs;
        *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = drawfx::P(&f);
    }
    return hit;
}

template <class Call> void CheckDirty(difftest::Context &ctx, const char *what, Call &&call)
{
    sgfx::CheckSides(ctx, what, kSeeds,
                     [](uint32_t seed, drawfx::Cells &) {
                         Build(seed);
                         for (unsigned i = 0; i < gw.count; ++i) gw.nodes[i].On(0, &DirtyQuery);
                     },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         const unsigned calls = 1 + rng() % 6;
                         for (unsigned k = 0; k < calls; ++k) {
                             call(isOriginal, rng);
                             NoteState();
                         }
                     });
}

// A node reference that is sometimes null.
Fake *NodeOrNull(std::mt19937 &rng) { return rng() % 8 == 0 ? nullptr : &AnyNode(rng); }

// 0xaebc70: a node whose slot 50 answers a counted object (or null), whose slot 39 logs its argument.
constexpr unsigned kSlot50Count = 51, kTargetCount = 40;
constexpr std::array<Slot, kSlot50Count> Slot50Slots()
{
    std::array<Slot, kSlot50Count> r{};
    for (unsigned i = 0; i < kSlot50Count; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[50] = {0, Ret::Int, true, "Slot50", ""};
    return r;
}
constexpr std::array<Slot, kTargetCount> TargetSlots()
{
    std::array<Slot, kTargetCount> r{};
    for (unsigned i = 0; i < kTargetCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    r[39] = {1, Ret::Int, true, "Slot39", "u"};
    return r;
}
constexpr auto kSlot50Table = Slot50Slots();
constexpr auto kTargetTable = TargetSlots();
constexpr drawfx::Iface kSlot50Node{"Slot50Node", kSlot50Count, kSlot50Table.data()};
constexpr drawfx::Iface kTarget{"Target", kTargetCount, kTargetTable.data()};
Fake gHolder, gTarget;

// 0xaed340: a builder whose slot 42 writes {cur, mid, end} through its hidden out; counted iterators (slot 4 IsEqual of
// another iterator, 5 the item, 6 advance); a part whose slot 34 (kShape) logs (name, item).
constexpr unsigned kBuilderCount = 43, kIterCount = 7, kPartCount = 35;
constexpr std::array<Slot, kPartCount> PartSlots()
{
    std::array<Slot, kPartCount> r{};
    for (unsigned i = 0; i < kPartCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[34] = {2, Ret::Int, true, "BindItem", "su"};
    return r;
}
constexpr auto kPartTable = PartSlots();
constexpr drawfx::Iface kPart{"Part", kPartCount, kPartTable.data()};
constexpr std::array<Slot, kBuilderCount> BuilderSlots()
{
    std::array<Slot, kBuilderCount> r{};
    for (unsigned i = 0; i < kBuilderCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[42] = {1, Ret::Int, true, "GetRange", "i"};
    return r;
}
constexpr std::array<Slot, kIterCount> IterSlots()
{
    std::array<Slot, kIterCount> r{};
    for (unsigned i = 0; i < kIterCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    r[4] = {1, Ret::Int, true, "IsEqual", "i"};
    r[5] = {0, Ret::Int, true, "Get", ""};
    r[6] = {0, Ret::Int, true, "Next", ""};
    return r;
}
constexpr auto kBuilderTable = BuilderSlots();
constexpr auto kIterTable = IterSlots();
constexpr drawfx::Iface kBuilder{"Builder", kBuilderCount, kBuilderTable.data()};
constexpr drawfx::Iface kIter{"Iter", kIterCount, kIterTable.data()};
Fake gBuilder, gCur, gMid, gEnd, gPart;
bool gMidPresent;
unsigned gItemCount;

uint32_t IterIsEqual(Fake &f, const uint32_t *a)
{
    const Fake *other = reinterpret_cast<const Fake *>(uintptr_t(a[0]));
    drawfx::Note("eq", {f.value[0], other ? other->value[0] : 0xffffffffu});
    return other && f.value[0] == other->value[0];
}
uint32_t IterGet(Fake &f, const uint32_t *) { return 0x500 + f.value[0] * 3; }
uint32_t IterNext(Fake &f, const uint32_t *) { return ++f.value[0]; }
uint32_t GetRange(Fake &, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = drawfx::P(&gCur);
    out[1] = gMidPresent ? drawfx::P(&gMid) : 0;
    out[2] = drawfx::P(&gEnd);
    return a[0];
}

} // namespace

DIFF_TEST(SG_Utilities_ShapeRefOfNodeOrChild, 0xaeb8a0)
{
    using Fn = void *(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xaeb8a0);
    CheckDirty(ctx, "ShapeRefOfNodeOrChild", [&](bool isOriginal, std::mt19937 &rng) {
        Fake *node = NodeOrNull(rng);
        drawfx::Note("call", node ? node->id : 0);
        NoteFound("found", isOriginal ? original(node) : ShapeRefOfNodeOrChild(node ? AsNode(*node) : nullptr));
    });
}

DIFF_TEST(SG_Utilities_TransformOfChild, 0xaeb9a0)
{
    using Fn = void *(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xaeb9a0);
    CheckDirty(ctx, "TransformOfChild", [&](bool isOriginal, std::mt19937 &rng) {
        Fake *node = NodeOrNull(rng);
        drawfx::Note("call", node ? node->id : 0);
        NoteFound("found", isOriginal ? original(node) : TransformOfChild(node ? AsNode(*node) : nullptr));
    });
}

DIFF_TEST(SG_Utilities_ShapeRefInSubtree, 0xaebb00)
{
    using Fn = void *(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xaebb00);
    CheckDirty(ctx, "ShapeRefInSubtree", [&](bool isOriginal, std::mt19937 &rng) {
        Fake *node = NodeOrNull(rng);
        drawfx::Note("call", node ? node->id : 0);
        NoteFound("found", isOriginal ? original(node) : ShapeRefInSubtree(node ? AsNode(*node) : nullptr));
    });
}

DIFF_TEST(SG_Utilities_ShapeRefOfNodeOrSubtree, 0xaeba70)
{
    using Fn = void *(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xaeba70);
    CheckDirty(ctx, "ShapeRefOfNodeOrSubtree", [&](bool isOriginal, std::mt19937 &rng) {
        Fake *node = NodeOrNull(rng);
        drawfx::Note("call", node ? node->id : 0);
        NoteFound("found", isOriginal ? original(node) : ShapeRefOfNodeOrSubtree(node ? AsNode(*node) : nullptr));
    });
}

DIFF_TEST(SG_Utilities_PassToSlot50Object, 0xaebc70)
{
    using Fn = uint8_t(__cdecl *)(void *, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaebc70);
    sgfx::CheckSides(ctx, "PassToSlot50Object", 32,
                     [](uint32_t, drawfx::Cells &) {
                         gHolder.Reset<kSlot50Node>(600);
                         gTarget.Reset<kTarget>(601);
                     },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (int k = 0; k < 6; ++k) {
                             const bool hasTarget = rng() % 3 != 0;
                             gHolder.value[0] = hasTarget ? drawfx::P(&gTarget) : 0;
                             gHolder.On(50, &Word<0>);
                             const bool nullNode = rng() % 6 == 0;
                             const uint32_t argument = rng();
                             drawfx::Note("call", {nullNode, hasTarget, argument});
                             void *node = nullNode ? nullptr : &gHolder;
                             drawfx::Note("al", isOriginal ? original(node, argument)
                                                           : uint8_t(PassToSlot50Object(
                                                                 reinterpret_cast<cICompositionTreeNode *>(node), argument)));
                             drawfx::Note("refs", uint32_t(gTarget.refs));
                         }
                     });
}

DIFF_TEST(SG_Utilities_BindNamedBuilderItems, 0xaed340)
{
    using Fn = void(__cdecl *)(const char *, void *, void *);
    const Fn original = ctx.Original<Fn>(0xaed340);
    static const char *const kNames[] = {"layer", "", "Water"};
    sgfx::CheckSides(ctx, "BindNamedBuilderItems", 32,
                     [](uint32_t, drawfx::Cells &) {
                         gBuilder.Reset<kBuilder>(700);
                         gBuilder.On(42, &GetRange);
                         gPart.Reset<kPart>(701);
                     },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (int k = 0; k < 6; ++k) {
                             gItemCount = rng() % 5;
                             gMidPresent = rng() % 3 != 0;
                             const unsigned start = gItemCount ? rng() % (gItemCount + 1) : 0;
                             gCur.Reset<kIter>(710);
                             gMid.Reset<kIter>(711);
                             gEnd.Reset<kIter>(712);
                             gCur.value[0] = start;
                             gMid.value[0] = 77; // never equal: the original compares against the end
                             gEnd.value[0] = gItemCount;
                             for (Fake *it : {&gCur, &gMid, &gEnd}) {
                                 it->On(4, &IterIsEqual);
                                 it->On(5, &IterGet);
                                 it->On(6, &IterNext);
                             }
                             const char *name = kNames[rng() % 3];
                             const unsigned nulls = rng() % 8; // 1 builder null, 2 part null, else both present
                             void *builder = nulls == 1 ? nullptr : &gBuilder;
                             void *part = nulls == 2 ? nullptr : &gPart;
                             drawfx::Note("call", {gItemCount, start, gMidPresent, nulls});
                             if (isOriginal)
                                 original(name, builder, part);
                             else
                                 BindNamedBuilderItems(name, static_cast<cIGZUnknown *>(builder),
                                                       static_cast<cIGZUnknown *>(part));
                             drawfx::Note("refs", {uint32_t(gCur.refs), uint32_t(gMid.refs), uint32_t(gEnd.refs), gCur.value[0]});
                         }
                     });
}

// ---- 0xae6900, 0xaea840, 0xaeab90: side-swapped names, the named-pair search and its table walk ----

namespace {

constexpr unsigned kNameBuilderCount = 21;
constexpr std::array<Slot, kNameBuilderCount> NameBuilderSlots()
{
    std::array<Slot, kNameBuilderCount> r{};
    for (unsigned i = 0; i < kNameBuilderCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[4] = {1, Ret::Int, true, "Append", "s"};
    r[20] = {2, Ret::Int, true, "AppendN", "su"};
    return r;
}
constexpr auto kNameBuilderTable = NameBuilderSlots();
constexpr drawfx::Iface kNameBuilder{"NameBuilder", kNameBuilderCount, kNameBuilderTable.data()};
Fake gNameBuilder;

// The search owners: three entries, each with its own range of counted iterators over up to four {first, second} pairs.
struct Pair {
    const char *first;
    const char *second;
};
constexpr unsigned kEntries = 3, kPairs = 4;
struct Entry {
    Fake fake, cur, mid, end;
    Pair pairs[kPairs];
    bool midPresent;
} gEntries[kEntries];

constexpr unsigned kEntryCount = 52, kPairIterCount = 7, kTableCount = 22;
constexpr std::array<Slot, kEntryCount> EntrySlots()
{
    std::array<Slot, kEntryCount> r{};
    for (unsigned i = 0; i < kEntryCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[7] = {3, Ret::Int, true, "Apply", "uuu"};
    r[51] = {2, Ret::Int, true, "GetRange", "iu"};
    return r;
}
constexpr std::array<Slot, kPairIterCount> PairIterSlots()
{
    std::array<Slot, kPairIterCount> r{};
    for (unsigned i = 0; i < kPairIterCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    r[4] = {1, Ret::Int, true, "IsEqual", "i"};
    r[5] = {1, Ret::Int, true, "Get", "i"};
    r[6] = {0, Ret::Int, true, "Next", ""};
    return r;
}
constexpr std::array<Slot, kTableCount> TableSlots()
{
    std::array<Slot, kTableCount> r{};
    for (unsigned i = 0; i < kTableCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[3] = {1, Ret::Int, true, "Reload", "u"};
    r[16] = {1, Ret::Int, true, "Setup", "u"};
    r[17] = {1, Ret::Int, true, "SetMode", "u"};
    r[18] = {0, Ret::Int, true, "Clear", ""};
    r[19] = {0, Ret::Int, true, "GetMode", ""};
    r[20] = {0, Ret::Int, true, "Count", ""};
    r[21] = {1, Ret::Int, true, "Entry", "u"};
    return r;
}
constexpr auto kEntryTable = EntrySlots();
constexpr auto kPairIterTable = PairIterSlots();
constexpr auto kTableTable = TableSlots();
constexpr drawfx::Iface kEntryIface{"NamedEntry", kEntryCount, kEntryTable.data()};
constexpr drawfx::Iface kPairIter{"PairIter", kPairIterCount, kPairIterTable.data()};
constexpr drawfx::Iface kTableIface{"NamedTable", kTableCount, kTableTable.data()};
Fake gTable;
Fake gFramework2;
cIGZUnknown *gCell;
unsigned gEntryCount;

uint32_t EntryIndex(const Fake &f) { return f.value[0]; }
uint32_t PairGet(Fake &f, const uint32_t *)
{
    Entry &e = gEntries[f.value[1]];
    drawfx::Note("get", {f.value[1], f.value[0]});
    return drawfx::P(&e.pairs[f.value[0] % kPairs]);
}
uint32_t EntryRange(Fake &f, const uint32_t *a)
{
    Entry &e = gEntries[EntryIndex(f)];
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    drawfx::Note("range", {EntryIndex(f), a[1]});
    out[0] = drawfx::P(&e.cur);
    out[1] = e.midPresent ? drawfx::P(&e.mid) : 0;
    out[2] = drawfx::P(&e.end);
    return a[0];
}
const char *const kFirsts[] = {"Body", "body", "Head", "x", ""};
const char *const kSeconds[] = {"A", "a", "B"};
const char *const kFirstsAsked[] = {"BODY", "head", "X", "", "body", "tail"};
const char *const kSecondsAsked[] = {"a", "A", "b", "C"};

// A fresh set of entries (each its counted iterators and pairs); the first `used` are live.
void SetupEntries(std::mt19937 &rng)
{
    for (unsigned i = 0; i < kEntries; ++i) {
        Entry &e = gEntries[i];
        e.fake.Reset<kEntryIface>(800 + i);
        e.fake.value[0] = i;
        e.fake.On(51, &EntryRange);
        const unsigned count = rng() % (kPairs + 1);
        unsigned which = 0;
        for (Fake *it : {&e.cur, &e.mid, &e.end}) {
            it->Reset<kPairIter>(900 + 3 * i + which++); // each its own id: the order of their Releases shows
            it->value[1] = i;
            it->On(4, &IterIsEqual);
            it->On(5, &PairGet);
            it->On(6, &IterNext);
        }
        e.cur.value[0] = 0;
        e.mid.value[0] = 77;
        e.end.value[0] = count;
        e.midPresent = rng() % 3 != 0;
        for (Pair &p : e.pairs) p = {kFirsts[rng() % 5], kSeconds[rng() % 3]};
    }
}

} // namespace

namespace {
// A caller's vector holding up to `before` earlier elements and room for `room` more: with room short of the tree, the
// vector grows inside the call (the original's growth is the game's own operator new; ours the process heap's).
void FillBefore(std::vector<void *> &v, std::mt19937 &rng)
{
    const unsigned before = rng() % 3;
    for (unsigned i = 0; i < before; ++i) v.push_back(reinterpret_cast<void *>(uintptr_t(0x1000 + i)));
    if (rng() % 2) v.reserve(1 + rng() % 48);
}
void NoteVector(const std::vector<void *> &v)
{
    drawfx::Note("size", uint32_t(v.size()));
    for (void *p : v) NoteFound("elem", p);
}
} // namespace

DIFF_TEST(SG_Utilities_CollectNodes, 0xaea980)
{
    using Fn = void(__cdecl *)(void *, std::vector<void *> *);
    const Fn original = ctx.Original<Fn>(0xaea980);
    Check(ctx, "CollectNodes", [&](bool isOriginal, std::mt19937 &rng) {
        Fake *node = NodeOrNull(rng);
        std::vector<void *> out;
        FillBefore(out, rng);
        drawfx::Note("call", node ? node->id : 0);
        if (isOriginal)
            original(node, &out);
        else
            CollectNodes(node ? AsNode(*node) : nullptr, out);
        NoteVector(out);
    });
}

DIFF_TEST(SG_Utilities_CollectShapes, 0xaea9f0)
{
    using Fn = void(__cdecl *)(void *, std::vector<void *> *);
    const Fn original = ctx.Original<Fn>(0xaea9f0);
    CheckDirty(ctx, "CollectShapes", [&](bool isOriginal, std::mt19937 &rng) {
        Fake *node = NodeOrNull(rng);
        std::vector<void *> out;
        FillBefore(out, rng);
        drawfx::Note("call", node ? node->id : 0);
        if (isOriginal)
            original(node, &out);
        else
            CollectShapes(node ? AsNode(*node) : nullptr, out);
        NoteVector(out);
    });
}

DIFF_TEST(SG_Utilities_AppendSideSwappedName, 0xae6900)
{
    using Fn = void(__cdecl *)(const char *, void *);
    const Fn original = ctx.Original<Fn>(0xae6900);
    static const char *const kNames2[] = {"l_arm", "r_leg", "l_", "r_", "l", "x", "", "L_arm", "l-arm", "lr_x", "r_l_a",
                                          "_l", "l_l_", "rl_x", "ab"};
    sgfx::CheckSides(ctx, "AppendSideSwappedName", 16, [](uint32_t, drawfx::Cells &) { gNameBuilder.Reset<kNameBuilder>(900); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 15; ++k) {
                             const char *name = kNames2[k];
                             if (rng() % 2) name = kNames2[rng() % 15];
                             drawfx::Note("call", k);
                             if (isOriginal)
                                 original(name, &gNameBuilder);
                             else
                                 AppendSideSwappedName(name, reinterpret_cast<cIGZUnknown *>(&gNameBuilder));
                         }
                     });
}

DIFF_TEST(SG_Utilities_IndexOfNamedPair, 0xaea840)
{
    using Fn = int32_t(__cdecl *)(void *, uint32_t, const char *, const char *);
    const Fn original = ctx.Original<Fn>(0xaea840);
    sgfx::CheckSides(ctx, "IndexOfNamedPair", 48, [](uint32_t, drawfx::Cells &) {},
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (int k = 0; k < 6; ++k) {
                             SetupEntries(rng);
                             Entry &e = gEntries[0];
                             const char *first = kFirstsAsked[rng() % 6], *second = kSecondsAsked[rng() % 4];
                             if (rng() % 2 && e.end.value[0]) {
                                 const Pair &p = e.pairs[rng() % e.end.value[0]];
                                 first = p.first;
                                 second = p.second;
                             }
                             const uint32_t key = rng() % 4;
                             drawfx::Note("call", {key, e.end.value[0], e.midPresent});
                             const int32_t index = isOriginal ? original(&e.fake, key, first, second)
                                                              : IndexOfNamedPair(reinterpret_cast<cIGZUnknown *>(&e.fake), key,
                                                                                 first, second);
                             drawfx::Note("index", uint32_t(index));
                             drawfx::Note("refs", {uint32_t(e.cur.refs), uint32_t(e.mid.refs), uint32_t(e.end.refs), e.cur.value[0]});
                         }
                     });
}

namespace {
uint32_t TableMode(Fake &f, const uint32_t *) { return f.value[0]; }
uint32_t TableSetMode(Fake &f, const uint32_t *a) { return f.value[0] = a[0], 0; }
uint32_t TableCount(Fake &, const uint32_t *) { return gEntryCount; }
uint32_t TableEntry(Fake &, const uint32_t *a) { return drawfx::P(&gEntries[a[0]].fake); }
uint32_t FrameworkCreate(Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[2])) = drawfx::P(&gTable);
    return 1;
}
void *StandFramework2() { return &gFramework2; }
} // namespace

DIFF_TEST(SG_Utilities_ApplyNamedPairs, 0xaeab90)
{
    using Fn = uint8_t(__cdecl *)(uint32_t, uint32_t, uint32_t, const char *, const char *, uint32_t, void *);
    const Fn original = ctx.Original<Fn>(0xaeab90);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework2};
    sgfx::CheckSides(ctx, "ApplyNamedPairs", 48, [](uint32_t, drawfx::Cells &) {},
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (int k = 0; k < 5; ++k) {
                             SetupEntries(rng);
                             gEntryCount = rng() % (kEntries + 1);
                             gFramework2.Reset<kFw>(950);
                             gFramework2.On(3, &FrameworkCreate);
                             gTable.Reset<kTableIface>(951);
                             gTable.value[0] = rng() % 4;
                             gTable.On(17, &TableSetMode);
                             gTable.On(19, &TableMode);
                             gTable.On(20, &TableCount);
                             gTable.On(21, &TableEntry);
                             const bool present = rng() % 2;
                             gCell = present ? reinterpret_cast<cIGZUnknown *>(&gTable) : nullptr;
                             const uint32_t mode = rng() % 4;
                             const char *first = kFirstsAsked[rng() % 6], *second = kSecondsAsked[rng() % 4];
                             if (rng() % 2 && gEntries[0].end.value[0]) {
                                 const Pair &p = gEntries[0].pairs[rng() % gEntries[0].end.value[0]];
                                 first = p.first;
                                 second = p.second;
                             }
                             const uint32_t a1 = rng(), a2 = rng(), value = rng();
                             drawfx::Note("call", {present, mode, gTable.value[0], gEntryCount, a1, a2, value});
                             const bool done = isOriginal ? original(a1, a2, mode, first, second, value, &gCell) != 0
                                                          : ApplyNamedPairs(a1, a2, mode, first, second, value, &gCell);
                             drawfx::Note("done", {done, gCell == reinterpret_cast<cIGZUnknown *>(&gTable), gTable.value[0]});
                             for (unsigned i = 0; i < kEntries; ++i) {
                                 const Entry &e = gEntries[i];
                                 drawfx::Note("refs", {uint32_t(e.cur.refs), uint32_t(e.mid.refs), uint32_t(e.end.refs), e.cur.value[0]});
                             }
                         }
                     });
}

// ---- lane 261111l: the helper block's leftovers (src/sg/SGUtilities.cpp, SGUtilitiesTree.cpp) ----

DIFF_TEST(SG_Utilities_ChannelSourceOf, 0xae43a0)
{
    using Fn = void *(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xae43a0);
    sgfx::CheckSides(ctx, "ChannelSourceOf", kSeeds,
                     [](uint32_t seed, drawfx::Cells &) {
                         Build(seed), BuildChannels();
                         // Each owner's slot 8 answers a word of its own (raw, not a pointer).
                         for (Ext &o : gc.owners)
                             o.faceFake.On(8, [](Fake &f, const uint32_t *) { return 0x5ce00000u | f.id; });
                     },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         const unsigned calls = 1 + rng() % 6;
                         for (unsigned k = 0; k < calls; ++k) {
                             gc.findMode = rng() % 4;
                             gc.queryMode = rng() % 4;
                             gc.owner = rng() % kOwners;
                             drawfx::Note("call", {gc.findMode, gc.queryMode, gc.owner});
                             const void *source = isOriginal ? original(&gc.animated) : ChannelSourceOf(&gc.animated);
                             drawfx::Note("source", drawfx::P(source));
                             NoteChannels();
                         }
                     });
}

namespace {
// A name object shaped like cRZString (vptr, std::string, count) whose slot 25 (Replace(pos, text, count)) is logged and
// applied to the text; the rest of its table is null (nothing else is called through it).
struct NameObject {
    const void *const *vt;
    std::string text;
    uint32_t refs;
};
struct NameReplacer {
    void *Replace(uint32_t pos, const char *with, uint32_t count)
    {
        NameObject *self = reinterpret_cast<NameObject *>(this);
        drawfx::Note("Replace", {pos, count});
        sgfx::NoteText("with", with);
        self->text.replace(pos, count, with, count);
        return this;
    }
};
template <typename PM> const void *MemberAddress(PM pm)
{
    static_assert(sizeof(PM) == sizeof(void *), "single-inheritance member pointer is a plain address");
    const void *p;
    std::memcpy(&p, &pm, sizeof p);
    return p;
}
const void *const *NameTable()
{
    static const void *table[34] = {};
    table[25] = MemberAddress(&NameReplacer::Replace);
    return table;
}

// Slot 33 GetParentConst: the node's parent in the tree (null for the root), found from the child links.
Fake *gParents[kMaxNodes];
uint32_t ParentOf(Fake &f, const uint32_t *) { return drawfx::P(gParents[IndexOf(f)]); }
void BuildParents()
{
    for (Fake *&p : gParents) p = nullptr;
    for (unsigned i = 0; i < gw.count; ++i) {
        gw.nodes[i].On(33, &ParentOf);
        for (uint32_t c = gw.nodes[i].value[kFirstChild]; c; c = reinterpret_cast<Fake *>(uintptr_t(c))->value[kNext])
            gParents[IndexOf(*reinterpret_cast<Fake *>(uintptr_t(c)))] = &gw.nodes[i];
    }
}
} // namespace

DIFF_TEST(SG_Utilities_SwapSidePrefix, 0xae68a0)
{
    using Fn = void(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xae68a0);
    static const char *const kNames3[] = {"l_arm", "r_leg", "L_x", "R_", "l_", "r_", "l", "r", "", "lr", "_l", "rl_x",
                                          "l-arm", "r_l_", "L_R_", "x_l_", "R_long name past the inline buffer"};
    constexpr unsigned kCount = sizeof(kNames3) / sizeof(kNames3[0]);
    sgfx::CheckSides(ctx, "SwapSidePrefix", 16, [](uint32_t, drawfx::Cells &) {},
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < kCount; ++k) {
                             NameObject name{NameTable(), kNames3[rng() % 2 ? k : rng() % kCount], 1};
                             const unsigned swaps = 1 + rng() % 2; // twice swaps back
                             drawfx::Note("call", {k, swaps});
                             for (unsigned s = 0; s < swaps; ++s) {
                                 if (isOriginal)
                                     original(&name);
                                 else
                                     SwapSidePrefix(reinterpret_cast<cRZString *>(&name));
                             }
                             sgfx::NoteText("name", name.text.c_str());
                             drawfx::Note("refs", name.refs);
                         }
                     });
}

DIFF_TEST(SG_Utilities_IsAncestorOf, 0xaeaae0)
{
    using Fn = bool(__cdecl *)(void *, void *);
    const Fn original = ctx.Original<Fn>(0xaeaae0);
    sgfx::CheckSides(ctx, "IsAncestorOf", kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed), BuildParents(); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 8; ++k) {
                             Fake &node = AnyNode(rng);
                             // An ancestor from the tree (itself among them), null, or a fake outside the tree.
                             const unsigned pick = rng() % 8;
                             Fake *ancestor = pick == 0 ? nullptr : pick == 1 ? &gw.framework : &AnyNode(rng);
                             if (pick == 2 && gParents[IndexOf(node)]) ancestor = gParents[IndexOf(node)];
                             drawfx::Note("call", {node.id, ancestor ? ancestor->id : 0});
                             const bool is = isOriginal ? original(ancestor, &node)
                                                        : IsAncestorOf(reinterpret_cast<cICompositionTreeNode *>(ancestor),
                                                                       AsNode(node));
                             drawfx::Note("is", is);
                         }
                     });
}

DIFF_TEST(SG_Utilities_NodeKindName, 0xaeab10)
{
    using Fn = const char *(__cdecl *)(uint32_t);
    const Fn original = ctx.Original<Fn>(0xaeab10);
    sgfx::CheckSides(ctx, "NodeKindName", 4, [](uint32_t, drawfx::Cells &) {}, [&](bool isOriginal, std::mt19937 &rng) {
        static const uint32_t kEdges[] = {9, 10, 11, 0xffffffffu, 0x80000000u, 0x7fffffffu, 0x100u, 0xfffffff6u};
        for (uint32_t kind = 0; kind < 13 + 8; ++kind) {
            const uint32_t k = kind < 13 ? kind : rng() % 2 ? kEdges[kind - 13] : uint32_t(rng());
            drawfx::Note("kind", k);
            sgfx::NoteText("name", isOriginal ? original(k) : NodeKindName(k));
        }
    });
}

DIFF_TEST(SG_Utilities_SetFlagInTree, 0xaeac50)
{
    using Fn = void(__cdecl *)(void *, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaeac50);
    Check(ctx, "SetFlagInTree", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        // Whole words: bits past 31 and values past one byte (the slot gets them as passed).
        const uint32_t bit = rng() % 4 ? rng() % 32 : uint32_t(rng());
        const uint32_t value = rng() % 3 == 0 ? 0 : rng() % 2 ? 1 : uint32_t(rng());
        drawfx::Note("call", {node.id, bit, value});
        if (isOriginal)
            original(&node, bit, value);
        else
            SetFlagInTree(AsNode(node), bit, value);
    });
}

namespace {
// ---- CollectTextureNames: each shape's binding cursors (slot 54: {cursor, middle, end}, middle and end counted fakes
// or null), the material manager (planted in 0x10f8fcc; slot 33 GetInfo answers a material by index) and materials
// (slot 25 count, slot 26 names from a pool). Names are interned by the string pool fake (sgfx::gPool).
constexpr unsigned kCursorSlotCount = 7;
constexpr std::array<Slot, kCursorSlotCount> CursorSlots()
{
    std::array<Slot, kCursorSlotCount> r{};
    for (unsigned i = 0; i < kCursorSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    r[4] = {1, Ret::Int, true, "AtEnd", "i"};
    r[5] = {1, Ret::Int, true, "Get", "i"};
    r[6] = {0, Ret::Int, true, "Next", ""};
    return r;
}
constexpr auto kCursorSlots = CursorSlots();
constexpr drawfx::Iface kCursor{"BindingCursor", kCursorSlotCount, kCursorSlots.data()};

constexpr unsigned kMatManagerSlotCount = 34;
constexpr std::array<Slot, kMatManagerSlotCount> MatManagerSlots()
{
    std::array<Slot, kMatManagerSlotCount> r{};
    for (unsigned i = 0; i < kMatManagerSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[33] = {2, Ret::Int, true, "GetInfo", "iu"};
    return r;
}
constexpr auto kMatManagerSlots = MatManagerSlots();
constexpr drawfx::Iface kMatManager{"MaterialManager", kMatManagerSlotCount, kMatManagerSlots.data()};

constexpr unsigned kMaterialSlotCount = 27;
constexpr std::array<Slot, kMaterialSlotCount> MaterialSlots()
{
    std::array<Slot, kMaterialSlotCount> r{};
    for (unsigned i = 0; i < kMaterialSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[25] = {0, Ret::Int, true, "CountTextures", ""};
    r[26] = {1, Ret::Int, true, "GetTextureName", "u"};
    return r;
}
constexpr auto kMaterialSlots = MaterialSlots();
constexpr drawfx::Iface kMaterial{"Material", kMaterialSlotCount, kMaterialSlots.data()};

constexpr unsigned kMaterials = 4, kMaxBindings = 4;
const char *const kTextureNames[] = {"wall_a", "WALL_A", "", "floor-b", "a texture name past the inline buffer", "x"};
constexpr unsigned kTextureNameCount = sizeof(kTextureNames) / sizeof(kTextureNames[0]);

enum : unsigned { kCursorPos, kCursorCount, kCursorFirstIndex }; // cursor words (indices from word 2)
struct Bindings {
    Fake cursor[kMaxNodes][kMaxShapes], middle[kMaxNodes][kMaxShapes], end[kMaxNodes][kMaxShapes];
    bool hasMiddle[kMaxNodes][kMaxShapes], hasEnd[kMaxNodes][kMaxShapes];
    Fake manager;
    Fake materials[kMaterials];
} gb;

// Shape slot 54 (out, subset): the shape's three cursors, the walk restarted.
uint32_t SubsetBindings(Fake &f, const uint32_t *a)
{
    const unsigned n = (f.id - 200) / 10, k = (f.id - 200) % 10;
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    gb.cursor[n][k].value[kCursorPos] = 0;
    out[0] = drawfx::P(&gb.cursor[n][k]);
    out[1] = gb.hasMiddle[n][k] ? drawfx::P(&gb.middle[n][k]) : 0;
    out[2] = gb.hasEnd[n][k] ? drawfx::P(&gb.end[n][k]) : 0;
    return a[0];
}
uint32_t BindingAtEnd(Fake &f, const uint32_t *) { return f.value[kCursorPos] >= f.value[kCursorCount]; }
// Slot 5 (out): the binding (word 0 a marker, word 1 the material index; words 2-3 left as they were).
uint32_t BindingGet(Fake &f, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = 0xb1000000u | (f.id << 4) | f.value[kCursorPos];
    out[1] = f.value[kCursorFirstIndex + f.value[kCursorPos]];
    return a[0];
}
uint32_t BindingNext(Fake &f, const uint32_t *) { return ++f.value[kCursorPos], 0; }
// Manager slot 33 (out, index): {hash, the index's material (by index modulo the four), 0, 0, 0}.
uint32_t MaterialInfoOf(Fake &, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = 0x4a500000u | a[1];
    out[1] = drawfx::P(&gb.materials[a[1] % kMaterials]);
    out[2] = out[3] = out[4] = 0;
    return a[0];
}
uint32_t MaterialName(Fake &f, const uint32_t *a) { return drawfx::P(kTextureNames[(f.value[1] + a[0]) % kTextureNameCount]); }

void BuildBindings(uint32_t seed, drawfx::Cells &cells)
{
    Build(seed);
    std::mt19937 rng(seed * 69069u + 5u);
    sgfx::gPool.Reset(cells);
    gb.manager.Reset<kMatManager>(600);
    gb.manager.On(33, &MaterialInfoOf);
    cells.Set(0x10f8fcc, &gb.manager);
    for (unsigned m = 0; m < kMaterials; ++m) {
        Fake &mat = gb.materials[m];
        mat.Reset<kMaterial>(610 + m);
        mat.value[0] = rng() % 4; // textures
        mat.value[1] = rng() % kTextureNameCount;
        mat.On(25, &Word<0>);
        mat.On(26, &MaterialName);
    }
    for (unsigned n = 0; n < gw.count; ++n) {
        gw.nodes[n].On(92, &Shapes);
        // Flag 4 (a shape-ref node) flipped on about half the nodes.
        if (rng() % 2) gw.nodes[n].value[kFlags] ^= 1u << 4;
        for (unsigned k = 0; k < gw.shapeCount[n]; ++k) {
            gw.shapes[n][k].On(54, &SubsetBindings);
            Fake &c = gb.cursor[n][k];
            c.Reset<kCursor>(700 + n * 10 + k);
            c.On(4, &BindingAtEnd);
            c.On(5, &BindingGet);
            c.On(6, &BindingNext);
            c.value[kCursorCount] = rng() % (kMaxBindings + 1);
            for (unsigned b = 0; b < kMaxBindings; ++b) c.value[kCursorFirstIndex + b] = rng() % 3 ? rng() % 9 : rng();
            gb.middle[n][k].Reset<kCursor>(800 + n * 10 + k);
            gb.end[n][k].Reset<kCursor>(1100 + n * 10 + k);
            gb.hasMiddle[n][k] = rng() % 3 != 0;
            gb.hasEnd[n][k] = rng() % 3 != 0;
        }
    }
}

void NoteBindings()
{
    for (unsigned n = 0; n < gw.count; ++n)
        for (unsigned k = 0; k < gw.shapeCount[n]; ++k)
            drawfx::Note("cursors", {uint32_t(gb.cursor[n][k].refs), uint32_t(gb.middle[n][k].refs),
                                     uint32_t(gb.end[n][k].refs), gb.cursor[n][k].value[kCursorPos]});
    sgfx::gPool.NoteCounts();
}
} // namespace

DIFF_TEST(SG_Utilities_CollectTextureNames, 0xae8090)
{
    using Fn = void(__cdecl *)(void *, void *);
    const Fn original = ctx.Original<Fn>(0xae8090);
    sgfx::CheckSides(ctx, "CollectTextureNames", kSeeds, &BuildBindings, [&](bool isOriginal, std::mt19937 &rng) {
        const unsigned calls = 1 + rng() % 3;
        for (unsigned k = 0; k < calls; ++k) {
            Fake &node = AnyNode(rng);
            {
                // The caller's vector: 0-2 earlier names and sometimes room reserved, so it grows inside the call (the
                // original's growth is the game's operator new and copy / close order; ours the same STL's).
                std::vector<CountedServiceCell> out;
                const unsigned before = rng() % 3;
                if (rng() % 2) out.reserve(1 + rng() % 12);
                for (unsigned i = 0; i < before; ++i) out.emplace_back(kTextureNames[rng() % kTextureNameCount]);
                drawfx::Note("call", {node.id, before, uint32_t(out.capacity())});
                if (isOriginal)
                    original(&out, &node);
                else
                    CollectTextureNames(&out, AsNode(node));
                drawfx::Note("size", {uint32_t(out.size()), uint32_t(out.capacity())});
                for (const CountedServiceCell &cell : out) drawfx::Note("name", cell.mValue);
                NoteBindings();
            } // the names closed here, in the vector's order
            NoteBindings();
            NoteState();
        }
    });
}
