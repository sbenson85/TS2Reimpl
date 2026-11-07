// Differential tests of nGZSceneGraph::cViewerRefNode (lane TZ, scenegraph N3r): the game's viewer-reference node (its
// factory 0xa65fb0, its vtables 0xe71b10 / 0xe71d60 and the bodies behind them) against ours (Create, our vtables), each
// side building its own nodes. In half the seeds our side builds through Construct in fresh memory instead of Create.
//  - Factory: a fresh node's words and count, then let go of by Release (the deleting destructor 0xa64bd0, the
//    destructor 0xa64820 -> 0xa64750, the Impl's 0xa64840 and the entries' display-list slots freed), or by the
//    destructor 0xa64820 against Destruct, or the deleting destructor against DeletingDestructor; the constructor
//    0xa644a0 in place against Construct.
//  - Slots: trees of nodes driven through QueryInterface 0xa688e0, IsKindOf (0xa67be0), Accept / AcceptReadOnly
//    0xa696f0, the viewer base's slots 87-122 (91 0xa69340, 103 0xa69680, 104 0xa65550 and the base Impl's bodies), the
//    registry (GetEventAttribute 0xa65510 -> 0xa66600, with "fov" / "nearFarClip" planted), GetTypeInfo 0xa67c00, flags
//    and tree moves; and the entries: slots 124-138 (ResizeEntries 0xa67950 .. SetProjection 0xa67770) and the
//    attributes 7 / 10 (0xa68000 / 0xa66e50), against a fake display list (the detail object 0xaead80) and material
//    manager (0x10f8fcc). The node template's and composition copies too: OnEvent 3 (0xa66150, the base Impl's 0xae0c20)
//    from the held bound or not, NotifyListeners / AskListeners 5 / 6 (0xa69240 / 0xa65f70), the bound slots 71-75
//    (0xa68740 / 0xa68760 / 0xa686d0 / 0xa686e0 / 0xa68730) with and without a held bound, and the behaviour's 12 / 13.
//    The other copies in the vtables, each through our vtable on both sides (Slots and View):
//   0xa69a30 vf7 SetAttribute, 0xa689a0 vf10 GetAttribute, 0xa687d0 vf12 GetEventCount, 0xa64f20 vf13 GetEventIds,
//   0xa696c0 vf25 IsKindOf, 0xa68650 vf70 IsEverywhere, 0xa64c60 vf82 Draw, 0xa64c30 vf83 Cull,
//   0xa69550 vf92 SetExplicit, 0xa69570 vf93 SetTextures, 0xa695a0 vf94 SetTexture, 0xa695b0 vf95 SetTextureByName,
//   0xa69370 vf96 SetSurfaces, 0xa69530 vf99 SetRefA8, 0xa69540 vf100 SetRefA4, 0xa695f0 vf101 SetBoundAndPlane,
//   0xa695e0 vf102 SetBound, 0xa68820 vf105 GetTextureCount, 0xa68ff0 vf106 GetTexture,
//   0xa68fe0 vf107 GetTextureRef94, 0xa69020 vf108 the texture handle, 0xa69010 vf109 GetHandleTexture,
//   0xa68fc0 vf110 GetTargetSize, 0xa656d0 vf111 GetColorSurface, 0xa656c0 vf112 GetDepthSurface,
//   0xa69000 vf113 IsDeviceViewMode, 0xa656e0 vf114 UsesDeviceTargets, 0xa68fa0 vf116 the +0xa8 reference,
//   0xa68fb0 vf117 GetRenderStrategy, 0xa68f90 vf123 Render, 0xa694a0 vf124 ResizeEntries,
//   0xa69620 vf125 SetEntryPlacement, 0xa69490 vf126 SetMaterialCount, 0xa69610 vf127 SetMaterial,
//   0xa69630 vf128 SetViewport, 0xa69380 vf129 SetSubrect, 0xa693f0 vf130 SetFixedAspect,
//   0xa69510 vf131 SetPixelAspect, 0xa693d0 vf132 SetPerspective, 0xa69410 vf133 SetDepthRange,
//   0xa69460 vf134 SetNearFar, 0xa69310 vf135 SetOffsets, 0xa694b0 vf136 SetOrtho, 0xa69300 vf137 SetProjection,
//   0xa68830 vf138 GetEntryCount, 0xa699a0 vf139 GetEntry, 0xa65530 vf140 GetView, 0xa65580 vf141 GetViewport,
//   0xa68880 vf142 WorldToScreen, 0xa68860 vf143 WorldToClip, 0xa69820 vf144 ScreenToWorld,
//   0xa69800 vf145 ClipToWorld, 0xa68912 QueryInterface adjustor thunk, 0xa68f55 Release adjustor thunk,
//   0xa69720 cViewerRefNodeT<cIViewerRefNode, cViewerRefNodeImpl> persist face slot 4 GetTypeInfo
//  - View: entries made through the setters, then GetEntry 0xa699b0 (RefreshAspect 0xa69840, UpdateProjection 0xa656f0),
//    GetView 0xa66760, GetViewport 0xa66800, the transforms 0xa66bc0 / 0xa66a30 / 0xa67e40 / 0xa67cb0 (every float
//    compared by its bits), Render (123, 0xa674d0) and the cull 83 (0xa661a0) through a fake parent's stage, the draw
//    82 (the base's 0xae09e0), with the target size, the "usePixelAspectRatio" setting and the frame changing.
//  - Clone: 15 (0xa655a0, the Impl's 0xa68660 and the base's 0xae1cb0) with and without a clone context, in each mode.
//  - Names: slot 51 (0xa66440) and the iterators it hands out (cViewerRefAttributeNameIterator).
//  - Read / Write / Persist: the Impl's Read 0xa66eb0 and Write 0xa68060, directly or through the +8 interface's slots 3
//    / 5 (0xa689d0 / 0xa69a70), on one tape of stream items (sg_viewer_ref_tape.h): records of versions 9 to 15 read,
//    every node written, and a round trip.
// Left out of the comparison: the vtable words 0 and 2, word 29 (the detail limit the constructor never writes: planted),
// word 53 but its byte (the +0xc8 byte and three bytes nobody writes) and the entries' vector words 61-63 (heap
// addresses: the entries are noted on their own).
#include "sg_transform_pilot.h"
#include "sg_viewer_ref_fx.h"
#include "sg_viewer_ref_tape.h"
#include "sgfx.h"
#include "ts_services_fixture.h"

#include "sg/cNullArrayAttributeEntryIterator.h"
#include "sg/cTransformNode.h"
#include "sg/cViewerRefNode.h"
#include "sg/cViewer_LEGACY.h"

#include <array>
#include <bit>
#include <cstring>
#include <malloc.h>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kFactory = 0xa65fb0;
constexpr uint32_t kConstruct = 0xa644a0;
constexpr uint32_t kDestruct = 0xa64820;
constexpr uint32_t kDeleting = 0xa64bd0;
constexpr uint32_t kSize = cViewerRefNode::kSize;
constexpr uint64_t kSkipWords = 0b101ull | 1ull << 29 | 1ull << 53 | 7ull << 61;
constexpr uint32_t kEventCell = 0x10f63e0;
constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr uint32_t kMaterialCell = 0x10f8fcc; // GetCachedQueriedObject's cache: the material manager
constexpr uint32_t kViewTag = 0x79000000;
constexpr uint32_t kFov = cViewerRefNodeImpl::kFovAttribute;
constexpr uint32_t kNearFar = cViewerRefNodeImpl::kNearFarAttribute;

