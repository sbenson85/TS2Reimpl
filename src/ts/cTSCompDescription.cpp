#include "ts/cTSCompDescription.h"
#include "ts/cTSDecalEffect.h"
#include "ts/cTSLightEffect.h"
#include "ts/cTSModelEffect.h"
#include "ts/cTSSequenceEffect.h"
#include "ts/cTSSoundEffect.h"
#include "ts/cTSWaterEffect.h"

#include "game/original.h"
#include "game/vtable.h"
#include "rz/cRZString.h"

// The effect component descriptions (lane IT): the stream helpers and the description structs' serializers. The reads that
// mask a value (the flags) read into a temporary that starts as the stream pointer, as the originals read into their own
// stream argument's stack slot: a stream that fails without writing leaves that.

namespace fxdesc {

namespace {
uint32_t Initial(void *stream)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream));
}
} // namespace

void GetSint8(void *stream, uint8_t *value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint8_t *)>(stream, 4)(stream, value);
}
void PutSint8(void *stream, uint8_t value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint8_t)>(stream, 4)(stream, value);
}
void GetColor(void *stream, Color *c)
{
    GetFloat(stream, &c->r);
    GetFloat(stream, &c->g);
    GetFloat(stream, &c->b);
}
void PutColor(void *stream, const Color &c)
{
    PutFloat(stream, c.r);
    PutFloat(stream, c.g);
    PutFloat(stream, c.b);
}
void GetUint8(void *stream, uint8_t *value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint8_t *)>(stream, 5)(stream, value);
}
void GetSint16(void *stream, int16_t *value)
{
    VirtualSlot<bool(__thiscall *)(void *, int16_t *)>(stream, 6)(stream, value);
}
void GetUint16(void *stream, uint16_t *value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint16_t *)>(stream, 7)(stream, value);
}
void GetUint32(void *stream, uint32_t *value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint32_t *)>(stream, 9)(stream, value);
}
void GetFloat(void *stream, float *value)
{
    VirtualSlot<bool(__thiscall *)(void *, float *)>(stream, 12)(stream, value);
}
void PutUint8(void *stream, uint8_t value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint8_t)>(stream, 5)(stream, value);
}
void PutSint16(void *stream, int16_t value)
{
    VirtualSlot<bool(__thiscall *)(void *, int16_t)>(stream, 6)(stream, value);
}
void PutUint16(void *stream, uint16_t value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint16_t)>(stream, 7)(stream, value);
}
void PutUint32(void *stream, uint32_t value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint32_t)>(stream, 9)(stream, value);
}
void PutFloat(void *stream, float value)
{
    VirtualSlot<bool(__thiscall *)(void *, float)>(stream, 12)(stream, value);
}
void GetVector2(void *stream, Vector2 *v)
{
    GetFloat(stream, &v->x);
    GetFloat(stream, &v->y);
}
void PutVector2(void *stream, const Vector2 &v)
{
    PutFloat(stream, v.x);
    PutFloat(stream, v.y);
}

void GetVector3(void *stream, Vector3 *v)
{
    GetFloat(stream, &v->x);
    GetFloat(stream, &v->y);
    GetFloat(stream, &v->z);
}
void PutVector3(void *stream, const Vector3 &v)
{
    PutFloat(stream, v.x);
    PutFloat(stream, v.y);
    PutFloat(stream, v.z);
}
void GetText(void *stream, std::string *text)
{
    cRZString &buffer = EffectsReadBuffer();
    VirtualSlot<bool(__thiscall *)(void *, cRZString *)>(stream, 15)(stream, &buffer);
    if (text != &buffer.mString) text->assign(buffer.mString.c_str(), buffer.mString.size());
}
void PutText(void *stream, const std::string &text)
{
    VirtualSlot<bool(__thiscall *)(void *, const char *)>(stream, 14)(stream, text.c_str());
}

// The flags word: read, then masked to `mask`.
uint32_t GetMasked(void *stream, uint32_t mask)
{
    uint32_t value = Initial(stream);
    GetUint32(stream, &value);
    return value & mask;
}

} // namespace fxdesc

using namespace fxdesc;

// ---- brush (type 4) ----

