#pragma once
// nGZSceneGraph::cAttributeFFTextureMatrix (lane TK, scenegraph M3b part 3): a fixed-function texture stage's
// coordinate source and matrix, whole in the standalone shape. The material's shader makes it through the COM director
// (clsid 0x8aa6d94b, the factory 0xac32a0 hooked to make ours); the game reaches its bodies only through our vtable, so
// the image's constructor, destructor, slot bodies and the four helpers only they call (0xac4bd0, 0xac2bc0, 0xab79e0,
// 0xac6090) are in effect through the factory hook (db/status/cAttributeFFTextureMatrix.jsonl).
//
// The parameters build a 3x3 matrix M (BuildMatrix): a 2D transform of the texture plane (a scale, a turn about the
// plane's normal, a translation, all about a centre) or a turn about an axis scaled uniformly, inverted on request.
// Apply then sends the stage two render state commands: the coordinate transform type (how many coordinates, and
// whether projected) and the 4x4 texture matrix, built from the source:
//   0 kSourceMatrix      M alone (two coordinates)
//   1 kSourceFrame       the context's frame (+0xdc) as a 4x4 (0x19c0e0), swizzled, times M
//   2 kSourceFrameMatrix the frame's 3x3 (times M)
//   3 kSourceDirection   a basis from the item's world offset direction and the up axis (0, 0, 1), swizzled, times M
//   4 kSourceProjection  the context's projection, swizzled, times M (four coordinates, projected)
//   5 kSourceItemInverse the inverse of the item's world transform's 3x3 (times M)
// "Times M" only when a transform parameter was set (+0x18); a 2D M then mixes rows 0 and 1 with rows 0, 1 and 3 (the
// texture plane's affine 3x3), an axis turn rows 0 to 2 with rows 0 to 2.
#include "sg/SGShaderAttributes.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// 0xab79e0 (__cdecl, returns al): whether two 3x3 matrices differ by more than 31 units in the last place in any
// element (each float's bits as a sign-magnitude integer made two's complement; -32 still counts as near). NaNs compare
// by their bits as well.
bool __cdecl TextureMatrixDiffers(const float *a, const float *b);

// 0xac6090 (__cdecl): the 4x4's rows reordered by the swizzle's nibbles (high to low: the source row of rows 0 to 3;
// 0x123 keeps them). The original reads a nibble above 3 past its copy of the rows (its stack); ours gives a row of zeros.
void __cdecl SwizzleTextureRows(float *rows, uint16_t swizzle);

