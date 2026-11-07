// Differential tests of nGZSceneGraph::cViewerRefNodeRecursive (lane VF, scenegraph N3r): the game's recursive viewer
// reference (its factory 0xa6ac70, its vtables 0xe72eac / 0xe730a8 and the bodies behind them) against ours (Create, our
// vtables), each side building its own nodes. The node is cViewerRefNode's viewer base level with an Impl that has a
// vtable (node+0xc, its one slot FillViewport 0xa6a650) and materials of its own, so every template copy in its vtable is
// a body of its own (the composition part at node+0x10). Reused: the viewer world and its guards (sg_viewer_ref_fx.h),
// the persist tape and the base records (sg_viewer_ref_tape.h), the transform pilot (sg_transform_pilot.h, level 4).
//  - Pilot_Slots / Pilot_Events: the composition, object-graph, transform and bounded copies through the pilot's ops (the
//    tree links and moves, extensions, tags, flags, transforms and attributes 7 / 10, the limit, the holder and bound
//    slots, the events up the tree, Connect / Disconnect and the behaviour's 12 / 13).
//  - Factory: a fresh node's words, then let go of by Release (the deleting destructor 0xa6a330), the destructor 0xa6a130
//    against Destruct or the deleting destructor against DeletingDestructor; the constructor 0xa69f50 against Construct.
//    Under them the template levels' constructors 0xa69f30 / 0xa69ed0 / 0xa69e10 / 0xa69d30 / 0xa69e70 / 0xa69d90 /
//    0xa69db0 and destructors 0xa6a110 / 0xa6a0f0 / 0xa6a0b0 / 0xa6a010 / 0xa6a0d0 / 0xa6a030 / 0xa6a070, and the Impl's
//    constructor 0xa69f70 and destructor 0xa6a150 (the materials' display-list slots freed).
//  - Slots: QueryInterface 0xa6c160 (both faces), IsKindOf 0xa6cc20 (the Impl's 0xa6ba80), the type info through the +8
//    face's slot 4 (the Impl's 0xa6baa0), Accept, the viewer base's 87-122, the layers 77-86,
//    the materials 123 / 124 (0xa6c950 / 0xa6cbb0 -> 0xa6b980 / 0xa6b9c0), the events 3 / 5 / 6, the bound slots 71-75,
//    FillViewport through the Impl's vtable, the draw 82 and slot 51.
//  - Clone: 15 (0xa6a930, the Impl's CloneInto 0xa6bde0) with and without a clone context, in each mode.
//  - Read / Write / Persist: the Impl's Read 0xa6b750 and Write 0xa6bb80, directly or through the +8 face's slots 3 / 5
//    (0xa6c270 / 0xa6ceb0), and a round trip.
//  - The other copies in the vtables, each through our vtable on both sides (the pilot's ops and Slots):
//   0xa6a360 vf45 AddChild, 0xa6a390 vf22 AddExtension, 0xa6a3a0 vf82 Draw (-> the base Impl's CullAndDraw),
//   0xa6a3c0 vf77 AddLayer, 0xa6a3d0 vf78 AddLayerBits,
//   0xa6a3e0 vf51 MakeIterator51 (the composition family's empty attribute range), 0xa6a5c0 vf44 MakeIterator44,
//   0xa6a5e0 vf43 MakeIterator43, 0xa6a600 vf41/42 GetChildren(Const), 0xa6a620 vf104 GetClearInfo,
//   0xa6aa20 vf112 GetDepthSurface, 0xa6aa30 vf111 GetColorSurface, 0xa6aa40 vf114 UsesDeviceTargets,
//   0xa6ac30 vf6 AskListeners (the parent's OnQuery, else the listeners), 0xa6ace0 vf54 GetWorldTransform,
//   0xa6ae20 vf3 OnEvent (the behaviour's, then the base Impl's 0xae0c20),
//   0xa6ae60 vf4 slot 4 (-> shared 0x147110 / 0x147ce0), 0xa6aeb0 vf28 HasExtension, 0xa6bdd0 vf70 IsEverywhere,
//   0xa6be40 vf73 IntersectsBound, 0xa6be50 vf74 IntersectsLinear, 0xa6be60 vf75 FirstHitLinear,
//   0xa6beb0 vf71 GetBoundBoxIn, 0xa6bed0 vf72 GetBoundBox, 0xa6bef0 vf84 CountLayers,
//   0xa6bf00 vf105 GetTextureCount, 0xa6c110 vf58 FromParent34, 0xa6c120 vf59 FromParent1c,
//   0xa6c130 vf56 ToParent34, 0xa6c140 vf57 ToParent1c, 0xa6c150 vf24 FindExtension,
//   0xa6c192 +8 face slot 0 QueryInterface (this - 8), 0xa6c1a0 vf10 GetAttribute (-> 0xa3a9b0),
//   0xa6c2f0 vf2 Release (-> shared 0x360650), 0xa6c2f5 +8 face slot 2 Release (this - 8),
//   0xa6c300 vf46 RemoveChild, 0xa6c310 vf23 RemoveExtension, 0xa6c320 vf79 RemoveLayer,
//   0xa6c330 vf80 RemoveLayerBits, 0xa6c340 vf86 GetLayerState, 0xa6c360 vf85 MakeLayerNames,
//   0xa6c380 vf116 the +0xa8 reference, 0xa6c390 vf117 GetRenderStrategy, 0xa6c3a0 vf110 GetTargetSize,
//   0xa6c3c0 vf107 GetTextureRef94, 0xa6c3d0 vf106 GetTexture, 0xa6c3e0 vf113 IsDeviceViewMode,
//   0xa6c3f0 vf109 GetHandleTexture, 0xa6c400 vf108 the texture handle,
//   0xa6c410 +8 face slot 6 GetOwnerInfo (the parent's face), 0xa6c740 vf32 GetFromParent32,
//   0xa6c760 vf31 GetFromParent31, 0xa6c8e0 vf61 SetLimit, 0xa6c8f0 vf91 Slot91 (the clear record),
//   0xa6c920 vf96 SetSurfaces, 0xa6c930 vf66 SetBoundFlag, 0xa6c940 vf29 SetFlag, 0xa6c960 vf65 SetRotation,
//   0xa6c9a0 vf67 SetHolder, 0xa6c9c0 vf98 SetLayerTagMatch, 0xa6c9d0 vf99 SetRefA8, 0xa6c9e0 vf100 SetRefA4,
//   0xa6c9f0 vf92 SetExplicit, 0xa6ca10 vf93 SetTextures, 0xa6ca40 vf94 SetTexture, 0xa6ca50 vf95 SetTextureByName,
//   0xa6ca80 vf19 SetTag (-> 0xa349d0), 0xa6ca90 vf102 SetBound, 0xa6caa0 vf101 SetBoundAndPlane,
//   0xa6cac0 vf63 SetTransform, 0xa6cad0 vf62 SetTransformParts, 0xa6cb10 vf64 SetTranslation,
//   0xa6cb50 vf47 SetLink, 0xa6cc30 vf27 GetTagHandle, 0xa6cc40 vf26 GetTag (-> 0xa34bb0), 0xa6cc60 vf48 TestFlag,
//   0xa6cc80 vf53 GetLocalTransform, 0xa6cd60 +8 face slot 4 GetTypeInfo, 0xa6cdc0 vf60 OnContentChanged,
//   0xa6cdd0 vf7 SetAttribute (-> 0xa3acb0).
// Left out of the comparison: the vtable words 0 and 2 and the Impl's (3: checked through FillViewport), word 54 but its
// byte (the +0xc8 byte of the viewer base and three bytes nobody writes) and the words past +0xf8 (the materials'
// vector: heap addresses, the materials noted on their own; the display-list slot +0x104, which the constructor never
// writes, planted alike on both sides and noted; the slot count). The detail limit (word 30) the constructor never
// writes either: planted.
#include "sg_renderable_fx.h"
#include "sg_transform_pilot.h"
#include "sg_viewer_ref_fx.h"
#include "sg_viewer_ref_tape.h"
#include "sgfx.h"
#include "ts_services_fixture.h"

#include "sg/cNullArrayAttributeEntryIterator.h"
#include "sg/cRenderGroupNameIterator.h"
#include "sg/cTransformNode.h"
#include "sg/cViewerRefNode.h"
#include "sg/cViewerRefNodeRecursive.h"

