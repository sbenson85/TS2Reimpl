// cTSParticlesGeometry (lane WQ). Layout and the hook decision: src/ts/cTSParticlesGeometry.h.
//
// Render (0xbbdd50) is scalar SSE single throughout, except where the original called a function with an x87 result: the
// curve reading 0xbb5620 (the screw radius and angle: fmul by a float, fmul again by the 2 pi float) and Dot87 (the warp
// phase: fadd of a float, fstp as a double; the stretch's two dots). Those sites follow the thread's precision control (24
// bits on the game's device thread, 53 in the harness and on other threads) through Mul87 / Add87 / Dot87.
#include "ts/cTSParticlesGeometry.h"

#include "compgeom/ConvexMath.h"
#include "compgeom/CompGeom.h"
#include "compgeom/Frames.h"
#include "compgeom/Matrix4.h"
#include "compgeom/Rigid.h"
#include "game/game.h"
#include "gz/GraphicServices.h"
#include "gz/RenderStates.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cVertexArrayDX.h"
#include "ts/ParticleCurve.h"
#include "ts/cTSParticlesEffect.h"
#include "ts/cTSParticlesDescription.h"

#include <cmath>
#include <cstring>
#include <new>

using namespace fx;
using nGZCompGeom::convex::Add87;
using nGZCompGeom::convex::Mul87;
using nGZCompGeom::convex::Stored;

