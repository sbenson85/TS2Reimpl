// Differential tests of nGZSceneGraph's tree code (lane OM, scenegraph N0 part 3 step 4; src/sg/SGTreeCode.h): the
// game's functions (each reaching the game's own propagation root 0xa39620 and recursions 0xa39740 / 0xa39820 /
// 0xa39920) against ours, on a random tree of node fakes.
//
//   SG_TreeCode_Connected         NodeConnected 0xa396d0
//   SG_TreeCode_Disconnecting     NodeDisconnecting 0xa39710
//   SG_TreeCode_TransformChanged  NodeTransformChanged 0xa39690
//   SG_TreeCode_BoundFlagChanged  NodeBoundFlagChanged 0xa39ae0
//   SG_TreeCode_Mixed             FindPropagationRoot 0xa39620 (its answer noted) between calls of the four above
//   SG_TreeCode_LimitRoot         FindLimitRoot 0xae7aa0 (lane VS; the const walk: slot 33), on random nodes and null
//   SG_TreeCode_GatherLimits      GatherLimitTransforms 0xae7910 (lane VS; slots 41 / 37 / 55 / 57) into a table of
//                                 0-8 transforms, limits up to 9 so some fall outside it; the table noted afterwards
//
// Each seed builds 1-10 node fakes in a tree (each a child of an earlier one, appended to its children), each with
// random flags (15 bits: the bounded, partition, root-stop, stale and queued bits among them), a limit (0, 0x7fffffff
// or small), a scene (slot 32: one of two scene fakes or none) and a holder (slot 69: one of two holder fakes or
// none), then calls the function under test on 1-4 random nodes. The log holds every slot call in order (SetFlag,
// GetChildren, the scenes' QueueNode / UnqueueNode, the holders' RemovePartition / RemoveNode, Clear, the events with
// their body's vtable word); each node's flags are noted at the end.
#include "sgfx.h"

#include "sg/SGNodeInterfaces.h"
#include "sg/SGTreeCode.h"

#include <array>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