#include <bit>
#include <cstring>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kFactory = 0xa6ac70;
constexpr uint32_t kConstruct = 0xa69f50;
constexpr uint32_t kDestruct = 0xa6a130;
constexpr uint32_t kDeleting = 0xa6a330;
constexpr uint32_t kRead = 0xa6b750;
constexpr uint32_t kWrite = 0xa6bb80;
constexpr uint32_t kSetWordC4 = 0xa6ca60; // slot 97, hooked
constexpr uint32_t kSize = cViewerRefNodeRecursive::kSize;
constexpr uint32_t kNoted = 0xf8; // the words compared whole (the rest: NoteRecursive)
constexpr uint64_t kSkipWords = 0b1101ull | 1ull << 54;
constexpr uint32_t kEventCell = 0x10f6424;
constexpr uint32_t kMaterialCell = 0x10f8fcc; // GetCachedQueriedObject's cache: the material manager
constexpr uint32_t kViewTag = 0x79000000;
constexpr uint32_t kBase = 0x10;   // the viewer base Impl in the node
constexpr unsigned kLimitWord = 30; // the renderable Impl's detail limit (+0x68 of the base)
constexpr unsigned kSlotWord = 65;  // the Impl's display-list slot (+0xf8)
constexpr unsigned kCountWord = 66; // its count (+0xfc)

constexpr auto kListSlots = svx::Sparse<44>({{19, {3, Ret::Int, true, "ReallocSlots", "uuu"}},
                                             {20, {2, Ret::Int, true, "FreeSlots", "uu"}},
                                             {21, {0, Ret::Int, true, "BeginScene", ""}},
                                             {22, {0, Ret::Int, true, "EndScene", ""}},
                                             {23, {7, Ret::Int, true, "DrawViewer", "aF2F2F2W13F16F16"}},
                                             {24, {3, Ret::Int, true, "DrawPass", "u@0x1u"}},
                                             {27, {2, Ret::Int, true, "DrawNode", "au"}},
                                             {43, {0, Ret::Int, true, "GetFrame", ""}}});
constexpr drawfx::Iface kList{"DisplayList", 44, kListSlots.data()};
constexpr auto kMaterialSlots = svx::Sparse<34>({{19, {1, Ret::Int, true, "FindOrLoad", "s"}},
                                                 {20, {1, Ret::Int, true, "AddRefIndex", "u"}},
                                                 {21, {1, Ret::Int, true, "ReleaseIndex", "u"}},
                                                 {33, {2, Ret::Int, true, "GetInfo", "iu"}}}); // (out, index)
constexpr drawfx::Iface kMaterials{"MaterialManager", 34, kMaterialSlots.data()};
constexpr auto kContextSlots = svx::Sparse<4>({{3, {3, Ret::Int, true, "FindClone", "aua"}}});
constexpr drawfx::Iface kContext{"CloneContext", 4, kContextSlots.data()};
constexpr auto kStageSlots = svx::Sparse<94>({{93, {1, Ret::Int, true, "SetPass", "a"}}});
constexpr drawfx::Iface kStage{"Stage", 94, kStageSlots.data()};
constexpr auto kSettingsSlots = svx::Sparse<10>({{9, {2, Ret::Int, true, "GetProperty", "ii"}}});
constexpr drawfx::Iface kSettings{"Settings", 10, kSettingsSlots.data()};

struct World {
    sgfx::Nodes nodes;
    Fake traversal, context, viewList, pass, materials;
    Fake stage, parent, settings; // the cull's: the parent's stage (slot 32 -> its slot 93), the configuration
    Fake listener; // answers every query
    sgfx::Manager manager;
    uint32_t frames[4] = {};
    unsigned frameAt = 0, slotCalls = 0;
    unsigned drawn = 0; // DrawViewer calls (the cull test's coverage)
    const char *infoNames[8] = {}; // the material names GetInfo answers (by a hash of the index)
};
World w;
// The view the draws and culls read: the cull reads the projection past what cViewerView names (+0x144).
struct alignas(16) BigView {
    cViewerView view;
    float projection[16];
};
static_assert(offsetof(BigView, projection) == 0x144);
BigView gBig;
cViewerView &gView = gBig.view;

// A node's range tag: 0x10c-byte nodes need more than sgfx's 0x100 per index.
uint32_t NodeTag(unsigned i) { return sgfx::kNodeTag | i << 12; }

cICompositionTreeNode *__cdecl MakeOurs() { return cViewerRefNodeRecursive::Create(); }
cICompositionTreeNode *__cdecl MakeByConstruct()
{
    return reinterpret_cast<cViewerRefNodeRecursive *>(::operator new(kSize))->Construct();
}
cIViewerRefNodeRecursive *As(cICompositionTreeNode *n)
{
    return static_cast<cIViewerRefNodeRecursive *>(static_cast<cIRenderableNode *>(n));
}
uint32_t CountOf(const cICompositionTreeNode *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }
uint32_t *Words(cICompositionTreeNode *n) { return reinterpret_cast<uint32_t *>(n); }
cIGZUnknown *Held(unsigned i) { return reinterpret_cast<cIGZUnknown *>(&svx::gVw.held[i]); }
// The words the constructor leaves as they were: planted alike on both sides.
void Plant(cICompositionTreeNode *n, std::mt19937 &rng)
{
    Words(n)[kLimitWord] = rng() % 2 ? 0x7fffffffu : uint32_t(rng());
    Words(n)[kSlotWord] = rng() % 2 ? 0xffffffffu : uint32_t(rng() % 64);
}

// ---- the fakes' handlers ----

// ReallocSlots: a new run of slots for a non-zero count, else none.
uint32_t ListRealloc(Fake &, const uint32_t *a) { return a[2] ? 0x100u + 0x10u * (w.slotCalls++ % 64) : 0xffffffffu; }
uint32_t ListFrame(Fake &, const uint32_t *) { return w.frames[w.frameAt++ % 4]; }
uint32_t ListDrawViewer(Fake &, const uint32_t *)
{
    ++w.drawn;
    return P(&w.pass);
}
void MakeList(Fake &f, uint32_t id)
{
    f.Reset<kList>(id);
    f.On(19, &ListRealloc);
    f.On(23, &ListDrawViewer);
    f.On(43, &ListFrame);
}
uint32_t MaterialFind(Fake &, const uint32_t *a)
{
    const char *name = reinterpret_cast<const char *>(uintptr_t(a[0]));
    return name[0] == 'x' ? 0xffffffffu : 40u + uint32_t(std::strlen(name));
}
// GetInfo(out, index): the record's first word a name (or none), the rest made from the index.
uint32_t MaterialInfoOut(Fake &, const uint32_t *a)
{
    uint32_t *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = P(w.infoNames[(a[1] * 0x9e3779b1u) >> 29]);
    for (uint32_t i = 1; i < 5; ++i) out[i] = a[1] * 0x01010101u + i;
    return a[0];
}
// FindClone: the prepared node for the recursive interface, value[1] (the held bound fake, or none) for anything else.
uint32_t ContextFind(Fake &self, const uint32_t *a)
{
    void *clone = reinterpret_cast<void *>(uintptr_t(a[1] == cViewerRefNodeRecursive::kIid ? self.value[0] : self.value[1]));
    if (clone) static_cast<cIGZUnknown *>(clone)->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[2])) = clone;
    return 0;
}

// The fakes both the pilot's world and the viewer tests' need: the viewer world, the render-group registry and the
// detail object (the display list the materials' slots come from), the material manager.
void BuildViewer(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed ^ 0x5eedu);
    svx::Build(seed, cells);
    svx::gVw.bound.Settle<8, 3>("uau");
    svx::gVw.bound.Settle<9, 3>("uau");
    svx::gVw.conn.Settle<12, 4>("uuau");
    w.materials.Reset<kMaterials>(707);
    w.materials.On(19, &MaterialFind);
    w.materials.On(33, &MaterialInfoOut);
    static const char *const kInfoNames[] = {nullptr, "plain", "*abc!def", "*nobang", "", "*!", "*grp!*x!y",
                                             "##0x1c0532fa!mat_c"};
    for (const char *&name : w.infoNames) name = kInfoNames[rng() % 8];
    cells.Set(kMaterialCell, static_cast<const void *>(&w.materials));
    for (uint32_t &f : w.frames) f = rng() % 4 ? rng() % 3 : uint32_t(rng());
    w.frameAt = w.slotCalls = 0;
    sgrx::Setup(rng);
    MakeList(sgrx::Registry::Get().detail, 702);
}

