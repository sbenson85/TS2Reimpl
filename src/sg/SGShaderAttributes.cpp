// The shader's attribute and apply parts (lane TG, scenegraph M3b): see SGShaderAttributes.h.
#include "sg/SGShaderAttributes.h"

#include "compgeom/SeparatingAxes.h" // FramePlacement::Compose
#include "game/game.h"
#include "game/original.h"
#include "gz/RenderStates.h"
#include "gz/cVector4fEvent.h" // ts2_rva_db07d4
#include "gz/cDeviceStateDX.h"
#include "sg/cStringPool.h" // JenkinsHash

#include <bit>
#include <cmath>
#include <cstring>

namespace nGZSceneGraph {

namespace {
cShaderApplyContext *Context(void *ctx) { return static_cast<cShaderApplyContext *>(ctx); }
cShaderApplyItem *Item(void *item) { return static_cast<cShaderApplyItem *>(item); }

// A render state command built on the stack: the state's game vtable (named at each call, so tools/callers.py sees
// it), its id and its size; the rest zero.
template <class State> State Command(uint8_t id, const void *vtable)
{
    State state{};
    state.mVTable = vtable;
    state.mId = id;
    state.mSize = static_cast<uint16_t>(sizeof(State));
    return state;
}

constexpr uint8_t kColorScalarId = 0x1a;
constexpr uint8_t kTextureMatrixId = 0xf;
constexpr uint8_t kTextureTransformTypeId = 0x10;

void Apply(nGZGraphic4::cIDeviceState *state, const void *command)
{
    state->ApplyStateCommand(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(command)));
}

// floor(x) (for |x| under 2^22): 1.5 * 2^23 added to x - 0.5 rounds it to the nearest even integer in the low mantissa
// bits, taken back up by one when that landed one short (x itself an integer rounded down).
int32_t Floor(float x)
{
    const float shifted = x - 0.5f + 12582912.0f;
    const int32_t whole = static_cast<int32_t>(std::bit_cast<uint32_t>(shifted) - 0x4b400000u);
    return static_cast<float>(whole + 1) == x ? whole + 1 : whole;
}

// x less floor(x).
float Fraction(float x) { return x - static_cast<float>(Floor(x)); }

// v * 256 rounded to an integer, clamped to 0..255 (the 1.5 * 2^15 rounding trick: v + 49152 puts v * 256 in the low
// mantissa bits). The clamp is branch-free on the wrapped 32-bit difference: negatives to 0, and anything whose
// 0xff - i is negative as a 32-bit int to all ones (only the low byte is used). That second test also catches i just
// above INT_MIN (v near -98304), where 0xff - i wraps: those give 0xff, as in the game.
uint32_t AlphaByte(float v)
{
    const uint32_t i = std::bit_cast<uint32_t>(v + 49152.0f) - 0x47400000u;
    const uint32_t keep = (i & 0x80000000u) ? 0u : i;
    const uint32_t high = ((0xffu - i) & 0x80000000u) ? 0xffffffffu : 0u;
    return (keep | high) & 0xffu;
}
} // namespace

float __cdecl AnimationWave(int32_t wave, float rate, float phase, float time)
{
    const float x = rate * time + phase;
    switch (wave) {
    case 0:
        return Fraction(x);
    case 1:
        return std::fabs(Fraction(x) - 0.5f) * 2.0f;
    case 2: {
        constexpr float kTwoPi = 6.2831855f;
        return (static_cast<float>(std::sin(static_cast<double>(x * kTwoPi))) + 1.0f) * 0.5f;
    }
    default:
        return 0.0f;
    }
}

// ---- cFFDepthOffset ----

cIGZUnknown *__cdecl cFFDepthOffset::Create() { return new cFFDepthOffset; }

cFFDepthOffset::~cFFDepthOffset() = default;

void cFFDepthOffset::Apply(void *ctx, void *)
{
    cShaderApplyContext *const c = Context(ctx);
    D3DMATRIX m = c->mProjection;
    constexpr float kScale = 1.0f / 32768.0f;
    m._32 = -static_cast<float>(mOffset) * kScale * m._42 + m._32;
    c->mState->SetProjection(&m);
}

