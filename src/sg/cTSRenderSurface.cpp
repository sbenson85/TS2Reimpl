// nTSSG render surfaces (lane 261108c). Layout and roles: src/sg/cTSRenderSurface.h.
#include "sg/cTSRenderSurface.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h"  // GetPixelFormatsManagerCached
#include "gz/cDeviceDX.h"
#include "gz/cDeviceStateDX.h"   // cIDeviceState
#include "gz/cGZPropertySet.h"   // gzany:: the value vtables
#include "gz/cIGZCOM.h"
#include "gz/cIGZPropertySet.h"  // GZServiceString
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIPixelFormatsManager.h"
#include "misc/UtilitiesE.h"     // GetCachedQueriedObject
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "sg/cDisplayList.h"     // Pass
#include "sg/cMaterialManager.h" // cIMaterialManager
#include "sg/cRenderTargetTexture.h"
#include "sg/cTextureImage.h"    // cITexture

#include <cstring>
#include <new>

namespace nTSSG {

using nGZGraphic4::cIDepthStencilSurface;
using nGZGraphic4::cIDeviceDX;
using nGZGraphic4::cIRenderTarget;
using nGZSceneGraph::cIRenderTargetTexture;
using nGZSceneGraph::cITexture;
using nGZSceneGraph::cTextureHandle;

namespace {

// The colour every clear passes (the original's read-only global 0x10d9ce8: zeros).
const float kZero[4] = {};

// The COM classes the surfaces make (class id, interface id).
constexpr uint32_t kRenderTargetClass = 0x5b75b757, kRenderTargetIid = 0xbb75b749;
constexpr uint32_t kDepthStencilClass = 0x7b75b73a, kDepthStencilIid = 0xfb75b72b;
constexpr uint32_t kRenderTargetTextureClass = 0x2b96cc2b;

// The settings service (class, interface) and its key.
constexpr uint32_t kSettingsId = 0x0ac1887e, kSettingsIid = 0x4bb0d269;

// 0x18afc0 (__cdecl, not ours): the device's multisample type changed (enable, quality); false when it cannot be.
bool SetDeviceMultisample(uint32_t enable, uint32_t quality)
{
    using Fn = bool(__cdecl *)(uint32_t, uint32_t);
    return OriginalFunction<Fn, 0x18afc0>()(enable, quality);
}

cIGZCOM *Com() { return static_cast<cIGZCOM *>(GetDirectorFrameworkObject()); }

// The members' counted assignment the original's inline code has: the new one counted, stored, the old released.
template <class T> void AssignCounted(T *&member, T *value)
{
    T *const old = member;
    if (value != old) {
        if (value)
            value->AddRef();
        member = value;
        if (old)
            old->Release();
    }
}

// The settings service's boolean value (cAnyBoolean): the destructor stores the base's vtable.
struct AnyBoolean {
    const void *mVTable;
    bool mValue;
    explicit AnyBoolean(bool value) : mVTable(gzany::BooleanVTable()), mValue(value) {}
    AnyBoolean(const AnyBoolean &) = delete;
    AnyBoolean &operator=(const AnyBoolean &) = delete;
    ~AnyBoolean() { *static_cast<const void *volatile *>(&mVTable) = gzany::AnyVTable(); }
};

// The render-target texture behind a handle (the texture's QueryInterface, as the original: unchecked).
void QueryTarget(const cTextureHandle &handle, cIRenderTargetTexture *&target)
{
    cIGZUnknown *const texture = handle.Get();
    if (cIRenderTargetTexture *const old = target) {
        target = nullptr;
        old->Release();
    }
    // ORIGINAL-BUG: neither the texture (null for a released id) nor the answer is checked.
    texture->QueryInterface(cIRenderTargetTexture::kIid, reinterpret_cast<void **>(&target));
}

} // namespace

// ---- the members ----------------------------------------------------------------------------------------------------

cTargetsCommand::cTargetsCommand()
{
    mVTable = cRenderTargetsState::VTable();
    mId = 0;
    mSize = sizeof(nGZGraphic4::cRenderTargetsState);
    for (void *&surface : mTargets.surface)
        surface = nullptr;
    mDepthTarget = nullptr;
}

cTargetsCommand::~cTargetsCommand() { *static_cast<const void *volatile *>(&mVTable) = cStateHeader::VTable(); }

cZeroedStringMember::cZeroedStringMember()
{
    std::memset(static_cast<void *>(&value), 0, sizeof(value));
    value.Construct();
}

void cZeroedStringMember::Assign(const char *text) { value.mString.assign(text); }

cTSRenderSurfaceImpl::cTSRenderSurfaceImpl()
    : mRequestedWidth(0), mRequestedHeight(0), mDevice(nullptr), mColor(nullptr), mDepth(nullptr), mCreated(0),
      mWidth(0), mHeight(0), mScale(1.0f), mHasDepth(0)
{
}

void cTSRenderSurfaceImpl::Bind(cISurface *color, cISurface *depth)
{
    mColor = color;
    mDepth = depth;
    if (color) {
        mWidth = color->GetWidth();
        mHeight = mColor->GetHeight();
    } else {
        mWidth = 0;
        mHeight = 0;
    }
    mTargets.mSize = sizeof(nGZGraphic4::cRenderTargetsState);
    mTargets.mId = 0;
    mTargets.mTargets.surface[0] = color;
    mTargets.mTargets.surface[1] = nullptr;
    mTargets.mTargets.surface[2] = nullptr;
    mTargets.mTargets.surface[3] = nullptr;
    mTargets.mDepthTarget = depth;
}

void cTSRenderSurfaceImpl::Apply(Pass *pass, bool force)
{
    if (force || pass->mTextures[0] != mColor) {
        static_cast<nGZGraphic4::cIDeviceState *>(pass->mDeviceState)
            ->ApplyStateCommand(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&mTargets)));
        pass->mTextureCount = 1;
        pass->mTextures[0] = mColor;
        pass->mTextureRef94 = mDepth;
    }
}

