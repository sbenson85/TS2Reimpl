#pragma once
// nTSSG render surfaces (lane 261108c, scenegraph TS1): the targets the dynamic display-list render strategy
// (src/sg/cDisplayListDynamicRenderStrategy.h) draws into: the device's back buffer, a pair of render-target and
// depth-stencil surfaces, or a scene-graph render-target texture. The strategy builds them in its slot 8 (0x375b00:
// new 0x70 / 0x74 / 0xc4 and the constructor), names and sizes them through the non-virtual members below and drives
// them through the 22-slot interface; its destructor deletes them through slot 21.
//
// Layout (cTSRenderSurfaceImpl, the abstract base, vtable 0xdbb95c; constructor 0x36eb60):
//   +0 vptr  +4 requested width  +8 requested height  +0xc the device (cIDeviceDX, not counted)
//   +0x10 the colour surface  +0x14 the depth surface (the bound pair, not counted)  +0x18 created (byte)
//   +0x1c width  +0x20 height (the colour surface's, 0 without one)  +0x24 scale (1.0)  +0x28 has depth (byte)
//   +0x2c the cRenderTargetsState command the pass applies (0x1c bytes: the colour surface and the depth surface)
// The constructor leaves +0x19..+0x1b, +0x29..+0x2b and the state's byte +5 (+0x31) as they were.
// cTSRenderBackbufferSurface (0x70, vtable 0xdbba6c, constructor 0x36eb10): +0x48 the multisample count, +0x4c the
// name (cRZString), +0x6c a byte set while the device's multisample type changes (+0x6d..+0x6f not written).
// cTSRenderTargetSurface (0x74, vtable 0xdbb9b4, constructor 0x36ebf0): +0x48 the multisample count, +0x4c the name,
// +0x6c the render target (cIRenderTarget, counted), +0x70 the depth-stencil surface (counted).
// cTSRenderTextureSurface (0xc4, vtable 0xdbba10, constructor 0x36ec70; derives from the target surface): +0x74 alpha
// (byte) and +0x75 reparse-shaders (byte), neither written by the constructor; +0x78 the material name (cRZString),
// +0x98 an external render-target texture (not counted), +0x9c the texture name, +0xbc the texture's handle (the
// render-target texture the manager holds), +0xc0 the aspect (1.0).
//
// Standalone shape (docs/standalone-shape.md): real classes with our vtables. The constructors are hooked (the
// strategy's inline `new` then builds ours), and so are the non-virtual members the original strategy calls (Init,
// Configure, SetExternal, ReleaseSurfaces, ResetTexture) and Bind / Reset. The slots, the destructors and ReparseShaders
// are in effect through callers (only our vtables and members reach them). The shared slot bodies (folded with other
// classes) are inline here, uncounted. Slot names are ours (no symbols).
#include "gz/RenderStates.h"
#include "gz/cSurfaceDX.h"
#include "rz/cRZString.h"
#include "sg/cTextureManager.h"       // cTextureHandle

#include <cstddef>
#include <cstdint>


namespace nGZGraphic4 {
class cIDeviceDX;
class cISurface;
class cIRenderTarget;
class cIDepthStencilSurface;
} // namespace nGZGraphic4

namespace nGZSceneGraph {
struct Pass;
class cIRenderTargetTexture;
} // namespace nGZSceneGraph

