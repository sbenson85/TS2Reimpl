#pragma once
// The Direct3D resource classes of nGZGraphic4 (lane NJ, rendering R3): the surfaces (cImageSurfaceDX,
// cDepthStencilSurfaceDX, cRenderTargetDX, cLockableRenderTargetDX, cMipSurfaceDX) and the textures (cTextureDX,
// cCubeTextureDX, src/gz/cTextureDX.h). docs/subsystems/rendering.md, R3.
//
// Standalone shape (docs/standalone-shape.md): each class is made by a cdecl creator (no arguments; the object comes
// from the class's fixed-size pool with a reference count of 0). The creators are hooked (src/hook/tables/cSurfaceDX.cpp,
// cTextureDX.cpp): they are the switch, so the game's vtables are never reached and every slot is in effect through
// its callers. The mips are made only by the textures (their level surfaces), so they follow the texture creators.
//
// Layout of every surface (two faces, as the original's):
//   +0 the primary interface (cIImageSurface, cIDepthStencilSurface, cIRenderTarget or cILockableRenderTarget)
//   +4 cISurfaceDX (the Direct3D side: Init, the device, the IDirect3DSurface9)
//   +8 reference count, +0xc the device (cIDeviceDX, not counted), +0x10 / +0x12 width / height (u16, from GetDesc),
//   +0x14 the IDirect3DSurface9 (counted); the lockables +0x18 their lock count; the mip +0x1c its access flags.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d9.h>

#include <cstddef>
#include <cstdint>

#include "gz/cIGZUnknown.h"

#include "game/owned.h"
#include "rz/RZFastFixedAllocators.h"

// The classes' fixed-size pools (operator new / delete).
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x1c>, 10ca058);
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x20>, fec63c);
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x1c>, 10ca07c);
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x18>, 10ca064);
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x18>, 10ca070);
// The surfaces' LogD3DError source files (the surface file; the texture file, for cMipSurfaceDX), the IID the
// surface code asks GetContainer for (IID_IDirect3DBaseTexture9) and cSurfaceCopy's device enumeration kind for
// each surface class (0-5).
TS2_OWNED_STATIC(const char, d9dab0)[];
TS2_OWNED_STATIC(const char, d9dcd8)[];
TS2_OWNED_STATIC(const IID, d9c8a8);
TS2_OWNED_STATIC(const uint32_t, d9db08)[6];