void cTSRenderSurfaceImpl::Clear(Pass *pass, uint32_t flags, float z, uint32_t stencil)
{
    Apply(pass, false);
    mDevice->Clear(flags, kZero, z, stencil);
}

void cTSRenderSurfaceImpl::ClearRect(Pass *, const int32_t *rect, uint32_t flags)
{
    mDevice->ClearTargets(&mTargets, rect, flags, kZero, 1.0f, 0);
}

void cTSRenderSurfaceImpl::CopyFrom(cITSRenderSurface *other)
{
    if (other == this)
        return;
    CopyDepth(other);
    CopyColor(other, true);
}

void cTSRenderSurfaceImpl::CopyDepth(cITSRenderSurface *other)
{
    if (mHasDepth)
        mDepth->Copy(other->GetDepthSurface());
}

void cTSRenderSurfaceImpl::CopyRect(cITSRenderSurface *other, const RECT *rect)
{
    if (other == this)
        return;
    cISurface *const source = other->GetColorSurface();
    void *const format = mColor->GetPixelFormat();
    if (format != source->GetPixelFormat()) {
        mColor->StretchRect(rect, other->GetColorSurface(), rect);
        return;
    }
    const POINT origin{rect->left, rect->top};
    mColor->CopyRect(&origin, other->GetColorSurface(), rect);
}

void cTSRenderSurfaceImpl::CopyColor(cITSRenderSurface *other, bool stretch)
{
    if (other == this)
        return;
    if (!stretch) {
        cISurface *const source = other->GetColorSurface();
        void *const format = mColor->GetPixelFormat();
        stretch = format != source->GetPixelFormat();
    }
    if (stretch)
        mColor->Stretch(other->GetColorSurface());
    else
        mColor->Copy(other->GetColorSurface());
}

// ---- the back buffer ------------------------------------------------------------------------------------------------

void cTSRenderBackbufferSurface::Init(const char *name, bool hasDepth)
{
    mName.Assign(name);
    mHasDepth = hasDepth;
}

void cTSRenderBackbufferSurface::Configure(cIDeviceDX *device, uint32_t samples)
{
    cISurface *const backBuffer = static_cast<cISurface *>(device->GetBackBuffer(0));
    const uint32_t width = backBuffer->GetWidth();
    const uint32_t height = backBuffer->GetHeight();
    if (mDevice != device || mSamples != samples || mRequestedWidth != width || mRequestedHeight != height) {
        mDevice = device;
        mRequestedWidth = width;
        mRequestedHeight = height;
        mWidth = width;
        mHeight = height;
        mSamples = samples;
        mCreated = 0;
    }
}

