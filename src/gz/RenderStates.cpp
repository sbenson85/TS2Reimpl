#include "gz/RenderStates.h"

#include <cstring>

// The render states' CloneInto slots and out-of-line copy constructors (src/gz/RenderStates.h). Each copies as the
// implicit copy constructor does: the head with the cStateHeader vtable (and the stage or sampler header with its
// vtable), then the state's own vtable, then its fields in order.

namespace nGZGraphic4 {

namespace {

template <class State> State *Destination(void *dest)
{
    return static_cast<State *>(dest);
}

} // namespace

void cAlphaBlendState::CloneInto(void *dest) const
{
    auto *d = Destination<cAlphaBlendState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mEnabled = mEnabled;
    d->mSource = mSource;
    d->mDest = mDest;
    d->mOperation = mOperation;
}

void cAlphaTestFunction::CloneInto(void *dest) const
{
    auto *d = Destination<cAlphaTestFunction>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mFunction = mFunction;
}

void cAlphaTestState::CloneInto(void *dest) const
{
    auto *d = Destination<cAlphaTestState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mEnabled = mEnabled;
    d->mReference = mReference;
}

void cClippingState::CloneInto(void *dest) const
{
    auto *d = Destination<cClippingState>(dest);
    d->CopyHeader(*this);
    d->mVTable = VTable();
    d->mEnabled = mEnabled;
}

void cColorScalarState::CloneInto(void *dest) const
{
    auto *d = Destination<cColorScalarState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mColor = mColor;
}

void cColorWriteEnableState::CloneInto(void *dest) const
{
    auto *d = Destination<cColorWriteEnableState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mMask = mMask;
}

void cCullModeState::CloneInto(void *dest) const
{
    auto *d = Destination<cCullModeState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mMode = mMode;
}

void cDepthTestFunction::CloneInto(void *dest) const
{
    auto *d = Destination<cDepthTestFunction>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mFunction = mFunction;
}

void cDepthTestState::CloneInto(void *dest) const
{
    auto *d = Destination<cDepthTestState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mTestEnabled = mTestEnabled;
    d->mWriteEnabled = mWriteEnabled;
}

void cFFLightingState::CloneInto(void *dest) const
{
    auto *d = Destination<cFFLightingState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mEnabled = mEnabled;
    d->mSetting9 = mSetting9;
    d->mSettingA = mSettingA;
}

void cFillModeState::CloneInto(void *dest) const
{
    auto *d = Destination<cFillModeState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mMode = mMode;
}

void cShadingState::CloneInto(void *dest) const
{
    auto *d = Destination<cShadingState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mMode = mMode;
}

void cSpecularAddState::CloneInto(void *dest) const
{
    auto *d = Destination<cSpecularAddState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mEnabled = mEnabled;
}

void cStencilOperationState::CloneInto(void *dest) const
{
    auto *d = Destination<cStencilOperationState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mFailOp = mFailOp;
    d->mDepthFailOp = mDepthFailOp;
    d->mPassOp = mPassOp;
}

void cTexCoordInterpolationState::CloneInto(void *dest) const
{
    auto *d = Destination<cTexCoordInterpolationState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mSetting8 = mSetting8;
    d->mSetting9 = mSetting9;
}

void cDepthRangeState::CloneInto(void *dest) const
{
    auto *d = Destination<cDepthRangeState>(dest);
    d->CopyHeader(*this);
    d->mVTable = VTable();
    d->mMin = mMin;
    d->mMax = mMax;
}

void cFFPerVertexColorState::CloneInto(void *dest) const
{
    auto *d = Destination<cFFPerVertexColorState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mEnabled = mEnabled;
    d->mDiffuseSource = mDiffuseSource;
    d->mSpecularSource = mSpecularSource;
    d->mAmbientSource = mAmbientSource;
    d->mEmissiveSource = mEmissiveSource;
}

void cFFTextureCoordsSourceState::CloneInto(void *dest) const
{
    auto *d = Destination<cFFTextureCoordsSourceState>(dest);
    d->CopyStageHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mSource = mSource;
}

void cFFTextureTransformTypeState::CloneInto(void *dest) const
{
    auto *d = Destination<cFFTextureTransformTypeState>(dest);
    d->CopyStageHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mCount = mCount;
    d->mProjected = mProjected;
}

void cStencilState::CloneInto(void *dest) const
{
    auto *d = Destination<cStencilState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mEnabled = mEnabled;
    d->mReference = mReference;
}

void cTextureAddressingState::CloneInto(void *dest) const
{
    auto *d = Destination<cTextureAddressingState>(dest);
    d->CopySamplerHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mAddressU = mAddressU;
    d->mAddressV = mAddressV;
    d->mAddressW = mAddressW;
}

void cStencilFunctionState::CloneInto(void *dest) const
{
    auto *d = Destination<cStencilFunctionState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mFunction = mFunction;
    d->mReference = mReference;
    d->mMask = mMask;
}

void cTextureFilterState::CloneInto(void *dest) const
{
    auto *d = Destination<cTextureFilterState>(dest);
    d->CopySamplerHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mMinFilter = mMinFilter;
    d->mMagFilter = mMagFilter;
    d->mMaxAnisotropy = mMaxAnisotropy;
}

void cTextureMipFilterState::CloneInto(void *dest) const
{
    auto *d = Destination<cTextureMipFilterState>(dest);
    d->CopySamplerHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mMipFilter = mMipFilter;
    d->mLodBias = mLodBias;
}

void cFFEnabledLights::CloneInto(void *dest) const
{
    auto *d = Destination<cFFEnabledLights>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mCount = mCount;
    d->mLights = mLights;
}

cTextureBlendState *cTextureBlendState::CopyFrom(const cTextureBlendState &other)
{
    CopyStageHeader(other);
    mVTable = GameAddress<void>(kVTable);
    mColorOp = other.mColorOp;
    mColorArg1 = other.mColorArg1;
    mColorArg2 = other.mColorArg2;
    mColorArg0 = other.mColorArg0;
    mAlphaOp = other.mAlphaOp;
    mAlphaArg1 = other.mAlphaArg1;
    mAlphaArg2 = other.mAlphaArg2;
    mAlphaArg0 = other.mAlphaArg0;
    mResultArg = other.mResultArg;
    return this;
}

void cTextureBlendState::CloneInto(void *dest) const
{
    Destination<cTextureBlendState>(dest)->CopyFrom(*this);
}

void cRenderTargetsState::CloneInto(void *dest) const
{
    auto *d = Destination<cRenderTargetsState>(dest);
    d->CopyHeader(*this);
    d->mVTable = VTable();
    d->mTargets = mTargets;
    d->mDepthTarget = mDepthTarget;
}

void cScissorTestState::CloneInto(void *dest) const
{
    auto *d = Destination<cScissorTestState>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mLeft = mLeft;
    d->mTop = mTop;
    d->mRight = mRight;
    d->mBottom = mBottom;
    d->mEnabled = mEnabled;
}

void cViewportState::CloneInto(void *dest) const
{
    auto *d = Destination<cViewportState>(dest);
    d->CopyHeader(*this);
    d->mVTable = VTable();
    d->mX = mX;
    d->mY = mY;
    d->mWidth = mWidth;
    d->mHeight = mHeight;
    d->mMinZ = mMinZ;
    d->mMaxZ = mMaxZ;
}

cFogState *cFogState::CopyFrom(const cFogState &other)
{
    CopyHeader(other);
    mVTable = GameAddress<void>(kVTable);
    mEnabled = other.mEnabled;
    mMode9 = other.mMode9;
    mModeA = other.mModeA;
    mModeB = other.mModeB;
    mColor = other.mColor;
    mStart = other.mStart;
    mEnd = other.mEnd;
    mDensity = other.mDensity;
    return this;
}

void cFogState::CloneInto(void *dest) const
{
    Destination<cFogState>(dest)->CopyFrom(*this);
}

void cFFMaterial::CloneInto(void *dest) const
{
    auto *d = Destination<cFFMaterial>(dest);
    d->CopyHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    d->mDiffuse = mDiffuse;
    d->mAmbient = mAmbient;
    d->mSpecular = mSpecular;
    d->mEmissive = mEmissive;
    d->mPower = mPower;
}

void cFFTextureMatrixState::CloneInto(void *dest) const
{
    auto *d = Destination<cFFTextureMatrixState>(dest);
    d->CopyStageHeader(*this);
    d->mVTable = GameAddress<void>(kVTable);
    for (int i = 0; i < 4; ++i) d->mRows[i] = mRows[i];
}

cFFLight *cFFLight::CopyFrom(const cFFLight &other)
{
    CopyHeader(other);
    mVTable = GameAddress<void>(kVTable);
    mType = other.mType;
    mIndex = other.mIndex;
    mDiffuse = other.mDiffuse;
    mSpecular = other.mSpecular;
    mPosition = other.mPosition;
    mDirection = other.mDirection;
    for (int i = 0; i < 6; ++i) mParams[i] = other.mParams[i];
    return this;
}

void cFFLight::CloneInto(void *dest) const
{
    Destination<cFFLight>(dest)->CopyFrom(*this);
}

} // namespace nGZGraphic4

