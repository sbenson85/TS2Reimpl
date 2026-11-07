// The textures (lane NJ, rendering R3): src/gz/cTextureDX.h.
#include "gz/cTextureDX.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "ifaces/cIDeviceEnum.h"
#include "ifaces/cIDeviceResourceManager.h"
#include "ifaces/cIPixelFormat.h"
#include "ifaces/cIPixelFormatsManager.h"
#include "rz/MiscHelpersA.h" // LogD3DError
#include "rz/RZFastFixedAllocators.h"

namespace nGZGraphic4 {

namespace {

// Each class's LogD3DError source file and lines.
struct Lines {
    uint32_t file, memory, memoryLevel, reset, pool, video, videoLevel, quality, qualityLevel, width, height, format,
        lod, create, createLevel, target, targetLevel, depth, depthLevel, d3dFormat, levelBytes;
};
constexpr Lines kTextureLines{0xd9dcd8, 0xbb, 0xc1, 0xcf, 0xf4, 0xfd, 0x109, 0x11e, 0x128, 0x14c, 0x155, 0x15e, 0x172,
                              0x1bf, 0x1d0, 0x209, 0x217, 0x250, 0x25e, 0x29a, 0x2a3};
constexpr Lines kCubeLines{0xd9de40, 0xbf, 0xc5, 0xd4, 0xf9, 0x102, 0x10e, 0x123, 0x12d, 0x151, 0x15a, 0x163, 0x177,
                           0x1c7, 0x1da, 0x215, 0x225, 0x260, 0x270, 0x2ad, 0x2b6};
template <unsigned Faces> constexpr const Lines &LinesOf() { return Faces == 1 ? kTextureLines : kCubeLines; }

template <unsigned Faces> void Check(HRESULT hr, uint32_t line)
{
    if (FAILED(hr))
        LogD3DError(static_cast<uint32_t>(hr),
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<const char>(LinesOf<Faces>().file))),
                    line);
}

ifaces::cIPixelFormatsManager *Formats()
{
    return As<ifaces::cIPixelFormatsManager>(GetPixelFormatsManagerCached());
}
ifaces::cIDeviceResourceManager *Manager(cIDeviceDX *device)
{
    return As<ifaces::cIDeviceResourceManager>(device->GetResourceManager());
}
uint32_t BitsOf(void *format) { return As<ifaces::cIPixelFormat>(format)->GetBitsPerPixel(); }

// The failures a create gives up on quietly (D3DERR_OUTOFVIDEOMEMORY, E_OUTOFMEMORY, D3DERR_DEVICELOST's sibling
// 0x88760827); any other failure is reported and the create carries on.
bool QuietFailure(HRESULT hr)
{
    return static_cast<uint32_t>(hr) == 0x8876017c || static_cast<uint32_t>(hr) == 0x8007000e ||
           static_cast<uint32_t>(hr) == 0x88760827;
}

constexpr uint32_t kIidDeviceEnumFormats = 0x6c02c311; // the device enum's face asked for a P8 substitute (slot 28)
constexpr uint32_t kAccessLevel = 8, kAccessDepth = 2, kAccessTarget = 4;

void *PoolAlloc(uint32_t rva) { return GameAddress<RZFixedAllocatorBase>(rva)->Alloc(); }

} // namespace

template <class P, unsigned F> HRESULT cTextureBaseDX<P, F>::LevelDesc(uint32_t level, D3DSURFACE_DESC *desc)
{
    if constexpr (F == 1)
        return static_cast<IDirect3DTexture9 *>(mTexture)->GetLevelDesc(level, desc);
    else
        return static_cast<IDirect3DCubeTexture9 *>(mTexture)->GetLevelDesc(level, desc);
}

template <class P, unsigned F> cTextureBaseDX<P, F>::~cTextureBaseDX()
{
    Detach();
    for (unsigned i = kSlots; i-- > 0;)
        if (mMips[i]) mMips[i]->Release();
    if (mTexture) {
        mTexture->Release();
        mTexture = nullptr;
    }
}

