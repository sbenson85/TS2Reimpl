// nGZSceneGraph::cAttributeFFTextureMatrix (lane TK, scenegraph M3b part 3): see cAttributeFFTextureMatrix.h.
#include "sg/cAttributeFFTextureMatrix.h"

#include "compgeom/Frames.h"         // InverseMatrix3
#include "compgeom/Matrix4.h"        // FrameMath::ToMatrix4
#include "compgeom/Rigid.h"          // QuaternionToMatrix
#include "compgeom/SeparatingAxes.h" // MatrixProduct, FramePlacement::Invert
#include "game/game.h"
#include "game/original.h"
#include "gz/RenderStates.h"
#include "gz/cDeviceStateDX.h"
#include "sg/cStringPool.h" // JenkinsHash

#include <cmath>
#include <cstring>

namespace nGZSceneGraph {

namespace {
using nGZGraphic4::cFFTextureMatrixState;
using nGZGraphic4::cFFTextureTransformTypeState;

constexpr uint8_t kTextureMatrixId = 0xf;
constexpr uint8_t kTextureTransformTypeId = 0x10;

// The texture plane's normal, the 2D turn's axis and source 3's up (the image's 0x106f428).
constexpr float kUp[3] = {0.0f, 0.0f, 1.0f};
constexpr float kIdentity3[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

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

// The stage's matrix command, the identity.
cFFTextureMatrixState MatrixCommand(uint32_t stage)
{
    auto command = Command<cFFTextureMatrixState>(kTextureMatrixId, GameAddress<void>(cFFTextureMatrixState::kVTable));
    command.mStage = stage;
    command.mRows[0].r = 1.0f;
    command.mRows[1].g = 1.0f;
    command.mRows[2].b = 1.0f;
    command.mRows[3].a = 1.0f;
    return command;
}

// The stage's transform type command (no coordinates transformed).
cFFTextureTransformTypeState TypeCommand(uint32_t stage)
{
    auto command = Command<cFFTextureTransformTypeState>(kTextureTransformTypeId,
                                                         GameAddress<void>(cFFTextureTransformTypeState::kVTable));
    command.mStage = stage;
    return command;
}

void Send(nGZGraphic4::cIDeviceState *state, const void *command)
{
    state->ApplyStateCommand(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(command)));
}

float *Rows(cFFTextureMatrixState &command) { return &command.mRows[0].r; }

// The upper 3x3 of the rows from m (the rest kept).
void SetUpper(float *rows, const float *m)
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) rows[4 * i + j] = m[3 * i + j];
}

// 1 / |v| (the length in double precision, rounded to a float first).
float InverseLength(float x, float y, float z)
{
    const float lengthSq = (x * x + y * y) + z * z;
    return 1.0f / static_cast<float>(std::sqrt(static_cast<double>(lengthSq)));
}

// The rows times M (the product after a swizzle, all four columns): an axis turn's rows 0 to 2 from rows 0 to 2; the 2D
// transform's rows 0 and 1 from rows 0, 1 and 3 (the plane's homogeneous row).
void MultiplyRows(float *rows, const float *m, bool axis)
{
    float r[16];
    std::memcpy(r, rows, sizeof r);
    const float *const third = axis ? r + 8 : r + 12;
    const int count = axis ? 3 : 2;
    for (int i = 0; i < count; ++i)
        for (int j = 0; j < 4; ++j)
            rows[4 * i + j] = (m[3 * i] * r[j] + m[3 * i + 1] * r[4 + j]) + m[3 * i + 2] * third[j];
}

// The turn by `turns` about the unit axis (x, y, z) times inv, as a quaternion, then its matrix.
void TurnMatrix(float *out, float turns, float x, float y, float z, float inv)
{
    constexpr float kTwoPi = 6.2831855f;
    const float half = turns * kTwoPi * 0.5f;
    const float s = static_cast<float>(std::sin(static_cast<double>(half)));
    float q[4];
    q[0] = x * inv * s;
    q[1] = y * inv * s;
    q[2] = z * inv * s;
    q[3] = static_cast<float>(std::cos(static_cast<double>(half)));
    nGZCompGeom::QuaternionToMatrix(out, q);
}
} // namespace