namespace nGZGraphic4 {

class cIDeviceDX;

// Interface ids the resources answer (QueryInterface). The split of the two resource ids between cIDeviceResource and
// cIAllocDeviceResource is a guess; both are answered by every surface and texture.
constexpr uint32_t kIidDeviceResource = 0x973e60f;
constexpr uint32_t kIidAllocDeviceResource = 0xda02e09f;
constexpr uint32_t kIidSurface = 0x9b71bf9d;
constexpr uint32_t kIidSurfaceDX = 0x5b71b5ce;            // the +4 face
constexpr uint32_t kIidImageSurface = 0x1b75b70b;
constexpr uint32_t kIidLockableSurface = 0xdb75b6e9;
constexpr uint32_t kIidDepthStencilSurface = 0xfb75b72b;
constexpr uint32_t kIidRenderTarget = 0xbb75b749;
constexpr uint32_t kIidLockableRenderTarget = 0x4dee64cf;
constexpr uint32_t kIidD3DSurface = 0x4a5a7223;           // IDirect3DSurface9 (+0x14, not counted)
constexpr uint32_t kIidD3DResource = 0xcb60e61d;          // IDirect3DResource9 (+0x14, not counted)


// nGZGraphic4::cIDeviceResource / cIAllocDeviceResource (slots 3-14).
class cIDeviceResource : public cIGZUnknown {
public:
    virtual void ReleaseResource() = 0;            // 3: unregister and release the Direct3D object
    virtual cIDeviceDX *GetDevice() = 0;           // 4
    virtual cIDeviceDX *GetDeviceConst() = 0;      // 5
    virtual bool IsCreated() = 0;                  // 6: the Direct3D object exists
    virtual uint32_t GetMemorySize() = 0;          // 7: the object's size plus the bytes outside the default pool
    virtual bool SurvivesReset() = 0;              // 8: not in the default pool (the device keeps it over a reset)
    virtual bool SurvivesModeChange() = 0;         // 9: kept over a display format change
};
class cIAllocDeviceResource : public cIDeviceResource {
public:
    virtual uint32_t SetPriority(uint32_t priority) = 0; // 10: the Direct3D resource's
    virtual void PreLoad() = 0;                          // 11
    virtual uint32_t GetPool() = 0;                      // 12: D3DPOOL
    virtual uint32_t GetVideoMemorySize() = 0;           // 13: bytes in the default / managed pools (0 otherwise)
    virtual uint32_t GetPriority() = 0;                  // 14
};

// A lock of a lockable surface (0x24 bytes): the surface (counted while locked), the locked rectangle, the bits, the
// pitch, the bytes of one row and the number of rows (block rows for compressed formats).
struct cSurfaceLock {
    void *surface;     // +0 cILockableSurface (counted)
    RECT rect;         // +4
    void *bits;        // +0x14
    int32_t pitch;     // +0x18
    uint32_t rowBytes; // +0x1c
    uint32_t rows;     // +0x20
};
static_assert(sizeof(cSurfaceLock) == 0x24, "cSurfaceLock: 0x24 bytes");

// nGZGraphic4::cISurface (slots 15-24).
class cISurface : public cIAllocDeviceResource {
public:
    // 15: copies `sourceRect` of `source` (whole when null) to `targetPoint` (the origin when null), clipped.
    virtual bool CopyRect(const POINT *targetPoint, cISurface *source, const RECT *sourceRect) = 0;
    virtual bool Copy(cISurface *source) = 0;                                                    // 16: the whole source
    // 17: StretchRect of `sourceRect` into `targetRect` (clipped to both surfaces; true when nothing is left).
    virtual bool StretchRect(const RECT *targetRect, cISurface *source, const RECT *sourceRect) = 0;
    virtual bool Stretch(cISurface *source) = 0;                                                 // 18: whole surfaces
    // 19: kind flags: 1 lockable, 2 depth stencil, 4 render target, 8 a texture level.
    virtual uint32_t GetAccessFlags() = 0;
    virtual uint32_t GetWidth() = 0;           // 20
    virtual uint32_t GetHeight() = 0;          // 21
    virtual void *GetPixelFormat() = 0;        // 22: cIPixelFormat
    virtual uint32_t GetMultiSampleType() = 0; // 23
    virtual uint32_t GetMultiSampleQuality() = 0; // 24
};

// nGZGraphic4::cILockableSurface (25-28).
class cILockableSurface : public cISurface {
public:
    // 25: locks the rectangle (x, y, width, height) clipped to the surface; returns the rows (0 when nothing is left).
    virtual uint32_t LockRect(cSurfaceLock *lock, int32_t x, int32_t y, int32_t width, int32_t height, bool readOnly) = 0;
    // 26: locks the whole surface. The original's eax is the pitch (callers treat it as a bool).
    virtual int32_t Lock(cSurfaceLock *lock, bool readOnly) = 0;
    virtual void Unlock(cSurfaceLock *lock) = 0; // 27
    virtual uint32_t GetLockCount() = 0;         // 28
};

// The leaves (their last slot is the deleting destructor).
class cIImageSurface : public cILockableSurface {
public:
    // 29: an offscreen plain surface; poolChoice 0 default, 1 system memory, 2 scratch, 3 system memory when the
    // display can use the format, scratch otherwise.
    virtual bool Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t poolChoice) = 0;
    virtual ~cIImageSurface() = default; // 30
};
class cILockableRenderTarget : public cILockableSurface {
public:
    virtual bool Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format) = 0; // 29
    virtual ~cILockableRenderTarget() = default;                                                // 30
};
class cIDepthStencilSurface : public cISurface {
public:
    virtual bool Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t multiSample,
                        uint32_t multiSampleQuality) = 0; // 25
    virtual ~cIDepthStencilSurface() = default;           // 26
};
class cIRenderTarget : public cISurface {
public:
    virtual bool Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t multiSample,
                        uint32_t multiSampleQuality) = 0; // 25
    virtual ~cIRenderTarget() = default;                  // 26
};

