#include "ts/cTSDecalEffect.h"

#include "compgeom/ConvexMath.h"
#include "compgeom/SeparatingAxes.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZRandom.h"
#include "ts/ParticleCurve.h"
#include "ts/cTSCompDescription.h"

#include <bit>
#include <cmath>
#include <new>

using nGZCompGeom::convex::Add87;
using nGZCompGeom::convex::BitsOf;
using nGZCompGeom::convex::Mul87;
using nGZCompGeom::convex::Stored;
using namespace fx;

namespace {

enum : uint32_t { // the description's flags (src/gz/ParticleCommands.h names them)
    kWater = 4,
    kRepeat = 8,
    kTerrainScale = 0x10,
    kStatic = 0x40,
};
enum : uint8_t { // the description's life mode
    kLifeLoop = 0,
    kLifeSingle = 1,
    kLifeSustain = 2,
};

constexpr float kOneOverTwoPi = 0.15915494f;

cRZRandom &Random() { return *GameAddress<cRZRandom>(0x10fd138); }

// The default placement; the second byte stays what it was.
void ResetPlacement(EffectsTransform &t)
{
    t.flags = 0;
    t.pad[1] = 0;
    t.pad[2] = 0;
    for (auto &row : t.m)
        for (float &v : row) v = 0.0f;
    t.m[0][0] = t.m[1][1] = t.m[2][2] = 1.0f;
    t.scale = 1.0f;
    t.translation = {0.0f, 0.0f, 0.0f};
}

// fmul dword [mem] on a value the x87 holds: a NaN in the register beats a signalling memory operand.
double Mul87Memory(double held, float memory)
{
    if (held != held && memory != memory && !(BitsOf(memory) & 0x00400000u)) return x87::Rounded(held);
    return Mul87(held, static_cast<double>(memory));
}

// 1 +- the variance, a random number between.
float Varied(float variance)
{
    return static_cast<float>(Random().RandomDoubleRangeUniform(static_cast<double>(Sub(1.0f, variance)),
                                                                static_cast<double>(Add(variance, 1.0f))));
}

// The colour list at t.
// ORIGINAL-BUG: a t outside [0, 1] (a sustained decal never gives one; NaN does) reads past the list.
Vector3 ColorAt(const fxdesc::Color *colors, uint32_t count, float t)
{
    const KeyPosition key = FindKey(Mul(FromUnsigned(count - 1), t));
    const fxdesc::Color &a = colors[key.mIndex];
    if (key.mFraction > 0.0f) {
        const fxdesc::Color &b = colors[key.mIndex + 1];
        return {Add(Mul(Sub(b.r, a.r), key.mFraction), a.r), Add(Mul(Sub(b.g, a.g), key.mFraction), a.g),
                Add(Mul(Sub(b.b, a.b), key.mFraction), a.b)};
    }
    return {a.r, a.g, a.b};
}

} // namespace

float cTSDecalEffect::AlphaAt(float t) const
{
    const auto *d = static_cast<const cTSDecalDescription *>(mDescription);
    return Mul(Mul(Stored(EvaluateCurve(d->mAlpha, t)), mAlphaScale), mAlphaFactor);
}

float cTSDecalEffect::SizeAt(float t) const
{
    const auto *d = static_cast<const cTSDecalDescription *>(mDescription);
    return Stored(Mul87Memory(Mul87(EvaluateCurve(d->mSize, t), mSize), mSizeFactor));
}

float cTSDecalEffect::RotationAt(float t) const
{
    const auto *d = static_cast<const cTSDecalDescription *>(mDescription);
    return Stored(Add87(Mul87(EvaluateCurve(d->mRotate, t), mRotateScale), static_cast<double>(mRotation)));
}

cTSDecalEffect::cTSDecalEffect(const cTSDecalDescription *description)
    : cTSEffectBase(description), mPosition{0.0f, 0.0f, 0.0f}, mRotation(0.0f), mRepeatScale(1.0f), mAspectScale(1.0f),
      mAlphaFactor(1.0f), mSizeFactor(1.0f), mPinned((description->mFlags & kTerrainScale) != 0)
{
    ResetPlacement(mPlacement);
    ResetPlacement(mPlacement2);
    mTexture.Construct();
    // maxss: a NaN life is the floor.
    const float life = description->mLife > 0.0001f ? description->mLife : 0.0001f;
    mRate = Div(1.0f, life);
    mAlphaScale = Varied(description->mAlphaVary);
    mSizeScale = Varied(description->mSizeVary);
    const float rotateVary = description->mRotateVary;
    mRotateScale = rotateVary > 0.0f ? static_cast<float>(Random().RandomDoubleRangeUniform(-static_cast<double>(rotateVary),
                                                                                           static_cast<double>(rotateVary)))
                                     : 1.0f;
    mSize = mSizeScale;
}