// The materials' state: the slot word, the count, the material indices.
void NoteRecursive(const char *label, cICompositionTreeNode *n)
{
    const uint32_t *words = Words(n);
    drawfx::Call c;
    c.note = label;
    c.words.push_back(words[54] & 0xff);
    c.words.push_back(words[kSlotWord]);
    c.words.push_back(words[kCountWord]);
    const uint32_t *begin = reinterpret_cast<const uint32_t *>(uintptr_t(words[62]));
    const uint32_t count = (words[63] - words[62]) / 4;
    c.words.push_back(count);
    for (uint32_t i = 0; i < count && i < 16; ++i) c.words.push_back(begin[i]);
    drawfx::gLog.push_back(std::move(c));
}

// ---- the pilot (level 4: a bounded node of another family; see sg_transform_pilot.h) ----

struct RecursiveCfg {
    using Interface = cIViewerRefNodeRecursive;
    static constexpr uint32_t kFactory = ::kFactory;
    static constexpr uint32_t kSize = kNoted; // (the words compared: the rest are noted by NoteNode)
    static constexpr int kLevel = 4;
    static constexpr uint32_t kIid = cViewerRefNodeRecursive::kIid;
    static constexpr uint32_t kEventCell = ::kEventCell;
    static constexpr uint64_t kSkipWords = ::kSkipWords;
    using Scope = svx::Stands;
    static cICompositionTreeNode *Create() { return cViewerRefNodeRecursive::Create(); }
    static void SetupCells(uint32_t seed, drawfx::Cells &cells)
    {
        BuildViewer(seed, cells);
        // (svx::Build's world took the name pool over: the pilot's two names again)
        sgfx::gPool.Reset(cells);
        sgfx::gPool.Handle("alpha");
        sgfx::gPool.Handle("beta");
    }
    static void Prepare(cICompositionTreeNode *n, std::mt19937 &rng) { Plant(n, rng); }
    static void NoteNode(const char *label, cICompositionTreeNode *n) { NoteRecursive(label, n); }
};
using RecursivePilot = sgtp::Pilot<RecursiveCfg>;

// ---- the viewer tests' world ----

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gResources.Reset(cells);
    BuildViewer(seed, cells);
    sgfx::MakeTraversal(w.traversal, 701, rng() % 3);
    MakeList(w.viewList, 703);
    w.pass.Reset<sgfx::kUnknown>(704);
    w.context.Reset<kContext>(708);
    w.context.On(3, &ContextFind);
    std::memset(&gBig, 0, sizeof(gBig));
    gView.mDrawer = &w.viewList;
    drawfx::AddRange(&gBig, sizeof(gBig), kViewTag);
    w.nodes = sgfx::Nodes{};
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, kEventCell);
    sgfx::MakeListener(w.listener, 503, true);
    cells.Set(cTransformNodeBehaviorTraits::kEventManagerCell, w.manager.manager);
}

void NoteNodes(const char *label)
{
    w.nodes.NoteShape(label);
    w.nodes.NoteWords(label, kSkipWords);
    for (unsigned i = 0; i < w.nodes.count; ++i) NoteRecursive(label, w.nodes[i]);
}

void Hold(cICompositionTreeNode *n, std::mt19937 &rng)
{
    n->AddRef();
    drawfx::AddRange(n, kSize, NodeTag(w.nodes.count));
    w.nodes.at[w.nodes.count++] = n;
    Plant(n, rng);
}
// A clone and its subtree (mode 1 clones the children too), the test's reference being the clone's own.
void Adopt(cICompositionTreeNode *n, bool top = true)
{
    if (w.nodes.count >= sgfx::Nodes::kMax) return;
    drawfx::AddRange(n, kSize, NodeTag(w.nodes.count));
    w.nodes.at[w.nodes.count++] = n;
    if (!top) n->AddRef();
    Words(n)[kLimitWord] = 0x1234;
    Words(n)[kSlotWord] = 0x4321;
    for (cICompositionTreeNode *c = n->GetFirstChild(); c; c = c->GetNextSibling()) Adopt(c, false);
}

void MakeTree(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    const bool construct = rng() % 2;
    const sgfx::Factory make = original ? ctx.Original<sgfx::Factory>(kFactory) : construct ? &MakeByConstruct : &MakeOurs;
    const unsigned n = 1 + rng() % 4;
    int parents[sgfx::Nodes::kMax];
    parents[0] = -1;
    for (unsigned i = 1; i < n; ++i) parents[i] = rng() % 3 ? int(rng() % i) : -1;
    w.nodes.count = n;
    w.nodes.size = kNoted;
    for (unsigned i = 0; i < n; ++i) {
        w.nodes.at[i] = make();
        w.nodes.at[i]->AddRef();
        drawfx::AddRange(w.nodes.at[i], kSize, NodeTag(i));
    }
    w.nodes.Link(parents);
    for (unsigned i = 0; i < n; ++i) Plant(w.nodes[i], rng);
}

void NoteFakes()
{
    for (const Fake *f : {&w.pass, &w.materials, &w.viewList}) drawfx::Note("fake refs", {f->id, uint32_t(f->refs)});
    svx::NoteExtras();
}

void NoteFreed(difftest::FreeRecorder &freed)
{
    size_t sizes[64];
    const size_t n = freed.SortedSizes(sizes, 64);
    drawfx::Call c;
    c.note = "freed";
    for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
    drawfx::gLog.push_back(std::move(c));
}

void ReleaseAll()
{
    difftest::FreeRecorder freed;
    w.nodes.Release();
    freed.Stop();
    NoteFreed(freed);
    NoteFakes();
}

void NoteIdRange(nGZEvent::cAttributeIDRange range)
{
    unsigned steps = 0;
    while (steps < 12 && !range.mCurrent->IsEqual(range.mEnd)) {
        drawfx::Note("id", range.mCurrent->Get());
        range.mCurrent->Next();
        ++steps;
    }
    nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
}

void Mark(difftest::Context &ctx, int k, unsigned op)
{
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
}

float Fraction(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return float(int(rng() % 9) - 2) * 0.25f;
    case 1: return svx::WildFloat(rng);
    case 2: return 0.0f;
    case 3: return 1.0f;
    default: return float(rng() % 1001) / 1000.0f;
    }
}
float Any(std::mt19937 &rng) { return rng() % 6 ? sgtx::RandomFloat(rng) : svx::WildFloat(rng); }
// Where the original dereferences null (the viewer base at node +0x10): sg_viewer_ref_fx.h.
bool TargetSafe(cICompositionTreeNode *n) { return svx::TargetSafe(n, kBase); }
bool HandleSafe(cICompositionTreeNode *n) { return svx::HandleSafe(n, kBase); }

const char *const kMaterialNames[] = {nullptr, "xmissing", "lambert", "glass_material"};

void OperateMaterials(cICompositionTreeNode *n, std::mt19937 &rng)
{
    cIViewerRefNodeRecursive *s = As(n);
    if (rng() % 3 == 0) {
        const uint32_t count = rng() % 6 ? rng() % 4 : 5;
        drawfx::Note("material count", count);
        s->SetMaterialCount(count);
    } else {
        const uint32_t index = rng() % 5;
        drawfx::Note("material", index);
        s->SetMaterial(index, kMaterialNames[rng() % 4]);
    }
}

void NoteNames(cRenderGroupNameRange &range)
{
    cIRenderGroupNameIterator *it = range.mCurrent;
    for (unsigned steps = 0; steps < 40 && !it->IsEqual(range.mEnd); ++steps) {
        sgfx::NoteText("layer", it->Get() ? it->Get() : "(null)");
        it->Next();
    }
    drawfx::Note("names at end", uint8_t(it->IsEqual(range.mEnd)));
    for (cIRenderGroupNameIterator *i : {range.mCurrent, range.mBegin, range.mEnd}) drawfx::Note("released", i->Release());
}