// nGZGraphic4::cISurfaceDX: the +4 face.
class cISurfaceDX : public cIGZUnknown {
public:
    virtual cISurface *GetSurface() = 0;      // 3: the primary face
    virtual cISurface *GetSurfaceConst() = 0; // 4
    // 5: takes `surface` (counted) and its size; registers the resource with the device's manager.
    virtual void Init(cIDeviceDX *device, IDirect3DSurface9 *surface) = 0;
    virtual cIDeviceDX *GetDevice() = 0;            // 6
    virtual IDirect3DSurface9 *GetD3DSurface() = 0; // 7
};

// What the surfaces share (one template per primary interface, as the original's cSurfaceDX<>).
template <class Primary> class cSurfaceDX : public Primary, public cISurfaceDX {
public:
    cSurfaceDX() = default;
    cSurfaceDX(const cSurfaceDX &) = delete;
    cSurfaceDX &operator=(const cSurfaceDX &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override { return ++mRefs; }
    uint32_t Release() override;
    void ReleaseResource() override;
    cIDeviceDX *GetDevice() override { return mDevice; }
    cIDeviceDX *GetDeviceConst() override { return mDevice; }
    bool IsCreated() override { return mSurface != nullptr; }
    uint32_t GetMemorySize() override;
    bool SurvivesReset() override;
    bool SurvivesModeChange() override { return false; }
    uint32_t SetPriority(uint32_t priority) override { return mSurface->SetPriority(priority); }
    void PreLoad() override { mSurface->PreLoad(); }
    uint32_t GetPool() override;
    uint32_t GetVideoMemorySize() override;
    uint32_t GetPriority() override { return mSurface->GetPriority(); }
    bool CopyRect(const POINT *targetPoint, cISurface *source, const RECT *sourceRect) override;
    bool Copy(cISurface *source) override;
    bool StretchRect(const RECT *targetRect, cISurface *source, const RECT *sourceRect) override;
    bool Stretch(cISurface *source) override;
    uint32_t GetWidth() override { return mWidth; }
    uint32_t GetHeight() override { return mHeight; }
    void *GetPixelFormat() override;
    uint32_t GetMultiSampleType() override;
    uint32_t GetMultiSampleQuality() override;

    cISurface *GetSurface() override { return this; }
    cISurface *GetSurfaceConst() override { return this; }
    void Init(cIDeviceDX *device, IDirect3DSurface9 *surface) override;
    IDirect3DSurface9 *GetD3DSurface() override { return mSurface; }

    // The texture levels' 0x162c80: Init without the manager.
    void SetSurface(cIDeviceDX *device, IDirect3DSurface9 *surface);

protected:
    ~cSurfaceDX();
    bool AnswerCommon(uint32_t riid, void **out);
    // The common Create tail: CreateDepthStencilSurface / CreateRenderTarget with the device's checks around it.
    template <class Make> bool CreateWith(cIDeviceDX *device, void *format, bool refresh, uint32_t line, Make make);

public:
    uint32_t mRefs = 0;               // +8
    cIDeviceDX *mDevice = nullptr;    // +0xc
    uint16_t mWidth = 0;              // +0x10
    uint16_t mHeight = 0;             // +0x12
    IDirect3DSurface9 *mSurface = nullptr; // +0x14
};

template <class Primary> class cLockableSurfaceDX : public cSurfaceDX<Primary> {
public:
    // 0x1628e0 for cIImageSurface (+4 face thunk 0x16295d): the lockable id, then what every surface answers. Only an
    // abstract base's: every complete surface overrides it, so nothing in the game reaches it.
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t LockRect(cSurfaceLock *lock, int32_t x, int32_t y, int32_t width, int32_t height, bool readOnly) override;
    int32_t Lock(cSurfaceLock *lock, bool readOnly) override;
    void Unlock(cSurfaceLock *lock) override;
    uint32_t GetLockCount() override { return mLockCount; }

    uint32_t mLockCount = 0; // +0x18

protected:
    ~cLockableSurfaceDX() = default;
};

class cImageSurfaceDX : public cLockableSurfaceDX<cIImageSurface> {
public:
    static constexpr uint32_t kSize = 0x1c, kPoolRva = 0x10ca058;
    static rz::cRZFastFixedAllocator2<0x1c> &Pool() { return ts2_rva_10ca058; }
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);
    static cIImageSurface *__cdecl New(); // the creator 0x161d40 (hooked)

    bool QueryInterface(uint32_t riid, void **out) override;
    bool SurvivesModeChange() override;
    uint32_t GetAccessFlags() override { return 1; }
    bool Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t poolChoice) override;
    ~cImageSurfaceDX() override = default;
};