namespace nTSSG {

using nGZGraphic4::cISurface;
using nGZSceneGraph::Pass;

class cITSRenderSurface {
public:
    virtual uint32_t GetWidth() = 0;                    // 0
    virtual uint32_t GetHeight() = 0;                   // 1
    virtual void SetScale(float scale) = 0;             // 2
    virtual float GetScale() = 0;                       // 3
    virtual float GetAspect() = 0;                      // 4
    virtual bool HasDepth() = 0;                        // 5
    virtual bool IsMultisampled() = 0;                  // 6
    virtual bool IsTexture() = 0;                       // 7
    virtual bool IsValid() = 0;                         // 8: the device objects exist
    virtual bool IsCreated() = 0;                       // 9: the created byte
    virtual void Create() = 0;                          // 10
    virtual void Destroy() = 0;                         // 11
    virtual void Apply(Pass *pass, bool force) = 0;     // 12: the targets command, unless the pass has them already
    virtual void Clear(Pass *pass, uint32_t flags, float z, uint32_t stencil) = 0; // 13
    virtual void ClearRect(Pass *pass, const int32_t *rect, uint32_t flags) = 0;    // 14
    virtual void CopyFrom(cITSRenderSurface *other) = 0;                            // 15: depth, then colour stretched
    virtual void CopyDepth(cITSRenderSurface *other) = 0;                           // 16
    virtual void CopyRect(cITSRenderSurface *other, const RECT *rect) = 0;       // 17
    virtual void CopyColor(cITSRenderSurface *other, bool stretch) = 0;             // 18
    virtual cISurface *GetColorSurface() = 0;                                       // 19
    virtual cISurface *GetDepthSurface() = 0;                                       // 20
    virtual ~cITSRenderSurface() = default;                                         // 21 (scalar deleting)
};

// The render-targets command at +0x2c: the state's constructor and destructor as the original's inline code has them.
struct cTargetsCommand : nGZGraphic4::cRenderTargetsState {
    cTargetsCommand();
    ~cTargetsCommand();
    cTargetsCommand(const cTargetsCommand &) = delete;
    cTargetsCommand &operator=(const cTargetsCommand &) = delete;
};
static_assert(sizeof(cTargetsCommand) == 0x1c);

// A cRZString member whose string bytes all start at zero, as the original's inline constructor leaves them.
struct cZeroedStringMember {
    union {
        cRZString value;
    };
    cZeroedStringMember();
    ~cZeroedStringMember() { value.Destruct(); }
    cZeroedStringMember(const cZeroedStringMember &) = delete;
    cZeroedStringMember &operator=(const cZeroedStringMember &) = delete;
    const char *Text() const { return value.ToChar(); }
    void Assign(const char *text);
};
static_assert(sizeof(cZeroedStringMember) == 0x20);

class cTSRenderSurfaceImpl : public cITSRenderSurface {
public:
    cTSRenderSurfaceImpl(); // 0x36eb60

    uint32_t GetWidth() override { return mWidth; }        // folded 0x147d00
    uint32_t GetHeight() override { return mHeight; }      // folded 0x15e550
    void SetScale(float scale) override { mScale = scale; } // folded 0x375650
    float GetScale() override { return mScale; }           // folded 0x153f90
    float GetAspect() override { return 1.0f; }            // folded 0x18c7a0
    bool HasDepth() override { return mHasDepth != 0; }    // folded 0x3730d0
    bool IsTexture() override { return false; }            // folded 0x147480
    bool IsCreated() override { return mCreated != 0; }    // folded 0x156110
    void Apply(Pass *pass, bool force) override;                                  // 0x3751f0
    void Clear(Pass *pass, uint32_t flags, float z, uint32_t stencil) override;   // 0x370f50
    void ClearRect(Pass *pass, const int32_t *rect, uint32_t flags) override;     // 0x370fe0
    void CopyFrom(cITSRenderSurface *other) override;                             // 0x371210
    void CopyDepth(cITSRenderSurface *other) override;                            // 0x371120
    void CopyRect(cITSRenderSurface *other, const RECT *rect) override;        // 0x371010
    void CopyColor(cITSRenderSurface *other, bool stretch) override;              // 0x3710a0
    cISurface *GetColorSurface() override { return mColor; } // folded 0x149cf0
    cISurface *GetDepthSurface() override { return mDepth; } // folded 0x149d00

    // 0x375a90 (ret 8): the bound pair: the sizes from the colour surface (0 without one) and the targets command (the
    // colour surface in target 0, the others none, the depth surface).
    void Bind(cISurface *color, cISurface *depth);

    uint32_t mRequestedWidth;  // +0x04
    uint32_t mRequestedHeight; // +0x08
    nGZGraphic4::cIDeviceDX *mDevice; // +0x0c
    cISurface *mColor;         // +0x10
    cISurface *mDepth;         // +0x14
    uint8_t mCreated;          // +0x18
    uint8_t mUnset19[3];       // +0x19 (not written)
    uint32_t mWidth;           // +0x1c
    uint32_t mHeight;          // +0x20
    float mScale;              // +0x24
    uint8_t mHasDepth;         // +0x28
    uint8_t mUnset29[3];       // +0x29 (not written)
    cTargetsCommand mTargets;  // +0x2c
};
static_assert(offsetof(cTSRenderSurfaceImpl, mCreated) == 0x18);
static_assert(offsetof(cTSRenderSurfaceImpl, mHasDepth) == 0x28);
static_assert(offsetof(cTSRenderSurfaceImpl, mTargets) == 0x2c);
static_assert(sizeof(cTSRenderSurfaceImpl) == 0x48);

class cTSRenderBackbufferSurface final : public cTSRenderSurfaceImpl {
public:
    cTSRenderBackbufferSurface() = default; // 0x36eb10
    ~cTSRenderBackbufferSurface() override = default; // 0x36f7f0 (slot 21: 0x36fbd0)