void cTSRenderBackbufferSurface::Create()
{
    if (!mDevice || !mDevice->IsDeviceReady())
        return;
    if (!mCreated) {
        const uint32_t samples = mSamples;
        mCreated = 1;
        mChangingMultisample = 1;
        if (!SetDeviceMultisample(samples != 0, samples != 0 ? samples - 1 : 0))
            mSamples = 0;
        mChangingMultisample = 0;
    }
    cISurface *const backBuffer = static_cast<cISurface *>(mDevice->GetBackBuffer(0));
    cISurface *const depth = mHasDepth ? static_cast<cISurface *>(mDevice->GetTargetProviderSurface()) : nullptr;
    Bind(backBuffer, depth);
}

void cTSRenderBackbufferSurface::Destroy()
{
    if (!mDevice || !mDevice->IsDeviceReady())
        return;
    if (mCreated) {
        mCreated = 0;
        mChangingMultisample = 1;
        SetDeviceMultisample(0, 0);
        mChangingMultisample = 0;
    }
    cISurface *const backBuffer = static_cast<cISurface *>(mDevice->GetBackBuffer(0));
    cISurface *const depth = mHasDepth ? static_cast<cISurface *>(mDevice->GetTargetProviderSurface()) : nullptr;
    Bind(backBuffer, depth);
}

// ---- the render target ----------------------------------------------------------------------------------------------

cTSRenderTargetSurface::~cTSRenderTargetSurface()
{
    if (mDepthStencil)
        mDepthStencil->Release();
    if (mTarget)
        mTarget->Release();
}

bool cTSRenderTargetSurface::IsValid()
{
    if (!mTarget || !mTarget->IsCreated())
        return false;
    // ORIGINAL-BUG: with depth, the depth-stencil surface is used without a null check.
    if (mHasDepth && !mDepthStencil->IsCreated())
        return false;
    return true;
}

void cTSRenderTargetSurface::Init(const char *name, bool hasDepth)
{
    mName.Assign(name);
    mHasDepth = hasDepth;
    if (mTarget)
        return;
    cIRenderTarget *target = nullptr;
    Com()->GetClassObject(kRenderTargetClass, kRenderTargetIid, reinterpret_cast<void **>(&target));
    cIDepthStencilSurface *depth = nullptr;
    Com()->GetClassObject(kDepthStencilClass, kDepthStencilIid, reinterpret_cast<void **>(&depth));
    AssignCounted(mTarget, target);
    AssignCounted(mDepthStencil, depth);
    if (depth)
        depth->Release();
    if (target)
        target->Release();
}

void cTSRenderTargetSurface::Configure(cIDeviceDX *device, uint32_t width, uint32_t height, uint32_t samples)
{
    if (mDevice != device || mRequestedWidth != width || mRequestedHeight != height || mSamples != samples ||
        !IsValid()) {
        mDevice = device;
        mRequestedWidth = width;
        mRequestedHeight = height;
        mSamples = samples;
        if (mCreated)
            Destroy();
    }
}

void cTSRenderTargetSurface::Reset()
{
    mTargets.mId = 0;
    mTargets.mSize = sizeof(nGZGraphic4::cRenderTargetsState);
    for (void *&surface : mTargets.mTargets.surface)
        surface = nullptr;
    mTargets.mDepthTarget = nullptr;
    if (mTarget && mTarget->IsCreated())
        mTarget->ReleaseResource();
    if (mHasDepth && mDepthStencil && mDepthStencil->IsCreated())
        mDepthStencil->ReleaseResource();
    mCreated = 0;
}

void cTSRenderTargetSurface::ReleaseSurfaces()
{
    Reset();
    if (cIRenderTarget *const target = mTarget) {
        mTarget = nullptr;
        target->Release();
    }
    if (cIDepthStencilSurface *const depth = mDepthStencil) {
        mDepthStencil = nullptr;
        depth->Release();
    }
}

void cTSRenderTargetSurface::Create()
{
    if (mCreated) {
        Bind(mTarget, mDepthStencil);
        return;
    }
    void *const format = static_cast<ifaces::cIPixelFormatsManager *>(nGZGraphic4::GetPixelFormatsManagerCached())
                             ->GetARGB(0x20, 0, 8, 8, 8);
    void *const depthFormat = // receiver: nGZGraphic4::cPixelFormatsManagerDX (11 GetDepthStencil, 0x15f720, ret 0x10)
        RawSlot<11, void *>(nGZGraphic4::GetPixelFormatsManagerCached(), 0x20u, 0x18u, 8u, 0u);
    const uint32_t multisample = mSamples != 0;
    const uint32_t quality = mSamples != 0 ? mSamples - 1 : 0;
    Reset();
    if (mTarget->Create(mDevice, mRequestedWidth, mRequestedHeight, format, multisample, quality)) {
        if (!mHasDepth ||
            mDepthStencil->Create(mDevice, mRequestedWidth, mRequestedHeight, depthFormat, multisample, quality)) {
            Bind(mTarget, mDepthStencil);
            mCreated = 1;
            return;
        }
        mTarget->ReleaseResource();
    }
    Bind(nullptr, nullptr);
}

