#pragma once
#include <cstddef>
#include <cstdint>
#include <new>

#include "game/game.h"
#include "game/owned.h"

// The vtables our code stores as owned statics (game/owned.h): the image's tables in the hooked game, our own tables
// (ts2_own_vt_* in RenderStates.cpp) in an image-free program (standalone/owned_statics.cpp). The other states still
// store GameAddress words: nothing image-free reaches them yet.
TS2_OWNED_STATIC(const OwnedVTable, d9c594); // cStateHeader
TS2_OWNED_STATIC(const OwnedVTable, d9c5e8); // cClippingState
TS2_OWNED_STATIC(const OwnedVTable, d9c5dc); // cDepthRangeState
TS2_OWNED_STATIC(const OwnedVTable, d9ff54); // cRenderTargetsState
TS2_OWNED_STATIC(const OwnedVTable, d9c5d0); // cViewportState

// nGZGraphic4 render states (RVAs 1.18.0.187): small value objects the renderer, the scene graph and the material
// commands build on the stack or in state blocks, each a cStateBase<T> over cStateHeader. Two slots each:
//   slot 0  deleting destructor: stores the cStateHeader vtable (the destructors are otherwise trivial), frees
//           sizeof(T) when bit 0 of the flags is set, returns this. The compiler folded the bodies by size, so one RVA
//           serves every state of that size (0x14c620 is all the 0xc-byte states).
//   slot 1  CloneInto(dest): copy-constructs the state into the memory at dest (placement new of T's implicit copy
//           constructor: cStateHeader part with its vtable, then T's vtable, then T's fields). The three large states
//           call their out-of-line copy constructor instead.
//
// Hook-shaped, not in the standalone shape (docs/standalone-shape.md): the game constructs these objects inline in
// dozens of render functions (tools/xrefs.py on any of the vtables), so every live object carries the game's vtables
// and the members are hooked at their RVAs and store the game's vtables, as the originals do. The classes become real
// C++ classes (virtual destructor, virtual CloneInto in cStateBase<T>) once their constructing code is ours.
//
// Field names follow each state's role (Direct3D render and sampler states); the meaning of a byte is tentative where
// the comment says so. Only the copies define the layouts here: byte, word, dword, 8-, 12- and 16-byte groups.

namespace nGZGraphic4 {

struct Float3 {
    float x, y, z;
};
static_assert(sizeof(Float3) == 12, "layout");

struct Float4 {
    float r, g, b, a;
};
static_assert(sizeof(Float4) == 16, "layout");

// The common head: vtable, a state id byte and a 16-bit size (the padding byte at +5 is never copied).
struct cStateHeader {
    static constexpr uint32_t kVTable = 0xd9c594;
    static const void *VTable() { return &ts2_rva_d9c594; }

    // Slot 0 of every state (0x14c870 for cStateHeader's own 8 bytes; the folded bodies for each other size). State is
    // the class whose size is freed.
    template <class State> cStateHeader *DeletingDestructor(uint32_t flags);

    // The copy of the head that every copy constructor starts with: the cStateHeader vtable, the id and the size.
    void CopyHeader(const cStateHeader &other)
    {
        mVTable = VTable();
        mId = other.mId;
        mSize = other.mSize;
    }

