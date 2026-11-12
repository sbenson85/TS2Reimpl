#pragma once
// cGZDrawContext: the 2D drawing context the windows draw through (docs/subsystems/rendering.md, R5). It keeps the
// drawing state (an origin, a clip, four colours, a line width and three state bytes, a gradient, a texture and its
// clip) with stacks for the transform and the state, and hands primitives to an engine (src/gz/cIGZDrawContextEngine.h)
// made by the draw system for its target buffer. State changes are only marked; the next primitive sends what changed
// (FlushState).
//
// 0xdc bytes, one face (vftable 0xe4ed70, 49 slots). Created by the constructor 0x8baff0 (called by cGZDrawSystem slot 5)
// and by the COM director's creator 0x8bf7d0 (constructor inline). Standalone shape (docs/standalone-shape.md): both are
// hooked (cGZDrawContextAdapters::Construct, cGZDrawContext::Create), so no object carries the game's vtable and every
// slot, the destructor (0x8bb120, reached only from Release) and the members (FlushState 0x8bbef0, DrawKind6 0x8befb0 and
// the primitive builders behind Dispatch, src/gz/cGZDrawContextBuilders.cpp) are in effect through their callers. Dispatch
// 0x8bc040 is hooked at its RVA (cGZDrawContextAdapters::Dispatch): the builders call it back (kinds 0, 2, 4, 5), a cycle
// the in-effect check needs cut. The rectangle intersection 0x373520 (shared by many classes) is ours since lane
// 261108s (src/gz/cGZRect.h).
#include <cstdint>
#include <vector>

#include "gz/cIGZBuffer.h"
#include "gz/cIGZDrawContextEngine.h"
#include "gz/cIGZUnknown.h"

struct cGZPoint {
    int32_t x;
    int32_t y;
};

// cIGZDrawContext (49 slots). Rectangles and points are ints in the context's coordinates (the origin added) unless
// the slot says "absolute"; the primitive slots take float (x, y) pairs.
class cIGZDrawContext : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x2ae63219;

    virtual void PushTransform() = 0;                                   // slot 3: origin and clip
    virtual void ResetClip() = 0;                                       // slot 4: the clip back to the bounds
    virtual void IntersectClip(const cGZRect *rect) = 0;                // slot 5
    virtual void IntersectClipAbsolute(const cGZRect *rect) = 0;        // slot 6
    virtual void Translate(const cGZPoint *offset) = 0;                 // slot 7
    virtual void SetOrigin(const cGZPoint *origin) = 0;                 // slot 8
    virtual void PopTransform() = 0;                                    // slot 9
    virtual void PushState() = 0;                                       // slot 10
    virtual void PopState() = 0;                                        // slot 11
    // Slots 12-14 return their argument (eax): cGZDrawFontRenderer::BeginDraw reads GetOrigin's answer through it.
    virtual cGZRect *GetBounds(cGZRect *bounds) = 0;                    // slot 12
    virtual cGZRect *GetClip(cGZRect *clip) = 0;                        // slot 13
    virtual cGZPoint *GetOrigin(cGZPoint *origin) = 0;                  // slot 14
    virtual int32_t GetTransformDepth() = 0;                            // slot 15
    virtual bool HasTarget() = 0;                                       // slot 16
    virtual void SetTarget(cIGZBuffer *target) = 0;                     // slot 17
    virtual cIGZDrawContextEngine *GetEngine() = 0;                     // slot 18
    virtual void ResetState() = 0;                                      // slot 19
    virtual void SetColorAt(uint32_t index, uint32_t argb) = 0;         // slot 20
    virtual void SetColor(uint32_t argb) = 0;                           // slot 21
    virtual void vf22(uint32_t value) = 0;                              // slot 22: the state word
    virtual void vf23(uint8_t value) = 0;                               // slot 23: state byte 2
    virtual void vf24(uint8_t value) = 0;                               // slot 24: state byte 1
    virtual void vf25(uint8_t value) = 0;                               // slot 25: state byte 0
    virtual void SetLineWidth(float width) = 0;                         // slot 26
    virtual void ClearGradient() = 0;                                   // slot 27
    virtual void SetGradient(const float *coefficients, float x, float y) = 0; // slot 28
    virtual void ClearTexture() = 0;                                    // slot 29
    virtual void SetTexture(cIGZUnknown *texture, float x, float y, float sx, float sy) = 0; // slot 30
    virtual void ClearTextureClip() = 0;                                // slot 31
    virtual void SetTextureClip(const cGZRect *clip) = 0;               // slot 32
    virtual void FillClip() = 0;                                        // slot 33
    virtual void DrawPoint(int32_t x, int32_t y) = 0;                   // slot 34
    virtual void DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1) = 0; // slot 35
    virtual void FillRect(const cGZRect *rect) = 0;                     // slot 36
    virtual void vf37(int32_t left, int32_t top, int32_t right, int32_t bottom) = 0; // slot 37: kind 5
    virtual void vf38(int32_t left, int32_t top, int32_t right, int32_t bottom) = 0; // slot 38: kind 5 too
    // slot 39: the texture's `source` rectangle stretched over `target`
    virtual void DrawTextureStretched(cIGZUnknown *texture, const cGZRect *source, const cGZRect *target) = 0;
    // slot 40: `target` filled with the texture drawn at (x, y) from target's corner, clipped to `clip`
    virtual void DrawTextureClipped(cIGZUnknown *texture, const cGZRect *clip, const cGZRect *target, int32_t x,
                                    int32_t y) = 0;
    virtual void vf41() = 0;                                            // slot 41: engine slot 17
    virtual bool InBatch() = 0;                                         // slot 42
    virtual void BeginBatch() = 0;                                      // slot 43
    virtual void EndBatch() = 0;                                        // slot 44
    virtual void DrawPrimitives(uint32_t kind, const float *points, int32_t count) = 0;         // slot 45
    virtual void DrawPrimitivesAbsolute(uint32_t kind, const float *points, int32_t count) = 0; // slot 46
    virtual bool LockSurface(cGZDrawSurface *surface, uint32_t readOnly) = 0; // slot 47: the engine's slot 20
    virtual void UnlockSurface() = 0;                                   // slot 48: the engine's slot 21
};

