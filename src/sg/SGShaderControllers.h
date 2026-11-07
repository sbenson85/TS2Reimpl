#pragma once
// nGZSceneGraph::cShaderComponentT<I> and cShaderControllerT<I> (lane TG, scenegraph M3b): the two template levels under
// the shader's parts (the attribute, apply and animation classes a material's shader keeps: cFFDepthOffset,
// cAttributeFFTextureMatrix, cApplyRenderTarget, ...). One template each, instantiated per interface I as the game does
// (18 nGZSceneGraph instantiations, RVA order below; nTSSG::cApplyUserLightState is a 19th, elsewhere in the image).
//
//   cShaderComponentT<I> : I            +0 vtable, +4 the count, +8 the registered byte (size 0xc)
//   cShaderControllerT<I> : ...T<I>     +0xc the hash Init computes (size 0x10)
//
// The slots (cIShaderComponent, 0-13; I's own from 14; the destructor at d, the count of I's slots; then
// cShaderControllerT's two pure virtuals d + 1 HashParams and d + 2 EqualParams, which every leaf class overrides):
//   0 QueryInterface (per copy, both levels)  1 AddRef (shared_vf_148fb0)  2 Release (one body per d, below)
//   3 Reset (does nothing, 0x140fe0)  4 / 5 SetRegistered / IsRegistered (the byte; 0x1489f0 / 0x1480a0, folded)
//   6 GetHash (+0xc, 0x145770)  7 IsEquivalent (per copy)  8 Init (per copy)  9-13 the controller's defaults (folded
//   do-nothing / zero bodies 0x1b9a50, 0x6c6b0, 0x300a0)
// Release: the last reference unregisters a registered part from the material manager (GetCachedQueriedObject's slot
// 27) and deletes through slot d. The image keeps one Release body per d (shared between the instantiations with that
// d, and the leaf classes' slot 2 jump stubs to them).
//
// The game's copies (db/status/SGShaderControllers.jsonl): each instantiation's component and controller
// QueryInterface, IsEquivalent, Init, constructors, destructors and deleting destructors, and the Release bodies. They are
// hooked to cShaderControllerCopy<I>'s adapters (this = the game's object) or to the template's own constructor and
// destructor (src/hook/tables/SGShaderControllers.cpp). The leaf classes in the standalone shape derive the templates
// and carry them in our vtables.
#include "gz/cIGZUnknown.h"
#include "sg/cShader.h" // ShaderHashMix, ShaderHashCombine

#include <cstdint>

namespace nGZSceneGraph {

// The part interface (iid 0x9c544810): slots 3-13, which every shader part has.
class cIShaderComponent : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x9c544810;

    virtual void Reset() = 0;                       // 3
    virtual void SetRegistered(bool on) = 0;        // 4: the byte Release checks (cShader calls its own SetEnabled)
    virtual bool IsRegistered() = 0;                // 5
    virtual uint32_t GetHash() = 0;                 // 6
    virtual bool IsEquivalent(cIShaderComponent *other) = 0;       // 7
    virtual bool Init(void *ctx, void *init, void *table, void *key) = 0; // 8 (ret 0x10): computes the hash
    virtual void Slot9(void *out, const void *key) = 0; // 9
    virtual uint32_t Slot10() = 0;                  // 10: the part's kind
    virtual void Apply(void *ctx, void *frame) = 0; // 11
    virtual void Slot12(void *device, const void *key) = 0; // 12
    virtual void EndState(void *ctx) = 0;           // 13
};

// The attributes' parameters: read from the drawn item's values block at mIndex (a byte offset) when mIndex >= 0 and
// the item has a block, else mValue.
struct cIndexedFloat {
    int32_t mIndex;
    float mValue;
};
struct cIndexedFloat2 {
    int32_t mIndex;
    float mValue[2];
};
struct cIndexedFloat3 {
    int32_t mIndex;
    float mValue[3];
};

