// Differential tests of nGZSceneGraph::cShapeRefNode (lanes SN and SP, scenegraph H2a / H2b): the game's shape-reference
// node (its factory 0xa55270, its vtables 0xe6ef70 / 0xe6f108 and the bodies behind them) against ours (Create, our
// vtables), each side building its own nodes with its own factory. In half the seeds our side takes the game-shaped
// route (MakeSidedTree, below): our constructor hook's Construct in fresh memory and the hook replacements laid in a
// vtable copy, as cScreenShapeRef (vtable 0xe91708) runs them in game; so the slot adapters (0xa58060, 0xa55370,
// 0xa553b0, 0xa54ec0, 0xa58950, 0xa588e0, 0xa548f0, 0xa58710, 0xa553f0, 0xa55400) and the destructor hooks run.
//  - Factory: a fresh node's words, flags and count; then released (the blocks freed). The levels' constructors 0xa541e0
//    (the shape-reference level 0xa54070, the renderable level 0xa54010, the Impl 0xa54200; half the seeds 0xa541e0
//    itself against Construct, in place) and destructors (the deleting destructor 0xa54880, the shape-reference level
//    0xa543d0, the Impl 0xa544f0).
//  - Shapes (lane SP): trees of nodes with shapes added and removed through 94 / 95 (AddShape 0xa55750, RemoveShape
//    0xa570f0), events 3 (0xa55370, the Impl's 0xa560f0), queries 4 (0xa553b0, the Impl's 0xa561a0), 97 (0xa553f0,
//    0xa561f0), the shapes' detail keys (0xa59340) and the material block 0xa589a0 against a fake material manager in
//    cell 0x10f8fcc; released still holding shapes (0xa543d0 removes them front first).
//  - Slots: small trees of nodes driven through QueryInterface 0xa58060 (and the renderable level's shared 0xa58020),
//    IsKindOf 0xa57220, Accept / AcceptReadOnly 0xa58950, slot 70 0xa57cf0, the layer slots (shared 0xa54900 0xa54910
//    0xa586f0 0xa58700 0x1d44f0 0xa57ff0 0xa58720), and the shape-reference slots 87 (shared 0x559c60), 88 (0x251bf0),
//    89 / 90 (0x24bdc0 / 0x4daa20), 91 0xa588d0, 92 / 93 0xa588e0, 96 0xa552e0, 98 (shared 0xa58000), 99 0xa55400, on
//    shape lists (fakes of cIShape 0x3c6eb1f0, and nulls) the test plants (PlantShapes: adding through slot 94 runs the
//    material block 0xa589a0, which the Shapes test drives).
//  - Clone: 15 (0xa54ec0) with and without a clone context, in each mode, of nodes without shapes (the copying 0xa57d00).
//  - Persist (lane TB, H2c): trees of nodes with shapes that have morphs (shape slot 58 names them), driven through the
//    persist face's Write (0xa57b80: the morph names' texts, the weights, the block's data), Read (0xa56c90: shapes,
//    weights read or skipped, names read and dropped, the block's data read or skipped, by record version), GetTypeInfo
//    (0xa57240), clones (0xa57d00 copying names, weights and data), shapes added and removed and morph names changed
//    (the morph rebuild 0xa580f0 keeping weights by name, the name maker 0xa57e60, the all-near-zero test 0xa54930);
//    the pool's counts compared after the release.
//  - Bounds (lane TB, H2d): trees of shaped nodes under transforms, the shapes growing the node's box (shape slots 60 /
//    59) when the world refresh (or a direct call) runs slot 60 (0xa59330, the Impl's 0xa572f0: the block's
//    transforms from the records, the box from the shapes), the boxes 71 / 72 (0xa57f60 / 0xa57f80, the Impl's
//    0xa569e0 / 0xa56930), and the hits 73 / 74 / 75 (0xa57de0 / 0xa57e00 / 0xa57f30, the Impl's 0xa56230 / 0xa564b0 /
//    0xa56720) against a fake bound and linear and the shapes' parts (fake bounds in their level infos).
//  - Attributes (lane TD, H2e): shaped nodes with material blocks driven through SetAttribute 7 (0xa593a0, the Impl's
//    0xa575a0), GetAttribute 10 (0xa580a0, the Impl's 0xa56b00) on the float lists, the morph weights and the colour
//    (and the transform Impl's ids), and the attribute names 51 (0xa54970, the Impl's 0xa55830: the name list 0xa54740
//    and its iterators 0xa541a0, 0xa54160, 0xa54480, 0xa54840, 0xa54fb0, 0xa550b0, 0xa57f50, 0xa58010, 0xa54920,
//    0xa550f0 from the pool allocate 0xa545e0) walked, cloned and released.
//  - Draw (lane TD, H2e): Draw 82 (0xa548d0, the Impl's 0xa555d0) against a fake drawer (the display list's slot 25)
//    with plane masks and the view's frustum, slot 83 (0xa548b0, the Impl's 0xa55430: the shapes' slot 46, the rest
//    transforms checked every eleventh call through 0xae79e0), and IsKindOf 25 (0xa58930, the Impl's 0xa57220).
//  - Behavior (lane TF, H2f): shaped nodes with listeners driven through NotifyListeners 5 (0xa58850), AskListeners 6
//    (0xa55230), GetEventCount 12 (0xa57fa0), GetEventIds 13 (0xa54990), GetEventAttribute 14 (0xa54ea0, the Impl's
//    0xa55f00) with and without material blocks and morphs, and the +8 face's QueryInterface 0xa58092 and Release
//    0xa586e5.
// Left out of the comparison: the vtable words 0 and 2 (each side's own); word 29 (the Impl's +0x68, the detail limit the
// renderable constructor never writes: a fresh node holds what its allocator left; slot 88 is read after the test
// plants it); and the vector words (31-36: the shapes' and the detail records' begin / end / capacity; 44-49: the two
// vectors at +0xa4 / +0xb0), heap addresses, whose contents are noted instead.
#include "sg_renderable_fx.h"
#include "sg_transform_pilot.h"
#include "sgfx.h"

#include "sg/cShape.h"
#include "hook/hooks.h"
#include "sg/cNullArrayAttributeEntryIterator.h"
#include "sg/cShapeRefNode.h"
#include "sg/cViewerRefNodeBaseImpl.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <array>
#include <bit>
#include <cfloat>
#include <climits>
#include <cstring>
#include <random>

#include <windows.h>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kFactory = 0xa55270;
constexpr uint32_t kSize = cShapeRefNode::kSize;
constexpr uint64_t kSkipWords = 0b101ull | 1ull << 29 | 0x3full << 31 | 0x3full << 44;

// The shape-reference behaviour's registry (0xa585a0, ours since lane TF; stood in by the scope on both sides, the game's
// function and ours, over the planted map; tests/sg_shape_ref_registry.cpp tests it): its map 0x10f62d0 and guard 0x10f62d8,
// its holder 0xa53800's once byte 0x10f62e0 and guard 0x10f62e8.
constexpr sgfx::RegistryStatics kShapeRefRegistry{0x10f62d0, 0x10f62d8, 0x10f62e0, 0x10f62e8};

// A shape (cIShape): 29 (node) when added to a node, 30 (node) when removed (0xa55750 / 0xa570f0).
constexpr std::array<Slot, 61> MakeShapeSlots()
{
    std::array<Slot, 61> a{};
    for (Slot &s : a) s = {0, Ret::Int, false, "", ""};
    a[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    a[1] = {0, Ret::Int, true, "AddRef", ""};
    a[2] = {0, Ret::Int, true, "Release", ""};
    a[5] = {3, Ret::Int, true, "NotifyListeners", "uua"};
    a[29] = {1, Ret::Int, true, "Attached", "a"};
    a[30] = {1, Ret::Int, true, "Detached", "a"};
    // 46 (slot 83's draw: the flags, the material table's and weights' places, the block's transforms, slot 83's first
    // argument, the shape's detail handle)
    a[46] = {6, Ret::Int, true, "DrawShape", "uaaaau"};
    a[52] = {1, Ret::Int, true, "LevelInfo", "a"};
    a[54] = {2, Ret::Int, true, "GetSubsetBindings", "au"};
    a[58] = {2, Ret::Int, true, "MorphName", "au"};
    a[59] = {4, Ret::Int, true, "GrowBoxPlaced", "F6F7u@2x7"}; // the box, the inverted transform, the block's transforms
    a[60] = {1, Ret::Int, true, "GrowBox", "F6"};
    return a;
}
constexpr auto kShapeSlots = MakeShapeSlots();
constexpr drawfx::Iface kShape{"Shape", 61, kShapeSlots.data()};

struct World {
    sgfx::Nodes nodes;
    Fake traversal, context;
    Fake shapes[3];
    sgfx::Manager manager;
};
World w;
cIShape *gPlanted[sgfx::Nodes::kMax][4]; // the planted shape lists (PlantShapes)

cICompositionTreeNode *__cdecl MakeOurs() { return cShapeRefNode::Create(); }
sgfx::Factory Maker(difftest::Context &ctx, bool original)
{
    return original ? ctx.Original<sgfx::Factory>(kFactory) : &MakeOurs;
}
cIShapeRefNode *As(cICompositionTreeNode *n) { return static_cast<cIShapeRefNode *>(static_cast<cIRenderableNode *>(n)); }
uint32_t CountOf(const cICompositionTreeNode *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }
const uint32_t *Words(const cICompositionTreeNode *n) { return reinterpret_cast<const uint32_t *>(n); }

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gPool.Reset(cells);
    sgfx::MakeTraversal(w.traversal, 450, rng() % 3);
    for (unsigned i = 0; i < 3; ++i) w.shapes[i].Reset<kShape>(700 + i);
    w.nodes = sgfx::Nodes{};
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, cShapeRefNodeBehaviorTraits::kEventManagerCell);
    sgrx::Setup(rng);
    drawfx::AddRange(gPlanted, sizeof(gPlanted), 0x7d000000);
}

// The vectors the words leave out: each vector's size and capacity, the shapes (normalised) and the records.
void NoteVectors(const char *label)
{
    for (unsigned i = 0; i < w.nodes.count; ++i) {
        const uint32_t *v = Words(w.nodes[i]);
        drawfx::Call c;
        c.note = label;
        for (unsigned word : {31u, 34u, 44u, 47u}) {
            const uint32_t first = v[word], last = v[word + 1], end = v[word + 2];
            c.words.push_back(last - first);
            c.words.push_back(end - first);
        }
        for (uint32_t p = v[31]; p != v[32]; p += 4) c.words.push_back(drawfx::Norm(*reinterpret_cast<uint32_t *>(p)));
        for (uint32_t p = v[34]; p != v[35]; p += 4) c.words.push_back(*reinterpret_cast<uint32_t *>(p));
        drawfx::gLog.push_back(std::move(c));
    }
}

void NoteNodes(const char *label)
{
    w.nodes.NoteShape(label);
    w.nodes.NoteWords(label, kSkipWords);
    NoteVectors(label);
}

