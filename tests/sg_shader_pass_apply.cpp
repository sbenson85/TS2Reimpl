// Differential tests for the eight shader pass apply functions (src/sg/cShaderPassApply.cpp, ShaderPass::Apply):
//   SG_ShaderPassApply0 0xaa44d0   SG_ShaderPassApply1 0xaa3d70   SG_ShaderPassApply2 0xaa3e90   SG_ShaderPassApply3 0xaa3f70
//   SG_ShaderPassApply4 0xaa40a0   SG_ShaderPassApply5 0xaa4180   SG_ShaderPassApply6 0xaa42b0   SG_ShaderPassApply7 0xaa43a0
//
// Each case draws a pass (flags, programs, state block, controllers, part mapper), a frame with its geometry, the incoming
// *out and the flag dword, and runs the image's function and ours (ShaderPass::ApplyFor of the same index) on them. The
// device state (slots 11, 12, 14), the geometries (slots 6, 7, 8), the controllers (slot 11) and the mapper (slot 8) are
// fakes that log every call with their full argument dwords (the clipping command by its fields); the comparison is the log,
// *out and the pass and frames afterwards.
#include "difftest.h"
#include "game/game.h"
#include "gz/RenderStates.h"
#include "sg/cShader.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace nGZSceneGraph;