namespace {

constexpr uint32_t kUsagePosition = 0x5b830781;
constexpr uint32_t kUsageTexture = 0xbb8307ab;
constexpr uint32_t kUsageColor = 0xdb830795;
constexpr uint32_t kIidWhole = 0x89d39f12;

constexpr float kTwoPi = 6.2831855f;
constexpr float kAlignmentEpsilon = 1e-4f;
constexpr float kSmall = 1e-6f;
constexpr float kToUnit = 1.0f / 255.0f; // 0x3b808081

// Description alignments (the parser's words are not known yet).
constexpr uint8_t kAlignSpeed = 1;      // the emit direction, turned by both transforms, stands in for the velocity
constexpr uint8_t kAlignSpeedLocal = 5; // the same; the axes turned afterwards
constexpr uint8_t kAlignFixedAxis = 6;  // around the global up axis, facing the view

constexpr uint32_t kFlagsHighAspectAfterRotate = 1; // bit 32 of the description's flags

const Vector3 kAxisX{1.0f, 0.0f, 0.0f}; // 0x107cd10
const Vector3 kAxisY{0.0f, 1.0f, 0.0f}; // 0x107cd1c
const Vector3 kAxisZ{0.0f, 0.0f, 1.0f}; // 0x107cd28

float Abs(float a) { return std::fabs(a); }
float Max(float a, float b) { return a > b ? a : b; } // maxss
float Min(float a, float b) { return a < b ? a : b; } // minss

void PutFloat(uint8_t *at, float value) { std::memcpy(at, &value, sizeof value); }

// A colour channel in [0, 1] to a byte: the value in the low bits of 49152 + v (256 steps per unit), clamped.
uint32_t PackChannel(float value)
{
    const float shifted = Add(value, 49152.0f);
    uint32_t bits;
    std::memcpy(&bits, &shifted, 4);
    const int32_t raw = static_cast<int32_t>(bits + 0xb8c00000u);
    return static_cast<uint32_t>((~(raw >> 31) & raw) | ((0xff - raw) >> 31)) & 0xff;
}

Vector3 Scaled(const Vector3 &v, float s) { return {Mul(v.x, s), Mul(v.y, s), Mul(v.z, s)}; }

// One list of colour-sized keys at t (the colours, the pivots). ORIGINAL-BUG: no range check here, unlike the curves: a t
// past 1 or a NaN one (a particle born with no life) reads past the list.
struct KeySpan {
    const fxdesc::Color *mKeys;
    KeyPosition mPosition;
    bool mSingle;
};
KeySpan FindKeys(const std::vector<fxdesc::Color> &keys, float t)
{
    const int32_t last = static_cast<int32_t>(keys.size()) - 1;
    if (last == 0) return {keys.data(), {0, 0.0f}, true};
    return {keys.data(), FindKey(Mul(FromUnsigned(static_cast<uint32_t>(last)), t)), false};
}

// The two axes of the quads, before each particle's own turn.
struct Basis {
    Vector3 mRight;
    Vector3 mUp;
};

Basis AlongDirection(const cTSParticlesEffect *effect, const cTSParticlesDescription *description, uint8_t alignment)
{
    const float half = 0.5f;
    Vector3 direction{Mul(Add(description->m38.x, description->m44.x), half),
                      Mul(Add(description->m38.y, description->m44.y), half),
                      Mul(Add(description->m38.z, description->m44.z), half)};
    if (alignment == kAlignSpeed) {
        effect->mLocal.Rotate(&direction);
        effect->mWorld.Rotate(&direction);
    }
    Basis basis;
    const float length = SqrtF(Add(Add(Mul(direction.y, direction.y), Mul(direction.x, direction.x)), 0.0f));
    if (length > kAlignmentEpsilon) {
        const float y = Div(direction.y, length);
        const float z = Div(0.0f, length);
        const float x = Div(direction.x, length);
        basis.mRight = {Sub(Mul(y, kAxisZ.z), Mul(z, kAxisZ.y)), Sub(Mul(z, kAxisZ.x), Mul(x, kAxisZ.z)),
                        Sub(Mul(x, kAxisZ.y), Mul(y, kAxisZ.x))};
        basis.mUp = {x, y, z};
    } else {
        basis.mRight = kAxisX;
        basis.mUp = kAxisY;
    }
    if (alignment == kAlignSpeedLocal) {
        effect->mLocal.Rotate(&basis.mRight);
        effect->mLocal.Rotate(&basis.mUp);
        effect->mWorld.Rotate(&basis.mRight);
        effect->mWorld.Rotate(&basis.mUp);
    }
    return basis;
}

Basis AroundAxis(const cTSParticlesEffect *effect, const nGZSceneGraph::cGeometryRenderContext *context)
{
    Vector3 axis = kAxisZ;
    effect->mLocal.Rotate(&axis);
    effect->mWorld.Rotate(&axis);
    const float *const view = context->mView.mMatrix;
    const float fx_ = view[1], fy = view[4], fz = view[7];
    const float cx = Sub(Mul(axis.z, fy), Mul(axis.y, fz));
    const float cy = Sub(Mul(axis.x, fz), Mul(axis.z, fx_));
    const float cz = Sub(Mul(axis.y, fx_), Mul(axis.x, fy));
    const float length = Add(SqrtF(Add(Add(Mul(cx, cx), Mul(cy, cy)), Mul(cz, cz))), 0.1f);
    return {{Div(cx, length), Div(cy, length), Div(cz, length)}, axis};
}

// The per-frame reading of the description and the camera.
struct Frame {
    Basis mBasis;
    float mScreen[16];    // view times projection, the blooming particles' screen test
    bool mBloom;
    float mAlphaFloor, mAlphaSlope, mSizeFloor, mSizeSlope;
    bool mTiled;
    float mTileU, mTileV;
    uint32_t mMaskU, mMaskV;
    uint32_t mShiftV;
    float mFrameSpeed;
    uint32_t mFirstFrame, mFrameCount;
    bool mAspectAfterRotate;
    bool mScrewRadiusActive;
};

// The particle's position after the screw: a turn around its own velocity at the curve's radius, or (no radius curve) the
// effect's screw around the centre's z axis; then the warps.
Vector3 Displaced(const cTSParticlesEffect *effect, const cTSParticlesDescription *d, const cTSParticle *p, float t,
                  bool radiusActive)
{
    Vector3 position = p->mPosition;
    if (radiusActive) {
        float radius = Stored(Mul87(EvaluateCurve(d->mScrewRadius, t), p->mScrewRadius));
        if (!(radius == 0.0f)) {
            const float vx = p->mVelocity.x, vy = p->mVelocity.y, vz = p->mVelocity.z;
            const float xx = Mul(vx, vx), yy = Mul(vy, vy), zz = Mul(vz, vz);
            const float speed = SqrtF(Add(Add(xx, yy), zz));
            if (!(kSmall > speed)) {
                const Vector3 centre = position;
                float angle = Stored(Mul87(
                    Mul87(EvaluateCurve(d->mScrewAngle, t), p->mScrewAngle), static_cast<double>(kTwoPi)));
                if (d->mScrewAngleToSpeed == 1) angle = Mul(angle, speed);
                else if (d->mScrewAngleToSpeed == 2) angle = Div(angle, speed);
                if (d->mScrewRadiusToSpeed == 1) radius = Mul(speed, radius);
                else if (d->mScrewRadiusToSpeed == 2) radius = Div(radius, speed);
                const float limit = d->mScrewRadiusMax;
                if (radius > limit) radius = limit;
                else radius = -limit > radius ? -limit : radius;

                Vector3 side{1.0f, 0.0f, 0.0f};
                if (!(vx == 0.0f && vy == 0.0f)) {
                    const float nx = -vx;
                    const float length = SqrtF(Add(Add(Mul(vy, vy), Mul(nx, nx)), 0.0f));
                    side = {Div(vy, length), Div(nx, length), Div(0.0f, length)};
                }
                float offset[3] = {Mul(radius, side.x), Mul(radius, side.y), Mul(radius, side.z)};
                const float inverse = Div(1.0f, speed);
                const float half = Mul(angle, 0.5f);
                const float s = SinF(half);
                const float q[4] = {Mul(Mul(vx, inverse), s), Mul(Mul(vy, inverse), s), Mul(Mul(vz, inverse), s), CosF(half)};
                nGZCompGeom::RotateByQuaternionOrdered(offset, q);
                position = {Add(centre.x, offset[0]), Add(centre.y, offset[1]), Add(centre.z, offset[2])};
            }
        }
    } else if (!(d->mScrew == 0.0f)) {
        const Vector3 &c = effect->mLocal.translation;
        const float dx = Sub(position.x, c.x), dy = Sub(position.y, c.y), dz = Sub(position.z, c.z);
        const float turn = Mul(dz, d->mScrew);
        const float s = SinF(turn), co = CosF(turn);
        position.x = Add(Sub(Mul(dx, co), Mul(dy, s)), c.x);
        position.y = Add(Add(Mul(s, dx), Mul(dy, co)), c.y);
    }

    if (!d->mWarps.empty()) {
        const Vector3 &c = effect->mLocal.translation;
        const float from[3] = {Sub(position.x, c.x), Sub(position.y, c.y), Sub(position.z, c.z)};
        for (const cTSParticleWarp &warp : d->mWarps) {
            const double dot = nGZCompGeom::Dot87(from, &warp.mFirst.x);
            const float phase = Mul(static_cast<float>(effect->mTime), warp.mValue);
            const float s = static_cast<float>(std::sin(Add87(dot, static_cast<double>(phase))));
            position.x = Add(position.x, Mul(warp.mSecond.x, s));
            position.y = Add(position.y, Mul(warp.mSecond.y, s));
            position.z = Add(position.z, Mul(warp.mSecond.z, s));
        }
    }
    return position;
}

// One particle's quad at the current write position.
struct Cursor {
    uint8_t *mPosition;
    uint8_t *mColor;
    uint8_t *mTexture;
    uint32_t mStride;
};

void PutCorner(Cursor &cursor, const Vector3 &corner)
{
    PutFloat(cursor.mPosition, corner.x);
    PutFloat(cursor.mPosition + 4, corner.y);
    PutFloat(cursor.mPosition + 8, corner.z);
    cursor.mPosition += cursor.mStride;
}
void PutTexture(Cursor &cursor, float u, float v)
{
    PutFloat(cursor.mTexture, u);
    PutFloat(cursor.mTexture + 4, v);
    cursor.mTexture += cursor.mStride;
}

void WriteParticle(const cTSParticlesEffect *effect, const cTSParticlesDescription *d, const Frame &frame,
                   const cTSParticle *p, Cursor &cursor)
{
    const float t = Div(p->mAge, p->mLife);
    Vector3 position = Displaced(effect, d, p, t, frame.mScrewRadiusActive);

    const EffectsTransform &world = effect->mWorld;
    if (world.flags & EffectsTransform::kRotated) {
        const Vector3 in = position;
        position = {RowFromY(in.x, in.y, in.z, world.m[0][0], world.m[0][1], world.m[0][2]),
                    RowFromY(in.x, in.y, in.z, world.m[1][0], world.m[1][1], world.m[1][2]),
                    RowFromY(in.x, in.y, in.z, world.m[2][0], world.m[2][1], world.m[2][2])};
    }
    const float scale = world.scale;
    position = {Add(Mul(scale, position.x), world.translation.x), Add(Mul(scale, position.y), world.translation.y),
                Add(Mul(scale, position.z), world.translation.z)};

    float size = Mul(Mul(Mul(EvaluateCurve(d->mSize, t), p->mSize), effect->mSizeScale), scale);

    Vector3 key;
    const KeySpan colors = FindKeys(d->mColors, t);
    const fxdesc::Color &color0 = colors.mKeys[colors.mPosition.mIndex];
    if (!colors.mSingle && colors.mPosition.mFraction > 0.0f) {
        const fxdesc::Color &color1 = colors.mKeys[colors.mPosition.mIndex + 1];
        const float f = colors.mPosition.mFraction;
        key = {Add(Mul(Sub(color1.r, color0.r), f), color0.r), Add(Mul(Sub(color1.g, color0.g), f), color0.g),
               Add(Mul(Sub(color1.b, color0.b), f), color0.b)};
    } else {
        key = {color0.r, color0.g, color0.b};
    }
    const float red = Mul(Mul(p->mColor.x, key.x), effect->mColorScale.x);
    const float green = Mul(Mul(p->mColor.y, key.y), effect->mColorScale.y);
    const float blue = Mul(Mul(p->mColor.z, key.z), effect->mColorScale.z);
    // mAspect (+0xb4) is what scales the alpha, mAlpha (+0x88) the aspect: the serializers' names are swapped.
    float alpha = Mul(Mul(EvaluateCurve(d->mAspect, t), p->mAlpha), effect->mAlphaScale);

    if (frame.mBloom) {
        const float *const m = frame.mScreen;
        const float x = position.x, y = position.y, z = position.z;
        const float w = Add(RowFromY(x, y, z, m[12], m[13], m[14]), m[15]);
        const float inverse = Div(1.0f, w);
        const float screenY = Abs(Mul(Add(RowFromY(x, y, z, m[4], m[5], m[6]), m[7]), inverse));
        const float screenX = Abs(Mul(Add(Add(Add(Mul(m[1], y), Mul(m[0], x)), Mul(m[2], z)), m[3]), inverse));
        const float edge = Sub(1.0f, Max(screenY, screenX));
        if (edge > 0.0f) {
            const float grownSize = Min(Add(Mul(edge, frame.mSizeSlope), frame.mSizeFloor), 1.0f);
            const float grownAlpha = Min(Add(Mul(edge, frame.mAlphaSlope), frame.mAlphaFloor), 1.0f);
            size = Mul(grownSize, size);
            alpha = Mul(alpha, grownAlpha);
        } else {
            size = Mul(size, frame.mSizeFloor);
            alpha = Mul(alpha, frame.mAlphaFloor);
        }
    }

    const uint32_t packed =
        PackChannel(alpha) << 24 | PackChannel(red) << 16 | PackChannel(green) << 8 | PackChannel(blue);

    const Vector3 &right = frame.mBasis.mRight;
    const Vector3 &up = frame.mBasis.mUp;
    Vector3 a = up, b = right;
    const float stretch = d->mStretch;
    if (!(stretch == 0.0f)) {
        const float along = Stored(nGZCompGeom::Dot87(&p->mVelocity.x, &a.x));
        const float across = Stored(nGZCompGeom::Dot87(&p->mVelocity.x, &b.x));
        const float length = SqrtF(Add(Mul(across, across), Mul(along, along)));
        if (length > kSmall) {
            const float inverse = Div(1.0f, length);
            const float co = Mul(inverse, along);
            const float si = Mul(inverse, across);
            a = {Add(Mul(up.x, co), Mul(right.x, si)), Add(Mul(up.y, co), Mul(right.y, si)),
                 Add(Mul(up.z, co), Mul(right.z, si))};
            b = {Sub(Mul(right.x, co), Mul(up.x, si)), Sub(Mul(right.y, co), Mul(up.y, si)),
                 Sub(Mul(right.z, co), Mul(up.z, si))};
            if (length > stretch) a = Scaled(a, Div(length, stretch));
        }
    }
    a = Scaled(a, size);
    b = Scaled(b, size);

    const float rotation = Add(Mul(EvaluateCurve(d->mRotateCurve[0], t), p->mRotation), d->mRotateOffset[0]);
    const float aspect = Mul(EvaluateCurve(d->mAlpha, t), p->mAspect);
    if (!frame.mAspectAfterRotate) b = Scaled(b, aspect);
    if (Abs(rotation) > kSmall) {
        const float radians = Mul(rotation, kTwoPi);
        const float si = SinF(radians), co = CosF(radians);
        const Vector3 oldA = a, oldB = b;
        a = {Sub(Mul(oldA.x, co), Mul(oldB.x, si)), Sub(Mul(oldA.y, co), Mul(oldB.y, si)),
             Sub(Mul(oldA.z, co), Mul(oldB.z, si))};
        b = {Add(Mul(oldB.x, co), Mul(oldA.x, si)), Add(Mul(oldB.y, co), Mul(oldA.y, si)),
             Add(Mul(oldB.z, co), Mul(oldA.z, si))};
    }
    if (frame.mAspectAfterRotate) b = Scaled(b, aspect);

    float pivotX, pivotY;
    const KeySpan pivots = FindKeys(d->mPivots, t);
    const fxdesc::Color &pivot0 = pivots.mKeys[pivots.mPosition.mIndex];
    if (!pivots.mSingle && pivots.mPosition.mFraction > 0.0f) {
        const fxdesc::Color &pivot1 = pivots.mKeys[pivots.mPosition.mIndex + 1];
        const float f = pivots.mPosition.mFraction;
        pivotX = Add(Mul(f, Sub(pivot1.r, pivot0.r)), pivot0.r);
        pivotY = Add(Mul(f, Sub(pivot1.g, pivot0.g)), pivot0.g);
    } else {
        pivotX = pivot0.r;
        pivotY = pivot0.g;
    }
    const float qx = Add(p->mPivot[0], pivotX);
    const float qy = Add(p->mPivot[1], pivotY);

    const Vector3 corner{Sub(Sub(position.x, Mul(b.x, qx)), Mul(a.x, qy)), Sub(Sub(position.y, Mul(b.y, qx)), Mul(a.y, qy)),
                         Sub(Sub(position.z, Mul(b.z, qx)), Mul(a.z, qy))};
    const Vector3 acrossCorner{Add(b.x, corner.x), Add(b.y, corner.y), Add(b.z, corner.z)};
    PutCorner(cursor, {Add(a.x, corner.x), Add(a.y, corner.y), Add(a.z, corner.z)});
    PutCorner(cursor, corner);
    PutCorner(cursor, acrossCorner);
    PutCorner(cursor, {Add(a.x, acrossCorner.x), Add(acrossCorner.y, a.y), Add(acrossCorner.z, a.z)});

    float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
    if (frame.mTiled) {
        const float animated = Add(Mul(frame.mFrameSpeed, p->mAge), static_cast<float>(static_cast<int32_t>(p->mFrameOffset)));
        uint32_t tile = static_cast<uint32_t>(static_cast<int64_t>(animated));
        if (frame.mFrameCount) tile = (tile - frame.mFirstFrame) % frame.mFrameCount + frame.mFirstFrame;
        const uint32_t column = tile & frame.mMaskU;
        const uint32_t row = (tile >> (frame.mShiftV & 31)) & frame.mMaskV;
        u0 = Mul(FromUnsigned(column), frame.mTileU);
        u1 = Add(u0, frame.mTileU);
        v0 = Mul(FromUnsigned(row), frame.mTileV);
        v1 = Add(v0, frame.mTileV);
    }
    PutTexture(cursor, u0, v0);
    PutTexture(cursor, u0, v1);
    PutTexture(cursor, u1, v1);
    PutTexture(cursor, u1, v0);

    for (int corner4 = 0; corner4 != 4; ++corner4) {
        std::memcpy(cursor.mColor, &packed, 4);
        cursor.mColor += cursor.mStride;
    }
}

} // namespace

