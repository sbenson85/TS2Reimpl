// Differential tests of nGZSceneGraph::cBoundedNodeImpl (lane OB, scenegraph N0 part 3). Each member runs on a 0x60-byte
// Impl block (the transform node's, then the holder words and the cached world transform) in the world of
// sg_transform_fx.h (its top says what the fakes leave out and why). The original runs at its RVA, ours as the member.
// The blocks have flag 12 (the cache is stale) clear: with it set the box members call the tree code's refresh
// (0xa39b70), which needs real nodes and runs in sg_explicit_bound_node.cpp.
#include "sg_transform_fx.h"

#include "sg/cBoundedNodeImpl.h"

namespace {

using namespace nGZSceneGraph;
using namespace sgtx;

World<24> w;
cBoundedNodeImpl *Impl() { return w.Impl<cBoundedNodeImpl>(); }

void NoteBox(const cBoundBox &box) { NoteFloats("box", box.mMin, 6); }

} // namespace

DIFF_TEST(SG_BoundedNodeImpl_Construct, 0xa3b290)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xa3b290);
    w.Check(
        ctx, "constructor",
        [&](bool original, std::mt19937 &) {
            for (uint32_t &word : w.block) word = 0xcdcdcdcd;
            void *r = original ? orig(w.block) : Impl()->Construct();
            drawfx::Note("returns the block", r == w.block);
        },
        4);
}

DIFF_TEST(SG_BoundedNodeImpl_GetBoundBox, 0xa3b300)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *, void *)>(0xa3b300);
    w.Check(ctx, "GetBoundBox", [&](bool original, std::mt19937 &rng) {
        cBoundBox box;
        for (float &v : box.mMin) v = RandomFloat(rng); // (all six floats are written)
        for (float &v : box.mMax) v = RandomFloat(rng);
        void *r = original ? orig(w.block, &box, &w.outer) : Impl()->GetBoundBox(&box, w.Outer());
        drawfx::Note("returns out", r == &box);
        NoteBox(box);
    });
}

DIFF_TEST(SG_BoundedNodeImpl_GetBoundBoxIn, 0xa3b3b0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *, void *, const void *)>(0xa3b3b0);
    w.Check(ctx, "GetBoundBoxIn", [&](bool original, std::mt19937 &rng) {
        cBoundBox box;
        for (float &v : box.mMin) v = RandomFloat(rng);
        for (float &v : box.mMax) v = RandomFloat(rng);
        cTransform34 frame = RandomFrame(rng);
        if (rng() % 3 == 0) reinterpret_cast<uint32_t *>(&frame)[0] = 1; // (a frame without a matrix part too)
        void *r = original ? orig(w.block, &box, &w.outer, &frame) : Impl()->GetBoundBoxIn(&box, w.Outer(), &frame);
        drawfx::Note("returns out", r == &box);
        NoteBox(box);
    });
}

DIFF_TEST(SG_BoundedNodeImpl_Read, 0xa3b4a0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa3b4a0);
    w.Check(ctx, "Read", [&](bool original, std::mt19937 &rng) {
        sgfx::Reader &r = sgfx::gReader;
        r.Version(rng() % 16);    // the bounded node's record
        r.Version(rng() % 16);    // the transform part's
        r.Version(rng() % 16);    // the composition part's
        r.Version(4 + rng() % 2); // the object-graph part's
        r.Word(0);                // (no extensions)
        r.Text(rng() % 2 ? "alpha" : "beta");
        r.Word(0); // (no children)
        for (int i = 0; i < 7; ++i) r.Float(RandomFloat(rng));
        r.Word(rng() % 2 ? 0x7fffffffu : uint32_t(rng()));
        r.Word(rng() % 2); // the bound flag
        if (rng() % 2) w.block[6] |= 1u << 6; // (flag 6: the bound flag is not applied)
        if (original)
            orig(w.block, &w.outer, &r.fake);
        else
            Impl()->Read(w.OuterAs<cIBoundedNode>(), reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
    });
}

DIFF_TEST(SG_BoundedNodeImpl_SetBoundFlag, 0xa3b530)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, bool)>(0xa3b530);
    w.Check(ctx, "SetBoundFlag", [&](bool original, std::mt19937 &rng) {
        for (int k = 0; k < 4; ++k) {
            const bool value = rng() % 2;
            w.outer.value[sgfx::kFlagsWord] ^= (rng() % 2) << 13; // (flag 13: already queued, or not)
            if (original)
                orig(w.block, &w.outer, value);
            else
                Impl()->SetBoundFlag(w.OuterAs<cIBoundedNode>(), value);
            drawfx::Note("step", {uint32_t(k), uint32_t(value)});
        }
    });
}

DIFF_TEST(SG_BoundedNodeImpl_IsKindOf, 0xa3b5a0)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t)>(0xa3b5a0);
    w.Check(
        ctx, "IsKindOf",
        [&](bool original, std::mt19937 &rng) {
            const uint32_t iids[] = {0x7d14eb66, 0xe51c10f5, 0x4512dd13, 0x5aef607b, 0x3b253dc7, 0x7d3a2385, 1, 0,
                                     0xfb859324, 2, uint32_t(rng())};
            for (uint32_t iid : iids)
                drawfx::Note("kind", {iid, uint8_t(original ? orig(w.block, iid) : Impl()->IsKindOf(iid))});
        },
        4);
}

DIFF_TEST(SG_BoundedNodeImpl_GetTypeInfo, 0xa3b5c0)
{
    auto orig = ctx.Original<cPersistTypeInfo *(__thiscall *)(void *, cPersistTypeInfo *)>(0xa3b5c0);
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

DIFF_TEST(SG_BoundedNodeImpl_Write, 0xa3b670)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa3b670);
    w.Check(ctx, "Write", [&](bool original, std::mt19937 &) {
        if (original)
            orig(w.block, &w.outer, &sgfx::gWriter.fake);
        else
            Impl()->Write(w.OuterAs<cIBoundedNode>(), reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
    });
}
