#pragma once
// The shader's attribute and apply parts (lane TG, scenegraph M3b): the leaf classes over cShaderControllerT<I>
// (src/sg/SGShaderControllers.h), each whole in the standalone shape. The material's shader makes them through the COM
// director (the factory, hooked, makes ours); the game then reaches their bodies only through our vtables, so the
// image's constructors, destructors and slot bodies are in effect through the factory hooks
// (db/status/SGShaderAttributes.jsonl).
//
// Each leaf: +0x10 on its own parameters. Slots: 0-13 the templates' (10 the part's kind, 11 Apply, 13 EndState
// overridden here), 14.. the interface's setters, d the deleting destructor, d + 1 HashParams, d + 2 EqualParams.
#include "sg/SGShaderControllers.h"

#include <d3d9.h>

#include <cstddef>
#include <cstdint>

namespace nGZGraphic4 {
class cIDeviceDX;
class cIDeviceState;
} // namespace nGZGraphic4

namespace nGZSceneGraph {

// The render context Apply and EndState get (only the fields the parts read).
struct cShaderApplyContext {
    uint8_t mBefore[8];
    float mTime; // +8: the animated parts' clock
    uint8_t mBeforeViewport[0x84 - 0xc];
    uint32_t mViewport[4]; // +0x84: the viewport cApplyRenderTarget sends and EndState restores (cViewportState's +8)
    uint8_t mBeforeTargets[0x9c - 0x94];
    void *mPrimarySurface; // +0x9c: the compositing target's one surface (cApplyRenderTarget kind 0)
    void *mPrimaryDepth;   // +0xa0: its depth surface
    uint32_t mSurfaceCount; // +0xa4: the context's render target surfaces (mSurfaces), restored by EndState
    void *mSurfaces[4];     // +0xa8
    void *mDepth;           // +0xb8: their depth surface
    uint8_t mBeforeFrame[0xdc - 0xbc];
    // +0xdc: a frame (AffineTransform3 layout: flags, the 3x3 at +0xe0, the offset), cAttributeFFTextureMatrix's
    // sources 1 and 2 (tentative: the view's)
    float mFrame[13];
    uint8_t mBeforeProjection[0x144 - 0x110];
    D3DMATRIX mProjection; // +0x144: the projection the parts start from and EndState restores
    uint8_t mBeforeOuter[0x284 - 0x184];
    // +0x284: another context whose viewport (+0x84) cApplyRenderTarget's viewport mode 1 sends (null: this one's);
    // tentative: the enclosing pass's
    const cShaderApplyContext *mOuter;
    uint8_t mBeforeSamePass[0x298 - 0x288];
    // +0x298: set when the previous pass's setup is reused (cShader's slot 11, ShaderRenderCtx::mSame); then
    // cApplyShaderProgramConstants leaves the constants as they are
    uint8_t mSamePass;
    uint8_t mBeforeDevice[0x2a8 - 0x299];
    nGZGraphic4::cIDeviceDX *mDevice;   // +0x2a8: the device (cApplyRenderTarget's clear)
    nGZGraphic4::cIDeviceState *mState; // +0x2ac
};
static_assert(offsetof(cShaderApplyContext, mViewport) == 0x84);
static_assert(offsetof(cShaderApplyContext, mPrimarySurface) == 0x9c);
static_assert(offsetof(cShaderApplyContext, mSurfaces) == 0xa8);
static_assert(offsetof(cShaderApplyContext, mDepth) == 0xb8);
static_assert(offsetof(cShaderApplyContext, mOuter) == 0x284);
static_assert(offsetof(cShaderApplyContext, mDevice) == 0x2a8);
static_assert(offsetof(cShaderApplyContext, mTime) == 8);
static_assert(offsetof(cShaderApplyContext, mFrame) == 0xdc);
static_assert(offsetof(cShaderApplyContext, mProjection) == 0x144);
static_assert(offsetof(cShaderApplyContext, mState) == 0x2ac);
static_assert(offsetof(cShaderApplyContext, mSamePass) == 0x298);

// What Apply's second argument points at (the drawn item; only the fields the parts read).
struct cShaderApplyItem {
    uint8_t mBefore[0x20];
    const uint8_t *mValues; // +0x20: the item's parameter values, read at the parts' byte offsets (null for none)
    uint8_t mBetween[4];
    const void *mTransform; // +0x28: the item's world transform (cIDeviceState::SetWorld's 13 words)
    class cIShaderEventSource *const *mEventSource; // +0x2c: where the colour events come from
    uint32_t mModulatorRange;                       // +0x30: nibble 3 the first modulator, nibble 4 one past the last
    uint8_t mBetweenRange[8];
    class cIColorModulator *const *const *mModulators; // +0x3c: the modulator holders, indexed by position