void cFFDepthOffset::EndState(void *ctx)
{
    cShaderApplyContext *const c = Context(ctx);
    c->mState->SetProjection(&c->mProjection);
}

uint32_t cFFDepthOffset::HashParams() { return ShaderHashMix(static_cast<uint32_t>(mOffset)); }

bool cFFDepthOffset::EqualParams(cIFFDepthOffset *other)
{
    return mOffset == static_cast<cFFDepthOffset *>(other)->mOffset;
}

// ---- cFFApplyTransforms ----

cIGZUnknown *__cdecl cFFApplyTransforms::Create() { return new cFFApplyTransforms; }

cFFApplyTransforms::~cFFApplyTransforms() = default;

void cFFApplyTransforms::Apply(void *ctx, void *item)
{
    nGZGraphic4::cIDeviceState *const state = Context(ctx)->mState;
    if (mUseItemTransform) {
        state->SetWorld(static_cast<cShaderApplyItem *>(item)->mTransform);
        return;
    }
    // The identity: the rotation's diagonal (words 1, 5 and 9) one, the rest and the translation zero.
    float identity[13] = {};
    identity[1] = identity[5] = identity[9] = 1.0f;
    state->SetWorld(identity);
}

uint32_t cFFApplyTransforms::HashParams() { return mUseItemTransform; }

bool cFFApplyTransforms::EqualParams(cIFFApplyTransforms *other)
{
    return mUseItemTransform == static_cast<cFFApplyTransforms *>(other)->mUseItemTransform;
}

// ---- cAnimColorScalarAlpha ----

cIGZUnknown *__cdecl cAnimColorScalarAlpha::Create() { return new cAnimColorScalarAlpha; }

// The colour starts as the image's default colour word (0x10f67a0, only this constructor reads it).
cAnimColorScalarAlpha::cAnimColorScalarAlpha()
    : mColor(*GameAddress<uint32_t>(0x10f67a0)), mRate(0.0f), mPhase(0.0f), mFrom(0.0f), mTo(1.0f), mWave(1)
{
}

cAnimColorScalarAlpha::~cAnimColorScalarAlpha() = default;

void cAnimColorScalarAlpha::Apply(void *ctx, void *)
{
    cShaderApplyContext *const c = Context(ctx);
    const float t = AnimationWave(mWave, mRate, mPhase, c->mTime);
    const float alpha = (1.0f - t) * mFrom + t * mTo;
    nGZGraphic4::cColorScalarState state = Command<nGZGraphic4::cColorScalarState>(
        kColorScalarId, GameAddress<void>(nGZGraphic4::cColorScalarState::kVTable));
    state.mColor = (mColor & 0xffffffu) | AlphaByte(alpha) << 24;
    nGZSceneGraph::Apply(c->mState, &state);
}

void cAnimColorScalarAlpha::SetAnimation(float rate, float phase, float from, float to, int32_t wave)
{
    mRate = rate;
    mPhase = phase;
    mFrom = from;
    mTo = to;
    mWave = wave;
}

uint32_t cAnimColorScalarAlpha::HashParams() { return JenkinsHash(&mColor, 0x18, 0); }

bool cAnimColorScalarAlpha::EqualParams(cIAnimColorScalarAlpha *other)
{
    return std::memcmp(&mColor, &static_cast<cAnimColorScalarAlpha *>(other)->mColor, 0x18) == 0;
}

// ---- cAttributeFFMaterial ----