void cTSRenderTargetSurface::Destroy()
{
    if (mCreated) {
        Reset();
        mCreated = 0;
    }
}

// ---- the render-target texture --------------------------------------------------------------------------------------

bool cTSRenderTextureSurface::IsValid()
{
    cITexture *texture = mExternal;
    if (!texture) {
        texture = static_cast<cITexture *>(mTexture.Get());
        if (!texture)
            return false;
    }
    return texture->IsCreated();
}

void cTSRenderTextureSurface::Init(const char *material, const char *texture, bool reparse, bool hasDepth, bool alpha)
{
    cTSRenderTargetSurface::Init(texture, hasDepth);
    mReparse = reparse;
    mMaterial.Assign(material);
    mTextureName.Assign(texture);
    mAlpha = alpha;
    mTexture.Reset();
    cIRenderTargetTexture *target = nullptr;
    Com()->GetClassObject(kRenderTargetTextureClass, cIRenderTargetTexture::kIid, reinterpret_cast<void **>(&target));
    target->Create(0x10, 0x10, 0x18, mAlpha ? 8 : 0, 8, 1, kZero, mHasDepth != 0);
    const uint32_t id = nGZSceneGraph::TextureManager()->Add(target, mTextureName.Text(), 5);
    {
        const cTextureHandle added(id);
        mTexture.Assign(&added);
    }
    if (target)
        target->Release();
}

void cTSRenderTextureSurface::Configure(cIDeviceDX *device, uint32_t width, uint32_t height)
{
    if (width != 0) {
        if (mDevice != device || mRequestedWidth != width || mRequestedHeight != height || !IsValid()) {
            mDevice = device;
            mRequestedWidth = width;
            mRequestedHeight = height;
            if (mCreated)
                Destroy();
        }
        return;
    }
    mDevice = device;
    mCreated = 0;
    mExternal = nullptr;
    mRequestedWidth = 0;
    mRequestedHeight = 0;
    Bind(nullptr, nullptr);
}

void cTSRenderTextureSurface::SetExternal(cIRenderTargetTexture *texture)
{
    cTSRenderTargetSurface::Init("ExternalTexture", texture->GetDepthSurface() != nullptr);
    mExternal = texture;
    cISurface *const depth = texture->GetDepthSurface();
    cISurface *const color = texture->GetTargetSurface(0);
    Bind(color, depth);
    mReparse = 0;
}

void cTSRenderTextureSurface::ReparseShaders()
{
    auto *const materials = static_cast<nGZSceneGraph::cIMaterialManager *>(GetCachedQueriedObject());
    if (mReparse) {
        ScopedRef settings{nullptr};
        if (auto *const framework = static_cast<ifaces::cIGZFrameWork *>(GetServiceObject()))
            framework->GetSystemService(kSettingsId, kSettingsIid, &settings.mObject);
        if (settings.mObject) {
            GZServiceString key("reparseShaders");
            AnyBoolean value(true);
            static_cast<cIGZPropertySetString *>(settings.mObject)->SetProperty(&key, &value);
        }
        settings.ReleaseObject();
        return;
    }
    if (!mMaterial.value.mString.empty())
        materials->ReloadAll(mMaterial.Text(), 0);
}

void cTSRenderTextureSurface::Create()
{
    if (mTexture.mId == cTextureHandle::kNone) {
        Bind(nullptr, nullptr);
        return;
    }
    cIRenderTargetTexture *target = nullptr;
    QueryTarget(mTexture, target);
    if (!mCreated) {
        target->Create(static_cast<int32_t>(mRequestedWidth), static_cast<int32_t>(mRequestedHeight), 0x18,
                       mAlpha ? 8 : 0, 8, 1, kZero, mHasDepth != 0);
        ReparseShaders();
        if (target->IsCreated()) {
            cISurface *const depth = target->GetDepthSurface();
            cISurface *const color = target->GetTargetSurface(0);
            Bind(color, depth);
            mCreated = 1;
            mAspect = static_cast<float>(mWidth) / static_cast<float>(mRequestedWidth);
        } else {
            mCreated = 1;
            Destroy();
            if (mCreated) { // the game's check: Destroy, entered with mCreated 1, always clears it
                mCreated = 0;
                Bind(nullptr, nullptr);
            }
        }
    } else {
        cISurface *const depth = target->GetDepthSurface();
        cISurface *const color = target->GetTargetSurface(0);
        Bind(color, depth);
    }
    if (target)
        target->Release();
}