bool __cdecl TextureMatrixDiffers(const float *a, const float *b)
{
    for (int i = 0; i < 9; ++i) {
        uint32_t x, y;
        std::memcpy(&x, a + i, 4);
        std::memcpy(&y, b + i, 4);
        const uint32_t sx = x >> 31, sy = y >> 31;
        // A negative float's magnitude bits flipped: the bits then order as two's complement (off by the sign bit).
        const uint32_t ox = x ^ (sx ? 0x7fffffffu : 0u), oy = y ^ (sy ? 0x7fffffffu : 0u);
        const uint32_t d = ox - oy - sy + sx;
        if (((static_cast<uint32_t>(static_cast<int32_t>(d) >> 26)) ^ d) & 0xffffffe0u) return true;
    }
    return false;
}

void __cdecl SwizzleTextureRows(float *rows, uint16_t swizzle)
{
    float copy[16];
    std::memcpy(copy, rows, sizeof copy);
    for (int i = 0; i < 4; ++i) {
        const unsigned from = (swizzle >> (12 - 4 * i)) & 0xf;
        if (from < 4)
            std::memcpy(rows + 4 * i, copy + 4 * from, 16);
        else
            std::memset(rows + 4 * i, 0, 16);
    }
}

cIGZUnknown *__cdecl cAttributeFFTextureMatrix::Create() { return new cAttributeFFTextureMatrix; }

cAttributeFFTextureMatrix::~cAttributeFFTextureMatrix() = default;

bool cAttributeFFTextureMatrix::IsDynamic() const
{
    return mAngle.mIndex >= 0 || mAxis.mIndex >= 0 || mScale.mIndex >= 0 || mScaleUV.mIndex >= 0 ||
           mTranslation.mIndex >= 0 || mCenter.mIndex >= 0;
}

void cAttributeFFTextureMatrix::BuildMatrix(float *out, const uint8_t *values) const
{
    // A parameter's value: the item's when indexed and the item has values, else the stored one.
    const auto read = [values](int32_t index, const float *stored) {
        return index >= 0 && values ? reinterpret_cast<const float *>(values + index) : stored;
    };
    float m[9];
    if (!mAxisRotation) {
        const float k = *read(mScale.mIndex, &mScale.mValue);
        const float *const uv = read(mScaleUV.mIndex, mScaleUV.mValue);
        const float scale[9] = {uv[0] * k, 0.0f, 0.0f, 0.0f, uv[1] * k, 0.0f, 0.0f, 0.0f, 1.0f};
        const float angle = *read(mAngle.mIndex, &mAngle.mValue);
        float turn[9];
        TurnMatrix(turn, angle, kUp[0], kUp[1], kUp[2], InverseLength(kUp[0], kUp[1], kUp[2]));
        const float *const t = read(mTranslation.mIndex, mTranslation.mValue);
        const float translation[9] = {1.0f, 0.0f, t[0], 0.0f, 1.0f, t[1], 0.0f, 0.0f, 1.0f};
        const float *const c = read(mCenter.mIndex, mCenter.mValue);
        const float toCenter[9] = {1.0f, 0.0f, -c[0], 0.0f, 1.0f, -c[1], 0.0f, 0.0f, 1.0f};
        const float fromCenter[9] = {1.0f, 0.0f, c[0], 0.0f, 1.0f, c[1], 0.0f, 0.0f, 1.0f};
        float a[9], b[9], d[9];
        nGZCompGeom::MatrixProduct(a, toCenter, scale);
        nGZCompGeom::MatrixProduct(b, a, turn);
        nGZCompGeom::MatrixProduct(d, b, translation);
        nGZCompGeom::MatrixProduct(m, d, fromCenter);
    } else {
        const float angle = *read(mAngle.mIndex, &mAngle.mValue);
        const float *const axis = read(mAxis.mIndex, mAxis.mValue);
        const float x = axis[0], y = axis[1], z = axis[2];
        TurnMatrix(m, angle, x, y, z, InverseLength(x, y, z));
        const float k = *read(mScale.mIndex, &mScale.mValue);
        for (float &e : m) e *= k;
    }
    if (mInvert) {
        float inverse[9];
        nGZCompGeom::InverseMatrix3(inverse, m);
        std::memcpy(m, inverse, sizeof m);
    }
    std::memcpy(out, m, sizeof m);
}

