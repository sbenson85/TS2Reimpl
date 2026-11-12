#include "sg/cBlurBrush.h"

#include "game/game.h"
#include "gz/GraphicServices.h"
#include "gz/RenderStates.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cTextureDX.h"
#include "sg/cTextureImage.h"

#include <new>

namespace nTSSG {

using namespace nGZGraphic4;

namespace {

constexpr uint32_t kStride = 5;          // words per 20-byte vertex
constexpr uint32_t kStateBlockKind = 3;  // CreateStateBlock's first argument
constexpr uint32_t kClearAll = 0xffff;   // Clear's flags: every buffer
constexpr uint32_t kClearColourCell = 0x10da49c; // the game global (four floats) the target is cleared to
constexpr uint8_t kDepthTestId = 0x14, kDepthFunctionId = 0x15, kMipFilterId = 0x1f, kTextureBlendId = 0x19,
                  kAlphaBlendId = 0x11;

// A render state command for the state cache (zeroed, the state's game vtable, id and size).
template <class State> State Command(uint8_t id, void *vtable)
{
    State state{};
    state.mVTable = vtable;
    state.mId = id;
    state.mSize = static_cast<uint16_t>(sizeof(State));
    return state;
}

void ReleaseBlock(cIDeviceResource *&block)
{
    if (cIDeviceResource *const old = block) {
        block = nullptr;
        old->Release();
    }
}

nGZSceneGraph::cITexture *TextureOf(const nGZSceneGraph::cTextureHandle &handle)
{
    return static_cast<nGZSceneGraph::cITexture *>(handle.Get());
}

} // namespace

cBlurBrush::cBlurBrush() = default;

cBlurBrush::~cBlurBrush()
{
    // The block is released but not cleared; the texture handle's destructor follows.
    if (mStateBlock) mStateBlock->Release();
}

bool cBlurBrush::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kIidBrush) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cBlurBrush::AddRef() { return ++mRefCount; }

uint32_t cBlurBrush::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

uint32_t cBlurBrush::GetStateBlock(cIDraw2D *draw2d, cIDeviceDX *device)
{
    if (mStateBlock && mStateBlock->IsCreated()) return uint32_t(uintptr_t(mStateBlock));
    cIDeviceState *const state = device->GetStateConst();
    draw2d->ApplyDrawState();
    if (mTexture.mId != nGZSceneGraph::cTextureHandle::kNone) {
        // ORIGINAL-BUG: a valid id the manager has no texture for (Get null) is dereferenced.
        nGZSceneGraph::cITexture *const texture = TextureOf(mTexture);
        if (!texture->IsCreated()) texture->Restore();
        if (!texture->IsCreated()) {
            ReleaseBlock(mStateBlock);
            return 0;
        }
        state->SetTexture(0, reinterpret_cast<cIGZUnknown *>(texture->GetDeviceTexture(0)));
    }
    auto depth = Command<cDepthTestState>(kDepthTestId, GameAddress<void>(cDepthTestState::kVTable));
    state->ApplyStateCommand(uint32_t(uintptr_t(&depth)));
    auto function = Command<cDepthTestFunction>(kDepthFunctionId, GameAddress<void>(cDepthTestFunction::kVTable));
    function.mFunction = 7;
    state->ApplyStateCommand(uint32_t(uintptr_t(&function)));
    auto mip = Command<cTextureMipFilterState>(kMipFilterId, GameAddress<void>(cTextureMipFilterState::kVTable));
    state->ApplyStateCommand(uint32_t(uintptr_t(&mip)));
    auto blend = Command<cTextureBlendState>(kTextureBlendId, GameAddress<void>(cTextureBlendState::kVTable));
    blend.mColorArg1 = 3;
    blend.mColorArg2 = 2;
    blend.mColorArg0 = 3;
    blend.mAlphaArg1 = 3;
    blend.mAlphaArg2 = 2;
    blend.mAlphaArg0 = 3;
    state->ApplyStateCommand(uint32_t(uintptr_t(&blend)));
    auto alpha = Command<cAlphaBlendState>(kAlphaBlendId, GameAddress<void>(cAlphaBlendState::kVTable));
    alpha.mEnabled = 1;
    alpha.mSource = 1;
    alpha.mDest = 1;
    state->ApplyStateCommand(uint32_t(uintptr_t(&alpha)));
    ReleaseBlock(mStateBlock);
    state->CreateStateBlock(kStateBlockKind, uint32_t(uintptr_t(&mStateBlock)));
    return uint32_t(uintptr_t(mStateBlock));
}

