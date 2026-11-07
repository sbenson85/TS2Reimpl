// nGZSceneGraph::cApplyRenderTarget (lane TM, scenegraph M3a): see cApplyRenderTarget.h.
#include "sg/cApplyRenderTarget.h"

#include "game/game.h"
#include "gz/RenderStates.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceStateDX.h"
#include "rz/cRZStd.h"
#include "sg/SGResourceBlockers.h" // cInvalidDwordTag::IsInvalid
#include "sg/cStringPool.h"        // JenkinsHash
#include "sg/cRenderTargetTexture.h" // cIRenderTargetTexture
#include "sg/cTextureImage.h"      // cITexture

#include <cstring>

namespace nGZSceneGraph {

namespace {
using nGZGraphic4::cRenderTargetsState;
using nGZGraphic4::cViewportState;

constexpr uint8_t kRenderTargetsId = 0;
constexpr uint8_t kViewportId = 1;

// What Init's first argument answers (slot 21 with 1: the device's render target count).
class cIRenderTargetCaps {
public:
    virtual void Slot0() = 0;
    virtual void Slot1() = 0;
    virtual void Slot2() = 0;
    virtual void Slot3() = 0;
    virtual void Slot4() = 0;
    virtual void Slot5() = 0;
    virtual void Slot6() = 0;
    virtual void Slot7() = 0;
    virtual void Slot8() = 0;
    virtual void Slot9() = 0;
    virtual void Slot10() = 0;
    virtual void Slot11() = 0;
    virtual void Slot12() = 0;
    virtual void Slot13() = 0;
    virtual void Slot14() = 0;
    virtual void Slot15() = 0;
    virtual void Slot16() = 0;
    virtual void Slot17() = 0;
    virtual void Slot18() = 0;
    virtual void Slot19() = 0;
    virtual void Slot20() = 0;
    virtual uint32_t RenderTargetCount(uint32_t which) = 0; // 21
};

// cRZStd's report slot (vtable 0xda3e6c slot 2), as cShader.cpp views it.
struct ReportView {
    virtual void __cdecl Slot0(const char *, ...) = 0;
    virtual void Slot1() = 0;
    virtual void __cdecl Report(const char *group, uint32_t level, const char *format, ...) = 0;
};

constexpr const char kControllersFile[] =
    "C:\\dev\\sims2remaster\\DL\\GZFrame\\SceneDB\\GZSceneGraph\\source\\GZSceneGraphShaderControllers.cpp";

cRZStd *Reporter(cRZStd &raw, uint32_t line)
{
    return raw.Construct(8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kControllersFile)), line);
}

void Say(uint32_t line, const char *format)
{
    cRZStd raw;
    reinterpret_cast<ReportView *>(Reporter(raw, line))->Report("Shader validation", 4u, format);
}

template <class State> State Command(uint8_t id, const void *vtable)
{
    State state{};
    state.mVTable = vtable;
    state.mId = id;
    state.mSize = static_cast<uint16_t>(sizeof(State));
    return state;
}

cRenderTargetsState TargetsCommand()
{
    return Command<cRenderTargetsState>(kRenderTargetsId, cRenderTargetsState::VTable());
}

cViewportState ViewportCommand(const uint32_t *viewport)
{
    auto command = Command<cViewportState>(kViewportId, cViewportState::VTable());
    std::memcpy(&command.mX, viewport, 16);
    return command;
}

void Send(nGZGraphic4::cIDeviceState *state, const void *command)
{
    state->ApplyStateCommand(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(command)));
}

bool IsInvalid(const cTextureHandle &handle)
{
    return reinterpret_cast<const SGResourceBlockers::cInvalidDwordTag *>(&handle)->IsInvalid() != 0;
}

// The first `count` of the context's surfaces (at most the command's four: the original copies `count` words into its
// stack command, but count is the smaller of the device's render target count and the context's, both at most 4 under
// Direct3D 9), with the context's depth.
void FillContextTargets(cRenderTargetsState &command, const cShaderApplyContext *ctx, uint32_t count)
{
    command.mDepthTarget = ctx->mDepth;
    if (count > 4) count = 4;
    for (uint32_t i = 0; i < count; ++i) command.mTargets.surface[i] = ctx->mSurfaces[i];
}
} // namespace

cIGZUnknown *__cdecl cApplyRenderTarget::Create() { return new cApplyRenderTarget; }

cApplyRenderTarget::~cApplyRenderTarget() = default;

void cApplyRenderTarget::SendTargets(const cShaderApplyContext *ctx) const
{
    if (mKind == kTargetPrimary) {
        auto command = TargetsCommand();
        command.mDepthTarget = ctx->mPrimaryDepth;
        command.mTargets.surface[0] = ctx->mPrimarySurface;
        Send(ctx->mState, &command);
        return;
    }
    if (mKind == kTargetContext) {
        const uint32_t count = ctx->mSurfaceCount;
        auto command = TargetsCommand();
        if (mIndex == kAllSurfaces) {
            FillContextTargets(command, ctx, mMaxTargets < count ? mMaxTargets : count);
        } else {
            if (count <= mIndex) return;
            command.mTargets.surface[0] = ctx->mSurfaces[mIndex];
            command.mDepthTarget = ctx->mDepth;
        }
        Send(ctx->mState, &command);
        return;
    }
    cIGZUnknown *const texture = mTexture.Get();
    cIRenderTargetTexture *target = nullptr;
    if (texture) texture->QueryInterface(cIRenderTargetTexture::kIid, reinterpret_cast<void **>(&target));
    target->SetAsTargets(ctx->mState, static_cast<int32_t>(mIndex)); // no null check in the original
    if (target) target->Release();
}

