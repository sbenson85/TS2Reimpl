// Differential tests of nGZSceneGraph::cPartitionNodeImpl (lane OI, scenegraph N1 step 1). Each member runs on a
// 0x60-byte Impl block (the bounded node's state: the partition adds only its type record) in the world of
// sg_transform_fx.h (its top says what the fakes leave out and why). The original runs at its RVA, ours as the member.
// Read is 0xa4bd60, cExplicitBoundNodeImpl's (verified there); it is run here against our partition member too.
#include "sg_transform_fx.h"

#include "sg/cPartitionNodeImpl.h"

namespace {

using namespace nGZSceneGraph;
using namespace sgtx;

World<24> w;
cPartitionNodeImpl *Impl() { return w.Impl<cPartitionNodeImpl>(); }

} // namespace

DIFF_TEST(SG_PartitionNodeImpl_Construct, 0xadfe10)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xadfe10);
    w.Check(
        ctx, "constructor",
        [&](bool original, std::mt19937 &) {
            for (uint32_t &word : w.block) word = 0xcdcdcdcd;
            void *r = original ? orig(w.block) : Impl()->Construct();
            drawfx::Note("returns the block", r == w.block);
        },
        4);
}

DIFF_TEST(SG_PartitionNodeImpl_Read, 0xa4bd60)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa4bd60);
    w.Check(ctx, "Read", [&](bool original, std::mt19937 &rng) {
        sgfx::Reader &r = sgfx::gReader;
        for (int i = 0; i < 4; ++i) r.Version(rng() % 16); // the partition, bounded, transform and composition records
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

DIFF_TEST(SG_PartitionNodeImpl_IsKindOf, 0xadfe40)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t)>(0xadfe40);
    w.Check(
        ctx, "IsKindOf",
        [&](bool original, std::mt19937 &rng) {
            const uint32_t iids[] = {0x1c8e36aa, 0x7d14eb66, 0xe51c10f5, 0x4512dd13, 0x5aef607b, 0x7d3a2385, 0xebfda963,
                                     1,          0,          0x1c8e36ab, 0x3b253dc7, uint32_t(rng())};
            for (uint32_t iid : iids)
                drawfx::Note("kind", {iid, uint8_t(original ? orig(w.block, iid) : Impl()->IsKindOf(iid))});
        },
        4);
}

DIFF_TEST(SG_PartitionNodeImpl_GetTypeInfo, 0xadfe60)
{
    auto orig = ctx.Original<cPersistTypeInfo *(__thiscall *)(void *, cPersistTypeInfo *)>(0xadfe60);
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

DIFF_TEST(SG_PartitionNodeImpl_Write, 0xadff10)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xadff10);
    w.Check(ctx, "Write", [&](bool original, std::mt19937 &) {
        // Each level's record names a type it releases once written: the frees say the names went.
        difftest::FreeRecorder freed;
        if (original)
            orig(w.block, &w.outer, &sgfx::gWriter.fake);
        else
            Impl()->Write(w.OuterAs<cIBoundedNode>(), reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        freed.Stop();
        drawfx::Note("frees", uint32_t(freed.Count()));
    });
}