// vtable 0xe77578, 0x88 bytes, factory 0xac32a0.
class cAttributeFFTextureMatrix : public cShaderControllerT<cIAttributeFFTextureMatrix> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac32a0
    enum : uint16_t {
        kSourceMatrix = 0,
        kSourceFrame = 1,
        kSourceFrameMatrix = 2,
        kSourceDirection = 3,
        kSourceProjection = 4,
        kSourceItemInverse = 5,
    };
    static constexpr uint16_t kNoSwizzle = 0x123;

    cAttributeFFTextureMatrix() = default; // 0xabdc00
    ~cAttributeFFTextureMatrix() override; // 0xabe2f0 (deleting 0xabea10, slot 24)

    // 0xac4bd0: a parameter reads the item's values (an index >= 0).
    bool IsDynamic() const;
    // 0xac2bc0 (__thiscall, ret 8): M from the parameters (read from `values` where indexed), into out (9 floats).
    void BuildMatrix(float *out, const uint8_t *values) const;

    // 9 (0xac20b0): the matrix source with no indexed parameter: the stage's commands (the matrix, then the type of two
    // coordinates) once, when M is not near the identity or a swizzle is set. Nothing otherwise.
    void Slot9(void *device, const void *key) override;
    // 10 (0xac2b70): 1 when Apply needs the item (an indexed parameter; the cache dropped) or another source (M cached
    // in +0x60); 0 for the plain matrix source.
    uint32_t Slot10() override;
    void Apply(void *ctx, void *item) override;          // 11 (0xabfd60)
    // 12 (0xac1a50): the stage's commands for a preview without a scene: source 0 two coordinates and the identity;
    // 1 to 3 three coordinates and the Euler matrix of (pi / 2, pi, pi / 4) (0x1a5660); 4 four projected coordinates and
    // a fixed projection; above, none.
    void Slot12(void *device, const void *key) override;

    void SetStage(uint32_t stage) override { mStage = stage; } // 14 (folded shared_vf 0x1eeb80)
    void SetSource(uint16_t source) override;                  // 15 (0xac6600)
    void SetSwizzle(uint16_t swizzle) override;                // 16 (0xac65f0)
    void SetRotation(const cIndexedFloat *angle) override;     // 17 (0xac64d0): turns
    void SetAxisRotation(const cIndexedFloat *angle, const cIndexedFloat3 *axis) override; // 18 (0xac64a0)
    void SetScale(const cIndexedFloat *scale) override;        // 19 (0xac65b0)
    void SetScaleUV(const cIndexedFloat2 *scale) override;     // 20 (0xac65d0)
    void SetTranslation(const cIndexedFloat2 *offset) override; // 21 (0xac6700)
    void SetCenter(const cIndexedFloat2 *center) override;     // 22 (0xac6400)
    void SetInvert(bool on) override;                          // 23 (0xac62c0)
    uint32_t HashParams() override;                            // 25 (0xac35e0): JenkinsHash of +0x10..+0x5f
    bool EqualParams(cIAttributeFFTextureMatrix *other) override; // 26 (0xac5470): the same 20 words

    uint32_t mStage = 0;                 // +0x10
    uint16_t mSource = kSourceMatrix;    // +0x14
    uint16_t mSwizzle = kNoSwizzle;      // +0x16
    bool mHasTransform = false;          // +0x18: a transform parameter was set (Apply multiplies by M)
    bool mAxisRotation = false;          // +0x19: M is the turn about mAxis (SetAxisRotation), not the 2D transform
    bool mInvert = false;                // +0x1a
    uint8_t mPad = 0;                    // +0x1b: zeroed with the flags, hashed with them
    cIndexedFloat mAngle{-1, 0.0f};      // +0x1c: turns
    cIndexedFloat3 mAxis{-1, {1.0f, 0.0f, 0.0f}}; // +0x24
    cIndexedFloat mScale{-1, 1.0f};      // +0x34
    cIndexedFloat2 mScaleUV{-1, {1.0f, 1.0f}};    // +0x3c
    cIndexedFloat2 mTranslation{-1, {0.0f, 0.0f}}; // +0x48
    cIndexedFloat2 mCenter{-1, {0.0f, 0.0f}};      // +0x54
    float mCache[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}; // +0x60: M when Slot10 cached it
    bool mCacheValid = false;            // +0x84
};
static_assert(sizeof(cAttributeFFTextureMatrix) == 0x88);
static_assert(offsetof(cAttributeFFTextureMatrix, mStage) == 0x10);
static_assert(offsetof(cAttributeFFTextureMatrix, mSwizzle) == 0x16);
static_assert(offsetof(cAttributeFFTextureMatrix, mPad) == 0x1b);
static_assert(offsetof(cAttributeFFTextureMatrix, mAngle) == 0x1c);
static_assert(offsetof(cAttributeFFTextureMatrix, mAxis) == 0x24);
static_assert(offsetof(cAttributeFFTextureMatrix, mScale) == 0x34);
static_assert(offsetof(cAttributeFFTextureMatrix, mScaleUV) == 0x3c);
static_assert(offsetof(cAttributeFFTextureMatrix, mTranslation) == 0x48);
static_assert(offsetof(cAttributeFFTextureMatrix, mCenter) == 0x54);
static_assert(offsetof(cAttributeFFTextureMatrix, mCache) == 0x60);
static_assert(offsetof(cAttributeFFTextureMatrix, mCacheValid) == 0x84);

} // namespace nGZSceneGraph