// The interfaces (I::kIid the component's iid, kIid + 1 the controller's; kSlots = d, I's slot count).
#define SHADER_IFACE_BEGIN(Name, iid, slots)                                                                       \
    class Name : public cIShaderComponent {                                                                           \
    public:                                                                                                           \
        static constexpr uint32_t kIid = iid;                                                                         \
        static constexpr int kSlots = slots;
#define SHADER_IFACE_END(Name)                                                                                     \
    }                                                                                                                 \
    ;                                                                                                                 \
    static_assert(Name::kSlots >= 14, #Name);

SHADER_IFACE_BEGIN(cIAnimColorScalarAlpha, 0xfcb89ab2, 16)
    virtual void SetColor(const uint32_t *color) = 0; // 14
    // 15 (ret 0x14): the alpha's wave (AnimationWave's rate, phase and kind) and the values it blends between.
    virtual void SetAnimation(float rate, float phase, float from, float to, int32_t wave) = 0;
SHADER_IFACE_END(cIAnimColorScalarAlpha)
SHADER_IFACE_BEGIN(cIAnimatedTextureMatrix, 0xbc992c0a, 19)
    virtual void SetTarget(int32_t target, uint32_t index) = 0; // 14
    // 15, 16, 17 (ret 0x14): the rotation's, translation's and scale's wave: AnimationWave's rate and phase, the values it
    // blends between (the rotation's two turns; the others' two pairs) and the kind.
    virtual void SetRotation(float rate, float phase, float from, float to, int32_t wave) = 0;                // 15
    virtual void SetTranslation(float rate, float phase, const float *from, const float *to, int32_t wave) = 0; // 16
    virtual void SetScale(float rate, float phase, const float *from, const float *to, int32_t wave) = 0;       // 17
    virtual void SetCenter(const float *center) = 0;                                                          // 18
SHADER_IFACE_END(cIAnimatedTextureMatrix)
SHADER_IFACE_BEGIN(cIAnimatedTiledTexture, 0xfca91c0c, 17)
    virtual void SetTarget(int32_t target, uint32_t index) = 0; // 14
    virtual void SetTiles(int32_t columns, int32_t rows) = 0;   // 15
    virtual void SetRate(float rate) = 0;                       // 16
SHADER_IFACE_END(cIAnimatedTiledTexture)
SHADER_IFACE_BEGIN(cIApplyColorScalarRadiance, 0x0ba655de, 15)
    virtual void Slot14() = 0;
SHADER_IFACE_END(cIApplyColorScalarRadiance)
SHADER_IFACE_BEGIN(cIApplyFFLightStates, 0x4a0b687a, 14)
SHADER_IFACE_END(cIApplyFFLightStates)
SHADER_IFACE_BEGIN(cIApplyForceDepthTestFailCondition, 0x4d0817c1, 16)
    virtual void Slot14() = 0;
    virtual void Slot15() = 0;
SHADER_IFACE_END(cIApplyForceDepthTestFailCondition)
SHADER_IFACE_BEGIN(cIApplyNonStandardLightState, 0x4c168bfe, 15)
    virtual void Slot14() = 0;
SHADER_IFACE_END(cIApplyNonStandardLightState)
SHADER_IFACE_BEGIN(cIApplyRenderTarget, 0xec181823, 20)
    virtual void SetTexture(const char *name, uint32_t index) = 0;     // 14: a render target texture's surface
    virtual void SetTarget(uint32_t kind, uint32_t index) = 0;         // 15: the context's surfaces
    virtual void SetRestore(bool on) = 0;                              // 16
    virtual void SetViewportMode(uint8_t mode) = 0;                    // 17
    virtual void SetClearColor(bool on, const float *color) = 0;       // 18
    virtual void SetClearDepth(bool on, float z, uint32_t stencil) = 0; // 19
SHADER_IFACE_END(cIApplyRenderTarget)
SHADER_IFACE_BEGIN(cIApplyShaderProgramConstants, 0x8a60ffeb, 15)
    // 14 (ret 4): one binding (cApplyShaderProgramConstants::Binding, 0x20 bytes), copied.
    virtual void AddConstant(const void *binding) = 0;
SHADER_IFACE_END(cIApplyShaderProgramConstants)
SHADER_IFACE_BEGIN(cIApplyStencilStateForOverlappingReflections, 0x6df17b79, 14)
SHADER_IFACE_END(cIApplyStencilStateForOverlappingReflections)
SHADER_IFACE_BEGIN(cIAttributeColorScalar, 0x2a8b1e97, 18)
    virtual void SetColor(const cIndexedFloat3 *color, const cIndexedFloat *alpha) = 0; // 14
    virtual void SetAlpha(const cIndexedFloat *alpha) = 0;                              // 15
    virtual void SetColorEvent(int32_t index) = 0;                                      // 16
    // 17 (ret 8): the item's modulator at `position` in its group, asked for `index`.
    virtual void SetModulator(uint32_t position, int32_t index) = 0;
SHADER_IFACE_END(cIAttributeColorScalar)
SHADER_IFACE_BEGIN(cIAttributeFFMaterial, 0x69ff7545, 24)
    virtual void SetDiffuse(const cIndexedFloat3 *color, const cIndexedFloat *alpha) = 0;  // 14
    virtual void SetDiffuseAlpha(const cIndexedFloat *alpha) = 0;                          // 15
    virtual void SetAmbient(const cIndexedFloat3 *color, const cIndexedFloat *alpha) = 0;  // 16
    virtual void SetAmbientAlpha(const cIndexedFloat *alpha) = 0;                          // 17
    virtual void SetSpecular(const cIndexedFloat3 *color, const cIndexedFloat *alpha) = 0; // 18
    virtual void SetSpecularAlpha(const cIndexedFloat *alpha) = 0;                         // 19
    virtual void SetEmissive(const cIndexedFloat3 *color, const cIndexedFloat *alpha) = 0; // 20
    virtual void SetEmissiveAlpha(const cIndexedFloat *alpha) = 0;                         // 21
    virtual void SetPower(const cIndexedFloat *power) = 0;                                 // 22
    virtual void SetColorEvent(int32_t index) = 0;                                         // 23
SHADER_IFACE_END(cIAttributeFFMaterial)
SHADER_IFACE_BEGIN(cIAttributeFFScaleModelToViewTransform, 0x8b94423f, 15)
    virtual void SetScale(const cIndexedFloat *scale) = 0; // 14
SHADER_IFACE_END(cIAttributeFFScaleModelToViewTransform)
SHADER_IFACE_BEGIN(cIAttributeFFTextureMatrix, 0x8aa6d940, 24)
    virtual void SetStage(uint32_t stage) = 0;                                               // 14
    virtual void SetSource(uint16_t source) = 0;                                             // 15
    virtual void SetSwizzle(uint16_t swizzle) = 0;                                           // 16
    virtual void SetRotation(const cIndexedFloat *angle) = 0;                                // 17
    virtual void SetAxisRotation(const cIndexedFloat *angle, const cIndexedFloat3 *axis) = 0; // 18
    virtual void SetScale(const cIndexedFloat *scale) = 0;                                   // 19
    virtual void SetScaleUV(const cIndexedFloat2 *scale) = 0;                                // 20
    virtual void SetTranslation(const cIndexedFloat2 *offset) = 0;                           // 21
    virtual void SetCenter(const cIndexedFloat2 *center) = 0;                                // 22
    virtual void SetInvert(bool on) = 0;                                                     // 23
SHADER_IFACE_END(cIAttributeFFTextureMatrix)
SHADER_IFACE_BEGIN(cIFFApplyTransforms, 0xca0ca1d4, 15)
    virtual void SetUseItemTransform(bool on) = 0; // 14
SHADER_IFACE_END(cIFFApplyTransforms)
SHADER_IFACE_BEGIN(cIFFDepthOffset, 0xad30a7b6, 15)
    virtual void SetOffset(int32_t offset) = 0; // 14
SHADER_IFACE_END(cIFFDepthOffset)
SHADER_IFACE_BEGIN(cIShaderControllerNotify, 0x2c4cc946, 15)
    virtual void Slot14() = 0;
SHADER_IFACE_END(cIShaderControllerNotify)
SHADER_IFACE_BEGIN(cIValidateRenderShaderContext, 0x2d2f66a8, 20)
    virtual void AddVertexElement(uint32_t usage, uint8_t index, uint8_t mode) = 0; // 14: 0 forbidden, 1 required
    virtual void SetFlag(uint32_t flag, int32_t mode) = 0;                         // 15: 0 forbidden, 1 required
    virtual void SetLightCount(uint32_t type, int32_t count) = 0;                  // 16: -1 any
    virtual void SetTransformCount(int32_t count) = 0;                             // 17: -1 any
    virtual void SetMorphChannelCount(int32_t count) = 0;                          // 18: -1 any
    virtual void SetViewerRenderType(int32_t type) = 0;                            // 19: -1 any
SHADER_IFACE_END(cIValidateRenderShaderContext)

#undef SHADER_IFACE_BEGIN
#undef SHADER_IFACE_END

// The material manager's unregister (slot 27 of what GetCachedQueriedObject answers), Release's call.
void UnregisterShaderPart(cIGZUnknown *part);

template <class I> class cShaderComponentT : public I {
public:
    cShaderComponentT() : mRefCount(0), mRegistered(false) {}
    virtual ~cShaderComponentT() {} // slot I::kSlots

    // 0 (one copy per I): 1, cIShaderComponent's iid and I's give this, counted.
    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (out == nullptr) return false;
        if (iid != 1 && iid != cIShaderComponent::kIid && iid != I::kIid) return false;
        *out = this;
        this->AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++mRefCount; }
    // 2 (one body per I::kSlots): the count; at zero a registered part leaves the material manager, then the deleting
    // destructor (slot I::kSlots) with 1, and 0.
    uint32_t Release() override
    {
        if (--mRefCount != 0) return mRefCount;
        if (mRegistered) UnregisterShaderPart(this);
        delete this;
        return 0;
    }
    void Reset() override {}
    void SetRegistered(bool on) override { mRegistered = on; }
    bool IsRegistered() override { return mRegistered; }

    uint32_t mRefCount; // +4
    bool mRegistered;   // +8
};

