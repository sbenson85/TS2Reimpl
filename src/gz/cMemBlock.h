#pragma once
// nRZGraphic4::cMemBlockAsSurface / cMemBlockAsTexture and their Direct3D sides cMemBlockAsSurfaceImpl /
// cMemBlockAsTextureImpl (lane RN, milestone O row O-c2; docs/subsystems/scenegraph.md): a surface or texture over
// memory the caller owns. The engine's image code (cImageData, the bump-map strategies, the thumbnail factory, the image
// manipulator) wraps a block of pixels in one of these to hand it to code that takes engine surfaces and textures, and
// D3DX-style code reaches the pixels through the Direct3D faces (IDirect3DSurface9 / IDirect3DTexture9 look-alikes that
// never touch a device).
//
// Standalone shape (docs/standalone-shape.md): every entry point the game calls is hooked (src/hook/tables/cMemBlock.cpp:
// the face constructors 0x18d1a0 / 0x18d1e0 / 0x18c300, their destructors 0x18d2c0 / 0x18c350, the surface's Init
// 0x18d500 and the texture's SetTopLevel 0x18c5b0 / AddLevel 0x18c3e0). The Direct3D sides are made only by Init and
// SetTopLevel, so with those hooks no object carries the game's vtables (each vtable is referenced only by its class's
// constructor and destructor) and every slot is in effect through its callers.
//
// Layouts:
//   cMemBlockAsSurface (12 bytes): +0 vtable (cILockableSurface + deleting destructor, 30 slots), +4 count, +8 the
//     cMemBlockAsSurfaceImpl (COM-counted).
//   cMemBlockAsSurfaceImpl (0x2c): +0 vtable (IDirect3DSurface9 + deleting destructor, 18 slots), +4 count, +8 lock
//     count, +0xc access flags (1; 9 for a texture level), +0x10 the container (the texture's Impl, not counted), +0x14 the
//     bits, +0x18 the pitch, +0x1c / +0x20 width / height, +0x24 the bytes of one 4 x 4 block (block-compressed formats;
//     0 otherwise), +0x28 the pixel format (cIPixelFormat, not counted).
//   cMemBlockAsTexture (12 bytes): +0 vtable (cITexture, 30 slots), +4 count, +8 the cMemBlockAsTextureImpl (COM-counted).
//   cMemBlockAsTextureImpl (0x14): +0 vtable (IDirect3DTexture9 + deleting destructor, 23 slots), +4 count, +8 the levels
//     (a vector of counted cMemBlockAsSurface pointers: begin, end, capacity).
#include "gz/cTextureDX.h"
#include "simantics/cTSBehaviorTree.h" // RefPtrVector

#include <cstdint>

