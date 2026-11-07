// The surfaces (lane NJ, rendering R3): src/gz/cSurfaceDX.h.
#include "gz/cSurfaceDX.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "gz/cSurfaceCopy.h"
#include "ifaces/cIDeviceEnum.h"
#include "ifaces/cIDeviceResourceManager.h"
#include "ifaces/cIPixelFormat.h"
#include "ifaces/cIPixelFormatsManager.h"
#include "rz/MiscHelpersA.h" // LogD3DError
#include "rz/RZFastFixedAllocators.h"

namespace nGZGraphic4 {

namespace surface_detail {

const char *const kMipSourceFile = ts2_rva_d9dcd8; // the texture file (cMipSurfaceDX's pixel format)

void Report(HRESULT hr, uint32_t line, const char *file = ts2_rva_d9dab0)
{
    LogD3DError(static_cast<uint32_t>(hr), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file)), line);
}
void Check(HRESULT hr, uint32_t line, const char *file = ts2_rva_d9dab0)
{
    if (FAILED(hr)) Report(hr, line, file);
}

// The pixel formats manager (game code: 0x18acd0, the getter of its static instance).
ifaces::cIPixelFormatsManager *Formats()
{
    return As<ifaces::cIPixelFormatsManager>(GetPixelFormatsManagerCached());
}
ifaces::cIDeviceResourceManager *Manager(cIDeviceDX *device) { return As<ifaces::cIDeviceResourceManager>(device->GetResourceManager()); }

// Samples per pixel of a multisample type (MULTISAMPLE_NONMASKABLE: 2, or 4 with a quality level).
uint32_t Samples(const D3DSURFACE_DESC &desc)
{
    if (desc.MultiSampleType == D3DMULTISAMPLE_NONE) return 1;
    if (desc.MultiSampleType == D3DMULTISAMPLE_NONMASKABLE) return desc.MultiSampleQuality != 0 ? 4 : 2;
    return desc.MultiSampleType;
}


} // namespace surface_detail

using namespace surface_detail;

// ---- cSurfaceDX<> ----

template <class P> cSurfaceDX<P>::~cSurfaceDX()
{
    if (mSurface) {
        Manager(mDevice)->Remove(static_cast<P *>(this));
        mSurface->Release();
        mSurface = nullptr;
        mDevice = nullptr;
        mWidth = 0;
        mHeight = 0;
    }
}

template <class P> bool cSurfaceDX<P>::AnswerCommon(uint32_t riid, void **out)
{
    switch (riid) {
    case 1:
    case kIidDeviceResource:
    case kIidSurface:
    case kIidAllocDeviceResource:
        *out = static_cast<P *>(this);
        AddRef();
        return true;
    case kIidD3DSurface:
    case kIidD3DResource:
        *out = mSurface;
        return true;
    case kIidSurfaceDX:
        *out = static_cast<cISurfaceDX *>(this);
        AddRef();
        return true;
    default:
        return false;
    }
}

template <class P> bool cSurfaceDX<P>::QueryInterface(uint32_t riid, void **out)
{
    return out != nullptr && AnswerCommon(riid, out);
}

template <class P> uint32_t cSurfaceDX<P>::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

template <class P> void cSurfaceDX<P>::ReleaseResource()
{
    if (!IsCreated()) return;
    Manager(mDevice)->Remove(static_cast<P *>(this));
    if (mSurface) {
        mSurface->Release();
        mSurface = nullptr;
    }
    mDevice = nullptr;
    mWidth = 0;
    mHeight = 0;
}

template <class P> uint32_t cSurfaceDX<P>::GetMemorySize()
{
    constexpr uint32_t kObject = 0x18; // the original counts the common part's size for every surface
    if (!mSurface) return kObject;
    D3DSURFACE_DESC desc;
    Check(mSurface->GetDesc(&desc), 0x101);
    if (desc.Pool == D3DPOOL_DEFAULT) return kObject;
    const uint32_t bits = As<ifaces::cIPixelFormat>(this->GetPixelFormat())->GetBitsPerPixel();
    return (Samples(desc) * desc.Height * desc.Width * bits >> 3) + kObject;
}