// The viewer behaviour's registry (0xa68db0, ours; tests/sg_viewer_ref_registry.cpp): its map and guard, its holder's
// once byte and guard (sgfx::RegistryScope stands both sides' functions in over the planted map).
constexpr sgfx::RegistryStatics kViewerRegistry{0x10f63d0, 0x10f63d8, 0x10f63e4, 0x10f63ec};

// The display list (the detail object, and a view's drawer): the slots the viewer calls.
constexpr auto kListSlots = svx::Sparse<44>({{19, {3, Ret::Int, true, "ReallocSlots", "uuu"}},
                                             {20, {2, Ret::Int, true, "FreeSlots", "uu"}},
                                             {21, {0, Ret::Int, true, "BeginScene", ""}},
                                             {22, {0, Ret::Int, true, "EndScene", ""}},
                                             {23, {7, Ret::Int, true, "DrawViewer", "aF2F2F2W13F16F16"}},
                                             {24, {3, Ret::Int, true, "DrawPass", "u@0x1u"}},
                                             {27, {2, Ret::Int, true, "DrawNode", "au"}},
                                             {43, {0, Ret::Int, true, "GetFrame", ""}}});
constexpr drawfx::Iface kList{"DisplayList", 44, kListSlots.data()};
// The stage the parent chain answers (slot 32): slot 93 is handed the pass DrawViewer made.
constexpr auto kStageSlots = svx::Sparse<94>({{93, {1, Ret::Int, true, "SetPass", "a"}}});
constexpr drawfx::Iface kStage{"Stage", 94, kStageSlots.data()};
constexpr auto kMaterialSlots = svx::Sparse<34>({{19, {1, Ret::Int, true, "FindOrLoad", "s"}},
                                                 {20, {1, Ret::Int, true, "AddRefIndex", "u"}},
                                                 {21, {1, Ret::Int, true, "ReleaseIndex", "u"}},
                                                 {33, {2, Ret::Int, true, "GetInfo", "iu"}}}); // (out, index)
constexpr drawfx::Iface kMaterials{"MaterialManager", 34, kMaterialSlots.data()};
// The configuration service's settings ("usePixelAspectRatio" through slot 9).
constexpr auto kSettingsSlots = svx::Sparse<10>({{9, {2, Ret::Int, true, "GetProperty", "ii"}}});
constexpr drawfx::Iface kSettings{"Settings", 10, kSettingsSlots.data()};
constexpr auto kContextSlots = svx::Sparse<4>({{3, {3, Ret::Int, true, "FindClone", "aua"}}});
constexpr drawfx::Iface kContext{"CloneContext", 4, kContextSlots.data()};

struct World {
    sgfx::Nodes nodes;
    Fake traversal, context, list, viewList, pass, stage, parent, materials, settings;
    Fake listener; // answers every query: connected to a tree node, its descendants' asks find it through their parents
    sgfx::Manager manager;
    uint32_t frames[4] = {};
    unsigned frameAt = 0, slotCalls = 0;
    bool settingsUp = true;
    const char *infoNames[8] = {}; // the material names GetInfo answers (by a hash of the index)
};
World w;
alignas(16) cViewerView gView;

// A node's range tag: 0x110-byte nodes need more than sgfx's 0x100 per index.
uint32_t NodeTag(unsigned i) { return sgfx::kNodeTag | i << 12; }

cICompositionTreeNode *__cdecl MakeOurs() { return cViewerRefNode::Create(); }
cICompositionTreeNode *__cdecl MakeByConstruct()
{
    return reinterpret_cast<cViewerRefNode *>(::operator new(kSize))->Construct();
}
cIViewerRefNode *As(cICompositionTreeNode *n) { return static_cast<cIViewerRefNode *>(static_cast<cIRenderableNode *>(n)); }
uint32_t CountOf(const cICompositionTreeNode *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }
uint32_t *Words(cICompositionTreeNode *n) { return reinterpret_cast<uint32_t *>(n); }
cIGZUnknown *Held(unsigned i) { return reinterpret_cast<cIGZUnknown *>(&svx::gVw.held[i]); }
// The node's entries as the Impl (+0xc) keeps them: the vector at +0xe8.
const uint8_t *EntryBase(cICompositionTreeNode *n, uint32_t *count)
{
    const uint32_t *v = reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(n) + 0xc + 0xe8);
    *count = (v[1] - v[0]) / sizeof(cViewerRefEntry);
    return reinterpret_cast<const uint8_t *>(uintptr_t(v[0]));
}
uint32_t EntryCount(cICompositionTreeNode *n)
{
    uint32_t count;
    EntryBase(n, &count);
    return count;
}

// ---- the fakes' handlers ----

// ReallocSlots: a new run of slots for a non-zero count, else none.
uint32_t ListRealloc(Fake &, const uint32_t *a) { return a[2] ? 0x100u + 0x10u * (w.slotCalls++ % 64) : 0xffffffffu; }
uint32_t ListFrame(Fake &, const uint32_t *) { return w.frames[w.frameAt++ % 4]; }
uint32_t ListDrawViewer(Fake &, const uint32_t *) { return P(&w.pass); }
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
uint32_t SettingsGet(Fake &self, const uint32_t *a)
{
    sgfx::NoteText("key", *reinterpret_cast<const char *const *>(uintptr_t(a[0])));
    if (!self.value[1]) return 0;
    *reinterpret_cast<uint8_t *>(uintptr_t(a[1]) + 4) = uint8_t(self.value[0]);
    return 1;
}
// The framework's GetSystemService: the settings (while up), else the viewer world's event service.
uint32_t FwSystemService(Fake &self, const uint32_t *a)
{
    if (a[0] == cViewerRefNodeImpl::kSettingsService && a[1] == cViewerRefNodeImpl::kSettingsIid) {
        if (!w.settingsUp) return 0;
        ++w.settings.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &w.settings;
        return 1;
    }
    return svx::FwService(self, a);
}
// FindClone: the prepared node for the viewer interface, value[1] (the held bound fake, or none) for anything else.
uint32_t ContextFind(Fake &self, const uint32_t *a)
{
    void *clone = reinterpret_cast<void *>(uintptr_t(a[1] == cViewerRefNode::kIid ? self.value[0] : self.value[1]));
    if (clone) static_cast<cIGZUnknown *>(clone)->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[2])) = clone;
    return 0;
}

