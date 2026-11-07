// Differential tests of nGZSceneGraph::cFindBonesTraversal (lane QR, scenegraph D3 part 2; src/sg/cFindBonesTraversal.h).
//
// Each side's traversal comes from its own factory: the game's 0xa95d30 (new 0x1c, the constructor 0xa95c10) and ours
// Create; each is driven through its own vtable (24 slots). The nodes are node fakes (tests/sgfx.h kNode) in a random tree.
//
//   FindBonesTraversal_Slots  random calls on the traversal: the filter and the flag (set, get), Clear, the traversal
//                             slots 3-15 on random nodes (slot 3 Accept through the node, 4 the children visit, 5 the
//                             collecting visit, 6-15 the forwarders), At / Count, QueryInterface over the iids, extra
//                             AddRef / Release pairs; the list dumped every few calls. A node's Accept (slot 30) calls
//                             back into the traversal by the node's kind (slots 4, 5, 12, 13, 14, 15), so the visits
//                             recurse through the real tree; the children ranges (slot 42) are {first, first, end}
//                             with the end null or a later sibling. The log holds every node call (GetTag,
//                             GetChildren, GetNextSibling, TestFlag, Accept, AddRef, Release) in order.
//
// RVAs these tests reach (status entries cite them): the factory 0xa95d30, the constructor 0xa95c10 (and the template's
// 0xa95c00), the destructor 0xa95c60 (the template's 0xa95c50) and the deleting destructor 0xa95ca0, QueryInterface
// 0xa95d90 / 0xa95d60, the shared slot 3 body 0xa95eb0, Visit 0xa95dd0, VisitTransform 0xa95ec0, Clear 0xa95cc0, the
// forwarders 0x2b5b00 / 0x1a9f00 / 0x2b5b10 / 0x1ae2f0 / 0x2b5d40 / 0x2b5d50 and the accessors 0x148fb0 / 0x356910 /
// 0x14a260 / 0x1f0e90 / 0x46390 / 0x1b7090 / 0x808d30 / 0x806c60.
#include "difftest.h"
#include "sgfx.h"
#include "test_helpers.h"

#include "sg/cFindBonesTraversal.h"

#include <cstdint>
#include <cstdio>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kFactory = 0xa95d30;
constexpr uint32_t kObjectTag = 0x7c000000;
using FactoryFn = cIGZUnknown *(__cdecl *)();

enum : unsigned { kFlags, kNext, kFirstChild, kEnd, kTag, kKind };
constexpr unsigned kMaxNodes = 10;
const char kTagText[] = "bone";

Fake gNodes[kMaxNodes];
unsigned gCount;
difftest::Context *gCtx;

uint32_t TestFlag(Fake &f, const uint32_t *a) { return (f.value[kFlags] >> (a[0] & 31)) & 1; }
template <unsigned W> uint32_t Word(Fake &f, const uint32_t *) { return f.value[W]; }
// Slot 42 (hidden out): {first, first, end}.
uint32_t Children(Fake &f, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = out[1] = f.value[kFirstChild];
    out[2] = f.value[kEnd];
    return a[0];
}
// Slot 30: the node's kind says which visit it calls back.
uint32_t Accept(Fake &f, const uint32_t *a)
{
    auto *const t = reinterpret_cast<cICompositionTreeTraversal *>(uintptr_t(a[0]));
    auto *const node = reinterpret_cast<cICompositionTreeNode *>(&f);
    switch (f.value[kKind]) {
    case 0: return t->Visit(node);
    case 1: return t->VisitTransform(node);
    case 2: return t->VisitResource(node);
    case 3: return t->VisitPartition(node);
    case 4: return t->VisitScene(node);
    default: return t->VisitExplicitBound(node);
    }
}

void Build(uint32_t seed, drawfx::Cells &)
{
    std::mt19937 rng(seed * 2654435761u + 11u);
    gCount = 1 + rng() % kMaxNodes;
    uint32_t lastChild[kMaxNodes] = {};
    for (unsigned i = 0; i < gCount; ++i) {
        Fake &n = gNodes[i];
        sgfx::MakeNode(n, 100 + i);
        n.On(30, &Accept);
        n.On(38, &Word<kNext>);
        n.On(26, &Word<kTag>);
        n.On(42, &Children);
        n.On(48, &TestFlag);
        n.value[kFlags] = rng() & 0x7fff;
        n.value[kTag] = rng() % 3 ? P(kTagText) : 0;
        n.value[kKind] = rng() % 7 < 3 ? 1 : rng() % 6; // mostly transform nodes
        n.value[kNext] = n.value[kFirstChild] = n.value[kEnd] = 0;
        if (i == 0) continue;
        Fake &parent = gNodes[rng() % i];
        if (lastChild[&parent - gNodes])
            gNodes[lastChild[&parent - gNodes] - 1].value[kNext] = P(&n);
        else
            parent.value[kFirstChild] = P(&n);
        lastChild[&parent - gNodes] = i + 1;
    }
    // A range's end: a later sibling now and then (the visit stops before it), null otherwise.
    for (unsigned i = 0; i < gCount; ++i) {
        uint32_t at = gNodes[i].value[kFirstChild];
        if (!at || rng() % 3) continue;
        for (unsigned skip = rng() % 3; skip && reinterpret_cast<Fake *>(uintptr_t(at))->value[kNext]; --skip)
            at = reinterpret_cast<Fake *>(uintptr_t(at))->value[kNext];
        gNodes[i].value[kEnd] = at;
    }
}

