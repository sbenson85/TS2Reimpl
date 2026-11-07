#include "sg/cDefaultDisplayListRenderStrategy.h"

#include "game/game.h"
#include "gz/RenderStates.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceStateDX.h"
#include "sg/cDisplayList.h"
#include "sg/cIViewerRefNode.h"

#include <cstring>

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kUnknownIid = 1;
constexpr uint32_t kReadyReset = 8;           // the device state's reset when the view has no handle texture
constexpr uint8_t kViewportId = 1;
constexpr uint8_t kDepthRangeId = 2;
constexpr uint32_t kClearTarget = 1;          // D3DCLEAR_TARGET (the clear info's bit 0)
constexpr uint32_t kClearDepthStencil = 6;    // D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL (the clear info's bit 1)
constexpr float kClearColor[4] = {1.0f, 0.0f, 0.0f, 0.0f};

// The view's handle texture (the viewer's slot 109): slot 12 readies the device state for drawing into it.
class cIViewHandleTexture : public cIGZUnknown {
public:
    virtual void Slot3() = 0;
    virtual void Slot4() = 0;
    virtual void Slot5() = 0;
    virtual void Slot6() = 0;
    virtual void Slot7() = 0;
    virtual void Slot8() = 0;
    virtual void Slot9() = 0;
    virtual void Slot10() = 0;
    virtual void Slot11() = 0;
    virtual void PrepareDeviceState(nGZGraphic4::cIDeviceState *state, uint32_t flags) = 0; // 12
};

// An item's material variant (the material manager's GetVariant): slot 11 draws the item in the pass.
class cIDrawItemVariant : public cIGZUnknown {
public:
    virtual void Slot3() = 0;
    virtual void Slot4() = 0;
    virtual void Slot5() = 0;
    virtual void Slot6() = 0;
    virtual void Slot7() = 0;
    virtual void Slot8() = 0;
    virtual void Slot9() = 0;
    virtual void Slot10() = 0;
    virtual void Draw(Pass *pass, DrawItem *item) = 0; // 11
};

template <class State> State Command(uint8_t id, const void *vtable)
{
    State state{};
    state.mVTable = vtable;
    state.mId = id;
    state.mSize = static_cast<uint16_t>(sizeof(State));
    return state;
}

void Apply(nGZGraphic4::cIDeviceState *state, const void *command)
{
    state->ApplyStateCommand(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(command)));
}

} // namespace

bool cDefaultDisplayListRenderStrategy::QueryInterface(uint32_t iid, void **out)
{
    if (!out || (iid != kUnknownIid && iid != kIid)) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cDefaultDisplayListRenderStrategy::AddRef()
{
    return ++mRefCount;
}

uint32_t cDefaultDisplayListRenderStrategy::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

void cDefaultDisplayListRenderStrategy::BeginPass(Pass *)
{
}

void cDefaultDisplayListRenderStrategy::Render(Pass *pass, DrawItem *items, uint32_t count)
{
    // Through the interface: the pass's device state may be any implementation.
    nGZGraphic4::cIDeviceState *const state = pass->mDeviceState;
    if (pass->mHandleTexture)
        static_cast<cIViewHandleTexture *>(pass->mHandleTexture)->PrepareDeviceState(state, 0);
    else
        state->Reset(kReadyReset);
    state->SetProjection(reinterpret_cast<const D3DMATRIX *>(pass->mMatrices[0]));

    // The viewport as the four fractions' bits (the state's position and size words); its depth words stay 0.
    nGZGraphic4::cViewportState viewport = Command<nGZGraphic4::cViewportState>(kViewportId, nGZGraphic4::cViewportState::VTable());
    std::memcpy(&viewport.mX, pass->mViewportMin, sizeof(pass->mViewportMin));
    std::memcpy(&viewport.mWidth, pass->mViewportMax, sizeof(pass->mViewportMax));
    Apply(state, &viewport);
    nGZGraphic4::cDepthRangeState depth = Command<nGZGraphic4::cDepthRangeState>(kDepthRangeId, nGZGraphic4::cDepthRangeState::VTable());
    depth.mMin = pass->mDepthRange[0];
    depth.mMax = pass->mDepthRange[1];
    Apply(state, &depth);

    cViewerClearInfo clear{};
    std::memcpy(clear.mColor, kClearColor, sizeof(kClearColor));
    pass->mViewer->GetClearInfo(&clear);
    const uint32_t flags = ((clear.mFlags & 2) ? kClearDepthStencil : 0) | (clear.mFlags & kClearTarget);
    pass->mDevice->Clear(flags, clear.mColor, clear.mDepth, clear.mStencil);

    pass->mDrawStats[0] += count;
    for (DrawItem *item = items, *const end = items + count; item != end; ++item)
        static_cast<cIDrawItemVariant *>(item->mMaterial)->Draw(pass, item);
}

} // namespace nGZSceneGraph
