// Differential tests of nGZSceneGraph::cExplicitBoundNodeImpl (lane OB, scenegraph N0 part 3). Each member runs on a
// 0x64-byte Impl block (the bounded node's, then the shape) in the world of sg_transform_fx.h (its top says what the
// fakes leave out and why). The original runs at its RVA, ours as the member. The constructor is inlined into the node's
// own (0xa4b430), so it has no function here: the pilot test sg_explicit_bound_node.cpp covers it.
#include "sg_transform_fx.h"

#include "sg/cExplicitBoundNodeImpl.h"

namespace {

using namespace nGZSceneGraph;
using namespace sgtx;

World<25> w;
cExplicitBoundNodeImpl *Impl() { return w.Impl<cExplicitBoundNodeImpl>(); }

} // namespace

DIFF_TEST(SG_ExplicitBoundNodeImpl_Destruct, 0xa4b630)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xa4b630);
    w.Check(ctx, "destructor", [&](bool original, std::mt19937 &) {
        if (original)
            orig(w.block);
        else
            Impl()->Destruct();
    });
}

DIFF_TEST(SG_ExplicitBoundNodeImpl_Read, 0xa4bd60)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa4bd60);
    w.Check(ctx, "Read", [&](bool original, std::mt19937 &rng) {
        sgfx::Reader &r = sgfx::gReader;
        for (int i = 0; i < 4; ++i) r.Version(rng() % 16); // the explicit, bounded, transform and composition records
        r.Version(4 + rng() % 2);                           // the object-graph part's
        r.Word(0);                                          // (no extensions)
        r.Text(rng() % 2 ? "alpha" : "beta");
        r.Word(0); // (no children)
        for (int i = 0; i < 7; ++i) r.Float(RandomFloat(rng));
        r.Word(rng() % 2 ? 0x7fffffffu : uint32_t(rng()));
        r.Word(rng() % 2); // the bound flag
        if (rng() % 2) w.block[6] |= 1u << 6;
        if (original)
            orig(w.block, &w.outer, &r.fake);
        else
            Impl()->Read(w.OuterAs<cIBoundedNode>(), reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
    });
}

DIFF_TEST(SG_ExplicitBoundNodeImpl_IsKindOf, 0xa4bdd0)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t)>(0xa4bdd0);
    w.Check(
        ctx, "IsKindOf",
        [&](bool original, std::mt19937 &rng) {
            const uint32_t iids[] = {0x7d3a2385, 0x7d14eb66, 0xe51c10f5, 0x4512dd13, 0x5aef607b, 0x3b253dc7, 1, 0,
                                     0xfb859324, 2, uint32_t(rng())};
            for (uint32_t iid : iids)
                drawfx::Note("kind", {iid, uint8_t(original ? orig(w.block, iid) : Impl()->IsKindOf(iid))});
        },
        4);
}

DIFF_TEST(SG_ExplicitBoundNodeImpl_GetTypeInfo, 0xa4bdf0)
{
    auto orig = ctx.Original<cPersistTypeInfo *(__thiscall *)(void *, cPersistTypeInfo *)>(0xa4bdf0);
    w.Check(
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

DIFF_TEST(SG_ExplicitBoundNodeImpl_Write, 0xa4bec0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa4bec0);
    w.Check(ctx, "Write", [&](bool original, std::mt19937 &) {
        if (original)
            orig(w.block, &w.outer, &sgfx::gWriter.fake);
        else
            Impl()->Write(w.OuterAs<cIBoundedNode>(), reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
    });
}