// A texture level (made by cTextureDX / cCubeTextureDX for each level they create).
class cMipSurfaceDX final : public cImageSurfaceDX {
public:
    static constexpr uint32_t kSize = 0x20, kPoolRva = 0xfec63c;
    static rz::cRZFastFixedAllocator2<0x20> &Pool() { return ts2_rva_fec63c; }
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);

    void ReleaseResource() override {} // 3: the texture owns the level (0x140fe0)
    bool SurvivesModeChange() override { return false; } // 9: the texture answers for its levels (0x147480)
    uint32_t GetAccessFlags() override { return mAccess; }
    void *GetPixelFormat() override;
    ~cMipSurfaceDX() override;

    // 0x163ff0: the access flags, then SetSurface.
    void Attach(cIDeviceDX *device, IDirect3DSurface9 *surface, uint32_t access)
    {
        mAccess = access;
        SetSurface(device, surface);
    }

    uint32_t mAccess = 0; // +0x1c: 8 | 4 render target | 2 depth stencil | 1 not in the default pool
};

class cLockableRenderTargetDX final : public cLockableSurfaceDX<cILockableRenderTarget> {
public:
    static constexpr uint32_t kSize = 0x1c, kPoolRva = 0x10ca07c;
    static rz::cRZFastFixedAllocator2<0x1c> &Pool() { return ts2_rva_10ca07c; }
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);
    static cILockableRenderTarget *__cdecl New(); // 0x161da0 (hooked)

    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t GetAccessFlags() override { return 5; }
    bool Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format) override;
    ~cLockableRenderTargetDX() override = default;
};

class cDepthStencilSurfaceDX final : public cSurfaceDX<cIDepthStencilSurface> {
public:
    static constexpr uint32_t kSize = 0x18, kPoolRva = 0x10ca064;
    static rz::cRZFastFixedAllocator2<0x18> &Pool() { return ts2_rva_10ca064; }
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);
    static cIDepthStencilSurface *__cdecl New(); // 0x161ce0 (hooked)

    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t GetAccessFlags() override { return 2; }
    bool Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t multiSample,
                uint32_t multiSampleQuality) override;
    ~cDepthStencilSurfaceDX() override = default;
};

class cRenderTargetDX final : public cSurfaceDX<cIRenderTarget> {
public:
    static constexpr uint32_t kSize = 0x18, kPoolRva = 0x10ca070;
    static rz::cRZFastFixedAllocator2<0x18> &Pool() { return ts2_rva_10ca070; }
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);
    static cIRenderTarget *__cdecl New(); // 0x161e00 (hooked)

    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t GetAccessFlags() override { return 4; }
    bool Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t multiSample,
                uint32_t multiSampleQuality) override;
    ~cRenderTargetDX() override = default;
};

static_assert(sizeof(cImageSurfaceDX) == cImageSurfaceDX::kSize, "cImageSurfaceDX: 0x1c bytes");
static_assert(sizeof(cMipSurfaceDX) == cMipSurfaceDX::kSize, "cMipSurfaceDX: 0x20 bytes");
static_assert(sizeof(cLockableRenderTargetDX) == cLockableRenderTargetDX::kSize, "cLockableRenderTargetDX: 0x1c bytes");
static_assert(sizeof(cDepthStencilSurfaceDX) == cDepthStencilSurfaceDX::kSize, "cDepthStencilSurfaceDX: 0x18 bytes");
static_assert(sizeof(cRenderTargetDX) == cRenderTargetDX::kSize, "cRenderTargetDX: 0x18 bytes");

// The surface copies are ours (gz/cSurfaceCopy.h, lane OS): 0x1614b0 copies a source rectangle to a target
// point by the cheapest way the pools allow; 0x162d60 says whether StretchRect can do a copy (and its filter);
// 0x160ef0 locks both and copies the rows; 0x161b20 makes a system-memory twin of a surface.

} // namespace nGZGraphic4