// 0xbf89e0
void __cdecl cTSBrushDescription::Read(void *stream, uint32_t, cTSBrushDescription *d)
{
    GetUint32(stream, &d->mFlags);
    GetFloat(stream, &d->mFloat4);
    GetFloat(stream, &d->mFloat8);
    GetUint8(stream, &d->mByteC);
    GetUint32(stream, &d->mValue10);
    GetVector2(stream, &d->mVector14);
    GetVector2(stream, &d->mVector1C);
    GetFloat(stream, &d->mFloat24);
}

// 0xbf8ba0
void __cdecl cTSBrushDescription::Write(void *stream, const cTSBrushDescription *d)
{
    PutUint32(stream, d->mFlags);
    PutFloat(stream, d->mFloat4);
    PutFloat(stream, d->mFloat8);
    PutUint8(stream, d->mByteC);
    PutUint32(stream, d->mValue10);
    PutVector2(stream, d->mVector14);
    PutVector2(stream, d->mVector1C);
    PutFloat(stream, d->mFloat24);
}

// ---- scrubber (type 5) ----

// 0xbfa0a0
void __cdecl cTSScrubberDescription::Read(void *stream, uint32_t, cTSScrubberDescription *d)
{
    d->mFlags = GetMasked(stream, 0xf);
    GetUint32(stream, &d->mValue4);
    GetUint32(stream, &d->mValue8);
    GetUint32(stream, &d->mValueC);
    GetFloat(stream, &d->mFloat10);
    GetFloat(stream, &d->mFloat14);
    GetUint32(stream, &d->mValue18);
    GetUint32(stream, &d->mValue1C);
    GetFloat(stream, &d->mFloat20);
    GetVector2(stream, &d->mVector24);
    GetFloat(stream, &d->mFloat2C);
    GetFloat(stream, &d->mFloat30);
}

// 0xbfa1f0
void __cdecl cTSScrubberDescription::Write(void *stream, const cTSScrubberDescription *d)
{
    PutUint32(stream, d->mFlags);
    PutUint32(stream, d->mValue4);
    PutUint32(stream, d->mValue8);
    PutUint32(stream, d->mValueC);
    PutFloat(stream, d->mFloat10);
    PutFloat(stream, d->mFloat14);
    PutUint32(stream, d->mValue18);
    PutUint32(stream, d->mValue1C);
    PutFloat(stream, d->mFloat20);
    PutVector2(stream, d->mVector24);
    PutFloat(stream, d->mFloat2C);
    PutFloat(stream, d->mFloat30);
}

// ---- sound (type 7) ----

// 0xbf2530 (slot 1): the description's effect, made by the effect's creator.
cITSComponentEffect *cTSSoundDescription::CreateEffect()
{
    return cTSSoundEffect::Create(this);
}

// 0xbf2580
void __cdecl cTSSoundDescription::Read(void *stream, uint32_t, cTSSoundDescription *d)
{
    d->mFlags = GetMasked(stream, 3);
    GetUint32(stream, &d->mSound);
    GetFloat(stream, &d->mFloat8);
    GetFloat(stream, &d->mFloatC);
    GetFloat(stream, &d->mFloat10);
}

// 0xbf2720
void __cdecl cTSSoundDescription::Write(void *stream, const cTSSoundDescription *d)
{
    PutUint32(stream, d->mFlags);
    PutUint32(stream, d->mSound);
    PutFloat(stream, d->mFloat8);
    PutFloat(stream, d->mFloatC);
    PutFloat(stream, d->mFloat10);
}

// ---- game (type 9) ----

// 0xbfa350
void __cdecl cTSGameDescription::Read(void *stream, uint32_t, cTSGameDescription *d)
{
    d->mFlags = GetMasked(stream, 7);
    GetUint32(stream, &d->mValue4);
    GetUint32(stream, &d->mValue8);
    GetFloat(stream, &d->mFloatC);
}

// 0xbfa460
void __cdecl cTSGameDescription::Write(void *stream, const cTSGameDescription *d)
{
    PutUint32(stream, d->mFlags);
    PutUint32(stream, d->mValue4);
    PutUint32(stream, d->mValue8);
    PutFloat(stream, d->mFloatC);
}

// ---- water (type 12) ----

// 0xbf6c30 (slot 1): the description's effect, made by the effect's creator.
cITSComponentEffect *cTSWaterDescription::CreateEffect()
{
    return cTSWaterEffect::Create(this);
}