void Hold(cICompositionTreeNode *n)
{
    n->AddRef();
    drawfx::AddRange(n, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
    w.nodes.at[w.nodes.count++] = n;
}

// The children a deep clone made (new nodes), held and registered like the others.
void HoldSubtree(cICompositionTreeNode *root)
{
    for (cICompositionTreeNode *c = root->GetFirstChild(); c; c = c->GetNextSibling()) {
        if (drawfx::Norm(P(c)) == P(c) && w.nodes.count < sgfx::Nodes::kMax) {
            Hold(c);
            const_cast<uint32_t *>(Words(c))[29] = 0x4321;
        }
        HoldSubtree(c);
    }
}

void MakeTree(sgfx::Factory make, std::mt19937 &rng)
{
    const unsigned n = 1 + rng() % 4;
    int parents[sgfx::Nodes::kMax];
    parents[0] = -1;
    for (unsigned i = 1; i < n; ++i) parents[i] = rng() % 3 ? int(rng() % i) : -1;
    w.nodes.Make(make, n, kSize);
    w.nodes.Link(parents);
    // The detail limit planted (word 29: left unwritten by the constructors).
    for (unsigned i = 0; i < n; ++i) const_cast<uint32_t *>(Words(w.nodes[i]))[29] = uint32_t(rng());
}

// The game-shaped route (our side, half the seeds): nodes as the game builds a cScreenShapeRef (0xc09820), the memory
// allocated and our hooked constructor 0xa541e0 (Construct) run in it, then a vtable laid over ours that holds, in each
// slot cScreenShapeRef's vtable 0xe91708 shares with cShapeRefNode's, what that slot runs once the hooks are in: the hook
// replacement (0xa58060, 0xa55370, 0xa553b0, 0xa54ec0, 0xa58950 twice, 0xa588e0 twice, 0xa548f0, 0xa58710, 0xa553f0,
// 0xa55400, 0xa58850, 0xa55230, 0xa57fa0, 0xa54990, 0xa54ea0; slot 100 the deleting destructor 0xa54880, or on odd nodes a
// stand-in for it calling the destructor hook 0xa543d0), and over the +8 face's the face hooks (0xa58092, 0xa586e5). The
// original side builds and calls the game's as before. An adapter that dispatched virtually would call
// itself here (no end: the test fails).
void *gShaped[sgfx::Nodes::kMax][101];
void *gShapedFace[sgfx::Nodes::kMax][7];
cICompositionTreeNode *__cdecl MakeByConstruct()
{
    return static_cast<cShapeRefNode *>(::operator new(kSize))->Construct();
}
struct DestructStandIn : cShapeRefNode {
    cShapeRefNode *DeleteThis(uint32_t flags)
    {
        Destruct();
        if (flags & 1) ::operator delete(this, kSize);
        return this;
    }
};
void LayGameShape(unsigned i)
{
    void **const object = reinterpret_cast<void **>(w.nodes[i]);
    void **const table = gShaped[i];
    std::memcpy(table, object[0], sizeof(gShaped[i]));
    table[0] = MemberAddress<&cShapeRefNode::HookQueryInterface>();
    table[3] = MemberAddress<&cShapeRefNode::HookOnEvent>();
    table[4] = MemberAddress<&cShapeRefNode::HookOnQuery>();
    table[15] = MemberAddress<&cShapeRefNode::HookClone>();
    table[30] = table[50] = MemberAddress<&cShapeRefNode::HookAccept>();
    table[92] = table[93] = MemberAddress<&cShapeRefNode::HookGetShapes>();
    table[94] = MemberAddress<&cShapeRefNode::HookAddShape>();
    table[95] = MemberAddress<&cShapeRefNode::HookRemoveShape>();
    table[97] = MemberAddress<&cShapeRefNode::HookAnyShapeSlot52>();
    table[99] = MemberAddress<&cShapeRefNode::HookHasShape>();
    table[60] = MemberAddress<&cShapeRefNode::HookOnContentChanged>();
    table[71] = MemberAddress<&cShapeRefNode::HookGetBoundBoxIn>();
    table[72] = MemberAddress<&cShapeRefNode::HookGetBoundBox>();
    table[73] = MemberAddress<&cShapeRefNode::HookIntersectsBound>();
    table[74] = MemberAddress<&cShapeRefNode::HookIntersectsLinear>();
    table[75] = MemberAddress<&cShapeRefNode::HookFirstHitLinear>();
    table[7] = MemberAddress<&cShapeRefNode::HookSetAttribute>();
    table[10] = MemberAddress<&cShapeRefNode::HookGetAttribute>();
    table[25] = MemberAddress<&cShapeRefNode::HookIsKindOf>();
    table[51] = MemberAddress<&cShapeRefNode::HookMakeIterator51>();
    table[82] = MemberAddress<&cShapeRefNode::HookDraw>();
    table[83] = MemberAddress<&cShapeRefNode::HookSlot83>();
    table[5] = MemberAddress<&cShapeRefNode::HookNotifyListeners>();
    table[6] = MemberAddress<&cShapeRefNode::HookAskListeners>();
    table[12] = MemberAddress<&cShapeRefNode::HookGetEventCount>();
    table[13] = MemberAddress<&cShapeRefNode::HookGetEventIds>();
    table[14] = MemberAddress<&cShapeRefNode::HookGetEventAttribute>();
    table[100] = i % 2 ? MemberAddress<&DestructStandIn::DeleteThis>() : MemberAddress<&cShapeRefNode::DeletingDestructor>();
    object[0] = table;
    void **const face = gShapedFace[i];
    std::memcpy(face, object[2], sizeof(gShapedFace[i]));
    face[0] = reinterpret_cast<void *>(&cShapeRefNode::HookFaceQueryInterface);
    face[2] = reinterpret_cast<void *>(&cShapeRefNode::HookFaceRelease);
    object[2] = face;
}
void MakeSidedTree(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    const bool shaped = rng() % 2;
    MakeTree(original ? Maker(ctx, true) : shaped ? &MakeByConstruct : &MakeOurs, rng);
    if (shaped && !original)
        for (unsigned i = 0; i < w.nodes.count; ++i) LayGameShape(i);
}

// The shape lists, planted: the game's AddShape / RemoveShape (0xa55750 / 0xa570f0) tell the node, whose event body
// (0xa560f0) then rebuilds its material block (0xa589a0: the material manager, the shapes' slot 52, function-local
// statics), which needs the Shapes test's world; the Slots test plants instead. Each node's list points into the test's storage (begin, end, capacity
// written as the game's vector holds them) and is emptied before the node goes.
void PlantShapes(unsigned i, std::mt19937 &rng)
{
    uint32_t *v = const_cast<uint32_t *>(Words(w.nodes[i]));
    const unsigned count = rng() % 5, capacity = count + rng() % (5 - count);
    for (unsigned k = 0; k < count; ++k)
        gPlanted[i][k] = rng() % 6 ? reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]) : nullptr;
    const uint32_t first = capacity ? P(gPlanted[i]) : 0;
    v[31] = first;
    v[32] = first + 4 * count;
    v[33] = first + 4 * capacity;
    drawfx::Note("planted", {i, count, capacity});
}
void UnplantShapes()
{
    for (unsigned i = 0; i < w.nodes.count; ++i) {
        uint32_t *v = const_cast<uint32_t *>(Words(w.nodes[i]));
        if (v[31] >= P(gPlanted) && v[31] < P(gPlanted) + sizeof(gPlanted)) v[31] = v[32] = v[33] = 0;
    }
}

void ReleaseAll()
{
    UnplantShapes();
    // The block at +0xbc (word 50), planted where none is held: the Impl's destructor (0xa544f0) frees it first.
    for (unsigned i = 0; i < w.nodes.count; i += 2) {
        uint32_t *v = const_cast<uint32_t *>(Words(w.nodes[i]));
        if (!v[50]) v[50] = P(HeapAlloc(GetProcessHeap(), 0, 0x28 + 8 * i));
    }
    difftest::FreeRecorder freed;
    w.nodes.Release();
    freed.Stop();
    size_t sizes[64];
    const size_t n = freed.SortedSizes(sizes, 64);
    drawfx::Call c;
    c.note = "freed";
    for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
    drawfx::gLog.push_back(std::move(c));
    for (const Fake &s : w.shapes) drawfx::Note("shape refs", {s.id, uint32_t(s.refs)});
}

void NoteRange(const char *label, const cShapeRange &r)
{
    drawfx::Note(label, {drawfx::Norm(P(r.mFirst)), drawfx::Norm(P(r.mCurrent)), drawfx::Norm(P(r.mEnd)),
                         uint32_t(r.mEnd - r.mFirst)});
}

void Operate(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    cICompositionTreeNode *n = ns[rng() % ns.count];
    cIShapeRefNode *s = As(n);
    const unsigned op = rng() % 14;
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: { // QueryInterface
        const uint32_t iids[] = {cIShapeRefNode::kIid, cRenderableNodeImpl::kIid, 0x4512dd13, 0xdb01d4b2, 0x5aef607b,
                                 0x5b0aaa74, 0xbb11a53e, 1, 0xe51c10f5, 0x7d14eb66, 0x7d3a2385, 0xfb06dba0};
        const uint32_t iid = iids[rng() % 12];
        void *out = nullptr;
        const uint32_t before = CountOf(n);
        const bool r = rng() % 8 ? n->QueryInterface(iid, &out) : n->QueryInterface(iid, nullptr);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out)), CountOf(n) - before});
        if (CountOf(n) != before) n->Release();
        break;
    }
    case 1: { // kinds
        const uint32_t iids[] = {cIShapeRefNode::kIid, cRenderableNodeImpl::kIid, 0x4512dd13, 0x5aef607b, 0x3b253dc7,
                                 0xbb11a53e, 0x5b0aaa74, 1, 0, 0xdb01d4b2, 0xe51c10f5, 0x7d14eb66, 0x7d3a2385};
        for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
        break;
    }
    case 2: { // traversals
        auto *t = reinterpret_cast<cICompositionTreeTraversal *>(&w.traversal);
        drawfx::Note("accept", {n->Accept(t), n->AcceptReadOnly(t)});
        break;
    }
    case 3: { // the flags half-word and slot 70
        const uint16_t flags[] = {0, 0x100, 0xffff, 0xfeff, uint16_t(rng())};
        s->SetShapeFlags(flags[rng() % 5]);
        drawfx::Note("shape flags", {s->GetShapeFlags(), uint8_t(static_cast<cIBoundedNode *>(s)->IsEverywhere()), s->GetRefKind()});
        break;
    }
    case 4: { // 88-90
        if (rng() % 2) s->SetWord6c(rng() % 2 ? 0 : uint32_t(rng()));
        drawfx::Note("words", {s->GetDetailLimit(), s->GetWord6c()});
        break;
    }
    case 5: { // the layers by bits
        const uint32_t mask = sgrx::RandomMask(rng);
        if (rng() % 2) s->AddLayerBits(mask);
        else s->RemoveLayerBits(mask);
        if (rng() % 3 == 0) s->SetLayerTag(uint32_t(rng()));
        uint32_t state[3] = {0x5555, 0x6666, 0x7777};
        const uint32_t *r = s->GetLayerState(state);
        drawfx::Note("layers", {uint8_t(r == state), state[0], state[1], state[2], s->CountLayers()});
        break;
    }
    case 6: { // the layers by name
        static const char *const kNames[] = {"alpha", "glowb", "shadowc", "zeta"};
        const char *name = kNames[rng() % 4];
        if (rng() % 2) s->AddLayer(name);
        else s->RemoveLayer(name);
        uint32_t state[2] = {};
        s->GetLayerState(state);
        drawfx::Note("layers", {state[0], state[1], s->CountLayers()});
        break;
    }
    case 7:
    case 8:
    case 9: // a new list planted
        PlantShapes(rng() % ns.count, rng);
        break;
    case 10: { // the shape lists and the search
        NoteRange("shapes", s->GetShapes());
        NoteRange("shapes const", s->GetShapesConst());
        drawfx::Note("count", s->ShapeCount());
        for (Fake &f : w.shapes) drawfx::Note("has", {f.id, uint8_t(s->HasShape(reinterpret_cast<cIShape *>(&f)))});
        drawfx::Note("has null", uint8_t(s->HasShape(nullptr)));
        break;
    }
    case 11: { // move a node
        cICompositionTreeNode *c = ns[rng() % ns.count];
        if (cICompositionTreeNode *p = c->GetParent()) p->RemoveChild(c);
        cICompositionTreeNode *to = ns[rng() % ns.count];
        bool inside = false;
        for (cICompositionTreeNode *a = to; a; a = a->GetParent()) inside |= a == c;
        if (!inside) to->AddChild(c);
        break;
    }
    case 12: { // flags
        uint32_t bit = rng() % 40;
        if ((bit & 31) == 1 || (bit & 31) == 12) bit = 0;
        n->SetFlag(bit, rng() % 2);
        drawfx::Note("flags", {uint8_t(n->TestFlag(bit)), n->GetFlags()});
        break;
    }
    case 13: // the detail limit planted again
        const_cast<uint32_t *>(Words(n))[29] = rng() % 2 ? 0x7fffffffu : uint32_t(rng());
        drawfx::Note("limit", s->GetDetailLimit());
        break;
    }
}

// ---- the shape lists, the detail keys and the material block (SG_ShapeRefNode_Shapes) ----
// The shapes' answers come from per-shape settings the test draws: slot 52 (the 0x20-byte level info: the detail key,
// which is also the length of the material index list, the list, the transform count, the word slot 97 tests; its word
// +8 stays 0, which keeps 0xa580f0's per-shape tree out), slot 54 (the binding range: three cursor fakes per shape, the
// middle one sometimes null) and slot 5 (the subordinate's listeners told, its body noted). The material manager (cell
// 0x10f8fcc, GetCachedQueriedObject's) maps an index to one of four material fakes or none; each material says whether
// it is skipped, how many parameters of each of the four types it has, its data size, each parameter's offset, and
// writes each value into the event it is handed (by the event's vtable). A shape's bindings always include its own
// material indices: a material the index list names that no binding names leaves the original reading past its offsets
// vector (out of the test's scope; the game's shapes list a material per binding).
struct ShapeCfg {
    uint32_t key;        // the detail key and the index count (0-4)
    uint32_t indices[4];
    uint32_t noIndices;  // the index list null
    uint32_t transforms; // the transform slots asked for
    uint32_t word10;     // slot 97's word
    uint32_t bindings[8];
    uint32_t bindingCount;
    uint32_t middle; // the range's middle cursor present
};
struct MaterialCfg {
    uint32_t skip;
    uint32_t counts[4]; // parameters per type
    uint32_t extra;     // data past the parameters
};
ShapeCfg gShapeCfg[3];
uint32_t gMorphs[3];     // each shape's morph count (its level info's word +8; 0 outside the Persist test)
uint32_t gMorphShift[3]; // where its morph names start in kMorphNames
MaterialCfg gMaterialCfg[4];
int gMaterialOf[6];             // a manager index's material, -1 for none
uint32_t gDetailHandles;        // the detail object's registrations so far
struct GrowCfg {
    uint32_t on;
    float box[6]; // min, then max (inside out sometimes)
};
GrowCfg gGrow[3];      // each shape's box for slots 59 / 60 (off outside the Bounds test)
uint32_t gPartsOn[3];  // its level info lists its parts (word +0x1c)
float gRest[3][3][7];  // its rest transforms (word +0x18, when gRestOn): translation, then rotation
uint32_t gRestOn[3];
uint32_t gParts[3][4]; // the parts: fake bounds, or none
uint32_t gStaticVectors[3][64]; // the original's three function-local vectors (0xa589a0), on test storage
constexpr uint32_t kMaterialCell = 0x10f8fcc;
constexpr uint32_t kStaticVectors[3] = {0x10f62a0, 0x10f62b0, 0x10f62c0}; // each {first, last, end}, its guard at +0xc
constexpr uint32_t kParamTypes[4] = {0xbb22dd17, 0x9b5b3300, 0x7b5b330c, 0x5b5b331a};
constexpr uint32_t kParamEvents[4] = {0xdb07f4, 0xdb2bc8, 0xdb5e70, 0xdb07d4};
constexpr uint32_t kTransformTag = 0x7a000000;
constexpr uint32_t kStaleBit = 12; // the node flag a transform change sets (cleared by the world-transform refresh)
constexpr uint32_t kListenedQuery = 0x1234; // a query some nodes' listener answers (one of the registry's ids)