void RandomFrames(std::mt19937 &rng)
{
    for (uint32_t &f : w.frames) f = rng() % 4 ? rng() % 3 : uint32_t(rng());
}
// The device's target size record (width, height, scale) and its presence; the setting.
void RandomTarget(std::mt19937 &rng)
{
    svx::Vw &v = svx::gVw;
    const uint32_t sizes[] = {640, 480, 1024, 768, 0, 1, 3000};
    v.provWords[0] = rng() % 4 ? sizes[rng() % 7] : uint32_t(rng());
    v.provWords[1] = rng() % 4 ? sizes[rng() % 7] : uint32_t(rng());
    v.provWords[2] = std::bit_cast<uint32_t>(rng() % 3 ? 0.5f + float(rng() % 4) * 0.25f : svx::WildFloat(rng));
    if (rng() % 6 == 0) v.haveDevice = !v.haveDevice;
    w.settings.value[0] = rng() % 2;
    w.settings.value[1] = rng() % 4 != 0;
    w.settingsUp = rng() % 5 != 0;
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gResources.Reset(cells);
    svx::Build(seed, cells);
    svx::gVw.fw.On(5, &FwSystemService);
    // Connect / Disconnect: the listener is the node (a clone not adopted yet: a heap record).
    svx::gVw.bound.Settle<8, 3>("uau");
    svx::gVw.bound.Settle<9, 3>("uau");
    svx::gVw.conn.Settle<12, 4>("uuau"); // IsConnected(event, source, listener, event)
    svx::gVw.haveDevice = rng() % 6 != 0;
    w.settings.Reset<kSettings>(700);
    w.settings.On(9, &SettingsGet);
    sgfx::MakeTraversal(w.traversal, 701, rng() % 3);
    w.traversal.Settle<11, 1>("a"); // Visit(viewer-reference node): the node
    w.traversal.Returns(11, 0x90 + w.traversal.ret[4]);
    MakeList(w.viewList, 703);
    w.pass.Reset<sgfx::kUnknown>(704);
    w.stage.Reset<kStage>(705);
    sgfx::MakeFlagNode<sgfx::kExplicitBoundNode>(w.parent, 706);
    w.parent.value[sgfx::kFlagsWord] = 0;
    w.parent.value[sgfx::kLimitWord] = 0x7fffffff;
    w.parent.Returns(32, &w.stage);
    w.materials.Reset<kMaterials>(707);
    w.materials.On(19, &MaterialFind);
    w.materials.On(33, &MaterialInfoOut);
    static const char *const kInfoNames[] = {nullptr, "plain", "*abc!def", "*nobang", "", "*!", "*grp!*x!y",
                                             "##0x1c0532fa!mat_c"};
    for (const char *&name : w.infoNames) name = kInfoNames[rng() % 8];
    cells.Set(kMaterialCell, static_cast<const void *>(&w.materials));
    w.context.Reset<kContext>(708);
    w.context.On(3, &ContextFind);
    RandomFrames(rng);
    RandomTarget(rng);
    w.frameAt = w.slotCalls = 0;
    std::memset(&gView, 0, sizeof(gView));
    gView.mDrawer = &w.viewList;
    drawfx::AddRange(&gView, sizeof(gView), kViewTag);
    w.nodes = sgfx::Nodes{};
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, kEventCell);
    sgfx::MakeListener(w.listener, 503, true);
    cells.Set(cTransformNodeBehaviorTraits::kEventManagerCell, w.manager.manager);
    // The legacy viewers' behaviour (cIObjectGraphNode's: their last Release disconnects through it).
    cells.Set(0x10f63dc, w.manager.manager);
    sgrx::Setup(rng);
    // The detail object is the display list: slot 43 (GetFrame) is what the renderable code asks as the limit too.
    MakeList(sgrx::Registry::Get().detail, 702);
}

// ---- the trees ----

void NoteEntries(const char *label)
{
    for (unsigned i = 0; i < w.nodes.count; ++i) {
        uint32_t count;
        const uint8_t *base = EntryBase(w.nodes[i], &count);
        drawfx::Note(label, count);
        for (uint32_t e = 0; e < count && e < 8; ++e) {
            const uint32_t *words = reinterpret_cast<const uint32_t *>(base + e * sizeof(cViewerRefEntry));
            drawfx::Call c;
            c.note = "entry";
            for (unsigned k = 0; k < 0xb4 / 4; ++k) c.words.push_back(k == 1 ? words[k] & 0xff : words[k]);
            const uint32_t *m = reinterpret_cast<const uint32_t *>(uintptr_t(words[0xb4 / 4]));
            const uint32_t n = (words[0xb8 / 4] - words[0xb4 / 4]) / 4;
            c.words.push_back(n);
            for (uint32_t j = 0; j < n; ++j) c.words.push_back(m[j]);
            c.words.push_back(words[0xc0 / 4]);
            c.words.push_back(words[0xc4 / 4]);
            c.words.push_back(words[0xc8 / 4] & 0xff);
            drawfx::gLog.push_back(std::move(c));
        }
    }
}

void NoteNodes(const char *label)
{
    w.nodes.NoteShape(label);
    for (unsigned i = 0; i < w.nodes.count; ++i) drawfx::Note("byte c8", Words(w.nodes[i])[53] & 0xff);
    w.nodes.NoteWords(label, kSkipWords);
    NoteEntries(label);
}

void Hold(cICompositionTreeNode *n)
{
    n->AddRef();
    drawfx::AddRange(n, kSize, NodeTag(w.nodes.count));
    w.nodes.at[w.nodes.count++] = n;
}
// A clone and its subtree (mode 1 clones the children too), the test's reference being the clone's own.
void Adopt(cICompositionTreeNode *n, bool top = true)
{
    if (w.nodes.count >= sgfx::Nodes::kMax) return;
    drawfx::AddRange(n, kSize, NodeTag(w.nodes.count));
    w.nodes.at[w.nodes.count++] = n;
    if (!top) n->AddRef();
    Words(n)[29] = 0x1234;
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
    w.nodes.size = kSize;
    for (unsigned i = 0; i < n; ++i) {
        w.nodes.at[i] = make();
        w.nodes.at[i]->AddRef();
        drawfx::AddRange(w.nodes.at[i], kSize, NodeTag(i));
    }
    w.nodes.Link(parents);
    for (unsigned i = 0; i < n; ++i) Words(w.nodes[i])[29] = uint32_t(rng());
}

void NoteFakes()
{
    for (const Fake *f : {&w.pass, &w.stage, &w.parent, &w.settings, &w.materials})
        drawfx::Note("fake refs", {f->id, uint32_t(f->refs)});
    svx::NoteExtras();
}

void ReleaseAll()
{
    difftest::FreeRecorder freed;
    w.nodes.Release();
    freed.Stop();
    size_t sizes[64];
    const size_t n = freed.SortedSizes(sizes, 64);
    drawfx::Call c;
    c.note = "freed";
    for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
    drawfx::gLog.push_back(std::move(c));
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

void NoteAttribute(const char *label, const cAttributeValue &v)
{
    drawfx::Call c;
    c.note = label;
    c.words.push_back(v.mType);
    for (float f : v.mValues) c.words.push_back(std::bit_cast<uint32_t>(f));
    drawfx::gLog.push_back(std::move(c));
}

void Mark(difftest::Context &ctx, int k, unsigned op)
{
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
}

// ---- random values ----

// A fraction of the target: mostly inside [0, 1], sometimes past it, sometimes anything.
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
float Positive(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return svx::WildFloat(rng);
    case 1: return 0.0f;
    default: return 0.05f + float(rng() % 2000) / 10.0f;
    }
}
float Any(std::mt19937 &rng) { return rng() % 6 ? sgtx::RandomFloat(rng) : svx::WildFloat(rng); }
void RandomRigid(cRigidTransform *t, std::mt19937 &rng)
{
    static const float kQuats[][4] = {{0, 0, 0, 1}, {0, 0.6f, 0, 0.8f}, {0.5f, 0.5f, 0.5f, 0.5f}, {0.8f, 0, 0, -0.6f}};
    for (int i = 0; i < 3; ++i) t->mValues[i] = Any(rng);
    const unsigned q = rng() % 5;
    for (int i = 0; i < 4; ++i) t->mValues[3 + i] = q < 4 ? kQuats[q][i] : Any(rng);
}
// Where the original dereferences null (the base Impl at node +0xc): sg_viewer_ref_fx.h.
bool TargetSafe(cICompositionTreeNode *n) { return svx::TargetSafe(n, 0xc); }
bool HandleSafe(cICompositionTreeNode *n) { return svx::HandleSafe(n, 0xc); }
uint32_t Index(cICompositionTreeNode *n, std::mt19937 &rng) { return rng() % EntryCount(n); }