template <class P> bool cSurfaceDX<P>::SurvivesReset()
{
    D3DSURFACE_DESC desc;
    Check(mSurface->GetDesc(&desc), 0xed);
    return desc.Pool != D3DPOOL_DEFAULT;
}

template <class P> uint32_t cSurfaceDX<P>::GetPool()
{
    D3DSURFACE_DESC desc;
    Check(mSurface->GetDesc(&desc), 0x129);
    return desc.Pool;
}

template <class P> uint32_t cSurfaceDX<P>::GetVideoMemorySize()
{
    D3DSURFACE_DESC desc;
    Check(mSurface->GetDesc(&desc), 0x133);
    if (desc.Pool == D3DPOOL_SYSTEMMEM || desc.Pool == D3DPOOL_SCRATCH) return 0;
    const uint32_t bits = As<ifaces::cIPixelFormat>(Formats()->GetFormat(desc.Format))->GetBitsPerPixel();
    return Samples(desc) * desc.Height * desc.Width * bits >> 3;
}

template <class P> void *cSurfaceDX<P>::GetPixelFormat()
{
    D3DSURFACE_DESC desc;
    Check(mSurface->GetDesc(&desc), 0x1b7);
    return Formats()->GetFormat(desc.Format);
}

template <class P> uint32_t cSurfaceDX<P>::GetMultiSampleType()
{
    D3DSURFACE_DESC desc;
    Check(mSurface->GetDesc(&desc), 0x1c1);
    return desc.MultiSampleType;
}

template <class P> uint32_t cSurfaceDX<P>::GetMultiSampleQuality()
{
    D3DSURFACE_DESC desc;
    Check(mSurface->GetDesc(&desc), 0x1cb);
    return desc.MultiSampleQuality;
}

// The copies are cSurfaceCopy.h (rendering.md, R3b): they reach the surfaces through their faces and Direct3D.
namespace {
cISurfaceDX *FaceOf(cISurface *surface)
{
    cISurfaceDX *face = nullptr;
    surface->QueryInterface(kIidSurfaceDX, reinterpret_cast<void **>(&face));
    return face;
}
} // namespace

template <class P> bool cSurfaceDX<P>::CopyRect(const POINT *targetPoint, cISurface *source, const RECT *sourceRect)
{
    cISurfaceDX *face = FaceOf(source);
    const bool done = CopySurfaceRectDX(face, sourceRect, this, targetPoint);
    if (face) face->Release();
    return done;
}

template <class P> bool cSurfaceDX<P>::Copy(cISurface *source)
{
    cISurfaceDX *face = FaceOf(source);
    const POINT origin{0, 0};
    const LONG height = static_cast<LONG>(source->GetHeight());
    const LONG width = static_cast<LONG>(source->GetWidth());
    const RECT whole{0, 0, width, height};
    const bool done = CopySurfaceRectDX(face, &whole, this, &origin);
    if (face) face->Release();
    return done;
}