void cAttributeFFTextureMatrix::Slot9(void *device, const void *)
{
    if (mSource != kSourceMatrix || IsDynamic()) return;
    float m[9];
    BuildMatrix(m, nullptr);
    if (!TextureMatrixDiffers(m, kIdentity3) && mSwizzle == kNoSwizzle) return;
    auto *const state = static_cast<nGZGraphic4::cIDeviceState *>(device);
    auto matrix = MatrixCommand(mStage);
    SetUpper(Rows(matrix), m);
    if (mSwizzle != kNoSwizzle) SwizzleTextureRows(Rows(matrix), mSwizzle);
    Send(state, &matrix);
    auto type = TypeCommand(mStage);
    type.mCount = 2;
    Send(state, &type);
}

uint32_t cAttributeFFTextureMatrix::Slot10()
{
    if (IsDynamic()) {
        mCacheValid = false;
        return 1;
    }
    if (mSource == kSourceMatrix) return 0;
    mCacheValid = true;
    BuildMatrix(mCache, nullptr);
    return 1;
}

void cAttributeFFTextureMatrix::Apply(void *ctx, void *itemPtr)
{
    const cShaderApplyContext *const c = static_cast<cShaderApplyContext *>(ctx);
    const cShaderApplyItem *const item = static_cast<cShaderApplyItem *>(itemPtr);
    auto matrix = MatrixCommand(mStage);
    auto type = TypeCommand(mStage);
    float m[9];
    if (mCacheValid)
        std::memcpy(m, mCache, sizeof m);
    else
        BuildMatrix(m, item->mValues);
    float *const rows = Rows(matrix);
    // The swizzle, then M when a transform parameter was set.
    const auto finish = [&] {
        if (mSwizzle != kNoSwizzle) SwizzleTextureRows(rows, mSwizzle);
        if (mHasTransform) MultiplyRows(rows, m, mAxisRotation);
    };
    float product[9];
    switch (mSource) {
    case kSourceMatrix:
        SetUpper(rows, m);
        type.mCount = 2;
        break;
    case kSourceFrame: {
        float frame[16] = {};
        const float *const f = static_cast<const nGZCompGeom::FrameMath *>(
                                   reinterpret_cast<const AffineTransform3 *>(c->mFrame))->ToMatrix4(frame);
        std::memcpy(rows, f, sizeof frame);
        finish();
        type.mCount = 3;
        break;
    }
    case kSourceFrameMatrix: {
        const float *source = c->mFrame + 1;
        if (mHasTransform) source = nGZCompGeom::MatrixProduct(product, source, m);
        SetUpper(rows, source);
        type.mCount = 3;
        break;
    }
    case kSourceDirection: {
        const float *const offset = static_cast<const float *>(item->mTransform) + 10;
        const float dx = offset[0], dy = offset[1], dz = offset[2];
        const float inv = InverseLength(dx, dy, dz);
        const float nx = dx * inv, ny = dy * inv, nz = dz * inv;
        // Row 0 the direction across the up axis, row 1 the direction, row 2 row 0 across the direction.
        const float ax = ny * kUp[2] - nz * kUp[1];
        const float ay = nz * kUp[0] - nx * kUp[2];
        const float az = nx * kUp[1] - ny * kUp[0];
        rows[0] = ax;
        rows[1] = ay;
        rows[2] = az;
        rows[4] = nx;
        rows[5] = ny;
        rows[6] = nz;
        rows[8] = ay * nz - az * ny;
        rows[9] = az * nx - ax * nz;
        rows[10] = ax * ny - ay * nx;
        finish();
        type.mCount = 3;
        break;
    }
    case kSourceProjection:
        std::memcpy(rows, &c->mProjection, 64);
        finish();
        type.mCount = 4;
        type.mProjected = 1;
        break;
    case kSourceItemInverse: {
        nGZCompGeom::FramePlacement inverse;
        std::memcpy(&inverse, item->mTransform, sizeof inverse);
        inverse.Invert();
        const float *source = inverse.mMatrix;
        if (mHasTransform) source = nGZCompGeom::MatrixProduct(product, inverse.mMatrix, m);
        SetUpper(rows, source);
        type.mCount = 3;
        break;
    }
    default:
        break;
    }
    Send(c->mState, &type);
    Send(c->mState, &matrix);
}