namespace nRZGraphic4 {

using nGZGraphic4::cIDeviceDX;
using nGZGraphic4::cIImageSurface;
using nGZGraphic4::cSurfaceLock;

constexpr uint32_t kMemBlockLevelFlags = 9;      // the access flags of a texture's level (lockable, texture level)
constexpr HRESULT kInvalidCall = HRESULT(0x8876086c); // D3DERR_INVALIDCALL

// The Direct3D side of a memory surface: answers GetDesc and LockRect from the memory; everything else fails or is empty.
class cMemBlockAsSurfaceImpl : public IDirect3DSurface9 {
public:
    // 0x18d200 (ret 0x14): the fields; a block-compressed format (one answering the DXT interface) gives its block bytes.
    cMemBlockAsSurfaceImpl(void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch);
    cMemBlockAsSurfaceImpl(const cMemBlockAsSurfaceImpl &) = delete;
    cMemBlockAsSurfaceImpl &operator=(const cMemBlockAsSurfaceImpl &) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override; // 0: E_NOINTERFACE (0x16a920, shared)
    ULONG STDMETHODCALLTYPE AddRef() override;                                     // 1: 0x18c4c0 (both Impls)
    ULONG STDMETHODCALLTYPE Release() override;                                    // 2: 0x18d8e0
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **device) override;       // 3: 0x18d480
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, const void *data, DWORD size, DWORD flags) override; // 4
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, void *data, DWORD *size) override;                   // 5
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID guid) override;                                           // 6
    DWORD STDMETHODCALLTYPE SetPriority(DWORD priority) override;                  // 7
    DWORD STDMETHODCALLTYPE GetPriority() override;                                // 8
    void STDMETHODCALLTYPE PreLoad() override;                                     // 9
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override;                          // 10: D3DRTYPE_SURFACE
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **container) override; // 11: 0x18d360
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *desc) override;             // 12: 0x18d430
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override; // 13: 0x18d7f0
    HRESULT STDMETHODCALLTYPE UnlockRect() override;                               // 14: 0x18d960
    HRESULT STDMETHODCALLTYPE GetDC(HDC *dc) override;                             // 15
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC dc) override;                          // 16
    virtual ~cMemBlockAsSurfaceImpl();                                             // 17: 0x18d330 (0x18d300)

    uint32_t mRefs = 0;          // +4
    uint32_t mLockCount = 0;     // +8
    uint32_t mAccessFlags = 1;   // +0xc
    IUnknown *mContainer = nullptr; // +0x10
    uint8_t *mBits;              // +0x14
    uint32_t mPitch;             // +0x18
    uint32_t mWidth;             // +0x1c
    uint32_t mHeight;            // +0x20
    uint32_t mBlockBytes = 0;    // +0x24
    void *mFormat;               // +0x28 (cIPixelFormat)
};
static_assert(sizeof(cMemBlockAsSurfaceImpl) == 0x2c, "cMemBlockAsSurfaceImpl: 0x2c bytes");
static_assert(offsetof(cMemBlockAsSurfaceImpl, mRefs) == 4 && offsetof(cMemBlockAsSurfaceImpl, mLockCount) == 8 &&
                  offsetof(cMemBlockAsSurfaceImpl, mAccessFlags) == 0xc &&
                  offsetof(cMemBlockAsSurfaceImpl, mContainer) == 0x10 &&
                  offsetof(cMemBlockAsSurfaceImpl, mBits) == 0x14 && offsetof(cMemBlockAsSurfaceImpl, mWidth) == 0x1c &&
                  offsetof(cMemBlockAsSurfaceImpl, mBlockBytes) == 0x24 &&
                  offsetof(cMemBlockAsSurfaceImpl, mFormat) == 0x28,
              "cMemBlockAsSurfaceImpl layout");

// A lockable engine surface over memory.
class cMemBlockAsSurface : public nGZGraphic4::cILockableSurface {
public:
    cMemBlockAsSurface() = default; // 0x18d1e0: empty (no Impl until Init)
    // 0x18d1a0 (ret 0x14): empty, then Init.
    cMemBlockAsSurface(void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch);
    cMemBlockAsSurface(const cMemBlockAsSurface &) = delete;
    cMemBlockAsSurface &operator=(const cMemBlockAsSurface &) = delete;

    // 0x18d500 (ret 0x14): lets go of the Impl it had (ReleaseResource) and makes a new one over the memory, counted.
    void Init(void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch);

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0x18d880
    uint32_t AddRef() override { return ++mRefs; }           // 1: 0x148fb0 (shared)
    uint32_t Release() override;                             // 2: 0x178b40 (shared)
    void ReleaseResource() override;                         // 3: 0x18c7d0 (both faces): the Impl let go
    cIDeviceDX *GetDevice() override { return nullptr; }     // 4
    cIDeviceDX *GetDeviceConst() override { return nullptr; } // 5
    bool IsCreated() override { return mImpl != nullptr; }   // 6: 0x17da80 (shared)
    uint32_t GetMemorySize() override { return 0; }          // 7
    bool SurvivesReset() override { return true; }           // 8
    bool SurvivesModeChange() override { return true; }      // 9
    // 10: the original's `ret 4` leaves eax as its caller had it (no result); ours answers 0.
    uint32_t SetPriority(uint32_t) override { return 0; }
    void PreLoad() override {}                               // 11
    uint32_t GetPool() override { return D3DPOOL_SCRATCH; }  // 12: 0x18c4d0 (shared)
    uint32_t GetVideoMemorySize() override { return 0; }     // 13
    uint32_t GetPriority() override { return 0; }            // 14
    bool CopyRect(const POINT *, nGZGraphic4::cISurface *, const RECT *) override { return false; }    // 15
    bool Copy(nGZGraphic4::cISurface *) override { return false; }                                      // 16
    bool StretchRect(const RECT *, nGZGraphic4::cISurface *, const RECT *) override { return false; }  // 17
    bool Stretch(nGZGraphic4::cISurface *) override { return false; }                                   // 18
    uint32_t GetAccessFlags() override { return mImpl->mAccessFlags; } // 19: 0x18d900
    uint32_t GetWidth() override { return mImpl->mWidth; }             // 20: 0x18d980
    uint32_t GetHeight() override { return mImpl->mHeight; }           // 21: 0x18d4f0
    void *GetPixelFormat() override { return mImpl->mFormat; }         // 22: 0x18d350
    uint32_t GetMultiSampleType() override { return 0; }               // 23
    uint32_t GetMultiSampleQuality() override { return 0; }            // 24
    uint32_t LockRect(cSurfaceLock *lock, int32_t x, int32_t y, int32_t width, int32_t height, bool readOnly) override; // 25: 0x18d580
    int32_t Lock(cSurfaceLock *lock, bool readOnly) override;          // 26: 0x18d710
    void Unlock(cSurfaceLock *lock) override;                          // 27: 0x18d910
    uint32_t GetLockCount() override { return mImpl->mLockCount; }     // 28: 0x18d870
    virtual ~cMemBlockAsSurface();                                     // 29: 0x18d310 (0x18d2c0)