void cApplyRenderTarget::SendViewport(const cShaderApplyContext *ctx) const
{
    const uint32_t *viewport;
    if (mViewportMode == kViewportContext) {
        viewport = ctx->mViewport;
    } else if (mViewportMode == kViewportOuter) {
        viewport = ctx->mOuter ? ctx->mOuter->mViewport : ctx->mViewport;
    } else if (mViewportMode == kViewportUnit) {
        static constexpr uint32_t kUnit[4] = {0, 0, 0x3f800000u, 0x3f800000u};
        viewport = kUnit;
    } else {
        return;
    }
    // MinZ and MaxZ (+0x18, +0x1c) are left as the stack had them in the original; ours zero.
    auto command = ViewportCommand(viewport);
    Send(ctx->mState, &command);
}

void cApplyRenderTarget::Reset()
{
    if (!IsInvalid(mTexture)) static_cast<cITexture *>(mTexture.Get())->ReleaseResource();
}

bool cApplyRenderTarget::Init(void *caps, void *key, void *table, void *reportArg)
{
    const bool report = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(reportArg)) != 0;
    mMaxTargets = static_cast<cIRenderTargetCaps *>(caps)->RenderTargetCount(1);
    const auto base = [&] { return cShaderControllerT<cIApplyRenderTarget>::Init(caps, key, table, reportArg); };
    if (mKind == kTargetPrimary) {
        if (mIndex == 0) return base();
        if (report) Say(0x74b, "...failed -- compositing target only has one render target surface\n");
        return false;
    }
    if (mKind == kTargetContext) {
        if (mIndex <= mMaxTargets) return base();
        if (report) Say(0x73f, "...failed -- out of range surface index\n");
        return false;
    }
    if (IsInvalid(mTexture)) {
        if (report) Say(0x757, "...failed -- render target texture handle is invalid\n");
        return false;
    }
    auto *const texture = static_cast<cITexture *>(mTexture.Get()); // no null check in the original
    cIRenderTargetTexture *target = nullptr;
    if (!texture->QueryInterface(cIRenderTargetTexture::kIid, reinterpret_cast<void **>(&target))) {
        if (report) Say(0x763, "...failed -- texture handle does not identify a render target texture\n");
    } else {
        texture->Restore();
        if (!target->IsCreated()) {
            if (report) Say(0x76c, "...failed -- render target texture not initialized\n");
        } else {
            const uint32_t surfaces = target->GetDeviceTextureCount();
            if (mIndex == kAllSurfaces && surfaces > mMaxTargets) {
                if (report) {
                    cRZStd raw;
                    reinterpret_cast<ReportView *>(Reporter(raw, 0x774))
                        ->Report("Shader validation", 4u,
                                 "...failed -- render target texture has more RT surfaces (%d) than the device supports "
                                 "(%d)\n",
                                 surfaces, mMaxTargets);
                }
            } else if (mIndex <= surfaces) {
                // ORIGINAL-BUG: the unsigned test fails every index of -1 (all surfaces) that fits the device.
                if (target) target->Release();
                return base();
            } else if (report) {
                Say(0x77b, "...failed -- out of range surface index\n");
            }
        }
    }
    if (target) target->Release();
    return false;
}

uint32_t cApplyRenderTarget::Slot10() { return (mRestore ? 2u : 0u) + 1u; }

void cApplyRenderTarget::Apply(void *ctxArg, void *)
{
    const auto *const ctx = static_cast<const cShaderApplyContext *>(ctxArg);
    SendTargets(ctx);
    SendViewport(ctx);
    if (mClearColor || mClearDepth) {
        const uint32_t flags = (mClearColor ? 1u : 0u) | (mClearDepth ? 6u : 0u);
        ctx->mDevice->Clear(flags, mColor, mZ, mStencil);
    }
}

void cApplyRenderTarget::EndState(void *ctxArg)
{
    const auto *const ctx = static_cast<const cShaderApplyContext *>(ctxArg);
    auto targets = TargetsCommand();
    const uint32_t count = ctx->mSurfaceCount;
    FillContextTargets(targets, ctx, mMaxTargets < count ? mMaxTargets : count);
    Send(ctx->mState, &targets);
    auto viewport = ViewportCommand(ctx->mViewport);
    Send(ctx->mState, &viewport);
}

void cApplyRenderTarget::SetTexture(const char *name, uint32_t index)
{
    cTextureHandle texture(name);
    mTexture.Assign(&texture);
    mIndex = index;
}

void cApplyRenderTarget::SetTarget(uint32_t kind, uint32_t index)
{
    mKind = kind;
    mIndex = index;
}

void cApplyRenderTarget::SetClearColor(bool on, const float *color)
{
    mClearColor = on;
    std::memcpy(mColor, color, sizeof mColor);
}

void cApplyRenderTarget::SetClearDepth(bool on, float z, uint32_t stencil)
{
    mClearDepth = on;
    mZ = z;
    mStencil = stencil;
}

uint32_t cApplyRenderTarget::HashParams() { return JenkinsHash(&mTexture, 0x28, 0); }

bool cApplyRenderTarget::EqualParams(cIApplyRenderTarget *other)
{
    return std::memcmp(&mTexture, &static_cast<cApplyRenderTarget *>(other)->mTexture, 0x28) == 0;
}

} // namespace nGZSceneGraph
