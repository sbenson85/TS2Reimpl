// Differential tests of nGZSceneGraph::cCompositionTreeNodeImpl (lane NZ, scenegraph N0). Each member runs on a
// 0x1c-byte Impl block (the object-graph part, then parent, previous and next sibling, first child, flags) in a world of
// sgfx fakes: the outer node, five child nodes (state-backed links: SetLink writes them, the getters read them), the
// block's own parent and siblings, an extension, the string pool, a persist reader and writer. The original runs at its
// RVA, ours as the member; the calls (notifications and their bodies included), the block, every fake's links and counts
// and the pool's counts are compared.
#include "difftest.h"
#include "sgfx.h"

#include "sg/cCompositionTreeNodeImpl.h"

#include <algorithm>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr unsigned kChildren = 5; // ids 110-114
constexpr uint32_t kBlockTag = 0x7b000000;
constexpr unsigned kSeeds = 96;

struct World {
    Fake outer;
    Fake child[kChildren];
    Fake parent, prev, next; // the block's own links (ids 120-122)
    Fake ext;                // id 200
    alignas(8) uint32_t block[7];
    uint32_t list[kChildren];
    unsigned length;
};
World w;

cCompositionTreeNodeImpl *Impl() { return reinterpret_cast<cCompositionTreeNodeImpl *>(w.block); }
cICompositionTreeNode *Node(Fake &f) { return reinterpret_cast<cICompositionTreeNode *>(&f); }

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gPool.Reset(cells);
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    sgfx::MakeNode(w.outer, 100);
    for (unsigned i = 0; i < kChildren; ++i) sgfx::MakeNode(w.child[i], 110 + i);
    sgfx::MakeNode(w.parent, 120);
    sgfx::MakeNode(w.prev, 121);
    sgfx::MakeNode(w.next, 122);
    sgfx::MakeExtension(w.ext, 200);
    // The children: a random selection in random order, linked as a node's children are.
    unsigned order[kChildren];
    for (unsigned i = 0; i < kChildren; ++i) order[i] = i;
    std::shuffle(order, order + kChildren, rng);
    w.length = rng() % (kChildren + 1);
    for (unsigned i = 0; i < w.length; ++i) w.list[i] = order[i];
    for (unsigned i = 0; i < w.length; ++i) {
        Fake &c = w.child[w.list[i]];
        c.value[sgfx::kParentWord] = P(&w.outer);
        c.value[sgfx::kPrevWord] = i ? P(&w.child[w.list[i - 1]]) : 0;
        c.value[sgfx::kNextWord] = i + 1 < w.length ? P(&w.child[w.list[i + 1]]) : 0;
    }
    w.block[0] = rng() % 2 ? P(&w.ext) : 0;
    w.block[1] = sgfx::kNoHandle;
    if (rng() % 2) {
        w.block[1] = sgfx::gPool.Handle("alpha");
        ++sgfx::gPool.counts[0];
    }
    w.block[2] = rng() % 2 ? P(&w.parent) : 0;
    w.block[3] = rng() % 2 ? P(&w.prev) : 0;
    w.block[4] = rng() % 2 ? P(&w.next) : 0;
    w.block[5] = w.length ? P(&w.child[w.list[0]]) : 0;
    w.block[6] = uint32_t(rng());
    w.outer.value[sgfx::kFirstChildWord] = w.block[5];
    drawfx::AddRange(w.block, sizeof(w.block), kBlockTag);
}

void NoteWorld()
{
    drawfx::Call c;
    c.note = "impl";
    drawfx::Words(c.words, w.block, 7, "auaaaau"); // extension, handle, links, flags (lane RK: by kind)
    drawfx::gLog.push_back(std::move(c));
    Fake *all[] = {&w.outer, &w.child[0], &w.child[1], &w.child[2], &w.child[3], &w.child[4],
                   &w.parent, &w.prev,     &w.next,     &w.ext};
    for (Fake *f : all)
        drawfx::Note("fake", {f->id, uint32_t(f->refs), drawfx::Ptr(f->value[0]), drawfx::Ptr(f->value[1]),
                              f == &w.ext ? f->value[2] : drawfx::Ptr(f->value[2])}); // (an extension's +2: persistent)
    sgfx::gPool.NoteCounts();
    sgfx::gReader.NoteNames();
}

template <class Run> void Check(difftest::Context &ctx, const char *what, Run run, unsigned seeds = kSeeds)
{
    sgfx::CheckSides(ctx, what, seeds, Build, [&](bool original, std::mt19937 &rng) {
        run(original, rng);
        NoteWorld();
    });
}

Fake &AnyChild(std::mt19937 &rng) { return w.child[rng() % kChildren]; }

} // namespace

DIFF_TEST(SG_CompositionTreeNodeImpl_Construct, 0xa36920)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xa36920);
    Check(
        ctx, "constructor",
        [&](bool original, std::mt19937 &) {
            for (uint32_t &word : w.block) word = 0xcdcdcdcd;
            void *r = original ? orig(w.block) : Impl()->Construct();
            drawfx::Note("returns the block", r == w.block);
        },
        4);
}

DIFF_TEST(SG_CompositionTreeNodeImpl_Destruct, 0xa36bd0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xa36bd0);
    Check(ctx, "destructor", [&](bool original, std::mt19937 &) {
        if (original)
            orig(w.block);
        else
            Impl()->Destruct();
    });
}

DIFF_TEST(SG_CompositionTreeNodeImpl_AddChild, 0xa37d00)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa37d00);
    Check(ctx, "AddChild", [&](bool original, std::mt19937 &rng) {
        Fake &c = rng() % 4 == 0 && w.length ? w.child[w.list[0]] : AnyChild(rng);
        if (original)
            orig(w.block, &w.outer, &c);
        else
            Impl()->AddChild(Node(w.outer), Node(c));
    });
}