    // The hooked entry points (__fastcall: `this` in ecx, the dummy edx, then the original's stack arguments). Our own
    // callers use them too (cDraw2D, the image manipulator), so the tests' stand-ins reach both sides alike.
    static cMemBlockAsSurface *__fastcall Construct(void *memory, void *, void *bits, void *format, uint32_t width,
                                                    uint32_t height, uint32_t pitch); // 0x18d1a0
    static cMemBlockAsSurface *__fastcall ConstructEmpty(void *memory, void *);       // 0x18d1e0
    static void __fastcall Destruct(cMemBlockAsSurface *self, void *);                // 0x18d2c0

    uint32_t mRefs = 0;                        // +4
    cMemBlockAsSurfaceImpl *mImpl = nullptr;   // +8
};
static_assert(sizeof(cMemBlockAsSurface) == 12, "cMemBlockAsSurface: 12 bytes");

// The Direct3D side of a memory texture: its levels are the faces' Impls.
class cMemBlockAsTextureImpl : public IDirect3DTexture9 {
public:
    cMemBlockAsTextureImpl() = default; // 0x18c320
    cMemBlockAsTextureImpl(const cMemBlockAsTextureImpl &) = delete;
    cMemBlockAsTextureImpl &operator=(const cMemBlockAsTextureImpl &) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override; // 0: E_NOINTERFACE (0x16a920, shared)
    ULONG STDMETHODCALLTYPE AddRef() override;                                     // 1: 0x18c4c0 (both Impls)
    ULONG STDMETHODCALLTYPE Release() override;                                    // 2: 0x18c7b0
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **device) override;       // 3: 0x18c4f0: level 0's
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, const void *data, DWORD size, DWORD flags) override; // 4
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, void *data, DWORD *size) override;                   // 5
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID guid) override;                                           // 6
    DWORD STDMETHODCALLTYPE SetPriority(DWORD priority) override;                  // 7
    DWORD STDMETHODCALLTYPE GetPriority() override;                                // 8
    void STDMETHODCALLTYPE PreLoad() override;                                     // 9
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override;                          // 10: 0x18c570, D3DRTYPE_TEXTURE
    DWORD STDMETHODCALLTYPE SetLOD(DWORD lod) override;                            // 11
    DWORD STDMETHODCALLTYPE GetLOD() override;                                     // 12
    DWORD STDMETHODCALLTYPE GetLevelCount() override;                              // 13: 0x18c510
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE type) override; // 14
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override;        // 15
    void STDMETHODCALLTYPE GenerateMipSubLevels() override;                        // 16
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC *desc) override; // 17: 0x18c520
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level, IDirect3DSurface9 **surface) override; // 18: 0x18c540
    HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override; // 19: 0x18c6d0
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override;                     // 20: 0x18c970
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT *rect) override;             // 21
    virtual ~cMemBlockAsTextureImpl();                                             // 22: 0x18c3c0 (0x18c390)

    cMemBlockAsSurface *Level(uint32_t level) const { return static_cast<cMemBlockAsSurface *>(mLevels.mFirst[level]); }

    uint32_t mRefs = 0;           // +4
    RefPtrVector mLevels = {};    // +8: cMemBlockAsSurface (counted)
};
static_assert(sizeof(cMemBlockAsTextureImpl) == 0x14, "cMemBlockAsTextureImpl: 0x14 bytes");
static_assert(offsetof(cMemBlockAsTextureImpl, mLevels) == 8, "cMemBlockAsTextureImpl: levels at +8");