// ---- the slots ----

constexpr unsigned kSlotOps = 23;

void OperateSlots(difftest::Context &ctx, bool original, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    cICompositionTreeNode *n = ns[rng() % ns.count];
    cIViewerRefNodeRecursive *s = As(n);
    const unsigned op = rng() % kSlotOps;
    Mark(ctx, k, op);
    switch (op) {
    case 0: { // QueryInterface, on either face
        const uint32_t iids[] = {cViewerRefNodeRecursive::kIid, cViewerRefNodeRecursive::kBaseIid, cViewerRefNode::kIid,
                                 sgfx::kIidTree, sgfx::kIidGraph, 0xdb01d4b2, 0x5b0aaa74, 0xbb11a53e, 1, 0xe51c10f5,
                                 0x7d14eb66, 0x7d3a2385, 0x12345678};
        const uint32_t iid = iids[rng() % 13];
        void *out = nullptr;
        const uint32_t before = CountOf(n);
        cIGZUnknown *const face8 = reinterpret_cast<cIGZUnknown *>(reinterpret_cast<char *>(n) + 8);
        cIGZUnknown *const via = rng() % 4 ? static_cast<cIGZUnknown *>(n) : face8;
        const bool r = rng() % 8 ? via->QueryInterface(iid, &out) : via->QueryInterface(iid, nullptr);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out)), CountOf(n) - before});
        if (CountOf(n) != before) via->Release();
        break;
    }
    case 1: { // kinds
        const uint32_t iids[] = {cViewerRefNodeRecursive::kIid, cViewerRefNodeRecursive::kBaseIid, cViewerRefNode::kIid,
                                 sgfx::kIidTree, sgfx::kIidGraph, 0x3b253dc7, 0xbb11a53e, 0x5b0aaa74, 1, 0, 0xdb01d4b2,
                                 0xe51c10f5, 0x7d14eb66, 0x9c5ed164};
        for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
        break;
    }
    case 2: { // traversals, slot 70
        auto *t = reinterpret_cast<cICompositionTreeTraversal *>(&w.traversal);
        drawfx::Note("accept", {n->Accept(t), n->AcceptReadOnly(t), uint8_t(s->IsEverywhere())});
        break;
    }
    case 3: { // the registry: 12, 13, 14
        drawfx::Note("events", n->GetEventCount());
        NoteIdRange(n->GetEventIds());
        const uint32_t ids[] = {sgfx::kRegistryIds[0], sgfx::kRegistryIds[1], sgfx::kRegistryIds[2], uint32_t(rng())};
        const nGZEvent::cEventAttribute a = n->GetEventAttribute(ids[rng() % 4]);
        drawfx::Note("attribute", {a.mId, a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        sgfx::NoteText("attribute name", a.mName ? a.mName : "(null)");
        break;
    }
    case 4: { // flags, a node moved
        uint32_t bit = rng() % 40;
        if ((bit & 31) == 1 || (bit & 31) == 6 || (bit & 31) == 12) bit = 0;
        n->SetFlag(bit, rng() % 2);
        drawfx::Note("flags", {uint8_t(n->TestFlag(bit)), n->GetFlags()});
        cICompositionTreeNode *c = ns[rng() % ns.count];
        if (cICompositionTreeNode *p = c->GetParent()) p->RemoveChild(c);
        cICompositionTreeNode *to = ns[rng() % ns.count];
        bool inside = false;
        for (cICompositionTreeNode *a = to; a; a = a->GetParent()) inside |= a == c;
        if (!inside) to->AddChild(c);
        break;
    }
    case 5: { // the type (the +8 face's slot 4)
        cPersistTypeInfo info = n->GetReferent()->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 6: { // 87-90, 97, 98
        if (rng() % 2) s->Slot90(uint32_t(rng()));
        if (rng() % 2) s->Slot97(uint32_t(rng()));
        if (rng() % 3 == 0) { // 97's body called directly, as cTSFenceEntry does (hooked: 0xa6ca60 against SetWordC4)
            const uint32_t value = uint32_t(rng());
            if (original) ctx.Original<void(__thiscall *)(void *, uint32_t)>(kSetWordC4)(n, value);
            else reinterpret_cast<cViewerRefNodeRecursive *>(n)->SetWordC4(value);
        }
        if (rng() % 2) s->SetLayerTagMatch(uint32_t(rng()));
        Words(n)[kLimitWord] = rng() % 2 ? 0x7fffffffu : uint32_t(rng());
        drawfx::Note("words", {uint32_t(s->GetLayerTagMatch()), s->Slot88(), s->Slot89(), s->GetRenderType(), s->Slot120()});
        break;
    }
    case 7: { // 91 / 104
        if (rng() % 2) {
            cViewerClearInfo info{uint32_t(rng() % 4), {Any(rng), Any(rng), Any(rng), Any(rng)}, Any(rng), uint32_t(rng())};
            s->Slot91(&info);
        }
        cViewerClearInfo out;
        std::memset(&out, 0x5c, sizeof out);
        s->GetClearInfo(&out);
        drawfx::Call c;
        c.note = "clear";
        const uint32_t *words = reinterpret_cast<const uint32_t *>(&out);
        for (unsigned i = 0; i < 7; ++i) c.words.push_back(words[i]);
        drawfx::gLog.push_back(std::move(c));
        break;
    }
    case 8: { // 99, 100, 103 and their getters
        cIGZUnknown *const pick[] = {nullptr, Held(rng() % 12), Held(rng() % 12)};
        if (rng() % 2) s->Slot99(pick[rng() % 3]);
        if (rng() % 2) s->Slot100(pick[rng() % 3]);
        if (rng() % 2) s->Slot103(pick[rng() % 3]);
        drawfx::Note("refs", {drawfx::Norm(P(s->Slot116())), drawfx::Norm(P(s->GetRenderStrategy())),
                              drawfx::Norm(s->GetTextureMode()), drawfx::Norm(P(s->GetFrameCacheHolder()))});
        break;
    }
    case 9: { // the textures 92-96
        const unsigned which = rng() % 5;
        drawfx::Note("texture", which);
        if (which == 0) s->Slot92(rng() % 3 ? Held(rng() % 12) : nullptr, rng() % 2 ? Held(rng() % 12) : nullptr, Any(rng));
        if (which == 1) {
            cIGZUnknown *textures[4];
            for (cIGZUnknown *&t : textures) t = rng() % 4 ? Held(rng() % 12) : nullptr;
            s->Slot93(rng() % 5, textures, rng() % 2 ? Held(rng() % 12) : nullptr, Any(rng));
        }
        if (which == 2) s->Slot94(rng() % 3 ? 1 + rng() % 9 : 0xffffffffu);
        if (which == 3) s->Slot95(rng() % 4 ? (rng() % 2 ? "xnone" : "target") : nullptr, rng() % 2);
        if (which == 4) s->Slot96(rng() % 3 ? Held(rng() % 12) : nullptr, rng() % 3 ? Held(rng() % 12) : nullptr);
        break;
    }
    case 10: { // the texture getters 105-114 (but where the original dereferences null: see TargetSafe, HandleSafe)
        if (TargetSafe(n)) {
            cViewerTargetSize size{0x5c5c5c5c, 0x5c5c5c5c, 0};
            const void *r = s->GetTargetSize(&size);
            drawfx::Note("target", {uint8_t(r == &size), size.mWidth, size.mHeight, std::bit_cast<uint32_t>(size.mScale)});
        }
        if (HandleSafe(n))
            drawfx::Note("handle textures",
                         {s->GetTextureCount(), drawfx::Norm(P(s->GetTexture(int32_t(rng() % 4)))),
                          drawfx::Norm(P(s->GetTextureRef94()))});
        drawfx::Note("textures", {s->Slot108(), drawfx::Norm(P(s->GetHandleTexture())),
                                  drawfx::Norm(P(s->GetColorSurface())), drawfx::Norm(P(s->GetDepthSurface())),
                                  uint8_t(s->Slot113()), uint8_t(s->Slot114())});
        break;
    }
    case 11: { // the bound 101 / 102 and its getters 118-120
        cIBoundedNode *const bound = rng() % 3 ? reinterpret_cast<cIBoundedNode *>(&svx::gVw.bound) : nullptr;
        if (rng() % 2) {
            float plane[4];
            for (float &f : plane) f = Any(rng);
            s->Slot101(bound, plane, uint32_t(rng()));
        } else {
            s->Slot102(bound);
        }
        const float *plane = s->Slot119();
        drawfx::Note("bound", {drawfx::Norm(P(s->Slot118())), s->Slot120(), std::bit_cast<uint32_t>(plane[0]),
                               std::bit_cast<uint32_t>(plane[3])});
        break;
    }
    case 12: // the materials 123 / 124
    case 13:
        OperateMaterials(n, rng);
        NoteRecursive("materials", n);
        break;
    case 14: { // the layers by bits (78, 80, 81, 84, 86)
        const uint32_t mask = sgrx::RandomMask(rng);
        if (rng() % 2) s->AddLayerBits(mask);
        else s->RemoveLayerBits(mask);
        if (rng() % 3 == 0) s->SetLayerTag(uint32_t(rng()));
        uint32_t state[3] = {0x5555, 0x6666, 0x7777};
        const uint32_t *r = s->GetLayerState(state);
        drawfx::Note("layers", {uint8_t(r == state), state[0], state[1], state[2], s->CountLayers()});
        break;
    }
    case 15: { // the layers by name (77, 79) and their names (85)
        static const char *const kNames[] = {"alpha", "glowb", "shadowc", "zeta"};
        const char *name = kNames[rng() % 4];
        if (rng() % 2) s->AddLayer(name);
        else s->RemoveLayer(name);
        uint32_t state[2] = {};
        s->GetLayerState(state);
        drawfx::Note("layers", {state[0], state[1], s->CountLayers()});
        cRenderGroupNameRange range{};
        drawfx::Note("names returned", uint8_t(s->MakeLayerNames(&range) == &range));
        NoteNames(range);
        break;
    }
    case 16: // an event told (3): from the held bound or not; the connection's body naming this node or not
    case 17: { // the listeners told (5) or asked (6)
        const uint32_t types[] = {0xa884a15, nGZEvent::cConnectionChangedEvent::kType, cBoundedNodeImpl::kBoundChangedEvent,
                                  0x12345678};
        const uint32_t type = types[rng() % 4];
        const uint32_t targetEvents[] = {0xa884a15, cBoundedNodeImpl::kBoundChangedEvent, 7};
        nGZEvent::cConnectionChangedEvent body{};
        body.mDisconnected = rng() % 4 ? 1 : uint32_t(rng() % 3);
        body.mTarget = reinterpret_cast<nGZEvent::cIAbstractBehaviorType *>(rng() % 4 ? static_cast<void *>(n)
                                                                                      : static_cast<void *>(&svx::gVw.dev));
        body.mTargetEvent = targetEvents[rng() % 3];
        drawfx::Note("event", {type, body.mDisconnected, uint8_t(P(body.mTarget) == P(n) ? 1 : 0), body.mTargetEvent});
        if (op == 16) {
            n->OnEvent(type, uint32_t(rng()), &body, rng() % 4 ? P(&svx::gVw.bound) : P(&svx::gVw.dev));
        } else if (rng() % 2) {
            // Sometimes this node is its parent's held bound (the viewer base's +0xac): planted for the call only.
            cICompositionTreeNode *const parent = n->GetParent();
            uint32_t *const held = parent && rng() % 2 ? &Words(parent)[(kBase + 0xac) / 4] : nullptr;
            const uint32_t was = held ? *held : 0;
            if (held) {
                *held = P(s);
                n->SetFlag(0xb, rng() % 2 != 0);
                n->SetFlag(0xd, rng() % 2 != 0);
            }
            drawfx::Note("planted", uint8_t(held != nullptr));
            n->NotifyListeners(type, uint32_t(rng()), &body);
            if (held) {
                drawfx::Note("parent", {*held == P(s) ? 1u : 0u, As(parent)->GetFlags()});
                *held = was;
            }
        } else {
            if (rng() % 2) w.manager.Listen(ns[rng() % ns.count], type, w.listener, 0x500 + rng() % 4);
            drawfx::Note("asked", uint8_t(n->AskListeners(type, uint32_t(rng()), &body)));
        }
        drawfx::Note("after", {drawfx::Norm(P(s->Slot118())), n->GetFlags()});
        break;
    }
    case 18: { // the bound slots 71-75 (the held bound answers; with none, the node's own box)
        float frame[13]; // a whole cTransform34 (the slot reads all 13 words)
        for (float &f : frame) f = Fraction(rng);
        const cBoundBox box = rng() % 2 ? s->GetBoundBox() : s->GetBoundBoxIn(reinterpret_cast<const cTransform34 *>(frame));
        drawfx::Call c;
        c.note = "box";
        const uint32_t *words = reinterpret_cast<const uint32_t *>(&box);
        for (unsigned i = 0; i < 6; ++i) c.words.push_back(words[i]);
        drawfx::gLog.push_back(std::move(c));
        float t = rng() % 4 ? 0.5f * float(rng() % 13) : 3.0e38f;
        const bool hit = s->FirstHitLinear(&t, nullptr);
        drawfx::Note("bound slots", {uint8_t(s->IntersectsBound(uint32_t(rng()), uint32_t(rng()))),
                                     uint8_t(s->IntersectsLinear(nullptr)), uint8_t(hit), std::bit_cast<uint32_t>(t)});
        break;
    }
    case 19: { // FillViewport, through the Impl's vtable (node+0xc)
        if (!TargetSafe(n)) break;
        float out[16];
        std::memset(out, 0x5c, sizeof out);
        using Fn = float *(__thiscall *)(void *, float *);
        void *const impl = reinterpret_cast<uint8_t *>(n) + 0xc;
        const float *r = (*reinterpret_cast<Fn *const *>(impl))[0](impl, out);
        drawfx::Note("viewport returned", uint8_t(r == out));
        sgtx::NoteFloats("viewport", out, 16);
        break;
    }
    case 20: { // 82
        gView.mNode = rng() % 6 ? nullptr : static_cast<const void *>(n);
        s->Draw(&gView, rng() % 3 ? 0x3fu : uint32_t(rng()));
        break;
    }
    case 21: { // 51: the composition family's empty attribute range
        cArrayAttributeEntryRange range{};
        const void *const r = n->MakeIterator51(&range, rng() % 2 ? 0x1234 : uint32_t(rng()));
        drawfx::Note("range", {uint8_t(r == &range), uint8_t(range.mCurrent->IsEqual(range.mBegin)),
                               uint8_t(range.mBegin->IsEqual(range.mEnd))});
        for (cIArrayAttributeEntryIterator *it : {range.mCurrent, range.mBegin, range.mEnd})
            drawfx::Note("released", it->Release());
        break;
    }
    case 22: { // the target changing (the device's size record and its presence)
        svx::Vw &v = svx::gVw;
        const uint32_t sizes[] = {640, 480, 1024, 768, 0, 1, 3000};
        v.provWords[0] = rng() % 4 ? sizes[rng() % 7] : uint32_t(rng());
        v.provWords[1] = rng() % 4 ? sizes[rng() % 7] : uint32_t(rng());
        if (rng() % 6 == 0) v.haveDevice = !v.haveDevice;
        break;
    }
    }
}

// The base's state a clone copies: textures, references, the bound, the clear words; and materials.
void GiveBase(cICompositionTreeNode *n, std::mt19937 &rng)
{
    cIViewerRefNodeRecursive *s = As(n);
    if (rng() % 2) {
        cIGZUnknown *textures[4];
        for (cIGZUnknown *&t : textures) t = rng() % 4 ? Held(rng() % 12) : nullptr;
        s->Slot93(1 + rng() % 4, textures, rng() % 2 ? Held(rng() % 12) : nullptr, Any(rng));
    } else if (rng() % 2) {
        s->Slot94(1 + rng() % 9);
    }
    if (rng() % 2) s->Slot96(Held(rng() % 12), rng() % 2 ? Held(rng() % 12) : nullptr);
    if (rng() % 2) s->Slot103(Held(rng() % 12));
    if (rng() % 2) s->Slot97(uint32_t(rng()));
    if (rng() % 2) {
        float plane[4];
        for (float &f : plane) f = Any(rng);
        s->Slot101(rng() % 3 ? reinterpret_cast<cIBoundedNode *>(&svx::gVw.bound) : nullptr, plane, uint32_t(rng()));
    }
}
// Materials named through the setters (GetInfo answers each index a name, or none).
void GiveMaterials(cICompositionTreeNode *n, std::mt19937 &rng)
{
    if (rng() % 4 == 0) return;
    if (rng() % 2) As(n)->SetMaterialCount(1 + rng() % 3);
    for (int k = 0; k < 3; ++k) As(n)->SetMaterial(rng() % 4, kMaterialNames[rng() % 4]);
}

// ---- persistence ----

void PersistBuild(uint32_t seed, drawfx::Cells &cells)
{
    Build(seed, cells);
    vtape::Install();
}

// The node's Impl (+0xc) on either side, or the +8 face's slot 3 / 5.
void CallRead(difftest::Context &ctx, bool original, cICompositionTreeNode *n, bool face)
{
    auto *reader = &sgfx::gReader.fake;
    drawfx::Note("read through the face", uint8_t(face));
    uint8_t *const node = reinterpret_cast<uint8_t *>(n);
    if (face) {
        using Fn = void(__thiscall *)(void *, void *);
        void *const persist = node + 8;
        (*reinterpret_cast<Fn *const *>(persist))[3](persist, reader);
    } else if (original) {
        ctx.Original<void(__thiscall *)(void *, void *, void *)>(kRead)(node + 0xc, n, reader);
    } else {
        reinterpret_cast<cViewerRefNodeRecursiveImpl *>(node + 0xc)
            ->Read(reinterpret_cast<cViewerRefNodeRecursive *>(n), reinterpret_cast<ifaces::cIGZPersistReader *>(reader));
    }
}
void CallWrite(difftest::Context &ctx, bool original, cICompositionTreeNode *n, bool face)
{
    auto *writer = &sgfx::gWriter.fake;
    drawfx::Note("write through the face", uint8_t(face));
    uint8_t *const node = reinterpret_cast<uint8_t *>(n);
    if (face) {
        using Fn = void(__thiscall *)(void *, void *);
        void *const persist = node + 8;
        (*reinterpret_cast<Fn *const *>(persist))[5](persist, writer);
    } else if (original) {
        ctx.Original<void(__thiscall *)(void *, void *, void *)>(kWrite)(node + 0xc, n, writer);
    } else {
        reinterpret_cast<cViewerRefNodeRecursiveImpl *>(node + 0xc)
            ->Write(reinterpret_cast<cViewerRefNodeRecursive *>(n), reinterpret_cast<ifaces::cIGZPersistWriter *>(writer));
    }
}

// The materials' records: the count, then per material a flag and (when set) its name.
void TapeMaterials(std::mt19937 &rng)
{
    vtape::Tape &t = vtape::gTape;
    const uint32_t count = rng() % 5 ? rng() % 4 : 0;
    t.Word(count);
    static const char *const kNames[] = {"mat_a", "*grp!mat_b", "x_missing", "", "##0x1c0532fa!mat_c", "lambert"};
    for (uint32_t m = 0; m < count; ++m) {
        const uint32_t present = vtape::BoolWord(rng);
        t.Word(present);
        if (present & 0xff) t.Text(kNames[rng() % 6]);
    }
}

// ---- the cull (slot 83 0xa6a370 -> 0xa6aec0; lane VG) ----

// The settings' GetProperty: notes the key; value[1] says whether it answers, value[0] the boolean it gives.
uint32_t SettingsGet(Fake &self, const uint32_t *a)
{
    sgfx::NoteText("key", *reinterpret_cast<const char *const *>(uintptr_t(a[0])));
    if (!self.value[1]) return 0;
    *reinterpret_cast<uint8_t *>(uintptr_t(a[1]) + 4) = uint8_t(self.value[0]);
    return 1;
}
// The framework's GetSystemService: the settings (always there: the original asks them unchecked), else the viewer
// world's answer.
uint32_t FwSystemService(Fake &self, const uint32_t *a)
{
    if (a[0] == cViewerRefNodeImpl::kSettingsService && a[1] == cViewerRefNodeImpl::kSettingsIid) {
        ++w.settings.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &w.settings;
        return 1;
    }
    return svx::FwService(self, a);
}

// The viewer tests' world with the cull's fakes: the settings, the stage the parent answers (slot 32) and the parent.
void CullBuild(uint32_t seed, drawfx::Cells &cells)
{
    Build(seed, cells);
    svx::gVw.fw.On(5, &FwSystemService);
    w.settings.Reset<kSettings>(700);
    w.settings.On(9, &SettingsGet);
    w.stage.Reset<kStage>(705);
    sgfx::MakeFlagNode<sgfx::kExplicitBoundNode>(w.parent, 706);
    w.parent.value[sgfx::kFlagsWord] = 0;
    w.parent.value[sgfx::kLimitWord] = 0x7fffffff;
    w.parent.Returns(32, &w.stage);
    w.drawn = 0;
}

// The node's root given the fake parent (the composition Impl's parent word, node+0x18) for the call, then let go of.
struct Parented {
    uint32_t *word;
    explicit Parented(cICompositionTreeNode *n)
    {
        while (cICompositionTreeNode *p = n->GetParent()) n = p;
        word = &Words(n)[(kBase + 8) / 4];
        *word = P(&w.parent);
    }
    ~Parented() { *word = 0; }
};

// Mostly ordinary values (most cases draw), sometimes wild ones.
float Mild(std::mt19937 &rng) { return rng() % 10 ? sgtx::RandomFloat(rng) : svx::WildFloat(rng); }

// A frame: the flags pick the inverse's branch (identity, rotation, scale, general), the matrix and offset any values.
void RandomFrame(float *frame, std::mt19937 &rng)
{
    static const uint32_t kFlags[] = {0, 1, 2, 3, 4, 5, 0xc, 0xd, 0x10, 0x11, 0x13, 0x1f};
    frame[0] = std::bit_cast<float>(rng() % 8 ? kFlags[rng() % 12] : uint32_t(rng()));
    for (unsigned i = 1; i < 13; ++i) frame[i] = Mild(rng);
}

// A projection: a perspective one (the near and far planes, the field of view), or any sixteen values.
void RandomProjection(float *m, std::mt19937 &rng)
{
    if (rng() % 3) {
        for (unsigned i = 0; i < 16; ++i) m[i] = 0.0f;
        m[0] = 0.5f + float(rng() % 300) / 100.0f;
        m[5] = 0.5f + float(rng() % 300) / 100.0f;
        m[10] = -1.0f - float(rng() % 100) / 1000.0f;
        m[11] = -0.1f - float(rng() % 100) / 100.0f;
        m[14] = -1.0f;
        if (rng() % 4 == 0) m[rng() % 16] = Mild(rng);
    } else {
        for (unsigned i = 0; i < 16; ++i) m[i] = Mild(rng);
    }
}

// The state a cull reads: the bound with its plane and flags (or none, or everywhere), the bound's world transform and
// box, the settings' answer, and the view (its rectangle +0x84, depth range +0x94, frame +0x110, projection +0x144).
void SetUpCull(cICompositionTreeNode *n, std::mt19937 &rng)
{
    svx::Vw &v = svx::gVw;
    if (rng() % 3) {
        float plane[4];
        for (float &f : plane) f = Mild(rng);
        if (rng() % 8 == 0) plane[0] = plane[1] = plane[2] = 0.0f;
        const uint32_t flags = rng() % 4 | (rng() % 4 ? 0u : uint32_t(rng()) & ~3u);
        As(n)->Slot101(rng() % 6 ? reinterpret_cast<cIBoundedNode *>(&v.bound) : nullptr, plane, flags);
    }
    v.bound.Returns(70, uint32_t(rng() % 5 == 0));
    for (float &f : v.world) f = Mild(rng);
    for (float &f : v.box) f = Mild(rng);
    w.settings.value[0] = rng() % 4 != 0;
    w.settings.value[1] = rng() % 4 != 0;
    float *const view = reinterpret_cast<float *>(&gBig);
    for (unsigned i = 0; i < 2; ++i) view[0x84 / 4 + i] = rng() % 4 ? float(rng() % 500) / 1000.0f : Fraction(rng);
    for (unsigned i = 2; i < 4; ++i) view[0x84 / 4 + i] = rng() % 4 ? 0.5f + float(rng() % 501) / 1000.0f : Fraction(rng);
    for (unsigned i = 0; i < 2; ++i) view[0x94 / 4 + i] = Fraction(rng);
    if (rng() % 2) RandomFrame(gView.mFrame.mValues, rng);
    if (rng() % 2) RandomProjection(gBig.projection, rng);
}

// Slot 83 on a node whose root's parent answers the stage (the target safe to size: TargetSafe).
void OperateCull(difftest::Context &ctx, int k, std::mt19937 &rng, unsigned *cover)
{
    cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
    Mark(ctx, k, 100);
    SetUpCull(n, rng);
    if (!TargetSafe(n)) return;
    const uint32_t flags = Words(n)[(kBase + 0xc0) / 4];
    const bool bounded = Words(n)[(kBase + 0xac) / 4] != 0 && svx::gVw.bound.ret[70] == 0;
    const unsigned before = w.drawn;
    {
        Parented parented(n);
        As(n)->Slot83(&gView, reinterpret_cast<void *>(uintptr_t(rng())));
    }
    drawfx::Note("cull", {w.drawn - before, Words(n)[(kBase + 0x78) / 4]});
    if (w.drawn != before) ++cover[bounded ? 1 + (flags & 3) : 0];
}

void NoteCullFakes()
{
    for (const Fake *f : {&w.settings, &w.stage, &w.parent}) drawfx::Note("fake refs", {f->id, uint32_t(f->refs)});
}

// ---- the mirrored view's geometry, function by function (lane VG) ----

template <class T> bool Same(const T &a, const T &b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }
uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

} // namespace