namespace {
constexpr uint8_t kFFMaterialId = 0xc;

// The value at `index` in `values` when index >= 0 and there are values, else `fallback`.
template <class T> const T &ValueAt(const uint8_t *values, int32_t index, const T &fallback)
{
    return index >= 0 && values ? *reinterpret_cast<const T *>(values + index) : fallback;
}

// A material colour into the state's four words: the alpha in word 0, the three values in words 1-3.
void PutColor(nGZGraphic4::Float4 &out, const cMaterialColor &c, const uint8_t *values)
{
    float *const w = &out.r;
    w[0] = ValueAt(values, c.mAlpha.mIndex, c.mAlpha.mValue);
    using Three = float[3];
    const Three &v = ValueAt<Three>(values, c.mColor.mIndex, c.mColor.mValue);
    w[1] = v[0];
    w[2] = v[1];
    w[3] = v[2];
}

nGZGraphic4::cFFMaterial MaterialState(const cAttributeFFMaterial &m, const uint8_t *values)
{
    nGZGraphic4::cFFMaterial state =
        Command<nGZGraphic4::cFFMaterial>(kFFMaterialId, GameAddress<void>(nGZGraphic4::cFFMaterial::kVTable));
    PutColor(state.mDiffuse, m.mDiffuse, values);
    PutColor(state.mAmbient, m.mAmbient, values);
    PutColor(state.mSpecular, m.mSpecular, values);
    PutColor(state.mEmissive, m.mEmissive, values);
    state.mPower = ValueAt(values, m.mPower.mIndex, m.mPower.mValue);
    return state;
}

// The colour event the item's source fills (cVectorEvent<cVector<4, float>> with the game's vtable 0xdb07d4, 0x14
// bytes): four scales, 1 until filled.
struct cColorEvent {
    static constexpr uint32_t kVTable = 0xdb07d4;
    const void *mVTable;
    float mScale[4];
};
static_assert(sizeof(cColorEvent) == 0x14);

void Scale(nGZGraphic4::Float4 &c, const float (&s)[4])
{
    float *const w = &c.r;
    for (int i = 0; i < 4; ++i) w[i] *= s[i];
}
} // namespace

cIGZUnknown *__cdecl cAttributeFFMaterial::Create() { return new cAttributeFFMaterial; }

cAttributeFFMaterial::cAttributeFFMaterial()
    : mDiffuse{{-1, {1.0f, 1.0f, 1.0f}}, {-1, 1.0f}}, mAmbient{{-1, {1.0f, 1.0f, 1.0f}}, {-1, 1.0f}},
      mSpecular{{-1, {0.0f, 0.0f, 0.0f}}, {-1, 1.0f}}, mEmissive{{-1, {0.0f, 0.0f, 0.0f}}, {-1, 1.0f}},
      mPower{-1, 0.0f}, mColorEvent(-1)
{
}

cAttributeFFMaterial::~cAttributeFFMaterial() = default;

bool cAttributeFFMaterial::IsDynamic() const
{
    return mColorEvent >= 0 || mDiffuse.mColor.mIndex >= 0 || mDiffuse.mAlpha.mIndex >= 0 ||
           mAmbient.mColor.mIndex >= 0 || mAmbient.mAlpha.mIndex >= 0 || mSpecular.mColor.mIndex >= 0 ||
           mSpecular.mAlpha.mIndex >= 0 || mEmissive.mColor.mIndex >= 0 || mEmissive.mAlpha.mIndex >= 0 ||
           mPower.mIndex >= 0;
}

void cAttributeFFMaterial::Slot9(void *device, const void *)
{
    if (IsDynamic()) return;
    nGZGraphic4::cFFMaterial state = MaterialState(*this, nullptr);
    nGZSceneGraph::Apply(static_cast<nGZGraphic4::cIDeviceState *>(device), &state);
}

void cAttributeFFMaterial::Apply(void *ctx, void *item)
{
    const cShaderApplyItem *const it = Item(item);
    nGZGraphic4::cFFMaterial state = MaterialState(*this, it->mValues);
    if (mColorEvent >= 0) {
        cColorEvent event{&ts2_rva_db07d4, {1.0f, 1.0f, 1.0f, 1.0f}};
        (*it->mEventSource)->GetEvent(&event, kColorEventType, mColorEvent);
        Scale(state.mDiffuse, event.mScale);
        Scale(state.mAmbient, event.mScale);
    }
    nGZSceneGraph::Apply(Context(ctx)->mState, &state);
}