template <unsigned N> constexpr std::array<Slot, N> BaseSlots()
{
    std::array<Slot, N> a{};
    for (Slot &s : a) s = {0, Ret::Int, false, "", ""};
    a[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    a[1] = {0, Ret::Int, true, "AddRef", ""};
    a[2] = {0, Ret::Int, true, "Release", ""};
    return a;
}
// A binding cursor: 4 at the given end, 5 the binding (a cSubsetBinding: 4 words, the material index at +4), 6 next.
constexpr std::array<Slot, 7> MakeCursorSlots()
{
    auto a = BaseSlots<7>();
    a[4] = {1, Ret::Int, true, "AtEnd", "a"};
    a[5] = {1, Ret::Int, true, "Get", "a"};
    a[6] = {0, Ret::Int, true, "Next", ""};
    return a;
}
constexpr auto kCursorSlots = MakeCursorSlots();
constexpr drawfx::Iface kCursor{"BindingCursor", 7, kCursorSlots.data()};
// The material manager: 33 (out, index) fills 0x14 bytes, the material at +8.
constexpr std::array<Slot, 34> MakeManagerSlots()
{
    auto a = BaseSlots<34>();
    a[33] = {2, Ret::Int, true, "Lookup", "au"};
    return a;
}
constexpr auto kManagerSlots = MakeManagerSlots();
constexpr drawfx::Iface kManager{"MaterialManager", 34, kManagerSlots.data()};
// A material: 5 its name (the attribute-name list's), 6 skipped, 9 (type) parameter count, 11 data size, 13 (out, index, type) parameter info (offset at
// +0xc), 14 (event, index) the value written into the event.
constexpr std::array<Slot, 15> MakeMaterialSlots()
{
    auto a = BaseSlots<15>();
    a[5] = {0, Ret::Int, true, "Name", ""};
    a[6] = {0, Ret::Int, true, "Skipped", ""};
    a[9] = {1, Ret::Int, true, "ParamCount", "u"};
    a[11] = {0, Ret::Int, true, "DataSize", ""};
    a[13] = {3, Ret::Int, true, "ParamInfo", "auu"};
    a[14] = {2, Ret::Int, true, "GetParam", "au"};
    return a;
}
constexpr auto kMaterialSlots = MakeMaterialSlots();
constexpr drawfx::Iface kMaterial{"Material", 15, kMaterialSlots.data()};

struct ShapeWorld {
    Fake cursors[3][3]; // per shape: first, middle, end
    Fake manager;
    Fake materials[4];
    Fake listener;
    cICompositionTreeNode *xf[2] = {};
    unsigned xfCount = 0;
};
ShapeWorld sw;

void RandomShape(unsigned i, std::mt19937 &rng)
{
    ShapeCfg &c = gShapeCfg[i];
    c = {};
    c.key = rng() % 5;
    for (uint32_t &x : c.indices) x = rng() % 6;
    c.noIndices = c.key && rng() % 8 == 0;
    c.transforms = rng() % 3 ? 0 : 1 + rng() % 3;
    c.word10 = rng() % 3 == 0;
    for (uint32_t k = 0; k < c.key; ++k) c.bindings[c.bindingCount++] = c.indices[k];
    for (uint32_t k = rng() % 3; k > 0; --k) c.bindings[c.bindingCount++] = rng() % 6;
    for (uint32_t k = c.bindingCount; k > 1; --k) std::swap(c.bindings[k - 1], c.bindings[rng() % k]);
    c.middle = rng() % 2;
    drawfx::Note("shape cfg", {i, c.key, c.noIndices, c.transforms, c.word10, c.bindingCount, c.middle});
}
void RandomMaterials(std::mt19937 &rng)
{
    for (int &m : gMaterialOf) {
        m = int(rng() % 6);
        if (m > 3) m = -1;
    }
    for (MaterialCfg &m : gMaterialCfg) {
        m.skip = rng() % 6 == 0;
        for (uint32_t &n : m.counts) n = rng() % 3;
        m.extra = 4 * (rng() % 2);
    }
}

uint32_t TypeIndex(uint32_t type)
{
    for (uint32_t k = 0; k < 4; ++k)
        if (kParamTypes[k] == type) return k;
    return 4;
}
uint32_t ParamBase(const MaterialCfg &m, uint32_t k)
{
    uint32_t base = 0;
    for (uint32_t j = 0; j < k; ++j) base += m.counts[j] * 4 * (j + 1);
    return base;
}

// The shape's handlers (value[0]: its index).
// While a clone is made its new node is not registered yet, so a body naming it is normalised once the clone is
// (FlushDeferredBodies): the body's words are kept raw and their log entry rewritten.
struct DeferredBody {
    size_t at;
    std::vector<uint32_t> raw;
};
bool gDeferBodies = false;
std::vector<DeferredBody> gDeferredBodies;
uint32_t ShapeNotify(Fake &, const uint32_t *a)
{
    const void *body = reinterpret_cast<const void *>(uintptr_t(a[2]));
    sgfx::NoteBody("shape notify", body);
    uint32_t head[3] = {};
    uint32_t words[16] = {};
    unsigned count = 0;
    if (gDeferBodies && body && sgfx::detail::ReadBody(body, head, words, &count))
        gDeferredBodies.push_back({drawfx::gLog.size() - 1, std::vector<uint32_t>(words, words + count)});
    return 0;
}
void FlushDeferredBodies()
{
    for (const DeferredBody &d : gDeferredBodies)
        for (size_t i = 0; i < d.raw.size(); ++i) drawfx::gLog[d.at].words[4 + i] = drawfx::Norm(d.raw[i]);
    gDeferredBodies.clear();
    gDeferBodies = false;
}
uint32_t ShapeLod(Fake &self, const uint32_t *a)
{
    const ShapeCfg &c = gShapeCfg[self.value[0]];
    const uint32_t parts = gPartsOn[self.value[0]] ? P(gParts[self.value[0]]) : 0u;
    const uint32_t rest = gRestOn[self.value[0]] ? P(gRest[self.value[0]]) : 0u;
    const uint32_t info[8] = {c.key, c.noIndices ? 0u : P(c.indices), gMorphs[self.value[0]], c.transforms, c.word10, 0, rest, parts};
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), info, sizeof(info));
    return a[0];
}
// 60 (the box) / 59 (the box, the inverted transform, the block's transform count and transforms): the shape's box
// taken in, and under 59 the first block transform's position too.
void GrowBy(uint32_t at, const float *g)
{
    float *b = reinterpret_cast<float *>(uintptr_t(at));
    for (unsigned k = 0; k < 3; ++k) {
        b[k] = (std::min)(b[k], g[k]);
        b[k + 3] = (std::max)(b[k + 3], g[k + 3]);
    }
}
uint32_t ShapeGrow(Fake &self, const uint32_t *a)
{
    if (gGrow[self.value[0]].on) GrowBy(a[0], gGrow[self.value[0]].box);
    return 0;
}
uint32_t ShapeGrowPlaced(Fake &self, const uint32_t *a)
{
    ShapeGrow(self, a);
    if (gGrow[self.value[0]].on && a[2]) {
        const float *t = reinterpret_cast<const float *>(uintptr_t(a[3]));
        const float point[6] = {t[0], t[1], t[2], t[0], t[1], t[2]};
        GrowBy(a[0], point);
    }
    return 0;
}
uint32_t ShapeBindings(Fake &self, const uint32_t *a)
{
    const uint32_t i = self.value[0];
    Fake *const c = sw.cursors[i];
    c[0].value[1] = 0;
    uint32_t *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = P(&c[0]);
    out[1] = gShapeCfg[i].middle ? P(&c[1]) : 0;
    out[2] = P(&c[2]);
    return a[0];
}
uint32_t CursorAtEnd(Fake &self, const uint32_t *) { return self.value[1] >= gShapeCfg[self.value[0]].bindingCount; }
uint32_t CursorGet(Fake &self, const uint32_t *a)
{
    uint32_t *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (unsigned k = 0; k < 4; ++k) out[k] = 0x1100 + k;
    out[1] = gShapeCfg[self.value[0]].bindings[self.value[1]];
    return a[0];
}
uint32_t CursorNext(Fake &self, const uint32_t *)
{
    ++self.value[1];
    return 0;
}
uint32_t ManagerLookup(Fake &, const uint32_t *a)
{
    const int m = a[1] < 6 ? gMaterialOf[a[1]] : -1;
    const uint32_t info[5] = {a[1], 0x2200, m < 0 ? 0u : P(&sw.materials[m]), 0x2203, 0x2204};
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), info, sizeof(info));
    return a[0];
}
constexpr const char *kMaterialNames[4] = {"skin", "cloth", "hair", "glass"};
uint32_t MaterialName(Fake &self, const uint32_t *) { return P(kMaterialNames[self.value[0]]); }
uint32_t MaterialSkipped(Fake &self, const uint32_t *) { return gMaterialCfg[self.value[0]].skip; }
uint32_t MaterialCount(Fake &self, const uint32_t *a)
{
    const uint32_t k = TypeIndex(a[0]);
    return k < 4 ? gMaterialCfg[self.value[0]].counts[k] : 0;
}
uint32_t MaterialSize(Fake &self, const uint32_t *)
{
    const MaterialCfg &m = gMaterialCfg[self.value[0]];
    return ParamBase(m, 4) + m.extra;
}
uint32_t MaterialInfo(Fake &self, const uint32_t *a)
{
    const MaterialCfg &m = gMaterialCfg[self.value[0]];
    const uint32_t k = TypeIndex(a[2]);
    const uint32_t info[4] = {a[1], k, 0x3300, k < 4 ? ParamBase(m, k) + a[1] * 4 * (k + 1) : 0};
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), info, sizeof(info));
    return a[0];
}
uint32_t MaterialParam(Fake &self, const uint32_t *a)
{
    uint32_t *event = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (uint32_t k = 0; k < 4; ++k) {
        if (event[0] != P(GameAddress<const void>(kParamEvents[k]))) continue;
        float *data = reinterpret_cast<float *>(event + 1);
        // Some vector parameters left unset: the event keeps what it held (zero first, then the last value written).
        // The float event is always written (0xa589a0 leaves its first value as the stack held it).
        if (k && (self.value[0] + a[1] + k) % 3 == 0) return 0;
        for (uint32_t j = 0; j <= k; ++j) data[j] = float(self.value[0] * 100 + k * 10 + a[1]) + 0.25f * float(j + 1);
        return 0;
    }
    drawfx::Note("unknown event", event[0]);
    return 0;
}
uint32_t DetailRegister(Fake &, const uint32_t *) { return 0x6000 + ++gDetailHandles; }