DIFF_TEST(SG_ViewerRefRecursive_Pilot_Slots, 0xa6ca70) { RecursivePilot::Slots(ctx); }
DIFF_TEST(SG_ViewerRefRecursive_Pilot_Events, 0xa6c860) { RecursivePilot::Events(ctx); }

struct Scopes {
    svcfx::AllocatorScope allocator;
    svx::Stands stands;
    sgfx::RegistryScope node{sgfx::kNodeRegistry};
    sgfx::RegistryScope transform{sgfx::kTransformRegistry};
    sgtp::RefreshScope refresh;
};

DIFF_TEST(SG_ViewerRefRecursive_Factory, kFactory)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "factory", 32, Build, [&](bool original, std::mt19937 &rng) {
        cICompositionTreeNode *n;
        if (rng() % 2) { // 0xa69f50 against Construct, in fresh memory
            void *const memory = ::operator new(kSize);
            cViewerRefNodeRecursive *const made =
                original ? ctx.Original<cViewerRefNodeRecursive *(__thiscall *)(void *)>(kConstruct)(memory)
                         : static_cast<cViewerRefNodeRecursive *>(memory)->Construct();
            drawfx::Note("in place", uint8_t(static_cast<void *>(made) == memory));
            n = reinterpret_cast<cICompositionTreeNode *>(made);
        } else {
            n = original ? ctx.Original<sgfx::Factory>(kFactory)() : MakeOurs();
        }
        drawfx::Note("count", CountOf(n));
        w.nodes.size = kNoted;
        Hold(n, rng);
        NoteNodes("new node");
        if (rng() % 2) GiveBase(n, rng);
        if (rng() % 2) GiveMaterials(n, rng);
        NoteNodes("held");
        const unsigned end = rng() % 3;
        drawfx::Note("end", end);
        if (end == 0) {
            ReleaseAll();
            return;
        }
        w.nodes.count = 0;
        difftest::FreeRecorder freed;
        if (end == 1) { // 0xa6a130 against Destruct, then the memory freed
            if (original) ctx.Original<void(__thiscall *)(void *)>(kDestruct)(n);
            else reinterpret_cast<cViewerRefNodeRecursive *>(n)->Destruct();
            ::operator delete(n);
        }
        const uint32_t flags = rng() % 2;
        if (end == 2) { // 0xa6a330 against DeletingDestructor (bit 0: freed)
            drawfx::Note("flags", flags);
            void *r = original ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting)(n, flags)
                               : reinterpret_cast<cViewerRefNodeRecursive *>(n)->DeletingDestructor(flags);
            drawfx::Note("returns this", uint8_t(r == n));
        }
        freed.Stop();
        if (end == 2 && !(flags & 1)) ::operator delete(n); // destroyed only: the block is still ours to free
        NoteFreed(freed);
        NoteFakes();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ViewerRefRecursive_Slots, 0xa6c160)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "slots", 96, Build, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        for (unsigned i = 0; i < w.nodes.count; ++i)
            if (rng() % 2) GiveMaterials(w.nodes[i], rng);
        NoteNodes("built");
        for (int k = 0; k < 40; ++k) OperateSlots(ctx, original, k, rng);
        NoteNodes("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ViewerRefRecursive_Clone, 0xa6a930)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "clone", 64, Build, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        for (unsigned i = 0; i < w.nodes.count; ++i) {
            if (rng() % 2) GiveBase(w.nodes[i], rng);
            GiveMaterials(w.nodes[i], rng);
        }
        for (int round = 0; round < 3; ++round) {
            cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
            const uint32_t mode = rng() % 3;
            cINodeCloneContext *context = nullptr;
            const bool viaConstruct = rng() % 2;
            if (rng() % 3 == 0 && mode != 1 && w.nodes.count < sgfx::Nodes::kMax) {
                cICompositionTreeNode *prepared =
                    original ? ctx.Original<sgfx::Factory>(kFactory)() : viaConstruct ? MakeByConstruct() : MakeOurs();
                Hold(prepared, rng);
                if (rng() % 2) GiveMaterials(prepared, rng);
                w.context.value[0] = P(prepared);
                w.context.value[1] = rng() % 2 ? P(&svx::gVw.bound) : 0;
                context = reinterpret_cast<cINodeCloneContext *>(&w.context);
            }
            const uint32_t iids[] = {sgfx::kIidTree, sgfx::kIidGraph, cViewerRefNodeRecursive::kIid, 0x12345678};
            void *out = nullptr;
            n->Clone(mode, iids[rng() % 4], &out, context);
            const bool isNew = out && drawfx::Norm(P(out)) == P(out);
            drawfx::Note("clone", {mode, out != nullptr, uint8_t(isNew), isNew ? 0u : drawfx::Norm(P(out))});
            if (isNew && w.nodes.count < sgfx::Nodes::kMax) {
                Adopt(static_cast<cICompositionTreeNode *>(out));
                NoteNodes("cloned");
            } else if (out) {
                static_cast<cICompositionTreeNode *>(out)->Release();
            }
        }
        NoteNodes("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

// Read 0xa6b750, through the +8 face's slot 3 (0xa6c270) or on the Impl: the type record, the viewer base's records
// (any of their versions), then the materials named in the stream's group (slot 28) or none, over a node that may hold
// materials already.
DIFF_TEST(SG_ViewerRefRecursive_Read, kRead)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "read", 128, PersistBuild, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        for (unsigned i = 0; i < w.nodes.count; ++i) GiveMaterials(w.nodes[i], rng);
        cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
        sgfx::gReader.fake.value[0] = vtape::StreamGroup(rng);
        vtape::TapeBase(rng() % 3, rng);
        TapeMaterials(rng);
        if (rng() % 8 == 0) vtape::gTape.Word(0x5a5a5a5a); // more than the record holds
        NoteNodes("built");
        CallRead(ctx, original, n, rng() % 2);
        vtape::gTape.NotePosition("tape read");
        NoteNodes("read");
        ReleaseAll();
    });
    w.manager.Drop();
}