template <class P, unsigned F> bool cTextureBaseDX<P, F>::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIidD3DBaseTexture) {
        *out = mTexture; // before the null check, as the original
        return true;
    }
    if (!out) return false;
    switch (riid) {
    case 1:
    case kIidDeviceResource:
    case kIidAllocDeviceResource:
    case kIidTextureBase:
        *out = static_cast<P *>(this);
        AddRef();
        return true;
    case kIidTextureBaseDX:
        *out = static_cast<cITextureBaseDX *>(this);
        AddRef();
        return true;
    case kIidD3DResource:
        *out = mTexture;
        return true;
    default:
        break;
    }
    if constexpr (F == 1) {
        if (riid == kIidTexture) {
            *out = static_cast<P *>(this);
            AddRef();
            return true;
        }
        if (riid == kIidD3DTexture) {
            *out = mTexture;
            return true;
        }
    } else {
        if (riid == kIidCubeTexture) {
            *out = static_cast<P *>(this);
            AddRef();
            return true;
        }
        if (riid == kIidD3DCubeTexture) {
            *out = mTexture;
            return true;
        }
    }
    return false;
}

template <class P, unsigned F> uint32_t cTextureBaseDX<P, F>::Release()
{
    if (--mRefs != 0) return mRefs;
    delete static_cast<P *>(this);
    return 0;
}

template <class P, unsigned F> void cTextureBaseDX<P, F>::Detach()
{
    if (!IsCreated()) return;
    Manager(mDevice)->Remove(static_cast<P *>(this));
    for (unsigned face = 0; face < F; ++face)
        for (uint32_t level = 0; level < mLevels; ++level) mMips[face * 16 + level]->SetSurface(nullptr, nullptr);
    mLevels = 0;
    if (mTexture) {
        mTexture->Release();
        mTexture = nullptr;
    }
    mDevice = nullptr;
}

template <class P, unsigned F> void cTextureBaseDX<P, F>::ReleaseResource() { Detach(); }

template <class P, unsigned F> uint32_t cTextureBaseDX<P, F>::GetMemorySize()
{
    constexpr uint32_t kObject = kSize;
    if (!mTexture) return kObject;
    D3DSURFACE_DESC desc;
    Check<F>(LevelDesc(0, &desc), LinesOf<F>().memory);
    if (desc.Pool == D3DPOOL_DEFAULT) return kObject;
    const uint32_t bits = BitsOf(this->GetPixelFormat());
    uint32_t sum = 0;
    for (uint32_t i = 0; i < mLevels; ++i) {
        D3DSURFACE_DESC level;
        Check<F>(LevelDesc(i, &level), LinesOf<F>().memoryLevel);
        sum += level.Height * level.Width * bits >> 3;
    }
    return sum * F + kObject;
}

template <class P, unsigned F> bool cTextureBaseDX<P, F>::SurvivesReset()
{
    D3DSURFACE_DESC desc;
    Check<F>(LevelDesc(0, &desc), LinesOf<F>().reset);
    return desc.Pool != D3DPOOL_DEFAULT;
}

template <class P, unsigned F> bool cTextureBaseDX<P, F>::SurvivesModeChange()
{
    cIGZUnknown *deviceEnum = mDevice->GetDeviceEnumConst();
    return As<ifaces::cIDeviceEnum>(deviceEnum)->IsFormatSupportedOnDisplay(this->GetPixelFormat(), F == 1 ? 4 : 6);
}

template <class P, unsigned F> uint32_t cTextureBaseDX<P, F>::GetPool()
{
    D3DSURFACE_DESC desc;
    Check<F>(LevelDesc(0, &desc), LinesOf<F>().pool);
    return desc.Pool;
}

template <class P, unsigned F> uint32_t cTextureBaseDX<P, F>::GetVideoMemorySize()
{
    D3DSURFACE_DESC desc;
    Check<F>(LevelDesc(0, &desc), LinesOf<F>().video);
    if (desc.Pool == D3DPOOL_SYSTEMMEM || desc.Pool == D3DPOOL_SCRATCH) return 0;
    const uint32_t bits = BitsOf(this->GetPixelFormat());
    uint32_t sum = 0;
    for (uint32_t i = this->GetLOD(); i < mLevels; ++i) {
        D3DSURFACE_DESC level;
        Check<F>(LevelDesc(i, &level), LinesOf<F>().videoLevel);
        sum += level.Height * level.Width * bits >> 3;
    }
    return sum * F;
}

template <class P, unsigned F> void cTextureBaseDX<P, F>::SetQuality(float quality)
{
    if (quality == mQuality) return; // a NaN always counts as a change
    D3DSURFACE_DESC desc;
    Check<F>(LevelDesc(0, &desc), LinesOf<F>().quality);
    if (desc.Pool == D3DPOOL_MANAGED) {
        const uint32_t bits = BitsOf(this->GetPixelFormat());
        uint32_t total = 0, resident = 0;
        for (uint32_t i = 0; i < mLevels; ++i) {
            D3DSURFACE_DESC level;
            Check<F>(LevelDesc(i, &level), LinesOf<F>().qualityLevel);
            const uint32_t bytes = level.Height * level.Width * bits >> 3;
            total += bytes;
            if (i >= mTexture->GetLOD()) resident += bytes;
        }
        const uint32_t dropped = total - resident;
        const float old = mQuality;
        ifaces::cIDeviceResourceManager *manager = Manager(mDevice);
        // Not (old > quality): unordered counts as lowering too.
        const uint32_t bytes = !(old > quality) ? dropped : dropped - resident;
        manager->AddPending(static_cast<int32_t>(bytes * F));
    }
    mQuality = quality;
}

