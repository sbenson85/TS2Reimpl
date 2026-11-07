// Differential tests of nGZSceneGraph::cTransformNodeImpl (lane OB, scenegraph N0 part 3). Each member runs on a
// 0x3c-byte Impl block (the composition part, then the limit and the rigid transform) in the world of
// sg_transform_fx.h (its top says what the fakes leave out and why). The original runs at its RVA, ours as the member.
// CloneInto (0xa3ae50) is covered by the node-level clone test (sg_transform_node.cpp), where the tree code it calls
// runs on real nodes.
#include "sg_transform_fx.h"

#include "sg/cTransformNodeImpl.h"

namespace {

using namespace nGZSceneGraph;
using namespace sgtx;

World<15> w;
cTransformNodeImpl *Impl() { return w.Impl<cTransformNodeImpl>(); }

const uint32_t kAttributeIds[] = {cITransformNode::kLimitAttribute, cITransformNode::kTransformAttribute,
                                  cITransformNode::kRotationAttribute, cITransformNode::kTranslationAttribute,
                                  0x12345678};

} // namespace

DIFF_TEST(SG_TransformNodeImpl_Construct, 0xa3a020)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xa3a020);
    w.Check(
        ctx, "constructor",
        [&](bool original, std::mt19937 &) {
            for (uint32_t &word : w.block) word = 0xcdcdcdcd;
            void *r = original ? orig(w.block) : Impl()->Construct();
            drawfx::Note("returns the block", r == w.block);
        },
        4);
}

DIFF_TEST(SG_TransformNodeImpl_GetWorldTransform, 0xa3a7d0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0xa3a7d0);
    w.Check(ctx, "GetWorldTransform", [&](bool original, std::mt19937 &rng) {
        cRigidTransform out;
        for (float &v : out.mValues) v = RandomFloat(rng); // (every float is written)
        void *r = original ? orig(w.block, &out) : Impl()->GetWorldTransform(&out);
        drawfx::Note("returns out", r == &out);
        NoteFloats("world", out.mValues, 7);
    });
}

DIFF_TEST(SG_TransformNodeImpl_FromParent34, 0xa3a860)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, const void *)>(0xa3a860);
    w.Check(ctx, "FromParent34", [&](bool original, std::mt19937 &rng) {
        cTransform34 in = RandomFrame(rng), out = RandomFrame(rng);
        if (original)
            orig(w.block, &out, &in);
        else
            Impl()->FromParent34(&out, &in);
        NoteFloats("out", reinterpret_cast<float *>(&out), sizeof(out) / 4);
    });
}

DIFF_TEST(SG_TransformNodeImpl_FromParent1c, 0xa3a8a0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, const void *)>(0xa3a8a0);
    w.Check(ctx, "FromParent1c", [&](bool original, std::mt19937 &rng) {
        cRigidTransform in = RandomRigid(rng), out = RandomRigid(rng);
        if (original)
            orig(w.block, &out, &in);
        else
            Impl()->FromParent1c(&out, &in);
        NoteFloats("out", out.mValues, 7);
    });
}

DIFF_TEST(SG_TransformNodeImpl_ToParent34, 0xa3a8d0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, const void *)>(0xa3a8d0);
    w.Check(ctx, "ToParent34", [&](bool original, std::mt19937 &rng) {
        cTransform34 in = RandomFrame(rng), out = RandomFrame(rng);
        if (original)
            orig(w.block, &out, &in);
        else
            Impl()->ToParent34(&out, &in);
        NoteFloats("out", reinterpret_cast<float *>(&out), sizeof(out) / 4);
    });
}

DIFF_TEST(SG_TransformNodeImpl_ToParent1c, 0xa3a960)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, const void *)>(0xa3a960);
    w.Check(ctx, "ToParent1c", [&](bool original, std::mt19937 &rng) {
        cRigidTransform in = RandomRigid(rng), out = RandomRigid(rng);
        if (original)
            orig(w.block, &out, &in);
        else
            Impl()->ToParent1c(&out, &in);
        NoteFloats("out", out.mValues, 7);
    });
}

DIFF_TEST(SG_TransformNodeImpl_GetAttribute, 0xa3a9b0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, uint32_t, uint32_t)>(0xa3a9b0);
    w.Check(ctx, "GetAttribute", [&](bool original, std::mt19937 &rng) {
        for (uint32_t id : kAttributeIds) {
            cAttributeValue out;
            out.mType = 0x1111;
            for (float &v : out.mValues) v = 77.0f; // (the untouched floats stay as they were)
            const uint32_t extra = rng();
            if (original)
                orig(w.block, &out, id, extra);
            else
                Impl()->GetAttribute(&out, id, extra);
            drawfx::Note("type", out.mType);
            NoteFloats("record", out.mValues, 7);
        }
    });
}

