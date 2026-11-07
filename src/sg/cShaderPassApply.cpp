// The eight pass apply functions (0xaa44d0, 0xaa3d70, 0xaa3e90, 0xaa3f70, 0xaa40a0, 0xaa4180, 0xaa42b0, 0xaa43a0): one
// body, the three choices Setup makes for a pass as template arguments. See src/sg/cShader.h.
#include "sg/cShader.h"

#include "game/game.h"
#include "gz/RenderStates.h"
#include "gz/cDeviceStateDX.h"
#include "sg/cIGeometryRender.h"

#include <cstdint>
#include <cstring>

namespace nGZSceneGraph {

using nGZGraphic4::cClippingState;
using nGZGraphic4::cIDeviceState;

namespace {

constexpr uint8_t kClippingId = 3;
constexpr uint32_t kNoDraw = 0xffffffff; // *out when the geometry has nothing to draw

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

cIGeometryRender *Geometry(const ShaderFrame *frame) { return reinterpret_cast<cIGeometryRender *>(frame->m0c); }

cGeometryRenderContext *DrawContext(ShaderRenderCtx *ctx) { return reinterpret_cast<cGeometryRenderContext *>(ctx); }

void ApplyAll(const ShaderPartVec &parts, ShaderRenderCtx *ctx, ShaderFrame *frame)
{
    for (cIShaderPart **p = parts.mBegin; p != parts.mEnd; ++p) (*p)->Apply(ctx, frame);
}

} // namespace

template <bool kPreprocess, bool kHardware, bool kMapped>
void ShaderPass::Apply(ShaderRenderCtx *ctx, ShaderFrame *frame, uint32_t *out, uint32_t flag)
{
    if constexpr (kMapped) frame = mP8->MapFrame(frame, ctx);
    if constexpr (kPreprocess) {
        ctx->mState->SetVertexShader(reinterpret_cast<cIGZUnknown *>(mPc));
        ApplyAll(mV14, ctx, frame);
        *out = Geometry(frame)->Prepare(frame->mSubset, Word(ctx), Word(mP10), mB6);
    }
    if (*out == kNoDraw) return;
    if (static_cast<uint8_t>(flag) || mB4) ctx->mState->ApplyStateBlock(Word(mP20));
    cClippingState clipping;
    std::memset(&clipping, 0, sizeof(clipping));
    clipping.mVTable = cClippingState::VTable();
    clipping.mId = kClippingId;
    clipping.mSize = static_cast<uint16_t>(sizeof(cClippingState));
    clipping.mEnabled = static_cast<uint8_t>(((frame->m16 >> 1) & 1) | mB3e | mB6);
    ctx->mState->ApplyStateCommand(Word(&clipping));
    ApplyAll(mV24, ctx, frame);
    if constexpr (kHardware)
        *out = Geometry(frame)->Render(frame->mSubset, DrawContext(ctx), mB6, mB7);
    else
        Geometry(frame)->Begin(frame->mSubset, DrawContext(ctx), *out);
}

template void ShaderPass::Apply<false, false, false>(ShaderRenderCtx *, ShaderFrame *, uint32_t *, uint32_t);
template void ShaderPass::Apply<true, false, false>(ShaderRenderCtx *, ShaderFrame *, uint32_t *, uint32_t);
template void ShaderPass::Apply<false, true, false>(ShaderRenderCtx *, ShaderFrame *, uint32_t *, uint32_t);
template void ShaderPass::Apply<true, true, false>(ShaderRenderCtx *, ShaderFrame *, uint32_t *, uint32_t);
template void ShaderPass::Apply<false, false, true>(ShaderRenderCtx *, ShaderFrame *, uint32_t *, uint32_t);
template void ShaderPass::Apply<true, false, true>(ShaderRenderCtx *, ShaderFrame *, uint32_t *, uint32_t);
template void ShaderPass::Apply<false, true, true>(ShaderRenderCtx *, ShaderFrame *, uint32_t *, uint32_t);
template void ShaderPass::Apply<true, true, true>(ShaderRenderCtx *, ShaderFrame *, uint32_t *, uint32_t);

namespace {
template <typename T> ShaderPass::ApplyFn AsApplyFn(T pmf)
{
    static_assert(sizeof(T) == sizeof(ShaderPass::ApplyFn), "single-inheritance members only");
    ShaderPass::ApplyFn fn;
    std::memcpy(&fn, &pmf, sizeof(fn));
    return fn;
}
} // namespace

ShaderPass::ApplyFn ShaderPass::ApplyFor(uint32_t index)
{
    switch (index) {
    case 0: return AsApplyFn(&ShaderPass::Apply<false, false, false>); // 0xaa44d0
    case 1: return AsApplyFn(&ShaderPass::Apply<true, false, false>);  // 0xaa3d70
    case 2: return AsApplyFn(&ShaderPass::Apply<false, true, false>);  // 0xaa3e90
    case 3: return AsApplyFn(&ShaderPass::Apply<true, true, false>);   // 0xaa3f70
    case 4: return AsApplyFn(&ShaderPass::Apply<false, false, true>);  // 0xaa40a0
    case 5: return AsApplyFn(&ShaderPass::Apply<true, false, true>);   // 0xaa4180
    case 6: return AsApplyFn(&ShaderPass::Apply<false, true, true>);   // 0xaa42b0
    default: return AsApplyFn(&ShaderPass::Apply<true, true, true>);   // 0xaa43a0
    }
}

} // namespace nGZSceneGraph
