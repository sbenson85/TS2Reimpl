// Differential tests of nGZSceneGraph::cRenderableNodeImpl (lane PX, scenegraph N3). Each member runs on a 0x70-byte Impl
// block (the bounded node's, then the layer mask, the word at +0x64, the cached detail limit and a zero) in the world of
// sg_transform_fx.h, with the render-group registry and the detail object as stand-ins (sg_renderable_fx.h). The original
// runs at its RVA, ours as the member. CloneInto (0xae0630) is covered by the node-level clone test of the viewer
// reference (it calls the tree code on real nodes). The layer-name range (0xae02f0) and its iterators:
// sg_render_group_name_iterator.cpp.
#include "sg_renderable_fx.h"

namespace {

using namespace nGZSceneGraph;
using namespace sgtx;

World<28> w;
cRenderableNodeImpl *Impl() { return w.Impl<cRenderableNodeImpl>(); }

// A name the registry has, one it has not, or the empty one.
const char *RandomName(std::mt19937 &rng)
{
    sgrx::Registry &r = sgrx::Registry::Get();
    for (unsigned tries = 0; tries < 8; ++tries) {
        const char *name = r.names[rng() % 32];
        if (name && rng() % 4 != 0) return name;
    }
    static const char *const kFresh[] = {"fresh", "newgroup", ""};
    return kFresh[rng() % 3];
}

} // namespace

DIFF_TEST(SG_RenderableNodeImpl_Construct, 0xadfff0)
{
    sgrx::Services services;
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xadfff0);
    w.Check(
        ctx, "constructor",
        [&](bool original, std::mt19937 &rng) {
            sgrx::Setup(rng);
            for (uint32_t &word : w.block) word = 0xcdcdcdcd;
            void *r = original ? orig(w.block) : Impl()->Construct();
            drawfx::Note("returns the block", r == w.block);
        },
        4);
}

DIFF_TEST(SG_RenderableNodeImpl_AddLayer, 0xae00a0)
{
    sgrx::Services services;
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *)>(0xae00a0);
    w.Check(ctx, "AddLayer", [&](bool original, std::mt19937 &rng) {
        sgrx::Setup(rng);
        sgrx::RandomLayerWords(w.block, rng);
        for (int k = 0; k < 4; ++k) {
            const char *name = RandomName(rng);
            if (original)
                orig(w.block, name);
            else
                Impl()->AddLayer(name);
            drawfx::Note("step", {uint32_t(k)});
        }
    });
}

DIFF_TEST(SG_RenderableNodeImpl_AddLayerBits, 0xae00d0)
{
    sgrx::Services services;
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xae00d0);
    w.Check(ctx, "AddLayerBits", [&](bool original, std::mt19937 &rng) {
        sgrx::Setup(rng);
        sgrx::RandomLayerWords(w.block, rng);
        for (int k = 0; k < 4; ++k) {
            const uint32_t bits = sgrx::RandomMask(rng);
            if (original)
                orig(w.block, bits);
            else
                Impl()->AddLayerBits(bits);
            drawfx::Note("step", {uint32_t(k)});
        }
    });
}

DIFF_TEST(SG_RenderableNodeImpl_RemoveLayer, 0xae02a0)
{
    sgrx::Services services;
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *)>(0xae02a0);
    w.Check(ctx, "RemoveLayer", [&](bool original, std::mt19937 &rng) {
        sgrx::Setup(rng);
        sgrx::RandomLayerWords(w.block, rng);
        for (int k = 0; k < 4; ++k) {
            const char *name = RandomName(rng);
            if (original)
                orig(w.block, name);
            else
                Impl()->RemoveLayer(name);
            drawfx::Note("step", {uint32_t(k)});
        }
    });
}

DIFF_TEST(SG_RenderableNodeImpl_RemoveLayerBits, 0xae02d0)
{
    sgrx::Services services;
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xae02d0);
    w.Check(ctx, "RemoveLayerBits", [&](bool original, std::mt19937 &rng) {
        sgrx::Setup(rng);
        sgrx::RandomLayerWords(w.block, rng);
        for (int k = 0; k < 4; ++k) {
            const uint32_t bits = sgrx::RandomMask(rng);
            if (original)
                orig(w.block, bits);
            else
                Impl()->RemoveLayerBits(bits);
            drawfx::Note("step", {uint32_t(k)});
        }
    });
}

DIFF_TEST(SG_RenderableNodeImpl_CountLayers, 0xae00f0)
{
    sgrx::Services services;
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0xae00f0);
    w.Check(ctx, "CountLayers", [&](bool original, std::mt19937 &rng) {
        sgrx::Setup(rng);
        for (int k = 0; k < 6; ++k) {
            w.block[24] = sgrx::RandomMask(rng);
            drawfx::Note("count", {w.block[24], original ? orig(w.block) : Impl()->CountLayers()});
        }
    });
}