template <class P> bool cSurfaceDX<P>::StretchRect(const RECT *targetRect, cISurface *source, const RECT *sourceRect)
{
    IDirect3DSurface9 *d3dSource = nullptr;
    source->QueryInterface(kIidD3DSurface, reinterpret_cast<void **>(&d3dSource)); // not counted
    D3DSURFACE_DESC sourceDesc, targetDesc;
    Check(d3dSource->GetDesc(&sourceDesc), 0x171);
    Check(mSurface->GetDesc(&targetDesc), 0x174);
    const RECT sourceBounds{0, 0, static_cast<LONG>(sourceDesc.Width), static_cast<LONG>(sourceDesc.Height)};
    const RECT targetBounds{0, 0, static_cast<LONG>(targetDesc.Width), static_cast<LONG>(targetDesc.Height)};
    RECT from = *sourceRect;
    RECT to = *targetRect;
    if (!ClipCopyRects(&from, &sourceBounds, &to, &targetBounds)) return true; // nothing left to copy
    cISurfaceDX *face = FaceOf(source);
    const bool resized = from.right - from.left != to.right - to.left || from.bottom - from.top != to.bottom - to.top;
    uint32_t filter = resized;
    bool done = false;
    if (CanStretchRectDX(face, &sourceDesc, &from, this, &targetDesc, &to, &filter, resized)) {
        IDirect3DDevice9 *d3d = mDevice->GetD3DDevice();
        done = SUCCEEDED(d3d->StretchRect(d3dSource, &from, mSurface, &to, static_cast<D3DTEXTUREFILTERTYPE>(filter)));
    }
    if (face) face->Release();
    return done;
}

template <class P> bool cSurfaceDX<P>::Stretch(cISurface *source)
{
    const LONG sourceHeight = static_cast<LONG>(source->GetHeight());
    const LONG sourceWidth = static_cast<LONG>(source->GetWidth());
    const RECT from{0, 0, sourceWidth, sourceHeight};
    const LONG height = static_cast<LONG>(this->GetHeight());
    const LONG width = static_cast<LONG>(this->GetWidth());
    const RECT to{0, 0, width, height};
    return this->StretchRect(&to, source, &from);
}

template <class P> void cSurfaceDX<P>::SetSurface(cIDeviceDX *device, IDirect3DSurface9 *surface)
{
    mDevice = device;
    if (surface) {
        surface->AddRef(); // the original's counted local and its copy
        surface->AddRef();
    }
    if (mSurface) {
        mSurface->Release();
        mSurface = nullptr;
    }
    mSurface = surface;
    if (!surface) {
        mWidth = 0;
        mHeight = 0;
        return;
    }
    surface->Release();
    D3DSURFACE_DESC desc; // read whatever GetDesc answered
    Check(surface->GetDesc(&desc), 0x75);
    mWidth = static_cast<uint16_t>(desc.Width);
    mHeight = static_cast<uint16_t>(desc.Height);
}

template <class P> void cSurfaceDX<P>::Init(cIDeviceDX *device, IDirect3DSurface9 *surface)
{
    SetSurface(device, surface);
    Manager(device)->Add(static_cast<P *>(this));
}

template <class P>
template <class Make>
bool cSurfaceDX<P>::CreateWith(cIDeviceDX *device, void *format, bool refresh, uint32_t line, Make make)
{
    if (!device->IsDeviceReady()) return false;
    if (refresh) Manager(device)->RefreshAvailableMemory();
    cIDeviceDX *deviceDX = nullptr;
    device->QueryInterface(cDeviceDX::kIidDeviceDX, reinterpret_cast<void **>(&deviceDX));
    IDirect3DDevice9 *d3d = deviceDX->GetD3DDevice();
    const uint32_t id = As<ifaces::cIPixelFormat>(format)->GetFormatId();
    IDirect3DSurface9 *surface = nullptr;
    const HRESULT hr = make(d3d, static_cast<D3DFORMAT>(id), &surface);
    bool done = false;
    if (hr == D3DERR_INVALIDCALL) {
        Report(hr, line);
    } else if (SUCCEEDED(hr)) {
        static_cast<cISurfaceDX *>(this)->Init(deviceDX, surface);
        done = true;
    }
    if (surface) surface->Release();
    if (deviceDX) deviceDX->Release();
    return done;
}

// ---- cLockableSurfaceDX<> ----

template <class P> bool cLockableSurfaceDX<P>::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == kIidLockableSurface) {
        *out = static_cast<P *>(this);
        this->AddRef();
        return true;
    }
    return this->AnswerCommon(riid, out);
}