template <class I> class cShaderControllerT : public cShaderComponentT<I> {
public:
    static constexpr uint32_t kControllerIid = 0x2d2df503;

    cShaderControllerT() : mHash(0) {}
    ~cShaderControllerT() override {}

    // 0 (one copy per I): the controller iid gives this, counted; I's controller iid (I::kIid + 1) gives this uncounted
    // (the image's; its callers do not release it); the rest is the component's.
    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (out == nullptr) return false;
        if (iid == kControllerIid) {
            *out = this;
            this->AddRef();
            return true;
        }
        if (iid == I::kIid + 1) {
            *out = this;
            return true;
        }
        return cShaderComponentT<I>::QueryInterface(iid, out);
    }
    uint32_t GetHash() override { return mHash; }
    // 7 (one copy per I): `other` as I's controller (uncounted); the same object, or the same hash (asked of `other`)
    // and EqualParams.
    bool IsEquivalent(cIShaderComponent *other) override
    {
        void *same = nullptr;
        if (!other->QueryInterface(I::kIid + 1, &same)) return false;
        if (same == this) return true;
        return other->GetHash() == mHash && EqualParams(static_cast<I *>(same));
    }
    // 8 (one copy per I; the arguments unused): the hash of the parameters and I's iid.
    bool Init(void *, void *, void *, void *) override
    {
        const uint32_t params = HashParams();
        mHash = ShaderHashCombine(ShaderHashMix(params), ShaderHashMix(I::kIid));
        return true;
    }
    void Slot9(void *, const void *) override {}
    uint32_t Slot10() override { return 0; }
    void Apply(void *, void *) override {}
    void Slot12(void *, const void *) override {}
    void EndState(void *) override {}

    virtual uint32_t HashParams() = 0;            // I::kSlots + 1
    virtual bool EqualParams(I *other) = 0;       // I::kSlots + 2: the parameters equal other's

    uint32_t mHash; // +0xc
};

