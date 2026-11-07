// nGZSceneGraph::cRenderTargetTexture (lane TN): see cRenderTargetTexture.h.
#include "sg/cRenderTargetTexture.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/DeviceTargetSize.h"
#include "gz/GraphicModeSearch.h" // FindClosestColorFormat, FindClosestDepthFormat, FindFormatLowerBound
#include "gz/GraphicServices.h"
#include "gz/RenderStates.h" // cRenderTargetsState
#include "gz/cDeviceDX.h"
#include "gz/cDeviceStateDX.h" // cIDeviceState
#include "gz/cGZEventReflector.h" // ReleaseIteratorRange
#include "gz/cIGZCOM.h"
#include "gz/cSurfaceDX.h"
#include "gz/cTextureDX.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "sg/TextureDescription.h" // AdjustTextureSize

#include <bit>

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kTargetClass = 0xbb892198; // the director's classes: a colour target (a device texture) ...
constexpr uint32_t kTargetIid = 0x5b891e1e;
constexpr uint32_t kDepthClass = 0x7b75b73a; // ... and the depth-stencil surface
constexpr uint32_t kDepthIid = 0xfb75b72b;
constexpr uint32_t kTargets = 4;

} // namespace

cRenderTargetTexture *__cdecl cRenderTargetTexture::New() { return new cRenderTargetTexture(); }

cRenderTargetTexture::cRenderTargetTexture()
{
    for (unsigned i = 0; i < 4; ++i) mClear[i] = std::bit_cast<float>(ts2_rva_10f675c[i]);
    if (!nGZGraphic4::GetCurrentDeviceCached()) return;
    // Each target is made by the director's COM, asked for before the old pointer (null here) goes, as the original.
    for (auto &target : mTargets) {
        auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
        if (nGZGraphic4::cITexture *const old = target) {
            target = nullptr;
            old->Release();
        }
        com->GetClassObject(kTargetClass, kTargetIid, reinterpret_cast<void **>(&target));
    }
    auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    if (nGZGraphic4::cIDepthStencilSurface *const old = mDepth) {
        mDepth = nullptr;
        old->Release();
    }
    com->GetClassObject(kDepthClass, kDepthIid, reinterpret_cast<void **>(&mDepth));
}

cRenderTargetTexture::~cRenderTargetTexture()
{
    if (mDepth) mDepth->Release();
    for (unsigned i = kTargets; i-- > 0;) { // the array's destructor: last first, each cleared before its release
        if (nGZGraphic4::cITexture *const target = mTargets[i]) {
            mTargets[i] = nullptr;
            target->Release();
        }
    }
}

bool cRenderTargetTexture::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kIid && riid != kIidTexture) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cRenderTargetTexture::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

// ORIGINAL-BUG: the four targets and the depth surface are released without a null check (a texture made without a
// device has none), and all four go whatever the count.
void cRenderTargetTexture::ReleaseResource()
{
    for (auto *target : mTargets) target->ReleaseResource();
    mDepth->ReleaseResource();
}

bool cRenderTargetTexture::IsCreated()
{
    if (mCount == 0) return false;
    for (uint32_t i = 0; i < mCount; ++i)
        if (!mTargets[i]->IsCreated()) return false;
    // ORIGINAL-BUG: the depth surface is asked without a null check.
    if ((mFlags & kFlagDepth) && !mDepth->IsCreated()) return false;
    return true;
}

nGZGraphic4::cITextureBase *cRenderTargetTexture::GetDeviceTexture(uint32_t index)
{
    return index < mCount ? mTargets[index] : nullptr;
}

// Slots 9-11 release what exists, store the new description and make the resources (Restore).
void cRenderTargetTexture::Create(int32_t width, int32_t height, int32_t formatBits, int32_t alphaBits,
                                  int32_t stencilBits, uint32_t count, const float *clear, uint32_t flags)
{
    ReleaseResource();
    mWidth = width;
    mHeight = height;
    mFormatBits = formatBits;
    mAlphaBits = alphaBits;
    // ORIGINAL-BUG: the depth bits follow the flags the texture had before, not the new ones.
    mDepthBits = (mFlags & kFlagDepth) ? 0 : 0x18;
    mScaleX = 0;
    mStencilBits = stencilBits;
    mFlags = flags;
    mCount = count;
    mScaleY = 0;
    mAspect = 1.0f;
    for (unsigned i = 0; i < 4; ++i) mClear[i] = clear[i];
    Restore();
}

void cRenderTargetTexture::CreateSimple(int32_t width, int32_t height, int32_t formatBits)
{
    ReleaseResource();
    mWidth = width;
    mHeight = height;
    mFormatBits = formatBits;
    mAlphaBits = 0;
    mDepthBits = 0x18;
    mStencilBits = 8;
    mScaleX = 0;
    mScaleY = 0;
    mFlags = kFlagDepth | kFlagFloat;
    mCount = 1;
    mAspect = 1.0f;
    for (unsigned i = 0; i < 4; ++i) mClear[i] = std::bit_cast<float>(ts2_rva_10f675c[i]);
    Restore();
}