class cGZDrawContext : public cIGZDrawContext {
public:
    static constexpr uint32_t kSize = 0xdc;

    // The state PushState saves and the flush compares with what the engine was last sent (16 bytes, copied whole).
    struct State {
        uint8_t byte0;   // +0 slot 25, engine slot 11
        uint8_t byte1;   // +1 slot 24, engine slot 10
        uint8_t byte2;   // +2 slot 23, engine slot 9
        uint8_t pad;     // +3 never written (copied along)
        uint32_t word;   // +4 slot 22, engine slot 8
        float lineWidth; // +8 slot 26
        int32_t lineWidthRounded; // +0xc 1 at construction; in the sent copy, the width rounded (at least 1)
    };
    // What PushTransform saves (24 bytes).
    struct Transform {
        float originX, originY;
        cGZRect clip;
    };

    cGZDrawContext();
    // 0x8bf7d0 (cdecl): the COM director's creator, `new cGZDrawContext` (no reference taken).
    static cGZDrawContext *Create();

    bool QueryInterface(uint32_t riid, void **ppvObj) override; // slot 0 (0x8bb370)
    uint32_t AddRef() override;                                 // slot 1 (0x8bb330)
    uint32_t Release() override;                                // slot 2 (0x8bb340)
    void PushTransform() override;                              // slot 3 (0x8bb3a0)
    void ResetClip() override;                                  // slot 4 (0x8bb3e0)
    void IntersectClip(const cGZRect *rect) override;           // slot 5 (0x8bb410)
    void IntersectClipAbsolute(const cGZRect *rect) override;   // slot 6 (0x8bb460)
    void Translate(const cGZPoint *offset) override;            // slot 7 (0x8bb480)
    void SetOrigin(const cGZPoint *origin) override;            // slot 8 (0x8bb4b0)
    void PopTransform() override;                               // slot 9 (0x8bb4d0)
    void PushState() override;                                  // slot 10 (0x8bb510)
    void PopState() override;                                   // slot 11 (0x8bb540)
    cGZRect *GetBounds(cGZRect *bounds) override;               // slot 12 (0x8bb5a0)
    cGZRect *GetClip(cGZRect *clip) override;                   // slot 13 (0x8bb5b0)
    cGZPoint *GetOrigin(cGZPoint *origin) override;             // slot 14 (0x8bb5c0)
    int32_t GetTransformDepth() override;                       // slot 15 (0x8bb5e0)
    bool HasTarget() override;                                  // slot 16 (0x8bb600)
    void SetTarget(cIGZBuffer *target) override;                // slot 17 (0x8bb610)
    cIGZDrawContextEngine *GetEngine() override;                // slot 18 (0x8bb7d0)
    void ResetState() override;                                 // slot 19 (0x8bb7e0)
    void SetColorAt(uint32_t index, uint32_t argb) override;    // slot 20 (0x8bb840)
    void SetColor(uint32_t argb) override;                      // slot 21 (0x8bb830)
    void vf22(uint32_t value) override;                         // slot 22 (0x8bb870)
    void vf23(uint8_t value) override;                          // slot 23 (0x8bb880)
    void vf24(uint8_t value) override;                          // slot 24 (0x8bb890)
    void vf25(uint8_t value) override;                          // slot 25 (0x8bb8a0)
    void SetLineWidth(float width) override;                    // slot 26 (0x8bb8b0)
    void ClearGradient() override;                              // slot 27 (0x8bb8d0)
    void SetGradient(const float *coefficients, float x, float y) override; // slot 28 (0x8bb8f0)
    void ClearTexture() override;                               // slot 29 (0x8bb920)
    void SetTexture(cIGZUnknown *texture, float x, float y, float sx, float sy) override; // slot 30 (0x8bb970)
    void ClearTextureClip() override;                           // slot 31 (0x8bb9e0)
    void SetTextureClip(const cGZRect *clip) override;          // slot 32 (0x8bba10)
    void FillClip() override;                                   // slot 33 (0x8bbac0)
    void DrawPoint(int32_t x, int32_t y) override;              // slot 34 (0x8bbaf0)
    void DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1) override; // slot 35 (0x8bbb40)
    void FillRect(const cGZRect *rect) override;                // slot 36 (0x8bbbb0)
    void vf37(int32_t left, int32_t top, int32_t right, int32_t bottom) override; // slot 37 (0x8bbbe0)
    void vf38(int32_t left, int32_t top, int32_t right, int32_t bottom) override; // slot 38 (0x8bbc40)
    void DrawTextureStretched(cIGZUnknown *texture, const cGZRect *source, const cGZRect *target) override; // 39 (0x8bbca0)
    void DrawTextureClipped(cIGZUnknown *texture, const cGZRect *clip, const cGZRect *target, int32_t x,
                            int32_t y) override;            // slot 40 (0x8bbd90)
    void vf41() override;                                   // slot 41 (0x8bba60)
    bool InBatch() override;                                // slot 42 (0x8bba70)
    void BeginBatch() override;                             // slot 43 (0x8bba80)
    void EndBatch() override;                               // slot 44 (0x8bbaa0)
    void DrawPrimitives(uint32_t kind, const float *points, int32_t count) override;         // slot 45 (0x8bbe30)
    void DrawPrimitivesAbsolute(uint32_t kind, const float *points, int32_t count) override; // slot 46 (0x8bbeb0)
    bool LockSurface(cGZDrawSurface *surface, uint32_t readOnly) override; // slot 47 (0x8bbed0)
    void UnlockSurface() override;                          // slot 48 (0x8bbee0)

    // 0x8bbef0: sends the engine what changed of the state (when +0x64 is set) and the clip (when +0x65 is set).
    void FlushState();
    // 0x8bc040 (ret 0xc): primitives of `kind` to the engine, prepared as its route for them asks.
    void Dispatch(uint32_t kind, const float *points, int32_t count);
    // 0x8befb0 (ret 8): kind 6.
    void DrawKind6(const float *points, int32_t count);

    // The primitive builders Dispatch picks by the engine's route (src/gz/cGZDrawContextBuilders.cpp). Each takes the
    // primitives' float (x, y) pairs and their point count, and either hands the engine spans (DrawSpans: word pairs of
    // row << 16 | x) or turns them into a simpler kind and goes through Dispatch / DrawQuadList again.
    void BuildPoints(const float *points, int32_t count);                  // 0x8bc290 kind 0
    void BuildLines(const float *points, int32_t count);                   // 0x8bc5f0 kind 1
    void AppendLineQuad(const float *line);                                // 0x8bc8f0 one segment as a quad of the width
    void BuildLineSpans(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1); // 0x8bccd0 a line in 1/256 pixels
    void BuildCurveQuads(const float *points, int32_t count);              // 0x8bd430 kind 2, route 1
    void BuildCurveSpans(const float *points, int32_t count);              // 0x8bd660 kind 2, route 2
    void BuildEllipseOutlineQuads(const float *points, int32_t count);     // 0x8bdc50 kind 4, route 1
    void BuildEllipseOutlineCurves(const float *points, int32_t count);    // 0x8be3d0 kind 4, route 2
    void BuildRectSpans(const float *points, int32_t count);               // 0x8be790 kind 3, route 2
    void BuildEllipsePolygons(const float *points, int32_t count);         // 0x8bea10 kind 5, route 1
    void BuildEllipseSpans(const float *points, int32_t count);            // 0x8bec30 kind 5, route 2
    void BuildTriangleSpans(const float *points, int32_t count);           // 0x8beff0 kind 6, route 2
    void DrawQuadList(const float *points, int32_t count);                 // 0x8bf130 kind 7
    void BuildQuadSpans(const float *points, int32_t count);               // 0x8bf250 kind 7, route 2
    void BuildPolygon(const float *points, int32_t count);                 // 0x8bf480 kind 8
    // Swaps the quad vectors (+0xb4 and +0xc0): what was built becomes the list to draw.
    void SwapQuads() { mQuads.swap(mQuadsOut); }

    // A counted reference released when the context goes (the target, the engine).
    template <class T> struct Held {
        T *p = nullptr;
        Held() = default;
        Held(const Held &) = delete;
        Held &operator=(const Held &) = delete;
        ~Held()
        {
            if (p) p->Release();
        }
    };

    uint32_t mColors[4];                   // +0x04
    Held<cIGZBuffer> mTarget;              // +0x14
    Held<cIGZDrawContextEngine> mEngine;   // +0x18
    float mOriginX;                        // +0x1c (not set by the constructor)
    float mOriginY;                        // +0x20
    cGZRect mClip;                         // +0x24
    cGZRect mSentClip;                     // +0x34 the clip the engine was last sent
    State mState;                          // +0x44
    State mSent;                           // +0x54 what the engine was last sent
    bool mStateDirty;                      // +0x64
    bool mClipDirty;                       // +0x65 (not set by the constructor)
    uint8_t mPad66[2];                     // +0x66
    cGZRect mBounds;                       // +0x68 the target's, from the engine
    cGZRect mTextureArea;                  // +0x78 what the texture covers (engine slot 14)
    bool mTextured;                        // +0x88
    bool mTextureClipped;                  // +0x89
    uint8_t mPad8a[2];                     // +0x8a
    int32_t mBatchDepth;                   // +0x8c
    std::vector<Transform> mTransforms;    // +0x90
    std::vector<State> mStates;            // +0x9c
    std::vector<float> mPoints;            // +0xa8 DrawPrimitives' points with the origin added
    std::vector<float> mQuads;             // +0xb4 Dispatch's rectangles as quads (swapped with +0xc0)
    std::vector<float> mQuadsOut;          // +0xc0
    std::vector<int32_t> mSpans;           // +0xcc the span builder's (0x8be790)
    uint32_t mRefCount;                    // +0xd8