// Write 0xa6bb80, through the +8 face's slot 5 (0xa6ceb0) or on the Impl: every node of a tree, with materials named
// (GetInfo answers a name, a "*group!name" reference, or none).
DIFF_TEST(SG_ViewerRefRecursive_Write, kWrite)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "write", 128, PersistBuild, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        for (unsigned i = 0; i < w.nodes.count; ++i) {
            if (rng() % 2) GiveBase(w.nodes[i], rng);
            GiveMaterials(w.nodes[i], rng);
        }
        for (unsigned i = 0; i < w.nodes.count; ++i) {
            vtape::gTape.Clear();
            difftest::AllocationCounter counted;
            CallWrite(ctx, original, w.nodes[i], rng() % 2);
            drawfx::Note("left allocated", uint32_t(counted.Allocations() - counted.Frees()));
            vtape::gTape.NoteItems("written");
        }
        NoteNodes("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

// A round trip through the +8 face (Write 0xa6ceb0 into the tape, Read 0xa6c270 into a fresh node of the same side).
DIFF_TEST(SG_ViewerRefRecursive_Persist, 0xa6c270)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "persist", 96, PersistBuild, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
        if (rng() % 2) GiveBase(n, rng);
        GiveMaterials(n, rng);
        CallWrite(ctx, original, n, true);
        vtape::gTape.NoteItems("written");
        sgfx::gReader.fake.value[0] = vtape::StreamGroup(rng);
        if (w.nodes.count < sgfx::Nodes::kMax) {
            cICompositionTreeNode *fresh = original ? ctx.Original<sgfx::Factory>(kFactory)() : MakeOurs();
            Hold(fresh, rng);
            if (rng() % 2) GiveMaterials(fresh, rng);
            CallRead(ctx, original, fresh, true);
            vtape::gTape.NotePosition("tape read");
        }
        NoteNodes("read back");
        ReleaseAll();
    });
    w.manager.Drop();
}

