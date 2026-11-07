// The memory surfaces and textures (lane RN): src/gz/cMemBlock.h.
#include "gz/cMemBlock.h"

#include <new>

#include "game/vtable.h"
#include "gz/PixelFormat.h" // kIidDXT
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGraphicSystemDX.h"
#include "ifaces/cIPixelFormat.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

namespace nRZGraphic4 {

namespace {

constexpr uint32_t kGraphicServiceId = 0x85522803, kGraphicServiceIid = 0xc55289ba;
constexpr uint32_t kSlotGetBlockBytes = 16; // the DXT formats' bytes per 4 x 4 block

ifaces::cIPixelFormat *Format(void *format) { return As<ifaces::cIPixelFormat>(format); }

// The graphic system's do-nothing device (cGraphicSystemDX slot 29), through the framework's service.
// ORIGINAL-BUG: the service is used without a check (no framework or no graphic system crashes); it always exists while
// anything draws.
IDirect3DDevice9 *NullDevice(RZAutoRef &holder)
{
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kGraphicServiceId, kGraphicServiceIid, &holder.mObject);
    return static_cast<IDirect3DDevice9 *>(As<ifaces::cIGraphicSystemDX>(holder.mObject)->GetNullDevice());
}

// The swap both lockable faces do: the lock holds the surface (counted), the one it held before let go.
void Hold(cSurfaceLock *lock, cMemBlockAsSurface *self)
{
    if (lock->surface == static_cast<nGZGraphic4::cILockableSurface *>(self)) return;
    self->AddRef();
    void *const held = lock->surface;
    lock->surface = static_cast<nGZGraphic4::cILockableSurface *>(self);
    if (held) static_cast<cIGZUnknown *>(held)->Release();
}

// The lock emptied (an empty rectangle, or Unlock): the surface let go, nothing locked.
void Empty(cSurfaceLock *lock)
{
    if (void *const held = lock->surface) {
        lock->surface = nullptr;
        static_cast<cIGZUnknown *>(held)->Release();
    }
    lock->rect = RECT{}; // ORIGINAL-BUG: the original copies an uninitialised local rectangle here
    lock->bits = nullptr;
    lock->pitch = 0;
    lock->rowBytes = 0;
    lock->rows = 0;
}

constexpr DWORD LockFlags(bool readOnly) { return (readOnly ? D3DLOCK_READONLY : 0) | D3DLOCK_NOSYSLOCK; }

} // namespace

// ---- cMemBlockAsSurfaceImpl ----

cMemBlockAsSurfaceImpl::cMemBlockAsSurfaceImpl(void *bits, void *format, uint32_t width, uint32_t height,
                                               uint32_t pitch)
    : mBits(static_cast<uint8_t *>(bits)), mPitch(pitch), mWidth(width), mHeight(height), mFormat(format)
{
    void *dxt = nullptr;
    if (static_cast<cIGZUnknown *>(format)->QueryInterface(nGZGraphic4::kIidDXT, &dxt))
        mBlockBytes = VirtualCall<uint32_t(__thiscall *)(void *), kSlotGetBlockBytes>(dxt);
    if (dxt) static_cast<cIGZUnknown *>(dxt)->Release();
}

cMemBlockAsSurfaceImpl::~cMemBlockAsSurfaceImpl() = default;

HRESULT cMemBlockAsSurfaceImpl::QueryInterface(REFIID, void **) { return E_NOINTERFACE; }
ULONG cMemBlockAsSurfaceImpl::AddRef() { return ++mRefs; }
ULONG cMemBlockAsSurfaceImpl::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

HRESULT cMemBlockAsSurfaceImpl::GetDevice(IDirect3DDevice9 **device)
{
    RZAutoRef holder{nullptr};
    *device = NullDevice(holder); // not counted
    holder.ReleaseObject();
    return D3D_OK;
}

HRESULT cMemBlockAsSurfaceImpl::SetPrivateData(REFGUID, const void *, DWORD, DWORD) { return kInvalidCall; }
HRESULT cMemBlockAsSurfaceImpl::GetPrivateData(REFGUID, void *, DWORD *) { return kInvalidCall; }
HRESULT cMemBlockAsSurfaceImpl::FreePrivateData(REFGUID) { return kInvalidCall; }
DWORD cMemBlockAsSurfaceImpl::SetPriority(DWORD) { return 0; }
DWORD cMemBlockAsSurfaceImpl::GetPriority() { return 0; }
void cMemBlockAsSurfaceImpl::PreLoad() {}
D3DRESOURCETYPE cMemBlockAsSurfaceImpl::GetType() { return D3DRTYPE_SURFACE; }