// The world for the shape tests: Build's, plus the shapes' answers, the cursors, the manager and the materials, the
// detail object's registrations, the listener, the original's static vectors on test storage (their guards marked
// done) and the transform family's event manager.
void BuildShapes(uint32_t seed, drawfx::Cells &cells)
{
    Build(seed, cells);
    std::mt19937 rng(seed ^ 0x5eedu);
    for (unsigned i = 0; i < 3; ++i) {
        Fake &s = w.shapes[i];
        s.value[0] = i;
        s.On(5, &ShapeNotify);
        s.On(52, &ShapeLod);
        s.On(54, &ShapeBindings);
        s.On(59, &ShapeGrowPlaced);
        s.On(60, &ShapeGrow);
        for (unsigned k = 0; k < 3; ++k) {
            Fake &c = sw.cursors[i][k];
            c.Reset<kCursor>(720 + 3 * i + k);
            c.value[0] = i;
            c.On(4, &CursorAtEnd);
            c.On(5, &CursorGet);
            c.On(6, &CursorNext);
        }
    }
    sw.manager.Reset<kManager>(770);
    sw.manager.On(33, &ManagerLookup);
    for (unsigned m = 0; m < 4; ++m) {
        Fake &f = sw.materials[m];
        f.Reset<kMaterial>(780 + m);
        f.value[0] = m;
        f.On(5, &MaterialName);
        f.On(6, &MaterialSkipped);
        f.On(9, &MaterialCount);
        f.On(11, &MaterialSize);
        f.On(13, &MaterialInfo);
        f.On(14, &MaterialParam);
    }
    Fake &detail = sgrx::Registry::Get().detail;
    detail.Settle<19, 3>("uuu");
    detail.On(19, &DetailRegister);
    detail.Settle<20, 2>("uu");
    gDetailHandles = 0;
    sgfx::MakeListener(sw.listener, 500);
    cells.Set(kMaterialCell, &sw.manager);
    for (unsigned v = 0; v < 3; ++v) {
        cells.Set(kStaticVectors[v], gStaticVectors[v]);
        cells.Set(kStaticVectors[v] + 4, gStaticVectors[v]);
        cells.Set(kStaticVectors[v] + 8, gStaticVectors[v] + 64);
        cells.Set(kStaticVectors[v] + 0xc, uint32_t(INT_MIN));
    }
    cells.Set(cTransformNodeBehaviorTraits::kEventManagerCell, w.manager.manager);
    for (unsigned i = 0; i < 3; ++i) RandomShape(i, rng);
    RandomMaterials(rng);
    sw.xfCount = 0;
    for (unsigned i = 0; i < 3; ++i) gMorphs[i] = gMorphShift[i] = gPartsOn[i] = gRestOn[i] = 0;
    std::memset(gGrow, 0, sizeof(gGrow));
}

// Our side's three function-local vectors grown once, past what any seed needs, so that neither side's vectors move
// while a seed runs (a move frees a block, and the release's frees are compared): the original's sit on test storage.
void WarmOurStatics()
{
    drawfx::Cells cells;
    drawfx::ResetAll();
    BuildShapes(0, cells);
    for (unsigned m = 0; m < 4; ++m) gMaterialCfg[m] = {0, {1, 1, 1, 1}, 0};
    for (int i = 0; i < 6; ++i) gMaterialOf[i] = i % 4;
    gShapeCfg[0] = {4, {0, 1, 2, 3}, 0, 0, 0, {0, 1, 2, 3}, 4, 0};
    cICompositionTreeNode *n = MakeOurs();
    n->AddRef();
    for (int k = 0; k < 9; ++k) As(n)->AddShape(reinterpret_cast<cIShape *>(&w.shapes[0]));
    n->Release();
    w.manager.Drop();
    drawfx::ResetAll();
}

cICompositionTreeNode *__cdecl MakeOurTransform() { return cTransformNode::Create(); }
cITransformNode *Xf(cICompositionTreeNode *n) { return static_cast<cITransformNode *>(n); }

void RandomPlace(cITransformNode *x, std::mt19937 &rng)
{
    const float t[3] = {float(int(rng() % 9) - 4), float(rng() % 5), 0.5f * float(rng() % 3)};
    x->SetTranslation(t);
    if (rng() % 2) {
        const float q[4] = {0.0f, 0.6f, 0.0f, 0.8f};
        x->SetRotation(q);
    }
}

// Sometimes a transform root (limit 0) over top-level nodes, and under it sometimes a transform with a limit in the
// block's range: the block's transforms are then the root's world transform and the limited node's (0xae7aa0,
// 0xae7910), else the identity.
void MakeTransforms(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    if (rng() % 3 == 0) return;
    const sgfx::Factory make = original ? ctx.Original<sgfx::Factory>(0xa3a6c0) : &MakeOurTransform;
    const unsigned count = 1 + rng() % 2;
    for (unsigned i = 0; i < count; ++i) {
        cICompositionTreeNode *x = make();
        x->AddRef();
        drawfx::AddRange(x, cTransformNode::kSize, kTransformTag | (i << 8));
        sw.xf[sw.xfCount++] = x;
        Xf(x)->SetLimit(i ? 1 + rng() % 3 : 0);
        RandomPlace(Xf(x), rng);
        if (i) sw.xf[0]->AddChild(x);
    }
    for (unsigned i = 0; i < w.nodes.count; ++i)
        if (!w.nodes[i]->GetParent() && rng() % 4) sw.xf[rng() % sw.xfCount]->AddChild(w.nodes[i]);
}

// A node's block: its words, the pointers in it (the header's and the material table's) as offsets into the block.
void NoteBlock(const char *label, unsigned i)
{
    const uint32_t *b = reinterpret_cast<const uint32_t *>(uintptr_t(Words(w.nodes[i])[50]));
    drawfx::Call c;
    c.note = label;
    c.words.push_back(i);
    c.words.push_back(b != nullptr);
    if (b) {
        const uint32_t base = P(b), n = b[0] / 4;
        uint64_t pointers[4] = {};
        auto mark = [&](uint32_t k) {
            if (k < 256) pointers[k / 64] |= 1ull << (k % 64);
        };
        for (uint32_t k : {1u, 3u, 4u, 5u, 6u, 7u, 8u}) mark(k);
        if (b[4]) {
            const uint32_t t = (b[4] - base) / 4;
            for (uint32_t j = 0; j < b[t] && t + 1 + j < n; ++j) mark(t + 1 + j);
        }
        for (uint32_t k = 0; k < n && k < 256; ++k) {
            const bool pointer = (pointers[k / 64] >> (k % 64)) & 1;
            c.words.push_back(pointer && b[k] ? 0x40000000u | (b[k] - base) : b[k]);
        }
    }
    drawfx::gLog.push_back(std::move(c));
}

void NoteShapeNodes(const char *label)
{
    w.nodes.NoteShape(label);
    w.nodes.NoteWords(label, kSkipWords | 1ull << 50);
    NoteVectors(label);
    for (unsigned i = 0; i < w.nodes.count; ++i) NoteBlock(label, i);
}

void OperateShapes(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    const unsigned which = rng() % ns.count;
    cICompositionTreeNode *n = ns[which];
    cIShapeRefNode *s = As(n);
    const unsigned op = rng() % 15;
    drawfx::Note("op", {op, which});
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0:
    case 1:
    case 2: // a shape added (94)
        if (s->ShapeCount() < 6) s->AddShape(reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]));
        break;
    case 3:
    case 4: { // a listed shape removed (95)
        const cShapeRange r = s->GetShapes();
        if (r.mEnd != r.mFirst) s->RemoveShape(r.mFirst[rng() % (r.mEnd - r.mFirst)]);
        break;
    }
    case 5: // a shape's settings changed (seen at the next refresh)
        RandomShape(rng() % 3, rng);
        break;
    case 6: // the materials changed
        if (rng() % 2) RandomMaterials(rng);
        else gMaterialCfg[rng() % 4].skip ^= 1;
        break;
    case 7:
    case 8: { // events (3); half the time the tree refreshed first (the stale flags cleared, as the world-transform
              // refresh leaves them), so a transform event's propagation shows in the flags
        if (rng() % 2) {
            for (unsigned i = 0; i < ns.count; ++i) ns[i]->SetFlag(kStaleBit, false);
            for (unsigned i = 0; i < sw.xfCount; ++i) sw.xf[i]->SetFlag(kStaleBit, false);
        }
        const uint32_t types[] = {cShapeRefNodeImpl::kDetailChangedEvent, cShapeRefNodeImpl::kMaterialsChangedEvent,
                                  cShapeRefNodeImpl::kSubordinateEvent, cShapeRefNodeImpl::kTransformEvent, 0x12345678};
        const uint32_t type = types[rng() % 5];
        cSubordinateListChanged body(rng() % 4, rng() % 3 ? cIShape::kIid : 0x4512dd13u);
        drawfx::Note("event", {type, body.mKind, body.mIid});
        n->OnEvent(type, rng() % 2, &body, 0);
        for (unsigned i = 0; i < ns.count; ++i) drawfx::Note("flags", ns[i]->GetFlags());
        break;
    }
    case 9: { // queries (4)
        cSubordinateListChanged body(0, 0);
        // The refresh (the Impl's), the listened query (the behaviour's: its listener answers), an unknown one.
        const uint32_t types[] = {cShapeRefNodeImpl::kRefreshQuery, cShapeRefNodeImpl::kRefreshQuery, kListenedQuery,
                                  0x12345678u};
        const uint32_t type = types[rng() % 4];
        drawfx::Note("query", {type, uint8_t(n->OnQuery(type, rng() % 2, &body))});
        break;
    }
    case 10: // 97
        drawfx::Note("slot 97", uint8_t(s->AnyShapeSlot52()));
        break;
    case 11: { // a node moved: under another node, under a transform, or to the top
        cICompositionTreeNode *c = ns[rng() % ns.count];
        if (cICompositionTreeNode *p = c->GetParent()) p->RemoveChild(c);
        const unsigned to = rng() % (ns.count + sw.xfCount + 1);
        if (to < ns.count) {
            bool inside = false;
            for (cICompositionTreeNode *a = ns[to]; a; a = a->GetParent()) inside |= a == c;
            if (!inside) ns[to]->AddChild(c);
        } else if (to < ns.count + sw.xfCount) {
            sw.xf[to - ns.count]->AddChild(c);
        }
        break;
    }
    case 12: // a transform moved
        if (sw.xfCount) RandomPlace(Xf(sw.xf[rng() % sw.xfCount]), rng);
        break;
    case 13: // the node's block and lists
        NoteBlock("block", which);
        NoteVectors("vectors");
        break;
    case 14: { // the list and the search (92, 98, 99)
        const cShapeRange r = s->GetShapesConst(); // heap: the list's own words noted, and its place in the node's vector
        drawfx::Note("shapes", {uint32_t(r.mFirst == reinterpret_cast<cIShape **>(uintptr_t(Words(n)[31]))),
                                uint32_t(r.mCurrent - r.mFirst), uint32_t(r.mEnd - r.mFirst)});
        for (cIShape **p = r.mFirst; p != r.mEnd; ++p) drawfx::Note("listed", drawfx::Norm(P(*p)));
        drawfx::Note("count", s->ShapeCount());
        for (Fake &f : w.shapes) drawfx::Note("has", {f.id, uint8_t(s->HasShape(reinterpret_cast<cIShape *>(&f)))});
        break;
    }
    }
}

// The test's references let go with the shapes still held (each node's destructor removes them front first, 0xa543d0),
// the nodes before the transforms; the blocks freed, then every fake's count.
void ReleaseShapes()
{
    difftest::FreeRecorder freed;
    w.nodes.Release();
    for (unsigned i = sw.xfCount; i-- > 0;) sw.xf[i]->Release();
    sw.xfCount = 0;
    freed.Stop();
    size_t sizes[96];
    const size_t n = freed.SortedSizes(sizes, 96);
    drawfx::Call c;
    c.note = "freed";
    c.words.push_back(uint8_t(freed.Overflowed()));
    for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
    drawfx::gLog.push_back(std::move(c));
    for (const Fake &s : w.shapes) drawfx::Note("shape refs", {s.id, uint32_t(s.refs)});
    for (const auto &cs : sw.cursors)
        for (const Fake &f : cs) drawfx::Note("cursor refs", {f.id, uint32_t(f.refs)});
    drawfx::Note("registrations", gDetailHandles);
}

// ---- persistence, the morph names and clones (SG_ShapeRefNode_Persist, lane TB) ----
// Each shape has some morphs (its level info's word +8, gMorphs) and names each through slot 58 (its name, a morph name
// from kMorphNames past its shift): shapes 0 and 2 share a name, so their morphs' names collide. A changed shift leaves
// a node's old names unmatched (0xa580f0 keeps them after the matched ones).
constexpr const char *kShapeNames[3] = {"body", "head", "body"};
constexpr const char *kMorphNames[5] = {"smile", "frown", "blink", "wink", "pout"};
uint32_t ShapeMorphName(Fake &self, const uint32_t *a)
{
    const uint32_t i = self.value[0];
    const uint32_t parts[2] = {P(kShapeNames[i]), P(kMorphNames[(a[1] + gMorphShift[i]) % 5])};
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), parts, sizeof(parts));
    return a[0];
}
// The reader's GetVoid (15: data, size) fills the bytes with a pattern; GetFloat32Array (20: data, count) takes the word
// queue's floats; slot 23 (a skip: bytes) is only logged.
uint8_t gFill;
uint32_t ReaderVoid(Fake &, const uint32_t *a)
{
    uint8_t *data = reinterpret_cast<uint8_t *>(uintptr_t(a[0]));
    for (uint32_t k = 0; k < a[1]; ++k) data[k] = uint8_t(gFill + 7 * k);
    return 1;
}
uint32_t ReaderFloats(Fake &self, const uint32_t *a)
{
    float *out = reinterpret_cast<float *>(uintptr_t(a[0]));
    for (uint32_t k = 0; k < a[1]; ++k) out[k] = std::bit_cast<float>(sgfx::detail::ReaderUint32(self, a));
    return 1;
}
// The writer's SetVoid (15) and SetFloat32Array (20): their contents noted.
uint32_t WriterVoid(Fake &, const uint32_t *a)
{
    drawfx::Call c;
    c.note = "written bytes";
    const uint8_t *data = reinterpret_cast<const uint8_t *>(uintptr_t(a[0]));
    for (uint32_t k = 0; k < a[1]; ++k) c.words.push_back(data[k]);
    drawfx::gLog.push_back(std::move(c));
    return 1;
}
uint32_t WriterFloats(Fake &, const uint32_t *a)
{
    sgtx::NoteFloats("written floats", reinterpret_cast<const float *>(uintptr_t(a[0])), a[1]);
    return 1;
}