namespace {

std::string gLog;

void Note(const char *what, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s %x %x %x %x\n", what, a, b, c, d);
    gLog += buf;
}

uint32_t Id(const void *p) { return uint32_t(uintptr_t(p)); }

template <int N> struct Pads : Pads<N - 1> {
    virtual void pad(Pads<N> *) {}
};
template <> struct Pads<0> {};

// The device state cache: 11 the state block, 12 a command, 14 the vertex program.
struct FakeState : Pads<11> {
    virtual void ApplyStateBlock(uint32_t block) { Note("st-block", block); } // 11
    virtual void ApplyCommand(uint32_t command)                               // 12
    {
        const auto *c = reinterpret_cast<const nGZGraphic4::cClippingState *>(uintptr_t(command));
        Note("st-command", Id(c->mVTable) - Id(GameAddress<void>(nGZGraphic4::cClippingState::kVTable)), c->mId, c->mSize,
             c->mEnabled);
    }
    virtual void p13() {}
    virtual void SetVertexShader(uint32_t program) { Note("st-vs", program); } // 14
};

// A geometry (cIGeometryRender): 6 prepare, 7 begin, 8 render; every argument as the dword pushed.
struct FakeGeometry : Pads<6> {
    uint32_t id = 0;
    uint32_t prepared = 0, rendered = 0;
    virtual uint32_t Prepare(uint32_t subset, uint32_t ctx, uint32_t decl, uint32_t flag) // 6
    {
        Note("geo-prepare", id ^ subset << 8, ctx, decl, flag);
        return prepared;
    }
    virtual void Begin(uint32_t subset, uint32_t ctx, uint32_t first) { Note("geo-begin", id ^ subset << 8, ctx, first); } // 7
    // The third argument is a byte (cIGeometryRender.h): the image's caller pushes it zero-extended, ours with whatever the
    // register held above it. Every implementation of slot 8 in the image (cGeometryNode 0xa435d0, cDynamicGeometryRect
    // 0xa441b0, the floor / wall / snow / pool water / particle nodes, the terrain, floor and lot skirt overlays and their
    // 0x3b1150) reads only its low byte, so that is what is compared.
    virtual uint32_t Render(uint32_t subset, uint32_t ctx, uint32_t a, uint32_t b) // 8
    {
        Note("geo-render", id ^ subset << 8, ctx, a & 0xff, b);
        return rendered;
    }
};

// A part: 8 maps the frame (the mapper), 11 applies (a controller); any other slot logs that it was called.
ShaderFrame *gMapped = nullptr;
struct FakePart : cIShaderPart {
    uint32_t id = 0;
    void Slot0() override { Note("s0", id); }
    void AddRef() override { Note("s1", id); }
    void Release() override { Note("s2", id); }
    void Reset() override { Note("s3", id); }
    void Slot4() override { Note("s4", id); }
    void Slot5() override { Note("s5", id); }
    void Slot6() override { Note("s6", id); }
    void Slot7() override { Note("s7", id); }
    ShaderFrame *MapFrame(ShaderFrame *frame, ShaderRenderCtx *ctx) override
    {
        Note("map", id, Id(frame), Id(ctx));
        return gMapped;
    }
    const uint32_t *Slot9(void *, const void *) override { Note("s9", id); return nullptr; }
    void Slot10() override { Note("s10", id); }
    void Apply(ShaderRenderCtx *ctx, ShaderFrame *frame) override { Note("apply", id, Id(ctx), Id(frame)); }
    void Slot12(void *, const void *) override { Note("s12", id); }
    void EndState(void *) override { Note("s13", id); }
};

constexpr uint32_t kApplyRvas[8] = {0xaa44d0, 0xaa3d70, 0xaa3e90, 0xaa3f70, 0xaa40a0, 0xaa4180, 0xaa42b0, 0xaa43a0};

void SetVec(ShaderPartVec &v, std::vector<cIShaderPart *> &store)
{
    v.mBegin = store.data();
    v.mEnd = store.data() + store.size();
    v.mCap = v.mEnd;
}

// A byte that is mostly 0 or 1, sometimes another value (the original ors the bytes, it does not test them).
uint8_t Flag(uint32_t r) { return (r & 3) == 0 ? 0 : (r & 3) == 1 ? 1 : (r & 3) == 2 ? 0 : uint8_t(r >> 2); }

void RunApply(difftest::Context &ctx, uint32_t index)
{
    using ApplyFn = ShaderPass::ApplyFn;
    const ApplyFn original = ctx.Original<ApplyFn>(kApplyRvas[index]);
    const ApplyFn ours = ShaderPass::ApplyFor(index);
    const bool preprocess = index & 1, mapped = index & 4;
    std::mt19937 rng(kApplyRvas[index]);
    FakeState state;
    FakeGeometry geoA, geoB;
    geoA.id = 0xa0;
    geoB.id = 0xb0;
    FakePart parts[8];
    for (uint32_t i = 0; i < 8; ++i) parts[i].id = 0x10 + i;
    std::vector<uint8_t> rcMem(0x2c0), frameA(0x40), frameB(0x40);
    ShaderRenderCtx *rc = reinterpret_cast<ShaderRenderCtx *>(rcMem.data());
    ShaderFrame *fa = reinterpret_cast<ShaderFrame *>(frameA.data());
    ShaderFrame *fb = reinterpret_cast<ShaderFrame *>(frameB.data());
    int drawn = 0, skipped = 0, blocks = 0, clipOn = 0;
    for (int iter = 0; iter < 600; ++iter) {
        const uint32_t bits = rng();
        for (auto &b : rcMem) b = uint8_t(rng());
        for (auto &b : frameA) b = uint8_t(rng());
        for (auto &b : frameB) b = uint8_t(rng());
        rc->mState = reinterpret_cast<nGZGraphic4::cIDeviceState *>(&state);
        fa->m0c = Id(&geoA);
        fb->m0c = Id(&geoB);
        gMapped = fb;
        geoA.prepared = (bits & 3) == 0 ? 0xffffffff : rng();
        geoB.prepared = (bits & 0xc) == 0 ? 0xffffffff : rng();
        geoA.rendered = rng();
        geoB.rendered = rng();

        ShaderPass pass;
        std::memset(&pass, 0, sizeof(pass));
        for (uint32_t i = 0; i < sizeof(pass); ++i) reinterpret_cast<uint8_t *>(&pass)[i] = uint8_t(rng());
        pass.mB4 = Flag(rng());
        pass.mB5 = Flag(rng());
        pass.mB6 = Flag(rng());
        pass.mB7 = Flag(rng());
        pass.mB3e = Flag(rng());
        pass.mP8 = &parts[0];
        pass.mPc = reinterpret_cast<cIShaderPart *>(uintptr_t(0x5000 + (rng() & 0xff)));
        pass.mP10 = reinterpret_cast<cIShaderPart *>(uintptr_t(0x6000 + (rng() & 0xff)));
        pass.mP20 = reinterpret_cast<cIShaderPart *>(uintptr_t(0x7000 + (rng() & 0xff)));
        std::vector<cIShaderPart *> v14, v24, v30, v58;
        for (uint32_t n = rng() % 4, i = 0; i < n; ++i) v14.push_back(&parts[1 + rng() % 7]);
        for (uint32_t n = rng() % 4, i = 0; i < n; ++i) v24.push_back(&parts[1 + rng() % 7]);
        SetVec(pass.mV14, v14);
        SetVec(pass.mV24, v24);
        SetVec(pass.mV30, v30);
        SetVec(pass.mV58, v58);
        const uint32_t outIn = (bits & 0x30) == 0 ? 0xffffffff : rng();
        const uint32_t flag = (bits & 0x40) ? rng() & ~0xffu : rng();

        const ShaderPass passIn = pass;
        const std::vector<uint8_t> rcIn = rcMem, faIn = frameA, fbIn = frameB;
        uint32_t outA = outIn;
        gLog.clear();
        original(&pass, rc, fa, &outA, flag);
        const std::string logA = gLog;
        const bool passSame = std::memcmp(&pass, &passIn, sizeof(pass)) == 0;
        const bool rest = rcMem == rcIn && frameA == faIn && frameB == fbIn;
        rcMem = rcIn;
        frameA = faIn;
        frameB = fbIn;
        pass = passIn;
        uint32_t outB = outIn;
        gLog.clear();
        ours(&pass, rc, fa, &outB, flag);
        ++ctx.cases;
        const bool passSameB = std::memcmp(&pass, &passIn, sizeof(pass)) == 0;
        const bool restB = rcMem == rcIn && frameA == faIn && frameB == fbIn;
        if (logA != gLog || outA != outB || passSame != passSameB || rest != restB)
            ctx.Fail("index %u iter %d: out %x / %x, pass kept %d / %d, rest kept %d / %d; original\n%sours\n%s", index, iter, outA,
                     outB, passSame, passSameB, rest, restB, logA.c_str(), gLog.c_str());
        const uint32_t seen = preprocess ? (mapped ? geoB.prepared : geoA.prepared) : outIn;
        if (seen == 0xffffffff) ++skipped;
        else ++drawn;
        if (logA.find("st-block") != std::string::npos) ++blocks;
        if (logA.find("st-command 0 3 c 0") == std::string::npos && logA.find("st-command") != std::string::npos) ++clipOn;
    }
    if (drawn < 50 || skipped < 50 || blocks < 50 || clipOn < 50)
        ctx.Fail("coverage: drawn %d skipped %d state blocks %d clipping on %d", drawn, skipped, blocks, clipOn);
}

} // namespace

DIFF_TEST(SG_ShaderPassApply0, 0xaa44d0) { RunApply(ctx, 0); }
DIFF_TEST(SG_ShaderPassApply1, 0xaa3d70) { RunApply(ctx, 1); }
DIFF_TEST(SG_ShaderPassApply2, 0xaa3e90) { RunApply(ctx, 2); }
DIFF_TEST(SG_ShaderPassApply3, 0xaa3f70) { RunApply(ctx, 3); }
DIFF_TEST(SG_ShaderPassApply4, 0xaa40a0) { RunApply(ctx, 4); }
DIFF_TEST(SG_ShaderPassApply5, 0xaa4180) { RunApply(ctx, 5); }
DIFF_TEST(SG_ShaderPassApply6, 0xaa42b0) { RunApply(ctx, 6); }
DIFF_TEST(SG_ShaderPassApply7, 0xaa43a0) { RunApply(ctx, 7); }