HRESULT cMemBlockAsSurfaceImpl::GetContainer(REFIID riid, void **container)
{
    *container = nullptr;
    HRESULT hr;
    if (mContainer) {
        hr = mContainer->QueryInterface(riid, container);
    } else {
        RZAutoRef holder{nullptr};
        hr = NullDevice(holder)->QueryInterface(riid, container);
        holder.ReleaseObject();
    }
    // ORIGINAL-BUG: the answer is inverted: a container that has the interface fails, one that has not succeeds.
    return hr == S_OK ? kInvalidCall : D3D_OK;
}

HRESULT cMemBlockAsSurfaceImpl::GetDesc(D3DSURFACE_DESC *desc)
{
    desc->Format = static_cast<D3DFORMAT>(Format(mFormat)->GetFormatId());
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = 0;
    desc->Pool = D3DPOOL_SCRATCH;
    desc->MultiSampleType = D3DMULTISAMPLE_NONE;
    desc->MultiSampleQuality = 0;
    desc->Width = mWidth;
    desc->Height = mHeight;
    return D3D_OK;
}

HRESULT cMemBlockAsSurfaceImpl::LockRect(D3DLOCKED_RECT *locked, const RECT *rect, DWORD)
{
    if (rect && (static_cast<uint32_t>(rect->left) >= mWidth || static_cast<uint32_t>(rect->top) >= mHeight))
        return kInvalidCall;
    ++mLockCount;
    const uint32_t x = rect ? static_cast<uint32_t>(rect->left) : 0;
    const uint32_t y = rect ? static_cast<uint32_t>(rect->top) : 0;
    uint32_t offset;
    if (mBlockBytes != 0)
        offset = (y >> 2) * mPitch + (x >> 2) * mBlockBytes; // 4 x 4 blocks
    else
        offset = y * mPitch + ((Format(mFormat)->GetBitsPerPixel() * x) >> 3);
    locked->Pitch = static_cast<INT>(mPitch);
    locked->pBits = mBits + offset;
    return D3D_OK;
}

HRESULT cMemBlockAsSurfaceImpl::UnlockRect()
{
    if (mLockCount == 0) return kInvalidCall;
    --mLockCount;
    return D3D_OK;
}

HRESULT cMemBlockAsSurfaceImpl::GetDC(HDC *) { return kInvalidCall; }
HRESULT cMemBlockAsSurfaceImpl::ReleaseDC(HDC) { return kInvalidCall; }

// ---- cMemBlockAsSurface ----

cMemBlockAsSurface::cMemBlockAsSurface(void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch)
{
    Init(bits, format, width, height, pitch);
}

cMemBlockAsSurface::~cMemBlockAsSurface() { cMemBlockAsSurface::ReleaseResource(); }

void cMemBlockAsSurface::Init(void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch)
{
    ReleaseResource(); // through the vtable, as the original
    mImpl = new cMemBlockAsSurfaceImpl(bits, format, width, height, pitch);
    mImpl->AddRef();
}

bool cMemBlockAsSurface::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    switch (riid) {
    case 1:
    case nGZGraphic4::kIidDeviceResource:
    case nGZGraphic4::kIidAllocDeviceResource:
    case nGZGraphic4::kIidSurface:
    case nGZGraphic4::kIidLockableSurface:
        *out = static_cast<nGZGraphic4::cILockableSurface *>(this);
        AddRef();
        return true;
    case nGZGraphic4::kIidD3DSurface:
    case nGZGraphic4::kIidD3DResource:
        *out = static_cast<IDirect3DSurface9 *>(mImpl);
        return mImpl != nullptr;
    default:
        return false;
    }
}

uint32_t cMemBlockAsSurface::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

void cMemBlockAsSurface::ReleaseResource()
{
    if (!mImpl) return;
    mImpl->Release();
    mImpl = nullptr;
}