// The game's copies: `this` is the game's object (its vtables the game's); each adapter runs the template's body.
template <class I> class cShaderControllerCopy : public cShaderControllerT<I> {
    using Component = cShaderComponentT<I>;
    using Controller = cShaderControllerT<I>;

public:
    static constexpr int kSlot = I::kSlots;

    bool HookComponentQueryInterface(uint32_t iid, void **out)
    {
        [[msvc::forceinline_calls]] return Component::QueryInterface(iid, out);
    }
    bool HookQueryInterface(uint32_t iid, void **out) { [[msvc::forceinline_calls]] return Controller::QueryInterface(iid, out); }
    bool HookIsEquivalent(cIShaderComponent *other) { [[msvc::forceinline_calls]] return Controller::IsEquivalent(other); }
    bool HookInit(void *ctx, void *init, void *table, void *key)
    {
        [[msvc::forceinline_calls]] return Controller::Init(ctx, init, table, key);
    }
    uint32_t HookRelease() { [[msvc::forceinline_calls]] return Component::Release(); }
    // The two levels' deleting destructors (ret 4, returns this): the level's destructor, then the block (0xc / 0x10).
    Component *HookComponentDestroy(uint32_t flags)
    {
        this->Component::~Component();
        if (flags & 1) ::operator delete(this, sizeof(Component));
        return this;
    }
    Controller *HookControllerDestroy(uint32_t flags)
    {
        this->Controller::~Controller();
        if (flags & 1) ::operator delete(this, sizeof(Controller));
        return this;
    }
};

} // namespace nGZSceneGraph