void BuildPersist(uint32_t seed, drawfx::Cells &cells)
{
    BuildShapes(seed, cells);
    std::mt19937 rng(seed ^ 0x9e75u);
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    Fake &r = sgfx::gReader.fake;
    r.Settle<15, 2>("au");
    r.On(15, &ReaderVoid);
    r.Settle<20, 2>("au");
    r.On(20, &ReaderFloats);
    r.Settle<23, 1>("u");
    Fake &wr = sgfx::gWriter.fake;
    wr.Settle<15, 2>("au");
    wr.On(15, &WriterVoid);
    wr.Settle<20, 2>("au");
    wr.On(20, &WriterFloats);
    for (unsigned i = 0; i < 3; ++i) {
        w.shapes[i].On(58, &ShapeMorphName);
        gMorphs[i] = rng() % 4;
        gMorphShift[i] = rng() % 5;
    }
    gFill = uint8_t(rng());
}

// A node's morphs: the count byte (+0xaf), the names (their pool texts and counts) and the weights (bits), the flags.
void NoteMorphs(const char *label, unsigned i)
{
    const uint32_t *v = Words(w.nodes[i]);
    drawfx::Call c;
    c.note = label;
    c.words = {i, reinterpret_cast<const uint8_t *>(v)[0xaf], As(w.nodes[i])->GetShapeFlags()};
    const uint32_t *names = reinterpret_cast<const uint32_t *>(uintptr_t(v[44]));
    const uint32_t nameCount = (v[45] - v[44]) / 4;
    c.words.push_back(nameCount);
    for (uint32_t k = 0; k < nameCount; ++k) {
        const int at = sgfx::gPool.Index(names[k]);
        c.words.push_back(at < 0 ? names[k] : uint32_t(sgfx::gPool.counts[at]));
        if (at >= 0) c.text += sgfx::gPool.texts[at] + ";";
    }
    const float *weights = reinterpret_cast<const float *>(uintptr_t(v[47]));
    const uint32_t weightCount = (v[48] - v[47]) / 4;
    c.words.push_back(weightCount);
    for (uint32_t k = 0; k < weightCount; ++k) c.words.push_back(std::bit_cast<uint32_t>(weights[k]));
    drawfx::gLog.push_back(std::move(c));
}
void NoteAllMorphs(const char *label)
{
    for (unsigned i = 0; i < w.nodes.count; ++i) NoteMorphs(label, i);
}

// The morphs the node will have once `extra` shapes are added: its shapes' counts and theirs.
uint32_t MorphsWith(cIShapeRefNode *s, const unsigned *extra, unsigned extraCount)
{
    uint32_t total = 0;
    const cShapeRange r = s->GetShapes();
    for (cIShape **p = r.mFirst; p != r.mEnd; ++p)
        for (unsigned i = 0; i < 3; ++i)
            if (*p == reinterpret_cast<cIShape *>(&w.shapes[i])) total += gMorphs[i];
    for (unsigned k = 0; k < extraCount; ++k) total += gMorphs[extra[k]];
    return total;
}

// A read into node `n`: its records (the shape-reference record's version 0x13-0x16), the renderable levels' words, then
// the shape-reference part: shapes (some refs null), flags, the weights (their count the node's morph count after the
// read, or not: then skipped), the names (version 0x15 on), the block's data (its size the block's now, or not: then
// skipped) and the colour (version 0x14 on).
void QueueRead(cICompositionTreeNode *n, std::mt19937 &rng)
{
    sgfx::Reader &r = sgfx::gReader;
    r.versionCount = r.versionAt = r.wordCount = r.wordAt = r.refCount = r.refAt = r.textCount = r.textAt = 0;
    const uint32_t version = 0x13 + rng() % 4;
    r.Version(version);
    for (int i = 0; i < 4; ++i) r.Version(rng() % 16);
    r.Version(4 + rng() % 2);
    r.Word(0);
    r.Text(rng() % 2 ? "alpha" : "beta");
    r.Word(0);
    for (int i = 0; i < 7; ++i) r.Float(sgtx::RandomFloat(rng));
    r.Word(rng() % 2 ? 0x7fffffffu : uint32_t(rng()));
    r.Word(rng() % 2);
    r.Word(rng() % 3 == 0);
    const unsigned groups = rng() % 2;
    r.Word(groups);
    for (unsigned i = 0; i < groups; ++i) r.Text(rng() % 2 ? "gamma" : "delta");
    r.Word(uint32_t(rng()));
    r.Word(rng() % 2);
    // The shape-reference part.
    unsigned added[3];
    const unsigned shapes = rng() % 3;
    unsigned addedCount = 0;
    r.Word(shapes);
    for (unsigned k = 0; k < shapes; ++k) {
        if (rng() % 5 == 0) {
            r.Ref(nullptr);
        } else {
            added[addedCount] = rng() % 3;
            r.Ref(&w.shapes[added[addedCount++]]);
        }
    }
    r.Word(uint32_t(rng()) & 0xffff);
    const uint32_t morphs = MorphsWith(As(n), added, addedCount);
    uint32_t weights = rng() % 4 ? morphs : rng() % 4;
    if (weights > 8) weights = morphs + 1; // past what the word queue holds: skipped
    r.Word(weights);
    if (weights && weights == morphs)
        for (uint32_t k = 0; k < weights; ++k) r.Float(rng() % 3 ? 0.0005f * float(rng() % 4) : sgtx::RandomFloat(rng));
    if (version >= 0x15)
        for (uint32_t k = 0; k < weights && r.textCount < 8; ++k) r.Text(kMorphNames[rng() % 5]);
    const uint32_t *block = reinterpret_cast<const uint32_t *>(uintptr_t(Words(n)[50]));
    r.Word(rng() % 3 == 0 ? 0u : block && rng() % 4 ? block[2] : 4 * (rng() % 6));
    r.Word(uint32_t(rng()));
    drawfx::Note("queued", {version, shapes, addedCount, morphs, weights, r.wordCount});
}

void OperatePersist(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    const unsigned which = rng() % ns.count;
    cICompositionTreeNode *n = ns[which];
    cIShapeRefNode *s = As(n);
    const unsigned op = rng() % 9;
    drawfx::Note("op", {op, which});
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: // a shape added
        if (s->ShapeCount() < 5) s->AddShape(reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]));
        break;
    case 1: { // a listed shape removed
        const cShapeRange r = s->GetShapes();
        if (r.mEnd != r.mFirst) s->RemoveShape(r.mFirst[rng() % (r.mEnd - r.mFirst)]);
        break;
    }
    case 2: // a shape's morphs changed (seen at the next rebuild)
        if (rng() % 2)
            gMorphs[rng() % 3] = rng() % 4;
        else
            gMorphShift[rng() % 3] = rng() % 5;
        break;
    case 3: { // written
        difftest::FreeRecorder freed;
        n->GetReferent()->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        freed.Stop();
        drawfx::Note("write frees", uint32_t(freed.Count()));
        break;
    }
    case 4:
    case 5: { // read
        QueueRead(n, rng);
        n->GetReferent()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&sgfx::gReader.fake));
        drawfx::Note("read", {sgfx::gReader.wordAt, sgfx::gReader.refAt, sgfx::gReader.textAt});
        NoteMorphs("read", which);
        NoteBlock("read block", which);
        break;
    }
    case 6: { // the type
        cPersistTypeInfo info = n->GetReferent()->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 7: { // cloned (15, the copying 0xa57d00), the clone kept
        if (ns.count >= sgfx::Nodes::kMax) break;
        void *out = nullptr;
        gDeferBodies = true;
        n->Clone(rng() % 3, sgfx::kIidTree, &out, nullptr);
        const bool isNew = out && drawfx::Norm(P(out)) == P(out);
        drawfx::Note("clone", {out != nullptr, uint8_t(isNew)});
        const unsigned at = ns.count;
        if (isNew) {
            drawfx::AddRange(out, kSize, sgfx::kNodeTag | (ns.count << 8));
            ns.at[ns.count++] = static_cast<cICompositionTreeNode *>(out);
            const_cast<uint32_t *>(Words(static_cast<cICompositionTreeNode *>(out)))[29] = 0x1234;
        }
        if (out) HoldSubtree(static_cast<cICompositionTreeNode *>(out)); // a deep clone's new children
        FlushDeferredBodies();
        if (isNew) {
            NoteMorphs("clone", at);
            NoteBlock("clone block", at);
        } else if (out) {
            static_cast<cICompositionTreeNode *>(out)->Release();
        }
        break;
    }
    case 8:
        NoteMorphs("morphs", which);
        break;
    }
}

// ---- bounds and hits (SG_ShapeRefNode_Bounds, lane TB) ----
// The shapes grow the node's box (slot 60, or with block transforms slot 59) by their gGrow box (none, or one that may
// be inside out); their level infos list parts (gParts, fake bounds) the hit tests visit. The fakes answer from a bit
// pattern (value[1]) by their call count (value[2]); the first-hit answers a distance from the count too.
constexpr std::array<Slot, 21> MakeBoundSlots()
{
    std::array<Slot, 21> a = BaseSlots<21>();
    a[19] = {3, Ret::Int, true, "IntersectsCuboidInAt", "F13F3F13"};
    a[20] = {3, Ret::Int, true, "IntersectsBoundInAt", "F13iF13"};
    return a;
}
constexpr auto kBoundSlots = MakeBoundSlots();
constexpr drawfx::Iface kBound{"Bound", 21, kBoundSlots.data()};
constexpr std::array<Slot, 30> MakeLinearSlots()
{
    std::array<Slot, 30> a = BaseSlots<30>();
    a[17] = {2, Ret::Int, true, "IntersectsCuboidIn", "F3F13"};
    a[18] = {2, Ret::Int, true, "IntersectsBoundIn", "iF13"};
    a[29] = {3, Ret::Int, true, "FirstHitBoundIn", "aiF13"};
    return a;
}
constexpr auto kLinearSlots = MakeLinearSlots();
constexpr drawfx::Iface kLinear{"Linear", 30, kLinearSlots.data()};

struct BoundWorld {
    Fake parts[4];
    Fake bound;
    Fake linear;
};
BoundWorld bw;

uint32_t Answer(Fake &self, const uint32_t *)
{
    const uint32_t k = self.value[2]++;
    return self.value[1] >> (k % 32) & 1;
}
uint32_t AnswerHit(Fake &self, const uint32_t *a)
{
    const uint32_t k = self.value[2]++;
    *reinterpret_cast<float *>(uintptr_t(a[0])) = 0.5f * float((k * 7 + self.value[3]) % 13);
    return self.value[1] >> (k % 32) & 1;
}
void RandomAnswers(Fake &f, std::mt19937 &rng)
{
    f.value[1] = rng() % 4 ? rng() : rng() % 2 ? 0u : ~0u;
    f.value[3] = rng() % 13;
}

void RandomGrow(unsigned i, std::mt19937 &rng)
{
    GrowCfg &g = gGrow[i];
    g.on = rng() % 5 != 0;
    for (unsigned k = 0; k < 3; ++k) {
        g.box[k] = float(int(rng() % 7) - 3) * 0.75f;
        g.box[k + 3] = g.box[k] + float(int(rng() % 5) - (rng() % 6 == 0 ? 3 : 0)) * 0.5f;
    }
    gPartsOn[i] = rng() % 4 != 0;
    for (uint32_t &p : gParts[i]) p = rng() % 4 ? P(&bw.parts[rng() % 4]) : 0;
    drawfx::Note("grow", {i, g.on, gPartsOn[i]});
}

void BuildBounds(uint32_t seed, drawfx::Cells &cells)
{
    BuildShapes(seed, cells);
    std::mt19937 rng(seed ^ 0xb0b0u);
    for (unsigned k = 0; k < 4; ++k) {
        bw.parts[k].Reset<kBound>(800 + k);
        bw.parts[k].On(20, &Answer);
        RandomAnswers(bw.parts[k], rng);
    }
    bw.bound.Reset<kBound>(810);
    bw.bound.On(19, &Answer);
    RandomAnswers(bw.bound, rng);
    bw.linear.Reset<kLinear>(820);
    bw.linear.On(17, &Answer);
    bw.linear.On(18, &Answer);
    bw.linear.On(29, &AnswerHit);
    RandomAnswers(bw.linear, rng);
    drawfx::AddRange(gParts, sizeof(gParts), 0x7e000000);
    for (unsigned i = 0; i < 3; ++i) RandomGrow(i, rng);
}

cIBoundedNode *Bounded(cICompositionTreeNode *n) { return As(n); }

