// Differential tests of nGZSceneGraph::cViewerRefNodeBaseImpl (lane PY, scenegraph N3 part 2). Each member runs on a
// 0xe8-byte Impl block (sg_viewer_ref_fx.h: the renderable part, then the viewer's, seeded with counted fakes) with the
// framework, the held bound node, the device and the texture manager as stand-ins. The original runs at its RVA, ours as
// the member; the block, every fake's count and the calls made are compared. Not here: the texture getters, the cull walk
// and the event handler (sg_viewer_ref_node_base_impl_part2.cpp) and CloneInto (docs/subsystems/scenegraph.md).
#include "sg_viewer_ref_fx.h"

namespace {

using namespace nGZSceneGraph;
using namespace svx;

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

const char *RandomTexture(std::mt19937 &rng)
{
    static const char *const kNames[] = {"tex", "xbad", "a-longer-name", "x"};
    return kNames[rng() % 4];
}

cIGZUnknown *PickHeld(std::mt19937 &rng) { return rng() % 3 == 0 ? nullptr : Held(&gVw.held[rng() % 12]); }

} // namespace

DIFF_TEST(SG_ViewerRef_Construct, 0xae06d0)
{
    Stands stands;
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xae06d0);
    Check(
        ctx, "constructor",
        [&](bool original, std::mt19937 &rng) {
            SeedBase(rng);
            for (uint32_t &word : gW.block) word = 0xcdcdcdcd;
            void *r = original ? orig(gW.block) : Impl()->Construct();
            drawfx::Note("returns the block", r == gW.block);
        },
        32);
}

DIFF_TEST(SG_ViewerRef_Destruct, 0xae08a0)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xae08a0);
    Check(ctx, "destructor", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        if (original)
            orig(gW.block);
        else
            Impl()->Destruct();
    });
}

DIFF_TEST(SG_ViewerRef_GetDepthSurface, 0xae0b90)
{
    Stands stands;
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xae0b90);
    Check(ctx, "GetDepthSurface", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        void *r = original ? orig(gW.block) : Impl()->GetDepthSurface();
        drawfx::Note("surface", {drawfx::Norm(drawfx::P(r))});
    });
}

DIFF_TEST(SG_ViewerRef_GetColorSurface, 0xae0bd0)
{
    Stands stands;
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xae0bd0);
    Check(ctx, "GetColorSurface", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        void *r = original ? orig(gW.block) : Impl()->GetColorSurface();
        drawfx::Note("surface", {drawfx::Norm(drawfx::P(r))});
    });
}

DIFF_TEST(SG_ViewerRef_UsesDeviceTargets, 0xae0c10)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *)>(0xae0c10);
    Stands stands;
    Check(ctx, "UsesDeviceTargets", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        for (int k = 0; k < 2; ++k) {
            SeedState(rng);
            drawfx::Note("answer", {uint8_t(original ? orig(gW.block) : Impl()->UsesDeviceTargets())});
        }
    });
}

DIFF_TEST(SG_ViewerRef_BoundSlot70, 0xae0ce0)
{
    Stands stands;
    auto orig = ctx.Original<bool(__thiscall *)(void *)>(0xae0ce0);
    Check(ctx, "BoundSlot70", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        drawfx::Note("answer", {uint8_t(original ? orig(gW.block) : Impl()->BoundSlot70())});
    });
}

DIFF_TEST(SG_ViewerRef_BoundSlot73, 0xae0d00)
{
    Stands stands;
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, uint32_t)>(0xae0d00);
    Check(ctx, "BoundSlot73", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        const uint32_t a = uint32_t(rng()), b = uint32_t(rng());
        drawfx::Note("answer", {uint8_t(original ? orig(gW.block, a, b) : Impl()->BoundSlot73(a, b))});
    });
}

DIFF_TEST(SG_ViewerRef_IntersectsLinear, 0xae0d20)
{
    Stands stands;
    auto orig = ctx.Original<bool(__thiscall *)(void *, void *)>(0xae0d20);
    Check(ctx, "IntersectsLinear", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        auto *linear = reinterpret_cast<nGZCompGeom::cILinear3 *>(&gVw.held[12]);
        drawfx::Note("answer", {uint8_t(original ? orig(gW.block, linear) : Impl()->IntersectsLinear(linear))});
    });
}