    const void *mVTable; // +0
    uint8_t mId;         // +4
    uint16_t mSize;      // +6
};
static_assert(offsetof(cStateHeader, mSize) == 6, "layout");
static_assert(sizeof(cStateHeader) == 8, "layout: the size 0x14c870 frees");

// States of one texture stage (fixed-function texture stage states).
struct cTextureStageStateHeader : cStateHeader {
    static constexpr uint32_t kVTable = 0xd9c5a0;
    void CopyStageHeader(const cTextureStageStateHeader &other)
    {
        CopyHeader(other);
        mVTable = GameAddress<void>(kVTable);
        mStage = other.mStage;
    }
    uint32_t mStage; // +8
};
static_assert(sizeof(cTextureStageStateHeader) == 0xc, "layout");

// States of one sampler.
struct cSamplerStateHeader : cStateHeader {
    static constexpr uint32_t kVTable = 0xd9c5ac;
    void CopySamplerHeader(const cSamplerStateHeader &other)
    {
        CopyHeader(other);
        mVTable = GameAddress<void>(kVTable);
        mSampler = other.mSampler;
    }
    uint32_t mSampler; // +8
};
static_assert(sizeof(cSamplerStateHeader) == 0xc, "layout");

// --- 0xc-byte states (deleting destructor 0x14c620) ---

struct cAlphaBlendState : cStateHeader { // vtable 0xd9c5f4, CloneInto 0x14e880
    static constexpr uint32_t kVTable = 0xd9c5f4;
    void CloneInto(void *dest) const;
    uint8_t mEnabled;   // +8
    uint8_t mSource;    // +9  (blend factors and op: tentative)
    uint8_t mDest;      // +0xa
    uint8_t mOperation; // +0xb
};
static_assert(sizeof(cAlphaBlendState) == 0xc, "layout");

struct cAlphaTestFunction : cStateHeader { // vtable 0xd9c60c, CloneInto 0x14e8c0
    static constexpr uint32_t kVTable = 0xd9c60c;
    void CloneInto(void *dest) const;
    uint8_t mFunction; // +8
};
static_assert(sizeof(cAlphaTestFunction) == 0xc, "layout");

struct cAlphaTestState : cStateHeader { // vtable 0xd9c600, CloneInto 0x14e8f0
    static constexpr uint32_t kVTable = 0xd9c600;
    void CloneInto(void *dest) const;
    uint8_t mEnabled;   // +8
    uint8_t mReference; // +9 (tentative)
};
static_assert(sizeof(cAlphaTestState) == 0xc, "layout");

struct cClippingState : cStateHeader { // vtable 0xd9c5e8, CloneInto 0x14e920
    static constexpr uint32_t kVTable = 0xd9c5e8;
    static const void *VTable() { return &ts2_rva_d9c5e8; }
    void CloneInto(void *dest) const;
    uint8_t mEnabled; // +8
};
static_assert(sizeof(cClippingState) == 0xc, "layout");

struct cColorScalarState : cStateHeader { // vtable 0xd9c624, CloneInto 0x14e950
    static constexpr uint32_t kVTable = 0xd9c624;
    void CloneInto(void *dest) const;
    uint32_t mColor; // +8
};
static_assert(sizeof(cColorScalarState) == 0xc, "layout");

struct cColorWriteEnableState : cStateHeader { // vtable 0xda00dc, CloneInto 0x186a10
    static constexpr uint32_t kVTable = 0xda00dc;
    void CloneInto(void *dest) const;
    uint32_t mMask; // +8
};
static_assert(sizeof(cColorWriteEnableState) == 0xc, "layout");

struct cCullModeState : cStateHeader { // vtable 0xd9c5c4, CloneInto 0x14e980
    static constexpr uint32_t kVTable = 0xd9c5c4;
    void CloneInto(void *dest) const;
    uint8_t mMode; // +8
};
static_assert(sizeof(cCullModeState) == 0xc, "layout");

struct cDepthTestFunction : cStateHeader { // vtable 0xda0504, CloneInto 0x1884c0
    static constexpr uint32_t kVTable = 0xda0504;
    void CloneInto(void *dest) const;
    uint8_t mFunction; // +8
};
static_assert(sizeof(cDepthTestFunction) == 0xc, "layout");

struct cDepthTestState : cStateHeader { // vtable 0xd9c5b8, CloneInto 0x14e9e0
    static constexpr uint32_t kVTable = 0xd9c5b8;
    void CloneInto(void *dest) const;
    uint8_t mTestEnabled;  // +8
    uint8_t mWriteEnabled; // +9 (tentative)
};
static_assert(sizeof(cDepthTestState) == 0xc, "layout");

struct cFFLightingState : cStateHeader { // vtable 0xda0224, CloneInto 0x187180
    static constexpr uint32_t kVTable = 0xda0224;
    void CloneInto(void *dest) const;
    uint8_t mEnabled;  // +8
    uint8_t mSetting9; // +9 (meaning unknown)
    uint8_t mSettingA; // +0xa (meaning unknown)
};
static_assert(sizeof(cFFLightingState) == 0xc, "layout");

struct cFillModeState : cStateHeader { // vtable 0xda00c4, CloneInto 0x186a40
    static constexpr uint32_t kVTable = 0xda00c4;
    void CloneInto(void *dest) const;
    uint8_t mMode; // +8
};
static_assert(sizeof(cFillModeState) == 0xc, "layout");

struct cShadingState : cStateHeader { // vtable 0xda00d0, CloneInto 0x186a80
    static constexpr uint32_t kVTable = 0xda00d0;
    void CloneInto(void *dest) const;
    uint8_t mMode; // +8
};
static_assert(sizeof(cShadingState) == 0xc, "layout");

struct cSpecularAddState : cStateHeader { // vtable 0xda0704, CloneInto 0x188bd0
    static constexpr uint32_t kVTable = 0xda0704;
    void CloneInto(void *dest) const;
    uint8_t mEnabled; // +8
};
static_assert(sizeof(cSpecularAddState) == 0xc, "layout");

struct cStencilOperationState : cStateHeader { // vtable 0xda05d4, CloneInto 0x188700
    static constexpr uint32_t kVTable = 0xda05d4;
    void CloneInto(void *dest) const;
    uint8_t mFailOp;      // +8
    uint8_t mDepthFailOp; // +9
    uint8_t mPassOp;      // +0xa
};
static_assert(sizeof(cStencilOperationState) == 0xc, "layout");

struct cTexCoordInterpolationState : cStateHeader { // vtable 0xda092c, CloneInto 0x189170
    static constexpr uint32_t kVTable = 0xda092c;
    void CloneInto(void *dest) const;
    uint8_t mSetting8; // +8 (meaning unknown)
    uint8_t mSetting9; // +9 (meaning unknown)
};
static_assert(sizeof(cTexCoordInterpolationState) == 0xc, "layout");

// --- 0x10-byte states (deleting destructor 0x14c650) ---

struct cDepthRangeState : cStateHeader { // vtable 0xd9c5dc, CloneInto 0x14e9b0
    static constexpr uint32_t kVTable = 0xd9c5dc;
    static const void *VTable() { return &ts2_rva_d9c5dc; }
    void CloneInto(void *dest) const;
    float mMin; // +8
    float mMax; // +0xc
};
static_assert(sizeof(cDepthRangeState) == 0x10, "layout");

struct cFFPerVertexColorState : cStateHeader { // vtable 0xda0254, CloneInto 0x187210
    static constexpr uint32_t kVTable = 0xda0254;
    void CloneInto(void *dest) const;
    uint8_t mEnabled;        // +8
    uint8_t mDiffuseSource;  // +9 (material sources: tentative)
    uint8_t mSpecularSource; // +0xa
    uint8_t mAmbientSource;  // +0xb
    uint8_t mEmissiveSource; // +0xc
};
static_assert(sizeof(cFFPerVertexColorState) == 0x10, "layout");

struct cFFTextureCoordsSourceState : cTextureStageStateHeader { // vtable 0xda0260, CloneInto 0x187260
    static constexpr uint32_t kVTable = 0xda0260;
    void CloneInto(void *dest) const;
    uint32_t mSource; // +0xc
};
static_assert(sizeof(cFFTextureCoordsSourceState) == 0x10, "layout");

struct cFFTextureTransformTypeState : cTextureStageStateHeader { // vtable 0xda0278, CloneInto 0x1872f0
    static constexpr uint32_t kVTable = 0xda0278;
    void CloneInto(void *dest) const;
    uint8_t mCount;     // +0xc (tentative)
    uint8_t mProjected; // +0xd (tentative)
};
static_assert(sizeof(cFFTextureTransformTypeState) == 0x10, "layout");

struct cStencilState : cStateHeader { // vtable 0xda05bc, CloneInto 0x188740
    static constexpr uint32_t kVTable = 0xda05bc;
    void CloneInto(void *dest) const;
    uint8_t mEnabled;    // +8
    uint32_t mReference; // +0xc (tentative)
};
static_assert(sizeof(cStencilState) == 0x10, "layout");

struct cTextureAddressingState : cSamplerStateHeader { // vtable 0xd9c630, CloneInto 0x14e7a0
    static constexpr uint32_t kVTable = 0xd9c630;
    void CloneInto(void *dest) const;
    uint8_t mAddressU; // +0xc
    uint8_t mAddressV; // +0xd
    uint8_t mAddressW; // +0xe
};
static_assert(sizeof(cTextureAddressingState) == 0x10, "layout");

// --- 0x14-byte states (deleting destructor 0x14c8d0) ---

struct cStencilFunctionState : cStateHeader { // vtable 0xda05c8, CloneInto 0x1886c0
    static constexpr uint32_t kVTable = 0xda05c8;
    void CloneInto(void *dest) const;
    uint8_t mFunction;   // +8
    uint32_t mReference; // +0xc
    uint32_t mMask;      // +0x10
};
static_assert(sizeof(cStencilFunctionState) == 0x14, "layout");

struct cTextureFilterState : cSamplerStateHeader { // vtable 0xd9c63c, CloneInto 0x14e7f0
    static constexpr uint32_t kVTable = 0xd9c63c;
    void CloneInto(void *dest) const;
    uint8_t mMinFilter;      // +0xc
    uint8_t mMagFilter;      // +0xd
    uint32_t mMaxAnisotropy; // +0x10 (tentative)
};
static_assert(sizeof(cTextureFilterState) == 0x14, "layout");

struct cTextureMipFilterState : cSamplerStateHeader { // vtable 0xd9c648, CloneInto 0x14e840
    static constexpr uint32_t kVTable = 0xd9c648;
    void CloneInto(void *dest) const;
    uint8_t mMipFilter;  // +0xc
    uint32_t mLodBias;   // +0x10 (tentative)
};
static_assert(sizeof(cTextureMipFilterState) == 0x14, "layout");

// --- 0x18-byte states (deleting destructor 0x14c8a0) ---

struct cFFEnabledLights : cStateHeader { // vtable 0xda023c, CloneInto 0x187130
    static constexpr uint32_t kVTable = 0xda023c;
    void CloneInto(void *dest) const;
    uint32_t mCount;   // +8 (tentative)
    struct Indices {
        uint32_t a, b, c;
    } mLights;         // +0xc (a 12-byte group, copied as one: tentative)
};
static_assert(sizeof(cFFEnabledLights) == 0x18, "layout");

// The stage's blend setup: nine bytes (operations and arguments; meanings tentative). Its copy is out of line
// (0x14bd50) and CloneInto (0x14ea60) calls it.
struct cTextureBlendState : cTextureStageStateHeader { // vtable 0xd9c618
    static constexpr uint32_t kVTable = 0xd9c618;
    void CloneInto(void *dest) const;
    __declspec(noinline) cTextureBlendState *CopyFrom(const cTextureBlendState &other);
    uint8_t mColorOp;   // +0xc
    uint8_t mColorArg1; // +0xd
    uint8_t mColorArg2; // +0xe
    uint8_t mColorArg0; // +0xf
    uint8_t mAlphaOp;   // +0x10
    uint8_t mAlphaArg1; // +0x11
    uint8_t mAlphaArg2; // +0x12
    uint8_t mAlphaArg0; // +0x13
    uint8_t mResultArg; // +0x14
};
static_assert(offsetof(cTextureBlendState, mResultArg) == 0x14, "layout");
static_assert(sizeof(cTextureBlendState) == 0x18, "layout");

// --- 0x1c-byte states (deleting destructor 0x186480) ---

struct cRenderTargetsState : cStateHeader { // vtable 0xd9ff54, CloneInto 0x1864b0
    static constexpr uint32_t kVTable = 0xd9ff54;
    static const void *VTable() { return &ts2_rva_d9ff54; }
    void CloneInto(void *dest) const;
    struct Targets {
        void *surface[4];
    } mTargets;         // +8 (copied as one 16-byte group)
    void *mDepthTarget; // +0x18 (tentative)
};
static_assert(sizeof(cRenderTargetsState) == 0x1c, "layout");

struct cScissorTestState : cStateHeader { // vtable 0xd9ff60, CloneInto 0x1864e0
    static constexpr uint32_t kVTable = 0xd9ff60;
    void CloneInto(void *dest) const;
    int32_t mLeft;    // +8
    int32_t mTop;     // +0xc
    int32_t mRight;   // +0x10
    int32_t mBottom;  // +0x14
    uint8_t mEnabled; // +0x18
};
static_assert(sizeof(cScissorTestState) == 0x1c, "layout");

// --- 0x20-byte state (deleting destructor 0x14c900) ---

struct cViewportState : cStateHeader { // vtable 0xd9c5d0, CloneInto 0x14ea10
    static constexpr uint32_t kVTable = 0xd9c5d0;
    static const void *VTable() { return &ts2_rva_d9c5d0; }
    void CloneInto(void *dest) const;
    uint32_t mX;      // +8
    uint32_t mY;      // +0xc
    uint32_t mWidth;  // +0x10
    uint32_t mHeight; // +0x14
    float mMinZ;      // +0x18
    float mMaxZ;      // +0x1c
};
static_assert(sizeof(cViewportState) == 0x20, "layout");

// --- 0x24-byte state (deleting destructor 0x186890) ---

// Fog: four mode bytes, the colour and three parameters. Copy out of line (0x186820), CloneInto 0x186a70.
struct cFogState : cStateHeader { // vtable 0xda00e8
    static constexpr uint32_t kVTable = 0xda00e8;
    void CloneInto(void *dest) const;
    __declspec(noinline) cFogState *CopyFrom(const cFogState &other);
    uint8_t mEnabled;    // +8
    uint8_t mMode9;      // +9 (meaning unknown)
    uint8_t mModeA;      // +0xa (meaning unknown)
    uint8_t mModeB;      // +0xb (meaning unknown)
    Float3 mColor;       // +0xc
    float mStart;        // +0x18 (tentative)
    float mEnd;          // +0x1c (tentative)
    float mDensity;      // +0x20 (tentative)
};
static_assert(offsetof(cFogState, mColor) == 0xc, "layout");
static_assert(sizeof(cFogState) == 0x24, "layout");

// --- 0x4c-byte states (deleting destructor 0x187100) ---

struct cFFMaterial : cStateHeader { // vtable 0xda0248, CloneInto 0x1871c0
    static constexpr uint32_t kVTable = 0xda0248;
    void CloneInto(void *dest) const;
    Float4 mDiffuse;  // +8
    Float4 mAmbient;  // +0x18
    Float4 mSpecular; // +0x28
    Float4 mEmissive; // +0x38
    float mPower;     // +0x48
};
static_assert(sizeof(cFFMaterial) == 0x4c, "layout");

struct cFFTextureMatrixState : cTextureStageStateHeader { // vtable 0xda026c, CloneInto 0x1872a0
    static constexpr uint32_t kVTable = 0xda026c;
    void CloneInto(void *dest) const;
    Float4 mRows[4]; // +0xc
};
static_assert(sizeof(cFFTextureMatrixState) == 0x4c, "layout");

// --- 0x58-byte state (deleting destructor 0x1870d0) ---

// One fixed-function light: type, a dword, four 3-float groups and six floats. Copy out of line (0x187030), CloneInto
// 0x187170.
struct cFFLight : cStateHeader { // vtable 0xda0230
    static constexpr uint32_t kVTable = 0xda0230;
    void CloneInto(void *dest) const;
    __declspec(noinline) cFFLight *CopyFrom(const cFFLight &other);
    uint32_t mType;     // +8
    uint32_t mIndex;    // +0xc (tentative)
    Float3 mDiffuse;    // +0x10 (tentative)
    Float3 mSpecular;   // +0x1c (tentative)
    Float3 mPosition;   // +0x28 (tentative)
    Float3 mDirection;  // +0x34 (tentative)
    float mParams[6];   // +0x40 (range, falloff, attenuations, cone angles: tentative)
};
static_assert(offsetof(cFFLight, mParams) == 0x40, "layout");
static_assert(sizeof(cFFLight) == 0x58, "layout");

template <class State> cStateHeader *cStateHeader::DeletingDestructor(uint32_t flags)
{
    mVTable = VTable();
    if (flags & 1) ::operator delete(this, sizeof(State));
    return this;
}

} // namespace nGZGraphic4