template <class P, unsigned F> uint32_t cTextureBaseDX<P, F>::GetWidth(uint32_t level)
{
    D3DSURFACE_DESC desc;
    Check<F>(LevelDesc(level, &desc), LinesOf<F>().width);
    return desc.Width;
}

template <class P, unsigned F> uint32_t cTextureBaseDX<P, F>::GetHeight(uint32_t level)
{
    D3DSURFACE_DESC desc;
    Check<F>(LevelDesc(level, &desc), LinesOf<F>().height);
    return desc.Height;
}

template <class P, unsigned F> void *cTextureBaseDX<P, F>::GetPixelFormat()
{
    D3DSURFACE_DESC desc;
    Check<F>(LevelDesc(0, &desc), LinesOf<F>().format);
    return Formats()->GetFormat(desc.Format == D3DFMT_P8 ? D3DFMT_A8 : desc.Format);
}

template <class P, unsigned F> uint32_t cTextureBaseDX<P, F>::GetLOD()
{
    D3DSURFACE_DESC desc;
    Check<F>(LevelDesc(0, &desc), LinesOf<F>().lod);
    return desc.Pool == D3DPOOL_MANAGED ? mTexture->GetLOD() : 0;
}

template <class P, unsigned F> uint32_t cTextureBaseDX<P, F>::GetD3DFormat()
{
    D3DSURFACE_DESC desc;
    Check<F>(LevelDesc(0, &desc), LinesOf<F>().d3dFormat);
    return desc.Format;
}

template <class P, unsigned F> uint32_t cTextureBaseDX<P, F>::GetLevelBytes(uint32_t level)
{
    D3DSURFACE_DESC desc;
    Check<F>(LevelDesc(level, &desc), LinesOf<F>().levelBytes);
    const uint32_t bits = BitsOf(this->GetPixelFormat());
    return (desc.Width * desc.Height * bits >> 3) * F;
}

template <class P, unsigned F>
template <class Make>
bool cTextureBaseDX<P, F>::Build(cIDeviceDX *device, cIDeviceDX *deviceDX, uint32_t access, float quality,
                                 uint32_t createLine, uint32_t levelLine, Make make)
{
    IDirect3DBaseTexture9 *texture = nullptr;
    const HRESULT hr = make(&texture);
    if (QuietFailure(hr)) {
        if (texture) texture->Release();
        return false;
    }
    Check<F>(hr, createLine);
    uint32_t levels = texture->GetLevelCount();
    if (levels > 16) levels = 16;
    mLevels = levels;
    for (unsigned face = 0; face < F; ++face) {
        for (uint32_t level = 0; level < mLevels; ++level) {
            cMipSurfaceDX *&slot = mMips[face * 16 + level];
            if (!slot) {
                cMipSurfaceDX *mip = new cMipSurfaceDX;
                if (mip != slot) {
                    mip->AddRef();
                    cMipSurfaceDX *old = slot;
                    slot = mip;
                    if (old) old->Release();
                }
            }
            IDirect3DSurface9 *surface = nullptr;
            HRESULT levelHr;
            if constexpr (F == 1)
                levelHr = static_cast<IDirect3DTexture9 *>(texture)->GetSurfaceLevel(level, &surface);
            else
                levelHr = static_cast<IDirect3DCubeTexture9 *>(texture)->GetCubeMapSurface(
                    static_cast<D3DCUBEMAP_FACES>(face), level, &surface);
            Check<F>(levelHr, levelLine);
            slot->Attach(deviceDX, surface, access);
            if (surface) surface->Release();
        }
    }
    mDevice = deviceDX;
    mQuality = quality;
    texture->AddRef();
    if (mTexture) mTexture->Release();
    mTexture = texture;
    Manager(device)->Add(static_cast<P *>(this));
    texture->Release();
    return true;
}

template class cTextureBaseDX<cITexture, 1>;
template class cTextureBaseDX<cICubeTexture, 6>;

// ---- cTextureDX ----