template <class P>
uint32_t cLockableSurfaceDX<P>::LockRect(cSurfaceLock *lock, int32_t x, int32_t y, int32_t width, int32_t height,
                                         bool readOnly)
{
    int32_t left = x, top = y;
    if (x < 0) {
        left = 0;
        width += x;
    }
    if (y < 0) {
        top = 0;
        height += y;
    }
    IDirect3DSurface9 *surface = this->GetD3DSurface();
    D3DSURFACE_DESC desc;
    Check(surface->GetDesc(&desc), 0x236);
    if (static_cast<int32_t>(desc.Width) < width + left) width = static_cast<int32_t>(desc.Width) - left;
    if (static_cast<int32_t>(desc.Height) < height + top) height = static_cast<int32_t>(desc.Height) - top;
    if (width < 1 || height < 1) {
        if (void *held = lock->surface) {
            lock->surface = nullptr;
            static_cast<cIGZUnknown *>(held)->Release();
        }
        lock->rect = RECT{}; // the original leaves an uninitialised local here
        lock->bits = nullptr;
        lock->pitch = 0;
        lock->rowBytes = 0;
        lock->rows = 0;
        return 0;
    }
    const RECT rect{left, top, left + width, top + height};
    D3DLOCKED_RECT locked; // read whatever LockRect answered
    Check(this->GetD3DSurface()->LockRect(&locked, &rect, (readOnly ? D3DLOCK_READONLY : 0) | D3DLOCK_NOSYSLOCK), 0x24f);
    ++mLockCount;
    ifaces::cIPixelFormat *format = As<ifaces::cIPixelFormat>(this->GetPixelFormat());
    int32_t rows = height / static_cast<int32_t>(format->GetBlockHeight());
    if (rows < 1) rows = 1;
    const uint32_t rowBytes = format->GetRowBytes(static_cast<uint32_t>(width));
    void *self = static_cast<P *>(this);
    if (lock->surface != self) {
        this->AddRef();
        void *held = lock->surface;
        lock->surface = self;
        if (held) static_cast<cIGZUnknown *>(held)->Release();
    }
    lock->rect = rect;
    lock->bits = locked.pBits;
    lock->pitch = locked.Pitch;
    lock->rowBytes = rowBytes;
    lock->rows = static_cast<uint32_t>(rows);
    return static_cast<uint32_t>(rows);
}

template <class P> int32_t cLockableSurfaceDX<P>::Lock(cSurfaceLock *lock, bool readOnly)
{
    D3DLOCKED_RECT locked; // read whatever LockRect answered
    Check(this->GetD3DSurface()->LockRect(&locked, nullptr, (readOnly ? D3DLOCK_READONLY : 0) | D3DLOCK_NOSYSLOCK), 0x214);
    ++mLockCount;
    D3DSURFACE_DESC desc;
    Check(this->GetD3DSurface()->GetDesc(&desc), 0x219);
    ifaces::cIPixelFormat *format = As<ifaces::cIPixelFormat>(this->GetPixelFormat());
    uint32_t rows = desc.Height / format->GetBlockHeight();
    if (rows == 0) rows = 1;
    const uint32_t rowBytes = format->GetRowBytes(desc.Width);
    void *self = static_cast<P *>(this);
    if (lock->surface != self) {
        this->AddRef();
        void *held = lock->surface;
        lock->surface = self;
        if (held) static_cast<cIGZUnknown *>(held)->Release();
    }
    lock->rect = RECT{0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
    lock->bits = locked.pBits;
    lock->pitch = locked.Pitch;
    lock->rowBytes = rowBytes;
    lock->rows = rows;
    return locked.Pitch;
}

template <class P> void cLockableSurfaceDX<P>::Unlock(cSurfaceLock *lock)
{
    --mLockCount;
    Check(this->GetD3DSurface()->UnlockRect(), 0x260);
    if (void *held = lock->surface) {
        lock->surface = nullptr;
        static_cast<cIGZUnknown *>(held)->Release();
    }
    lock->rect = RECT{}; // the original leaves an uninitialised local here
    lock->bits = nullptr;
    lock->pitch = 0;
    lock->rowBytes = 0;
    lock->rows = 0;
}

template class cSurfaceDX<cIImageSurface>;
template class cSurfaceDX<cILockableRenderTarget>;
template class cSurfaceDX<cIDepthStencilSurface>;
template class cSurfaceDX<cIRenderTarget>;
template class cLockableSurfaceDX<cIImageSurface>;
template class cLockableSurfaceDX<cILockableRenderTarget>;

// ---- the leaves ----

void *cImageSurfaceDX::operator new(size_t) { return reinterpret_cast<RZFixedAllocatorBase *>(&Pool())->Alloc(); }
void cImageSurfaceDX::operator delete(void *block, size_t) { reinterpret_cast<RZFastFixed0x1c *>(&Pool())->Free(block); }
cIImageSurface *__cdecl cImageSurfaceDX::New() { return new cImageSurfaceDX; }

bool cImageSurfaceDX::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == kIidImageSurface || riid == kIidLockableSurface) {
        *out = static_cast<cIImageSurface *>(this);
        AddRef();
        return true;
    }
    return AnswerCommon(riid, out);
}