// A frame from a quaternion (some turned about y), flags as the frame code leaves them, an offset.
AffineTransform3 RandomFrame(std::mt19937 &rng)
{
    AffineTransform3 f{};
    const float turned[4] = {0.0f, 0.6f, 0.0f, 0.8f}, level[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    game_code::SetFrameRotation(&f, rng() % 2 ? turned : level);
    if (rng() % 3) f.mUnknown0 |= 1;
    for (float &o : f.mOffset) o = float(int(rng() % 9) - 4) * 0.5f;
    return f;
}
void NoteBox(const char *label, const cBoundBox &b)
{
    drawfx::Note(label, {std::bit_cast<uint32_t>(b.mMin[0]), std::bit_cast<uint32_t>(b.mMin[1]),
                         std::bit_cast<uint32_t>(b.mMin[2]), std::bit_cast<uint32_t>(b.mMax[0]),
                         std::bit_cast<uint32_t>(b.mMax[1]), std::bit_cast<uint32_t>(b.mMax[2])});
}

void OperateBounds(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    const unsigned which = rng() % ns.count;
    cICompositionTreeNode *n = ns[which];
    cIShapeRefNode *s = As(n);
    const unsigned op = rng() % 14;
    drawfx::Note("op", {op, which});
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: // 72
        NoteBox("box", Bounded(n)->GetBoundBox());
        break;
    case 1: { // 71
        const AffineTransform3 f = RandomFrame(rng);
        NoteBox("box in", Bounded(n)->GetBoundBoxIn(reinterpret_cast<const cTransform34 *>(&f)));
        break;
    }
    case 2: { // 73
        const AffineTransform3 f = RandomFrame(rng);
        drawfx::Note("73", uint8_t(Bounded(n)->IntersectsBound(P(&bw.bound), P(&f))));
        break;
    }
    case 3: // 74
        drawfx::Note("74", uint8_t(Bounded(n)->IntersectsLinear(reinterpret_cast<nGZCompGeom::cILinear3 *>(&bw.linear))));
        break;
    case 4: { // 75
        float t = rng() % 4 ? 0.5f * float(rng() % 13) : FLT_MAX;
        const bool hit = Bounded(n)->FirstHitLinear(&t, reinterpret_cast<nGZCompGeom::cILinear3 *>(&bw.linear));
        drawfx::Note("75", {uint8_t(hit), std::bit_cast<uint32_t>(t)});
        break;
    }
    case 5: // a shape added (94)
        if (s->ShapeCount() < 6) s->AddShape(reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]));
        break;
    case 6: { // a listed shape removed (95)
        const cShapeRange r = s->GetShapes();
        if (r.mEnd != r.mFirst) s->RemoveShape(r.mFirst[rng() % (r.mEnd - r.mFirst)]);
        break;
    }
    case 7: // a transform moved (the nodes under it stale)
        if (sw.xfCount) RandomPlace(Xf(sw.xf[rng() % sw.xfCount]), rng);
        break;
    case 8: // a shape's box and parts, or its settings (the transform slots seen at the next block rebuild)
        if (rng() % 3) RandomGrow(rng() % 3, rng);
        else RandomShape(rng() % 3, rng);
        break;
    case 9: { // 60 called directly: a transform and records (some indices past the block's transforms)
        uint32_t records[4][8];
        const float q[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.6f, 0.0f, 0.8f}};
        const uint32_t count = rng() % 5;
        for (uint32_t r = 0; r < 4; ++r) {
            records[r][0] = rng() % 5;
            for (unsigned c = 0; c < 3; ++c) records[r][1 + c] = std::bit_cast<uint32_t>(float(int(rng() % 7) - 3));
            const float *rot = q[rng() % 2];
            for (unsigned c = 0; c < 4; ++c) records[r][4 + c] = std::bit_cast<uint32_t>(rot[c]);
        }
        cRigidTransform t{};
        for (unsigned c = 0; c < 3; ++c) t.mValues[c] = float(int(rng() % 9) - 4);
        std::memcpy(t.Rotation(), q[rng() % 2], 16);
        drawfx::Note("60", {count, records[0][0], records[1][0], records[2][0], records[3][0]});
        n->OnContentChanged(P(&t), count ? P(records) : 0, count);
        break;
    }
    case 10: // the words (the box at 37-42, the flags at 43) and the block
        w.nodes.NoteWords("words", kSkipWords | 1ull << 50);
        NoteBlock("block", which);
        break;
    case 11: { // a node moved under a transform or to the top
        cICompositionTreeNode *c = ns[rng() % ns.count];
        if (cICompositionTreeNode *p = c->GetParent()) p->RemoveChild(c);
        if (sw.xfCount && rng() % 2) sw.xf[rng() % sw.xfCount]->AddChild(c);
        break;
    }
    case 12: { // the materials changed (the block rebuilt by the event)
        cSubordinateListChanged body(0, 0);
        RandomMaterials(rng);
        n->OnEvent(cShapeRefNodeImpl::kMaterialsChangedEvent, 0, &body, 0);
        break;
    }
    case 13: // the answers changed
        RandomAnswers(rng() % 2 ? bw.linear : rng() % 2 ? bw.bound : bw.parts[rng() % 4], rng);
        break;
    }
}

// ---- attributes and attribute names (SG_ShapeRefNode_Attributes, lane TD) ----
// Slots 7 / 10 (0xa593a0 / 0xa580a0, the Impl's SetAttribute 0xa575a0 / GetAttribute 0xa56b00) on the block's parameter
// lists (float, float2-4: valid indices only, the original does not check them), the morph weights (an index under the
// weight count), the colour (Set's extra 0, or 1: ignored) and the transform family's ids; slot 51 (0xa54970, the Impl's
// 0xa55830) builds the name list (the materials' names and parameter words, the morph names, the colour's two names, or
// the empty range 0xa37d80), whose iterators (0xa541a0, 0xa54160, 0xa54480, 0xa54840, Clone 0xa54fb0, Get 0xa550b0, Next
// 0xa57f50, Previous 0xa58010, Advance 0xa54920, At 0xa550f0, from the pool 0x10f625c: 0xa545e0 / 0xa54610) and list
// (0xa54740) are walked and released. The original's seen-materials vector (0x10f6290, a function-local static) sits on
// test storage; ours is grown once before the seeds (WarmNameStatics). The detail limit's reads (the detail object's slot
// 43) show in the log.
constexpr uint32_t kNamePool = 0x10f6260; // the pool 0x10f625c's current-page word
constexpr uint32_t kSeenVector = 0x10f6290; // {first, last, end}, its guard at +0xc
constexpr uint32_t kFloatIds[4] = {0x09f5fdb0, 0x09f5fdb6, 0x09f5fdbc, 0x09f5fdc0};
constexpr uint32_t kMorphWeightId = 0x69c7eb40, kColourId = 0x8c798726;
uint32_t gSeen[64];

// The name iterators' pool (a cRZFastFixedAllocator2<0x10>: a pointer to its current page) on a test page, as
// tests/sg_geometry_node.cpp's IteratorPools: 0x800-aligned, the free list and a used count far from empty and full.
struct NamePool {
    static constexpr uint32_t kPage = 0x800, kBlock = 0x10, kTag = 0x7b200000;
    uint8_t *page;
    uint32_t saved;
    NamePool()
    {
        page = static_cast<uint8_t *>(_aligned_malloc(kPage, kPage));
        saved = *GameAddress<uint32_t>(kNamePool);
        *GameAddress<uint32_t>(kNamePool) = P(page);
        Reset(false);
    }
    ~NamePool()
    {
        *GameAddress<uint32_t>(kNamePool) = saved;
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
NamePool *gNamePool = nullptr;

void BuildAttributes(uint32_t seed, drawfx::Cells &cells)
{
    BuildPersist(seed, cells);
    gNamePool->Reset();
    cells.Set(kSeenVector, P(gSeen));
    cells.Set(kSeenVector + 4, P(gSeen));
    cells.Set(kSeenVector + 8, P(gSeen + 64));
    cells.Set(kSeenVector + 0xc, uint32_t(INT_MIN));
}

// Our side's seen vector grown past the four materials once, so that it never reallocates while a seed runs.
void WarmNameStatics()
{
    drawfx::Cells cells;
    drawfx::ResetAll();
    BuildShapes(0, cells);
    gNamePool->Reset(false);
    for (unsigned m = 0; m < 4; ++m) gMaterialCfg[m] = {0, {1, 1, 1, 1}, 0};
    for (int i = 0; i < 6; ++i) gMaterialOf[i] = i % 4;
    gShapeCfg[0] = {4, {0, 1, 2, 3}, 0, 0, 0, {0, 1, 2, 3}, 4, 0};
    cICompositionTreeNode *n = MakeOurs();
    n->AddRef();
    As(n)->AddShape(reinterpret_cast<cIShape *>(&w.shapes[0]));
    cArrayAttributeEntryRange r{};
    n->MakeIterator51(&r, kFloatIds[0]);
    for (cIGZUnknown *it : {static_cast<cIGZUnknown *>(r.mCurrent), static_cast<cIGZUnknown *>(r.mBegin),
                            static_cast<cIGZUnknown *>(r.mEnd)})
        if (it) it->Release();
    n->Release();
    w.manager.Drop();
    drawfx::ResetAll();
}

// An attribute and an index the node can answer: a parameter list the block has (an index under its count), a weight
// under the weight count, the colour, a transform id or an unknown one.
bool PickAttribute(cICompositionTreeNode *n, std::mt19937 &rng, uint32_t *id, uint32_t *extra)
{
    const uint32_t *v = Words(n);
    switch (rng() % 5) {
    case 0:
    case 1: {
        const uint32_t *block = reinterpret_cast<const uint32_t *>(uintptr_t(v[50]));
        const uint32_t list = rng() % 4;
        if (!block || !block[5 + list]) return false;
        const uint32_t count = *reinterpret_cast<const uint32_t *>(uintptr_t(block[5 + list]));
        if (!count) return false;
        *id = kFloatIds[list];
        *extra = rng() % count;
        return true;
    }
    case 2: {
        const uint32_t weights = (v[48] - v[47]) / 4;
        if (!weights) return false;
        *id = kMorphWeightId;
        *extra = rng() % weights;
        return true;
    }
    case 3:
        *id = kColourId;
        *extra = rng() % 4 ? 0u : 1u;
        return true;
    default: {
        const uint32_t others[] = {cITransformNode::kLimitAttribute, cITransformNode::kTransformAttribute,
                                   cITransformNode::kRotationAttribute, cITransformNode::kTranslationAttribute,
                                   0x12345678u};
        *id = others[rng() % 5];
        *extra = rng() % 4;
        return true;
    }
    }
}

void NoteValue(const char *label, const cAttributeValue &value)
{
    drawfx::Call c;
    c.note = label;
    c.words.push_back(value.mType);
    for (float f : value.mValues) c.words.push_back(std::bit_cast<uint32_t>(f));
    drawfx::gLog.push_back(std::move(c));
}

// A name entry: its texts (a material's name and the parameter's word; a shape's name and a morph's; the colour's
// group and name).
void NoteName(const char *label, uint32_t id, const cArrayAttributeEntryName &e)
{
    drawfx::Call c;
    c.note = label;
    const bool params = id != kMorphWeightId && id != kColourId;
    c.text = e.mWord0 ? std::string(reinterpret_cast<const char *>(uintptr_t(e.mWord0))) : "(null)";
    if (params) c.words.push_back(e.mWord1);
    else c.text += std::string(";") + (e.mWord1 ? reinterpret_cast<const char *>(uintptr_t(e.mWord1)) : "(null)");
    drawfx::gLog.push_back(std::move(c));
}

// An iterator's words past the vtable: the count, the list, the element (a name iterator), or the count (a null one).
void NoteNameIterator(const char *label, const cIArrayAttributeEntryIterator *it, bool names)
{
    drawfx::Call c;
    c.note = label;
    if (it) drawfx::Words(c.words, it, names ? 4 : 2, names ? "-uaa" : "-u");
    else c.words.push_back(0);
    drawfx::gLog.push_back(std::move(c));
}

// Slot 51's range walked: the ends' words, a clone of begin to end (Clone, AddRef, IsEqual, Get, Next), back one
// (Previous), Advance back to begin and At each way, then every iterator released (the last release frees the list).
void WalkNames(cICompositionTreeNode *n, uint32_t id)
{
    cArrayAttributeEntryRange r{};
    void *const back = n->MakeIterator51(&r, id);
    const bool names = id == kMorphWeightId || id == kColourId || std::find(std::begin(kFloatIds), std::end(kFloatIds), id) != std::end(kFloatIds);
    drawfx::Note("51", {uint8_t(back == &r), uint8_t(r.mCurrent != nullptr), uint8_t(r.mBegin != nullptr),
                        uint8_t(r.mEnd != nullptr)});
    NoteNameIterator("cursor", r.mCurrent, names);
    NoteNameIterator("begin", r.mBegin, names);
    NoteNameIterator("end", r.mEnd, names);
    if (r.mBegin && r.mEnd) {
        cIArrayAttributeEntryIterator *it = r.mBegin->Clone();
        NoteNameIterator("clone", it, names);
        if (it) {
            drawfx::Note("clone addref", it->AddRef());
            drawfx::Note("begin is end", uint8_t(r.mBegin->IsEqual(r.mEnd)));
            int32_t count = 0;
            for (; count < 24 && !it->IsEqual(r.mEnd); ++count) {
                NoteName("name", id, it->Get());
                it->Next();
            }
            drawfx::Note("walked", {uint32_t(count), uint8_t(it->IsEqual(r.mEnd))});
            if (names && count) {
                it->Previous();
                NoteName("last", id, it->Get());
                NoteNameIterator("back one", it, names);
                it->Advance(1 - count);
                drawfx::Note("advanced to begin", uint8_t(it->IsEqual(r.mBegin)));
                NoteName("at end - 1", id, it->At(count - 1));
                it->Advance(count - 1);
                NoteName("at -(count - 1)", id, it->At(1 - count));
            }
            drawfx::Note("clone released", it->Release());
        }
    }
    for (cIGZUnknown *it : {static_cast<cIGZUnknown *>(r.mCurrent), static_cast<cIGZUnknown *>(r.mBegin),
                            static_cast<cIGZUnknown *>(r.mEnd)})
        if (it) drawfx::Note("released", it->Release());
}

float AttributeFloat(std::mt19937 &rng, float current)
{
    switch (rng() % 7) {
    case 0: return current;
    case 1: return current + 1e-11f;
    case 2: return std::numeric_limits<float>::quiet_NaN();
    case 3: return 0.0f;
    case 4: return 0.0005f * float(rng() % 4);
    case 5: return float(int(rng() % 300) - 30) / 255.0f;
    default: return sgtx::RandomFloat(rng);
    }
}

void OperateAttributes(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    const unsigned which = rng() % ns.count;
    cICompositionTreeNode *n = ns[which];
    cIShapeRefNode *s = As(n);
    const unsigned op = rng() % 10;
    drawfx::Note("op", {op, which});
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: // a shape added
        if (s->ShapeCount() < 5) s->AddShape(reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]));
        break;
    case 1: { // a listed shape removed
        const cShapeRange r = s->GetShapes();
        if (r.mEnd != r.mFirst) s->RemoveShape(r.mFirst[rng() % (r.mEnd - r.mFirst)]);
        break;
    }
    case 2: { // the materials or the morphs changed, the block rebuilt by the event
        cSubordinateListChanged body(0, 0);
        if (rng() % 2) RandomMaterials(rng);
        else gMorphs[rng() % 3] = rng() % 4;
        n->OnEvent(cShapeRefNodeImpl::kMaterialsChangedEvent, 0, &body, 0);
        break;
    }
    case 3:
    case 4: { // 10
        uint32_t id, extra;
        if (!PickAttribute(n, rng, &id, &extra)) break;
        cAttributeValue out;
        out.mType = 0x1111;
        for (float &f : out.mValues) f = 77.0f;
        n->GetAttribute(P(&out), id, extra);
        drawfx::Note("get", {id, extra});
        NoteValue("got", out);
        break;
    }
    case 5:
    case 6:
    case 7: { // 7, from the current value (some components moved, by a little or more)
        uint32_t id, extra;
        if (!PickAttribute(n, rng, &id, &extra)) break;
        cAttributeValue value;
        value.mType = 0x2222;
        for (float &f : value.mValues) f = 0.0f;
        if (id != 0x12345678u) n->GetAttribute(P(&value), id, 0);
        if (id != kColourId || extra == 0) {
            cAttributeValue current = value;
            if (id == kColourId || std::find(std::begin(kFloatIds), std::end(kFloatIds), id) != std::end(kFloatIds) ||
                id == kMorphWeightId)
                n->GetAttribute(P(&current), id, extra);
            value = current;
        }
        for (float &f : value.mValues)
            if (rng() % 2) f = AttributeFloat(rng, f);
        if (id == cITransformNode::kLimitAttribute) value.mValues[0] = std::bit_cast<float>(rng() % 4);
        drawfx::Note("set", {id, extra});
        NoteValue("value", value);
        n->SetAttribute(id, extra, P(&value));
        NoteMorphs("after set", which);
        NoteBlock("after set", which);
        drawfx::Note("colour", Words(n)[51]);
        break;
    }
    case 8:
    case 9: { // 51
        uint32_t id, extra;
        if (rng() % 6 == 0) id = 0x12345678u;
        else if (!PickAttribute(n, rng, &id, &extra)) id = kFloatIds[rng() % 4];
        drawfx::Note("names of", id);
        WalkNames(n, id);
        gNamePool->Note();
        break;
    }
    }
}