// 0xbb4e90: the fields, then the vertex format: position, texture coordinate and colour in stream 0. Each use of the
// manager asks the getter.
cTSParticlesGeometry::cTSParticlesGeometry(cTSParticlesEffect *effect)
{
    using namespace nGZGraphic4;
    auto formats = [] { return static_cast<cIVertexFormatsManager *>(GetVertexFormatsManagerCached()); };

    mRefs = 0;
    mFormat = nullptr;
    mDeclaration = nullptr;
    mEffect = effect;
    for (float &bound : mBoundsA) bound = 0.0f;
    mFlags = 1;
    mZeros[0] = mZeros[1] = mZeros[2] = mZeros[3] = 0;

    formats()->BeginFormat();
    formats()->AddElement(0, 2, kUsagePosition, 0);
    formats()->AddElement(0, 1, kUsageTexture, 0);
    formats()->AddElement(0, 5, kUsageColor, 0);
    cIVertexFormatsManager *const manager = formats();
    if (mFormat) {
        cIVertexFormat *const old = mFormat;
        mFormat = nullptr;
        old->Release();
    }
    manager->EndFormat(&mFormat);

    for (int i = 0; i != 6; ++i) mBoundsB[i] = mBoundsA[i];
    mFormatRaw = mFormat;
    mDrawInfo[0] = mDrawInfo[1] = 0;
    mZeros2[0] = mZeros2[1] = mZeros2[2] = 0;
    mByte68 = 1;
}