// An engine texture over memory, one block per level.
class cMemBlockAsTexture : public nGZGraphic4::cITexture {
public:
    cMemBlockAsTexture() = default; // 0x18c300
    cMemBlockAsTexture(const cMemBlockAsTexture &) = delete;
    cMemBlockAsTexture &operator=(const cMemBlockAsTexture &) = delete;

    // 0x18c5b0 (ret 0x14): lets go of the levels it had and starts a new Impl whose top level is the memory.
    void SetTopLevel(void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch);
    // 0x18c3e0 (ret 0x10): appends the next level (in the top level's format).
    void AddLevel(void *bits, uint32_t width, uint32_t height, uint32_t pitch);

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0x18c720
    uint32_t AddRef() override { return ++mRefs; }           // 1: 0x148fb0 (shared)
    uint32_t Release() override;                             // 2: 0x178b40 (shared)
    void ReleaseResource() override;                         // 3: 0x18c7d0 (both faces)
    cIDeviceDX *GetDevice() override { return nullptr; }     // 4
    cIDeviceDX *GetDeviceConst() override { return nullptr; } // 5
    bool IsCreated() override;                               // 6: 0x18c6b0: at least one level
    uint32_t GetMemorySize() override { return 0; }          // 7
    bool SurvivesReset() override { return true; }           // 8
    bool SurvivesModeChange() override { return true; }      // 9
    uint32_t SetPriority(uint32_t) override { return 0; }    // 10: `ret 4` in the original, eax untouched
    void PreLoad() override {}                               // 11
    uint32_t GetPool() override { return D3DPOOL_SCRATCH; }  // 12: 0x18c4d0 (shared)
    uint32_t GetVideoMemorySize() override { return 0; }     // 13
    uint32_t GetPriority() override { return 0; }            // 14
    void SetQuality(float) override {}                       // 15
    uint32_t GetLevelCount() override { return mImpl->mLevels.Size(); } // 16: 0x18c710
    // 17 / 18 / 19: level 0's, whatever level is asked (0x18ce50, 0x18c580, 0x18c4e0).
    uint32_t GetWidth(uint32_t) override { return mImpl->Level(0)->mImpl->mWidth; }
    uint32_t GetHeight(uint32_t) override { return mImpl->Level(0)->mImpl->mHeight; }
    void *GetPixelFormat() override { return mImpl->Level(0)->mImpl->mFormat; }
    uint32_t GetLOD() override { return 0; }                 // 20
    float GetQuality() override { return 1.0f; }             // 21: 0x18c7a0 (shared)
    bool Create(cIDeviceDX *, uint32_t, uint32_t, void *, uint32_t, float, uint32_t) override { return false; } // 22
    bool CreateRenderTarget(cIDeviceDX *, uint32_t, uint32_t, void *, uint32_t) override { return false; }      // 23
    bool CreateDepthStencil(cIDeviceDX *, uint32_t, uint32_t, void *, uint32_t) override { return false; }      // 24
    // 25-28: 0x18c700, the level's face (not counted). The levels are lockable surfaces, not cIImageSurface: callers
    // use the slots they share (0-28).
    cIImageSurface *GetSurface(uint32_t level) override { return LevelSurface(level); }
    cIImageSurface *GetTargetSurface(uint32_t level) override { return LevelSurface(level); }
    cIImageSurface *GetLockableSurface(uint32_t level) override { return LevelSurface(level); }
    cIImageSurface *GetTargetLockableSurface(uint32_t level) override { return LevelSurface(level); }
    ~cMemBlockAsTexture() override; // 29: 0x18c3a0 (0x18c350)

    static cMemBlockAsTexture *__fastcall Construct(void *memory, void *); // 0x18c300
    static void __fastcall Destruct(cMemBlockAsTexture *self, void *);     // 0x18c350

    uint32_t mRefs = 0;                       // +4
    cMemBlockAsTextureImpl *mImpl = nullptr;  // +8

    // 0x18c700, slots 25-28 and called directly by the game's image code: the level's face, not counted.
    cIImageSurface *LevelSurface(uint32_t level)
    {
        return reinterpret_cast<cIImageSurface *>(mImpl->mLevels.mFirst[level]);
    }

private:
    void AppendLevel(void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch);
};
static_assert(sizeof(cMemBlockAsTexture) == 12, "cMemBlockAsTexture: 12 bytes");

} // namespace nRZGraphic4
