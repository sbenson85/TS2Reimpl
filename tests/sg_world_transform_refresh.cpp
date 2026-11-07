// Differential test of nGZSceneGraph's world-transform refresh (lane OP; src/sg/SGWorldTransformRefresh.h): the game's
// 0xa39b70 (with its recursion 0xa39a10 and the list appends 0xa39500 / 0xa39590 / 0xa39430, all reached through it)
// against ours, on a random tree of node fakes.
//
//   SG_WorldTransformRefresh  0xa39b70, 0xa39a10, 0xa39500, 0xa39590, 0xa39430
//
// Each seed builds 1-40 flag node fakes (sgfx::MakeFlagNode) in a tree (each a child of an earlier one, appended to its
// children), each with random flags (the stale bit 12 and the content bit 4 among them), a limit (0, 0x7fffffff or
// small), a children range whose end is 0 or one of its children (slot 41), a transform to the parent (slot 57: every
// float written, from the parent's floats and the node's id) and a world transform (slot 54, sgfx's per-id floats). Then
// the refresh runs on 1-3 random nodes. Some seeds pick a node whose slot 60 calls the refresh again (once) on another
// node, so the work lists are shared with a refresh still running. The log holds every slot call in order: slot 60 with
// the 7 floats its first argument points at, the limit records (8 words each, as many as its third argument says) and
// that count; slot 57 with the floats it is handed.
//
// The original's work lists are its function-local static (0x10f5fac-0x10f5fe8): the pilots' RefreshScope stands them up
// (50 limit records in the scope's memory; a root lists at most one limit per node of the tree, and a seed that
// re-enters has at most 24 nodes, so the original never reallocates that buffer: it would free the scope's memory to the
// game's heap). The content and root lists it grows from the game's heap.
#include "sg_transform_pilot.h"

#include "sg/SGNodeInterfaces.h"
#include "sg/SGWorldTransformRefresh.h"

#include <array>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Slot;

// kNode's table, with slot 57 logging the floats it is handed (the out is the caller's), and slot 60 the transform its
// first argument points at, then the limit records (the count is the third argument), then the count.
constexpr std::array<Slot, 62> RefreshSlots()
{
    std::array<Slot, 62> r{};
    for (unsigned i = 0; i < 62; ++i) r[i] = sgfx::kNodeSlots[i];
    r[57].args = "iF7";
    r[60].args = "F7@2x8u";
    return r;
}
constexpr auto kRefreshSlots = RefreshSlots();
constexpr drawfx::Iface kRefreshNode{"RefreshNode", 62, kRefreshSlots.data()};

constexpr unsigned kMaxNodes = 40;
constexpr unsigned kEndWord = sgfx::kReferentWord;        // slot 41's end: 0 or one of the node's children
constexpr unsigned kReenterWord = sgfx::kFirstExtensionWord; // slot 60 refreshes this node first (once), or 0

using EntryFn = void(__cdecl *)(void *);

struct World {
    Fake nodes[kMaxNodes];
    unsigned count = 0;
    EntryFn refresh = nullptr; // this side's
    bool reentered = false;
} gw;

uint32_t Children(Fake &f, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = out[1] = f.value[sgfx::kFirstChildWord];
    out[2] = f.value[kEndWord];
    return a[0];
}

// Slot 57 (out, in): every float written, from in's and the node's id.
uint32_t ToParent(Fake &f, const uint32_t *a)
{
    float *const out = reinterpret_cast<float *>(uintptr_t(a[0]));
    const float *const in = reinterpret_cast<const float *>(uintptr_t(a[1]));
    for (unsigned i = 0; i < 7; ++i) out[i] = in[i] * 0.5f + float(f.id % 7) + float(i) / 8.0f;
    return 0;
}

uint32_t ContentChanged(Fake &f, const uint32_t *)
{
    if (f.value[kReenterWord] && !gw.reentered) {
        gw.reentered = true;
        drawfx::Note("reenter", {f.id, reinterpret_cast<Fake *>(uintptr_t(f.value[kReenterWord]))->id});
        gw.refresh(reinterpret_cast<void *>(uintptr_t(f.value[kReenterWord])));
    }
    return 0;
}

void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 11u);
    // A refresh that runs again from a slot 60 lists more limits before the first one empties its list: at most 24 nodes
    // then (24 + 24 limit records, within the 50 the scope's buffer holds).
    const bool reenter = rng() % 4 == 0;
    gw.count = 1 + rng() % (reenter ? 24 : kMaxNodes);
    gw.reentered = false;
    uint32_t lastChild[kMaxNodes] = {};
    for (unsigned i = 0; i < gw.count; ++i) {
        Fake &n = gw.nodes[i];
        sgfx::MakeFlagNode<kRefreshNode>(n, 100 + i);
        n.On(41, &Children);
        n.On(57, &ToParent);
        n.On(60, &ContentChanged);
        // Mostly the stale and content bits, sometimes anything.
        n.value[sgfx::kFlagsWord] = rng() % 4 ? (rng() & (1u << 12 | 1u << 4)) : rng() & 0x7fff;
        switch (rng() % 4) {
        case 0: n.value[sgfx::kLimitWord] = 0; break;
        case 1: n.value[sgfx::kLimitWord] = 0x7fffffff; break;
        default: n.value[sgfx::kLimitWord] = 1 + rng() % 5; break;
        }
        if (i == 0) continue;
        const unsigned parent = (rng() % 3 == 0) ? i - 1 : rng() % i; // some deep chains
        Fake &p = gw.nodes[parent];
        n.value[sgfx::kParentWord] = drawfx::P(&p);
        if (lastChild[parent])
            reinterpret_cast<Fake *>(uintptr_t(lastChild[parent]))->value[sgfx::kNextWord] = drawfx::P(&n);
        else
            p.value[sgfx::kFirstChildWord] = drawfx::P(&n);
        lastChild[parent] = drawfx::P(&n);
    }
    // A range that ends early: at one of the node's children.
    for (unsigned i = 0; i < gw.count; ++i) {
        Fake &n = gw.nodes[i];
        if (!n.value[sgfx::kFirstChildWord] || rng() % 5) continue;
        unsigned children = 0;
        for (uint32_t c = n.value[sgfx::kFirstChildWord]; c; c = reinterpret_cast<Fake *>(uintptr_t(c))->value[sgfx::kNextWord])
            ++children;
        uint32_t end = n.value[sgfx::kFirstChildWord];
        for (unsigned k = rng() % children; k; --k) end = reinterpret_cast<Fake *>(uintptr_t(end))->value[sgfx::kNextWord];
        n.value[kEndWord] = end;
    }
    if (reenter) gw.nodes[rng() % gw.count].value[kReenterWord] = drawfx::P(&gw.nodes[rng() % gw.count]);
}

void __cdecl Ours(void *node) { RefreshWorldTransforms(static_cast<cICompositionTreeNode *>(node)); }

} // namespace

DIFF_TEST(SG_WorldTransformRefresh, 0xa39b70)
{
    sgtp::RefreshScope statics;
    const EntryFn original = ctx.Original<EntryFn>(0xa39b70);
    sgfx::CheckSides(ctx, "RefreshWorldTransforms", 160, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         gw.refresh = isOriginal ? original : &Ours;
                         const unsigned calls = 1 + rng() % 3;
                         for (unsigned k = 0; k < calls; ++k) {
                             Fake &node = gw.nodes[rng() % gw.count];
                             drawfx::Note("call", node.id);
                             gw.refresh(&node);
                         }
                     });
}