void cAttributeFFMaterial::SetDiffuse(const cIndexedFloat3 *color, const cIndexedFloat *alpha)
{
    mDiffuse.mColor = *color;
    mDiffuse.mAlpha = *alpha;
}
void cAttributeFFMaterial::SetDiffuseAlpha(const cIndexedFloat *alpha) { mDiffuse.mAlpha = *alpha; }
void cAttributeFFMaterial::SetAmbient(const cIndexedFloat3 *color, const cIndexedFloat *alpha)
{
    mAmbient.mColor = *color;
    mAmbient.mAlpha = *alpha;
}
void cAttributeFFMaterial::SetAmbientAlpha(const cIndexedFloat *alpha) { mAmbient.mAlpha = *alpha; }
void cAttributeFFMaterial::SetSpecular(const cIndexedFloat3 *color, const cIndexedFloat *alpha)
{
    mSpecular.mColor = *color;
    mSpecular.mAlpha = *alpha;
}
void cAttributeFFMaterial::SetSpecularAlpha(const cIndexedFloat *alpha) { mSpecular.mAlpha = *alpha; }
void cAttributeFFMaterial::SetEmissive(const cIndexedFloat3 *color, const cIndexedFloat *alpha)
{
    mEmissive.mColor = *color;
    mEmissive.mAlpha = *alpha;
}
void cAttributeFFMaterial::SetEmissiveAlpha(const cIndexedFloat *alpha) { mEmissive.mAlpha = *alpha; }

uint32_t cAttributeFFMaterial::HashParams() { return JenkinsHash(&mDiffuse, 0x6c, 0); }

bool cAttributeFFMaterial::EqualParams(cIAttributeFFMaterial *other)
{
    return std::memcmp(&mDiffuse, &static_cast<cAttributeFFMaterial *>(other)->mDiffuse, 0x6c) == 0;
}

// ---- cAttributeColorScalar ----

namespace {
// (alpha, r, g, b) as the colour scalar word 0xaarrggbb.
uint32_t ColorScalarWord(const float (&v)[4])
{
    return AlphaByte(v[0]) << 24 | AlphaByte(v[1]) << 16 | AlphaByte(v[2]) << 8 | AlphaByte(v[3]);
}

void ApplyColorScalar(nGZGraphic4::cIDeviceState *state, const float (&v)[4])
{
    nGZGraphic4::cColorScalarState command = Command<nGZGraphic4::cColorScalarState>(
        kColorScalarId, GameAddress<void>(nGZGraphic4::cColorScalarState::kVTable));
    command.mColor = ColorScalarWord(v);
    nGZSceneGraph::Apply(state, &command);
}
} // namespace

cIGZUnknown *__cdecl cAttributeColorScalar::Create() { return new cAttributeColorScalar; }

cAttributeColorScalar::cAttributeColorScalar()
    : mColor{-1, {0.0f, 0.0f, 0.0f}}, mAlpha{-1, 0.0f}, mColorEvent(-1), mModulatorIndex(-1), mModulatorPosition(0)
{
}

cAttributeColorScalar::~cAttributeColorScalar() = default;

void cAttributeColorScalar::Slot9(void *device, const void *)
{
    if (mColor.mIndex >= 0 || mAlpha.mIndex >= 0) return;
    const float v[4] = {mAlpha.mValue, mColor.mValue[0], mColor.mValue[1], mColor.mValue[2]};
    ApplyColorScalar(static_cast<nGZGraphic4::cIDeviceState *>(device), v);
}

uint32_t cAttributeColorScalar::Slot10()
{
    return mColorEvent >= 0 || mModulatorIndex >= 0 || mColor.mIndex >= 0 || mAlpha.mIndex >= 0;
}