// The controller level's constructor and destructor per instantiation (C names the linker resolves to the decorated ones,
// /alternatename in SGShaderControllers.cpp): the hooks of the image's copies. Code addresses, never data.
extern "C" const uint8_t ShaderControllerT_AnimColorScalarAlpha_Construct; // 0xabd3c0
extern "C" const uint8_t ShaderControllerT_AnimColorScalarAlpha_Destruct;  // 0xabe0a0
extern "C" const uint8_t ShaderControllerT_AnimatedTextureMatrix_Construct; // 0xabd3e0
extern "C" const uint8_t ShaderControllerT_AnimatedTextureMatrix_Destruct;  // 0xabe0b0
extern "C" const uint8_t ShaderControllerT_AnimatedTiledTexture_Construct; // 0xabd400
extern "C" const uint8_t ShaderControllerT_AnimatedTiledTexture_Destruct;  // 0xabe0c0
extern "C" const uint8_t ShaderControllerT_ApplyColorScalarRadiance_Construct; // 0xabd420
extern "C" const uint8_t ShaderControllerT_ApplyColorScalarRadiance_Destruct;  // 0xabe0d0
extern "C" const uint8_t ShaderControllerT_ApplyFFLightStates_Construct; // 0xabd440
extern "C" const uint8_t ShaderControllerT_ApplyFFLightStates_Destruct;  // 0xabe0e0
extern "C" const uint8_t ShaderControllerT_ApplyForceDepthTestFailCondition_Construct; // 0xabd460
extern "C" const uint8_t ShaderControllerT_ApplyForceDepthTestFailCondition_Destruct;  // 0xabe0f0
extern "C" const uint8_t ShaderControllerT_ApplyNonStandardLightState_Construct; // 0xabd480
extern "C" const uint8_t ShaderControllerT_ApplyNonStandardLightState_Destruct;  // 0xabe100
extern "C" const uint8_t ShaderControllerT_ApplyRenderTarget_Construct; // 0xabd4a0
extern "C" const uint8_t ShaderControllerT_ApplyRenderTarget_Destruct;  // 0xabe110
extern "C" const uint8_t ShaderControllerT_ApplyShaderProgramConstants_Construct; // 0xabd4c0
extern "C" const uint8_t ShaderControllerT_ApplyShaderProgramConstants_Destruct;  // 0xabe120
extern "C" const uint8_t ShaderControllerT_ApplyStencilStateForOverlappingReflections_Construct; // 0xabd4e0
extern "C" const uint8_t ShaderControllerT_ApplyStencilStateForOverlappingReflections_Destruct;  // 0xabe130
extern "C" const uint8_t ShaderControllerT_AttributeColorScalar_Construct; // 0xabd500
extern "C" const uint8_t ShaderControllerT_AttributeColorScalar_Destruct;  // 0xabe140
extern "C" const uint8_t ShaderControllerT_AttributeFFMaterial_Construct; // 0xabd520
extern "C" const uint8_t ShaderControllerT_AttributeFFMaterial_Destruct;  // 0xabe150
extern "C" const uint8_t ShaderControllerT_AttributeFFScaleModelToViewTransform_Construct; // 0xabd540
extern "C" const uint8_t ShaderControllerT_AttributeFFScaleModelToViewTransform_Destruct;  // 0xabe160
extern "C" const uint8_t ShaderControllerT_AttributeFFTextureMatrix_Construct; // 0xabd560
extern "C" const uint8_t ShaderControllerT_AttributeFFTextureMatrix_Destruct;  // 0xabe170
extern "C" const uint8_t ShaderControllerT_FFApplyTransforms_Construct; // 0xabd580
extern "C" const uint8_t ShaderControllerT_FFApplyTransforms_Destruct;  // 0xabe180
extern "C" const uint8_t ShaderControllerT_FFDepthOffset_Construct; // 0xabd5a0
extern "C" const uint8_t ShaderControllerT_FFDepthOffset_Destruct;  // 0xabe190
extern "C" const uint8_t ShaderControllerT_ShaderControllerNotify_Construct; // 0xabd5c0
extern "C" const uint8_t ShaderControllerT_ShaderControllerNotify_Destruct;  // 0xabe1a0
extern "C" const uint8_t ShaderControllerT_ValidateRenderShaderContext_Construct; // 0xabd5e0
extern "C" const uint8_t ShaderControllerT_ValidateRenderShaderContext_Destruct;  // 0xabe1b0