void cRenderTargetTexture::CreateRelative(float scaleX, float scaleY, int32_t formatBits, int32_t alphaBits,
                                          int32_t stencilBits, uint32_t count, const float *clear, uint32_t flags)
{
    ReleaseResource();
    mFormatBits = formatBits;
    mAlphaBits = alphaBits;
    // ORIGINAL-BUG: as Create, the depth bits follow the flags before this call.
    mDepthBits = (mFlags & kFlagDepth) ? 0 : 0x18;
    mScaleX = scaleX;
    mStencilBits = stencilBits;
    mWidth = 0;
    mFlags = flags | kFlagRelative;
    mCount = count;
    mHeight = 0;
    mScaleY = scaleY;
    mAspect = 1.0f;
    for (unsigned i = 0; i < 4; ++i) mClear[i] = clear[i];
    Restore();
}

void cRenderTargetTexture::Clear(const int32_t *rect, const float *colour, float depth, uint32_t stencil)
{
    ClearWith(0xffff, rect, colour, depth, stencil);
}

void cRenderTargetTexture::ClearColour(const int32_t *rect, const float *colour)
{
    ClearWith(1, rect, colour, 1.0f, 0);
}

void cRenderTargetTexture::ClearOwn(const int32_t *rect, float depth, uint32_t stencil)
{
    ClearWith(6, rect, mClear, depth, stencil);
}

nGZGraphic4::cIImageSurface *cRenderTargetTexture::GetTargetSurface(uint32_t index)
{
    // ORIGINAL-BUG: the index is not checked against the count (or the array).
    nGZGraphic4::cITexture *const target = mTargets[index];
    if (target && target->IsCreated()) return target->GetTargetSurface(0);
    return nullptr;
}

nGZGraphic4::cIDepthStencilSurface *cRenderTargetTexture::GetDepthSurface()
{
    // ORIGINAL-BUG: the depth surface is asked without a null check. The answer is held while it is looked at.
    if (!mDepth->IsCreated()) return nullptr;
    nGZGraphic4::cIDepthStencilSurface *const held = mDepth;
    if (held) held->AddRef();
    if (held) held->Release();
    return held;
}

namespace {

using nGZGraphic4::cIDeviceEnum;
using nGZGraphic4::cIPixelFormat;
using nGZGraphic4::cPixelFormatRange;
using nGZGraphic4::cRenderTargetsState;

// The usage words Restore asks the enumeration's formats for.
constexpr uint32_t kUsageFloatColour = 8, kUsageColour = 7, kUsageDepth = 0xb;
constexpr uint8_t kRenderTargetsId = 0;    // the render-targets state's command id
constexpr float kRoundingBias = 12582912.0f; // 1.5 * 2^23 (bits 0x4b400000)

// The range's copy constructor (0x167180): the cursor cloned and counted, then the ends counted.
cPixelFormatRange CopyRange(const cPixelFormatRange &range)
{
    cPixelFormatRange copy;
    copy.mCurrent = range.mCurrent->Clone();
    if (copy.mCurrent) copy.mCurrent->AddRef();
    copy.mBegin = range.mBegin;
    if (copy.mBegin) copy.mBegin->AddRef();
    copy.mEnd = range.mEnd;
    if (copy.mEnd) copy.mEnd->AddRef();
    return copy;
}

// The display's size scaled and rounded to the nearest (ties to even) as the original does it: the float plus 1.5 * 2^23
// leaves the integer in the low mantissa bits.
int32_t ScaledSize(uint32_t size, float scale)
{
    const float scaled = float(double(size)) * scale + kRoundingBias;
    return std::bit_cast<int32_t>(scaled) - std::bit_cast<int32_t>(kRoundingBias);
}

} // namespace