// The owned vtables (RenderStates.h): what the owned statics stand for in an image-free program, in the image's slot
// order (0 the deleting destructor, 1 CloneInto). The deleting destructors are the instantiations the hooks of the
// folded bodies install (src/hook/tables/RenderStates.cpp: 0x14c620 for 0xc bytes, 0x14c650 for 0x10, 0x186480 for 0x1c,
// 0x14c900 for cViewportState), so each slot is exactly what the hooked game runs. cStateHeader's slot 1 is pure.
namespace {
template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}
} // namespace

extern "C" int __cdecl _purecall();

using namespace nGZGraphic4;

extern "C" const void *ts2_own_vt_d9c594[2] = {
    Member(&cStateHeader::DeletingDestructor<cStateHeader>), // 0 (0x14c870)
    reinterpret_cast<const void *>(&_purecall),              // 1 (the image's _purecall)
};
extern "C" const void *ts2_own_vt_d9c5e8[2] = {
    Member(&cStateHeader::DeletingDestructor<cAlphaBlendState>), // 0 (0x14c620, the 0xc-byte states)
    Member(&cClippingState::CloneInto),                          // 1 (0x14e920)
};
extern "C" const void *ts2_own_vt_d9c5dc[2] = {
    Member(&cStateHeader::DeletingDestructor<cDepthRangeState>), // 0 (0x14c650, the 0x10-byte states)
    Member(&cDepthRangeState::CloneInto),                        // 1 (0x14e9b0)
};
extern "C" const void *ts2_own_vt_d9ff54[2] = {
    Member(&cStateHeader::DeletingDestructor<cRenderTargetsState>), // 0 (0x186480, the 0x1c-byte states)
    Member(&cRenderTargetsState::CloneInto),                        // 1 (0x1864b0)
};
extern "C" const void *ts2_own_vt_d9c5d0[2] = {
    Member(&cStateHeader::DeletingDestructor<cViewportState>), // 0 (0x14c900)
    Member(&cViewportState::CloneInto),                        // 1 (0x14ea10)
};