DIFF_TEST(SG_CompositionTreeNodeImpl_RemoveChild, 0xa38240)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa38240);
    Check(ctx, "RemoveChild", [&](bool original, std::mt19937 &rng) {
        if (!w.length) return;
        Fake &c = w.child[w.list[rng() % w.length]];
        if (original)
            orig(w.block, &w.outer, &c);
        else
            Impl()->RemoveChild(Node(w.outer), Node(c));
    });
}

DIFF_TEST(SG_CompositionTreeNodeImpl_SetFlag, 0xa38320)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, bool)>(0xa38320);
    Check(ctx, "SetFlag", [&](bool original, std::mt19937 &rng) {
        for (int i = 0; i < 6; ++i) {
            const uint32_t bit = rng() % 40;
            const bool value = rng() % 2;
            if (original)
                orig(w.block, bit, value);
            else
                Impl()->SetFlag(bit, value);
            drawfx::Note("flags", w.block[6]);
        }
    });
}

DIFF_TEST(SG_CompositionTreeNodeImpl_IsKindOf, 0xa38340)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t)>(0xa38340);
    Check(
        ctx, "IsKindOf",
        [&](bool original, std::mt19937 &rng) {
            const uint32_t iids[] = {0x4512dd13, 0x5aef607b, 0x3b253dc7, 0xbb11a53e, 0x5b0aaa74, 1,
                                     0,          0xfb859324, 0xdb01d4b2, 2,          uint32_t(rng())};
            for (uint32_t iid : iids)
                drawfx::Note("kind", {iid, uint8_t(original ? orig(w.block, iid) : Impl()->IsKindOf(iid))});
        },
        4);
}

namespace {
// A connection-change body (its fields only are read): connected (0), disconnecting (1) or another value; the
// subordinate a child node; the list the children's or another.
cGraphConnChanged MakeConnBody(std::mt19937 &rng)
{
    const uint32_t iids[] = {sgfx::kIidTree, sgfx::kIidTree, sgfx::kIidGraph};
    return cGraphConnChanged(rng() % 3, Node(w.outer), Node(AnyChild(rng)), iids[rng() % 3]);
}
} // namespace

DIFF_TEST(SG_CompositionTreeNodeImpl_OnEvent, 0xa38160)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, uint32_t, uint32_t, void *, uint32_t)>(0xa38160);
    Check(ctx, "OnEvent", [&](bool original, std::mt19937 &rng) {
        const uint32_t types[] = {cGraphConnChanged::kEventId, cGraphConnChanged::kEventId, cSubordinateListChanged::kEventId,
                                  0x5b51e806};
        const uint32_t type = types[rng() % 4];
        cGraphConnChanged body = MakeConnBody(rng);
        const uint32_t arg1 = rng(), sender = P(&w.child[0]);
        if (original)
            orig(w.block, &w.outer, type, arg1, &body, sender);
        else
            Impl()->OnEvent(Node(w.outer), type, arg1, &body, sender);
    });
}

DIFF_TEST(SG_CompositionTreeNodeImpl_OnGraphConnChanged, 0xa37be0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, const void *)>(0xa37be0);
    Check(ctx, "OnGraphConnChanged", [&](bool original, std::mt19937 &rng) {
        cGraphConnChanged body = MakeConnBody(rng);
        if (original)
            orig(w.block, &w.outer, &body);
        else
            Impl()->OnGraphConnChanged(Node(w.outer), &body);
    });
}

DIFF_TEST(SG_CompositionTreeNodeImpl_Read, 0xa38180)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa38180);
    Check(ctx, "Read", [&](bool original, std::mt19937 &rng) {
        sgfx::Reader &r = sgfx::gReader;
        r.Version(rng() % 16);        // the node's record
        r.Version(4 + rng() % 2);     // the object-graph part's (no legacy tag here: sg_object_graph_node_impl)
        const uint32_t extensions = rng() % 2;
        r.Word(extensions);
        if (extensions) r.Ref(&w.ext);
        r.Text(rng() % 2 ? "alpha" : "beta");
        const uint32_t children = rng() % 4;
        r.Word(children);
        for (uint32_t i = 0; i < children; ++i) r.Ref(rng() % 5 ? static_cast<void *>(&AnyChild(rng)) : nullptr);
        if (original)
            orig(w.block, &w.outer, &r.fake);
        else
            Impl()->Read(Node(w.outer), reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
    });
}

DIFF_TEST(SG_CompositionTreeNodeImpl_GetTypeInfo, 0xa38360)
{
    auto orig = ctx.Original<cPersistTypeInfo *(__thiscall *)(void *, cPersistTypeInfo *)>(0xa38360);
    Check(
        ctx, "GetTypeInfo",
        [&](bool original, std::mt19937 &) {
            cPersistTypeInfo info{};
            cPersistTypeInfo *r = original ? orig(w.block, &info) : Impl()->GetTypeInfo(&info);
            drawfx::Note("returns the record", r == &info);
            sgfx::NoteTypeInfo("type", &info);
            difftest::FreeRecorder freed;
            // Released into a local: a Note inside the recorder window can grow the log and count its free (VH, shuffle seed 11).
            const uint32_t released = info.mName ? info.mName->Release() : 0xdead;
            freed.Stop();
            drawfx::Note("name released", released);
            drawfx::Note("frees", uint32_t(freed.Count()));
        },
        4);
}

DIFF_TEST(SG_CompositionTreeNodeImpl_Write, 0xa38430)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa38430);
    Check(ctx, "Write", [&](bool original, std::mt19937 &) {
        if (original)
            orig(w.block, &w.outer, &sgfx::gWriter.fake);
        else
            Impl()->Write(Node(w.outer), reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
    });
}