void cRenderTargetTexture::Restore()
{
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (IsCreated() || !device->IsDeviceReady()) return;
    ReleaseResource();
    auto *const formats = static_cast<cIDeviceEnum *>(static_cast<void *>(device->GetDeviceEnumConst()));

    // The colour format: the float format of 32 red bits at or after it in the formats' order, or the closest ARGB
    // format with the bits asked (the alpha bits dropped, then half the colour bits).
    cIPixelFormat *colour;
    if (mFlags & kFlagFloat) {
        // The formats manager's GetABGRFloat (slot 8): the float format of 32 red bits.
        auto *const wanted = static_cast<cIPixelFormat *>(
            VirtualCall<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 8>(
                nGZGraphic4::GetPixelFormatsManagerCached(), 0u, 0u, 0u, 0x20u));
        if (!wanted) return;
        cPixelFormatRange range = formats->GetColorFormatsForDisplay(kUsageFloatColour, 0);
        colour = nGZGraphic4::FindFormatLowerBound(CopyRange(range), wanted);
        nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
    } else {
        cPixelFormatRange range = formats->GetColorFormatsForDisplay(kUsageColour, 0);
        colour = nullptr;
        if (mAlphaBits) colour = nGZGraphic4::FindClosestColorFormat(CopyRange(range), mFormatBits, mAlphaBits);
        if (!colour) colour = nGZGraphic4::FindClosestColorFormat(CopyRange(range), mFormatBits, 0);
        if (!colour) colour = nGZGraphic4::FindClosestColorFormat(CopyRange(range), mFormatBits >> 1, 0);
        nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
    }
    if (!colour) return;

    // The depth format for it: the stencil bits dropped, then half the depth bits; none fits, nothing is made.
    cIPixelFormat *depth = nullptr;
    if (mFlags & kFlagDepth) {
        cPixelFormatRange range = formats->GetDepthFormatsForDisplay(kUsageDepth, colour);
        if (mStencilBits) depth = nGZGraphic4::FindClosestDepthFormat(CopyRange(range), mDepthBits, mStencilBits, false);
        if (!depth) depth = nGZGraphic4::FindClosestDepthFormat(CopyRange(range), mDepthBits, 0, false);
        if (!depth) depth = nGZGraphic4::FindClosestDepthFormat(CopyRange(range), mDepthBits >> 1, 0, false);
        nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
        if (!depth) return;
    }

    // The size: the one asked, or the display's scaled (its aspect taken); then fitted to the device.
    int32_t width = mWidth, height = mHeight;
    uint32_t levels = 1;
    mAspect = 1.0f;
    if (mFlags & kFlagRelative) {
        nGZGraphic4::cDeviceDisplaySize size;
        const nGZGraphic4::cDeviceDisplaySize *const display = nGZGraphic4::GetDeviceTargetSize(&size);
        mAspect = display->mAspect;
        width = ScaledSize(display->mWidth, mScaleX);
        height = ScaledSize(display->mHeight, mScaleY);
    }
    const int32_t askedWidth = width, askedHeight = height;
    AdjustTextureSize(device, colour, &width, &height, &levels, true);
    // Keeping the aspect: the share the fitting changed it by is folded into the aspect.
    if (mFlags & kFlagAspect)
        mAspect = float(askedWidth) / float(askedHeight) / (float(width) / float(height)) * mAspect;

    for (uint32_t i = 0; i < mCount; ++i) mTargets[i]->CreateRenderTarget(device, width, height, colour, levels);
    if (mFlags & kFlagDepth) mDepth->Create(device, width, height, depth, 0, 0);
    if (IsCreated()) Clear(nullptr, mClear, 1.0f, 0);
}

// The targets as a render-targets state command (the game's state vtable 0xd9ff54): the first target's surface (or
// target `index`'s alone) and the depth surface when there is one. The depth surface is held while the first surface is
// asked for, as the original's smart pointer does.
nGZGraphic4::cRenderTargetsState cRenderTargetTexture::TargetsState(int32_t index)
{
    nGZGraphic4::cIDepthStencilSurface *const depth = (mFlags & kFlagDepth) ? mDepth : nullptr;
    if (depth) depth->AddRef();
    cRenderTargetsState state{};
    state.mVTable = cRenderTargetsState::VTable();
    state.mId = kRenderTargetsId;
    state.mSize = static_cast<uint16_t>(sizeof(cRenderTargetsState));
    state.mTargets.surface[0] = mTargets[index == -1 ? 0 : index]->GetTargetSurface(0);
    state.mDepthTarget = depth;
    if (depth) depth->Release();
    if (index != -1) return state; // ORIGINAL-BUG: the index is not checked against the count (or the array)
    // ORIGINAL-BUG: a count over four writes past the state's four surfaces.
    for (uint32_t i = 1; i < mCount; ++i) state.mTargets.surface[i] = mTargets[i]->GetTargetSurface(0);
    return state;
}

void cRenderTargetTexture::SetAsTargets(void *device, int32_t index)
{
    cRenderTargetsState state = TargetsState(index);
    static_cast<nGZGraphic4::cIDeviceState *>(device)->ApplyStateCommand(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&state)));
}

void cRenderTargetTexture::ClearWith(uint32_t flags, const int32_t *rect, const float *colour, float depth,
                                     uint32_t stencil)
{
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (!device->IsDeviceReady() || !IsCreated()) return;
    int32_t area[4] = {0, 0, 0x7fffffff, 0x7fffffff}; // the whole target
    if (rect)
        for (unsigned i = 0; i < 4; ++i) area[i] = rect[i];
    const cRenderTargetsState state = TargetsState(-1);
    device->ClearTargets(&state, area, flags, colour, depth, stencil);
}

} // namespace nGZSceneGraph