// ---- the entries (slots 124-138, the attributes) ----

void OperateEntries(unsigned op, cICompositionTreeNode *n, std::mt19937 &rng)
{
    cIViewerRefNode *s = As(n);
    if (op != 0 && op < 14 && EntryCount(n) == 0) op = 0;
    switch (op) {
    case 0: { // 124
        const uint32_t count = rng() % 6 ? rng() % 4 : 5;
        drawfx::Note("resize", count);
        s->ResizeEntries(count);
        break;
    }
    case 1: { // 125
        cRigidTransform t;
        RandomRigid(&t, rng);
        s->SetEntryPlacement(Index(n, rng), &t);
        break;
    }
    case 2: // 126
        s->SetMaterialCount(Index(n, rng), rng() % 4);
        break;
    case 3: { // 127
        const char *const names[] = {nullptr, "xmissing", "lambert", "glass_material"};
        s->SetMaterial(Index(n, rng), rng() % 5, names[rng() % 4]);
        break;
    }
    case 4: s->SetViewport(Index(n, rng), Fraction(rng), Fraction(rng), Fraction(rng), Fraction(rng)); break;
    case 5: s->SetSubrect(Index(n, rng), Fraction(rng), Fraction(rng), Fraction(rng), Fraction(rng)); break;
    case 6: s->SetFixedAspect(Index(n, rng), uint8_t(rng() % 3 ? rng() % 2 : rng())); break;
    case 7: s->SetPixelAspect(Index(n, rng), rng() % 3 ? 1.0f : Positive(rng)); break;
    case 8: s->SetPerspective(Index(n, rng), rng() % 4 ? 0.3f + float(rng() % 200) / 100.0f : Any(rng)); break;
    case 9: s->SetDepthRange(Index(n, rng), Fraction(rng), Fraction(rng)); break;
    case 10: s->SetNearFar(Index(n, rng), Positive(rng), Positive(rng) * 10.0f); break;
    case 11: s->SetOffsets(Index(n, rng), Fraction(rng) - 0.5f, Fraction(rng) - 0.5f); break;
    case 12: {
        const float l = Any(rng), b = Any(rng);
        s->SetOrtho(Index(n, rng), l, rng() % 6 ? l + Positive(rng) : Any(rng), b, rng() % 6 ? b + Positive(rng) : Any(rng),
                    Positive(rng), Positive(rng) * 10.0f);
        break;
    }
    case 13: {
        float m[16];
        for (float &f : m) f = Any(rng);
        if (rng() % 4 == 0) {
            // NaNs of different payloads where two terms meet: in rows 0 and 1 (the viewport rows sum the second
            // clip coordinate first) or in one row (the projection rows sum y's term first).
            const auto nan = [&rng] { return std::bit_cast<float>(0x7f800001u | (uint32_t(rng()) & 0x807fffffu)); };
            const unsigned col = rng() % 4;
            m[col] = nan();
            m[rng() % 2 ? 4 + rng() % 4 : (col ^ 1)] = nan();
        }
        s->SetProjection(Index(n, rng), m);
        break;
    }
    case 14: { // 7: an attribute set (an entry index for the viewer's ids: below the count)
        const uint32_t count = EntryCount(n);
        const uint32_t ids[] = {kFov, kNearFar, sgfx::kRegistryIds[0], 0x1234};
        uint32_t id = ids[rng() % 4];
        if ((id == kFov || id == kNearFar) && count == 0) id = 0x1234;
        cAttributeValue v{uint32_t(rng()), {}};
        for (float &f : v.mValues) f = rng() % 2 ? Positive(rng) : Any(rng);
        const uint32_t extra = count ? rng() % count : rng() % 4;
        drawfx::Note("set", {id, extra});
        if (rng() % 2) n->SetFlag(12, false);
        n->SetAttribute(id, extra, P(&v));
        drawfx::Note("flags", n->GetFlags());
        break;
    }
    case 15: { // 10: an attribute read
        const uint32_t count = EntryCount(n);
        const uint32_t ids[] = {kFov, kNearFar, sgfx::kRegistryIds[1], 0x1234};
        uint32_t id = ids[rng() % 4];
        if ((id == kFov || id == kNearFar) && count == 0) id = 0x1234;
        // (the viewer's ids read the entry brought up to date, so the target size: see TargetSafe)
        if ((id == kFov || id == kNearFar) && !TargetSafe(n)) id = 0x1234;
        cAttributeValue v{0x5a5a5a5a, {}};
        for (float &f : v.mValues) f = -7.5f;
        const uint32_t extra = count ? rng() % count : rng() % 4;
        n->GetAttribute(P(&v), id, extra);
        drawfx::Note("get", {id, extra});
        NoteAttribute("attribute", v);
        break;
    }
    }
}

// Entries made up through the setters.
void GiveEntries(std::mt19937 &rng)
{
    for (unsigned i = 0; i < w.nodes.count; ++i) {
        cICompositionTreeNode *n = w.nodes[i];
        if (rng() % 4 == 0) continue;
        As(n)->ResizeEntries(1 + rng() % 3);
        for (int k = 0; k < 6; ++k) OperateEntries(1 + rng() % 13, n, rng);
    }
}

// ---- the base's slots (87-122) and the node's ----