    bool IsMultisampled() override { return mSamples != 0; } // folded 0x3736b0
    bool IsValid() override { return mCreated != 0; }        // folded 0x156110
    void Create() override;                                  // 0x372700
    void Destroy() override;                                 // 0x375740

    // 0x3730e0 (ret 8): the name and whether there is a depth surface.
    void Init(const char *name, bool hasDepth);
    // 0x373390 (ret 8): the device and multisample count; the size is the device's back buffer's. A change of any of
    // them (device, count, size) makes the surface uncreated.
    void Configure(nGZGraphic4::cIDeviceDX *device, uint32_t samples);
    // 0x3736c0 (4 bytes, the original's: too short to hook): whether the multisample type is changing.
    bool InMultisampleChange() const { return mChangingMultisample != 0; }

    uint32_t mSamples = 0;              // +0x48
    cZeroedStringMember mName;          // +0x4c
    uint8_t mChangingMultisample = 0;   // +0x6c
    uint8_t mUnset6d[3];                // +0x6d (not written)
};
static_assert(offsetof(cTSRenderBackbufferSurface, mName) == 0x4c);
static_assert(offsetof(cTSRenderBackbufferSurface, mChangingMultisample) == 0x6c);
static_assert(sizeof(cTSRenderBackbufferSurface) == 0x70);

class cTSRenderTargetSurface : public cTSRenderSurfaceImpl {
public:
    cTSRenderTargetSurface() = default; // 0x36ebf0
    ~cTSRenderTargetSurface() override; // 0x36f870 (slot 21: 0x36fbf0)

    bool IsMultisampled() override { return mSamples != 0; } // folded 0x3736b0
    bool IsValid() override;                                 // 0x3736e0
    void Create() override;                                  // 0x372790
    void Destroy() override;                                 // 0x3757b0

    // 0x373100 (ret 8): the name and whether there is a depth surface; the first time, the render target and the
    // depth-stencil surface from the COM director.
    void Init(const char *name, bool hasDepth);
    // 0x3733f0 (ret 0x10): the device, the size and the multisample count; destroyed (slot 11) when created and any
    // differs or the surfaces are not valid (slot 8).
    void Configure(nGZGraphic4::cIDeviceDX *device, uint32_t width, uint32_t height, uint32_t samples);
    // 0x3759a0: the targets command emptied, the device objects released (the depth one only with depth), uncreated.
    void Reset();
    // 0x375950: Reset, then both surfaces released.
    void ReleaseSurfaces();

    uint32_t mSamples = 0;                              // +0x48
    cZeroedStringMember mName;                          // +0x4c
    nGZGraphic4::cIRenderTarget *mTarget = nullptr;     // +0x6c counted
    nGZGraphic4::cIDepthStencilSurface *mDepthStencil = nullptr; // +0x70 counted
};
static_assert(offsetof(cTSRenderTargetSurface, mTarget) == 0x6c);
static_assert(sizeof(cTSRenderTargetSurface) == 0x74);

class cTSRenderTextureSurface final : public cTSRenderTargetSurface {
public:
    cTSRenderTextureSurface() = default; // 0x36ec70
    ~cTSRenderTextureSurface() override = default; // 0x36f930 (slot 21: 0x36fc10)

    float GetAspect() override { return mAspect; }  // folded 0x372660
    bool IsMultisampled() override { return false; } // folded 0x147480
    bool IsTexture() override { return true; }       // folded 0x144910
    bool IsValid() override;                         // 0x373720
    void Create() override;                          // 0x372870
    void Destroy() override;                         // 0x3757d0