// ---- drawing (SG_ShapeRefNode_Draw, lane TD) ----
// Slot 82 (0xa548d0, the Impl's 0xa555d0) against a view (its drawer a fake: slot 25 draws the node placed, in its box,
// with the plane mask), its frustum planes axis-aligned around the origin at random distances (or wild), its frame
// random, the mask all planes or some; and slot 83 (0xa548b0, the Impl's 0xa55430) called in runs after content changes
// (60 directly, or the world refresh), the shapes' rest transforms (level info word +0x18) checked every eleventh call
// (0xae79e0, game code on both sides), each shape's slot 46 handed the flags, the lists and the detail handle.
struct DrawWorld {
    Fake drawer;
    cViewerView view;
};
DrawWorld dw;

uint32_t ShapeDrawn(Fake &, const uint32_t *a)
{
    const uint32_t *table = reinterpret_cast<const uint32_t *>(uintptr_t(a[1]));
    const uint32_t *weights = reinterpret_cast<const uint32_t *>(uintptr_t(a[2]));
    drawfx::Note("46 lists", {drawfx::Ptr(*table), drawfx::Ptr(*weights)});
    return 0;
}

void RandomRest(unsigned i, std::mt19937 &rng)
{
    gRestOn[i] = rng() % 3 != 0;
    for (auto &t : gRest[i]) {
        const bool same = rng() % 3 != 0;
        for (unsigned c = 0; c < 3; ++c) t[c] = same ? 0.0f : float(int(rng() % 5) - 2) * (rng() % 4 ? 1.0f : 0.001f);
        const float level[4] = {0.0f, 0.0f, 0.0f, 1.0f}, turned[4] = {0.0f, 0.6f, 0.0f, 0.8f};
        std::memcpy(t + 3, same || rng() % 2 ? level : turned, sizeof(level));
    }
}

void RandomView(std::mt19937 &rng)
{
    cViewerView &v = dw.view;
    std::memset(&v, 0, sizeof(v));
    v.mDrawer = &dw.drawer;
    for (unsigned p = 0; p < 6; ++p) {
        float *plane = v.mFrustum.plane[p];
        if (rng() % 10 == 0) {
            for (unsigned c = 0; c < 4; ++c) plane[c] = sgtx::RandomFloat(rng);
            continue;
        }
        plane[p / 2] = p % 2 ? 1.0f : -1.0f;
        plane[3] = -float(1 + rng() % 8) * 0.75f;
    }
    const AffineTransform3 f = RandomFrame(rng);
    std::memcpy(&v.mFrame, &f, sizeof(v.mFrame));
}

void BuildDraw(uint32_t seed, drawfx::Cells &cells)
{
    BuildBounds(seed, cells);
    std::mt19937 rng(seed ^ 0xd4a3u);
    dw.drawer.Reset<sgfx::kUnknown>(830);
    dw.drawer.Settle<25, 4>("aF13F6u");
    for (unsigned i = 0; i < 3; ++i) {
        w.shapes[i].On(46, &ShapeDrawn);
        RandomRest(i, rng);
    }
    RandomView(rng);
}

void OperateDraw(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    const unsigned which = rng() % ns.count;
    cICompositionTreeNode *n = ns[which];
    cIShapeRefNode *s = As(n);
    const unsigned op = rng() % 13;
    drawfx::Note("op", {op, which});
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0:
    case 1: { // 82 (a quarter of the time slot 70's bit 0x100 flipped first: set, the frustum test is skipped)
        uint32_t mask = uint32_t(rng());
        if (rng() % 3 == 0) mask = (mask & 0xffffff00u) | 0x3f;
        else if (rng() % 2) mask &= 0x3f;
        if (rng() % 4 == 0) s->SetShapeFlags(uint16_t(s->GetShapeFlags() ^ 0x100));
        drawfx::Note("draw", {mask, s->GetShapeFlags()});
        s->Draw(&dw.view, mask);
        break;
    }
    case 2:
    case 3: { // 83, in a run
        const unsigned calls = 1 + rng() % 12;
        void *const b = reinterpret_cast<void *>(uintptr_t(rng()));
        for (unsigned c = 0; c < calls; ++c) {
            s->Slot83(&w.context, b);
            drawfx::Note("83 flags", s->GetShapeFlags());
        }
        break;
    }
    case 4: // a shape added
        if (s->ShapeCount() < 6) s->AddShape(reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]));
        break;
    case 5: { // a listed shape removed
        const cShapeRange r = s->GetShapes();
        if (r.mEnd != r.mFirst) s->RemoveShape(r.mFirst[rng() % (r.mEnd - r.mFirst)]);
        break;
    }
    case 6: // a transform moved (the nodes under it stale)
        if (sw.xfCount) RandomPlace(Xf(sw.xf[rng() % sw.xfCount]), rng);
        break;
    case 7: // a shape's box or rest transforms, or the view
        if (rng() % 3 == 0) RandomView(rng);
        else if (rng() % 2) RandomGrow(rng() % 3, rng);
        else RandomRest(rng() % 3, rng);
        break;
    case 8:
    case 12: { // 60 called directly (12: then eleven 83 calls, so the rest-transform check runs on these records)
        uint32_t records[4][8];
        const float q[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.6f, 0.0f, 0.8f}};
        const uint32_t count = rng() % 5;
        for (uint32_t r = 0; r < 4; ++r) {
            records[r][0] = rng() % 4;
            for (unsigned c = 0; c < 3; ++c) records[r][1 + c] = std::bit_cast<uint32_t>(float(int(rng() % 7) - 3));
            const float *rot = q[rng() % 2];
            for (unsigned c = 0; c < 4; ++c) records[r][4 + c] = std::bit_cast<uint32_t>(rot[c]);
        }
        cRigidTransform t{};
        for (unsigned c = 0; c < 3; ++c) t.mValues[c] = float(int(rng() % 9) - 4);
        std::memcpy(t.Rotation(), q[rng() % 2], 16);
        drawfx::Note("60", {count, records[0][0], records[1][0], records[2][0], records[3][0]});
        n->OnContentChanged(P(&t), count ? P(records) : 0, count);
        if (op == 12)
            for (unsigned c = 0; c < 11; ++c) {
                s->Slot83(&w.context, nullptr);
                drawfx::Note("83 flags", s->GetShapeFlags());
            }
        break;
    }
    case 9: { // a node moved under a transform or to the top
        cICompositionTreeNode *c = ns[rng() % ns.count];
        if (cICompositionTreeNode *p = c->GetParent()) p->RemoveChild(c);
        if (sw.xfCount && rng() % 2) sw.xf[rng() % sw.xfCount]->AddChild(c);
        break;
    }
    case 10: { // the materials changed (the block rebuilt by the event)
        cSubordinateListChanged body(0, 0);
        RandomMaterials(rng);
        n->OnEvent(cShapeRefNodeImpl::kMaterialsChangedEvent, 0, &body, 0);
        break;
    }
    case 11: { // 25
        const uint32_t iids[] = {cIShapeRefNode::kIid, sgfx::kIidTree, sgfx::kIidGraph, 0x12345678u};
        drawfx::Note("25", uint8_t(n->IsKindOf(iids[rng() % 4])));
        break;
    }
    }
}

// ---- the behaviour slots (SG_ShapeRefNode_Behavior, lane TF) ----
// NotifyListeners 5 (0xa58850: the parent told first, then the listeners), AskListeners 6 (0xa55230: the parent asked, else
// the listeners), GetEventCount 12 (0xa57fa0), GetEventIds 13 (0xa54990, the ids walked and the three iterators released),
// GetEventAttribute 14 (0xa54ea0, the Impl's 0xa55f00: the record found or the missing one, the morph weight's count from
// the node's morphs, the four parameter lists' counts from the block when there is one), and the +8 face's QueryInterface
// 0xa58092 and Release 0xa586e5. The registry (0xa585a0, ours) is the planted map on both sides, with the shape-reference
// records added to it through the stand-in (AddOwnRecords). Blocks and morph counts change through the materials event, as
// in the Attributes test.
constexpr uint32_t kOwnIds[] = {kMorphWeightId, kFloatIds[0], kFloatIds[1], kFloatIds[2], kFloatIds[3], kColourId};
const char *const kOwnNames[] = {"w", "f1", "f2", "f3", "f4", "c"};
void AddOwnRecords()
{
    for (unsigned i = 0; i < 6; ++i) {
        const nGZEvent::cEventAttribute a{kOwnIds[i], kOwnNames[i], 7u + i, 0x400u + i, 3u, 0xcu};
        cShapeRefNodeBehaviorTraits::RegistryFor(kOwnIds[i], &a);
    }
}

uint32_t NameHash(const char *s)
{
    if (!s) return 0xffffffffu;
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}

