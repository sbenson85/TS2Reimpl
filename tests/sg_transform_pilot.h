#pragma once
// The pilot tests of the transform family (lane OB, scenegraph N0 part 3 step 3): cTransformNode (factory 0xa3a6c0) and
// cExplicitBoundNode (factory 0xa4bc30), the game's node (its factory, its vtables and the template bodies behind them)
// against ours (Create, our vtables), each side building its own nodes with its own factory, as
// tests/sg_composition_tree_node.cpp does for the composition node. Pilot<Cfg> drives whole trees through the slots and the
// +8 persist face and compares the calls the nodes make on the fakes (sgfx.h), the notes, every node's words but its two
// vtable words, and the blocks freed when the test lets go of the nodes. The tree code (propagation, world transform
// refresh) is original code on both sides: the nodes are real, so the stale-cache path (flag 12) runs.
//
// A Cfg names the factory, the node size, our Create, the node interface the slots are called through, the level (0: a
// transform node; 2: an explicit-bound node, which adds the bound, holder and shape slots; 3: a partition node, the
// bound and holder slots only, its own record in the persist stream) and the behaviour's cell.
#include "difftest.h"
#include "sg_transform_fx.h"
#include "sgfx.h"

#include "sg/cExplicitBoundNode.h"
#include "sg/cTransformNode.h"

#include <algorithm>
#include <array>
#include <bit>
#include <random>