void cBlurBrush::Draw(cIDraw2D *draw2d, uint32_t, uint32_t count, const float *points, const float *)
{
    cIDeviceDX *const device = GetCurrentDeviceCached();
    if (!device || !device->IsDeviceReady() || !mStateBlock) return;
    device->GetStateConst();
    device->Clear(kClearAll, GameAddress<const float>(kClearColourCell), 1.0f, 0);
    const float spanX = points[4] - points[0];
    const float spanY = points[5] - points[1];
    uint32_t width = 0, height = 0;
    if (mTexture.mId != nGZSceneGraph::cTextureHandle::kNone) {
        nGZSceneGraph::cITexture *const texture = TextureOf(mTexture);
        width = texture->GetDeviceTexture(0)->GetWidth(0);
        height = texture->GetDeviceTexture(0)->GetHeight(0);
    }
    const int32_t side = mRadius * 2 + 1;
    int32_t passes = side * side;
    int32_t left = 0xff;
    const int32_t n = int32_t(count);
    for (int32_t i = -mRadius; i <= mRadius; ++i) {
        for (int32_t j = -mRadius; j <= mRadius; ++j) {
            const int32_t share = left / passes;
            left -= share;
            --passes;
            const uint32_t colour = (uint32_t(share) & 0xff) * 0x01010101u;
            float *positions;
            uint32_t *colours;
            float *uvs;
            uint16_t *indices;
            if (!draw2d->LockBatch(count, uint32_t((n - 2) * 3), &positions, &colours, &uvs, &indices)) continue;
            float offsetU = 0.0f, offsetV = 0.0f, scaleU = 1.0f, scaleV = 1.0f;
            if (mTexture.mId != nGZSceneGraph::cTextureHandle::kNone) {
                scaleU = mScaleX / spanX;
                offsetU = ((mOffsetX - mScaleX * mOriginX) + float(i)) / float(width);
                scaleV = mScaleY / spanY;
                offsetV = ((mOffsetY - mScaleY * mOriginY) + float(j)) / float(height);
            }
            for (int32_t k = 0; k < n; ++k) {
                const float x = points[k * 2];
                const float y = points[k * 2 + 1];
                positions[0] = x;
                positions[1] = y;
                *colours = colour;
                uvs[0] = scaleU * x + offsetU;
                uvs[1] = scaleV * y + offsetV;
                positions += kStride;
                colours += kStride;
                uvs += kStride;
            }
            for (int32_t t = 0; t < n - 2; ++t) {
                *indices++ = 0;
                *indices++ = uint16_t(t + 1);
                *indices++ = uint16_t(t + 2);
            }
            draw2d->DrawBatch();
        }
    }
}

void cBlurBrush::SetOrigin(float x, float y)
{
    mOriginX = x;
    mOriginY = y;
}

void cBlurBrush::SetOffset(float x, float y)
{
    mOffsetX = x;
    mOffsetY = y;
}

void cBlurBrush::SetScale(float x, float y)
{
    mScaleX = x;
    mScaleY = y;
}

void cBlurBrush::SetTexture(uint32_t textureId)
{
    {
        const nGZSceneGraph::cTextureHandle other(textureId);
        mTexture = other;
    }
    ReleaseBlock(mStateBlock);
}

void cBlurBrush::SetRadius(int32_t radius) { mRadius = radius; }

cBlurBrush *__fastcall cBlurBrushAdapters::Construct(cBlurBrush *memory, void *) { return new (memory) cBlurBrush; }

void __fastcall cBlurBrushAdapters::Destruct(cBlurBrush *self, void *) { self->cBlurBrush::~cBlurBrush(); }

} // namespace nTSSG