void OperateBehavior(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    const unsigned which = rng() % ns.count;
    cICompositionTreeNode *n = ns[which];
    cIShapeRefNode *s = As(n);
    const unsigned op = rng() % 9;
    drawfx::Note("op", {op, which});
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: { // the materials or the morphs changed, the block rebuilt; or a shape added or removed
        const unsigned what = rng() % 4;
        if (what == 0 && s->ShapeCount() < 5) {
            s->AddShape(reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]));
        } else if (what == 1) {
            const cShapeRange r = s->GetShapes();
            if (r.mEnd != r.mFirst) s->RemoveShape(r.mFirst[rng() % (r.mEnd - r.mFirst)]);
        } else {
            if (rng() % 2) RandomMaterials(rng);
            else gMorphs[rng() % 3] = rng() % 4;
        }
        cSubordinateListChanged body(0, 0);
        n->OnEvent(cShapeRefNodeImpl::kMaterialsChangedEvent, 0, &body, 0);
        NoteBlock("block", which);
        break;
    }
    case 1:
    case 2: { // 5 and 6
        const uint32_t types[] = {cShapeRefNodeImpl::kBlockChangedEvent, kListenedQuery, cShapeRefNodeImpl::kDetailChangedEvent,
                                  cShapeRefNodeImpl::kRefreshQuery, 0x777u};
        const uint32_t type = types[rng() % 5];
        const uint32_t arg1 = rng() % 4;
        cSubordinateListChanged body(0, 0);
        void *const data = rng() % 3 ? &body : nullptr;
        drawfx::Note("listeners", {type, arg1, uint32_t(data != nullptr)});
        if (op == 1)
            n->NotifyListeners(type, arg1, data);
        else
            drawfx::Note("asked", uint8_t(n->AskListeners(type, arg1, data)));
        for (unsigned i = 0; i < ns.count; ++i) drawfx::Note("flags", ns[i]->GetFlags());
        break;
    }
    case 3: { // 12 and 13
        drawfx::Note("event count", n->GetEventCount());
        nGZEvent::cAttributeIDRange ids = n->GetEventIds();
        unsigned steps = 0;
        while (steps < 40 && !ids.mCurrent->IsEqual(ids.mEnd)) {
            drawfx::Note("event id", ids.mCurrent->Get());
            ids.mCurrent->Next();
            ++steps;
        }
        drawfx::Note("event ids", {steps, ids.mCurrent->Release(), ids.mBegin->Release(), ids.mEnd->Release()});
        break;
    }
    case 4:
    case 5:
    case 6: { // 14
        const uint32_t others[] = {sgfx::kRegistryIds[0], sgfx::kRegistryIds[1], sgfx::kRegistryIds[2], 0x5678u, 0x9f5fdb0u};
        const uint32_t event = rng() % 4 ? kOwnIds[rng() % 6] : others[rng() % 5];
        const nGZEvent::cEventAttribute a = n->GetEventAttribute(event);
        drawfx::Note("event attribute", {event, a.mId, NameHash(a.mName), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        break;
    }
    case 7: { // the +8 face's QueryInterface
        const uint32_t iids[] = {cIShapeRefNode::kIid, cRenderableNodeImpl::kIid, 0x4512dd13, 1, 0xdb01d4b2, 0x12345678u};
        const uint32_t iid = iids[rng() % 6];
        auto *const face = reinterpret_cast<cIGZUnknown *>(reinterpret_cast<uint8_t *>(n) + 8);
        void *out = nullptr;
        const uint32_t before = CountOf(n);
        const bool r = face->QueryInterface(iid, &out);
        drawfx::Note("face query", {iid, uint8_t(r), drawfx::Norm(P(out)), CountOf(n) - before});
        if (CountOf(n) != before) n->Release();
        break;
    }
    case 8: { // the +8 face's Release (a reference taken first, so the node lives)
        auto *const face = reinterpret_cast<cIGZUnknown *>(reinterpret_cast<uint8_t *>(n) + 8);
        const uint32_t times = 1 + rng() % 3;
        for (uint32_t t = 0; t < times; ++t) n->AddRef();
        for (uint32_t t = 0; t < times; ++t) drawfx::Note("face release", face->Release());
        break;
    }
    }
}

struct Scopes {
    sgrx::Services services;
    sgfx::RegistryScope node{sgfx::kNodeRegistry};
    sgfx::RegistryScope transform{sgfx::kTransformRegistry};
    sgfx::RegistryScope shapeRef{kShapeRefRegistry};
    sgtp::RefreshScope refresh;
};

} // namespace

DIFF_TEST(SG_ShapeRefNode_Factory, kFactory)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "factory", 16, Build, [&](bool original, std::mt19937 &rng) {
        // Half the seeds the constructor's own way on both sides (0xa541e0 against Construct): fresh memory, the
        // constructor run in it, the node it returns that memory.
        const bool shaped = rng() % 2;
        cICompositionTreeNode *n;
        if (shaped) {
            void *const memory = ::operator new(kSize);
            cShapeRefNode *const made = original ? ctx.Original<cShapeRefNode *(__thiscall *)(void *)>(0xa541e0)(memory)
                                                 : static_cast<cShapeRefNode *>(memory)->Construct();
            drawfx::Note("in place", uint8_t(static_cast<void *>(made) == memory));
            n = made;
        } else {
            n = original ? Maker(ctx, true)() : MakeOurs();
        }
        drawfx::Note("count", CountOf(n));
        Hold(n);
        w.nodes.size = kSize;
        if (shaped && !original) LayGameShape(0);
        NoteNodes("new node");
        const_cast<uint32_t *>(Words(n))[29] = uint32_t(rng());
        drawfx::Note("limit", As(n)->GetDetailLimit());
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ShapeRefNode_Slots, kFactory)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "slots", 64, Build, [&](bool original, std::mt19937 &rng) {
        MakeSidedTree(ctx, original, rng);
        NoteNodes("built");
        for (int k = 0; k < 24; ++k) Operate(ctx, k, rng);
        NoteNodes("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ShapeRefNode_Clone, 0xa54ec0)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "clone", 64, Build, [&](bool original, std::mt19937 &rng) {
        sgfx::Factory make = Maker(ctx, original);
        MakeSidedTree(ctx, original, rng);
        cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
        cIShapeRefNode *s = As(n);
        if (rng() % 2) s->SetShapeFlags(uint16_t(rng()));
        if (rng() % 2) s->SetWord6c(uint32_t(rng()));
        if (rng() % 2) s->AddLayerBits(sgrx::RandomMask(rng));
        const uint32_t mode = rng() % 3;
        cINodeCloneContext *context = nullptr;
        if (rng() % 3 == 0 && mode != 1) {
            cICompositionTreeNode *prepared = make();
            Hold(prepared);
            const_cast<uint32_t *>(Words(prepared))[29] = uint32_t(rng());
            sgfx::MakeCloneContext(w.context, prepared);
            context = reinterpret_cast<cINodeCloneContext *>(&w.context);
        }
        const uint32_t iids[] = {sgfx::kIidTree, sgfx::kIidGraph, cIShapeRefNode::kIid, 0x12345678};
        void *out = nullptr;
        n->Clone(mode, iids[rng() % 4], &out, context);
        const bool isNew = out && drawfx::Norm(P(out)) == P(out);
        if (isNew) {
            drawfx::AddRange(out, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
            w.nodes.at[w.nodes.count++] = static_cast<cICompositionTreeNode *>(out);
            const_cast<uint32_t *>(Words(static_cast<cICompositionTreeNode *>(out)))[29] = 0x1234;
        }
        drawfx::Note("clone", {out != nullptr, uint8_t(isNew), drawfx::Norm(P(out))});
        if (out && !isNew) static_cast<cICompositionTreeNode *>(out)->Release();
        if (out) HoldSubtree(static_cast<cICompositionTreeNode *>(out));
        for (const Fake &f : w.shapes) drawfx::Note("shape refs", {f.id, uint32_t(f.refs)});
        NoteNodes("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ShapeRefNode_Shapes, 0xa55750)
{
    Scopes scopes;
    WarmOurStatics();
    sgfx::CheckSides(ctx, "shapes", 64, BuildShapes, [&](bool original, std::mt19937 &rng) {
        MakeSidedTree(ctx, original, rng);
        for (unsigned i = 0; i < w.nodes.count; ++i) {
            w.manager.Listen(w.nodes[i], cShapeRefNodeImpl::kBlockChangedEvent, sw.listener,
                             cShapeRefNodeImpl::kBlockChangedEvent);
            if (rng() % 2) w.manager.Listen(w.nodes[i], kListenedQuery, sw.listener, kListenedQuery);
        }
        MakeTransforms(ctx, original, rng);
        for (unsigned i = 0; i < w.nodes.count; ++i)
            for (unsigned k = rng() % 3; k > 0; --k)
                As(w.nodes[i])->AddShape(reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]));
        NoteShapeNodes("built");
        for (int k = 0; k < 32; ++k) OperateShapes(ctx, k, rng);
        NoteShapeNodes("after");
        ReleaseShapes();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ShapeRefNode_Persist, 0xa56c90)
{
    Scopes scopes;
    WarmOurStatics();
    sgfx::CheckSides(ctx, "persist", 64, BuildPersist, [&](bool original, std::mt19937 &rng) {
        MakeSidedTree(ctx, original, rng);
        for (unsigned i = 0; i < w.nodes.count; ++i)
            for (unsigned k = rng() % 3; k > 0; --k)
                As(w.nodes[i])->AddShape(reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]));
        NoteAllMorphs("built");
        for (int k = 0; k < 24; ++k) OperatePersist(ctx, k, rng);
        NoteShapeNodes("after");
        NoteAllMorphs("after");
        sgfx::gReader.NoteNames();
        ReleaseShapes();
        sgfx::gPool.NoteCounts();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ShapeRefNode_Bounds, 0xa572f0)
{
    Scopes scopes;
    WarmOurStatics();
    sgfx::CheckSides(ctx, "bounds", 64, BuildBounds, [&](bool original, std::mt19937 &rng) {
        MakeSidedTree(ctx, original, rng);
        MakeTransforms(ctx, original, rng);
        for (unsigned i = 0; i < w.nodes.count; ++i)
            for (unsigned k = rng() % 4; k > 0; --k)
                As(w.nodes[i])->AddShape(reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]));
        NoteShapeNodes("built");
        for (int k = 0; k < 40; ++k) OperateBounds(ctx, k, rng);
        NoteShapeNodes("after");
        ReleaseShapes();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ShapeRefNode_Attributes, 0xa575a0)
{
    Scopes scopes;
    NamePool pool;
    gNamePool = &pool;
    WarmOurStatics();
    WarmNameStatics();
    sgfx::CheckSides(ctx, "attributes", 64, BuildAttributes, [&](bool original, std::mt19937 &rng) {
        MakeSidedTree(ctx, original, rng);
        for (unsigned i = 0; i < w.nodes.count; ++i)
            for (unsigned k = rng() % 4; k > 0; --k)
                As(w.nodes[i])->AddShape(reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]));
        NoteAllMorphs("built");
        for (int k = 0; k < 32; ++k) OperateAttributes(ctx, k, rng);
        NoteShapeNodes("after");
        NoteAllMorphs("after");
        gNamePool->Note();
        ReleaseShapes();
    });
    gNamePool = nullptr;
    w.manager.Drop();
}

DIFF_TEST(SG_ShapeRefNode_Draw, 0xa555d0)
{
    Scopes scopes;
    WarmOurStatics();
    sgfx::CheckSides(ctx, "draw", 64, BuildDraw, [&](bool original, std::mt19937 &rng) {
        MakeSidedTree(ctx, original, rng);
        MakeTransforms(ctx, original, rng);
        for (unsigned i = 0; i < w.nodes.count; ++i)
            for (unsigned k = rng() % 4; k > 0; --k)
                As(w.nodes[i])->AddShape(reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]));
        NoteShapeNodes("built");
        for (int k = 0; k < 40; ++k) OperateDraw(ctx, k, rng);
        NoteShapeNodes("after");
        ReleaseShapes();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ShapeRefNode_Behavior, 0xa55f00)
{
    Scopes scopes;
    AddOwnRecords();
    NamePool pool;
    gNamePool = &pool;
    WarmOurStatics();
    WarmNameStatics();
    sgfx::CheckSides(ctx, "behavior", 64, BuildAttributes, [&](bool original, std::mt19937 &rng) {
        MakeSidedTree(ctx, original, rng);
        for (unsigned i = 0; i < w.nodes.count; ++i) {
            w.manager.Listen(w.nodes[i], cShapeRefNodeImpl::kBlockChangedEvent, sw.listener,
                             cShapeRefNodeImpl::kBlockChangedEvent);
            if (rng() % 2) w.manager.Listen(w.nodes[i], kListenedQuery, sw.listener, kListenedQuery);
        }
        for (unsigned i = 0; i < w.nodes.count; ++i)
            for (unsigned k = rng() % 4; k > 0; --k)
                As(w.nodes[i])->AddShape(reinterpret_cast<cIShape *>(&w.shapes[rng() % 3]));
        NoteShapeNodes("built");
        for (int k = 0; k < 40; ++k) OperateBehavior(ctx, k, rng);
        NoteShapeNodes("after");
        ReleaseShapes();
    });
    gNamePool = nullptr;
    w.manager.Drop();
}