// The cull 0xa6aec0 through slot 83 (0xa6a370) on either side, among the other slots: the settings (bit 0), the frame
// check, the eye (the node's frame, or the view's reflected about the bound's plane with the oblique near plane), the
// bound's box on screen (bit 1), the draw (the rectangle a pixel of the target each way), the stage and the pass.
DIFF_TEST(SG_ViewerRefRecursive_Cull, 0xa6aec0)
{
    Scopes scopes;
    unsigned cover[5] = {};
    sgfx::CheckSides(ctx, "cull", 128, CullBuild, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        for (unsigned i = 0; i < w.nodes.count; ++i) {
            if (rng() % 2) GiveBase(w.nodes[i], rng);
            GiveMaterials(w.nodes[i], rng);
        }
        NoteNodes("built");
        for (int k = 0; k < 24; ++k) {
            if (rng() % 5 == 0) OperateSlots(ctx, original, k, rng);
            else OperateCull(ctx, k, rng, cover);
        }
        NoteNodes("after");
        ReleaseAll();
        NoteCullFakes();
    });
    w.manager.Drop();
    // Draws without the bound's branch, and with it for each of the flags' low bits.
    for (unsigned b = 0; b < 5; ++b)
        if (cover[b] < 20) ctx.Fail("cull: %u draws in branch %u", cover[b], b);
}