// 0xbb5490: the declaration, then the format.
cTSParticlesGeometry::~cTSParticlesGeometry()
{
    if (mDeclaration) mDeclaration->Release();
    if (mFormat) mFormat->Release();
}

// folded 0x3af660: the two ids this answers are the interface (1) and the class.
bool cTSParticlesGeometry::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != 1 && iid != kIidWhole) return false;
    *out = this;
    AddRef();
    return true;
}

// folded 0x153f50: the last release deletes through the deleting destructor.
uint32_t cTSParticlesGeometry::Release()
{
    if (--mRefs == 0) {
        delete this;
        return 0;
    }
    return mRefs;
}

// 0xbb9460: a declaration that is missing, or lost with its device, is asked for again.
bool cTSParticlesGeometry::IsSupported(uint32_t)
{
    using namespace nGZGraphic4;
    if (!mDeclaration || !mDeclaration->IsCreated()) {
        auto *const declarations = static_cast<cIVertexDeclarationsManagerDX *>(GetVertexDeclarationsManagerCached());
        if (mDeclaration) {
            cIVertexDeclarationDX *const old = mDeclaration;
            mDeclaration = nullptr;
            old->Release();
        }
        declarations->GetDeclaration(GetCurrentDeviceCached(), mFormat, &mDeclaration);
    }
    return mDeclaration && mDeclaration->IsCreated();
}

