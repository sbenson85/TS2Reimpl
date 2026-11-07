#pragma once
// cGZDrawContextRenderToHardware: the hardware draw context engine's base (1.18.0.187; docs/subsystems/rendering.md,
// R4b). It takes a cGZDrawContext's primitives (ChooseRoute's kind 6, triangles, and kind 3, rectangles), turns them
// into batches of 24-byte vertices (screen position, colour, texture coordinates) and hands them to its derived class
// through ten pure slots (25 to 34): the device state, viewport and projection, the texture and its tiles, the
// triangle and quad batches. nGZGraphic4::cDraw2D (src/gz/cDraw2D.h) is the only derived class: Direct3D through
// cDeviceDX.
//
// 0xf8 bytes. One face, cIGZDrawContextEngine (vtable 0xe4ec84, 35 slots: the engine's 24, the deleting destructor,
// the ten pure ones). Abstract: made only as cDraw2D's base (+8), whose constructor (0x14ba80) runs this one (0x8b8080).
// cDraw2D is made only by cDraw2DSystemService (ours), so the slots, the constructor, the destructor 0x8b8140 and the
// helpers are in effect through that creation.
//
// Textures: SetTexture's buffer is split by the derived class into tiles (GetTextureTiles: texture `index` with
// `count` pieces, each a TileRect: the area of the source it covers and where that sits in the tile's texture);
// drawing a textured primitive draws it once per tile piece it touches (SelectTile, then the batch), clipped to the
// piece. A texture clip (SetTextureClip) repeats the texture over the primitive, one copy per clip-sized cell.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gz/cIGZBuffer.h"
#include "gz/cIGZDrawContextEngine.h"

// 0x8b8390: the UI scale for a target of these bounds: 1 below the minimum height scaled or up to the design height,
// otherwise the height over the height of the framework's "uiScaleFactor" setting (clamped to 1..3, 1 unset: the design
// height, 2 and 3 the globals). x87 result, ret 4, ecx unused.
float __stdcall LegacyCalculateUIScale(const cGZRect *bounds);

class cGZDrawContextRenderToHardware : public cIGZDrawContextEngine {
public:
    static constexpr uint32_t kSize = 0xf8;
    static constexpr uint32_t kIid = 0x2ae78a07;

    // A batch vertex (24 bytes, the derived class's vertex format): screen position, z 0.5, ARGB colour, texture
    // coordinates.
    struct Vertex {
        float x, y, z;
        uint32_t colour;
        float u, v;
    };
    // A tile texture (16 bytes): the derived class's texture `index`, its `count` pieces (TileRects, in order), its size.
    struct Tile {
        int32_t index;
        uint32_t count;
        int32_t width, height;
    };
    // A tile piece (32 bytes): the area of the source buffer it covers and where that area sits in the tile's texture.
    struct TileRect {
        cGZRect source;
        cGZRect texels;
    };
    // A rectangle in the source buffer's texels (kind 3's rectangles, unscaled and moved to the texture's origin).
    struct TexelRect {
        float left, top, right, bottom;
    };

    cGZDrawContextRenderToHardware(); // 0x8b8080 (count 0)
    // 0x8b8140: the vectors freed, the texture and the target released. Slot 24 (0x8baf80 the deleting one).
    virtual ~cGZDrawContextRenderToHardware();

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // 0x8b82c0: ids 1 and kIid
    uint32_t AddRef() override;                              // 0x8b8290
    uint32_t Release() override;                             // 0x8b82a0: deleted through slot 24 at 0

