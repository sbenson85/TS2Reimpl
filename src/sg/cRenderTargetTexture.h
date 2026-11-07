#pragma once
// nGZSceneGraph::cRenderTargetTexture (lane TN, milestone O row M4b; docs/subsystems/scenegraph.md): the scene graph's
// texture the renderer draws into: up to four colour targets (device textures the director's COM makes, class
// 0xbb892198, face 0x5b891e1e) and a depth-stencil surface (class 0x7b75b73a, face 0xfb75b72b), made once the device
// exists (the shadow maps, the reflections, the post-processing buffers).
//
// What a texture is asked for is set up by slots 9-11 (a fixed size, a size of the display's, or a square colour target
// with a depth surface), which release the resources and call Restore (slot 3) to make them.
//
// Layout (0x58 bytes, the game's): +0 vtable, +4 mRefs, +8 mWidth, +0xc mHeight, +0x10 mFormatBits, +0x14 mAlphaBits
// (the colour format's minimum bits, tentative), +0x18 mDepthBits (0 or 0x18), +0x1c mStencilBits, +0x20 / +0x24 the
// scale of the display's size (flag 0x80000000), +0x28 mFlags (1 depth surface, 2 keep the aspect, 4 floating point
// target, the sign bit: the size is the display's), +0x2c mCount (colour targets), +0x30 mAspect (1.0 at first), +0x34
// mClear (four floats), +0x44 four colour targets (counted), +0x54 the depth surface (counted).
//
// Standalone shape (docs/standalone-shape.md): our class deriving from its interfaces, every slot an override. The
// factory the director's COM registers (0xab4df0, class 0x2b96cc2b; src/rz/COMDirectorData.cpp) is hooked and makes our
// object, so the constructor 0xab42b0, the destructor 0xab4560 and every slot but five (the game's vtable 0xe766fc is
// referenced only by them) are in effect through the factory. Restore 0xab5240 (slot 3), slot 12 0xab5b10 and the clear
// helper 0xab4b80 slots 13-15 call are ours too (lane VT), so the whole class is; what they call that is not ours yet
// is the device's render-target size 0x18aea0 and the state vtable 0xd9ff54 (milestone O row O-c). Slots 1, 2, 7 are
// bodies the image folds with other classes (0x148fb0, 0x2c4080 with cLightRigManager, 0x147770): reproduced here,
// never hooked.
#include "sg/cTextureImage.h" // cITexture

#include <bit>
#include <cstddef>
#include <cstdint>

#include "game/owned.h"

TS2_OWNED_STATIC(uint32_t, 10f675c)[4]; // the clear colour a new target starts with

namespace nGZGraphic4 {
struct cRenderTargetsState;
class cIDepthStencilSurface;
class cIImageSurface;
class cITexture;
} // namespace nGZGraphic4

namespace nGZSceneGraph {

// nGZSceneGraph::cIRenderTargetTexture (RTTI base of cRenderTargetTexture): how the target is set up and drawn into.
class cIRenderTargetTexture : public cITexture {
public:
    static constexpr uint32_t kIid = 0x0b96cc22;
    // 9: a target of width x height with `count` colour targets of the format bits a / b, `depthStencil` stencil bits,
    // cleared to `clear` (four floats), `flags`; released and made again.
    virtual void Create(int32_t width, int32_t height, int32_t formatBits, int32_t alphaBits, int32_t stencilBits,
                        uint32_t count, const float *clear, uint32_t flags) = 0;
    // 10: slot 9 with one 0x18-bit-depth target of flags 5 (a depth surface and a floating point colour target) and the
    // default clear colour.
    virtual void CreateSimple(int32_t width, int32_t height, int32_t formatBits) = 0;
    // 11: a target of the display's size scaled by scaleX / scaleY (flag 0x80000000 set in `flags`).
    virtual void CreateRelative(float scaleX, float scaleY, int32_t formatBits, int32_t alphaBits, int32_t stencilBits,
                                uint32_t count, const float *clear, uint32_t flags) = 0;
    // 12: the targets (all, or `index`) and the depth surface handed to `device`'s slot 12 as a cRenderTargetsState.
    virtual void SetAsTargets(void *device, int32_t index) = 0;
    virtual void Clear(const int32_t *rect, const float *colour, float depth, uint32_t stencil) = 0; // 13: all
    virtual void ClearColour(const int32_t *rect, const float *colour) = 0;                          // 14: colour, depth 1
    virtual void ClearOwn(const int32_t *rect, float depth, uint32_t stencil) = 0;                    // 15: own colour
    virtual float GetAspect() = 0;                                                                   // 16
    virtual nGZGraphic4::cIImageSurface *GetTargetSurface(uint32_t index) = 0;                       // 17
    virtual nGZGraphic4::cIDepthStencilSurface *GetDepthSurface() = 0;                               // 18
    virtual ~cIRenderTargetTexture() = default;                                                       // 19
};

class cRenderTargetTexture final : public cIRenderTargetTexture {
public:
    static constexpr uint32_t kSize = 0x58;
    static constexpr uint32_t kIidTexture = 0x4a95f211; // the cITexture id QueryInterface also answers
    static constexpr uint32_t kFlagDepth = 1, kFlagAspect = 2, kFlagFloat = 4, kFlagRelative = 0x80000000;