// The node fakes' table: kNode's slots 0-60, then the slots the tree code calls on bounded nodes (69), partitions
// (77) and holders (78 / 79), and scenes (95 / 96); the rest unsettled. One table for nodes, scenes and holders.
constexpr unsigned kTreeSlotCount = 97;
constexpr std::array<Slot, kTreeSlotCount> TreeSlots()
{
    std::array<Slot, kTreeSlotCount> r{};
    for (unsigned i = 0; i < 61; ++i) r[i] = sgfx::kNodeSlots[i];
    for (unsigned i = 61; i < kTreeSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[5] = {3, Ret::Int, true, "NotifyListeners", "uuF1"}; // the event body: its vtable word
    r[69] = {0, Ret::Int, true, "GetHolder", ""};
    r[77] = {0, Ret::Int, true, "Clear", ""};
    r[78] = {1, Ret::Int, true, "RemovePartition", "i"};
    r[79] = {1, Ret::Int, true, "RemoveNode", "i"};
    r[95] = {1, Ret::Int, true, "QueueNode", "i"};
    r[96] = {1, Ret::Int, true, "UnqueueNode", "i"};
    return r;
}
constexpr auto kTreeSlots = TreeSlots();
constexpr drawfx::Iface kTreeNode{"TreeNode", kTreeSlotCount, kTreeSlots.data()};

enum : unsigned { kFlags, kParent, kFirstChild, kNext, kScene, kLimit, kHolder, kLastChild };
constexpr unsigned kMaxNodes = 10;

struct World {
    Fake nodes[kMaxNodes];
    Fake scenes[2];
    Fake holders[2];
    unsigned count = 0;
} gw;

uint32_t SetFlag(Fake &f, const uint32_t *a)
{
    const uint32_t bit = 1u << (a[0] & 31);
    if (a[1] & 0xff)
        f.value[kFlags] |= bit;
    else
        f.value[kFlags] &= ~bit;
    return 0;
}
uint32_t TestFlag(Fake &f, const uint32_t *a) { return (f.value[kFlags] >> (a[0] & 31)) & 1; }
template <unsigned W> uint32_t Word(Fake &f, const uint32_t *) { return f.value[W]; }
// Slot 42 (hidden out): the cursor range {first, first, 0}.
uint32_t Children(Fake &f, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = out[1] = f.value[kFirstChild];
    out[2] = 0;
    return a[0];
}
// Slot 57 (out, in): a transform made from `in` and the node's id, different for each node and each input.
uint32_t ToParent(Fake &f, const uint32_t *a)
{
    float *const out = reinterpret_cast<float *>(uintptr_t(a[0]));
    const float *const in = reinterpret_cast<const float *>(uintptr_t(a[1]));
    for (unsigned k = 0; k < 7; ++k) out[k] = in[k] * 0.5f + float(f.id % 97) * float(k + 1);
    return 0;
}

void MakeTreeNode(Fake &f, uint32_t id)
{
    f.Reset<kTreeNode>(id);
    f.On(29, &SetFlag);
    f.On(32, &Word<kScene>);
    f.On(33, &Word<kParent>);
    f.On(34, &Word<kParent>);
    f.On(37, &Word<kNext>);
    f.On(38, &Word<kNext>);
    f.On(41, &Children);
    f.On(42, &Children);
    f.On(48, &TestFlag);
    f.On(49, &Word<kFlags>);
    f.On(55, &Word<kLimit>);
    f.On(57, &ToParent);
    f.On(69, &Word<kHolder>);
}

void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 7u);
    for (unsigned i = 0; i < 2; ++i) {
        gw.scenes[i].Reset<kTreeNode>(90 + i);
        gw.holders[i].Reset<kTreeNode>(95 + i);
    }
    gw.count = 1 + rng() % kMaxNodes;
    for (unsigned i = 0; i < gw.count; ++i) {
        Fake &n = gw.nodes[i];
        MakeTreeNode(n, 100 + i);
        n.value[kFlags] = rng() & 0x7fff;
        switch (rng() % 4) {
        case 0: n.value[kLimit] = 0; break;
        case 1: n.value[kLimit] = 0x7fffffff; break;
        default: n.value[kLimit] = 1 + rng() % 5; break;
        }
        const uint32_t scene = rng() % 3, holder = rng() % 3;
        n.value[kScene] = scene ? drawfx::P(&gw.scenes[scene - 1]) : 0;
        n.value[kHolder] = holder ? drawfx::P(&gw.holders[holder - 1]) : 0;
        if (i == 0) continue;
        Fake &parent = gw.nodes[rng() % i];
        n.value[kParent] = drawfx::P(&parent);
        if (parent.value[kLastChild])
            reinterpret_cast<Fake *>(uintptr_t(parent.value[kLastChild]))->value[kNext] = drawfx::P(&n);
        else
            parent.value[kFirstChild] = drawfx::P(&n);
        parent.value[kLastChild] = drawfx::P(&n);
    }
}

void NoteFlags()
{
    for (unsigned i = 0; i < gw.count; ++i) drawfx::Note("flags", {gw.nodes[i].id, gw.nodes[i].value[kFlags]});
}

cICompositionTreeNode *AsNode(Fake &f) { return reinterpret_cast<cICompositionTreeNode *>(&f); }

using EntryFn = void(__cdecl *)(void *);
using Ours = void (*)(cICompositionTreeNode *);
constexpr unsigned kSeeds = 64;

void Check(difftest::Context &ctx, const char *what, uint32_t rva, Ours ours)
{
    const EntryFn original = ctx.Original<EntryFn>(rva);
    sgfx::CheckSides(ctx, what, kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         const unsigned ops = 1 + rng() % 4;
                         for (unsigned k = 0; k < ops; ++k) {
                             Fake &node = gw.nodes[rng() % gw.count];
                             drawfx::Note("call", node.id);
                             if (isOriginal)
                                 original(&node);
                             else
                                 ours(AsNode(node));
                         }
                         NoteFlags();
                     });
}

} // namespace

DIFF_TEST(SG_TreeCode_Connected, 0xa396d0) { Check(ctx, "NodeConnected", 0xa396d0, &NodeConnected); }

DIFF_TEST(SG_TreeCode_Disconnecting, 0xa39710) { Check(ctx, "NodeDisconnecting", 0xa39710, &NodeDisconnecting); }