    // The value at `index` when index >= 0 and the item has values, else `fallback`.
    template <class T> const T &Value(int32_t index, const T &fallback) const
    {
        return index >= 0 && mValues ? *reinterpret_cast<const T *>(mValues + index) : fallback;
    }
};
static_assert(offsetof(cShaderApplyItem, mValues) == 0x20);
static_assert(offsetof(cShaderApplyItem, mTransform) == 0x28);
static_assert(offsetof(cShaderApplyItem, mEventSource) == 0x2c);
static_assert(offsetof(cShaderApplyItem, mModulatorRange) == 0x30);
static_assert(offsetof(cShaderApplyItem, mModulators) == 0x3c);

// 0xac6820 (__cdecl, the result on the x87 stack): a periodic wave of x = rate * time + phase in [0, 1]: kind 0 the
// sawtooth frac(x), 1 the triangle 2 |frac(x) - 0.5|, 2 the sine (sin(2 pi x) + 1) / 2 (the sine in double precision);
// any other kind 0. frac is x less floor(x) by the 1.5 * 2^23 rounding trick, exact while |x| < 2^22. Used by
// cAnimColorScalarAlpha and cAnimatedTextureMatrix (0xac23a0).
float __cdecl AnimationWave(int32_t wave, float rate, float phase, float time);

// cFFDepthOffset (vtable 0xe77a3c, 0x14 bytes, factory 0xac32f0 for class 0xcd30a7c0): pulls the drawn depth towards
// the eye by a fixed-point offset, through the projection.
class cFFDepthOffset : public cShaderControllerT<cIFFDepthOffset> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac32f0

    cFFDepthOffset() = default;            // 0xabde10: the offset left as it was
    ~cFFDepthOffset() override;            // 0xabe310 (deleting 0xabea60, slot 15)

    uint32_t Slot10() override { return 3; } // 10 (folded 0x18c4d0)
    // 11 (0xac1550): the context's projection with _32 moved by -offset / 32768 of _42, sent to the device state.
    void Apply(void *ctx, void *item) override;
    // 13 (0xac6800): the context's projection back on the device state.
    void EndState(void *ctx) override;
    void SetOffset(int32_t offset) override { mOffset = offset; } // 14 (folded 0x1eeb80)
    uint32_t HashParams() override;                                // 16 (0xac3610): ShaderHashMix of the offset
    bool EqualParams(cIFFDepthOffset *other) override;             // 17 (folded 0x15e840)

    int32_t mOffset; // +0x10
};
static_assert(sizeof(cFFDepthOffset) == 0x14);
static_assert(offsetof(cFFDepthOffset, mOffset) == 0x10);

// cFFApplyTransforms (vtable 0xe77130, 0x14 bytes, factory 0xac32d0 for class 0xca0ca1d9): the world transform for
// the fixed-function pipeline: the item's own, or the identity.
class cFFApplyTransforms : public cShaderControllerT<cIFFApplyTransforms> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac32d0

    cFFApplyTransforms() : mUseItemTransform(true) {} // 0xabddf0
    ~cFFApplyTransforms() override;                   // 0xabe300 (deleting 0xabea40, slot 15)

    uint32_t Slot10() override { return 1; } // 10 (folded 0x15e510)
    // 11 (0xac14c0): SetWorld with the item's transform, or with the identity when the flag is clear.
    void Apply(void *ctx, void *item) override;
    void SetUseItemTransform(bool on) override { mUseItemTransform = on; } // 14 (folded 0x1520c0)
    uint32_t HashParams() override;                     // 16 (0xac3600): the flag
    bool EqualParams(cIFFApplyTransforms *other) override; // 17 (0xac54a0)

    bool mUseItemTransform; // +0x10
};
static_assert(sizeof(cFFApplyTransforms) == 0x14);
static_assert(offsetof(cFFApplyTransforms, mUseItemTransform) == 0x10);