    static cRenderTargetTexture *__cdecl New(); // 0xab4df0: the director's factory (cdecl, no arguments; hooked)

    cRenderTargetTexture();  // 0xab42b0
    ~cRenderTargetTexture() override; // 0xab4560; the deleting destructor 0xab4660 (slot 19)
    cRenderTargetTexture(const cRenderTargetTexture &) = delete;
    cRenderTargetTexture &operator=(const cRenderTargetTexture &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0xab5a70
    uint32_t AddRef() override { return ++mRefs; }           // 1: 0x148fb0 (shared)
    uint32_t Release() override;                             // 2: shared_vf_2c4080
    void Restore() override;                                 // 3: 0xab5240
    void ReleaseResource() override;                         // 4: 0xab5dd0
    uint32_t GetKind() override { return 7; }                // 5: 0xab4b40
    bool IsCreated() override;                               // 6: 0xab46b0
    uint32_t GetDeviceTextureCount() override { return mCount; } // 7: 0x147770 (shared: returns +0x2c, the count)
    nGZGraphic4::cITextureBase *GetDeviceTexture(uint32_t index) override; // 8: 0xab4b60
    void Create(int32_t width, int32_t height, int32_t formatBits, int32_t alphaBits, int32_t stencilBits,
                uint32_t count, const float *clear, uint32_t flags) override; // 9: 0xab59b0
    void CreateSimple(int32_t width, int32_t height, int32_t formatBits) override; // 10: 0xab5940
    void CreateRelative(float scaleX, float scaleY, int32_t formatBits, int32_t alphaBits, int32_t stencilBits,
                        uint32_t count, const float *clear, uint32_t flags) override; // 11: 0xab58b0
    void SetAsTargets(void *device, int32_t index) override;                    // 12: 0xab5b10
    void Clear(const int32_t *rect, const float *colour, float depth, uint32_t stencil) override;    // 13: 0xab4700
    void ClearColour(const int32_t *rect, const float *colour) override;                              // 14: 0xab4730
    void ClearOwn(const int32_t *rect, float depth, uint32_t stencil) override;                       // 15: 0xab4750
    float GetAspect() override { return mAspect; }                                                    // 16: 0xab5a30
    nGZGraphic4::cIImageSurface *GetTargetSurface(uint32_t index) override;                           // 17: 0xab5ad0
    nGZGraphic4::cIDepthStencilSurface *GetDepthSurface() override;                                   // 18: 0xab4a90

    uint32_t mRefs = 0;                              // +4
    int32_t mWidth = 0;                              // +8
    int32_t mHeight = 0;                             // +0xc
    int32_t mFormatBits = 0;                         // +0x10
    int32_t mAlphaBits = 0;                          // +0x14
    int32_t mDepthBits = 0;                          // +0x18
    int32_t mStencilBits = 0;                        // +0x1c
    float mScaleX = 0;                               // +0x20
    float mScaleY = 0;                               // +0x24
    uint32_t mFlags = 0;                             // +0x28
    uint32_t mCount = 0;                             // +0x2c
    float mAspect = 1.0f;                            // +0x30
    float mClear[4];                                 // +0x34
    nGZGraphic4::cITexture *mTargets[4] = {};        // +0x44 (counted)
    nGZGraphic4::cIDepthStencilSurface *mDepth = nullptr; // +0x54 (counted)

private:
    void ClearWith(uint32_t flags, const int32_t *rect, const float *colour, float depth, uint32_t stencil); // 0xab4b80
    // The render-targets state command slot 12 and the clear helper build (inline in both originals).
    nGZGraphic4::cRenderTargetsState TargetsState(int32_t index);
};

static_assert(sizeof(cRenderTargetTexture) == cRenderTargetTexture::kSize, "cRenderTargetTexture: 0x58 bytes");
static_assert(offsetof(cRenderTargetTexture, mRefs) == 4 && offsetof(cRenderTargetTexture, mWidth) == 8 &&
                  offsetof(cRenderTargetTexture, mDepthBits) == 0x18 && offsetof(cRenderTargetTexture, mScaleX) == 0x20 &&
                  offsetof(cRenderTargetTexture, mFlags) == 0x28 && offsetof(cRenderTargetTexture, mCount) == 0x2c &&
                  offsetof(cRenderTargetTexture, mAspect) == 0x30 && offsetof(cRenderTargetTexture, mClear) == 0x34 &&
                  offsetof(cRenderTargetTexture, mTargets) == 0x44 && offsetof(cRenderTargetTexture, mDepth) == 0x54,
              "cRenderTargetTexture layout");

} // namespace nGZSceneGraph