void *cTextureDX::operator new(size_t) { return PoolAlloc(kPoolRva); }
void cTextureDX::operator delete(void *block, size_t) { GameAddress<RZFastFixed0x5c>(kPoolRva)->Free(block); }
cITexture *__cdecl cTextureDX::New() { return new cTextureDX; }

namespace {
// A choice of pool (Create's last argument): 0 default, 1 system memory, 2 managed.
// ORIGINAL-BUG: any other choice passes the device pointer as the pool (the original's register reuse).
uint32_t PoolOf(uint32_t choice, cIDeviceDX *device)
{
    switch (choice) {
    case 0: return D3DPOOL_DEFAULT;
    case 1: return D3DPOOL_SYSTEMMEM;
    case 2: return D3DPOOL_MANAGED;
    default: return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(device));
    }
}
// The format a create asks Direct3D for: P8 (0x1c here is the engine's palettised id) needs the device enum's
// substitute (0 refuses the create).
bool FormatFor(cIDeviceDX *device, uint32_t &id)
{
    if (id != 0x1c) return true;
    cIGZUnknown *deviceEnum = device->GetDeviceEnumConst();
    cIGZUnknown *formats = nullptr;
    deviceEnum->QueryInterface(kIidDeviceEnumFormats, reinterpret_cast<void **>(&formats));
    const uint32_t substitute = RawSlot<28, uint32_t>(formats, 4u);
    if (formats) formats->Release();
    if (substitute == 0) return false;
    id = substitute;
    return true;
}
} // namespace

bool cTextureDX::Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t levels,
                        float quality, uint32_t poolChoice)
{
    if (!device->IsDeviceReady()) return false;
    uint32_t id = As<ifaces::cIPixelFormat>(format)->GetFormatId();
    if (!FormatFor(device, id)) return false;
    cIDeviceDX *deviceDX = nullptr;
    device->QueryInterface(cDeviceDX::kIidDeviceDX, reinterpret_cast<void **>(&deviceDX));
    const uint32_t pool = PoolOf(poolChoice, device);
    if (pool == D3DPOOL_DEFAULT) Manager(device)->RefreshAvailableMemory();
    IDirect3DDevice9 *d3d = deviceDX->GetD3DDevice();
    const bool done = Build(device, deviceDX, kAccessLevel + (pool != 0), quality, kTextureLines.create,
                            kTextureLines.createLevel, [&](IDirect3DBaseTexture9 **out) {
                                return d3d->CreateTexture(width, height, levels, 0, static_cast<D3DFORMAT>(id),
                                                          static_cast<D3DPOOL>(pool),
                                                          reinterpret_cast<IDirect3DTexture9 **>(out), nullptr);
                            });
    if (deviceDX) deviceDX->Release();
    return done;
}

bool cTextureDX::CreateRenderTarget(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t levels)
{
    if (!device->IsDeviceReady()) return false;
    cIDeviceDX *deviceDX = nullptr;
    device->QueryInterface(cDeviceDX::kIidDeviceDX, reinterpret_cast<void **>(&deviceDX));
    Manager(device)->RefreshAvailableMemory();
    const D3DFORMAT id = static_cast<D3DFORMAT>(As<ifaces::cIPixelFormat>(format)->GetFormatId());
    IDirect3DDevice9 *d3d = deviceDX->GetD3DDevice();
    const bool done = Build(device, deviceDX, kAccessLevel | kAccessTarget, 1.0f, kTextureLines.target,
                            kTextureLines.targetLevel, [&](IDirect3DBaseTexture9 **out) {
                                return d3d->CreateTexture(width, height, levels, D3DUSAGE_RENDERTARGET, id,
                                                          D3DPOOL_DEFAULT,
                                                          reinterpret_cast<IDirect3DTexture9 **>(out), nullptr);
                            });
    if (deviceDX) deviceDX->Release();
    return done;
}

bool cTextureDX::CreateDepthStencil(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t levels)
{
    if (!device->IsDeviceReady()) return false;
    cIDeviceDX *deviceDX = nullptr;
    device->QueryInterface(cDeviceDX::kIidDeviceDX, reinterpret_cast<void **>(&deviceDX));
    Manager(device)->RefreshAvailableMemory();
    const D3DFORMAT id = static_cast<D3DFORMAT>(As<ifaces::cIPixelFormat>(format)->GetFormatId());
    IDirect3DDevice9 *d3d = deviceDX->GetD3DDevice();
    const bool done = Build(device, deviceDX, kAccessLevel | kAccessDepth, 1.0f, kTextureLines.depth,
                            kTextureLines.depthLevel, [&](IDirect3DBaseTexture9 **out) {
                                return d3d->CreateTexture(width, height, levels, D3DUSAGE_DEPTHSTENCIL, id,
                                                          D3DPOOL_DEFAULT,
                                                          reinterpret_cast<IDirect3DTexture9 **>(out), nullptr);
                            });
    if (deviceDX) deviceDX->Release();
    return done;
}