// 0xbf6ef0
void __cdecl cTSWaterDescription::Read(void *stream, uint32_t, cTSWaterDescription *d)
{
    d->mFlags = GetMasked(stream, 1);
    if (d->mFlags & 1) {
        GetUint8(stream, &d->mByte8);
        GetUint8(stream, &d->mByte9);
        GetSint16(stream, &d->mShortA);
        GetFloat(stream, &d->mFloat14);
        GetFloat(stream, &d->mFloat18);
        GetFloat(stream, &d->mFloat1C);
        GetFloat(stream, &d->mFloat20);
        GetFloat(stream, &d->mFloat24);
        return;
    }
    GetFloat(stream, &d->mFloat8);
    GetFloat(stream, &d->mFloatC);
    GetFloat(stream, &d->mFloat10);
}

// 0xbf71a0
void __cdecl cTSWaterDescription::Write(void *stream, const cTSWaterDescription *d)
{
    PutUint32(stream, d->mFlags);
    if (d->mFlags & 1) {
        PutUint8(stream, d->mByte8);
        PutUint8(stream, d->mByte9);
        PutSint16(stream, d->mShortA);
        PutFloat(stream, d->mFloat14);
        PutFloat(stream, d->mFloat18);
        PutFloat(stream, d->mFloat1C);
        PutFloat(stream, d->mFloat20);
        PutFloat(stream, d->mFloat24);
        return;
    }
    PutFloat(stream, d->mFloat8);
    PutFloat(stream, d->mFloatC);
    PutFloat(stream, d->mFloat10);
}

// ---- sequence (type 6) ----

// 0xbf3fe0 (slot 1)
cITSComponentEffect *cTSSequenceDescription::CreateEffect()
{
    return cTSSequenceEffect::Create(this);
}

// 0xbf3930
void __cdecl cTSSequenceEntry::Read(void *stream, cTSSequenceEntry *e)
{
    GetVector2(stream, &e->mPosition);
    GetText(stream, &e->mName);
}

// 0xbf3a10
void __cdecl cTSSequenceEntry::Write(void *stream, const cTSSequenceEntry *e)
{
    PutVector2(stream, e->mPosition);
    PutText(stream, e->mName);
}

// 0xbf42b0. The count's temporary starts as 0 here; the original's is a stack word the caller left (a read that fails
// without writing the count is undefined there).
void __cdecl cTSSequenceDescription::Read(void *stream, uint32_t, cTSSequenceDescription *d)
{
    uint32_t count = 0;
    GetUint32(stream, &count);
    d->mEntries.resize(count);
    for (uint32_t i = 0; i < count; ++i) cTSSequenceEntry::Read(stream, &d->mEntries[i]);
    d->mFlags = GetMasked(stream, 0xf);
}

// 0xbf4550
void __cdecl cTSSequenceDescription::Write(void *stream, const cTSSequenceDescription *d)
{
    const uint32_t count = uint32_t(d->mEntries.size());
    PutUint32(stream, count);
    for (uint32_t i = 0; i < count; ++i) cTSSequenceEntry::Write(stream, &d->mEntries[i]);
    PutUint32(stream, d->mFlags);
}

// ---- model (type 10) ----

// 0xbf2d40 (slot 1)
cITSComponentEffect *cTSModelDescription::CreateEffect()
{
    return cTSModelEffect::Create(this);
}

// 0xbf2f50
void __cdecl cTSModelDescription::Read(void *stream, uint32_t, cTSModelDescription *d)
{
    GetText(stream, &d->mName);
    GetFloat(stream, &d->mFloat20);
    GetVector3(stream, &d->mVector24);
    GetFloat(stream, &d->mFloat30);
}

// 0xbf3260
void __cdecl cTSModelDescription::Write(void *stream, const cTSModelDescription *d)
{
    PutText(stream, d->mName);
    PutFloat(stream, d->mFloat20);
    PutVector3(stream, d->mVector24);
    PutFloat(stream, d->mFloat30);
}

// ---- camera (type 8, lane IW) ----

// 0xbf5700 (slot 1)
cITSComponentEffect *cTSCameraDescription::CreateEffect()
{
    return OriginalFunction<cITSComponentEffect *(__cdecl *)(cTSCameraDescription *), 0xc08b90>()(this);
}