cICompositionTreeNode *AsNode(Fake &f) { return reinterpret_cast<cICompositionTreeNode *>(&f); }
Fake &RandomNode(std::mt19937 &rng) { return gNodes[rng() % gCount]; }

void Dump(cIFindBonesTraversal *t)
{
    const uint32_t count = t->Count();
    drawfx::Note("list", {count, uint32_t(t->GetFilter()), uint32_t(t->GetCollectUntagged())});
    for (uint32_t i = 0; i < count; ++i) drawfx::Note("at", drawfx::Norm(P(t->At(i))));
}

void NoteNodes()
{
    for (unsigned i = 0; i < gCount; ++i) drawfx::Note("node refs", {gNodes[i].id, uint32_t(gNodes[i].refs)});
}

const uint32_t kIids[] = {cIFindBonesTraversal::kIid, 1, 0xc51851eb, 0x5ca10ca7, 0, 0x12345678, 0xfb859324, 0x4512dd13};
const int32_t kFilters[] = {-1, -1, 0, 1, 2, 3, 5, 7, 14, 31, int32_t(0x80000000), -5};

void Operate(cIFindBonesTraversal *t, unsigned op, std::mt19937 &rng)
{
    switch (op) {
    case 0: t->SetFilter(kFilters[rng() % 12]); break;
    case 1: t->SetCollectUntagged(rng() % 2); break;
    case 2:
        if (rng() % 3 == 0) t->Clear();
        break;
    case 3: drawfx::Note("traverse", drawfx::Norm(t->Traverse(AsNode(RandomNode(rng))))); break;
    case 4: drawfx::Note("visit", drawfx::Norm(t->Visit(AsNode(RandomNode(rng))))); break;
    case 5: drawfx::Note("visit transform", drawfx::Norm(t->VisitTransform(AsNode(RandomNode(rng))))); break;
    case 6: {
        cICompositionTreeNode *const n = AsNode(RandomNode(rng));
        const unsigned slot = rng() % 10;
        drawfx::Note("forward", slot);
        uint32_t r = 0;
        switch (slot) {
        case 0: r = t->TraversalSlot6(n); break;
        case 1: r = t->TraversalSlot7(n); break;
        case 2: r = t->TraversalSlot8(n); break;
        case 3: r = t->TraversalSlot9(n); break;
        case 4: r = t->TraversalSlot10(n); break;
        case 5: r = t->TraversalSlot11(n); break;
        case 6: r = t->VisitResource(n); break;
        case 7: r = t->VisitPartition(n); break;
        case 8: r = t->VisitScene(n); break;
        default: r = t->VisitExplicitBound(n); break;
        }
        drawfx::Note("forward result", drawfx::Norm(r));
        break;
    }
    case 7: Dump(t); break;
    case 8: {
        const uint32_t count = t->Count();
        drawfx::Note("count", count);
        if (count) drawfx::Note("at", drawfx::Norm(P(t->At(rng() % count))));
        break;
    }
    case 9: {
        const uint32_t iid = kIids[rng() % 8];
        void *out = nullptr;
        const bool r = t->QueryInterface(iid, rng() % 8 ? &out : nullptr);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out))});
        if (out) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    default:
        drawfx::Note("add ref", t->AddRef());
        drawfx::Note("release", t->Release());
        break;
    }
}

} // namespace

// Random calls on each side's traversal over a random tree of node fakes.
DIFF_TEST(FindBonesTraversal_Slots, 0xa95d30)
{
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "slots", 96, Build, [&](bool original, std::mt19937 &rng) {
        cIGZUnknown *const made = original ? ctx.Original<FactoryFn>(kFactory)() : cFindBonesTraversal::Create();
        auto *const t = static_cast<cIFindBonesTraversal *>(made);
        drawfx::AddRange(reinterpret_cast<uint8_t *>(t), 0x1c, kObjectTag);
        drawfx::Note("first count", t->AddRef());
        Dump(t);
        const unsigned ops = 20 + rng() % 40;
        for (unsigned k = 0; k < ops; ++k) {
            const unsigned op = rng() % 11;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof(step), "step %u, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            Operate(t, op, rng);
            if (k % 8 == 7) Dump(t);
        }
        Dump(t);
        NoteNodes();
        drawfx::Note("last release", t->Release()); // the deleting destructor releases the collected nodes
        NoteNodes();
    });
}