// ---- cCubeTextureDX ----

void *cCubeTextureDX::operator new(size_t) { return PoolAlloc(kPoolRva); }
void cCubeTextureDX::operator delete(void *block, size_t) { GameAddress<RZFastFixed0x19c>(kPoolRva)->Free(block); }
cICubeTexture *__cdecl cCubeTextureDX::New() { return new cCubeTextureDX; }

bool cCubeTextureDX::Create(cIDeviceDX *device, uint32_t edge, void *format, uint32_t levels, float quality,
                            uint32_t poolChoice)
{
    if (!device->IsDeviceReady()) return false;
    uint32_t id = As<ifaces::cIPixelFormat>(format)->GetFormatId();
    if (!FormatFor(device, id)) return false;
    cIDeviceDX *deviceDX = nullptr;
    device->QueryInterface(cDeviceDX::kIidDeviceDX, reinterpret_cast<void **>(&deviceDX));
    const uint32_t pool = PoolOf(poolChoice, device);
    if (pool == D3DPOOL_DEFAULT) Manager(device)->RefreshAvailableMemory();
    IDirect3DDevice9 *d3d = deviceDX->GetD3DDevice();
    const bool done = Build(device, deviceDX, kAccessLevel + (pool != 0), quality, kCubeLines.create,
                            kCubeLines.createLevel, [&](IDirect3DBaseTexture9 **out) {
                                return d3d->CreateCubeTexture(edge, levels, 0, static_cast<D3DFORMAT>(id),
                                                              static_cast<D3DPOOL>(pool),
                                                              reinterpret_cast<IDirect3DCubeTexture9 **>(out), nullptr);
                            });
    if (deviceDX) deviceDX->Release();
    return done;
}

bool cCubeTextureDX::CreateRenderTarget(cIDeviceDX *device, uint32_t edge, void *format, uint32_t levels)
{
    if (!device->IsDeviceReady()) return false;
    cIDeviceDX *deviceDX = nullptr;
    device->QueryInterface(cDeviceDX::kIidDeviceDX, reinterpret_cast<void **>(&deviceDX));
    Manager(device)->RefreshAvailableMemory();
    IDirect3DDevice9 *d3d = deviceDX->GetD3DDevice();
    const D3DFORMAT id = static_cast<D3DFORMAT>(As<ifaces::cIPixelFormat>(format)->GetFormatId());
    const bool done = Build(device, deviceDX, kAccessLevel | kAccessTarget, 1.0f, kCubeLines.target,
                            kCubeLines.targetLevel, [&](IDirect3DBaseTexture9 **out) {
                                return d3d->CreateCubeTexture(edge, levels, D3DUSAGE_RENDERTARGET, id, D3DPOOL_DEFAULT,
                                                              reinterpret_cast<IDirect3DCubeTexture9 **>(out), nullptr);
                            });
    if (deviceDX) deviceDX->Release();
    return done;
}

bool cCubeTextureDX::CreateDepthStencil(cIDeviceDX *device, uint32_t edge, void *format, uint32_t levels)
{
    if (!device->IsDeviceReady()) return false;
    cIDeviceDX *deviceDX = nullptr;
    device->QueryInterface(cDeviceDX::kIidDeviceDX, reinterpret_cast<void **>(&deviceDX));
    Manager(device)->RefreshAvailableMemory();
    IDirect3DDevice9 *d3d = deviceDX->GetD3DDevice();
    const D3DFORMAT id = static_cast<D3DFORMAT>(As<ifaces::cIPixelFormat>(format)->GetFormatId());
    const bool done = Build(device, deviceDX, kAccessLevel | kAccessDepth, 1.0f, kCubeLines.depth,
                            kCubeLines.depthLevel, [&](IDirect3DBaseTexture9 **out) {
                                return d3d->CreateCubeTexture(edge, levels, D3DUSAGE_DEPTHSTENCIL, id, D3DPOOL_DEFAULT,
                                                              reinterpret_cast<IDirect3DCubeTexture9 **>(out), nullptr);
                            });
    if (deviceDX) deviceDX->Release();
    return done;
}

} // namespace nGZGraphic4