// cAnimColorScalarAlpha (vtable 0xe77680, 0x28 bytes, factory 0xac30c0): the colour scalar with an animated alpha.
class cAnimColorScalarAlpha : public cShaderControllerT<cIAnimColorScalarAlpha> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac30c0

    cAnimColorScalarAlpha(); // 0xabd600
    ~cAnimColorScalarAlpha() override; // 0xabe1c0 (deleting 0xabe870, slot 16)

    uint32_t Slot10() override { return 1; } // 10 (folded 0x15e510)
    // 11 (0xabeba0): t = AnimationWave(kind, rate, phase, the context's clock); the colour with its alpha byte the
    // blend (1 - t) from + t to (x 256, rounded, clamped to 0..255) to the device state as a cColorScalarState.
    void Apply(void *ctx, void *item) override;
    void SetColor(const uint32_t *color) override { mColor = *color; } // 14 (0xac61e0)
    void SetAnimation(float rate, float phase, float from, float to, int32_t wave) override; // 15 (0xac61a0)
    uint32_t HashParams() override;                                  // 17 (0xac34b0): JenkinsHash of +0x10..+0x27
    bool EqualParams(cIAnimColorScalarAlpha *other) override;        // 18 (0xac51a0): the same six words

    uint32_t mColor; // +0x10: 0xaarrggbb
    float mRate;     // +0x14
    float mPhase;    // +0x18
    float mFrom;     // +0x1c
    float mTo;       // +0x20
    int32_t mWave;   // +0x24: AnimationWave's kind
};
static_assert(sizeof(cAnimColorScalarAlpha) == 0x28);
static_assert(offsetof(cAnimColorScalarAlpha, mColor) == 0x10);

// A material colour: three values and an alpha, each the item's (at its index) or its own. The state carries the alpha
// in word 0 and the three values in words 1-3 (the constructor's defaults make the alpha 1 and the specular and emissive
// values 0, so the scalar is the alpha; RenderStates.h names Float4's words r, g, b, a).
struct cMaterialColor {
    cIndexedFloat3 mColor;
    cIndexedFloat mAlpha;
};
static_assert(sizeof(cMaterialColor) == 0x18);

// The object behind the drawn item's +0x2c (only slot 10 used here): fills a colour event for (type, index).
class cIShaderEventSource {
public:
    virtual void Slot0() = 0;
    virtual void Slot1() = 0;
    virtual void Slot2() = 0;
    virtual void Slot3() = 0;
    virtual void Slot4() = 0;
    virtual void Slot5() = 0;
    virtual void Slot6() = 0;
    virtual void Slot7() = 0;
    virtual void Slot8() = 0;
    virtual void Slot9() = 0;
    virtual void GetEvent(void *event, uint32_t type, int32_t index) = 0; // 10 (ret 0xc)
};

// What a drawn item keeps per modulator (only slot 42 used here): the colour it gives for an index.
class cIColorModulator {
public:
    virtual void Slot0() = 0; virtual void Slot1() = 0; virtual void Slot2() = 0; virtual void Slot3() = 0;
    virtual void Slot4() = 0; virtual void Slot5() = 0; virtual void Slot6() = 0; virtual void Slot7() = 0;
    virtual void Slot8() = 0; virtual void Slot9() = 0; virtual void Slot10() = 0; virtual void Slot11() = 0;
    virtual void Slot12() = 0; virtual void Slot13() = 0; virtual void Slot14() = 0; virtual void Slot15() = 0;
    virtual void Slot16() = 0; virtual void Slot17() = 0; virtual void Slot18() = 0; virtual void Slot19() = 0;
    virtual void Slot20() = 0; virtual void Slot21() = 0; virtual void Slot22() = 0; virtual void Slot23() = 0;
    virtual void Slot24() = 0; virtual void Slot25() = 0; virtual void Slot26() = 0; virtual void Slot27() = 0;
    virtual void Slot28() = 0; virtual void Slot29() = 0; virtual void Slot30() = 0; virtual void Slot31() = 0;
    virtual void Slot32() = 0; virtual void Slot33() = 0; virtual void Slot34() = 0; virtual void Slot35() = 0;
    virtual void Slot36() = 0; virtual void Slot37() = 0; virtual void Slot38() = 0; virtual void Slot39() = 0;
    virtual void Slot40() = 0; virtual void Slot41() = 0;
    // 42 (+0xa8, ret 8): the modulation colour (four scales) for `index`; `color` is the colour so far.
    virtual const float *GetColor(const float *color, int32_t index) = 0;
};