uint32_t cMemBlockAsSurface::LockRect(cSurfaceLock *lock, int32_t x, int32_t y, int32_t width, int32_t height,
                                      bool readOnly)
{
    // A rectangle starting left of or above the surface is clipped there.
    const int32_t left = x < 0 ? 0 : x;
    if (x < 0) width += x;
    const int32_t top = y < 0 ? 0 : y;
    if (y < 0) height += y;
    D3DSURFACE_DESC desc;
    mImpl->GetDesc(&desc);
    if (static_cast<int32_t>(desc.Width) < left + width) width = static_cast<int32_t>(desc.Width) - left;
    if (static_cast<int32_t>(desc.Height) < top + height) height = static_cast<int32_t>(desc.Height) - top;
    if (width < 1 || height < 1) {
        Empty(lock);
        return 0;
    }
    const RECT rect{left, top, left + width, top + height};
    D3DLOCKED_RECT locked;
    mImpl->LockRect(&locked, &rect, LockFlags(readOnly)); // inside the surface, so it cannot fail
    ifaces::cIPixelFormat *const format = Format(mImpl->mFormat);
    int32_t rows = height / static_cast<int32_t>(format->GetBlockHeight());
    if (rows < 1) rows = 1;
    const uint32_t rowBytes = format->GetRowBytes(static_cast<uint32_t>(width));
    Hold(lock, this);
    lock->rect = rect;
    lock->bits = locked.pBits;
    lock->pitch = locked.Pitch;
    lock->rowBytes = rowBytes;
    lock->rows = static_cast<uint32_t>(rows);
    return static_cast<uint32_t>(rows);
}

int32_t cMemBlockAsSurface::Lock(cSurfaceLock *lock, bool readOnly)
{
    D3DLOCKED_RECT locked;
    mImpl->LockRect(&locked, nullptr, LockFlags(readOnly));
    D3DSURFACE_DESC desc;
    mImpl->GetDesc(&desc);
    ifaces::cIPixelFormat *const format = Format(mImpl->mFormat);
    uint32_t rows = desc.Height / format->GetBlockHeight();
    if (rows == 0) rows = 1;
    const uint32_t rowBytes = format->GetRowBytes(desc.Width);
    Hold(lock, this);
    lock->rect = RECT{0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
    lock->bits = locked.pBits;
    lock->pitch = locked.Pitch;
    lock->rowBytes = rowBytes;
    lock->rows = rows;
    return locked.Pitch;
}

void cMemBlockAsSurface::Unlock(cSurfaceLock *lock)
{
    mImpl->UnlockRect();
    Empty(lock);
}

cMemBlockAsSurface *__fastcall cMemBlockAsSurface::Construct(void *memory, void *, void *bits, void *format,
                                                             uint32_t width, uint32_t height, uint32_t pitch)
{
    return ::new (memory) cMemBlockAsSurface(bits, format, width, height, pitch);
}

cMemBlockAsSurface *__fastcall cMemBlockAsSurface::ConstructEmpty(void *memory, void *)
{
    return ::new (memory) cMemBlockAsSurface();
}

void __fastcall cMemBlockAsSurface::Destruct(cMemBlockAsSurface *self, void *) { self->cMemBlockAsSurface::~cMemBlockAsSurface(); }

// ---- cMemBlockAsTextureImpl ----

cMemBlockAsTextureImpl::~cMemBlockAsTextureImpl() { mLevels.Destroy(); }

HRESULT cMemBlockAsTextureImpl::QueryInterface(REFIID, void **) { return E_NOINTERFACE; }
ULONG cMemBlockAsTextureImpl::AddRef() { return ++mRefs; }
ULONG cMemBlockAsTextureImpl::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

HRESULT cMemBlockAsTextureImpl::GetDevice(IDirect3DDevice9 **device) { return Level(0)->mImpl->GetDevice(device); }
HRESULT cMemBlockAsTextureImpl::SetPrivateData(REFGUID, const void *, DWORD, DWORD) { return kInvalidCall; }
HRESULT cMemBlockAsTextureImpl::GetPrivateData(REFGUID, void *, DWORD *) { return kInvalidCall; }
HRESULT cMemBlockAsTextureImpl::FreePrivateData(REFGUID) { return kInvalidCall; }
DWORD cMemBlockAsTextureImpl::SetPriority(DWORD) { return 0; }
DWORD cMemBlockAsTextureImpl::GetPriority() { return 0; }
void cMemBlockAsTextureImpl::PreLoad() {}
D3DRESOURCETYPE cMemBlockAsTextureImpl::GetType() { return D3DRTYPE_TEXTURE; }
DWORD cMemBlockAsTextureImpl::SetLOD(DWORD) { return 0; }
DWORD cMemBlockAsTextureImpl::GetLOD() { return 0; }
DWORD cMemBlockAsTextureImpl::GetLevelCount() { return mLevels.Size(); }
HRESULT cMemBlockAsTextureImpl::SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) { return D3D_OK; }
D3DTEXTUREFILTERTYPE cMemBlockAsTextureImpl::GetAutoGenFilterType() { return D3DTEXF_NONE; }
void cMemBlockAsTextureImpl::GenerateMipSubLevels() {}