    // cIGZDrawContextEngine
    bool SetTarget(cIGZBuffer *target) override;                                          // 0x8b82f0
    void Detach() override;                                                               // 0x8b8370: OnDetach
    cGZRect *GetBounds(cGZRect *bounds) override;                                         // 0x8b8380: the target's
    void SetClip(const cGZRect *clip) override;                                           // 0x8b8520
    void SetColors(uint32_t first, uint32_t count, const uint32_t *argb) override;        // 0x8b85d0
    void vf8(uint32_t value) override;                                                    // 0x8b8ab0 blend modes
    void vf9(uint8_t value) override;                                                     // 0x8b8af0
    void vf10(uint8_t value) override;                                                    // 0x8b8b00
    void vf11(uint8_t value) override;                                                    // 0x8b8b10 (no state change)
    void SetLineWidth(float width) override;                                              // 0x8b8b20 (nothing)
    void SetGradient(const float *coefficients, float x, float y) override;               // 0x8b89c0
    bool SetTexture(cIGZUnknown *texture, float x, float y, float sx, float sy, cGZRect *area) override; // 0x8b8600
    void ClearTextureClip() override;                                                     // 0x8b8990
    void SetTextureClip(const cGZRect *clip) override;                                    // 0x8b89a0
    void vf17() override;                                                                 // 0x8b8b30 the batch drawn
    void vf18() override;                                                                 // 0x8b8b40
    void vf19() override;                                                                 // 0x8b8b60
    bool LockSurface(cGZDrawSurface *surface, uint32_t readOnly) override;                // shared 0x1474b0: false
    void UnlockSurface() override;                                                        // shared 0x140fe0: nothing
    uint32_t ChooseRoute(uint32_t kind, const float *points, uint32_t count) override;    // 0x8b8b70
    void DrawSpans(const int32_t *spans, uint32_t count) override;                       // 0x8b8c10 (nothing)

    // The derived class's part (pure here).
    virtual bool OnTargetSet() = 0;                                      // slot 25: SetTarget's say
    virtual void OnDetach() = 0;                                         // slot 26
    virtual void ApplyState() = 0;                                       // slot 27: the blend / texture state
    virtual void SetViewport(const cGZRect *viewport) = 0;               // slot 28: device pixels
    virtual void SetProjection(const float *matrix) = 0;                 // slot 29: 16 floats, row-major
    virtual bool SetTextureSource(cIGZBuffer *texture) = 0;              // slot 30: null lets it go
    virtual void GetTextureTiles(std::vector<Tile> *tiles, std::vector<TileRect> *rects) = 0; // slot 31
    // slot 32: the tile texture drawn next; `scaled` is mScaled (0 or 1, pushed as a word).
    virtual void SelectTile(int32_t index, uint32_t scaled, uint32_t zero) = 0;
    virtual void DrawTriangles(const Vertex *vertices, uint32_t count) = 0; // slot 33: a triangle list
    virtual void DrawQuads(const Vertex *vertices, uint32_t count) = 0;     // slot 34: quads (four vertices each)

    // 0x8baa50: the batch handed to DrawQuads or DrawTriangles (by mQuadBatch) and emptied.
    void FlushBatch();
    // 0x8b8c20 (cDraw2D's batch slots call it): ApplyState and ApplyClip; both clean, no tile selected.
    void ApplyStateAndClip();
    // 0x8ba660: the clip (cut to the texture's area when mClipToArea) as the viewport and an orthographic projection
    // over it; an empty one projects everything away (the matrix at 0xe4ed20).
    void ApplyClip();
    // 0x8b8c40: the gradient's ARGB colour at (x, y).
    uint32_t GradientColor(float x, float y);
    // 0x8b9e10 (kind 6): `count` points as triangles in the colour (or the gradient), batched; with a texture, drawn
    // through the tiles at once.
    void DrawTriangleRoute(const float *points, int32_t count);
    // 0x8b8d30 (kind 3): `count` / 2 rectangles (two points each) as quads; with a texture, cut to the tiles.
    void DrawRectRoute(const float *points, int32_t count);
    // 0x8ba3a0: the batch's vertices given texture coordinates for each tile piece that meets `cell`, drawn once per
    // piece clipped to it, with the texture origin at (x, y).
    void DrawTiles(float x, float y, const cGZRect *cell);