void OperateSlots(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    cICompositionTreeNode *n = ns[rng() % ns.count];
    cIViewerRefNode *s = As(n);
    const unsigned op = rng() % 27;
    Mark(ctx, k, op);
    switch (op) {
    case 0: { // QueryInterface
        const uint32_t iids[] = {cViewerRefNode::kIid, cViewerRefNode::kBaseIid, sgfx::kIidTree, sgfx::kIidGraph,
                                 0xdb01d4b2, 0x5b0aaa74, 0xbb11a53e, 1, 0xe51c10f5, 0x7d14eb66, 0x7d3a2385, 0x12345678};
        const uint32_t iid = iids[rng() % 12];
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
        const uint32_t iids[] = {cViewerRefNode::kIid, cViewerRefNode::kBaseIid, sgfx::kIidTree, sgfx::kIidGraph,
                                 0x3b253dc7, 0xbb11a53e, 0x5b0aaa74, 1, 0, 0xdb01d4b2, 0xe51c10f5, 0x7d14eb66, 0x9c5ed164};
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
        const uint32_t ids[] = {sgfx::kRegistryIds[0], sgfx::kRegistryIds[1], sgfx::kRegistryIds[2], kFov, kNearFar,
                                uint32_t(rng())};
        const nGZEvent::cEventAttribute a = n->GetEventAttribute(ids[rng() % 6]);
        drawfx::Note("attribute", {a.mId, a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        sgfx::NoteText("attribute name", a.mName ? a.mName : "(null)");
        break;
    }
    case 4: { // flags
        uint32_t bit = rng() % 40;
        if ((bit & 31) == 1 || (bit & 31) == 6 || (bit & 31) == 12) bit = 0;
        n->SetFlag(bit, rng() % 2);
        drawfx::Note("flags", {uint8_t(n->TestFlag(bit)), n->GetFlags()});
        break;
    }
    case 5: { // a node moved
        cICompositionTreeNode *c = ns[rng() % ns.count];
        if (cICompositionTreeNode *p = c->GetParent()) p->RemoveChild(c);
        cICompositionTreeNode *to = ns[rng() % ns.count];
        bool inside = false;
        for (cICompositionTreeNode *a = to; a; a = a->GetParent()) inside |= a == c;
        if (!inside) to->AddChild(c);
        break;
    }
    case 6: { // the type
        cPersistTypeInfo info = n->GetReferent()->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 7: { // 87-90, 97, 98
        if (rng() % 2) s->Slot90(uint32_t(rng()));
        if (rng() % 2) s->Slot97(uint32_t(rng()));
        if (rng() % 2) s->SetLayerTagMatch(uint32_t(rng()));
        Words(n)[29] = rng() % 2 ? 0x7fffffffu : uint32_t(rng());
        drawfx::Note("words", {uint32_t(s->GetLayerTagMatch()), s->Slot88(), s->Slot89(), s->GetRenderType(), s->Slot120()});
        break;
    }
    case 8: { // 91 / 104
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
    case 9: { // 99, 100, 103 and their getters
        cIGZUnknown *const pick[] = {nullptr, Held(rng() % 12), Held(rng() % 12)};
        if (rng() % 2) s->Slot99(pick[rng() % 3]);
        if (rng() % 2) s->Slot100(pick[rng() % 3]);
        if (rng() % 2) s->Slot103(pick[rng() % 3]);
        drawfx::Note("refs", {drawfx::Norm(P(s->Slot116())), drawfx::Norm(P(s->GetRenderStrategy())),
                              drawfx::Norm(s->GetTextureMode()), drawfx::Norm(P(s->GetFrameCacheHolder()))});
        break;
    }
    case 10: { // the textures 92-96
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
    case 11: { // the texture getters 105-114 (but where the original dereferences null: see TargetSafe, HandleSafe)
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
    case 12: { // the bound 101 / 102 and its getters 118-120 (the event service is there: see sg_viewer_ref_fx.h)
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
    case 13: { // the count (138)
        drawfx::Note("count", s->GetEntryCount());
        break;
    }
    case 24: // an event told (3): from the held bound or not; the connection's body naming this node or not
    case 25: { // the listeners told (5) or asked (6): the parent hears it first
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
        if (op == 24) {
            n->OnEvent(type, uint32_t(rng()), &body, rng() % 4 ? P(&svx::gVw.bound) : P(&svx::gVw.dev));
        } else if (rng() % 2) {
            // Sometimes this node is its parent's held bound (base Impl +0xac): the parent's OnEvent then knows the
            // sender. Planted for the call only.
            cICompositionTreeNode *const parent = n->GetParent();
            uint32_t *const held = parent && rng() % 2 ? &Words(parent)[(0xc + 0xac) / 4] : nullptr;
            const uint32_t was = held ? *held : 0;
            if (held) {
                *held = P(s);
                // The parent copies this node's flag 0xb and, with 0xd set, hears that the bound changed.
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
            // Sometimes a listener on some node of the trees: a descendant's ask reaches it only through the parents.
            if (rng() % 2) w.manager.Listen(ns[rng() % ns.count], type, w.listener, 0x500 + rng() % 4);
            drawfx::Note("asked", uint8_t(n->AskListeners(type, uint32_t(rng()), &body)));
        }
        drawfx::Note("after", {drawfx::Norm(P(s->Slot118())), n->GetFlags()});
        break;
    }
    case 26: { // the bound slots 71-75 (the held bound answers; with none, the node's own box)
        float frame[12];
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
    default:
        OperateEntries(op - 14 + (rng() % 2 ? 0 : 6) + (op >= 20 ? 0 : 0), n, rng); // 0-15
        break;
    }
}

// ---- the view ----

// The node's root given the fake parent (whose slot 32 answers the stage) for the draw, then let go of again.
struct Parented {
    uint32_t *word;
    explicit Parented(cICompositionTreeNode *n)
    {
        while (cICompositionTreeNode *p = n->GetParent()) n = p;
        word = &Words(n)[5];
        *word = P(&w.parent);
    }
    ~Parented() { *word = 0; }
};

void NoteFloats(const char *label, const float *p, unsigned count, const void *returned)
{
    drawfx::Note("returns out", uint8_t(returned == p));
    sgtx::NoteFloats(label, p, count);
}

void OperateView(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
    cIViewerRefNode *s = As(n);
    const unsigned op = rng() % 20;
    Mark(ctx, k, op);
    const uint32_t count = EntryCount(n);
    if (op < 7 && count == 0) {
        s->ResizeEntries(1 + rng() % 2);
        return;
    }
    const uint32_t index = count ? rng() % count : 0;
    float point[3] = {Any(rng), Any(rng), Any(rng)};
    if (rng() % 3 == 0) point[2] = Fraction(rng);
    float out[16];
    std::memset(out, 0x5c, sizeof out);
    switch (op) {
    case 0: { // 139
        const cViewerRefEntry *e = s->GetEntry(index);
        uint32_t c;
        const uint8_t *base = EntryBase(n, &c);
        drawfx::Note("entry", uint32_t(reinterpret_cast<const uint8_t *>(e) - base));
        NoteEntries("after GetEntry");
        break;
    }
    case 1: { // 140
        cRigidTransform t;
        std::memset(&t, 0x5c, sizeof t);
        NoteFloats("view", t.mValues, 7, s->GetView(&t, index));
        break;
    }
    case 2: NoteFloats("viewport", out, 16, s->GetViewport(out, index)); break;
    case 3: NoteFloats("to screen", out, 3, s->WorldToScreen(out, point, index)); break;
    case 4: NoteFloats("to clip", out, 3, s->WorldToClip(out, point, index)); break;
    case 5: NoteFloats("screen to world", out, 3, s->ScreenToWorld(out, point, index)); break;
    case 6: NoteFloats("clip to world", out, 3, s->ClipToWorld(out, point, index)); break;
    case 7:
    case 8: { // 123
        Parented parented(n);
        s->Render();
        break;
    }
    case 9:
    case 10: { // 83
        Parented parented(n);
        s->Slot83(&gView, reinterpret_cast<void *>(uintptr_t(rng())));
        break;
    }
    case 11: { // 82
        gView.mNode = rng() % 6 ? nullptr : static_cast<const void *>(n);
        s->Draw(&gView, rng() % 3 ? 0x3fu : uint32_t(rng()));
        break;
    }
    case 12: // the target, the setting, the frames
        RandomTarget(rng);
        if (rng() % 2) RandomFrames(rng);
        break;
    case 13: { // the node's world transform as it is (fresh)
        RandomRigid(reinterpret_cast<cRigidTransform *>(&Words(n)[20]), rng);
        n->SetFlag(12, false);
        break;
    }
    case 14: { // no framework for one call (the setting reads as on)
        uint32_t *const cell = GameAddress<uint32_t>(kFrameworkCell);
        const uint32_t saved = *cell;
        *cell = 0;
        if (count) NoteFloats("viewport", out, 16, s->GetViewport(out, index));
        const cViewerRefEntry *e = count ? s->GetEntry(index) : nullptr;
        drawfx::Note("entry", uint8_t(e != nullptr));
        *cell = saved;
        NoteEntries("without framework");
        break;
    }
    default:
        OperateEntries(rng() % 16, n, rng);
        break;
    }
}

void NoteView()
{
    for (unsigned i = 0; i < w.nodes.count; ++i) {
        const uint32_t *words = Words(w.nodes[i]);
        drawfx::Note("view words", {words[0x84 / 4], words[0x10c / 4], words[0x104 / 4], words[0x108 / 4]});
    }
}

struct Scopes {
    svcfx::AllocatorScope allocator; // the settings key is a service string: the game's allocator service
    svx::Stands stands;
    sgfx::RegistryScope node{sgfx::kNodeRegistry};
    sgfx::RegistryScope transform{sgfx::kTransformRegistry};
    sgfx::RegistryScope viewer{kViewerRegistry};
    sgtp::RefreshScope refresh;
    Scopes()
    {
        auto &map = *GameAddress<nGZEvent::cEventAttributeMap>(kViewerRegistry.map);
        map[kFov] = {kFov, "fov", 0x777u, 0x500u, 0x600u, 4};
        map[kNearFar] = {kNearFar, "nearFarClip", 0x778u, 0x501u, 0x601u, 8};
    }
};

// The name iterators' pool (a cRZFastFixedAllocator2<0x14> at 0x10f639c: its current page at +4) on a test page.
struct NamePool {
    static constexpr uint32_t kCell = 0x10f63a0, kPage = 0x800, kBlock = 0x14, kTag = 0x7b300000;
    uint8_t *page;
    uint32_t saved;
    NamePool()
    {
        page = static_cast<uint8_t *>(_aligned_malloc(kPage, kPage));
        saved = *GameAddress<uint32_t>(kCell);
        *GameAddress<uint32_t>(kCell) = P(page);
        Reset(false);
    }
    ~NamePool()
    {
        *GameAddress<uint32_t>(kCell) = saved;
        _aligned_free(page);
    }
    void Reset(bool registered = true)
    {
        std::memset(page, 0, kPage);
        uint32_t *header = reinterpret_cast<uint32_t *>(page);
        header[3] = 1000;
        uint32_t next = 0;
        for (uint32_t at = kPage - kBlock; at >= 0x10; at -= kBlock) {
            *reinterpret_cast<uint32_t *>(page + at) = next;
            next = P(page + at);
        }
        header[2] = next;
        if (registered) drawfx::AddRange(page, kPage, kTag);
    }
    void Note() const
    {
        const uint32_t *header = reinterpret_cast<const uint32_t *>(page);
        drawfx::Note("pool", {drawfx::Ptr(header[2]), header[3]});
    }
};

void NoteIterator(const char *label, cIArrayAttributeEntryIterator *it, bool full)
{
    if (!it) {
        drawfx::Note(label, 0u);
        return;
    }
    const uint32_t *words = reinterpret_cast<const uint32_t *>(it);
    drawfx::Note(label, {full ? drawfx::Norm(P(it)) : 0u, words[1], full ? words[2] : 0u});
    const cArrayAttributeEntryName name = it->Get();
    sgfx::NoteText("group", name.mWord0 ? reinterpret_cast<const char *>(uintptr_t(name.mWord0)) : "(null)");
    sgfx::NoteText("name", name.mWord1 ? reinterpret_cast<const char *>(uintptr_t(name.mWord1)) : "(null)");
}

void OperateNames(difftest::Context &ctx, int k, NamePool &pool, std::mt19937 &rng)
{
    cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
    Mark(ctx, k, 0);
    if (rng() % 3 == 0) As(n)->ResizeEntries(rng() % 4);
    const uint32_t id = rng() % 5 ? (rng() % 2 ? kFov : kNearFar) : rng() % 2 ? 0x1234 : uint32_t(rng());
    drawfx::Note("id", id);
    cArrayAttributeEntryRange range{};
    const void *const returned = n->MakeIterator51(&range, id);
    drawfx::Note("returns the range", uint8_t(returned == &range));
    const bool full = id == kFov || id == kNearFar;
    NoteIterator("current", range.mCurrent, full);
    NoteIterator("begin", range.mBegin, full);
    NoteIterator("end", range.mEnd, full);
    pool.Note();
    cIArrayAttributeEntryIterator *const begin = range.mBegin, *const end = range.mEnd;
    drawfx::Note("equal", {uint8_t(range.mCurrent->IsEqual(begin)), uint8_t(begin->IsEqual(end)), uint8_t(end->IsEqual(begin)),
                           uint8_t(range.mCurrent->IsEqual(end))});
    for (int step = 0; step < 4 && !range.mCurrent->IsEqual(end); ++step) {
        const cArrayAttributeEntryName name = range.mCurrent->Get();
        drawfx::Note("walked", {uint32_t(name.mWord0 != 0), uint32_t(name.mWord1 != 0)});
        range.mCurrent->Next();
    }
    NoteIterator("walked to", range.mCurrent, full);
    range.mCurrent->Previous();
    drawfx::Note("previous", uint8_t(range.mCurrent->IsEqual(begin)));
    range.mCurrent->Advance(int32_t(rng() % 5) - 2);
    drawfx::Note("advanced", {uint8_t(range.mCurrent->IsEqual(begin)), uint8_t(range.mCurrent->IsEqual(end))});
    const cArrayAttributeEntryName at = range.mCurrent->At(int32_t(rng()));
    drawfx::Note("at", {uint32_t(at.mWord0 != 0), uint32_t(at.mWord1 != 0)});
    cIArrayAttributeEntryIterator *const clone = range.mEnd->Clone();
    NoteIterator("clone", clone, full);
    pool.Note();
    void *queried = nullptr;
    drawfx::Note("query", {uint8_t(begin->QueryInterface(1, &queried)), uint8_t(queried == begin), uint8_t(begin->QueryInterface(2, &queried)),
                           uint8_t(begin->QueryInterface(1, nullptr))});
    if (queried) begin->Release();
    drawfx::Note("refs", {uint32_t(begin->AddRef()), uint32_t(begin->Release())});
    if (clone) clone->AddRef(), clone->Release();
    for (cIGZUnknown *it : {static_cast<cIGZUnknown *>(range.mCurrent), static_cast<cIGZUnknown *>(range.mBegin),
                            static_cast<cIGZUnknown *>(range.mEnd)})
        drawfx::Note("released", it->Release());
    pool.Note();
}

// The base's state a clone copies: textures, references, the bound, the clear words.
void GiveBase(cICompositionTreeNode *n, std::mt19937 &rng)
{
    cIViewerRefNode *s = As(n);
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
    if (rng() % 2) s->SetLayerTagMatch(uint32_t(rng()));
    if (rng() % 2) {
        float plane[4];
        for (float &f : plane) f = Any(rng);
        s->Slot101(rng() % 3 ? reinterpret_cast<cIBoundedNode *>(&svx::gVw.bound) : nullptr, plane, uint32_t(rng()));
    }
}

// ---- persistence (Read 0xa66eb0, Write 0xa68060, the +8 interface's slots 3 / 5: 0xa689d0 / 0xa69a70) ----

constexpr uint32_t kRead = 0xa66eb0;
constexpr uint32_t kWrite = 0xa68060;
constexpr unsigned kLegacies = 4;

// The legacy viewers records before version 12 hand out: our cViewer_LEGACY (sg/cViewer_LEGACY.h), held once here.
cViewer_LEGACY *gLegacy[kLegacies];

void PersistBuild(uint32_t seed, drawfx::Cells &cells)
{
    Build(seed, cells);
    vtape::Install();
}

// The node's Impl (+0xc) on either side: the image's body or ours.
void CallRead(difftest::Context &ctx, bool original, cICompositionTreeNode *n, bool face)
{
    auto *reader = &sgfx::gReader.fake;
    drawfx::Note("read through the face", uint8_t(face));
    uint8_t *const node = reinterpret_cast<uint8_t *>(n);
    if (face) { // the +8 interface's Read (slot 3)
        using Fn = void(__thiscall *)(void *, void *);
        void *const persist = node + 8;
        (*reinterpret_cast<Fn *const *>(persist))[3](persist, reader);
    } else if (original) {
        ctx.Original<void(__thiscall *)(void *, void *, void *)>(kRead)(node + 0xc, n, reader);
    } else {
        reinterpret_cast<cViewerRefNodeImpl *>(node + 0xc)
            ->Read(reinterpret_cast<cViewerRefNode *>(n), reinterpret_cast<ifaces::cIGZPersistReader *>(reader));
    }
}
void CallWrite(difftest::Context &ctx, bool original, cICompositionTreeNode *n, bool face)
{
    auto *writer = &sgfx::gWriter.fake;
    drawfx::Note("write through the face", uint8_t(face));
    uint8_t *const node = reinterpret_cast<uint8_t *>(n);
    if (face) { // the +8 interface's Write (slot 5)
        using Fn = void(__thiscall *)(void *, void *);
        void *const persist = node + 8;
        (*reinterpret_cast<Fn *const *>(persist))[5](persist, writer);
    } else if (original) {
        ctx.Original<void(__thiscall *)(void *, void *, void *)>(kWrite)(node + 0xc, n, writer);
    } else {
        reinterpret_cast<cViewerRefNodeImpl *>(node + 0xc)
            ->Write(reinterpret_cast<cViewerRefNode *>(n), reinterpret_cast<ifaces::cIGZPersistWriter *>(writer));
    }
}

using vtape::BoolWord; // (the base parts' records and these: sg_viewer_ref_tape.h)
using vtape::StreamGroup;
using vtape::TapeBase;

void TapeEntry(uint32_t version, std::mt19937 &rng)
{
    vtape::Tape &t = vtape::gTape;
    const uint32_t type = rng() % 8 ? rng() % 3 : uint32_t(rng());
    t.Word(type);
    if (version >= 0xe) t.Word(BoolWord(rng));
    for (int i = 0; i < 20; ++i) t.Float(rng() % 3 ? sgtx::RandomFloat(rng) : svx::WildFloat(rng));
    for (int i = 0; i < 7; ++i) t.Float(Any(rng)); // the rotation, the translation
    if (version < 0xd || type == cViewerRefEntry::kExplicit)
        for (int i = 0; i < 16; ++i) t.Float(Any(rng));
    const uint32_t materials = rng() % 4;
    t.Word(materials);
    static const char *const kNames[] = {"mat_a", "*grp!mat_b", "x_missing", "", "##0x1c0532fa!mat_c", "lambert"};
    for (uint32_t m = 0; m < materials; ++m) {
        const uint32_t present = BoolWord(rng);
        t.Word(present);
        if (present & 0xff) t.Text(kNames[rng() % 6]);
    }
}

// A legacy record: the viewer (its view random, materials uncounted), then from version 10 the placement (a 3x3 matrix
// before version 11, a quaternion after) and the translation.
void TapeLegacy(uint32_t version, unsigned k, std::mt19937 &rng)
{
    cViewer_LEGACY *const l = new cViewer_LEGACY;
    l->AddRef();
    gLegacy[k] = l;
    cViewerRefEntry *view = &l->mView;
    float words[0xb4 / 4];
    for (float &f : words) f = Any(rng);
    std::memcpy(view, words, sizeof words);
    view->mMaterials.resize(rng() % 3);
    for (auto &m : view->mMaterials) m.mIndex = rng() % 4 ? int32_t(40 + rng() % 20) : -1;
    view->mDetailSlot = rng() % 2 ? -1 : int32_t(rng() % 64);
    view->mAspect = Any(rng);
    view->mDirty = uint8_t(rng());
    drawfx::AddRange(l, sizeof *l, 0x76000000 | k << 12);
    vtape::Tape &t = vtape::gTape;
    t.Ref(static_cast<cSGResource *>(l)); // the object itself (what its QueryInterface gives for kIid), AddRef'd
    if (version >= 10) {
        const unsigned rotation = version < 11 ? 9 : 4;
        for (unsigned i = 0; i < rotation; ++i) t.Float(rng() % 4 ? float(int(rng() % 5) - 2) * 0.5f : Any(rng));
        for (int i = 0; i < 3; ++i) t.Float(Any(rng));
    }
}

void DropLegacies(unsigned count)
{
    for (unsigned k = 0; k < count; ++k) {
        drawfx::Note("legacy refs", gLegacy[k]->GetRefCount());
        gLegacy[k]->Release();
    }
}

// Entries with materials named through the setters (GetInfo answers each index a name, or none).
void GiveMaterials(cICompositionTreeNode *n, std::mt19937 &rng)
{
    const uint32_t count = EntryCount(n);
    for (uint32_t e = 0; e < count; ++e) {
        if (rng() % 3 == 0) continue;
        As(n)->SetMaterialCount(e, 1 + rng() % 3);
        for (int k = 0; k < 3; ++k) OperateEntries(3, n, rng);
    }
}

} // namespace

DIFF_TEST(SG_ViewerRefNode_Factory, kFactory)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "factory", 32, Build, [&](bool original, std::mt19937 &rng) {
        cICompositionTreeNode *n;
        if (rng() % 2) { // 0xa644a0 against Construct, in fresh memory
            void *const memory = ::operator new(kSize);
            cViewerRefNode *const made = original ? ctx.Original<cViewerRefNode *(__thiscall *)(void *)>(kConstruct)(memory)
                                                  : static_cast<cViewerRefNode *>(memory)->Construct();
            drawfx::Note("in place", uint8_t(static_cast<void *>(made) == memory));
            n = reinterpret_cast<cICompositionTreeNode *>(made);
        } else {
            n = original ? ctx.Original<sgfx::Factory>(kFactory)() : MakeOurs();
        }
        drawfx::Note("count", CountOf(n));
        Hold(n);
        w.nodes.size = kSize;
        NoteNodes("new node");
        Words(n)[29] = uint32_t(rng());
        if (rng() % 2) {
            As(n)->ResizeEntries(1 + rng() % 3);
            for (int k = 0; k < 4; ++k) OperateEntries(rng() % 14, n, rng);
        }
        NoteNodes("held");
        const unsigned end = rng() % 3;
        drawfx::Note("end", end);
        if (end == 0) {
            ReleaseAll();
            return;
        }
        w.nodes.count = 0;
        difftest::FreeRecorder freed;
        if (end == 1) { // 0xa64820 against Destruct, then the memory freed
            if (original) ctx.Original<void(__thiscall *)(void *)>(kDestruct)(n);
            else reinterpret_cast<cViewerRefNode *>(n)->Destruct();
            ::operator delete(n);
        }
        const uint32_t flags = rng() % 2;
        if (end == 2) { // 0xa64bd0 against DeletingDestructor (bit 0: freed)
            drawfx::Note("flags", flags);
            void *r = original ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting)(n, flags)
                               : reinterpret_cast<cViewerRefNode *>(n)->DeletingDestructor(flags);
            drawfx::Note("returns this", uint8_t(r == n));
        }
        freed.Stop();
        if (end == 2 && !(flags & 1)) ::operator delete(n); // destroyed only: the block is still ours to free
        size_t sizes[64];
        const size_t count = freed.SortedSizes(sizes, 64);
        drawfx::Call c;
        c.note = "freed";
        for (size_t i = 0; i < count; ++i) c.words.push_back(uint32_t(sizes[i]));
        drawfx::gLog.push_back(std::move(c));
        NoteFakes();
    });
    w.manager.Drop();
}

// The entry setters behind slots 124-138, each reached through our vtable: ResizeEntries 0xa67950, SetEntryPlacement
// 0xa67b60, SetMaterialCount 0xa678f0, SetMaterial 0xa67a70, SetViewport 0xa67b90, SetSubrect 0xa677f0, SetFixedAspect
// 0xa67870, SetPixelAspect 0xa67a40, SetPerspective 0xa67840, SetDepthRange 0xa67890, SetNearFar 0xa678c0, SetOffsets
// 0xa677c0, SetOrtho 0xa679e0, SetProjection 0xa67770.
DIFF_TEST(SG_ViewerRefNode_Slots, 0xa688e0)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "slots", 64, Build, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        GiveEntries(rng);
        NoteNodes("built");
        for (int k = 0; k < 40; ++k) OperateSlots(ctx, k, rng);
        NoteNodes("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ViewerRefNode_View, 0xa656f0)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "view", 96, Build, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        GiveEntries(rng);
        NoteNodes("built");
        for (int k = 0; k < 48; ++k) OperateView(ctx, k, rng);
        NoteView();
        NoteNodes("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ViewerRefNode_Clone, 0xa655a0)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "clone", 64, Build, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        GiveEntries(rng);
        for (unsigned i = 0; i < w.nodes.count; ++i)
            if (rng() % 2) GiveBase(w.nodes[i], rng);
        for (int round = 0; round < 3; ++round) {
            cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
            const uint32_t mode = rng() % 3;
            cINodeCloneContext *context = nullptr;
            const bool viaConstruct = rng() % 2;
            if (rng() % 3 == 0 && mode != 1 && w.nodes.count < sgfx::Nodes::kMax) {
                cICompositionTreeNode *prepared =
                    original ? ctx.Original<sgfx::Factory>(kFactory)() : viaConstruct ? MakeByConstruct() : MakeOurs();
                Hold(prepared);
                Words(prepared)[29] = uint32_t(rng());
                if (rng() % 2) As(prepared)->ResizeEntries(rng() % 3);
                w.context.value[0] = P(prepared);
                w.context.value[1] = rng() % 2 ? P(&svx::gVw.bound) : 0;
                context = reinterpret_cast<cINodeCloneContext *>(&w.context);
            }
            const uint32_t iids[] = {sgfx::kIidTree, sgfx::kIidGraph, cViewerRefNode::kIid, 0x12345678};
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

// Slot 51 (0xa64c80 -> the Impl's MakeIterator51 0xa66440) with the iterators it hands out
// (cViewerRefAttributeNameIterator: constructors 0xa64470 / 0xa64440, Clone 0xa65690, deleting destructor 0xa64b90,
// the pool 0xa648c0 / 0xa648f0).
DIFF_TEST(SG_ViewerRefNode_Names, 0xa66440)
{
    Scopes scopes;
    NamePool pool;
    sgfx::CheckSides(ctx, "names", 64, [&](uint32_t seed, drawfx::Cells &cells) {
        Build(seed, cells);
        pool.Reset();
    }, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        GiveEntries(rng);
        NoteNodes("built");
        for (int k = 0; k < 24; ++k) OperateNames(ctx, k, pool, rng);
        NoteNodes("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

// Read 0xa66eb0, through the +8 interface's slot 3 (0xa689d0) or on the Impl: records of versions 9 to 15 read over a
// node that may hold entries already. Before 12 each entry is a legacy viewer's view (from 10 with the placement: a 3x3
// matrix at 10, a quaternion from 11); from 12 the entry's fields, the projection when the record holds one (before 13,
// or an explicit entry), the fixed-aspect flag from 14, and materials named in the stream's group (slot 28) or none.
DIFF_TEST(SG_ViewerRefNode_Read, kRead)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "read", 128, PersistBuild, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        GiveEntries(rng);
        cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
        const uint32_t version = 9 + rng() % 7;
        sgfx::gReader.fake.value[0] = StreamGroup(rng);
        TapeBase(version, rng);
        const uint32_t count = rng() % 5 ? rng() % 4 : 0;
        vtape::gTape.Word(count);
        unsigned legacies = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (version < 0xc)
                TapeLegacy(version, legacies++, rng);
            else
                TapeEntry(version, rng);
        }
        if (rng() % 8 == 0) vtape::gTape.Word(0x5a5a5a5a); // more than the record holds
        NoteNodes("built");
        CallRead(ctx, original, n, rng() % 2);
        vtape::gTape.NotePosition("tape read");
        NoteNodes("read");
        DropLegacies(legacies);
        ReleaseAll();
    });
    w.manager.Drop();
}

// Write 0xa68060, through the +8 interface's slot 5 (0xa69a70) or on the Impl: every node of a tree with entries made
// through the setters and materials named (GetInfo answers a name, a "*group!name" reference, or none).
DIFF_TEST(SG_ViewerRefNode_Write, kWrite)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "write", 128, PersistBuild, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        GiveEntries(rng);
        for (unsigned i = 0; i < w.nodes.count; ++i) GiveMaterials(w.nodes[i], rng);
        for (unsigned i = 0; i < w.nodes.count; ++i) {
            vtape::gTape.Clear();
            // (What is left allocated: the original's strings are cRZStrings, ours std::strings, so only the balance.)
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

// A round trip through the +8 interface (Write 0xa69a70 into the tape, Read 0xa689d0 into a fresh node of the same
// side, which may hold entries already), in the stream's group or none.
DIFF_TEST(SG_ViewerRefNode_Persist, 0xa689d0)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "persist", 96, PersistBuild, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        GiveEntries(rng);
        cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
        GiveMaterials(n, rng);
        CallWrite(ctx, original, n, true);
        vtape::gTape.NoteItems("written");
        sgfx::gReader.fake.value[0] = StreamGroup(rng);
        if (w.nodes.count < sgfx::Nodes::kMax) {
            cICompositionTreeNode *fresh = original ? ctx.Original<sgfx::Factory>(kFactory)() : MakeOurs();
            Hold(fresh);
            Words(fresh)[29] = uint32_t(rng());
            if (rng() % 2) As(fresh)->ResizeEntries(1 + rng() % 3);
            CallRead(ctx, original, fresh, true);
            vtape::gTape.NotePosition("tape read");
        }
        NoteNodes("read back");
        ReleaseAll();
    });
    w.manager.Drop();
}
