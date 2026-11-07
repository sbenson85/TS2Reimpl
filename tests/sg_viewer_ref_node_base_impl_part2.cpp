// Differential tests of nGZSceneGraph::cViewerRefNodeBaseImpl, part 2 (lane QC, scenegraph N3 part 2): the texture getters
// 0xae1340 / 0xae10f0 / 0xae1200 / 0xae1000, the cull walk 0xae09e0 and the event handler 0xae0c20. The world is the
// first part's (sg_viewer_ref_fx.h): the 0xe8-byte block seeded with counted fakes, the held bound node (slots 48 / 54 /
// 71 programmed), the current device with its target provider and surfaces, the texture manager's handle operations
// (the image's own code stands in on both sides), the texture target the handle object answers to (iid 0xb96cc22), and a
// view with a drawer whose slot 27 counts what it is asked to draw. The original runs at its RVA, ours as the member;
// the block, every fake's count and every call are compared. Floats come from WildFloat (ordinary values, small
// integers, wide exponents, denormals, signed zeros, infinities, NaNs of random payload and kind); the cull walk
// also plants a same-axis NaN pair of different payloads (eye and plane). Run at both x87 precisions
// (TS2_X87_PC=24 as the game, the default 53): only NaN-ness reaches the cull walk's outputs.
#include "sg_viewer_ref_fx.h"

#include "gz/cConnectionManager.h"
#include "sg/cObjectGraphNodeImpl.h"

namespace {

using namespace nGZSceneGraph;
using namespace svx;

// How many seeds reached each outcome (original side); a test fails when one is below `least`.
struct Tally {
    unsigned n[8] = {};
    void Need(difftest::Context &ctx, const char *what, const char *const *names, unsigned count, unsigned least)
    {
        for (unsigned i = 0; i < count; ++i)
            if (n[i] < least) ctx.Fail("%s: outcome \"%s\" reached %u times (need %u)", what, names[i], n[i], least);
    }
};

// A texture view: the mode, the handle and the held textures randomised so every branch of the getters is reached.
// (With mode 0 the original asks the handle's object unchecked: an invalid handle crashes it, so the getters that do so
// get a valid one.)
// (Mode 2 and above take the first held texture unchecked in 0xae1000, so it is held there when `needTexture` is set.)
void SeedViewerTextures(std::mt19937 &rng, bool needHandle, bool needTexture = false)
{
    SeedBase(rng);
    SeedState(rng);
    cViewerRefNodeBaseImpl *p = Impl();
    if (rng() % 3 == 0) p->mMode = 1; // (mode 1 has the most branches)
    if (p->mMode == 1 && rng() % 2) p->mTextures[0] = nullptr;
    if (needTexture && p->mMode >= 2 && !p->mTextures[0]) p->mTextures[0] = Held(&gVw.held[rng() % 12]);
    if (rng() % 3 == 0) gVw.dev.Returns(35, 0u);
    if (needHandle && p->mMode == 0 && p->mTextureHandle == 0xffffffffu) p->mTextureHandle = 1 + rng() % 9;
}

uint32_t DrawCount(drawfx::Fake &self, const uint32_t *a)
{
    ++self.value[0];
    self.value[1] = a[1];
    return 0;
}

} // namespace

DIFF_TEST(SG_ViewerRef_GetHandleTexture, 0xae1340)
{
    Stands stands;
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xae1340);
    Tally t;
    Check(ctx, "GetHandleTexture", [&](bool original, std::mt19937 &rng) {
        SeedViewerTextures(rng, false);
        void *r = original ? orig(gW.block) : Impl()->GetHandleTexture();
        drawfx::Note("texture", {drawfx::Norm(drawfx::P(r))});
        if (original) ++t.n[Impl()->mTextureHandle == 0xffffffffu ? 0 : r ? 1 : 2];
    });
    const char *const names[] = {"no handle", "texture", "refused"};
    t.Need(ctx, "GetHandleTexture", names, 3, 8);
}