cTSDecalEffect::~cTSDecalEffect()
{
    if (mRunning) cTSDecalEffect::Stop(1);
    mTexture.Destruct();
}

cITSComponentEffect *__cdecl cTSDecalEffect::Create(const cTSDecalDescription *description)
{
    return new (std::nothrow) cTSDecalEffect(description);
}

// ORIGINAL-BUG: a null owner is called through. The decal of an earlier run stays when running is cleared here, so the next
// Stop leaves it with the manager, and the next Start replaces it there without removing it.
void cTSDecalEffect::Attach(cIGZUnknown *owner)
{
    mRunning = false;
    ResetPlacement(mPlacement);
    mTexture.mString.clear();
    const auto *d = static_cast<const cTSDecalDescription *>(mDescription);
    using OverlayManagerFn = cITSOverlayManager *(__thiscall *)(void *, uint32_t);
    mManager.Assign(VirtualCall<OverlayManagerFn, 26>(owner, (d->mFlags >> 2) & 1));
}

void cTSDecalEffect::Start(uint32_t a)
{
    if (mRunning) return;
    const auto *d = static_cast<const cTSDecalDescription *>(mDescription);
    float t = 0.0f;
    if (a != 0) {
        if (d->mLifeMode == kLifeSingle) return;
        if (d->mLifeMode == kLifeSustain) t = 1.0f;
    }
    if (!mManager.Get()) return;

    const float size = Stored(Mul87(EvaluateCurve(d->mSize, t), mSize));
    const float rotation = RotationAt(t);
    const char *texture = mTexture.mString.empty() ? d->mTexture.c_str() : mTexture.mString.c_str();
    mDecal.Assign(mManager->CreateDecal(texture, &mPosition, size, rotation));
    if (!mDecal.Get()) return;

    cTSDecalParams params;
    mDecal->GetParams(&params);
    const float aspect = Stored(Mul87(EvaluateCurve(d->mAspect, t), mAspectScale));
    params.mAspect = aspect;
    if (d->mFlags & kRepeat) {
        const float repeat = Mul(d->mRepeat, mRepeatScale);
        params.mRepeatU = repeat;
        params.mRepeatV = Mul(aspect, repeat);
    } else {
        params.mRepeatU = 1.0f;
        params.mRepeatV = 1.0f;
    }
    params.mOffsetU = d->mOffset.x;
    params.mOffsetV = d->mOffset.y;
    mDecal->SetParams(&params);

    cITSOverlay *overlay = mDecal->GetOverlay();
    overlay->SetAlpha(AlphaAt(t));
    // ORIGINAL-BUG: an empty colour list is read at key 0 of no storage (count - 1 wraps to 2^32 - 1).
    const fxdesc::Color *colors = d->mColors.data();
    const uint32_t count = static_cast<uint32_t>(d->mColors.size());
    const Vector3 color = count == 1 ? Vector3{colors[0].r, colors[0].g, colors[0].b} : ColorAt(colors, count, t);
    overlay->SetColor(&color);
    overlay->SetDraw(d->mDraw);
    mTime = t;
    mRunning = true;
}

void cTSDecalEffect::Stop(uint32_t)
{
    if (!mRunning) return;
    mRunning = false;
    if (!mDecal.Get()) return;
    mManager->RemoveOverlay(mDecal->GetOverlay());
    mDecal.Reset();
}

