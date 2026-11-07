#pragma once
// cTSParticlesGeometry (lane WQ, rendering row E3): the dynamic geometry a particles effect draws itself with, one
// camera-facing quad per live particle in the display list's dynamic arrays (RTTI 0x10c1940, vtable 0xe8c8fc, 10 slots:
// cIGeometryRender; 0x78 bytes). The game makes it at one site, cTSParticlesRenderable::Setup 0xbbad30 (ours). Layout: src/sg/cDynamicGeometryRect.h's first 0x6c bytes (the folded slots read them), then
// the vertex format, its declaration and the effect (not counted).
//
// Standalone shape: the constructor and the destructor are hooked, so every object has our vtable. Slots 3, 8 and 9 and
// SetBounds run through their callers, their originals trapped (db/status/cTSParticlesGeometry.jsonl). Slots 0, 1, 2, 4, 5,
// 6 and 7 are folded bodies.
//
// The effect and its particles have no class yet (rows E4..E6): the views below hold what this code reads (offsets from the
// original's reads, names tentative).
#include "gz/cVertexFormat.h"
#include "sg/cIGeometryRender.h"
#include "ts/EffectsTransform.h"

#include <cstddef>
#include <cstdint>
#include <string>

class cTSParticlesDescription;
class cTSEffectCurve;
class cTSParticlesEffect;

class cTSParticlesGeometry final : public nGZSceneGraph::cIGeometryRender {
public:
    using cGeometryRenderContext = nGZSceneGraph::cGeometryRenderContext;

    explicit cTSParticlesGeometry(cTSParticlesEffect *effect); // 0xbb4e90
    ~cTSParticlesGeometry() override;                              // 0xbb5490 (slot 9: deleting 0xbb5890)

    bool QueryInterface(uint32_t iid, void **out) override; // 0: 1 and 0x89d39f12 answer this, counted (folded 0x3af660)
    uint32_t AddRef() override { return ++mRefs; }          // 1 (folded 0x148fb0)
    uint32_t Release() override;                            // 2 (folded 0x153f50)
    bool IsSupported(uint32_t what) override;               // 3: 0xbb9460, the declaration exists
    uint32_t *GetFlags() override { return &mFlags; }       // 4 (folded 0x142840)
    uint32_t *GetDrawInfo(uint32_t) override { return mDrawInfo; } // 5 (folded 0xa441a0)
    uint32_t Prepare(uint32_t, uint32_t, uint32_t, uint32_t) override { return 0; } // 6 (folded 0x18a5b0)
    void Begin(uint32_t, cGeometryRenderContext *, uint32_t) override {}            // 7 (folded 0x377e0)
    uint32_t Render(uint32_t unused0, cGeometryRenderContext *context, uint8_t flag, uint32_t unused3) override; // 8: 0xbbdd50

    // 0xbc10f0: both copies of the bounds (the renderable, when it adds itself to the draw list).
    void SetBounds(const float *bounds);

    uint32_t mRefs;                          // +4 (0)
    uint32_t mFlags;                         // +8 (1)
    uint32_t mZeros[4];                      // +0xc
    float mBoundsA[6];                       // +0x1c (0)
    uint32_t mDrawInfo[2];                   // +0x34 (0)
    uint32_t mUnset3c;                       // +0x3c (not written by the constructor)
    nGZGraphic4::cIVertexFormat *mFormatRaw; // +0x40
    uint32_t mZeros2[3];                     // +0x44
    float mBoundsB[6];                       // +0x50
    uint8_t mByte68;                         // +0x68 (1)
    uint8_t mUnset69[3];                     // +0x69 (not written)
    nGZGraphic4::cIVertexFormat *mFormat;    // +0x6c counted
    nGZGraphic4::cIVertexDeclarationDX *mDeclaration; // +0x70 counted
    cTSParticlesEffect *mEffect;         // +0x74 not counted
};
static_assert(sizeof(cTSParticlesGeometry) == 0x78, "cTSParticlesGeometry size");
static_assert(offsetof(cTSParticlesGeometry, mDrawInfo) == 0x34 && offsetof(cTSParticlesGeometry, mFormatRaw) == 0x40 &&
                  offsetof(cTSParticlesGeometry, mBoundsB) == 0x50 && offsetof(cTSParticlesGeometry, mFormat) == 0x6c &&
                  offsetof(cTSParticlesGeometry, mEffect) == 0x74,
              "cTSParticlesGeometry layout");

// The hooks' entry points (src/hook/tables/cTSParticlesGeometry.cpp): original calling convention, our bodies.
struct cTSParticlesGeometryAdapters {
    using Self = cTSParticlesGeometry;
    static Self *__fastcall Construct(Self *memory, void *, cTSParticlesEffect *effect);
    static void __fastcall Destruct(Self *self, void *);
    // 0xbb5620 and 0xbc0f80, this code's two leaves (members of other classes).
    static float __fastcall EvaluateCurve(const cTSEffectCurve *curve, void *, float t);
    static void __fastcall Rotate(const fx::EffectsTransform *transform, void *, fx::Vector3 *vector);
};