// The cull's helpers against the originals, alone (nothing but the cull reaches them): PlaneToFrame 0x19c980,
// Subtract3 0x1a6760, Transpose4 0x1a65e0, MultiplyBy4 0x1a6690, Reflect 0xa6c430 and ObliqueClip 0xa6bf10, apart and in
// place where the original allows it, with every inverse branch of the frame and NaNs.
DIFF_TEST(SG_ViewerRefRecursive_Geometry, 0xa6c430)
{
    using namespace nGZSceneGraph::recursive_view;
    using PlaneFn = void(__thiscall *)(const void *, float *, const float *);
    using SelfFn = float *(__thiscall *)(float *, const float *);
    using TransposeFn = float *(__cdecl *)(float *, const float *);
    using ReflectFn = AffineTransform3 *(__cdecl *)(AffineTransform3 *, const AffineTransform3 *, const float *);
    using ClipFn = void(__cdecl *)(float *, const AffineTransform3 *, const float *);
    std::mt19937 rng(0xa6c430);
    unsigned failures = 0, clipped = 0, kept = 0;
    auto check = [&](bool same, const char *what, unsigned i, const float *a, const float *b) {
        ++ctx.cases;
        if (same || failures >= 10) return;
        ++failures;
        ctx.Fail("%s case %u: %08x %08x %08x %08x / %08x %08x %08x %08x", what, i, Bits(a[0]), Bits(a[1]), Bits(a[2]),
                 Bits(a[3]), Bits(b[0]), Bits(b[1]), Bits(b[2]), Bits(b[3]));
    };
    for (unsigned i = 0; i < 4000; ++i) {
        AffineTransform3 frame;
        RandomFrame(reinterpret_cast<float *>(&frame), rng);
        float plane[4];
        for (float &f : plane) f = Mild(rng);
        if (rng() % 8 == 0) plane[0] = plane[1] = plane[2] = plane[3] = 0.0f;
        if (rng() % 8 == 0)
            for (float &f : plane) f *= 1.0e-12f;
        {
            float v[2][8];
            for (unsigned j = 0; j < 8; ++j) v[0][j] = j < 4 ? plane[j] : Mild(rng);
            std::memcpy(v[1], v[0], sizeof v[0]);
            const bool inPlace = rng() % 3 == 0;
            ctx.Original<PlaneFn>(0x19c980)(&frame, inPlace ? v[0] : v[0] + 4, v[0]);
            PlaneToFrame(&frame, inPlace ? v[1] : v[1] + 4, v[1]);
            check(Same(v[0], v[1]), "PlaneToFrame", i, v[0] + (inPlace ? 0 : 4), v[1] + (inPlace ? 0 : 4));
        }
        {
            float a[2][9], b[9];
            for (unsigned j = 0; j < 9; ++j) {
                a[0][j] = Mild(rng);
                b[j] = Mild(rng);
            }
            std::memcpy(a[1], a[0], sizeof a[0]);
            const bool r0 = ctx.Original<SelfFn>(0x1a6760)(a[0], b) == a[0];
            const bool r1 = Subtract3(a[1], b) == a[1];
            check(Same(a[0], a[1]) && r0 && r1, "Subtract3", i, a[0], a[1]);
        }
        {
            float m[2][20];
            for (float &f : m[0]) f = std::bit_cast<float>(uint32_t(rng())); // any bits: a copy keeps them all
            std::memcpy(m[1], m[0], sizeof m[0]);
            const unsigned at = rng() % 3 == 0 ? rng() % 4 : 4; // in place or overlapping (0..3), or apart
            const bool r0 = ctx.Original<TransposeFn>(0x1a65e0)(m[0], m[0] + at) == m[0];
            const bool r1 = Transpose4(m[1], m[1] + at) == m[1];
            check(Same(m[0], m[1]) && r0 && r1, "Transpose4", i, m[0], m[1]);
        }
        {
            float a[2][16], b[16];
            for (unsigned j = 0; j < 16; ++j) {
                a[0][j] = Mild(rng);
                b[j] = Mild(rng);
            }
            std::memcpy(a[1], a[0], sizeof a[0]);
            const bool r0 = ctx.Original<SelfFn>(0x1a6690)(a[0], b) == a[0];
            const bool r1 = MultiplyBy4(a[1], b) == a[1];
            check(Same(a[0], a[1]) && r0 && r1, "MultiplyBy4", i, a[0], a[1]);
        }
        {
            AffineTransform3 out[2];
            std::memset(out, 0x5c, sizeof out);
            const bool r0 = ctx.Original<ReflectFn>(0xa6c430)(&out[0], &frame, plane) == &out[0];
            const bool r1 = Reflect(&out[1], &frame, plane) == &out[1];
            check(Same(out[0], out[1]) && r0 && r1, "Reflect", i, out[0].mMatrix, out[1].mMatrix);
        }
        {
            float m[2][16], was[16];
            RandomProjection(m[0], rng);
            std::memcpy(was, m[0], sizeof was);
            std::memcpy(m[1], m[0], sizeof m[0]);
            ctx.Original<ClipFn>(0xa6bf10)(m[0], &frame, plane);
            ObliqueClip(m[1], &frame, plane);
            check(Same(m[0], m[1]), "ObliqueClip", i, m[0] + 8, m[1] + 8);
            ++(Same(m[0], was) ? kept : clipped);
        }
    }
    if (kept < 200 || clipped < 200) ctx.Fail("ObliqueClip: %u kept, %u clipped", kept, clipped);
}