DIFF_TEST(SG_ViewerRef_FirstHitLinear, 0xae0d40)
{
    Stands stands;
    auto orig = ctx.Original<bool(__thiscall *)(void *, float *, void *)>(0xae0d40);
    Check(ctx, "FirstHitLinear", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        float t = 0;
        auto *linear = reinterpret_cast<nGZCompGeom::cILinear3 *>(&gVw.held[12]);
        drawfx::Note("answer", {uint8_t(original ? orig(gW.block, &t, linear) : Impl()->FirstHitLinear(&t, linear))});
    });
}

DIFF_TEST(SG_ViewerRef_GetBoundBox, 0xae0d60)
{
    Stands stands;
    auto orig = ctx.Original<cBoundBox *(__thiscall *)(void *, cBoundBox *, void *)>(0xae0d60);
    Check(ctx, "GetBoundBox", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        cBoundBox out;
        std::memset(&out, 0xcd, sizeof(out));
        cBoundBox *r = original ? orig(gW.block, &out, gW.Outer()) : Impl()->GetBoundBox(&out, gW.Outer());
        drawfx::Note("returns the out", r == &out);
        sgtx::NoteFloats("box", reinterpret_cast<const float *>(&out), 6);
    });
}

DIFF_TEST(SG_ViewerRef_GetBoundBoxIn, 0xae0db0)
{
    Stands stands;
    auto orig = ctx.Original<cBoundBox *(__thiscall *)(void *, cBoundBox *, void *, const cTransform34 *)>(0xae0db0);
    Check(ctx, "GetBoundBoxIn", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        const cTransform34 frame = sgtx::RandomFrame(rng);
        cBoundBox out;
        std::memset(&out, 0xcd, sizeof(out));
        cBoundBox *r = original ? orig(gW.block, &out, gW.Outer(), &frame) : Impl()->GetBoundBoxIn(&out, gW.Outer(), &frame);
        drawfx::Note("returns the out", r == &out);
        sgtx::NoteFloats("box", reinterpret_cast<const float *>(&out), 6);
    });
}

DIFF_TEST(SG_ViewerRef_CallTextureSlot7, 0xae0e00)
{
    Stands stands;
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0xae0e00);
    Check(ctx, "CallTextureSlot7", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        auto *p = Impl();
        if (rng() % 2) {
            p->mMode = 0;
            p->mTextureHandle = 1 + rng() % 9; // (an invalid handle has no texture to call)
        } else if (p->mMode == 0) {
            p->mMode = 1 + rng() % 4;
        }
        drawfx::Note("answer", {original ? orig(gW.block) : p->CallTextureSlot7()});
    });
}

DIFF_TEST(SG_ViewerRef_IsDeviceViewMode, 0xae1320)
{
    Stands stands;
    auto orig = ctx.Original<bool(__thiscall *)(void *)>(0xae1320);
    Check(ctx, "IsDeviceViewMode", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        for (int k = 0; k < 4; ++k) {
            SeedState(rng);
            if (rng() % 2) Impl()->mMode = 1;
            drawfx::Note("answer", {uint8_t(original ? orig(gW.block) : Impl()->IsDeviceViewMode())});
        }
    });
}

DIFF_TEST(SG_ViewerRef_SetSurfaces, 0xae1470)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xae1470);
    Check(ctx, "SetSurfaces", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        for (int k = 0; k < 3; ++k) {
            // (the same object again, or another, or none)
            cIGZUnknown *color = rng() % 4 == 0 ? Impl()->mColorSurface : PickHeld(rng);
            cIGZUnknown *depth = rng() % 4 == 0 ? Impl()->mDepthSurface : PickHeld(rng);
            if (color) --reinterpret_cast<Fake *>(color)->refs; // (the caller's reference is not held)
            if (depth) --reinterpret_cast<Fake *>(depth)->refs;
            if (original)
                orig(gW.block, color, depth);
            else
                Impl()->SetSurfaces(color, depth);
            drawfx::Note("step", {uint32_t(k)});
        }
    });
}