void cAttributeColorScalar::Apply(void *ctx, void *item)
{
    const cShaderApplyItem *const it = Item(item);
    const float *const rgb = it->Value<float[3]>(mColor.mIndex, mColor.mValue);
    float v[4] = {it->Value<float>(mAlpha.mIndex, mAlpha.mValue), rgb[0], rgb[1], rgb[2]};
    if (mColorEvent >= 0) {
        cColorEvent event{&ts2_rva_db07d4, {1.0f, 1.0f, 1.0f, 1.0f}};
        (*it->mEventSource)->GetEvent(&event, kColorEventType, mColorEvent);
        for (int i = 0; i < 4; ++i) v[i] *= event.mScale[i];
    }
    if (mModulatorIndex >= 0) {
        const uint32_t first = it->mModulatorRange >> 12 & 0xf;
        const uint32_t count = (it->mModulatorRange >> 16 & 0xf) - first;
        if (mModulatorPosition < count) {
            cIColorModulator *const *const holder = it->mModulators[first + mModulatorPosition];
            if (holder) {
                const float so_far[4] = {v[0], v[1], v[2], v[3]};
                const float *const scale = (*holder)->GetColor(v, mModulatorIndex);
                for (int i = 0; i < 4; ++i) v[i] = scale[i] * so_far[i];
            }
        }
    }
    ApplyColorScalar(Context(ctx)->mState, v);
}

void cAttributeColorScalar::SetColor(const cIndexedFloat3 *color, const cIndexedFloat *alpha)
{
    mColor = *color;
    mAlpha = *alpha;
}

void cAttributeColorScalar::SetModulator(uint32_t position, int32_t index)
{
    mModulatorPosition = position;
    mModulatorIndex = index;
}

uint32_t cAttributeColorScalar::HashParams() { return JenkinsHash(&mColor, 0x24, 0); }

bool cAttributeColorScalar::EqualParams(cIAttributeColorScalar *other)
{
    return std::memcmp(&mColor, &static_cast<cAttributeColorScalar *>(other)->mColor, 0x24) == 0;
}

// ---- cAnimatedTiledTexture ----

cIGZUnknown *__cdecl cAnimatedTiledTexture::Create() { return new cAnimatedTiledTexture; }

cAnimatedTiledTexture::~cAnimatedTiledTexture() = default;

void cAnimatedTiledTexture::ApplyFrame(nGZGraphic4::cIDeviceState *state, float time) const
{
    const uint32_t columns = static_cast<uint32_t>(mColumns);
    const float width = 1.0f / static_cast<float>(mColumns);
    const float height = 1.0f / static_cast<float>(mRows);
    const uint32_t tile = static_cast<uint32_t>(Floor(time * mRate)) % (columns * static_cast<uint32_t>(mRows));
    const float u = static_cast<float>(tile % columns) / static_cast<float>(mColumns);
    const float v = static_cast<float>(tile / columns) / static_cast<float>(mRows);
    const nGZGraphic4::Float4 rowU{width, 0.0f, u, 0.0f}, rowV{0.0f, height, v, 0.0f};
    float *constants = nullptr;
    switch (mTarget) {
    case kStage: {
        auto type = Command<nGZGraphic4::cFFTextureTransformTypeState>(
            kTextureTransformTypeId, GameAddress<void>(nGZGraphic4::cFFTextureTransformTypeState::kVTable));
        type.mStage = mIndex;
        type.mCount = 2;
        nGZSceneGraph::Apply(state, &type);
        auto matrix = Command<nGZGraphic4::cFFTextureMatrixState>(
            kTextureMatrixId, GameAddress<void>(nGZGraphic4::cFFTextureMatrixState::kVTable));
        matrix.mStage = mIndex;
        matrix.mRows[0] = rowU;
        matrix.mRows[1] = rowV;
        matrix.mRows[3].a = 1.0f;
        nGZSceneGraph::Apply(state, &matrix);
        return;
    }
    case kVertexConstants: constants = state->LockVertexFloatConstants(mIndex, 2); break;
    case kPixelConstants: constants = state->LockPixelFloatConstants(mIndex, 2); break;
    default: return;
    }
    std::memcpy(constants, &rowU, sizeof rowU);
    std::memcpy(constants + 4, &rowV, sizeof rowV);
}

void cAnimatedTiledTexture::Apply(void *ctx, void *)
{
    const cShaderApplyContext *const c = Context(ctx);
    ApplyFrame(c->mState, c->mTime);
}