    // 0x373250 (ret 0x14): the target surface's Init (texture name, depth), the flags and names, then a 16 x 16
    // render-target texture made and added to the texture manager under the texture name; the handle takes it.
    void Init(const char *material, const char *texture, bool reparse, bool hasDepth, bool alpha);
    // 0x373450 (ret 0xc): the device and size; a zero width unbinds (no external texture, uncreated). Otherwise
    // destroyed (slot 11) when created and any differs or the surface is not valid (slot 8).
    void Configure(nGZGraphic4::cIDeviceDX *device, uint32_t width, uint32_t height);
    // 0x3734d0 (ret 4): an external render-target texture drawn into instead (not counted): the target surface's
    // Init ("ExternalTexture", whether it has depth), then its surfaces bound; reparse off.
    void SetExternal(nGZSceneGraph::cIRenderTargetTexture *texture);
    // 0x375990: the handle released.
    void ResetTexture() { mTexture.Reset(); }
    // 0x3713b0: with reparse on, the settings' "reparseShaders" set; otherwise the material reloaded, if named.
    void ReparseShaders();

    uint8_t mAlpha;                                    // +0x74 (not written by the constructor)
    uint8_t mReparse;                                  // +0x75 (not written by the constructor)
    uint8_t mUnset76[2];                               // +0x76 (not written)
    cZeroedStringMember mMaterial;                     // +0x78
    nGZSceneGraph::cIRenderTargetTexture *mExternal = nullptr; // +0x98
    cZeroedStringMember mTextureName;                  // +0x9c
    nGZSceneGraph::cTextureHandle mTexture;            // +0xbc
    float mAspect = 1.0f;                              // +0xc0
};
static_assert(offsetof(cTSRenderTextureSurface, mAlpha) == 0x74);
static_assert(offsetof(cTSRenderTextureSurface, mMaterial) == 0x78);
static_assert(offsetof(cTSRenderTextureSurface, mExternal) == 0x98);
static_assert(offsetof(cTSRenderTextureSurface, mTexture) == 0xbc);
static_assert(sizeof(cTSRenderTextureSurface) == 0xc4);

// The hooks' entry points (src/hook/tables/cTSRenderSurface.cpp): original calling convention, our bodies.
struct cTSRenderSurfaceAdapters {
    using Impl = cTSRenderSurfaceImpl;
    using Backbuffer = cTSRenderBackbufferSurface;
    using Target = cTSRenderTargetSurface;
    using Texture = cTSRenderTextureSurface;
    static Backbuffer *__fastcall ConstructBackbuffer(Backbuffer *memory, void *);
    static Target *__fastcall ConstructTarget(Target *memory, void *);
    static Texture *__fastcall ConstructTexture(Texture *memory, void *);
    static void __fastcall Bind(Impl *self, void *, cISurface *color, cISurface *depth);
    static void __fastcall Reset(Target *self, void *);
    static void __fastcall ReleaseSurfaces(Target *self, void *);
    static void __fastcall InitBackbuffer(Backbuffer *self, void *, const char *name, bool hasDepth);
    static void __fastcall InitTarget(Target *self, void *, const char *name, bool hasDepth);
    static void __fastcall InitTexture(Texture *self, void *, const char *material, const char *texture, bool reparse,
                                       bool hasDepth, bool alpha);
    static void __fastcall ConfigureBackbuffer(Backbuffer *self, void *, nGZGraphic4::cIDeviceDX *device, uint32_t samples);
    static void __fastcall ConfigureTarget(Target *self, void *, nGZGraphic4::cIDeviceDX *device, uint32_t width,
                                           uint32_t height, uint32_t samples);
    static void __fastcall ConfigureTexture(Texture *self, void *, nGZGraphic4::cIDeviceDX *device, uint32_t width,
                                            uint32_t height);
    static void __fastcall SetExternal(Texture *self, void *, nGZSceneGraph::cIRenderTargetTexture *texture);
    static void __fastcall ResetTexture(Texture *self, void *);
    // The target vtable's own slots, hooked rather than left to callers: the destructor 0x36f870 that re-stores the
    // vtable is also the target of an unwind funclet (0xd316f3, the hooked texture constructor's), which callers.py's
    // dead-code walk can't see past, so no hook would cut these slots' check (db/status/cTSRenderSurface.jsonl).
    static bool __fastcall IsValidTarget(Target *self, void *);
    static void __fastcall CreateTarget(Target *self, void *);
    static void __fastcall DestroyTarget(Target *self, void *);
    static Target *__fastcall DeleteTarget(Target *self, void *, unsigned flags);
};

} // namespace nTSSG
