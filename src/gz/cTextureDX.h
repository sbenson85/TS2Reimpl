#pragma once
// nGZGraphic4::cTextureDX and cCubeTextureDX (lane NJ, rendering R3; the surfaces are in gz/cSurfaceDX.h).
//
// A texture keeps its Direct3D texture and one cMipSurfaceDX per level (per face and level for the cube), made when
// the texture is created and kept (counted) until the texture goes; ReleaseResource only detaches them. Standalone
// shape: the creators 0x163600 / 0x1648a0 are hooked (src/hook/tables/cTextureDX.cpp), every slot is in effect through
// its callers.
//
// Layout: +0 cITexture / cICubeTexture, +4 cITextureBaseDX, +8 reference count, +0xc quality (float), +0x10 the device
// (cIDeviceDX, not counted), +0x14 the IDirect3DTexture9 / IDirect3DCubeTexture9 (counted), +0x18 the level count
// (at most 16), +0x1c the levels (16, or 6 x 16 for the cube: face * 16 + level).
#include "gz/cSurfaceDX.h"

namespace nGZGraphic4 {

constexpr uint32_t kIidTextureBase = 0x694312a3;
constexpr uint32_t kIidTexture = 0x5b891e1e;
constexpr uint32_t kIidCubeTexture = 0xe9443438;
constexpr uint32_t kIidTextureBaseDX = 0x1b891e38;  // the +4 face
constexpr uint32_t kIidD3DBaseTexture = 0x998dbd2;  // +0x14, not counted (answered before the null check)
constexpr uint32_t kIidD3DTexture = 0x8b54da90;
constexpr uint32_t kIidD3DCubeTexture = 0xab54dab1;

// nGZGraphic4::cITextureBase (15-21).
class cITextureBase : public cIAllocDeviceResource {
public:
    // 15: the share of the levels the device's budget may drop (1 all kept); tells the manager the managed bytes it
    // moves.
    virtual void SetQuality(float quality) = 0;
    virtual uint32_t GetLevelCount() = 0;            // 16
    virtual uint32_t GetWidth(uint32_t level) = 0;   // 17
    virtual uint32_t GetHeight(uint32_t level) = 0;  // 18
    virtual void *GetPixelFormat() = 0;              // 19 (cIPixelFormat; P8 answers as A8)
    virtual uint32_t GetLOD() = 0;                   // 20: the managed texture's LOD (0 outside the managed pool)
    virtual float GetQuality() = 0;                  // 21
};

class cITexture : public cITextureBase {
public:
    // 22: poolChoice 0 default (the manager refreshes first), 1 system memory, 2 managed.
    virtual bool Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t levels,
                        float quality, uint32_t poolChoice) = 0;
    virtual bool CreateRenderTarget(cIDeviceDX *device, uint32_t width, uint32_t height, void *format,
                                    uint32_t levels) = 0; // 23
    virtual bool CreateDepthStencil(cIDeviceDX *device, uint32_t width, uint32_t height, void *format,
                                    uint32_t levels) = 0; // 24
    virtual cIImageSurface *GetSurface(uint32_t level) = 0;               // 25
    virtual cIImageSurface *GetTargetSurface(uint32_t level) = 0;         // 26
    virtual cIImageSurface *GetLockableSurface(uint32_t level) = 0;       // 27
    virtual cIImageSurface *GetTargetLockableSurface(uint32_t level) = 0; // 28
    virtual ~cITexture() = default;                                       // 29
};

class cICubeTexture : public cITextureBase {
public:
    virtual bool Create(cIDeviceDX *device, uint32_t edge, void *format, uint32_t levels, float quality,
                        uint32_t poolChoice) = 0;                                                    // 22
    virtual bool CreateRenderTarget(cIDeviceDX *device, uint32_t edge, void *format, uint32_t levels) = 0; // 23
    virtual bool CreateDepthStencil(cIDeviceDX *device, uint32_t edge, void *format, uint32_t levels) = 0; // 24
    virtual cIImageSurface *GetSurface(uint32_t face, uint32_t level) = 0;               // 25
    virtual cIImageSurface *GetTargetSurface(uint32_t face, uint32_t level) = 0;         // 26
    virtual cIImageSurface *GetLockableSurface(uint32_t face, uint32_t level) = 0;       // 27
    virtual cIImageSurface *GetTargetLockableSurface(uint32_t face, uint32_t level) = 0; // 28
    virtual ~cICubeTexture() = default;                                                  // 29
};

// nGZGraphic4::cITextureBaseDX: the +4 face.
class cITextureBaseDX : public cITextureBase {
public:
    virtual cIDeviceDX *GetDeviceDX() = 0;                  // 22
    virtual IDirect3DBaseTexture9 *GetD3DTexture() = 0;     // 23
    virtual uint32_t GetD3DFormat() = 0;                    // 24: level 0's
    virtual uint32_t GetLevelBytes(uint32_t level) = 0;     // 25 (all faces for the cube)
};