void cAnimatedTiledTexture::Slot12(void *device, const void *)
{
    ApplyFrame(static_cast<nGZGraphic4::cIDeviceState *>(device), 10.0f);
}

void cAnimatedTiledTexture::SetTiles(int32_t columns, int32_t rows)
{
    mColumns = columns;
    mRows = rows;
}

uint32_t cAnimatedTiledTexture::HashParams() { return JenkinsHash(&mTarget, 0x14, 0); }

bool cAnimatedTiledTexture::EqualParams(cIAnimatedTiledTexture *other)
{
    return std::memcmp(&mTarget, &static_cast<cAnimatedTiledTexture *>(other)->mTarget, 0x14) == 0;
}

// ---- cAnimatedTextureMatrix ----

cIGZUnknown *__cdecl cAnimatedTextureMatrix::Create() { return new cAnimatedTextureMatrix; }

cAnimatedTextureMatrix::~cAnimatedTextureMatrix() = default;

void cAnimatedTextureMatrix::ApplyFrame(nGZGraphic4::cIDeviceState *state, float time) const
{
    constexpr float kTwoPi = 6.2831855f;
    nGZCompGeom::Matrix3x3 matrix = {{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
    if (mTranslationRate != 0.0f) {
        const float t = AnimationWave(mTranslationWave, mTranslationRate, mTranslationPhase, time);
        const float s = 1.0f - t;
        matrix.m[2] = mTranslationFrom[0] * s + mTranslationTo[0] * t;
        matrix.m[5] = mTranslationFrom[1] * s + mTranslationTo[1] * t;
    }
    if (mRotationRate != 0.0f) {
        const float t = AnimationWave(mRotationWave, mRotationRate, mRotationPhase, time);
        const float s = 1.0f - t;
        const float angle = (mRotationFrom * s + mRotationTo * t) * kTwoPi;
        const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
        const float n = static_cast<float>(std::sin(static_cast<double>(angle)));
        // The turn about the centre.
        const nGZCompGeom::Matrix3x3 rotation = {{c, -n, mCenter[1] * n + (1.0f - c) * mCenter[0], n, c,
                                                  mCenter[1] * (1.0f - c) - mCenter[0] * n, 0.0f, 0.0f, 1.0f}};
        matrix.MultiplyBy(&rotation);
    }
    if (mScaleRate != 0.0f) {
        const float t = AnimationWave(mScaleWave, mScaleRate, mScalePhase, time);
        const float s = 1.0f - t;
        const float x = mScaleFrom[0] * s + mScaleTo[0] * t;
        const float y = mScaleFrom[1] * s + mScaleTo[1] * t;
        // The scale about the centre.
        const nGZCompGeom::Matrix3x3 scale = {
            {x, 0.0f, (1.0f - x) * mCenter[0], 0.0f, y, (1.0f - y) * mCenter[1], 0.0f, 0.0f, 1.0f}};
        matrix.MultiplyBy(&scale);
    }
    const float *const m = matrix.m;
    const nGZGraphic4::Float4 rowU{m[0], m[1], m[2], 0.0f}, rowV{m[3], m[4], m[5], 0.0f};
    float *constants = nullptr;
    switch (mTarget) {
    case kStage: {
        auto type = Command<nGZGraphic4::cFFTextureTransformTypeState>(
            kTextureTransformTypeId, GameAddress<void>(nGZGraphic4::cFFTextureTransformTypeState::kVTable));
        type.mStage = mIndex;
        type.mCount = 2;
        nGZSceneGraph::Apply(state, &type);
        auto command = Command<nGZGraphic4::cFFTextureMatrixState>(
            kTextureMatrixId, GameAddress<void>(nGZGraphic4::cFFTextureMatrixState::kVTable));
        command.mStage = mIndex;
        command.mRows[0] = rowU;
        command.mRows[1] = rowV;
        command.mRows[2] = {m[6], m[7], m[8], 0.0f};
        command.mRows[3].a = 1.0f;
        nGZSceneGraph::Apply(state, &command);
        return;
    }
    case kVertexConstants: constants = state->LockVertexFloatConstants(mIndex, 2); break;
    case kPixelConstants: constants = state->LockPixelFloatConstants(mIndex, 2); break;
    default: return;
    }
    std::memcpy(constants, &rowU, sizeof rowU);
    std::memcpy(constants + 4, &rowV, sizeof rowV);
}

void cAnimatedTextureMatrix::Apply(void *ctx, void *)
{
    const cShaderApplyContext *const c = Context(ctx);
    ApplyFrame(c->mState, c->mTime);
}

void cAnimatedTextureMatrix::Slot12(void *device, const void *)
{
    ApplyFrame(static_cast<nGZGraphic4::cIDeviceState *>(device), 10.0f);
}

void cAnimatedTextureMatrix::SetRotation(float rate, float phase, float from, float to, int32_t wave)
{
    mRotationRate = rate;
    mRotationPhase = phase;
    mRotationFrom = from;
    mRotationTo = to;
    mRotationWave = wave;
}

void cAnimatedTextureMatrix::SetTranslation(float rate, float phase, const float *from, const float *to, int32_t wave)
{
    mTranslationRate = rate;
    mTranslationPhase = phase;
    mTranslationFrom[0] = from[0];
    mTranslationFrom[1] = from[1];
    mTranslationTo[0] = to[0];
    mTranslationTo[1] = to[1];
    mTranslationWave = wave;
}

void cAnimatedTextureMatrix::SetScale(float rate, float phase, const float *from, const float *to, int32_t wave)
{
    mScaleRate = rate;
    mScalePhase = phase;
    mScaleFrom[0] = 1.0f / from[0];
    mScaleFrom[1] = 1.0f / from[1];
    mScaleTo[0] = 1.0f / to[0];
    mScaleTo[1] = 1.0f / to[1];
    mScaleWave = wave;
}

void cAnimatedTextureMatrix::SetCenter(const float *center)
{
    mCenter[0] = center[0];
    mCenter[1] = center[1];
}

uint32_t cAnimatedTextureMatrix::HashParams() { return JenkinsHash(&mTarget, 0x5c, 0); }

bool cAnimatedTextureMatrix::EqualParams(cIAnimatedTextureMatrix *other)
{
    return std::memcmp(&mTarget, &static_cast<cAnimatedTextureMatrix *>(other)->mTarget, 0x5c) == 0;
}

// ---- cAttributeFFScaleModelToViewTransform ----

cIGZUnknown *__cdecl cAttributeFFScaleModelToViewTransform::Create()
{
    return new cAttributeFFScaleModelToViewTransform;
}

cAttributeFFScaleModelToViewTransform::~cAttributeFFScaleModelToViewTransform() = default;

void cAttributeFFScaleModelToViewTransform::Apply(void *ctx, void *item)
{
    cShaderApplyItem *const it = Item(item);
    const float s = it->Value<float>(mScale.mIndex, mScale.mValue);
    nGZCompGeom::FramePlacement frame{};
    frame.mMatrix[0] = frame.mMatrix[4] = frame.mMatrix[8] = 1.0f;
    using SetScale = void(__thiscall *)(nGZCompGeom::FramePlacement *, float, float, float);
    OriginalFunction<SetScale, 0x19d1f0>()(&frame, s, s, s);
    frame.Compose(static_cast<const nGZCompGeom::FramePlacement *>(it->mTransform));
    Context(ctx)->mState->SetWorld(&frame);
}

uint32_t cAttributeFFScaleModelToViewTransform::HashParams() { return JenkinsHash(&mScale, 8, 0); }

bool cAttributeFFScaleModelToViewTransform::EqualParams(cIAttributeFFScaleModelToViewTransform *other)
{
    const cIndexedFloat &o = static_cast<cAttributeFFScaleModelToViewTransform *>(other)->mScale;
    return std::memcmp(&mScale, &o, sizeof o) == 0;
}

} // namespace nGZSceneGraph