// cAttributeColorScalar (vtable 0xe77224, 0x34 bytes, factory 0xac3240): the colour scalar of a material, from its own
// values or the item's, scaled by a colour event and a modulator of the item.
class cAttributeColorScalar : public cShaderControllerT<cIAttributeColorScalar> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac3240
    static constexpr uint32_t kColorEventType = 0x8c798726;

    cAttributeColorScalar();           // 0xabd970: all indices -1, values 0
    ~cAttributeColorScalar() override; // 0xabe2c0 (deleting 0xabe9b0, slot 18)

    // 9 (0xac1df0, ret 8): when the colour and the alpha are the part's own, the colour scalar of them to the device state
    // (no event, no modulator).
    void Slot9(void *device, const void *key) override;
    uint32_t Slot10() override; // 10 (0xac2b30): anything per item: the event, the modulator, the colour or the alpha
    // 11 (0xabf7b0): (alpha, r, g, b) from the item or the part, times the colour event, times the modulator's colour
    // (when the item's group has one at the position), each x 256 rounded and clamped to a byte, to the device state.
    void Apply(void *ctx, void *item) override;
    void SetColor(const cIndexedFloat3 *color, const cIndexedFloat *alpha) override; // 14 (0xac6180, shared_vf)
    void SetAlpha(const cIndexedFloat *alpha) override { mAlpha = *alpha; }          // 15 (0xac6160, shared_vf)
    void SetColorEvent(int32_t index) override { mColorEvent = index; }              // 16 (0x1f9ac0, shared_vf)
    void SetModulator(uint32_t position, int32_t index) override;                    // 17 (0xac6670)
    uint32_t HashParams() override;                                                  // 19 (0xac3580): +0x10..+0x33
    bool EqualParams(cIAttributeColorScalar *other) override;                        // 20 (0xac5410)

    cIndexedFloat3 mColor;  // +0x10
    cIndexedFloat mAlpha;   // +0x20
    int32_t mColorEvent;    // +0x28: the colour event's index (negative for none)
    int32_t mModulatorIndex;    // +0x2c: what the modulator is asked for (negative for no modulator)
    uint32_t mModulatorPosition; // +0x30: which of the item's modulators
};
static_assert(sizeof(cAttributeColorScalar) == 0x34);
static_assert(offsetof(cAttributeColorScalar, mAlpha) == 0x20);
static_assert(offsetof(cAttributeColorScalar, mModulatorPosition) == 0x30);

// cAttributeFFMaterial (vtable 0xe77430, 0x7c bytes, factory 0xac3260): the fixed-function material, from its own
// values or the item's, its diffuse and ambient scaled by a colour event when one is set.
class cAttributeFFMaterial : public cShaderControllerT<cIAttributeFFMaterial> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac3260
    static constexpr uint32_t kColorEventType = 0x8c798726;

    cAttributeFFMaterial();           // 0xabd9d0: diffuse and ambient (1, 1, 1; 1), specular and emissive (0, 0, 0; 1)
    ~cAttributeFFMaterial() override; // 0xabe2d0 (deleting 0xabe9d0, slot 24)

    // 0xac4b80: whether anything is read per item: a colour event, or any parameter with an index >= 0.
    bool IsDynamic() const;
    // 9 (0xac1f30, ret 8): when nothing is per item, the material of the own values (no event) to the device state.
    void Slot9(void *device, const void *key) override;
    uint32_t Slot10() override { return IsDynamic(); } // 10 (0xac2b60)
    // 11 (0xabf9e0): the material of the item's values (or the own ones), the diffuse and ambient scaled word by word
    // by the colour event the item's event source gives for the index, to the device state.
    void Apply(void *ctx, void *item) override;
    void SetDiffuse(const cIndexedFloat3 *color, const cIndexedFloat *alpha) override;  // 14 (0xac6180, shared_vf)
    void SetDiffuseAlpha(const cIndexedFloat *alpha) override;                          // 15 (0xac6160, shared_vf)
    void SetAmbient(const cIndexedFloat3 *color, const cIndexedFloat *alpha) override;  // 16 (0xac6230)
    void SetAmbientAlpha(const cIndexedFloat *alpha) override;                          // 17 (0xac6250)
    void SetSpecular(const cIndexedFloat3 *color, const cIndexedFloat *alpha) override; // 18 (0xac6610)
    void SetSpecularAlpha(const cIndexedFloat *alpha) override;                         // 19 (0xac6630)
    void SetEmissive(const cIndexedFloat3 *color, const cIndexedFloat *alpha) override; // 20 (0xac6270)
    void SetEmissiveAlpha(const cIndexedFloat *alpha) override;                         // 21 (0xac6290)
    void SetPower(const cIndexedFloat *power) override { mPower = *power; }             // 22 (0xac6650)
    void SetColorEvent(int32_t index) override { mColorEvent = index; }                 // 23 (folded 0x4daa20)
    uint32_t HashParams() override;                          // 25 (0xac35a0): JenkinsHash of +0x10..+0x7b
    bool EqualParams(cIAttributeFFMaterial *other) override; // 26 (0xac5440): the same 27 words

    cMaterialColor mDiffuse;  // +0x10
    cMaterialColor mAmbient;  // +0x28
    cMaterialColor mSpecular; // +0x40
    cMaterialColor mEmissive; // +0x58
    cIndexedFloat mPower;     // +0x70
    int32_t mColorEvent;      // +0x78: the colour event's index (negative for none)
};
static_assert(sizeof(cAttributeFFMaterial) == 0x7c);
static_assert(offsetof(cAttributeFFMaterial, mAmbient) == 0x28);
static_assert(offsetof(cAttributeFFMaterial, mPower) == 0x70);