DIFF_TEST(SG_ViewerRef_SetRefA8, 0xae14d0)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0xae14d0);
    Check(ctx, "SetRefA8", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        for (int k = 0; k < 3; ++k) {
            cIGZUnknown *ref = rng() % 4 == 0 ? Impl()->mRefA8 : PickHeld(rng);
            if (ref) --reinterpret_cast<Fake *>(ref)->refs;
            if (original)
                orig(gW.block, ref);
            else
                Impl()->SetRefA8(ref);
        }
    });
}

DIFF_TEST(SG_ViewerRef_SetRefA4, 0xae1510)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0xae1510);
    Check(ctx, "SetRefA4", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        for (int k = 0; k < 3; ++k) {
            cIGZUnknown *ref = rng() % 4 == 0 ? Impl()->mRefA4 : PickHeld(rng);
            if (ref) --reinterpret_cast<Fake *>(ref)->refs;
            if (original)
                orig(gW.block, ref);
            else
                Impl()->SetRefA4(ref);
        }
    });
}

DIFF_TEST(SG_ViewerRef_SetExplicit, 0xae1550)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *, float)>(0xae1550);
    Check(ctx, "SetExplicit", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        for (int k = 0; k < 3; ++k) {
            cIGZUnknown *texture = rng() % 4 == 0 ? Impl()->mTextures[0] : PickHeld(rng);
            cIGZUnknown *ref = rng() % 4 == 0 ? Impl()->mRef94 : PickHeld(rng);
            if (texture) --reinterpret_cast<Fake *>(texture)->refs;
            if (ref) --reinterpret_cast<Fake *>(ref)->refs;
            const float scale = sgtx::RandomFloat(rng);
            if (original)
                orig(gW.block, texture, ref, scale);
            else
                Impl()->SetExplicit(texture, ref, scale);
        }
    });
}

DIFF_TEST(SG_ViewerRef_SetTextures, 0xae1600)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, void *const *, void *, float)>(0xae1600);
    Check(ctx, "SetTextures", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        for (int k = 0; k < 3; ++k) {
            const uint32_t count = rng() % 5;
            cIGZUnknown *array[4];
            for (unsigned i = 0; i < 4; ++i) {
                array[i] = rng() % 4 == 0 ? Impl()->mTextures[i] : PickHeld(rng);
                if (array[i]) --reinterpret_cast<Fake *>(array[i])->refs;
            }
            cIGZUnknown *ref = rng() % 4 == 0 ? Impl()->mRef94 : PickHeld(rng);
            if (ref) --reinterpret_cast<Fake *>(ref)->refs;
            const float scale = sgtx::RandomFloat(rng);
            if (original)
                orig(gW.block, count, reinterpret_cast<void *const *>(array), ref, scale);
            else
                Impl()->SetTextures(count, array, ref, scale);
        }
    });
}

DIFF_TEST(SG_ViewerRef_SetTexture, 0xae16e0)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xae16e0);
    Check(ctx, "SetTexture", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        for (int k = 0; k < 3; ++k) {
            const uint32_t id = rng() % 3 == 0 ? 0xffffffffu : uint32_t(rng() % 50);
            if (original)
                orig(gW.block, id);
            else
                Impl()->SetTexture(id);
        }
    });
}

DIFF_TEST(SG_ViewerRef_SetTextureByName, 0xae17a0)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *, uint32_t)>(0xae17a0);
    Check(ctx, "SetTextureByName", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        for (int k = 0; k < 4; ++k) {
            const char *name = rng() % 5 == 0 ? nullptr : RandomTexture(rng);
            const uint32_t group = rng() % 2 ? 0 : uint32_t(rng() % 100);
            if (original)
                orig(gW.block, name, group);
            else
                Impl()->SetTextureByName(name, group);
        }
    });
}

DIFF_TEST(SG_ViewerRef_SetBound, 0xae1870)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xae1870);
    Check(ctx, "SetBound", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        for (int k = 0; k < 3; ++k) {
            void *bound = rng() % 3 == 0 ? nullptr : &gVw.bound;
            if (original)
                orig(gW.block, &gW.outer, bound);
            else
                Impl()->SetBound(gW.OuterAs<cIBoundedNode>(), static_cast<cIBoundedNode *>(bound));
            drawfx::Note("step", {uint32_t(k)});
        }
    });
}

