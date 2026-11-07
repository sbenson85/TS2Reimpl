#pragma once
// nGZSceneGraph::cApplyRenderTarget (lane TM, scenegraph M3a): a shader pass part that points the device at a render
// target (the compositing target, one of the context's surfaces, or a render target texture's), sets the viewport, and
// optionally clears; EndState puts the context's surfaces and viewport back. Whole in the standalone shape: the
// material's shader makes it through the COM director (clsid 0xec181829, the factory 0xac31a0 hooked to make ours); the
// game then reaches its bodies only through our vtable, so the image's constructor, destructor, slot bodies and the two
// helpers only Apply calls (0xac1c40, 0xac22c0) are in effect through the factory hook
// (db/status/cApplyRenderTarget.jsonl).
#include "sg/SGShaderAttributes.h"
#include "sg/cTextureManager.h" // cTextureHandle

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// vtable 0xe76c00, 0x3c bytes, factory 0xac31a0.
class cApplyRenderTarget : public cShaderControllerT<cIApplyRenderTarget> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac31a0

    enum : uint32_t {
        kTargetPrimary = 0,  // the context's compositing target (one surface, mPrimarySurface)
        kTargetContext = 1,  // the context's surfaces: one by index, or all (index -1)
        kTargetTexture = 2,  // and above: the render target texture's (mTexture)
    };
    enum : uint8_t {
        kViewportContext = 0, // the context's viewport
        kViewportOuter = 1,   // the outer context's (the context's own when it has none)
        kViewportUnit = 2,    // {0, 0, 1.0f, 1.0f} (as bits)
        // above: no viewport command
    };
    static constexpr uint32_t kAllSurfaces = 0xffffffffu;

    cApplyRenderTarget() = default; // 0xabd810
    ~cApplyRenderTarget() override; // 0xabe230 (deleting 0xabe950, slot 20)

    // 0xac1c40 (ret 4): the render targets command for the target kind (none for an out-of-range context surface).
    void SendTargets(const cShaderApplyContext *ctx) const;
    // 0xac22c0 (ret 4): the viewport command for the viewport mode.
    void SendViewport(const cShaderApplyContext *ctx) const;

    // 3 (0xac6720): a valid texture's device resource released (its slot 4).
    void Reset() override;
    // 8 (0xac3f20): the device's render target count (caps slot 21 with 1) kept; the target checked against it (a
    // report line per failure when `report`'s low byte is set), then the template's hash. Init's first argument is
    // what the shader passes in (tentative: the device's caps).
    bool Init(void *caps, void *key, void *table, void *report) override;
    uint32_t Slot10() override; // 10 (0xac2b00): 3 with mRestore, else 1
    void Apply(void *ctx, void *item) override; // 11 (0xabf490): the targets, the viewport, then the clear
    void EndState(void *ctx) override;          // 13 (0xac6740): the context's surfaces and viewport

    void SetTexture(const char *name, uint32_t index) override; // 14 (0xac6440)
    void SetTarget(uint32_t kind, uint32_t index) override;     // 15 (0xac6420)
    void SetRestore(bool on) override { mRestore = on; }        // 16 (0x411b90, folded with unrelated code)
    void SetViewportMode(uint8_t mode) override { mViewportMode = mode; } // 17 (folded shared_vf 0x1b9690)
    void SetClearColor(bool on, const float *color) override;  // 18 (0xac61f0)
    void SetClearDepth(bool on, float z, uint32_t stencil) override; // 19 (0xac6210)
    uint32_t HashParams() override;                             // 21 (0xac3540): JenkinsHash of +0x10..+0x37
    bool EqualParams(cIApplyRenderTarget *other) override;      // 22 (0xac5350): the same 10 words

    cTextureHandle mTexture;           // +0x10
    uint32_t mKind = kTargetTexture;   // +0x14
    uint8_t mViewportMode = kViewportUnit + 1; // +0x18: none (3)
    bool mClearColor = false;          // +0x19
    bool mClearDepth = false;          // +0x1a: depth and stencil
    bool mRestore = false;             // +0x1b
    uint32_t mIndex = 0;               // +0x1c: the surface (kAllSurfaces for all of the context's)
    uint32_t mStencil = 0;             // +0x20
    float mZ = 0.0f;                   // +0x24
    // +0x28: the clear colour. The constructor copies it from the image's 0x10f67b4, a 16-byte global nothing else
    // references (zero in the image's bss, so zero here).
    float mColor[4] = {};
    uint32_t mMaxTargets = 0;          // +0x38: the device's render target count (Init)
};
static_assert(sizeof(cApplyRenderTarget) == 0x3c);
static_assert(offsetof(cApplyRenderTarget, mTexture) == 0x10);
static_assert(offsetof(cApplyRenderTarget, mViewportMode) == 0x18);
static_assert(offsetof(cApplyRenderTarget, mRestore) == 0x1b);
static_assert(offsetof(cApplyRenderTarget, mIndex) == 0x1c);
static_assert(offsetof(cApplyRenderTarget, mZ) == 0x24);
static_assert(offsetof(cApplyRenderTarget, mColor) == 0x28);
static_assert(offsetof(cApplyRenderTarget, mMaxTargets) == 0x38);

} // namespace nGZSceneGraph