bool cImageSurfaceDX::SurvivesModeChange()
{
    D3DSURFACE_DESC desc;
    Check(GetD3DSurface()->GetDesc(&desc), 0x29a);
    if (desc.Pool == D3DPOOL_SCRATCH) return true;
    cIGZUnknown *deviceEnum = GetDevice()->GetDeviceEnum();
    return As<ifaces::cIDeviceEnum>(deviceEnum)->IsFormatSupportedOnDisplay(GetPixelFormat(), 1);
}

bool cImageSurfaceDX::Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t poolChoice)
{
    if (!device->IsDeviceReady()) return false;
    cIDeviceDX *deviceDX = nullptr;
    device->QueryInterface(cDeviceDX::kIidDeviceDX, reinterpret_cast<void **>(&deviceDX));
    const uint32_t id = As<ifaces::cIPixelFormat>(format)->GetFormatId();
    uint32_t pool;
    switch (poolChoice) {
    case 0: pool = D3DPOOL_DEFAULT; break;
    case 1: pool = D3DPOOL_SYSTEMMEM; break;
    case 2: pool = D3DPOOL_SCRATCH; break;
    case 3:
        pool = As<ifaces::cIDeviceEnum>(device->GetDeviceEnumConst())->IsFormatSupportedOnDisplay(format, 1)
                   ? D3DPOOL_SYSTEMMEM
                   : D3DPOOL_SCRATCH;
        break;
    default:
        // ORIGINAL-BUG: any other choice passes the device pointer as the pool (the original's register reuse).
        pool = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(device));
        break;
    }
    IDirect3DSurface9 *surface = nullptr;
    IDirect3DDevice9 *d3d = deviceDX->GetD3DDevice();
    const HRESULT hr = d3d->CreateOffscreenPlainSurface(width, height, static_cast<D3DFORMAT>(id),
                                                        static_cast<D3DPOOL>(pool), &surface, nullptr);
    bool done = false;
    if (hr == D3DERR_INVALIDCALL) {
        Report(hr, 0x2d4);
    } else if (SUCCEEDED(hr)) {
        static_cast<cISurfaceDX *>(this)->Init(deviceDX, surface);
        done = true;
    }
    if (surface) surface->Release();
    if (deviceDX) deviceDX->Release();
    return done;
}

void *cMipSurfaceDX::operator new(size_t) { return reinterpret_cast<RZFixedAllocatorBase *>(&Pool())->Alloc(); }
void cMipSurfaceDX::operator delete(void *block, size_t) { reinterpret_cast<RZFastFixed0x20 *>(&Pool())->Free(block); }