// cAnimatedTiledTexture (vtable 0xe7787c, 0x24 bytes, factory 0xac3100): a texture of columns x rows tiles shown one
// after another at a rate, as a texture-coordinate transform: a fixed-function texture stage's matrix, or two rows of
// vertex or pixel shader constants.
class cAnimatedTiledTexture : public cShaderControllerT<cIAnimatedTiledTexture> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac3100
    enum : int32_t { kStage = 0, kVertexConstants = 1, kPixelConstants = 2 };

    cAnimatedTiledTexture() = default;          // 0xabd710: stage 0, 4 x 4 tiles, one a second
    ~cAnimatedTiledTexture() override;          // 0xabe1e0 (deleting 0xabe8b0, slot 17)

    // 0xac2860: the tile at `time` (floor(time x rate) modulo the tile count, unsigned, row by row) as the transform
    // (1/columns, 0, column/columns; 0, 1/rows, row/rows): to the stage `mIndex` (a cFFTextureTransformTypeState of
    // two coordinates, then the cFFTextureMatrixState), or to the two vertex or pixel constant registers at `mIndex`.
    // Other targets apply nothing.
    void ApplyFrame(nGZGraphic4::cIDeviceState *state, float time) const;

    uint32_t Slot10() override { return 1; } // 10 (folded 0x15e510)
    void Apply(void *ctx, void *item) override;               // 11 (0xabecb0): ApplyFrame at the context's clock
    void Slot12(void *device, const void *key) override;      // 12 (0xac1680): ApplyFrame at time 10
    void SetTarget(int32_t target, uint32_t index) override   // 14 (0xac6300, shared_vf)
    {
        mTarget = target;
        mIndex = index;
    }
    void SetTiles(int32_t columns, int32_t rows) override;    // 15 (0xac6690)
    void SetRate(float rate) override { mRate = rate; }       // 16 (0xac62b0)
    uint32_t HashParams() override;                           // 18 (0xac34f0): JenkinsHash of +0x10..+0x23
    bool EqualParams(cIAnimatedTiledTexture *other) override; // 19 (0xac5200): the same five words

    int32_t mTarget = kStage; // +0x10
    uint32_t mIndex = 0;      // +0x14: the texture stage or the first constant register
    int32_t mColumns = 4;     // +0x18
    int32_t mRows = 4;        // +0x1c
    float mRate = 1.0f;       // +0x20: tiles a second
};
static_assert(sizeof(cAnimatedTiledTexture) == 0x24);
static_assert(offsetof(cAnimatedTiledTexture, mTarget) == 0x10);