    uint32_t mCount;                    // +4
    cIGZBuffer *mTarget;                // +8
    uint32_t mTargetWidth;              // +0xc  (SetTarget's)
    uint32_t mTargetHeight;             // +0x10
    cGZRect mClip;                      // +0x14 in device pixels / UI scale, cut to the target
    cIGZBuffer *mTexture;               // +0x24
    cIGZUnknown *mTextureSource;        // +0x28 what SetTexture was given (compared by identity)
    float mTextureX;                    // +0x2c the texture's origin
    float mTextureY;                    // +0x30
    float mScaleX;                      // +0x34
    float mScaleY;                      // +0x38
    float mInverseScaleX;               // +0x3c
    float mInverseScaleY;               // +0x40
    bool mScaled;                       // +0x44 scaled, or drawn at a UI scale above 1
    uint8_t mModulate;                  // +0x45 (vf10)
    bool mBlend;                        // +0x46 vf8: a value above 0 but 2
    bool mBlendIs2;                     // +0x47 vf8: 2
    bool mBlendAbove2;                  // +0x48 vf8: 3 or more
    uint8_t mFlag49;                    // +0x49 (vf9)
    uint8_t mFlag4a;                    // +0x4a (vf11)
    bool mStateDirty;                   // +0x4b ApplyState before the next primitive
    bool mClipDirty;                    // +0x4c ApplyClip before the next primitive
    bool mQuadBatch;                    // +0x4d the batch holds quads (else triangles)
    uint8_t mPad4e[2];
    float mTextureWidth;                // +0x50 the texture's bounds
    float mTextureHeight;               // +0x54
    cGZRect mTextureArea;               // +0x58 the area it covers (SetTexture's out-parameter)
    cGZRect mTextureClip;               // +0x68
    int32_t mCurrentTile;               // +0x78 SelectTile's, -1 none
    bool mTextureClipped;               // +0x7c
    bool mClipToArea;                   // +0x7d ApplyClip cuts the clip to mTextureArea
    uint8_t mPad7e[2];
    uint32_t mGradientBase[4];          // +0x80 per channel (A R G B order of the coefficients), 12.20 fixed
    int32_t mGradientDx[4];             // +0x90 per 1/256 pixel
    int32_t mGradientDy[4];             // +0xa0
    bool mGradientOn;                   // +0xb0
    uint8_t mFlagB1;                    // +0xb1 SetTarget and vf19 set it, vf18 clears it
    uint8_t mPadB2[2];
    std::vector<Vertex> mVertices;      // +0xb4 the batch
    std::vector<Tile> mTiles;           // +0xc0
    std::vector<TileRect> mTileRects;   // +0xcc
    std::vector<TexelRect> mTexelRects; // +0xd8 kind 3's rectangles, textured
    uint32_t mColors[4];                // +0xe4 ARGB
    uint32_t mUnusedF4;                 // +0xf4
};
static_assert(sizeof(cGZDrawContextRenderToHardware) == cGZDrawContextRenderToHardware::kSize, "layout: 0xf8 bytes");
static_assert(sizeof(cGZDrawContextRenderToHardware::Vertex) == 0x18 && sizeof(cGZDrawContextRenderToHardware::Tile) == 0x10 &&
                  sizeof(cGZDrawContextRenderToHardware::TileRect) == 0x20 &&
                  sizeof(cGZDrawContextRenderToHardware::TexelRect) == 0x10,
              "element sizes");
static_assert(offsetof(cGZDrawContextRenderToHardware, mClip) == 0x14 &&
                  offsetof(cGZDrawContextRenderToHardware, mScaled) == 0x44 &&
                  offsetof(cGZDrawContextRenderToHardware, mTextureWidth) == 0x50 &&
                  offsetof(cGZDrawContextRenderToHardware, mCurrentTile) == 0x78 &&
                  offsetof(cGZDrawContextRenderToHardware, mGradientBase) == 0x80 &&
                  offsetof(cGZDrawContextRenderToHardware, mVertices) == 0xb4 &&
                  offsetof(cGZDrawContextRenderToHardware, mTexelRects) == 0xd8 &&
                  offsetof(cGZDrawContextRenderToHardware, mColors) == 0xe4,
              "layout");