cMipSurfaceDX::~cMipSurfaceDX() { SetSurface(nullptr, nullptr); }

void *cMipSurfaceDX::GetPixelFormat()
{
    D3DSURFACE_DESC desc;
    Check(GetD3DSurface()->GetDesc(&desc), 0x2e0, kMipSourceFile);
    // A palettised level answers as A8 (0x1c).
    return Formats()->GetFormat(desc.Format == D3DFMT_P8 ? D3DFMT_A8 : desc.Format);
}

void *cLockableRenderTargetDX::operator new(size_t) { return reinterpret_cast<RZFixedAllocatorBase *>(&Pool())->Alloc(); }
void cLockableRenderTargetDX::operator delete(void *block, size_t) { reinterpret_cast<RZFastFixed0x1c *>(&Pool())->Free(block); }
cILockableRenderTarget *__cdecl cLockableRenderTargetDX::New() { return new cLockableRenderTargetDX; }

bool cLockableRenderTargetDX::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == kIidLockableRenderTarget || riid == kIidLockableSurface) {
        *out = static_cast<cILockableRenderTarget *>(this);
        AddRef();
        return true;
    }
    return AnswerCommon(riid, out);
}

bool cLockableRenderTargetDX::Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format)
{
    return CreateWith(device, format, true, 0x412, [&](IDirect3DDevice9 *d3d, D3DFORMAT id, IDirect3DSurface9 **out) {
        return d3d->CreateRenderTarget(width, height, id, D3DMULTISAMPLE_NONE, 0, TRUE, out, nullptr);
    });
}

void *cDepthStencilSurfaceDX::operator new(size_t) { return reinterpret_cast<RZFixedAllocatorBase *>(&Pool())->Alloc(); }
void cDepthStencilSurfaceDX::operator delete(void *block, size_t) { reinterpret_cast<RZFastFixed0x18 *>(&Pool())->Free(block); }
cIDepthStencilSurface *__cdecl cDepthStencilSurfaceDX::New() { return new cDepthStencilSurfaceDX; }

bool cDepthStencilSurfaceDX::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == kIidDepthStencilSurface) {
        *out = static_cast<cIDepthStencilSurface *>(this);
        AddRef();
        return true;
    }
    return AnswerCommon(riid, out);
}

bool cDepthStencilSurfaceDX::Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format,
                                    uint32_t multiSample, uint32_t multiSampleQuality)
{
    return CreateWith(device, format, true, 0x33f, [&](IDirect3DDevice9 *d3d, D3DFORMAT id, IDirect3DSurface9 **out) {
        return d3d->CreateDepthStencilSurface(width, height, id, static_cast<D3DMULTISAMPLE_TYPE>(multiSample),
                                              multiSampleQuality, FALSE, out, nullptr);
    });
}

void *cRenderTargetDX::operator new(size_t) { return reinterpret_cast<RZFixedAllocatorBase *>(&Pool())->Alloc(); }
void cRenderTargetDX::operator delete(void *block, size_t) { reinterpret_cast<RZFastFixed0x18 *>(&Pool())->Free(block); }
cIRenderTarget *__cdecl cRenderTargetDX::New() { return new cRenderTargetDX; }

bool cRenderTargetDX::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == kIidRenderTarget) {
        *out = static_cast<cIRenderTarget *>(this);
        AddRef();
        return true;
    }
    return AnswerCommon(riid, out);
}

bool cRenderTargetDX::Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t multiSample,
                             uint32_t multiSampleQuality)
{
    return CreateWith(device, format, true, 0x3a9, [&](IDirect3DDevice9 *d3d, D3DFORMAT id, IDirect3DSurface9 **out) {
        return d3d->CreateRenderTarget(width, height, id, static_cast<D3DMULTISAMPLE_TYPE>(multiSample),
                                       multiSampleQuality, FALSE, out, nullptr);
    });
}

} // namespace nGZGraphic4