DIFF_TEST(SG_TreeCode_TransformChanged, 0xa39690)
{
    Check(ctx, "NodeTransformChanged", 0xa39690, &NodeTransformChanged);
}

DIFF_TEST(SG_TreeCode_BoundFlagChanged, 0xa39ae0)
{
    Check(ctx, "NodeBoundFlagChanged", 0xa39ae0, &NodeBoundFlagChanged);
}

DIFF_TEST(SG_TreeCode_Mixed, 0xa39620)
{
    using RootFn = void *(__cdecl *)(void *);
    const RootFn root = ctx.Original<RootFn>(0xa39620);
    const EntryFn entries[] = {ctx.Original<EntryFn>(0xa396d0), ctx.Original<EntryFn>(0xa39710),
                               ctx.Original<EntryFn>(0xa39690), ctx.Original<EntryFn>(0xa39ae0)};
    const Ours ours[] = {&NodeConnected, &NodeDisconnecting, &NodeTransformChanged, &NodeBoundFlagChanged};
    sgfx::CheckSides(ctx, "Mixed", kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         const unsigned ops = 2 + rng() % 6;
                         for (unsigned k = 0; k < ops; ++k) {
                             Fake &node = gw.nodes[rng() % gw.count];
                             const unsigned which = rng() % 5;
                             drawfx::Note("call", {which, node.id});
                             if (which == 4) {
                                 const void *found = isOriginal ? root(&node) : FindPropagationRoot(AsNode(node));
                                 drawfx::Note("root", drawfx::Norm(drawfx::P(found)));
                             } else if (isOriginal) {
                                 entries[which](&node);
                             } else {
                                 ours[which](AsNode(node));
                             }
                         }
                         NoteFlags();
                     });
}

DIFF_TEST(SG_TreeCode_LimitRoot, 0xae7aa0)
{
    using RootFn = void *(__cdecl *)(void *);
    const RootFn original = ctx.Original<RootFn>(0xae7aa0);
    sgfx::CheckSides(ctx, "FindLimitRoot", kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         const unsigned ops = 1 + rng() % 5;
                         for (unsigned k = 0; k < ops; ++k) {
                             Fake *const node = rng() % 6 ? &gw.nodes[rng() % gw.count] : nullptr;
                             drawfx::Note("call", node ? node->id : 0);
                             const void *found = isOriginal ? original(node) : FindLimitRoot(node ? AsNode(*node) : nullptr);
                             drawfx::Note("root", drawfx::Norm(drawfx::P(found)));
                         }
                     });
}

DIFF_TEST(SG_TreeCode_GatherLimits, 0xae7910)
{
    using GatherFn = void(__cdecl *)(void *, uint32_t, void *, const void *);
    const GatherFn original = ctx.Original<GatherFn>(0xae7910);
    sgfx::CheckSides(ctx, "GatherLimitTransforms", kSeeds,
                     [](uint32_t seed, drawfx::Cells &) {
                         Build(seed);
                         std::mt19937 rng(seed ^ 0xae7910u);
                         for (unsigned i = 0; i < gw.count; ++i)
                             if (rng() % 3 == 0) gw.nodes[i].value[kLimit] = 6 + rng() % 4; // 6-9: some past the table
                     },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         constexpr unsigned kTable = 9; // eight usable, one guard
                         cRigidTransform table[kTable];
                         for (unsigned i = 0; i < kTable; ++i)
                             for (unsigned k = 0; k < 7; ++k) table[i].mValues[k] = -1.0f - float(i * 7 + k);
                         const uint32_t count = rng() % 9;
                         cRigidTransform start;
                         for (unsigned k = 0; k < 7; ++k) start.mValues[k] = float(int(rng() % 200) - 100) * 0.25f;
                         Fake &node = gw.nodes[rng() % gw.count];
                         drawfx::Note("call", {node.id, count});
                         if (isOriginal)
                             original(table, count, &node, &start);
                         else
                             GatherLimitTransforms(table, count, AsNode(node), &start);
                         for (unsigned i = 0; i < kTable; ++i)
                             for (unsigned k = 0; k < 7; ++k) drawfx::NoteFloat("table", table[i].mValues[k]);
                         for (unsigned k = 0; k < 7; ++k) drawfx::NoteFloat("start", start.mValues[k]);
                     });
}