// 0xbf6150. The table's byte is read over a temporary that starts as the description pointer's low byte (the original reads
// into its description argument's stack slot).
void __cdecl cTSCameraDescription::Read(void *stream, uint32_t, cTSCameraDescription *d)
{
    d->mFlags = GetMasked(stream, 0x3ffff);
    GetFloat(stream, &d->mLife);
    GetFloat(stream, &d->mFade);
    d->mAmplitude.Read(stream);
    d->mFrequency.Read(stream);
    GetFloat(stream, &d->mShakeAspect);
    uint8_t table = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(d));
    GetUint8(stream, &table);
    d->mTable = table;
    d->mHeading.Read(stream);
    d->mPitch.Read(stream);
    d->mRoll.Read(stream);
    d->mOrbit.Read(stream);
    d->mFov.Read(stream);
    d->mNearClip.Read(stream);
    d->mFarClip.Read(stream);
    GetSint8(stream, &d->mZoom);
    GetSint8(stream, &d->mRotate);
    GetFloat(stream, &d->mAttachRadius);
    GetText(stream, &d->mSelect);
}

// 0xbf65f0
void __cdecl cTSCameraDescription::Write(void *stream, const cTSCameraDescription *d)
{
    PutUint32(stream, d->mFlags);
    PutFloat(stream, d->mLife);
    PutFloat(stream, d->mFade);
    d->mAmplitude.Write(stream);
    d->mFrequency.Write(stream);
    PutFloat(stream, d->mShakeAspect);
    PutUint8(stream, static_cast<uint8_t>(d->mTable));
    d->mHeading.Write(stream);
    d->mPitch.Write(stream);
    d->mRoll.Write(stream);
    d->mOrbit.Write(stream);
    d->mFov.Write(stream);
    d->mNearClip.Write(stream);
    d->mFarClip.Write(stream);
    PutSint8(stream, d->mZoom);
    PutSint8(stream, d->mRotate);
    PutFloat(stream, d->mAttachRadius);
    PutText(stream, d->mSelect);
}

// ---- screen (type 11, lane IW) ----

// 0xbf7bf0 (slot 1)
cITSComponentEffect *cTSScreenDescription::CreateEffect()
{
    return OriginalFunction<cITSComponentEffect *(__cdecl *)(cTSScreenDescription *), 0xc09ee0>()(this);
}

// 0xbf8190. The mode's byte is read over a temporary that starts as the stream pointer's low byte. The flags and the count
// share one temporary: a count read that fails leaves the flags word (unmasked). That temporary starts as 0 here; the
// original's is an unset stack word (a flags read that fails is undefined there).
void __cdecl cTSScreenDescription::Read(void *stream, uint32_t, cTSScreenDescription *d)
{
    uint8_t mode = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(stream));
    GetUint8(stream, &mode);
    d->mMode = mode;
    uint32_t word = 0;
    GetUint32(stream, &word);
    d->mFlags = word & 3;
    GetUint32(stream, &word);
    const uint32_t count = word;
    d->mColors.resize(count);
    for (uint32_t i = 0; i < count; ++i) GetColor(stream, &d->mColors[i]);
    d->mStrength.Read(stream);
    GetFloat(stream, &d->mLength);
    GetFloat(stream, &d->mDelay);
    GetText(stream, &d->mTexture);
}

// 0xbf8550
void __cdecl cTSScreenDescription::Write(void *stream, const cTSScreenDescription *d)
{
    PutUint8(stream, static_cast<uint8_t>(d->mMode));
    PutUint32(stream, d->mFlags);
    const uint32_t count = static_cast<uint32_t>(d->mColors.size());
    PutUint32(stream, count);
    for (uint32_t i = 0; i < count; ++i) PutColor(stream, d->mColors[i]);
    d->mStrength.Write(stream);
    PutFloat(stream, d->mLength);
    PutFloat(stream, d->mDelay);
    PutText(stream, d->mTexture);
}

// ---- light (type 13, lane IW) ----

// 0xbf9140 (slot 1)
cITSComponentEffect *cTSLightDescription::CreateEffect()
{
    return cTSLightEffect::Create(this);
}

// 0xbf9280. The count's temporary starts as 0 here; the original's is the stack word its caller's ecx left (a count read
// that fails is undefined there).
void __cdecl cTSLightDescription::Read(void *stream, uint32_t, cTSLightDescription *d)
{
    d->mFlags = GetMasked(stream, 3);
    uint32_t count = 0;
    GetUint32(stream, &count);
    d->mColors.resize(count);
    for (uint32_t i = 0; i < count; ++i) GetColor(stream, &d->mColors[i]);
    d->mCurve18.Read(stream);
    d->mCurve28.Read(stream);
    GetFloat(stream, &d->mFloat38);
}