DIFF_TEST(SG_RenderableNodeImpl_SetWorldTransform, 0xae04b0)
{
    sgrx::Services services;
    auto orig = ctx.Original<void(__thiscall *)(void *, const void *, uint32_t, uint32_t)>(0xae04b0);
    w.Check(ctx, "SetWorldTransform", [&](bool original, std::mt19937 &rng) {
        sgrx::Setup(rng);
        sgrx::RandomLayerWords(w.block, rng);
        if (rng() % 2) w.block[6] |= 1u << 12; // (stale: cleared by the call)
        const cRigidTransform t = RandomRigid(rng);
        if (original)
            orig(w.block, &t, 0x13572468u, 0x2468ace0u);
        else
            Impl()->SetWorldTransform(&t, 0x13572468u, 0x2468ace0u);
    });
}

DIFF_TEST(SG_RenderableNodeImpl_RefreshDetailLimit, 0xae06b0)
{
    sgrx::Services services;
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xae06b0);
    w.Check(ctx, "RefreshDetailLimit", [&](bool original, std::mt19937 &rng) {
        sgrx::Setup(rng);
        sgrx::RandomLayerWords(w.block, rng);
        if (original)
            orig(w.block);
        else
            Impl()->RefreshDetailLimit();
    });
}

DIFF_TEST(SG_RenderableNodeImpl_Read, 0xae0140)
{
    sgrx::Services services;
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xae0140);
    w.Check(ctx, "Read", [&](bool original, std::mt19937 &rng) {
        sgrx::Setup(rng);
        sgrx::RandomLayerWords(w.block, rng);
        sgfx::Reader &r = sgfx::gReader;
        for (int i = 0; i < 4; ++i) r.Version(rng() % 16); // the renderable, bounded, transform and composition records
        r.Version(4 + rng() % 2);                           // the object-graph part's
        r.Word(0);                                          // (no extensions)
        r.Text(rng() % 2 ? "alpha" : "beta");
        r.Word(0); // (no children)
        for (int i = 0; i < 7; ++i) r.Float(RandomFloat(rng));
        r.Word(rng() % 2 ? 0x7fffffffu : uint32_t(rng()));
        r.Word(rng() % 2); // the bound flag
        if (rng() % 2) w.block[6] |= 1u << 6;
        r.Word(rng() % 3 == 0); // all layers
        const unsigned count = rng() % 4;
        r.Word(count);
        for (unsigned i = 0; i < count; ++i) r.Text(RandomName(rng));
        r.Word(uint32_t(rng())); // the word at +0x64
        r.Word(rng() % 2);       // flag 10
        if (original)
            orig(w.block, &w.outer, &r.fake);
        else
            Impl()->Read(w.OuterAs<cIBoundedNode>(), reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
    });
}

DIFF_TEST(SG_RenderableNodeImpl_IsKindOf, 0xae03d0)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t)>(0xae03d0);
    w.Check(
        ctx, "IsKindOf",
        [&](bool original, std::mt19937 &rng) {
            const uint32_t iids[] = {0x1c585e30, 0x7d14eb66, 0xe51c10f5, 0x4512dd13, 0x5aef607b, 0x3b253dc7, 1, 0,
                                     0xfb859324, 2, uint32_t(rng())};
            for (uint32_t iid : iids)
                drawfx::Note("kind", {iid, uint8_t(original ? orig(w.block, iid) : Impl()->IsKindOf(iid))});
        },
        4);
}

DIFF_TEST(SG_RenderableNodeImpl_GetTypeInfo, 0xae03f0)
{
    auto orig = ctx.Original<cPersistTypeInfo *(__thiscall *)(void *, cPersistTypeInfo *)>(0xae03f0);
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

DIFF_TEST(SG_RenderableNodeImpl_Write, 0xae04f0)
{
    sgrx::Services services;
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xae04f0);
    w.Check(ctx, "Write", [&](bool original, std::mt19937 &rng) {
        sgrx::Setup(rng);
        sgrx::RandomLayerWords(w.block, rng);
        // The three name iterators the walk makes are all released: their blocks freed (the type name's too).
        difftest::FreeRecorder freed;
        if (original)
            orig(w.block, &w.outer, &sgfx::gWriter.fake);
        else
            Impl()->Write(w.OuterAs<cIBoundedNode>(), reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        freed.Stop();
        drawfx::Note("frees", uint32_t(freed.Count()));
    });
}