void cAttributeFFTextureMatrix::Slot12(void *device, const void *)
{
    auto *const state = static_cast<nGZGraphic4::cIDeviceState *>(device);
    auto matrix = MatrixCommand(mStage);
    auto type = TypeCommand(mStage);
    float *const rows = Rows(matrix);
    switch (mSource) {
    case kSourceMatrix: type.mCount = 2; break;
    case kSourceFrame:
    case kSourceFrameMatrix:
    case kSourceDirection: {
        constexpr float kHalfPi = 1.5707964f, kPi = 3.1415927f, kQuarterPi = 0.7853982f;
        float euler[9];
        const float *const e = nGZCompGeom::EulerAnglesToMatrix3(euler, kHalfPi, kPi, kQuarterPi); // 0x1a5660
        SetUpper(rows, e);
        type.mCount = 3;
        break;
    }
    case kSourceProjection: {
        // u from x, v from z, the depth from y less a tenth, projected by y.
        const float fixed[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                 0.0f, 1.0f, 0.0f, -0.1f, 0.0f, 1.0f, 0.0f, 0.0f};
        std::memcpy(rows, fixed, sizeof fixed);
        type.mCount = 4;
        type.mProjected = 1;
        break;
    }
    default: break;
    }
    Send(state, &type);
    Send(state, &matrix);
}

void cAttributeFFTextureMatrix::SetSource(uint16_t source) { mSource = source; }

void cAttributeFFTextureMatrix::SetSwizzle(uint16_t swizzle) { mSwizzle = swizzle; }

void cAttributeFFTextureMatrix::SetRotation(const cIndexedFloat *angle)
{
    mAngle = *angle;
    mHasTransform = true;
}

void cAttributeFFTextureMatrix::SetAxisRotation(const cIndexedFloat *angle, const cIndexedFloat3 *axis)
{
    mAngle = *angle;
    mAxis = *axis;
    mHasTransform = true;
    mAxisRotation = true;
}

void cAttributeFFTextureMatrix::SetScale(const cIndexedFloat *scale)
{
    mScale = *scale;
    mHasTransform = true;
}

void cAttributeFFTextureMatrix::SetScaleUV(const cIndexedFloat2 *scale)
{
    mScaleUV = *scale;
    mHasTransform = true;
}

void cAttributeFFTextureMatrix::SetTranslation(const cIndexedFloat2 *offset)
{
    mTranslation = *offset;
    mHasTransform = true;
}

void cAttributeFFTextureMatrix::SetCenter(const cIndexedFloat2 *center)
{
    mCenter = *center;
    mHasTransform = true;
}

void cAttributeFFTextureMatrix::SetInvert(bool on)
{
    mInvert = on;
    mHasTransform = true;
}

uint32_t cAttributeFFTextureMatrix::HashParams() { return JenkinsHash(&mStage, 0x50, 0); }

bool cAttributeFFTextureMatrix::EqualParams(cIAttributeFFTextureMatrix *other)
{
    return std::memcmp(&mStage, &static_cast<cAttributeFFTextureMatrix *>(other)->mStage, 0x50) == 0;
}

} // namespace nGZSceneGraph