DIFF_TEST(SG_ViewerRef_GetTextureRef94, 0xae10f0)
{
    Stands stands;
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xae10f0);
    Tally t;
    Check(ctx, "GetTextureRef94", [&](bool original, std::mt19937 &rng) {
        SeedViewerTextures(rng, true);
        void *r = original ? orig(gW.block) : Impl()->GetTextureRef94();
        drawfx::Note("ref", {drawfx::Norm(drawfx::P(r))});
        cViewerRefNodeBaseImpl *p = Impl();
        if (original) ++t.n[p->mMode == 0 ? (r ? 0 : 1) : p->mMode == 1 ? (p->mTextures[0] ? 2 : r ? 3 : 4) : 5];
    });
    const char *const names[] = {"handle ref", "handle refused", "mode 1 held", "device surface", "device none", "other mode"};
    t.Need(ctx, "GetTextureRef94", names, 6, 4);
}

DIFF_TEST(SG_ViewerRef_GetTextureN, 0xae1200)
{
    Stands stands;
    auto orig = ctx.Original<void *(__thiscall *)(void *, int)>(0xae1200);
    Tally t;
    Check(ctx, "GetTextureN", [&](bool original, std::mt19937 &rng) {
        SeedViewerTextures(rng, true);
        const int index = int(rng() % 4);
        void *r = original ? orig(gW.block, index) : Impl()->GetTextureN(index);
        drawfx::Note("texture", {drawfx::Norm(drawfx::P(r)), uint32_t(index)});
        cViewerRefNodeBaseImpl *p = Impl();
        if (original) ++t.n[p->mMode == 0 ? (r ? 0 : 1) : p->mMode == 1 ? (p->mTextures[0] ? 2 : r ? 3 : 4) : 5];
    });
    const char *const names[] = {"handle level", "handle refused", "mode 1 held", "device back buffer", "device none", "other mode"};
    t.Need(ctx, "GetTextureN", names, 6, 4);
}

DIFF_TEST(SG_ViewerRef_GetTargetSize, 0xae1000)
{
    Stands stands;
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0xae1000);
    Tally t;
    Check(ctx, "GetTargetSize", [&](bool original, std::mt19937 &rng) {
        SeedViewerTextures(rng, false, true);
        if (rng() % 4 == 0) Impl()->mScale = WildFloat(rng);
        uint32_t out[6];
        for (uint32_t &w : out) w = 0xa5a5a5a5u;
        void *r = original ? orig(gW.block, out) : Impl()->GetTargetSize(reinterpret_cast<cViewerTargetSize *>(out));
        drawfx::Note("returns out", r == out);
        drawfx::Note("size", {out[0], out[1], out[2], out[3], out[4], out[5]});
        if (original) {
            cViewerRefNodeBaseImpl *p = Impl();
            ++t.n[p->mMode == 0 ? 0 : p->mMode == 1 ? (p->mTextures[0] ? 1 : gVw.haveDevice ? 2 : 3) : 4];
        }
    });
    const char *const names[] = {"handle texture", "mode 1 held", "device size", "no device", "mode >= 2"};
    t.Need(ctx, "GetTargetSize", names, 5, 4);
}