// 0xbbdd50: the effect's live particles as quads, as many batches as the arrays' capacity asks. The frame's axes come from
// the description's alignment; each particle then adds its own displacement, size, colour, stretch and turn.
uint32_t cTSParticlesGeometry::Render(uint32_t, cGeometryRenderContext *context, uint8_t flag, uint32_t)
{
    using namespace nGZGraphic4;
    cTSParticlesEffect *const effect = mEffect;
    const cTSParticlesDescription *const d = effect->mDescription;

    Frame frame;
    const uint8_t alignment = d->mAlignment;
    if (alignment == kAlignSpeed || alignment == kAlignSpeedLocal) {
        frame.mBasis = AlongDirection(effect, d, alignment);
    } else if (alignment == kAlignFixedAxis) {
        frame.mBasis = AroundAxis(effect, context);
    } else {
        const float *const view = context->mView.mMatrix;
        frame.mBasis.mRight = {view[0], view[3], view[6]};
        frame.mBasis.mUp = {view[2], view[5], Abs(view[8])};
    }

    frame.mBloom = (d->mBloom[0] | d->mBloom[2]) != 0;
    frame.mAlphaFloor = frame.mAlphaSlope = frame.mSizeFloor = frame.mSizeSlope = 0.0f;
    if (frame.mBloom) {
        float view[16];
        nGZCompGeom::Matrix4Multiply(frame.mScreen, nGZCompGeom::AsFrame(context->mViewInverse).ToMatrix4(view),
                                     context->mProjection);
        frame.mAlphaFloor = Mul(static_cast<float>(static_cast<int32_t>(d->mBloom[1])), kToUnit);
        frame.mAlphaSlope = Mul(Mul(static_cast<float>(static_cast<int32_t>(d->mBloom[0])), kToUnit), 16.0f);
        frame.mSizeFloor = Mul(static_cast<float>(static_cast<int32_t>(d->mBloom[3])), kToUnit);
        frame.mSizeSlope = Mul(Mul(static_cast<float>(static_cast<int32_t>(d->mBloom[2])), kToUnit), 16.0f);
    }

    const int32_t tilesU = d->mTile[0], tilesV = d->mTile[1];
    frame.mTiled = tilesU > 1 || tilesV > 1;
    frame.mTileU = Div(1.0f, static_cast<float>(tilesU));
    frame.mTileV = Div(1.0f, static_cast<float>(tilesV));
    frame.mMaskU = static_cast<uint32_t>(tilesU - 1);
    frame.mMaskV = static_cast<uint32_t>(tilesV - 1);
    frame.mShiftV = 0;
    for (int32_t tiles = tilesU; tiles > 1; tiles >>= 1) ++frame.mShiftV;
    frame.mFrameSpeed = d->mFrameSpeed;
    frame.mFirstFrame = effect->mFirstFrame;
    frame.mFrameCount = d->mFrameCount;

    if (effect->mSettings->mSetsBlend) {
        cAlphaBlendState blend{};
        blend.mVTable = GameAddress<void>(cAlphaBlendState::kVTable);
        blend.mId = 0x11;
        blend.mSize = sizeof blend;
        blend.mEnabled = 1;
        blend.mSource = 1;
        context->mState->ApplyStateCommand(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&blend)));
        cAlphaTestState test{};
        test.mVTable = GameAddress<void>(cAlphaTestState::kVTable);
        test.mId = 0x12;
        test.mSize = sizeof test;
        context->mState->ApplyStateCommand(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&test)));
    }

    const uint32_t positionOffset = mFormat->GetOffsetAny(kUsagePosition, 0);
    const uint32_t colorOffset = mFormat->GetOffsetAny(kUsageColor, 0);
    const uint32_t textureOffset = mFormat->GetOffsetAny(kUsageTexture, 0);
    const uint32_t vertexSize = mFormat->GetStreamSize(0);
    cIDynamicVertexArray *const vertices = flag ? context->mVertices : context->mVerticesB;
    const uint32_t vertexRoom = vertices->GetCapacity() / (vertexSize * 4);
    cIDynamicVertexArray *const indices = flag ? context->mIndices : context->mIndicesB;
    const uint32_t indexRoom = indices->GetCapacity() / 12;
    const uint32_t room = vertexRoom < indexRoom ? vertexRoom : indexRoom;

    frame.mAspectAfterRotate = (static_cast<uint32_t>(d->mFlags >> 32) & kFlagsHighAspectAfterRotate) != 0;
    const cTSEffectCurve &radius = d->mScrewRadius;
    frame.mScrewRadiusActive =
        !(radius.mKind == cTSEffectCurve::kList && radius.mValues.size() == 1 && radius.mValues[0] == 0.0f);

    auto node = effect->mParticles.cbegin();
    uint32_t remaining = static_cast<uint32_t>(effect->mCount);
    while (remaining != 0) {
        // ORIGINAL-BUG: arrays too small for one quad (room 0) would loop here for ever.
        const uint32_t batch = remaining < room ? remaining : room;
        remaining -= batch;

        const uint32_t firstVertex = vertices->Reserve(vertexSize, batch * 4);
        const uint32_t firstIndex = indices->Reserve(2, batch * 6);
        uint8_t *const base = static_cast<uint8_t *>(vertices->Lock(1, 0, 0));
        Cursor cursor{base + positionOffset, base + colorOffset, base + textureOffset, mFormat->GetStreamSize(0)};
        for (int32_t i = 0; i < static_cast<int32_t>(batch); ++i) {
            WriteParticle(effect, d, frame, *node, cursor);
            ++node;
        }
        vertices->Unlock();

        uint16_t *index = static_cast<uint16_t *>(indices->Lock(1, 0, 0));
        for (int32_t i = 0; i < static_cast<int32_t>(batch); ++i) {
            const uint16_t start = static_cast<uint16_t>(i * 4);
            index[0] = start;
            index[1] = static_cast<uint16_t>(start + 1);
            index[2] = static_cast<uint16_t>(start + 2);
            index[3] = start;
            index[4] = static_cast<uint16_t>(start + 2);
            index[5] = static_cast<uint16_t>(start + 3);
            index += 6;
        }
        indices->Unlock();

        const uint32_t stride = mFormat->GetStreamSize(0);
        cIDeviceState *const state = context->mState;
        state->SetStreamSource(0, vertices, stride, 0);
        state->SetIndices(indices);
        state->SetVertexDeclaration(mDeclaration);
        context->mDevice->DrawIndexedPrimitive(3, batch * 2, firstVertex, 0, batch * 4, firstIndex);
    }

    ++effect->mFramesDrawn;
    return 0;
}

// 0xbc10f0.
void cTSParticlesGeometry::SetBounds(const float *bounds)
{
    std::memcpy(mBoundsA, bounds, sizeof mBoundsA);
    std::memcpy(mBoundsB, bounds, sizeof mBoundsB);
}

using Self = cTSParticlesGeometry;
using Adapters = cTSParticlesGeometryAdapters;

Self *__fastcall Adapters::Construct(Self *memory, void *, cTSParticlesEffect *effect)
{
    return ::new (memory) Self(effect);
}

void __fastcall Adapters::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
}

float __fastcall Adapters::EvaluateCurve(const cTSEffectCurve *curve, void *, float t)
{
    return fx::EvaluateCurve(*curve, t);
}

void __fastcall Adapters::Rotate(const fx::EffectsTransform *transform, void *, fx::Vector3 *vector)
{
    [[msvc::forceinline_calls]] transform->Rotate(vector);
}