HRESULT cMemBlockAsTextureImpl::GetLevelDesc(UINT level, D3DSURFACE_DESC *desc)
{
    return Level(level)->mImpl->GetDesc(desc);
}

HRESULT cMemBlockAsTextureImpl::GetSurfaceLevel(UINT level, IDirect3DSurface9 **surface)
{
    *surface = Level(level)->mImpl;
    (*surface)->AddRef();
    return D3D_OK;
}

HRESULT cMemBlockAsTextureImpl::LockRect(UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
    return Level(level)->mImpl->LockRect(locked, rect, flags);
}

HRESULT cMemBlockAsTextureImpl::UnlockRect(UINT level) { return Level(level)->mImpl->UnlockRect(); }
HRESULT cMemBlockAsTextureImpl::AddDirtyRect(const RECT *) { return D3D_OK; }

// ---- cMemBlockAsTexture ----

cMemBlockAsTexture::~cMemBlockAsTexture() { cMemBlockAsTexture::ReleaseResource(); }

bool cMemBlockAsTexture::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    switch (riid) {
    case 1:
    case nGZGraphic4::kIidDeviceResource:
    case nGZGraphic4::kIidTextureBase:
    case nGZGraphic4::kIidTexture:
    case nGZGraphic4::kIidAllocDeviceResource:
        *out = static_cast<nGZGraphic4::cITexture *>(this);
        AddRef();
        return true;
    case nGZGraphic4::kIidD3DBaseTexture:
    case nGZGraphic4::kIidD3DTexture:
    case nGZGraphic4::kIidD3DResource:
        *out = static_cast<IDirect3DTexture9 *>(mImpl);
        return mImpl != nullptr;
    default:
        return false;
    }
}

uint32_t cMemBlockAsTexture::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

void cMemBlockAsTexture::ReleaseResource()
{
    if (!mImpl) return;
    mImpl->Release();
    mImpl = nullptr;
}

bool cMemBlockAsTexture::IsCreated() { return mImpl && mImpl->mLevels.mFirst != mImpl->mLevels.mLast; }

// A new level over the memory: a face of its own (counted by the level list), marked a texture level of this texture.
void cMemBlockAsTexture::AppendLevel(void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch)
{
    cMemBlockAsSurface *surface = new cMemBlockAsSurface(bits, format, width, height, pitch);
    surface->AddRef();
    surface->mImpl->mAccessFlags = kMemBlockLevelFlags;
    surface->mImpl->mContainer = mImpl; // not counted
    RefPtrVector &levels = mImpl->mLevels;
    void *const element = static_cast<nGZGraphic4::cILockableSurface *>(surface);
    if (levels.mLast != levels.mEnd) {
        *levels.mLast = element;
        surface->AddRef();
        ++levels.mLast;
    } else {
        levels.EmplaceReallocate(levels.mLast, &element);
    }
    surface->Release();
}

void cMemBlockAsTexture::SetTopLevel(void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch)
{
    ReleaseResource(); // through the vtable, as the original
    mImpl = new cMemBlockAsTextureImpl;
    mImpl->AddRef();
    AppendLevel(bits, format, width, height, pitch);
}

void cMemBlockAsTexture::AddLevel(void *bits, uint32_t width, uint32_t height, uint32_t pitch)
{
    AppendLevel(bits, mImpl->Level(0)->mImpl->mFormat, width, height, pitch);
}

cMemBlockAsTexture *__fastcall cMemBlockAsTexture::Construct(void *memory, void *)
{
    return ::new (memory) cMemBlockAsTexture();
}

void __fastcall cMemBlockAsTexture::Destruct(cMemBlockAsTexture *self, void *) { self->cMemBlockAsTexture::~cMemBlockAsTexture(); }

} // namespace nRZGraphic4