DIFF_TEST(SG_TransformNodeImpl_SetAttribute, 0xa3acb0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, uint32_t, uint32_t, const void *)>(0xa3acb0);
    w.Check(ctx, "SetAttribute", [&](bool original, std::mt19937 &rng) {
        for (int k = 0; k < 4; ++k) {
            const uint32_t id = kAttributeIds[rng() % 5];
            cAttributeValue value;
            value.mType = uint32_t(rng());
            for (float &v : value.mValues) v = RandomFloat(rng);
            if (id == cITransformNode::kLimitAttribute && rng() % 3)
                value.mValues[0] = std::bit_cast<float>(rng() % 2 ? w.block[7] : uint32_t(rng()));
            w.outer.value[sgfx::kFlagsWord] &= ~0x1000u; // (flag 12 clear: the propagation runs)
            const uint32_t extra = rng();
            if (original)
                orig(w.block, &w.outer, id, extra, &value);
            else
                Impl()->SetAttribute(w.Outer(), id, extra, &value);
            drawfx::Note("step", k);
            NoteFloats("transform", reinterpret_cast<float *>(&w.block[7]), 8);
        }
    });
}

DIFF_TEST(SG_TransformNodeImpl_SetLimit, 0xa3ab70)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, uint32_t)>(0xa3ab70);
    w.Check(ctx, "SetLimit", [&](bool original, std::mt19937 &rng) {
        for (int k = 0; k < 4; ++k) {
            const uint32_t limit = rng() % 3 == 0 ? w.block[7] : rng() % 2 ? 0x7fffffffu : uint32_t(rng());
            if (rng() % 2) w.block[6] ^= 1u << 7; // (flag 7: the change is not announced)
            w.outer.value[sgfx::kFlagsWord] &= ~0x1000u;
            if (original)
                orig(w.block, &w.outer, limit);
            else
                Impl()->SetLimit(w.Outer(), limit);
            drawfx::Note("step", k);
        }
    });
}

DIFF_TEST(SG_TransformNodeImpl_SetTransform, 0xa3abb0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, const void *)>(0xa3abb0);
    w.Check(ctx, "SetTransform", [&](bool original, std::mt19937 &rng) {
        for (int k = 0; k < 3; ++k) {
            const cRigidTransform t = RandomRigid(rng);
            w.outer.value[sgfx::kFlagsWord] &= ~0x1000u;
            if (rng() % 3 == 0) w.outer.value[sgfx::kFlagsWord] |= 0x1000u; // (flag 12 set: nothing is announced)
            if (original)
                orig(w.block, &w.outer, &t);
            else
                Impl()->SetTransform(w.Outer(), &t);
            drawfx::Note("step", k);
        }
    });
}

DIFF_TEST(SG_TransformNodeImpl_IsKindOf, 0xa3abe0)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t)>(0xa3abe0);
    w.Check(
        ctx, "IsKindOf",
        [&](bool original, std::mt19937 &rng) {
            const uint32_t iids[] = {0xe51c10f5, 0x4512dd13, 0x5aef607b, 0x3b253dc7, 0x7d14eb66, 0x7d3a2385, 1, 0,
                                     0xfb859324, 2, uint32_t(rng())};
            for (uint32_t iid : iids)
                drawfx::Note("kind", {iid, uint8_t(original ? orig(w.block, iid) : Impl()->IsKindOf(iid))});
        },
        4);
}

DIFF_TEST(SG_TransformNodeImpl_GetTypeInfo, 0xa3ac00)
{
    auto orig = ctx.Original<cPersistTypeInfo *(__thiscall *)(void *, cPersistTypeInfo *)>(0xa3ac00);
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

DIFF_TEST(SG_TransformNodeImpl_Read, 0xa3aa60)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa3aa60);
    w.Check(ctx, "Read", [&](bool original, std::mt19937 &rng) {
        sgfx::Reader &r = sgfx::gReader;
        r.Version(rng() % 16);    // the transform node's record
        r.Version(rng() % 16);    // the composition part's
        r.Version(4 + rng() % 2); // the object-graph part's
        r.Word(0);                // (no extensions)
        r.Text(rng() % 2 ? "alpha" : "beta");
        r.Word(0); // (no children)
        for (int i = 0; i < 7; ++i) r.Float(RandomFloat(rng));
        r.Word(rng() % 2 ? 0x7fffffffu : uint32_t(rng()));
        if (original)
            orig(w.block, &w.outer, &r.fake);
        else
            Impl()->Read(w.Outer(), reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
    });
}

DIFF_TEST(SG_TransformNodeImpl_Write, 0xa3ad50)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa3ad50);
    w.Check(ctx, "Write", [&](bool original, std::mt19937 &) {
        if (original)
            orig(w.block, &w.outer, &sgfx::gWriter.fake);
        else
            Impl()->Write(w.Outer(), reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
    });
}