DIFF_TEST(SG_ViewerRef_SetBoundAndPlane, 0xae19b0)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *, const float *, uint32_t)>(0xae19b0);
    Check(ctx, "SetBoundAndPlane", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        void *bound = rng() % 3 == 0 ? nullptr : &gVw.bound;
        float plane[4];
        for (float &f : plane) f = sgtx::RandomFloat(rng);
        const uint32_t flags = uint32_t(rng());
        if (original)
            orig(gW.block, &gW.outer, bound, plane, flags);
        else
            Impl()->SetBoundAndPlane(gW.OuterAs<cIBoundedNode>(), static_cast<cIBoundedNode *>(bound), plane, flags);
    });
}

DIFF_TEST(SG_ViewerRef_Read, 0xae0e20)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xae0e20);
    Check(ctx, "Read", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        sgfx::Reader &r = sgfx::gReader;
        const uint32_t version = rng() % 7;
        r.Version(version); // the viewer's record
        for (int i = 0; i < 4; ++i) r.Version(rng() % 16); // the renderable, bounded, transform and composition records
        r.Version(4 + rng() % 2);                           // the object-graph part's
        r.Word(0);
        r.Text(rng() % 2 ? "alpha" : "beta");
        r.Word(0);
        for (int i = 0; i < 7; ++i) r.Float(sgtx::RandomFloat(rng));
        r.Word(rng() % 2 ? 0x7fffffffu : uint32_t(rng()));
        r.Word(rng() % 2);
        if (rng() % 2) gW.block[6] |= 1u << 6;
        r.Word(rng() % 3 == 0);
        const unsigned count = rng() % 4;
        r.Word(count);
        for (unsigned i = 0; i < count; ++i) r.Text(RandomName(rng));
        r.Word(uint32_t(rng()));
        r.Word(rng() % 2);
        // The viewer's own part.
        if (version >= 2) {
            r.Word(rng() % 2);
            r.Text(RandomTexture(rng));
        }
        if (version >= 3) {
            r.Word(rng() % 2);
            r.Ref(rng() % 3 == 0 ? nullptr : &gVw.bound);
            for (int i = 0; i < 4; ++i) r.Float(sgtx::RandomFloat(rng));
            r.Word(rng() % 2 ? uint32_t(rng()) : uint32_t(rng() % 4));
        }
        r.Word(uint32_t(rng() % 256));
        r.Word(uint32_t(rng()));
        if (version >= 4) {
            r.Word(uint32_t(rng()));
            for (int i = 0; i < 5; ++i) r.Float(sgtx::RandomFloat(rng));
            r.Word(uint32_t(rng()));
        }
        if (original)
            orig(gW.block, &gW.outer, &r.fake);
        else
            Impl()->Read(gW.OuterAs<cIBoundedNode>(), reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
    });
}

DIFF_TEST(SG_ViewerRef_IsKindOf, 0xae19e0)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t)>(0xae19e0);
    Check(
        ctx, "IsKindOf",
        [&](bool original, std::mt19937 &rng) {
            const uint32_t iids[] = {0xc0c0c0b, 0x1c585e30, 0x7d14eb66, 0xe51c10f5, 0x4512dd13, 0x5aef607b, 0x3b253dc7, 1, 0,
                                     0xfb859324, 2, uint32_t(rng())};
            for (uint32_t iid : iids)
                drawfx::Note("kind", {iid, uint8_t(original ? orig(gW.block, iid) : Impl()->IsKindOf(iid))});
        },
        4);
}

DIFF_TEST(SG_ViewerRef_GetTypeInfo, 0xae1a00)
{
    auto orig = ctx.Original<cPersistTypeInfo *(__thiscall *)(void *, cPersistTypeInfo *)>(0xae1a00);
    Check(
        ctx, "GetTypeInfo",
        [&](bool original, std::mt19937 &) {
            cPersistTypeInfo info{};
            cPersistTypeInfo *r = original ? orig(gW.block, &info) : Impl()->GetTypeInfo(&info);
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

DIFF_TEST(SG_ViewerRef_Write, 0xae1ad0)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xae1ad0);
    Check(ctx, "Write", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        difftest::FreeRecorder freed;
        if (original)
            orig(gW.block, &gW.outer, &sgfx::gWriter.fake);
        else
            Impl()->Write(gW.OuterAs<cIBoundedNode>(), reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        freed.Stop();
        drawfx::Note("frees", uint32_t(freed.Count()));
    });
}