private:
    ~cGZDrawContext(); // 0x8bb120 (inline in Release): the engine's Detach, then the members, the engine and target last
};

// The constructor as cGZDrawSystem slot 5 calls it, and Dispatch (hooked at its RVA: the builders call it back, so it cuts
// their cycle for the in-effect check).
struct cGZDrawContextAdapters {
    static cGZDrawContext *__fastcall Construct(void *memory, void *); // 0x8baff0
    static void __fastcall Dispatch(cGZDrawContext *self, void *, uint32_t kind, const float *points,
                                    int32_t count); // 0x8bc040
};

namespace drawcontext_layout {
static_assert(sizeof(cGZDrawContext) == cGZDrawContext::kSize, "cGZDrawContext size");
static_assert(sizeof(cGZDrawContext::State) == 16 && sizeof(cGZDrawContext::Transform) == 24, "entries");
static_assert(offsetof(cGZDrawContext, mColors) == 0x04 && offsetof(cGZDrawContext, mTarget) == 0x14, "layout");
static_assert(offsetof(cGZDrawContext, mEngine) == 0x18 && offsetof(cGZDrawContext, mOriginX) == 0x1c, "layout");
static_assert(offsetof(cGZDrawContext, mClip) == 0x24 && offsetof(cGZDrawContext, mSentClip) == 0x34, "layout");
static_assert(offsetof(cGZDrawContext, mState) == 0x44 && offsetof(cGZDrawContext, mSent) == 0x54, "layout");
static_assert(offsetof(cGZDrawContext, mStateDirty) == 0x64 && offsetof(cGZDrawContext, mBounds) == 0x68, "layout");
static_assert(offsetof(cGZDrawContext, mTextureArea) == 0x78 && offsetof(cGZDrawContext, mTextured) == 0x88, "layout");
static_assert(offsetof(cGZDrawContext, mBatchDepth) == 0x8c && offsetof(cGZDrawContext, mTransforms) == 0x90, "layout");
static_assert(offsetof(cGZDrawContext, mPoints) == 0xa8 && offsetof(cGZDrawContext, mSpans) == 0xcc, "layout");
static_assert(offsetof(cGZDrawContext, mRefCount) == 0xd8, "layout");
} // namespace drawcontext_layout