template <class Primary, unsigned Faces> class cTextureBaseDX : public Primary, public cITextureBaseDX {
public:
    static constexpr unsigned kSlots = 16 * Faces;
    static constexpr uint32_t kSize = 0x1c + 4 * kSlots;

    cTextureBaseDX() = default;
    cTextureBaseDX(const cTextureBaseDX &) = delete;
    cTextureBaseDX &operator=(const cTextureBaseDX &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override { return ++mRefs; }
    uint32_t Release() override;
    void ReleaseResource() override;
    cIDeviceDX *GetDevice() override { return mDevice; }
    cIDeviceDX *GetDeviceConst() override { return mDevice; }
    bool IsCreated() override { return mTexture != nullptr; }
    uint32_t GetMemorySize() override;
    bool SurvivesReset() override;
    bool SurvivesModeChange() override;
    uint32_t SetPriority(uint32_t priority) override { return mTexture->SetPriority(priority); }
    void PreLoad() override { mTexture->PreLoad(); }
    uint32_t GetPool() override;
    uint32_t GetVideoMemorySize() override;
    uint32_t GetPriority() override { return mTexture->GetPriority(); }
    void SetQuality(float quality) override;
    uint32_t GetLevelCount() override { return mLevels; }
    uint32_t GetWidth(uint32_t level) override;
    uint32_t GetHeight(uint32_t level) override;
    void *GetPixelFormat() override;
    uint32_t GetLOD() override;
    float GetQuality() override { return mQuality; }

    cIDeviceDX *GetDeviceDX() override { return mDevice; }
    IDirect3DBaseTexture9 *GetD3DTexture() override { return mTexture; }
    uint32_t GetD3DFormat() override;
    uint32_t GetLevelBytes(uint32_t level) override;

protected:
    ~cTextureBaseDX();
    HRESULT LevelDesc(uint32_t level, D3DSURFACE_DESC *desc);
    // The creates' common part, after the Direct3D texture is made: the level surfaces and the registration.
    // `make` makes the Direct3D texture into its argument and answers its HRESULT.
    template <class Make>
    bool Build(cIDeviceDX *device, cIDeviceDX *deviceDX, uint32_t access, float quality, uint32_t createLine,
               uint32_t levelLine, Make make);
    void Detach();

public:
    uint32_t mRefs = 0;                      // +8
    float mQuality = 1.0f;                   // +0xc
    cIDeviceDX *mDevice = nullptr;           // +0x10
    IDirect3DBaseTexture9 *mTexture = nullptr; // +0x14
    uint32_t mLevels = 0;                    // +0x18
    cMipSurfaceDX *mMips[kSlots] = {};       // +0x1c (counted)
};

class cTextureDX final : public cTextureBaseDX<cITexture, 1> {
public:
    static constexpr uint32_t kPoolRva = 0xfec630;
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);
    static cITexture *__cdecl New(); // the creator 0x163600 (hooked)

    bool Create(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t levels, float quality,
                uint32_t poolChoice) override;
    bool CreateRenderTarget(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t levels) override;
    bool CreateDepthStencil(cIDeviceDX *device, uint32_t width, uint32_t height, void *format, uint32_t levels) override;
    cIImageSurface *GetSurface(uint32_t level) override { return mMips[level]; }
    cIImageSurface *GetTargetSurface(uint32_t level) override { return mMips[level]; }
    cIImageSurface *GetLockableSurface(uint32_t level) override { return mMips[level]; }
    cIImageSurface *GetTargetLockableSurface(uint32_t level) override { return mMips[level]; }
    ~cTextureDX() override = default;
};

class cCubeTextureDX final : public cTextureBaseDX<cICubeTexture, 6> {
public:
    static constexpr uint32_t kPoolRva = 0xfec898;
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);
    static cICubeTexture *__cdecl New(); // the creator 0x1648a0 (hooked)

    bool Create(cIDeviceDX *device, uint32_t edge, void *format, uint32_t levels, float quality,
                uint32_t poolChoice) override;
    bool CreateRenderTarget(cIDeviceDX *device, uint32_t edge, void *format, uint32_t levels) override;
    bool CreateDepthStencil(cIDeviceDX *device, uint32_t edge, void *format, uint32_t levels) override;
    cIImageSurface *GetSurface(uint32_t face, uint32_t level) override { return mMips[face * 16 + level]; }
    cIImageSurface *GetTargetSurface(uint32_t face, uint32_t level) override { return mMips[face * 16 + level]; }
    cIImageSurface *GetLockableSurface(uint32_t face, uint32_t level) override { return mMips[face * 16 + level]; }
    cIImageSurface *GetTargetLockableSurface(uint32_t face, uint32_t level) override { return mMips[face * 16 + level]; }
    ~cCubeTextureDX() override = default;
};

static_assert(sizeof(cTextureDX) == 0x5c, "cTextureDX: 0x5c bytes");
static_assert(sizeof(cCubeTextureDX) == 0x19c, "cCubeTextureDX: 0x19c bytes");

} // namespace nGZGraphic4
