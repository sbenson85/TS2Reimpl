// nGZSceneGraph::cSpotLight (lane OT). Layout, faces and the switch: see the header.
#include "sg/cSpotLight.h"

#include "game/game.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "misc/SmallVirtuals.h" // nGZEvent::cNullEvent::kVTable
#include "gz/cEventFaces.h"    // nGZEvent::NullEventVTable (the owned word)
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <cmath>
#include <cstring>

namespace nGZSceneGraph {

static_assert(sizeof(cSpotLight) == cSpotLight::kSize, "cSpotLight size");
static_assert(offsetof(cSpotLight, mFalloff) == 0x64, "cSpotLight falloff");
static_assert(offsetof(cSpotLight, mRange) == 0x68, "cSpotLight range");
static_assert(offsetof(cSpotLight, mConeAngle) == 0x6c, "cSpotLight cone angle");
static_assert(offsetof(cSpotLight, mConeFalloffAngle) == 0x70, "cSpotLight cone falloff angle");
static_assert(offsetof(cSpotLight, mStrength) == 0x74, "cSpotLight strength");
static_assert(offsetof(cSpotLight, mCosOuter) == 0x78, "cSpotLight outer cosine");
static_assert(offsetof(cSpotLight, mCosInner) == 0x7c, "cSpotLight inner cosine");
static_assert(offsetof(cSpotLight, mShape) == 0x80, "cSpotLight cone");

cIStandardLight *__cdecl cSpotLight::Create() { return new cSpotLight; }

void cSpotLight::OnLightChanged()
{
    RadianceCutoff cutoff;
    float falloffAngle = std::fabs(mConeFalloffAngle);
    float coneAngle = std::fabs(mConeAngle);
    mConeFalloffAngle = falloffAngle;
    mConeAngle = coneAngle;
    if (falloffAngle + coneAngle > kMaxAngleSum) {
        mConeAngle = kMaxAngleSum;
        mConeFalloffAngle = 0.0f;
        falloffAngle = 0.0f;
        coneAngle = kMaxAngleSum;
    }
    mRange = LightRange(mColor, mIntensity, mFalloff, cutoff.Value());
    mCosOuter = static_cast<float>(std::cos(static_cast<double>((falloffAngle + coneAngle) * 0.5f)));
    mCosInner = static_cast<float>(std::cos(static_cast<double>(coneAngle * 0.5f)));
    mStrength = LightColorLength(mColor) * mIntensity * kStrengthScale;
    mShape.Fit(mRange, mConeFalloffAngle, mConeAngle);
    const void *nullEvent = nGZEvent::NullEventVTable();
    NotifyListeners(kBoundChangedEvent, 0, &nullEvent);
    cStandardLightBase::OnLightChanged();
}

void cSpotLight::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *)
{
    cSpotLight *const clone = new cSpotLight;
    CopyContentTo(clone, mode);
    clone->QueryInterface(iid, out);
}

float cSpotLight::StrengthOverBox(const float *box, AffineTransform3 *toLight)
{
    const float x0 = box[0], y0 = box[1], z0 = box[2], x1 = box[3], y1 = box[4], z1 = box[5];
    float corners[9][3] = {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z0}, {x0, y1, z1}, {x1, y0, z0},
                           {x1, y0, z1}, {x1, y1, z0}, {x1, y1, z1}, {0.0f, 0.0f, 0.0f}};
    // The ninth point is the centre, built from the copy the corners were taken from.
    corners[8][0] = (x1 + x0) * 0.5f;
    corners[8][1] = (y0 + y1) * 0.5f;
    corners[8][2] = (z0 + z1) * 0.5f;
    float strength[9];
    for (int i = 0; i < 9; ++i) {
        float local[3];
        toLight->TransformPoint(local, corners[i]);
        strength[i] = StrengthAt(local);
    }
    float best = strength[1] > strength[0] ? strength[1] : strength[0];
    for (int i = 2; i < 9; ++i) best = strength[i] > best ? strength[i] : best;
    return best;
}

float cSpotLight::StrengthAt(const float *v)
{
    const float x = v[0];
    const float y = v[1];
    const float z = v[2];
    const float negZ = -z;
    const float d2 = (x * x + y * y) + z * z;
    if (!(d2 > 0.0f)) return 0.0f;
    if (!(mRange * mRange > d2)) return 0.0f;
    const float dist = static_cast<float>(std::sqrt(static_cast<double>(d2)));
    if (!(negZ > mCosOuter * dist)) return 0.0f;
    const float result = mStrength / (d2 * mFalloff + 1.0f);
    if (!(mCosInner * dist > negZ)) return result;
    return result * (((negZ / dist) - mCosOuter) / (mCosInner - mCosOuter));
}

void cSpotLight::SetFalloff(float value)
{
    if (mFalloff == value) return;
    mFalloff = value;
    OnLightChanged();
}

void cSpotLight::SetRange(float value)
{
    if (mRange == value) return;
    mRange = value;
    mShape.Fit(mRange, mConeFalloffAngle, mConeAngle);
    const void *nullEvent = nGZEvent::NullEventVTable();
    NotifyListeners(kBoundChangedEvent, 0, &nullEvent);
    NotifyListeners(kChangedEvent, 0, &nullEvent);
}

void cSpotLight::SetConeAngle(float value)
{
    if (mConeAngle == value) return;
    mConeAngle = value;
    OnLightChanged();
}

void cSpotLight::SetConeFalloffAngle(float value)
{
    if (mConeFalloffAngle == value) return;
    mConeFalloffAngle = value;
    OnLightChanged();
}

void cSpotLight::GetParams(float *out, uint32_t)
{
    out[0] = mColor[0] * mIntensity;
    out[1] = mColor[1] * mIntensity;
    out[2] = mColor[2] * mIntensity;
    out[3] = mFactor;
    out[4] = mFalloff;
    out[5] = mRange;
    out[6] = mConeAngle;
    out[7] = mConeFalloffAngle;
}

void cSpotLight::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cStandardLightBase::Read(reader);
    mFalloff = reader->GetFloat32();
    mConeAngle = reader->GetFloat32();
    mConeFalloffAngle = reader->GetFloat32();
    mRange = reader->GetFloat32();
    OnLightChanged();
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cSpotLight::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cSpotLight");
    info.mWord4 = kClassId;
    info.mVersion = 5;
    return info;
}

void cSpotLight::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cSpotLight::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cStandardLightBase::Write(writer);
    writer->SetFloat32(mFalloff);
    writer->SetFloat32(mConeAngle);
    writer->SetFloat32(mConeFalloffAngle);
    writer->SetFloat32(mRange);
}

void cSpotLight::CopyContentTo(cSpotLight *target, uint32_t mode)
{
    CopyTo(target, mode);
    target->mFalloff = mFalloff;
    target->mRange = mRange;
    target->mConeAngle = mConeAngle;
    target->mConeFalloffAngle = mConeFalloffAngle;
    target->mStrength = mStrength;
    target->mCosInner = mCosInner;
    target->mCosOuter = mCosOuter;
    std::memcpy(target->mShape.mData, mShape.mData, sizeof(mShape.mData)); // 0x193560: 20 bytes from +4
}

} // namespace nGZSceneGraph
