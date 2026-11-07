#include "ts/cTSParticlesDescription.h"
#include "ts/cTSMetaParticlesEffect.h"
#include "ts/cTSParticlesEffect.h"

#include "game/original.h"

#include <cstring>
#include <new>
#include <utility>

// The particle effect descriptions (lane IX): the parts' serializers, the descriptions' constructors and serializers.
//
// The serializers' temporaries: where the original reads into a stack word that holds something known (its own stream or
// description argument's slot), ours starts from that value; where the word is unset (a count or flags word read first
// into a fresh slot), ours starts at 0, and a read that fails there is undefined in the original (the tests never fail it).

using namespace fxdesc;

namespace {

uint32_t Bits(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}
float FloatOf(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// A vector read with a count: the count into `count` (the caller's temporary, kept), the vector resized, then each element.
template <class T, class ReadOne> void ReadCounted(void *stream, std::vector<T> *v, uint32_t &count, ReadOne readOne)
{
    GetUint32(stream, &count);
    v->resize(count);
    for (uint32_t i = 0; i < count; ++i) readOne(&(*v)[i]);
}
template <class T, class WriteOne> void WriteCounted(void *stream, const std::vector<T> &v, WriteOne writeOne)
{
    const uint32_t count = static_cast<uint32_t>(v.size());
    PutUint32(stream, count);
    for (uint32_t i = 0; i < count; ++i) writeOne(&v[i]);
}

void ReadWarps(void *stream, std::vector<cTSParticleWarp> *v, uint32_t &count)
{
    ReadCounted(stream, v, count, [stream](cTSParticleWarp *w) { cTSParticleWarp::Read(stream, w); });
}
void ReadColliders(void *stream, std::vector<cTSParticleCollider> *v, uint32_t &count)
{
    ReadCounted(stream, v, count, [stream](cTSParticleCollider *c) { cTSParticleCollider::Read(stream, c); });
}

// The versions before attractor lists (particles before 8, meta before 4): one attractor, its centre read always and kept
// when `hasCentre` (a flag bit), then its curve and strength. The strength is read over `strengthSlot` (the original reads it
// into one of its arguments' stack slots). The list gets room for one first when it has none.
void ReadOldAttractor(void *stream, cTSBaseParticlesDescription *d, bool hasCentre, uint32_t strengthSlot)
{
    cTSParticleAttractor attractor;
    Vector3 centre{0.0f, 0.0f, 0.0f}; // unset in the original: a failing read there is undefined
    GetVector3(stream, &centre);
    if (hasCentre)
        attractor.SetCentre(centre);
    else
        attractor.ClearCentre();
    cTSEffectCurve curve;
    curve.Read(stream);
    attractor.SetCurve(curve);
    float strength = FloatOf(strengthSlot);
    GetFloat(stream, &strength);
    attractor.SetStrength(strength);
    if (d->mAttractors.capacity() == 0) d->mAttractors.reserve(1); // as the original (push_back alone grows to 1 too)
    d->mAttractors.push_back(attractor);
}

void PutColors(void *stream, const std::vector<Color> &v)
{
    WriteCounted(stream, v, [stream](const Color *c) { PutColor(stream, *c); });
}

} // namespace

// ---- the parts ----

// 0xbde880. The flags' bit 1 announces a byte; the byte announces +0x48.
void __cdecl cTSParticleCollider::Read(void *stream, cTSParticleCollider *c)
{
    uint32_t flags = 0;
    GetUint32(stream, &flags);
    c->mFlags = flags & 3;
    uint8_t extra = 0;
    if ((c->mFlags >> 1) & 1) GetUint8(stream, &extra);
    GetText(stream, &c->mName);
    GetVector3(stream, &c->mPlane[0]);
    GetVector3(stream, &c->mPlane[1]);
    GetVector3(stream, &c->mPlane[2]);
    GetFloat(stream, &c->m40);
    GetFloat(stream, &c->m44);
    if (extra) GetFloat(stream, &c->m48);
    GetFloat(stream, &c->mBounce);
    GetFloat(stream, &c->m50);
    GetText(stream, &c->m54);
    GetText(stream, &c->m6C);
}

// 0xbde940. Bit 1 always set, and the byte it announces always 1.
void __cdecl cTSParticleCollider::Write(void *stream, const cTSParticleCollider *c)
{
    PutUint32(stream, c->mFlags | 2);
    PutUint8(stream, 1);
    PutText(stream, c->mName);
    PutVector3(stream, c->mPlane[0]);
    PutVector3(stream, c->mPlane[1]);
    PutVector3(stream, c->mPlane[2]);
    PutFloat(stream, c->m40);
    PutFloat(stream, c->m44);
    PutFloat(stream, c->m48);
    PutFloat(stream, c->mBounce);
    PutFloat(stream, c->m50);
    PutText(stream, c->m54);
    PutText(stream, c->m6C);
}

// 0xbde600
void cTSParticleAttractor::SetCentre(const Vector3 &centre)
{
    mCentre = centre;
    mHasCentre = 1;
}

// 0xbde620
void cTSParticleAttractor::SetCurve(const cTSEffectCurve &curve)
{
    mCurve = curve;
}

// 0xbde550. A version word first (read over the stream argument's slot, not kept).
void cTSParticleAttractor::Read(void *stream)
{
    uint16_t version = static_cast<uint16_t>(Bits(stream));
    GetUint16(stream, &version);
    GetUint8(stream, &mHasCentre);
    GetVector3(stream, &mCentre);
    mCurve.Read(stream);
    GetFloat(stream, &mStrength);
    GetFloat(stream, &m30);
}

// 0xbde750
void cTSParticleAttractor::Write(void *stream) const
{
    PutUint16(stream, 1);
    PutUint8(stream, mHasCentre);
    PutVector3(stream, mCentre);
    mCurve.Write(stream);
    PutFloat(stream, mStrength);
    PutFloat(stream, m30);
}

// 0xbfa580
void __cdecl cTSParticleWarp::Read(void *stream, cTSParticleWarp *w)
{
    GetFloat(stream, &w->mValue);
    GetVector3(stream, &w->mFirst);
    GetVector3(stream, &w->mSecond);
}

// 0xbfa660
void __cdecl cTSParticleWarp::Write(void *stream, const cTSParticleWarp *w)
{
    PutFloat(stream, w->mValue);
    PutVector3(stream, w->mFirst);
    PutVector3(stream, w->mSecond);
}

// 0xbfa550
void __cdecl cTSParticlePathPoint::Read(void *stream, cTSParticlePathPoint *p)
{
    GetVector3(stream, &p->mPosition);
    GetVector3(stream, &p->mVelocity);
    GetFloat(stream, &p->mTime);
}

// 0xbfa620
void __cdecl cTSParticlePathPoint::Write(void *stream, const cTSParticlePathPoint *p)
{
    PutVector3(stream, p->mPosition);
    PutVector3(stream, p->mVelocity);
    PutFloat(stream, p->mTime);
}

// 0xbfb430. A version word first (not kept), the points, the flags.
void cTSParticlePath::Read(void *stream)
{
    uint32_t version = 0;
    GetUint32(stream, &version);
    uint32_t count = 0;
    ReadCounted(stream, &mPoints, count, [stream](cTSParticlePathPoint *p) { cTSParticlePathPoint::Read(stream, p); });
    GetUint32(stream, &mFlags);
}

// 0xbfb510
void cTSParticlePath::Write(void *stream) const
{
    PutUint32(stream, 1);
    WriteCounted(stream, mPoints, [stream](const cTSParticlePathPoint *p) { cTSParticlePathPoint::Write(stream, p); });
    PutUint32(stream, mFlags);
}

// 0xbfa500
void __cdecl cTSParticleWalk::Read(void *stream, cTSParticleWalk *w)
{
    GetVector2(stream, &w->mDelay);
    GetVector2(stream, &w->mStrength);
    GetFloat(stream, &w->mTurn[0]);
    GetFloat(stream, &w->mTurn[1]);
    GetFloat(stream, &w->mMix);
}

// 0xbfa5c0
void __cdecl cTSParticleWalk::Write(void *stream, const cTSParticleWalk *w)
{
    PutVector2(stream, w->mDelay);
    PutVector2(stream, w->mStrength);
    PutFloat(stream, w->mTurn[0]);
    PutFloat(stream, w->mTurn[1]);
    PutFloat(stream, w->mMix);
}

// 0xbe02f0
void *__cdecl ReadParticleTriples(void *stream, std::vector<Color> *v)
{
    uint32_t count = 0;
    ReadCounted(stream, v, count, [stream](Color *c) { GetColor(stream, c); });
    return stream;
}

// 0xbde7b0
void *__cdecl ReadParticleText(void *stream, std::string *text)
{
    GetText(stream, text);
    return stream;
}

// ---- the base ----

// 0xbfb990
cTSBaseParticlesDescription::~cTSBaseParticlesDescription() = default;

// ---- particles (type 1) ----

// 0xbe15d0: the base's defaults, then one white colour and one zero pivot.
cTSParticlesDescription::cTSParticlesDescription()
{
    mColors.push_back(Color(1.0f, 1.0f, 1.0f));
    mPivots.push_back(Color(0.0f, 0.0f, 0.0f));
}

// 0xbe1be0
cTSParticlesDescription::~cTSParticlesDescription() = default;

// 0xbe58b0 (slot 1): the description's effect, made by the effect's creator.
cITSComponentEffect *cTSParticlesDescription::CreateEffect()
{
    return cTSParticlesEffect::Create(this);
}

// 0xbe72e0. Versions 1..11; older ones lack fields (and the attractor list: one attractor then).
void __cdecl cTSParticlesDescription::Read(void *stream, uint32_t version, cTSParticlesDescription *d)
{
    const int32_t v = static_cast<int32_t>(version);
    uint32_t scratch = 0; // the original's one temporary for the flags, the old rotate values and the warp / collider counts
    if (v < 5) {
        uint32_t low = 0;
        GetUint32(stream, &low);
        d->mFlags = low;
    } else {
        GetUint32(stream, &scratch);
        const uint32_t high = scratch & 0x3f;
        GetUint32(stream, &scratch);
        d->mFlags = static_cast<uint64_t>(high) << 32 | scratch;
    }
    GetVector2(stream, &d->mLife);
    GetFloat(stream, &d->mPreroll);
    GetVector2(stream, &d->m28);
    GetVector2(stream, &d->m30);
    GetVector3(stream, &d->m38);
    GetVector3(stream, &d->m44);
    GetVector2(stream, &d->mEmitVolume);
    GetVector3(stream, &d->mSourceMin);
    GetVector3(stream, &d->mSourceMax);
    if (v > 2) GetFloat(stream, &d->mSourceRing);
    d->mRate.Read(stream);
    GetFloat(stream, &d->mRateVary);
    GetUint16(stream, reinterpret_cast<uint16_t *>(&d->mRateMode));
    GetFloat(stream, &d->m160);
    d->mSize.Read(stream);
    GetFloat(stream, &d->mSizeVary);
    d->mAlpha.Read(stream);
    GetFloat(stream, &d->mAlphaVary);
    GetVector3(stream, &d->mRotateAxis[0]);
    GetVector3(stream, &d->mRotateAxis[1]);
    GetUint8(stream, &d->mRotateCount);
    if (v < 6) {
        // One value per rotate, made a symmetric range (1, 1 for none above 0).
        for (Vector2 &vary : d->mRotateVary) {
            GetFloat(stream, reinterpret_cast<float *>(&scratch));
            const float f = FloatOf(scratch);
            if (f > 0.0f) {
                vary.y = f;
                vary.x = -f;
            } else {
                vary.y = 1.0f;
                vary.x = 1.0f;
            }
        }
    } else {
        GetVector2(stream, &d->mRotateVary[0]);
        GetVector2(stream, &d->mRotateVary[1]);
    }
    GetFloat(stream, &d->mRotateOffset[0]);
    GetFloat(stream, &d->mRotateOffset[1]);
    d->mRotateCurve[0].Read(stream);
    d->mRotateCurve[1].Read(stream);
    if (v > 4) {
        ReadParticleTriples(stream, &d->mPivots);
        GetVector3(stream, &d->mPivotVary);
    }
    d->mAspect.Read(stream);
    GetFloat(stream, &d->mAspectVary);
    ReadParticleTriples(stream, &d->mColors);
    GetVector3(stream, &d->mColorVary);
    GetText(stream, &d->mMaterial);
    GetUint8(stream, &d->mTile[0]);
    GetUint8(stream, &d->mTile[1]);
    GetUint8(stream, &d->mAlignment);
    GetUint8(stream, &d->mDraw);
    GetFloat(stream, &d->mLayer);
    GetFloat(stream, &d->mFrameSpeed);
    GetUint8(stream, &d->mFrameStart);
    GetUint8(stream, &d->mFrameCount);
    GetVector3(stream, &d->mForce);
    GetFloat(stream, &d->mGlobalWind);
    GetFloat(stream, &d->mBomb);
    GetVector3(stream, &d->mBombCentre);
    GetFloat(stream, &d->mDrag);
    GetFloat(stream, &d->mStretch);
    GetFloat(stream, &d->mScrew);
    ReadWarps(stream, &d->mWarps, scratch);
    for (uint8_t &b : d->mBloom) GetUint8(stream, &b);
    ReadColliders(stream, &d->mColliders, scratch);
    GetText(stream, &d->mConstrain);
    if (v > 10) GetVector3(stream, &d->mConstrainOffset);
    GetFloat(stream, &d->mBounce);
    for (float &f : d->mTerrainRepel) GetFloat(stream, &f);
    GetFloat(stream, &d->mDeath);
    GetFloat(stream, &d->mDeathByWater);
    GetVector2(stream, &d->mHeightRange);
    GetVector2(stream, &d->mWalkDelay);
    GetVector2(stream, &d->mWalkStrength);
    GetFloat(stream, &d->mWalkTurn.x);
    GetFloat(stream, &d->mWalkTurn.y);
    if (v < 8) {
        // The strength over the description argument's slot.
        ReadOldAttractor(stream, d, (d->mFlags & (1u << 27)) != 0, Bits(d));
        if (v >= 7)
            cTSParticlePartsAdapters::ReadPathOf(stream, &d->mPath);
        else if (v < 4)
            return;
    } else {
        cTSParticlePartsAdapters::ReadAttractors(stream, &d->mAttractors);
        cTSParticlePartsAdapters::ReadPathOf(stream, &d->mPath);
    }
    GetText(stream, &d->mDeathEffect);
    if (v > 8) GetFloat(stream, &d->mLimitHeight);
    if (v > 10) {
        d->mScrewRadius.Read(stream);
        GetUint8(stream, &d->mScrewRadiusToSpeed);
        GetFloat(stream, &d->mScrewRadiusVary);
        GetFloat(stream, &d->mScrewRadiusMax);
        d->mScrewAngle.Read(stream);
        GetUint8(stream, &d->mScrewAngleToSpeed);
        GetFloat(stream, &d->mScrewAngleVary);
    }
}

// 0xbe7db0 (version 11)
void __cdecl cTSParticlesDescription::Write(void *stream, const cTSParticlesDescription *d)
{
    PutUint32(stream, static_cast<uint32_t>(d->mFlags >> 32));
    PutUint32(stream, static_cast<uint32_t>(d->mFlags));
    PutVector2(stream, d->mLife);
    PutFloat(stream, d->mPreroll);
    PutVector2(stream, d->m28);
    PutVector2(stream, d->m30);
    PutVector3(stream, d->m38);
    PutVector3(stream, d->m44);
    PutVector2(stream, d->mEmitVolume);
    PutVector3(stream, d->mSourceMin);
    PutVector3(stream, d->mSourceMax);
    PutFloat(stream, d->mSourceRing);
    d->mRate.Write(stream);
    PutFloat(stream, d->mRateVary);
    PutUint16(stream, static_cast<uint16_t>(d->mRateMode));
    PutFloat(stream, d->m160);
    d->mSize.Write(stream);
    PutFloat(stream, d->mSizeVary);
    d->mAlpha.Write(stream);
    PutFloat(stream, d->mAlphaVary);
    PutVector3(stream, d->mRotateAxis[0]);
    PutVector3(stream, d->mRotateAxis[1]);
    PutUint8(stream, d->mRotateCount);
    PutVector2(stream, d->mRotateVary[0]);
    PutVector2(stream, d->mRotateVary[1]);
    PutFloat(stream, d->mRotateOffset[0]);
    PutFloat(stream, d->mRotateOffset[1]);
    d->mRotateCurve[0].Write(stream);
    d->mRotateCurve[1].Write(stream);
    PutColors(stream, d->mPivots);
    PutVector3(stream, d->mPivotVary);
    d->mAspect.Write(stream);
    PutFloat(stream, d->mAspectVary);
    PutColors(stream, d->mColors);
    PutVector3(stream, d->mColorVary);
    PutText(stream, d->mMaterial);
    PutUint8(stream, d->mTile[0]);
    PutUint8(stream, d->mTile[1]);
    PutUint8(stream, d->mAlignment);
    PutUint8(stream, d->mDraw);
    PutFloat(stream, d->mLayer);
    PutFloat(stream, d->mFrameSpeed);
    PutUint8(stream, d->mFrameStart);
    PutUint8(stream, d->mFrameCount);
    PutVector3(stream, d->mForce);
    PutFloat(stream, d->mGlobalWind);
    PutFloat(stream, d->mBomb);
    PutVector3(stream, d->mBombCentre);
    PutFloat(stream, d->mDrag);
    PutFloat(stream, d->mStretch);
    PutFloat(stream, d->mScrew);
    WriteCounted(stream, d->mWarps, [stream](const cTSParticleWarp *w) { cTSParticleWarp::Write(stream, w); });
    for (const uint8_t b : d->mBloom) PutUint8(stream, b);
    WriteCounted(stream, d->mColliders, [stream](const cTSParticleCollider *c) { cTSParticleCollider::Write(stream, c); });
    PutText(stream, d->mConstrain);
    PutVector3(stream, d->mConstrainOffset);
    PutFloat(stream, d->mBounce);
    for (const float f : d->mTerrainRepel) PutFloat(stream, f);
    PutFloat(stream, d->mDeath);
    PutFloat(stream, d->mDeathByWater);
    PutVector2(stream, d->mHeightRange);
    PutVector2(stream, d->mWalkDelay);
    PutVector2(stream, d->mWalkStrength);
    PutFloat(stream, d->mWalkTurn.x);
    PutFloat(stream, d->mWalkTurn.y);
    cTSParticlePartsAdapters::WriteAttractors(stream, &d->mAttractors);
    cTSParticlePartsAdapters::WritePathOf(stream, &d->mPath);
    PutText(stream, d->mDeathEffect);
    PutFloat(stream, d->mLimitHeight);
    d->mScrewRadius.Write(stream);
    PutUint8(stream, d->mScrewRadiusToSpeed);
    PutFloat(stream, d->mScrewRadiusVary);
    PutFloat(stream, d->mScrewRadiusMax);
    d->mScrewAngle.Write(stream);
    PutUint8(stream, d->mScrewAngleToSpeed);
    PutFloat(stream, d->mScrewAngleVary);
}

// ---- meta particles (type 2) ----

// 0xbecf80: the base's defaults, then one white colour.
cTSMetaParticlesDescription::cTSMetaParticlesDescription()
{
    mColors.push_back(Color(1.0f, 1.0f, 1.0f));
}

// 0xbed140
cTSMetaParticlesDescription::~cTSMetaParticlesDescription() = default;

// 0xbefb70 (slot 1)
cITSComponentEffect *cTSMetaParticlesDescription::CreateEffect()
{
    return cTSMetaParticlesEffect::Create(this);
}

// 0xbf1100. Versions 1..4.
void __cdecl cTSMetaParticlesDescription::Read(void *stream, uint32_t version, cTSMetaParticlesDescription *d)
{
    const int32_t v = static_cast<int32_t>(version);
    uint32_t scratch = 0; // the original's one temporary for the flags and the colour, warp and collider counts
    GetUint32(stream, &scratch);
    const uint32_t high = scratch & 3;
    GetUint32(stream, &scratch);
    d->mFlags = static_cast<uint64_t>(high) << 32 | scratch;
    GetVector2(stream, &d->mLife);
    GetFloat(stream, &d->mPreroll);
    GetVector2(stream, &d->m28);
    GetVector2(stream, &d->m30);
    GetVector3(stream, &d->m38);
    GetVector3(stream, &d->m44);
    GetVector2(stream, &d->mEmitVolume);
    GetVector3(stream, &d->mSourceMin);
    GetVector3(stream, &d->mSourceMax);
    if (v > 1) GetFloat(stream, &d->mSourceRing);
    d->mRate.Read(stream);
    GetFloat(stream, &d->mRateVary);
    GetUint32(stream, &d->mRateMode);
    d->mSize.Read(stream);
    GetFloat(stream, &d->mSizeVary);
    d->mAlpha.Read(stream);
    GetFloat(stream, &d->mAlphaVary);
    d->mRotateCurve.Read(stream);
    GetFloat(stream, &d->mRotateVary);
    GetFloat(stream, &d->mRotateOffset);
    ReadCounted(stream, &d->mColors, scratch, [stream](Color *c) { GetColor(stream, c); });
    GetVector3(stream, &d->mColorVary);
    d->mAspect.Read(stream);
    GetFloat(stream, &d->mAspectVary);
    GetText(stream, &d->mEffect);
    GetUint8(stream, &d->mAlignment);
    GetVector3(stream, &d->mForce);
    GetFloat(stream, &d->mGlobalWind);
    GetFloat(stream, &d->mBomb);
    GetVector3(stream, &d->mBombCentre);
    GetFloat(stream, &d->mDrag);
    GetFloat(stream, &d->mScrew);
    ReadWarps(stream, &d->mWarps, scratch);
    for (uint8_t &b : d->mBloom) GetUint8(stream, &b);
    ReadColliders(stream, &d->mColliders, scratch);
    GetFloat(stream, &d->mBounce);
    for (float &f : d->mTerrainRepel) GetFloat(stream, &f);
    GetFloat(stream, &d->mDeath);
    GetFloat(stream, &d->mDeathByWater);
    GetVector2(stream, &d->mHeightRange);
    for (cTSParticleWalk &w : d->mWalks) cTSParticleWalk::Read(stream, &w);
    GetVector3(stream, &d->mPreferDir);
    GetFloat(stream, &d->mDamp);
    GetFloat(stream, &d->mBank.x);
    GetFloat(stream, &d->mBank.y);
    if (v < 4) {
        // The strength over the stream argument's slot, which the walks' loop counted down to 0.
        ReadOldAttractor(stream, d, (d->mFlags & (1u << 28)) != 0, 0);
        if (v < 3) {
            // Before version 3 the path is its points alone (no version word, no flags). The count's temporary is a slot that
            // last held the collider list's end: a failing read there is undefined in the original.
            uint32_t count = 0;
            ReadCounted(stream, &d->mPath.mPoints, count,
                        [stream](cTSParticlePathPoint *p) { cTSParticlePathPoint::Read(stream, p); });
        } else {
            cTSParticlePartsAdapters::ReadPathOf(stream, &d->mPath);
        }
    } else {
        cTSParticlePartsAdapters::ReadAttractors(stream, &d->mAttractors);
        cTSParticlePartsAdapters::ReadPathOf(stream, &d->mPath);
    }
    GetFloat(stream, &d->mTractorResetSpeed);
}

// 0xbf1c80 (version 4)
void __cdecl cTSMetaParticlesDescription::Write(void *stream, const cTSMetaParticlesDescription *d)
{
    PutUint32(stream, static_cast<uint32_t>(d->mFlags >> 32));
    PutUint32(stream, static_cast<uint32_t>(d->mFlags));
    PutVector2(stream, d->mLife);
    PutFloat(stream, d->mPreroll);
    PutVector2(stream, d->m28);
    PutVector2(stream, d->m30);
    PutVector3(stream, d->m38);
    PutVector3(stream, d->m44);
    PutVector2(stream, d->mEmitVolume);
    PutVector3(stream, d->mSourceMin);
    PutVector3(stream, d->mSourceMax);
    PutFloat(stream, d->mSourceRing);
    d->mRate.Write(stream);
    PutFloat(stream, d->mRateVary);
    PutUint32(stream, d->mRateMode);
    d->mSize.Write(stream);
    PutFloat(stream, d->mSizeVary);
    d->mAlpha.Write(stream);
    PutFloat(stream, d->mAlphaVary);
    d->mRotateCurve.Write(stream);
    PutFloat(stream, d->mRotateVary);
    PutFloat(stream, d->mRotateOffset);
    PutColors(stream, d->mColors);
    PutVector3(stream, d->mColorVary);
    d->mAspect.Write(stream);
    PutFloat(stream, d->mAspectVary);
    PutText(stream, d->mEffect);
    PutUint8(stream, d->mAlignment);
    PutVector3(stream, d->mForce);
    PutFloat(stream, d->mGlobalWind);
    PutFloat(stream, d->mBomb);
    PutVector3(stream, d->mBombCentre);
    PutFloat(stream, d->mDrag);
    PutFloat(stream, d->mScrew);
    WriteCounted(stream, d->mWarps, [stream](const cTSParticleWarp *w) { cTSParticleWarp::Write(stream, w); });
    for (const uint8_t b : d->mBloom) PutUint8(stream, b);
    WriteCounted(stream, d->mColliders, [stream](const cTSParticleCollider *c) { cTSParticleCollider::Write(stream, c); });
    PutFloat(stream, d->mBounce);
    for (const float f : d->mTerrainRepel) PutFloat(stream, f);
    PutFloat(stream, d->mDeath);
    PutFloat(stream, d->mDeathByWater);
    PutVector2(stream, d->mHeightRange);
    for (const cTSParticleWalk &w : d->mWalks) cTSParticleWalk::Write(stream, &w);
    PutVector3(stream, d->mPreferDir);
    PutFloat(stream, d->mDamp);
    PutFloat(stream, d->mBank.x);
    PutFloat(stream, d->mBank.y);
    cTSParticlePartsAdapters::WriteAttractors(stream, &d->mAttractors);
    cTSParticlePartsAdapters::WritePathOf(stream, &d->mPath);
    PutFloat(stream, d->mTractorResetSpeed);
}

// ---- the hooks' entry points ----

cTSBaseParticlesDescription *__fastcall cTSParticlePartsAdapters::ConstructBase(void *memory, void *)
{
    return ::new (memory) cTSBaseParticlesDescription;
}
void __fastcall cTSParticlePartsAdapters::DestroyBase(cTSBaseParticlesDescription *self, void *)
{
    self->cTSBaseParticlesDescription::~cTSBaseParticlesDescription();
}
cTSBaseParticlesDescription *__fastcall cTSParticlePartsAdapters::AssignBase(cTSBaseParticlesDescription *self, void *,
                                                                           const cTSBaseParticlesDescription *from)
{
    *self = *from;
    return self;
}
cTSBaseParticlesDescription *__fastcall cTSParticlePartsAdapters::DeleteBase(cTSBaseParticlesDescription *self, void *,
                                                                           uint32_t flags)
{
    self->cTSBaseParticlesDescription::~cTSBaseParticlesDescription();
    if (flags & 1) ::operator delete(self, sizeof(cTSBaseParticlesDescription));
    return self;
}
cTSParticlesDescription *__fastcall cTSParticlePartsAdapters::DeleteParticles(cTSParticlesDescription *self, void *, uint32_t flags)
{
    self->cTSParticlesDescription::~cTSParticlesDescription();
    if (flags & 1) ::operator delete(self, sizeof(cTSParticlesDescription));
    return self;
}
cTSMetaParticlesDescription *__fastcall cTSParticlePartsAdapters::DeleteMeta(cTSMetaParticlesDescription *self, void *,
                                                                           uint32_t flags)
{
    self->cTSMetaParticlesDescription::~cTSMetaParticlesDescription();
    if (flags & 1) ::operator delete(self, sizeof(cTSMetaParticlesDescription));
    return self;
}
cTSParticlesDescription *__fastcall cTSParticlePartsAdapters::ConstructParticles(void *memory, void *)
{
    return ::new (memory) cTSParticlesDescription;
}
void __fastcall cTSParticlePartsAdapters::DestroyParticles(cTSParticlesDescription *self, void *)
{
    self->cTSParticlesDescription::~cTSParticlesDescription();
}
cTSParticlesDescription *__fastcall cTSParticlePartsAdapters::MoveParticles(cTSParticlesDescription *self, void *,
                                                                           cTSParticlesDescription *from)
{
    *self = std::move(*from);
    return self;
}
cTSParticlesDescription *__fastcall cTSParticlePartsAdapters::CopyParticles(cTSParticlesDescription *self, void *,
                                                                           const cTSParticlesDescription *from)
{
    *self = *from;
    return self;
}
cTSMetaParticlesDescription *__fastcall cTSParticlePartsAdapters::ConstructMeta(void *memory, void *)
{
    return ::new (memory) cTSMetaParticlesDescription;
}
void __fastcall cTSParticlePartsAdapters::DestroyMeta(cTSMetaParticlesDescription *self, void *)
{
    self->cTSMetaParticlesDescription::~cTSMetaParticlesDescription();
}
cTSMetaParticlesDescription *__fastcall cTSParticlePartsAdapters::CopyMeta(cTSMetaParticlesDescription *self, void *,
                                                                         const cTSMetaParticlesDescription *from)
{
    *self = *from;
    return self;
}
cTSParticleCollider *__fastcall cTSParticlePartsAdapters::ConstructCollider(void *memory, void *)
{
    return ::new (memory) cTSParticleCollider;
}
void __fastcall cTSParticlePartsAdapters::DestroyCollider(cTSParticleCollider *self, void *)
{
    self->~cTSParticleCollider();
}
cTSParticleCollider *__fastcall cTSParticlePartsAdapters::CopyCollider(void *memory, void *, const cTSParticleCollider *from)
{
    return ::new (memory) cTSParticleCollider(*from);
}
cTSParticleAttractor *__fastcall cTSParticlePartsAdapters::ConstructAttractor(void *memory, void *)
{
    return ::new (memory) cTSParticleAttractor;
}
void __fastcall cTSParticlePartsAdapters::DestroyAttractor(cTSParticleAttractor *self, void *)
{
    self->~cTSParticleAttractor();
}
cTSParticleAttractor *__fastcall cTSParticlePartsAdapters::CopyAttractor(void *memory, void *, const cTSParticleAttractor *from)
{
    return ::new (memory) cTSParticleAttractor(*from);
}
void __fastcall cTSParticlePartsAdapters::SetCentre(cTSParticleAttractor *self, void *, const Vector3 *centre)
{
    self->SetCentre(*centre);
}
void __fastcall cTSParticlePartsAdapters::SetCurve(cTSParticleAttractor *self, void *, const cTSEffectCurve *curve)
{
    self->SetCurve(*curve);
}
void __fastcall cTSParticlePartsAdapters::ReadAttractor(cTSParticleAttractor *self, void *, void *stream)
{
    self->Read(stream);
}
void __fastcall cTSParticlePartsAdapters::WriteAttractor(const cTSParticleAttractor *self, void *, void *stream)
{
    self->Write(stream);
}
void __cdecl cTSParticlePartsAdapters::ReadAttractorOf(void *stream, cTSParticleAttractor *a)
{
    a->Read(stream);
}
void __cdecl cTSParticlePartsAdapters::WriteAttractorOf(void *stream, const cTSParticleAttractor *a)
{
    a->Write(stream);
}
// 0xbddd90: a version word (not kept), the count, the vector resized, then each attractor.
void __cdecl cTSParticlePartsAdapters::ReadAttractors(void *stream, std::vector<cTSParticleAttractor> *v)
{
    uint16_t version = 0;
    GetUint16(stream, &version);
    uint32_t count = 0;
    ReadCounted(stream, v, count, [stream](cTSParticleAttractor *a) { ReadAttractorOf(stream, a); });
}
// 0xbdde70
void __cdecl cTSParticlePartsAdapters::WriteAttractors(void *stream, const std::vector<cTSParticleAttractor> *v)
{
    PutUint16(stream, 1);
    WriteCounted(stream, *v, [stream](const cTSParticleAttractor *a) { WriteAttractorOf(stream, a); });
}
void __fastcall cTSParticlePartsAdapters::ReadPath(cTSParticlePath *self, void *, void *stream)
{
    self->Read(stream);
}
void __fastcall cTSParticlePartsAdapters::WritePath(const cTSParticlePath *self, void *, void *stream)
{
    self->Write(stream);
}
void __cdecl cTSParticlePartsAdapters::ReadPathOf(void *stream, cTSParticlePath *p)
{
    p->Read(stream);
}
void __cdecl cTSParticlePartsAdapters::WritePathOf(void *stream, const cTSParticlePath *p)
{
    p->Write(stream);
}

// The factories 0xbe7ba0 (particles) and 0xbf1a70 (meta particles): the template's Create
// (src/ts/cTSCompDescription.cpp has its body; the instances are made here).
template <class D, int32_t kType> cITSComponentDescription *__cdecl cTSCompDescriptionBaseT<D, kType>::Create()
{
    return new (std::nothrow) cTSCompDescriptionBaseT;
}
template class cTSCompDescriptionBaseT<cTSParticlesDescription, 1>;
template class cTSCompDescriptionBaseT<cTSMetaParticlesDescription, 2>;