// 0xbf95a0
void __cdecl cTSLightDescription::Write(void *stream, const cTSLightDescription *d)
{
    PutUint32(stream, d->mFlags);
    const uint32_t count = static_cast<uint32_t>(d->mColors.size());
    PutUint32(stream, count);
    for (uint32_t i = 0; i < count; ++i) PutColor(stream, d->mColors[i]);
    d->mCurve18.Write(stream);
    d->mCurve28.Write(stream);
    PutFloat(stream, d->mFloat38);
}

// ---- decal (type 3, lane IW) ----

// 0xbec240 (slot 1)
cITSComponentEffect *cTSDecalDescription::CreateEffect()
{
    return cTSDecalEffect::Create(this);
}

// 0xbec8e0. The count's temporary starts as 0 here; the original's is a reused stack slot (a count read that fails is
// undefined there).
void __cdecl cTSDecalDescription::Read(void *stream, uint32_t, cTSDecalDescription *d)
{
    d->mFlags = GetMasked(stream, 0x7f);
    GetText(stream, &d->mTexture);
    GetUint8(stream, &d->mDraw);
    GetUint8(stream, &d->mLifeMode);
    GetFloat(stream, &d->mLife);
    d->mRotate.Read(stream);
    d->mSize.Read(stream);
    d->mAlpha.Read(stream);
    uint32_t count = 0;
    GetUint32(stream, &count);
    d->mColors.resize(count);
    for (uint32_t i = 0; i < count; ++i) GetColor(stream, &d->mColors[i]);
    d->mAspect.Read(stream);
    GetFloat(stream, &d->mAlphaVary);
    GetFloat(stream, &d->mSizeVary);
    GetFloat(stream, &d->mRotateVary);
    GetFloat(stream, &d->mRepeat);
    GetVector2(stream, &d->mOffset);
}

// 0xbecd10
void __cdecl cTSDecalDescription::Write(void *stream, const cTSDecalDescription *d)
{
    PutUint32(stream, d->mFlags);
    PutText(stream, d->mTexture);
    PutUint8(stream, d->mDraw);
    PutUint8(stream, d->mLifeMode);
    PutFloat(stream, d->mLife);
    d->mRotate.Write(stream);
    d->mSize.Write(stream);
    d->mAlpha.Write(stream);
    const uint32_t count = static_cast<uint32_t>(d->mColors.size());
    PutUint32(stream, count);
    for (uint32_t i = 0; i < count; ++i) PutColor(stream, d->mColors[i]);
    d->mAspect.Write(stream);
    PutFloat(stream, d->mAlphaVary);
    PutFloat(stream, d->mSizeVary);
    PutFloat(stream, d->mRotateVary);
    PutFloat(stream, d->mRepeat);
    PutVector2(stream, d->mOffset);
}

// The factories (0xbf8af0 brush, 0xbfa140 scrubber, 0xbf25d0 sound, 0xbfa3a0 game, 0xbf6f90 water, 0xbf4370 sequence,
// 0xbf3050 model).
template <class D, int32_t kType> cITSComponentDescription *__cdecl cTSCompDescriptionBaseT<D, kType>::Create()
{
    return new (std::nothrow) cTSCompDescriptionBaseT;
}

// The instances (their vtables and the members the hooks name).
template class cTSCompDescriptionBaseT<cTSBrushDescription, 4>;
template class cTSCompDescriptionBaseT<cTSScrubberDescription, 5>;
template class cTSCompDescriptionBaseT<cTSSoundDescription, 7>;
template class cTSCompDescriptionBaseT<cTSGameDescription, 9>;
template class cTSCompDescriptionBaseT<cTSWaterDescription, 12>;
template class cTSCompDescriptionBaseT<cTSSequenceDescription, 6>;
template class cTSCompDescriptionBaseT<cTSModelDescription, 10>;
template class cTSCompDescriptionBaseT<cTSCameraDescription, 8>;   // factory 0xbf6530
template class cTSCompDescriptionBaseT<cTSScreenDescription, 11>;  // factory 0xbf8360
template class cTSCompDescriptionBaseT<cTSLightDescription, 13>;   // factory 0xbf9370
template class cTSCompDescriptionBaseT<cTSDecalDescription, 3>;    // factory 0xbecb10