void cTSDecalEffect::AddToScene(void *step, void *context)
{
    if (!mDecal.Get()) return;
    ++*reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(context) + 0x1c);
    const auto *d = static_cast<const cTSDecalDescription *>(mDescription);
    mTime = Add(std::bit_cast<float>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(step))), mTime);
    float t = Mul(mTime, mRate);
    if (d->mLifeMode == kLifeLoop) {
        // The fraction of t past its floor: FindKey's nearest integer, through a signed conversion.
        const float shifted = Add(Sub(t, 0.5f), 12582912.0f);
        uint32_t whole = std::bit_cast<uint32_t>(shifted) + 0xb4c00000u;
        if (static_cast<float>(static_cast<int32_t>(whole + 1)) == t) whole = whole + 1;
        t = Sub(t, static_cast<float>(static_cast<int32_t>(whole)));
    } else if (t > 1.0f) {
        if (d->mLifeMode == kLifeSingle) {
            Stop(0);
            return;
        }
        t = 1.0f;
    }

    cITSOverlay *overlay = mDecal->GetOverlay();
    if (d->mFlags & kStatic) {
        overlay->SetAlpha(AlphaAt(t));
        return;
    }

    cTSDecalParams params;
    // ORIGINAL-BUG: the original never sets +0x28 here, so SetParams stores a stale stack word in the decal.
    params.mSpare = 0;
    params.mPosition = mPosition;
    params.mSize = SizeAt(t);
    params.mRotation = RotationAt(t);
    const float aspect = Stored(Mul87(EvaluateCurve(d->mAspect, t), mAspectScale));
    params.mAspect = aspect;
    if (d->mFlags & kRepeat) {
        const float repeat = Mul(d->mRepeat, mRepeatScale);
        params.mRepeatU = repeat;
        params.mRepeatV = Mul(repeat, aspect);
    } else {
        params.mRepeatU = 1.0f;
        params.mRepeatV = 1.0f;
    }
    params.mOffsetU = d->mOffset.x;
    params.mOffsetV = d->mOffset.y;
    mDecal->SetParams(&params);
    overlay->SetAlpha(AlphaAt(t));
    const uint32_t count = static_cast<uint32_t>(d->mColors.size());
    if (count > 1) {
        const Vector3 color = ColorAt(d->mColors.data(), count, t);
        overlay->SetColor(&color);
    }
}

void cTSDecalEffect::Update(const EffectsTransform *local, const EffectsTransform *parent)
{
    if (mPinned) return;
    const float x = Mul(parent->scale, local->translation.x);
    const float y = Mul(parent->scale, local->translation.y);
    const float z = Mul(parent->scale, local->translation.z);
    mPosition.x = Add(RowFromY(x, y, z, parent->m[0][0], parent->m[0][1], parent->m[0][2]), parent->translation.x);
    mPosition.y = Add(RowFromY(x, y, z, parent->m[1][0], parent->m[1][1], parent->m[1][2]), parent->translation.y);
    mPosition.z = Add(RowFromY(x, y, z, parent->m[2][0], parent->m[2][1], parent->m[2][2]), parent->translation.z);
    float product[9];
    nGZCompGeom::MatrixProduct(product, &local->m[0][0], &parent->m[0][0]);
    const float scale = Mul(parent->scale, local->scale);
    mSize = Mul(scale, mSizeScale);
    mRotation = Mul(static_cast<float>(std::atan2(static_cast<double>(product[3]), static_cast<double>(product[0]))), kOneOverTwoPi);

    const auto *d = static_cast<const cTSDecalDescription *>(mDescription);
    if ((d->mFlags & kStatic) && mDecal.Get()) {
        cTSDecalParams params;
        mDecal->GetParams(&params);
        params.mPosition = mPosition;
        params.mSize = SizeAt(0.0f);
        params.mRotation = RotationAt(0.0f);
        mDecal->SetParams(&params);
    }
}

bool cTSDecalEffect::HandleEvent(uint32_t kind, void *a, void *)
{
    const auto *v = static_cast<const float *>(a);
    switch (kind) {
    case 3:
        mAlphaFactor = v[0];
        return true;
    case 0xb: {
        const float width = Sub(v[2], v[0]);
        const float height = Sub(v[3], v[1]);
        mRotation = 0.0f;
        mAspectScale = Div(height, width);
        if (static_cast<const cTSDecalDescription *>(mDescription)->mFlags & kRepeat) mRepeatScale = width;
        mPinned = true;
        return true;
    }
    case 0x101:
        mSizeFactor = v[1];
        return true;
    }
    return false;
}

void cTSDecalEffect::SetFlagged(bool value)
{
    if (mDecal.Get()) mDecal->GetOverlay()->SetVisible(value);
}