namespace sgtp {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

// ---- the shape the explicit-bound node keeps (cIBound) and the line it is tested with (cILinear3) ----
namespace detail {
inline void NoteWordsAt(const char *label, uint32_t addr, unsigned n)
{
    drawfx::Call c;
    c.note = label;
    const uint32_t *p = reinterpret_cast<const uint32_t *>(uintptr_t(addr));
    for (unsigned i = 0; i < n; ++i) c.words.push_back(p[i]);
    drawfx::gLog.push_back(std::move(c));
}
// Clone(iid, out): hands out the copy (counted) the test prepared.
inline uint32_t ShapeClone(Fake &self, const uint32_t *a)
{
    Fake *copy = reinterpret_cast<Fake *>(uintptr_t(self.value[0]));
    if (!copy) return 0;
    ++copy->refs;
    *reinterpret_cast<Fake **>(uintptr_t(a[1])) = copy;
    return 1;
}
// 20 (frame, a, b): the frame noted; true when value[1] says so.
inline uint32_t ShapeSlot20(Fake &self, const uint32_t *a)
{
    NoteWordsAt("frame", a[0], 13);
    return self.value[1];
}
// 37 (out, frame): the box in the frame (a function of the frame's words); 38 (out, rigid): in the rigid transform.
inline uint32_t ShapeSlot37(Fake &, const uint32_t *a)
{
    NoteWordsAt("frame", a[1], 13);
    const uint32_t *f = reinterpret_cast<const uint32_t *>(uintptr_t(a[1]));
    uint32_t *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (unsigned i = 0; i < 6; ++i) out[i] = f[7 + i];
    return 0;
}
inline uint32_t ShapeSlot38(Fake &, const uint32_t *a)
{
    NoteWordsAt("rigid", a[1], 7);
    const uint32_t *f = reinterpret_cast<const uint32_t *>(uintptr_t(a[1]));
    uint32_t *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (unsigned i = 0; i < 6; ++i) out[i] = f[i] ^ 0x1u;
    return 0;
}
// The line's 18 (bound, frame) and 29 (t, bound, frame): the frame noted; the answers in value[].
inline uint32_t LinearIntersects(Fake &self, const uint32_t *a)
{
    NoteWordsAt("frame", a[1], 13);
    return self.value[1];
}
inline uint32_t LinearFirstHit(Fake &self, const uint32_t *a)
{
    NoteWordsAt("frame", a[2], 13);
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = self.value[0];
    return self.value[1];
}
} // namespace detail

#define SGFX_U {0, Ret::Int, false, "", ""}
inline constexpr Slot kBoundSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {2, Ret::Int, true, "Clone", "ui"}, // 3
    SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U,
    SGFX_U, SGFX_U,                                // 4-19
    {3, Ret::Int, true, "Slot20", "iuu"},          // 20
    SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U, SGFX_U,
    SGFX_U, SGFX_U, // 21-36
    {2, Ret::Int, true, "Slot37", "ii"},           // 37
    {2, Ret::Int, true, "Slot38", "ii"},           // 38
};
inline constexpr drawfx::Iface kBound{"Bound", 39, kBoundSlots};
constexpr std::array<Slot, 30> MakeLinearSlots()
{
    std::array<Slot, 30> a{};
    for (Slot &s : a) s = {0, Ret::Int, false, "", ""};
    a[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    a[1] = {0, Ret::Int, true, "AddRef", ""};
    a[2] = {0, Ret::Int, true, "Release", ""};
    a[18] = {2, Ret::Int, true, "IntersectsBoundIn", "ii"};
    a[29] = {3, Ret::Int, true, "FirstHitBoundIn", "iii"};
    return a;
}
inline constexpr std::array<Slot, 30> kLinearSlots = MakeLinearSlots();
inline constexpr drawfx::Iface kLinear{"Linear", 30, kLinearSlots.data()};
#undef SGFX_U

// The holder of a bounded node (slot 69's answer; the partition's object): the tree code's propagation (0xa39820) calls its
// slot 79 (RemoveNode) with the node, or slot 78 (RemovePartition, `ret 4` in the octree's 0xa52840) with the node when
// the node's flag 6 is set: the node is pushed before the branch (lane OM; this fake declared slot 78 without an
// argument, which only the original's frame-pointer code survived). Its other slots are unsettled.
constexpr std::array<Slot, 80> MakeHolderSlots()
{
    std::array<Slot, 80> a{};
    for (Slot &s : a) s = {0, Ret::Int, false, "", ""};
    a[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    a[1] = {0, Ret::Int, true, "AddRef", ""};
    a[2] = {0, Ret::Int, true, "Release", ""};
    a[78] = {1, Ret::Int, true, "RemovePartition", "i"};
    a[79] = {1, Ret::Int, true, "Slot79", "i"};
    return a;
}
inline constexpr std::array<Slot, 80> kHolderSlots = MakeHolderSlots();
inline constexpr drawfx::Iface kHolder{"Holder", 80, kHolderSlots.data()};

struct Parts {
    Fake shape, copy, linear, holder;
    void Build(std::mt19937 &rng)
    {
        shape.Reset<kBound>(130);
        copy.Reset<kBound>(131);
        linear.Reset<kLinear>(132);
        holder.Reset<kHolder>(133);
        shape.value[0] = P(&copy);
        shape.On(3, &detail::ShapeClone);
        for (Fake *b : {&shape, &copy}) {
            b->value[1] = rng() % 2;
            b->On(20, &detail::ShapeSlot20);
            b->On(37, &detail::ShapeSlot37);
            b->On(38, &detail::ShapeSlot38);
        }
        linear.value[0] = std::bit_cast<uint32_t>(float(int(rng() % 400) - 100) / 16.0f);
        linear.value[1] = rng() % 2;
        linear.On(18, &detail::LinearIntersects);
        linear.On(29, &detail::LinearFirstHit);
    }
};

// The statics of the tree code's world transform refresh (0xa39b70): a function-local static holding a table of 50 records
// of 8 words (limit 0x7fffffff, identity rotation: 0x640 bytes the original takes from operator new, begin / end /
// capacity at 0x10f5fb0 / b4 / b8, then a count 0x32 at fbc) and a worklist (count, data at fd4 / fd8), behind a
// thread-safe-static guard at 0x10f5fe8. Built on first use through the guard machinery, which the harness has not
// initialised; the scope places the finished statics (the table in the scope's own memory), the guard marked done, and
// restores the words at its end.
class RefreshScope {
public:
    RefreshScope()
    {
        std::memcpy(saved_, GameAddress<void>(kBase), sizeof(saved_));
        std::memset(GameAddress<void>(kBase), 0, sizeof(saved_));
        for (unsigned i = 0; i < 50; ++i) {
            uint32_t *record = table_ + i * 8;
            record[0] = 0x7fffffff;
            record[7] = 0x3f800000;
        }
        *GameAddress<uint32_t>(0x10f5fb0) = P(table_);
        *GameAddress<uint32_t>(0x10f5fb4) = P(table_) + 0x640;
        *GameAddress<uint32_t>(0x10f5fb8) = P(table_) + 0x640;
        *GameAddress<uint32_t>(0x10f5fbc) = 0x32;
        *GameAddress<int32_t>(0x10f5fe8) = INT_MIN;
    }
    ~RefreshScope() { std::memcpy(GameAddress<void>(kBase), saved_, sizeof(saved_)); }
    RefreshScope(const RefreshScope &) = delete;
    RefreshScope &operator=(const RefreshScope &) = delete;

private:
    static constexpr uint32_t kBase = 0x10f5fa0;
    uint8_t saved_[0x50];
    uint32_t table_[400] = {};
};

// A Cfg's optional parts (lane OI, the partition level): kSkipWords (more node words left out of the comparison, each
// with its reason in the Cfg), Scope (a type held for the whole test: pools the class allocates from), Setup(seed) (run
// as each side's world is built) and Prepare(node, rng) (run on each node MakeTree makes). Lane OK added SetupCells(seed,
// cells) (Setup with the cells, for fakes that sit in game cells), ReadResource(reader, rng) (what a read takes between
// the class's own record and the level records: cSceneNode's cSGResource part) and NoteNode(label, node) (state of a node
// kSkipWords leaves out, noted wherever the node words are) and kRootsOnly (every node a root: MakeTree links none, op 1
// moves none, op 13's event names the node itself; a class whose nodes are scene roots, which the tree code cannot
// nest: see tests/sg_scene_node.cpp) and kFixedFlags (flag bits 0-31 ops 2 and 20 never change).
namespace detail {
struct NoScope {};
template <class Cfg> constexpr uint32_t FixedFlagsOf()
{
    if constexpr (requires { Cfg::kFixedFlags; })
        return Cfg::kFixedFlags;
    else
        return 0;
}
template <class Cfg> constexpr uint64_t SkipWordsOf()
{
    if constexpr (requires { Cfg::kSkipWords; })
        return Cfg::kSkipWords;
    else
        return 0;
}
template <class Cfg> struct ScopeOf {
    using type = NoScope;
};
template <class Cfg>
    requires requires { typename Cfg::Scope; }
struct ScopeOf<Cfg> {
    using type = typename Cfg::Scope;
};
} // namespace detail

template <class Cfg> class Pilot {
public:
    using Iface = typename Cfg::Interface;
    static constexpr uint32_t kSize = Cfg::kSize;
    static constexpr uint32_t kSkipVtables = 0b101; // words 0 and 2
    static constexpr uint64_t kSkipWords = kSkipVtables | detail::SkipWordsOf<Cfg>();
    static constexpr unsigned kSeeds = 64;
    static constexpr bool kBounded = Cfg::kLevel >= 2;
    static constexpr bool kExplicit = Cfg::kLevel == 2;  // an explicit-bound node: the shape slots, the line fakes
    static constexpr bool kPartition = Cfg::kLevel == 3; // a partition node: its own record in the persist stream
    static constexpr bool kRootsOnly = requires { Cfg::kRootsOnly; }; // see the Cfg's optional parts
    static constexpr uint32_t kFixedFlags = detail::FixedFlagsOf<Cfg>();

    static void Prepare(cICompositionTreeNode *n, std::mt19937 &rng)
    {
        if constexpr (requires { Cfg::Prepare(n, rng); }) Cfg::Prepare(n, rng);
    }

    struct World {
        sgfx::Nodes nodes;
        Fake ext[3];
        Fake traversal, context, parent;
        Parts parts;
        sgfx::Manager manager;
    };
    static inline World w;

    static cICompositionTreeNode *__cdecl MakeOurs() { return Cfg::Create(); }
    static sgfx::Factory Maker(difftest::Context &ctx, bool original)
    {
        if constexpr (requires { Cfg::OriginalFactory(ctx); }) return original ? Cfg::OriginalFactory(ctx) : &MakeOurs;
        else return original ? ctx.Original<sgfx::Factory>(Cfg::kFactory) : &MakeOurs;
    }
    static Iface *As(cICompositionTreeNode *n) { return static_cast<Iface *>(n); }

    static void Build(uint32_t seed, drawfx::Cells &cells)
    {
        std::mt19937 rng(seed);
        sgfx::gPool.Reset(cells);
        sgfx::gPool.Handle("alpha");
        sgfx::gPool.Handle("beta");
        sgfx::gReader.Reset();
        sgfx::gWriter.Reset();
        for (unsigned i = 0; i < 3; ++i) sgfx::MakeExtension(w.ext[i], 200 + i, rng() % 2);
        sgfx::MakeTraversal(w.traversal, 450, rng() % 3);
        sgfx::MakeNode(w.parent, 110);
        w.parts.Build(rng);
        w.nodes = sgfx::Nodes{};
        w.manager.Drop(); // the other side's
        w.manager.Use(cells, Cfg::kEventCell);
        if constexpr (requires { Cfg::Setup(seed); }) Cfg::Setup(seed);
        if constexpr (requires { Cfg::SetupCells(seed, cells); }) Cfg::SetupCells(seed, cells);
    }

    // Every node's words (kSkipWords left out), then what the Cfg notes of each.
    static void NoteNodes(const char *label)
    {
        w.nodes.NoteWords(label, kSkipWords);
        if constexpr (requires { Cfg::NoteNode(label, w.nodes[0]); })
            for (unsigned i = 0; i < w.nodes.count; ++i) Cfg::NoteNode(label, w.nodes[i]);
    }

    static void MakeTree(sgfx::Factory make, std::mt19937 &rng)
    {
        const unsigned n = 1 + rng() % 6;
        int parents[sgfx::Nodes::kMax];
        parents[0] = -1;
        for (unsigned i = 1; i < n; ++i) parents[i] = rng() % 5 ? int(rng() % i) : -1;
        if constexpr (kRootsOnly) std::fill(parents, parents + n, -1);
        w.nodes.Make(make, n, kSize);
        for (unsigned i = 0; i < n; ++i) Prepare(w.nodes[i], rng);
        w.nodes.Link(parents);
    }

    static uint32_t CountOf(const cICompositionTreeNode *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }
    static bool InSubtree(cICompositionTreeNode *root, cICompositionTreeNode *n)
    {
        for (; n; n = n->GetParent())
            if (n == root) return true;
        return false;
    }
    static void HoldSubtree(cICompositionTreeNode *root)
    {
        for (cICompositionTreeNode *c = root->GetFirstChild(); c; c = c->GetNextSibling()) {
            if (drawfx::Norm(P(c)) == P(c) && w.nodes.count < sgfx::Nodes::kMax) {
                drawfx::AddRange(c, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
                c->AddRef();
                w.nodes.at[w.nodes.count++] = c;
            }
            HoldSubtree(c);
        }
    }
    static void NoteTransform(const char *label, const cRigidTransform &t)
    {
        sgtx::NoteFloats(label, t.mValues, 7);
    }
    static void NoteBox(const char *label, const cBoundBox &b) { sgtx::NoteFloats(label, b.mMin, 6); }

    // The things the transform-level slots need to be driven and noted.
    static void NoteTransformState(Iface *n)
    {
        NoteTransform("local", n->GetLocalTransform());
        drawfx::Note("limit", n->GetLimit55());
    }

    static constexpr unsigned kOps = 19 + (kExplicit ? 5 : kBounded ? 3 : 0);

    static void Operate(difftest::Context &ctx, int k, std::mt19937 &rng)
    {
        sgfx::Nodes &ns = w.nodes;
        cICompositionTreeNode *n = ns[rng() % ns.count];
        const unsigned op = rng() % kOps;
        drawfx::Note("op", op);
        char step[32];
        std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
        sgfx::MarkStep(ctx, step);
        switch (op) {
        case 0: { // QueryInterface
            const uint32_t iids[] = {0x4512dd13, 0xdb01d4b2, 0x5aef607b, 0x5b0aaa74, 0xbb11a53e, 1, 0xe51c10f5,
                                     0x7d14eb66, 0x7d3a2385, 0xfb06dba0};
            uint32_t iid = iids[rng() % 10];
            if constexpr (kPartition)
                if (rng() % 3 == 0) iid = rng() % 2 ? cIPartitionNode::kIid : Cfg::kIid;
            void *out = nullptr;
            const uint32_t before = CountOf(n);
            const bool r = rng() % 8 ? n->QueryInterface(iid, &out) : n->QueryInterface(iid, nullptr);
            drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out)), CountOf(n) - before});
            if (CountOf(n) != before) n->Release();
            break;
        }
        case 1: { // move a node
            if constexpr (!kRootsOnly) {
                cICompositionTreeNode *c = ns[rng() % ns.count];
                if (cICompositionTreeNode *p = c->GetParent()) p->RemoveChild(c);
                cICompositionTreeNode *to = ns[rng() % ns.count];
                if (!InSubtree(c, to)) to->AddChild(c);
            }
            break;
        }
        case 2: { // flags (not bit 1: the bounded flag, which the propagation reads the holder for: op 20)
            uint32_t bit = rng() % 40;
            if ((bit & 31) == 1 || (bit & 31) == 12 || (kFixedFlags >> (bit & 31) & 1)) bit = 0;
            n->SetFlag(bit, rng() % 2);
            drawfx::Note("flags", {uint8_t(n->TestFlag(bit)), uint8_t(n->TestFlag(rng() % 40)), n->GetFlags()});
            break;
        }
        case 3: { // the tag
            const char *texts[] = {"alpha", "beta", "", "gamma", nullptr};
            if (rng() % 2)
                n->SetTag(texts[rng() % 5]);
            else
                n->SetTagHandle(rng() % 3 ? sgfx::Pool::kFirstHandle + rng() % 2 : sgfx::kNoHandle);
            sgfx::NoteText("tag", n->GetTag());
            drawfx::Note("tag handle", n->GetTagHandle());
            break;
        }
        case 4: { // kinds
            const uint32_t iids[] = {0x4512dd13, 0x5aef607b, 0x3b253dc7, 0xbb11a53e, 0x5b0aaa74, 1, 0, 0xdb01d4b2,
                                     0xe51c10f5, 0x7d14eb66, 0x7d3a2385};
            for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
            if constexpr (kPartition)
                for (uint32_t iid : {cIPartitionNode::kIid, Cfg::kIid}) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
            break;
        }
        case 5: { // children ranges, links
            const cChildRange a = n->GetChildren(), b = n->GetChildrenConst();
            drawfx::Note("children", {drawfx::Norm(P(a.mCurrent)), drawfx::Norm(P(a.mBegin)), drawfx::Norm(P(a.mEnd)),
                                      drawfx::Norm(P(b.mCurrent)), drawfx::Norm(P(b.mBegin)), drawfx::Norm(P(b.mEnd)),
                                      uint8_t(n->HasChildren())});
            drawfx::Note("links", {drawfx::Norm(P(n->GetParentConst())), drawfx::Norm(P(n->GetPrevSiblingConst())),
                                   drawfx::Norm(P(n->GetNextSiblingConst())), drawfx::Norm(P(n->GetFirstChildConst()))});
            break;
        }
        case 6: { // transforms
            NoteTransform("local", n->GetLocalTransform());
            NoteTransform("world", n->GetWorldTransform());
            cRigidTransform in{}, out{};
            for (float &f : in.mValues) f = float(int(rng() % 200) - 100) / 8.0f;
            n->ToParent1c(&out, &in);
            NoteTransform("to parent", out);
            n->FromParent1c(&out, &in);
            NoteTransform("from parent", out);
            cTransform34 in34{}, out34{};
            for (float &f : in34.mValues) f = float(int(rng() % 200) - 100) / 8.0f;
            n->ToParent34(&out34, &in34);
            sgtx::NoteFloats("34 to", out34.mValues, 12);
            n->FromParent34(&out34, &in34);
            sgtx::NoteFloats("34 from", out34.mValues, 12);
            break;
        }
        case 7: // the parent's answers (nodes or null: a scene node answers itself), the limit, the behaviour's
            drawfx::Note("misc", {drawfx::Norm(n->GetFromParent31()), drawfx::Norm(n->GetFromParent32()), n->GetLimit55(),
                                  n->IsActive(), n->GetEventCount()});
            break;
        case 8: { // traversals
            auto *t = reinterpret_cast<cICompositionTreeTraversal *>(&w.traversal);
            drawfx::Note("accept", {n->Accept(t), n->AcceptReadOnly(t)});
            break;
        }
        case 9: { // extensions
            Fake &e = w.ext[rng() % 3];
            auto *ext = reinterpret_cast<cIExtension *>(&e);
            if (n->HasExtension(ext)) {
                n->RemoveExtension(ext);
            } else if (!e.value[sgfx::kExtOwnerWord]) {
                n->AddExtension(ext);
            }
            void *out = nullptr;
            const bool found = n->FindExtension(0x7777, &out, nullptr);
            drawfx::Note("extensions", {drawfx::Norm(P(n->GetFirstExtension())), drawfx::Norm(P(n->GetFirstExtensionConst())),
                                        uint8_t(n->HasExtension(ext)), uint8_t(found)});
            break;
        }
        case 10: { // content changed (the bounded levels take a transform; the transform node ignores them)
            cRigidTransform t = sgtx::RandomRigid(rng);
            n->OnContentChanged(kBounded ? P(&t) : uint32_t(rng()), rng(), rng());
            drawfx::Note("flags", n->GetFlags());
            if (kBounded) NoteTransform("world", n->GetWorldTransform());
            break;
        }
        case 11: { // the +8 face
            drawfx::Note("referent", {drawfx::Norm(P(n->GetReferent())), drawfx::Norm(P(n->GetReferentConst())),
                                      drawfx::Norm(n->GetReferent()->GetOwnerInfo())}); // (a node or null)
            break;
        }
        case 12: { // events up the tree
            cICompositionTreeNode *sub = ns[rng() % ns.count];
            cGraphConnChanged body(rng() % 3, n, sub, rng() % 2 ? sgfx::kIidTree : sgfx::kIidGraph);
            n->NotifyListeners(cGraphConnChanged::kEventId, rng(), &body);
            drawfx::Note("asked", uint8_t(n->AskListeners(0x1234, rng(), &body)));
            drawfx::Note("query", uint8_t(n->OnQuery(0x1234, 0, nullptr)));
            break;
        }
        case 13: { // an event to the node itself
            cICompositionTreeNode *sub = ns[rng() % ns.count];
            if constexpr (kRootsOnly) sub = n;
            cGraphConnChanged body(rng() % 3, n, sub, sgfx::kIidTree);
            n->OnEvent(rng() % 2 ? cGraphConnChanged::kEventId : 0x777, 0, &body, 0);
            break;
        }
        case 14: { // the limit
            const uint32_t limits[] = {0x7fffffff, 0, 5, uint32_t(rng())};
            As(n)->SetLimit(limits[rng() % 4]);
            NoteTransformState(As(n));
            break;
        }
        case 15: { // the transform, whole or in parts
            const cRigidTransform t = sgtx::RandomRigid(rng);
            switch (rng() % 4) {
            case 0: As(n)->SetTransform(&t); break;
            case 1: As(n)->SetTransformParts(&t.mValues[0], &t.mValues[3]); break;
            case 2: As(n)->SetTranslation(&t.mValues[0]); break;
            case 3: As(n)->SetRotation(&t.mValues[3]); break;
            }
            NoteTransformState(As(n));
            NoteTransform("world", n->GetWorldTransform());
            break;
        }
        case 16: { // the attributes (slots 7 and 10)
            const uint32_t ids[] = {cITransformNode::kLimitAttribute, cITransformNode::kTransformAttribute,
                                    cITransformNode::kRotationAttribute, cITransformNode::kTranslationAttribute,
                                    0x12345678};
            const uint32_t id = ids[rng() % 5];
            cAttributeValue v;
            v.mType = uint32_t(rng());
            for (float &f : v.mValues) f = sgtx::RandomFloat(rng);
            if (id == cITransformNode::kLimitAttribute) v.mValues[0] = std::bit_cast<float>(uint32_t(rng() % 2 ? 7 : rng()));
            const uint32_t extra = rng();
            if (rng() % 2) n->SetAttribute(id, extra, P(&v));
            cAttributeValue out;
            out.mType = 0x1111;
            for (float &f : out.mValues) f = 77.0f;
            n->GetAttribute(P(&out), id, extra);
            drawfx::Note("type", out.mType);
            sgtx::NoteFloats("record", out.mValues, 7);
            NoteTransformState(As(n));
            break;
        }
        case 17: // the visitor's kind-specific slot, noted with the link state
            drawfx::Note("accept", {n->Accept(reinterpret_cast<cICompositionTreeTraversal *>(&w.traversal))});
            break;
        case 18: { // a changed parent transform seen from below: the world transforms of the whole tree
            for (unsigned i = 0; i < ns.count; ++i) NoteTransform("tree world", ns[i]->GetWorldTransform());
            break;
        }
        default: OperateBounded(op, n, rng); break;
        }
    }

    // The bound slots of the explicit-bound level (66-78); a partition gets 66-72 here (its own 73-75 and the rest take
    // shapes and lines the partition tests drive).
    static void OperateBounded(unsigned op, cICompositionTreeNode *n, std::mt19937 &rng)
    {
        if constexpr (kBounded) {
            auto *b = static_cast<cIBoundedNode *>(static_cast<Iface *>(n));
            switch (op) {
            case 19:
                b->SetBoundFlag(rng() % 2);
                drawfx::Note("bound flag", {uint8_t(n->TestFlag(cIBoundedNode::kBoundFlag)), n->GetFlags()});
                break;
            case 20: {
                b->SetHolder(rng() % 2 ? &w.parts.holder : nullptr, uint32_t(rng()));
                drawfx::Note("holder", {drawfx::Norm(P(b->GetHolder())), drawfx::Norm(P(b->GetHolderConst())),
                                        b->GetHolderData(), uint8_t(b->IsEverywhere())});
                const bool bounded = rng() % 2 != 0;
                if constexpr (!(kFixedFlags >> cIBoundedNode::kBoundedFlag & 1)) n->SetFlag(cIBoundedNode::kBoundedFlag, bounded);
                break;
            }
            case 21: {
                NoteBox("box", b->GetBoundBox());
                const cTransform34 frame = sgtx::RandomFrame(rng);
                NoteBox("box in", b->GetBoundBoxIn(&frame));
                NoteTransform("world", n->GetWorldTransform());
                break;
            }
            case 22: {
                auto *lin = reinterpret_cast<nGZCompGeom::cILinear3 *>(&w.parts.linear);
                float t = float(rng() % 100) / 4.0f;
                if (rng() % 2) { // a stale cached transform when the slots run
                    const cRigidTransform moved = sgtx::RandomRigid(rng);
                    As(n)->SetTranslation(&moved.mValues[0]);
                }
                drawfx::Note("73", uint8_t(b->IntersectsBound(rng(), rng())));
                drawfx::Note("74", uint8_t(b->IntersectsLinear(lin)));
                const bool hit = b->FirstHitLinear(&t, lin);
                drawfx::Note("75", {uint8_t(hit), std::bit_cast<uint32_t>(t)});
                break;
            }
            case 23: {
                if constexpr (kExplicit) {
                    auto *e = static_cast<cIExplicitBoundNode *>(n);
                    e->SetShape(rng() % 4 ? reinterpret_cast<nGZCompGeom::cIShape *>(&w.parts.shape) : nullptr);
                    drawfx::Note("shape", {drawfx::Norm(P(e->GetShape())), uint32_t(w.parts.copy.refs),
                                           uint32_t(w.parts.shape.refs)});
                }
                break;
            }
            }
        }
    }

    static void ReleaseAll()
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
        for (const Fake &e : w.ext)
            drawfx::Note("extension", {e.id, uint32_t(e.refs), drawfx::Norm(e.value[0]), drawfx::Norm(e.value[1])});
        drawfx::Note("parts", {uint32_t(w.parts.shape.refs), uint32_t(w.parts.copy.refs), uint32_t(w.parts.holder.refs)});
        sgfx::gPool.NoteCounts();
    }

    // ---- the tests ----
    struct Registries {
        typename detail::ScopeOf<Cfg>::type scope;
        sgfx::RegistryScope node{sgfx::kNodeRegistry};
        sgfx::RegistryScope transform{sgfx::kTransformRegistry};
        RefreshScope refresh;
    };

    static void Factory(difftest::Context &ctx)
    {
        Registries registries;
        sgfx::CheckSides(ctx, "factory", 4, Build, [&](bool original, std::mt19937 &) {
            sgfx::Factory make = Maker(ctx, original);
            cICompositionTreeNode *n = make();
            drawfx::Note("count", CountOf(n));
            n->AddRef();
            w.nodes.at[0] = n;
            w.nodes.count = 1;
            w.nodes.size = kSize;
            drawfx::AddRange(n, kSize, sgfx::kNodeTag);
            NoteNodes("new node");
            w.nodes.NoteShape("new node");
            NoteTransformState(As(n));
            ReleaseAll();
        });
        w.manager.Drop();
    }

    static void Slots(difftest::Context &ctx)
    {
        Registries registries;
        sgfx::CheckSides(ctx, "slots", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
            MakeTree(Maker(ctx, original), rng);
            w.nodes.NoteShape("built");
            NoteNodes("built");
            for (int k = 0; k < 20; ++k) Operate(ctx, k, rng);
            w.nodes.NoteShape("after");
            NoteNodes("after");
            ReleaseAll();
        });
        w.manager.Drop();
    }

    static void Persist(difftest::Context &ctx)
    {
        Registries registries;
        sgfx::CheckSides(ctx, "persist", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
            MakeTree(Maker(ctx, original), rng);
            cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
            if (rng() % 2) {
                const cRigidTransform t = sgtx::RandomRigid(rng);
                As(n)->SetTransform(&t);
            }
            if (rng() % 2) As(n)->SetLimit(uint32_t(rng()));
            cIGZPersistSerializableReferent *face = nullptr;
            n->QueryInterface(0x5b0aaa74, reinterpret_cast<void **>(&face));
            drawfx::Note("face", drawfx::Norm(P(face)));
            if (rng() % 2) n->SetTag("alpha");
            if (rng() % 2) n->AddExtension(reinterpret_cast<cIExtension *>(&w.ext[0]));
            cPersistTypeInfo info = face->GetTypeInfo();
            sgfx::NoteTypeInfo("type", &info);
            if (info.mName) info.mName->Release();
            face->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
            cICompositionTreeNode *fresh = Maker(ctx, original)();
            fresh->AddRef();
            w.nodes.at[w.nodes.count] = fresh;
            drawfx::AddRange(fresh, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
            ++w.nodes.count;
            sgfx::Reader &r = sgfx::gReader;
            // this level's up to the composition part's (a partition node's face reads its own record first)
            r.Version(rng() % 16); // the class's own
            if constexpr (requires { Cfg::ReadResource(r, rng); }) Cfg::ReadResource(r, rng);
            for (unsigned i = 0; i < (kPartition ? 4u : kBounded ? 3u : 1u); ++i) r.Version(rng() % 16);
            r.Version(4);                                                                  // the object-graph part's
            const uint32_t extensions = rng() % 2;
            r.Word(extensions);
            if (extensions) r.Ref(&w.ext[1]);
            r.Text(rng() % 2 ? "beta" : "");
            uint32_t roots = 0;
            for (unsigned i = 0; i + 1 < w.nodes.count; ++i)
                if (!w.nodes[i]->GetParent() && rng() % 2) ++roots;
            r.Word(roots);
            for (unsigned i = 0, k = 0; i + 1 < w.nodes.count && k < roots; ++i)
                if (!w.nodes[i]->GetParent()) r.Ref(w.nodes[i]), ++k;
            for (int i = 0; i < 7; ++i) r.Float(sgtx::RandomFloat(rng));
            r.Word(rng() % 2 ? 0x7fffffffu : uint32_t(rng()));
            if (kBounded) r.Word(rng() % 2); // the bound flag
            cIGZPersistSerializableReferent *freshFace = nullptr;
            fresh->QueryInterface(0xbb11a53e, reinterpret_cast<void **>(&freshFace));
            freshFace->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
            freshFace->Release();
            face->Release();
            w.nodes.NoteShape("after");
            NoteNodes("after");
            NoteTransformState(As(fresh));
            sgfx::gReader.NoteNames();
            ReleaseAll();
        });
        w.manager.Drop();
    }

    static void Clone(difftest::Context &ctx)
    {
        Registries registries;
        sgfx::CheckSides(ctx, "clone", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
            sgfx::Factory make = Maker(ctx, original);
            MakeTree(make, rng);
            cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
            if constexpr (requires { Cfg::CloneSource(n); }) n = Cfg::CloneSource(n);
            if (rng() % 2) n->SetTag("beta");
            if (rng() % 2) n->AddExtension(reinterpret_cast<cIExtension *>(&w.ext[0]));
            if (rng() % 2) {
                const cRigidTransform t = sgtx::RandomRigid(rng);
                As(n)->SetTransform(&t);
            }
            if (rng() % 2) As(n)->SetLimit(uint32_t(rng()));
            if constexpr (kExplicit) {
                auto *b = static_cast<cIExplicitBoundNode *>(n);
                if (rng() % 2) b->SetShape(reinterpret_cast<nGZCompGeom::cIShape *>(&w.parts.shape));
                if (rng() % 2) b->SetHolder(nullptr, uint32_t(rng()));
            }
            const uint32_t mode = rng() % 3;
            cINodeCloneContext *context = nullptr;
            if (rng() % 3 == 0 && mode != 1) {
                cICompositionTreeNode *prepared = make();
                prepared->AddRef();
                w.nodes.at[w.nodes.count] = prepared;
                drawfx::AddRange(prepared, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
                ++w.nodes.count;
                sgfx::MakeCloneContext(w.context, prepared);
                context = reinterpret_cast<cINodeCloneContext *>(&w.context);
            }
            const uint32_t iids[] = {sgfx::kIidTree, sgfx::kIidGraph, Cfg::kIid, 0x12345678};
            void *out = nullptr;
            n->Clone(mode, iids[rng() % 4], &out, context);
            const bool isNew = out && drawfx::Norm(P(out)) == P(out);
            if (isNew) {
                drawfx::AddRange(out, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
                w.nodes.at[w.nodes.count++] = static_cast<cICompositionTreeNode *>(out);
            }
            drawfx::Note("clone", {out != nullptr, uint8_t(isNew), drawfx::Norm(P(out))});
            if (out && !isNew) static_cast<cICompositionTreeNode *>(out)->Release();
            if (out) HoldSubtree(static_cast<cICompositionTreeNode *>(out));
            for (unsigned i = 0; i < w.nodes.count; ++i) NoteTransformState(As(w.nodes[i]));
            w.nodes.NoteShape("after");
            NoteNodes("after");
            ReleaseAll();
        });
        w.manager.Drop();
    }

    static void Events(difftest::Context &ctx)
    {
        Registries registries;
        static Fake listeners[2];
        sgfx::Manager &manager = w.manager;
        auto build = [&](uint32_t seed, drawfx::Cells &cells) {
            Build(seed, cells);
            sgfx::MakeListener(listeners[0], 500, true);
            sgfx::MakeListener(listeners[1], 501, seed % 2 != 0);
        };
        sgfx::CheckSides(ctx, "events", 48, build, [&](bool original, std::mt19937 &rng) {
            MakeTree(Maker(ctx, original), rng);
            const uint32_t events[] = {cGraphConnChanged::kEventId, 0x1234};
            for (int k = 0; k < 12; ++k) {
                cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
                Fake &l = listeners[rng() % 2];
                const uint32_t event = events[rng() % 2];
                const unsigned op = rng() % 5;
                drawfx::Note("op", op);
                switch (op) {
                case 0: n->Connect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2)); break;
                case 1: n->Disconnect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2)); break;
                case 2: {
                    cGraphConnChanged body(rng() % 2, n, w.nodes[rng() % w.nodes.count],
                                           rng() % 2 ? sgfx::kIidTree : sgfx::kIidGraph);
                    n->NotifyListeners(event, rng(), &body);
                    break;
                }
                case 3: drawfx::Note("asked", uint8_t(n->AskListeners(event, rng(), nullptr))); break;
                case 4: drawfx::Note("active", {n->IsActive(), CountOf(n)}); break;
                }
            }
            w.nodes.NoteShape("after");
            NoteNodes("after");
            ReleaseAll();
            drawfx::Note("connected", {uint8_t(manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[0]))),
                                       uint8_t(manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[1])))});
        });
        w.manager.Drop();
    }
};

} // namespace sgtp