void cTSRenderTextureSurface::Destroy()
{
    if (mTexture.mId == cTextureHandle::kNone) {
        Bind(nullptr, nullptr);
        return;
    }
    cIRenderTargetTexture *target = nullptr;
    QueryTarget(mTexture, target);
    if (mCreated) {
        target->Create(0x10, 0x10, 0x18, mAlpha ? 8 : 0, 8, 1, kZero, mHasDepth != 0);
        if (target->IsCreated()) {
            cISurface *const depth = target->GetDepthSurface();
            cISurface *const color = target->GetTargetSurface(0);
            Bind(color, depth);
        } else {
            Bind(nullptr, nullptr);
        }
        ReparseShaders();
        mCreated = 0;
    } else {
        cISurface *const depth = target->GetDepthSurface();
        cISurface *const color = target->GetTargetSurface(0);
        Bind(color, depth);
    }
    if (target)
        target->Release();
}

// ---- the hooks' entry points ----------------------------------------------------------------------------------------

using A = cTSRenderSurfaceAdapters;

A::Backbuffer *__fastcall A::ConstructBackbuffer(Backbuffer *memory, void *) { return ::new (memory) Backbuffer; }
A::Target *__fastcall A::ConstructTarget(Target *memory, void *) { return ::new (memory) Target; }
A::Texture *__fastcall A::ConstructTexture(Texture *memory, void *) { return ::new (memory) Texture; }

void __fastcall A::Bind(Impl *self, void *, cISurface *color, cISurface *depth)
{
    [[msvc::forceinline_calls]] self->Impl::Bind(color, depth);
}

void __fastcall A::Reset(Target *self, void *) { [[msvc::forceinline_calls]] self->Target::Reset(); }
void __fastcall A::ReleaseSurfaces(Target *self, void *) { [[msvc::forceinline_calls]] self->Target::ReleaseSurfaces(); }

void __fastcall A::InitBackbuffer(Backbuffer *self, void *, const char *name, bool hasDepth)
{
    [[msvc::forceinline_calls]] self->Backbuffer::Init(name, hasDepth);
}

void __fastcall A::InitTarget(Target *self, void *, const char *name, bool hasDepth)
{
    [[msvc::forceinline_calls]] self->Target::Init(name, hasDepth);
}

void __fastcall A::InitTexture(Texture *self, void *, const char *material, const char *texture, bool reparse,
                               bool hasDepth, bool alpha)
{
    [[msvc::forceinline_calls]] self->Texture::Init(material, texture, reparse, hasDepth, alpha);
}

void __fastcall A::ConfigureBackbuffer(Backbuffer *self, void *, cIDeviceDX *device, uint32_t samples)
{
    [[msvc::forceinline_calls]] self->Backbuffer::Configure(device, samples);
}

void __fastcall A::ConfigureTarget(Target *self, void *, cIDeviceDX *device, uint32_t width, uint32_t height,
                                   uint32_t samples)
{
    [[msvc::forceinline_calls]] self->Target::Configure(device, width, height, samples);
}

void __fastcall A::ConfigureTexture(Texture *self, void *, cIDeviceDX *device, uint32_t width, uint32_t height)
{
    [[msvc::forceinline_calls]] self->Texture::Configure(device, width, height);
}

void __fastcall A::SetExternal(Texture *self, void *, cIRenderTargetTexture *texture)
{
    [[msvc::forceinline_calls]] self->Texture::SetExternal(texture);
}

void __fastcall A::ResetTexture(Texture *self, void *) { [[msvc::forceinline_calls]] self->Texture::ResetTexture(); }

bool __fastcall A::IsValidTarget(Target *self, void *) { return self->Target::IsValid(); }
void __fastcall A::CreateTarget(Target *self, void *) { self->Target::Create(); }
void __fastcall A::DestroyTarget(Target *self, void *) { self->Target::Destroy(); }

A::Target *__fastcall A::DeleteTarget(Target *self, void *, unsigned flags)
{
    self->Target::~Target();
    if (flags & 1)
        ::operator delete(self);
    return self;
}

} // namespace nTSSG