// cAnimatedTextureMatrix (vtable 0xe77780, 0x6c bytes, factory 0xac30e0): a texture-coordinate transform animated by
// three waves (a translation, a rotation, a scale, each optional: a rate of 0 leaves it out) about a centre, to a
// fixed-function texture stage's matrix or two rows of vertex or pixel shader constants (the target as
// cAnimatedTiledTexture's).
class cAnimatedTextureMatrix : public cShaderControllerT<cIAnimatedTextureMatrix> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac30e0
    enum : int32_t { kStage = 0, kVertexConstants = 1, kPixelConstants = 2 };

    cAnimatedTextureMatrix() = default; // 0xabd650
    ~cAnimatedTextureMatrix() override; // 0xabe1d0 (deleting 0xabe890, slot 19)

    // 0xac23a0: the 3x3 matrix (rows (a b tx), (c d ty), (0 0 1)) of the waves at `time`: the identity, the translation
    // put in the third column, then multiplied by the rotation about the centre and by the scale about the centre (each
    // only if its rate is not 0); then to the target as cAnimatedTiledTexture::ApplyFrame sends its rows (the 3x3 in the
    // upper left of the stage's 4x4, or the two rows of three and a zero in the constants).
    void ApplyFrame(nGZGraphic4::cIDeviceState *state, float time) const;

    uint32_t Slot10() override { return 1; } // 10 (folded 0x15e510)
    void Apply(void *ctx, void *item) override;          // 11 (0xabec90): ApplyFrame at the context's clock
    void Slot12(void *device, const void *key) override; // 12 (0xac1660): ApplyFrame at time 10
    void SetTarget(int32_t target, uint32_t index) override // 14 (0xac6300, shared_vf)
    {
        mTarget = target;
        mIndex = index;
    }
    void SetRotation(float rate, float phase, float from, float to, int32_t wave) override;                  // 15 (0xac64f0)
    void SetTranslation(float rate, float phase, const float *from, const float *to, int32_t wave) override; // 16 (0xac66b0)
    // 17 (0xac6530): the scale's ends are stored as reciprocals.
    void SetScale(float rate, float phase, const float *from, const float *to, int32_t wave) override;
    void SetCenter(const float *center) override; // 18 (0xac63e0)
    uint32_t HashParams() override;               // 20 (0xac34d0): JenkinsHash of +0x10..+0x6b
    bool EqualParams(cIAnimatedTextureMatrix *other) override; // 21 (0xac51d0): the same 23 words

    int32_t mTarget = kStage;   // +0x10
    uint32_t mIndex = 0;        // +0x14: the texture stage or the first constant register
    float mRotationRate = 0.0f; // +0x18
    float mRotationPhase = 0.0f; // +0x1c
    float mRotationFrom = 0.0f; // +0x20: turns
    float mRotationTo = 1.0f;   // +0x24
    int32_t mRotationWave = 1;  // +0x28
    float mScaleRate = 0.0f;    // +0x2c
    float mScalePhase = 0.0f;   // +0x30
    float mScaleFrom[2] = {1.0f, 1.0f}; // +0x34: reciprocals of what SetScale was given
    float mScaleTo[2] = {2.0f, 2.0f};   // +0x3c
    int32_t mScaleWave = 1;     // +0x44
    float mTranslationRate = 0.0f;  // +0x48
    float mTranslationPhase = 0.0f; // +0x4c
    float mTranslationFrom[2] = {}; // +0x50
    float mTranslationTo[2] = {};   // +0x58
    int32_t mTranslationWave = 1;   // +0x60
    float mCenter[2] = {0.5f, 0.5f}; // +0x64
};
static_assert(sizeof(cAnimatedTextureMatrix) == 0x6c);
static_assert(offsetof(cAnimatedTextureMatrix, mTarget) == 0x10);
static_assert(offsetof(cAnimatedTextureMatrix, mScaleFrom) == 0x34);
static_assert(offsetof(cAnimatedTextureMatrix, mTranslationRate) == 0x48);
static_assert(offsetof(cAnimatedTextureMatrix, mCenter) == 0x64);

// cAttributeFFScaleModelToViewTransform (vtable 0xe7730c, 0x18 bytes, factory 0xac3280): the world transform as the
// item's own after a uniform scale.
class cAttributeFFScaleModelToViewTransform : public cShaderControllerT<cIAttributeFFScaleModelToViewTransform> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac3280

    cAttributeFFScaleModelToViewTransform() : mScale{-1, 0.0f} {} // 0xabdbd0
    ~cAttributeFFScaleModelToViewTransform() override;            // 0xabe2e0 (deleting 0xabe9f0, slot 15)

    uint32_t Slot10() override { return 1; } // 10 (folded 0x15e510)
    // 11 (0xabfc40): the identity frame scaled by the scale (FramePlacement's SetScale 0x19d1f0), composed with the
    // item's transform, to SetWorld.
    void Apply(void *ctx, void *item) override;
    void SetScale(const cIndexedFloat *scale) override { mScale = *scale; } // 14 (0xac6590)
    uint32_t HashParams() override; // 16 (0xac35c0): JenkinsHash of the two words
    bool EqualParams(cIAttributeFFScaleModelToViewTransform *other) override; // 17 (folded 0x3a5930)

    cIndexedFloat mScale; // +0x10
};
static_assert(sizeof(cAttributeFFScaleModelToViewTransform) == 0x18);

} // namespace nGZSceneGraph