DIFF_TEST(SG_ViewerRef_CullAndDraw, 0xae09e0)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *, uint32_t)>(0xae09e0);
    Tally t;
    Check(ctx, "CullAndDraw", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        gVw.drawer.On(27, &DrawCount);
        cViewerRefNodeBaseImpl *p = Impl();
        p->mBound = rng() % 8 == 0 ? nullptr : reinterpret_cast<cIBoundedNode *>(&gVw.bound);
        if (rng() % 2) p->mPlaneFlags |= cViewerRefNodeBaseImpl::kPlaneActiveFlag;
        auto some = [&]() { return rng() % 7 == 0 ? WildFloat(rng) : sgtx::RandomFloat(rng); };
        for (float &f : p->mPlane) f = some();
        cViewerView &v = gVw.view;
        std::memset(&v, 0, sizeof(v));
        v.mDrawer = &gVw.drawer;
        v.mNode = rng() % 8 == 0 ? static_cast<const void *>(&gW.outer) : static_cast<const void *>(&gVw.dev);
        for (auto &row : v.mFrustum.plane)
            for (float &f : row) f = some();
        for (float &f : v.mEye) f = some();
        for (float &f : v.mFrame.mValues) f = some();
        for (float &f : gVw.box) f = some();
        if (rng() % 3 != 0) { // an ordered box: min below max
            for (unsigned i = 0; i < 3; ++i) {
                gVw.box[i] = sgtx::RandomFloat(rng) - 2.0f;
                gVw.box[3 + i] = gVw.box[i] + float(rng() % 1000) / 250.0f;
            }
        }
        if (rng() % 6 == 0) { // a same-axis pair of NaNs with different payloads
            const unsigned axis = rng() % 3;
            p->mPlane[axis] = NanWith(rng);
            v.mEye[axis] = NanWith(rng);
        } else if (rng() % 6 == 0) { // a plane through nothing: the eye's side is exactly 0 (the test is a strict "behind")
            for (float &f : p->mPlane) f = 0.0f;
        }
        uint32_t mask = uint32_t(rng());
        switch (rng() % 4) {
        case 0: mask = (mask & 0xffffff00u) | 0x3f; break;
        case 1: mask &= 0xffffff00u | 0x3f; break;
        default: break;
        }
        gVw.drawer.value[0] = 0;
        gVw.drawer.value[1] = 0;
        if (original)
            orig(gW.block, &gW.outer, &v, mask);
        else
            Impl()->CullAndDraw(gW.OuterAs<cIBoundedNode>(), &v, mask);
        drawfx::Note("drawn", {gVw.drawer.value[0], gVw.drawer.value[1]});
        if (original) {
            const bool self = v.mNode == &gW.outer;
            const bool drawn = gVw.drawer.value[0] != 0;
            ++t.n[self ? 0 : !drawn ? 1 : !p->mBound ? 2 : (uint8_t(mask) == 0x3f) ? 3 : gVw.drawer.value[1] != mask ? 4 : 5];
        }
    });
    const char *const names[] = {"own node", "culled", "no bound", "mask 0x3f", "mask narrowed", "mask kept"};
    t.Need(ctx, "CullAndDraw", names, 6, 4);
}

DIFF_TEST(SG_ViewerRef_OnEvent, 0xae0c20)
{
    Stands stands;
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, uint32_t, uint32_t, void *, uint32_t)>(0xae0c20);
    Tally t;
    Check(ctx, "OnEvent", [&](bool original, std::mt19937 &rng) {
        SeedBase(rng);
        SeedState(rng);
        cViewerRefNodeBaseImpl *p = Impl();
        static const uint32_t kTypes[] = {0xa884a15, 0x5b51e806, 0xcc6c9992, 0x5b51e806, 0x12345678, cGraphConnChanged::kEventId};
        const uint32_t type = kTypes[rng() % 6];
        // The composition node's own event (the base's handling runs first): a child node connected or disconnecting.
        cGraphConnChanged graphBody(rng() % 3, gW.OuterAs<cIObjectGraphNode>(), reinterpret_cast<cIObjectGraphNode *>(&gW.parent),
                                    rng() % 4 ? sgfx::kIidTree : sgfx::kIidGraph);
        nGZEvent::cConnectionChangedEvent body{};
        body.mDisconnected = rng() % 4 == 0 ? uint32_t(rng() % 3) : 1;
        body.mTarget = rng() % 4 ? reinterpret_cast<nGZEvent::cIAbstractBehaviorType *>(&gW.outer)
                                 : reinterpret_cast<nGZEvent::cIAbstractBehaviorType *>(&gVw.dev);
        static const uint32_t kTargetEvents[] = {0xa884a15, 0xcc6c9992, 0xcc6c9992, 0xa884a15, 7};
        body.mTargetEvent = kTargetEvents[rng() % 5];
        const uint32_t sender = rng() % 5 == 0 ? drawfx::P(&gVw.dev) : drawfx::P(&gVw.bound);
        const uint32_t arg1 = uint32_t(rng());
        const bool hadBound = p->mBound != nullptr;
        void *const data = type == cGraphConnChanged::kEventId ? static_cast<void *>(&graphBody) : static_cast<void *>(&body);
        if (original)
            orig(gW.block, &gW.outer, type, arg1, data, sender);
        else
            Impl()->OnEvent(gW.OuterAs<cIBoundedNode>(), type, arg1, data, sender);
        drawfx::Note("bound", {p->mBound != nullptr});
        if (original)
            ++t.n[type == 0x12345678 ? 0 : hadBound && !p->mBound ? 1 : p->mBound && drawfx::P(p->mBound) == sender ? 2 : 3];
    }, 192);
    const char *const names[] = {"other event", "bound removed", "sender is the bound", "sender is not the bound"};
    t.Need(ctx, "OnEvent", names, 4, 6);
}
