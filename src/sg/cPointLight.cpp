// nGZSceneGraph::cPointLight (lane OT). Layout, faces and the switch: see the header.
#include "sg/cPointLight.h"

#include "game/game.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "misc/SmallVirtuals.h" // nGZEvent::cNullEvent::kVTable
#include "gz/cEventFaces.h"    // nGZEvent::NullEventVTable (the owned word)
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <cstring>

namespace nGZSceneGraph {

static_assert(sizeof(cPointLight) == cPointLight::kSize, "cPointLight size");
static_assert(offsetof(cPointLight, mFalloff) == 0x64, "cPointLight falloff");
static_assert(offsetof(cPointLight, mRange) == 0x68, "cPointLight range");
static_assert(offsetof(cPointLight, mStrength) == 0x6c, "cPointLight strength");
static_assert(offsetof(cPointLight, mShape) == 0x70, "cPointLight sphere");

// (The original's operator new throws on failure, so its null branch is dead.)
cIStandardLight *__cdecl cPointLight::Create() { return new cPointLight; }

void cPointLight::OnLightChanged()
{
    RadianceCutoff cutoff; // held to the end, as the original's local reference
    mRange = LightRange(mColor, mIntensity, mFalloff, cutoff.Value());
    mStrength = LightColorLength(mColor) * mIntensity * kStrengthScale;
    mShape.mData[0] = mRange;
    const void *nullEvent = nGZEvent::NullEventVTable();
    NotifyListeners(kBoundChangedEvent, 0, &nullEvent);
    cStandardLightBase::OnLightChanged();
}

void cPointLight::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *)
{
    cPointLight *const clone = new cPointLight;
    CopyContentTo(clone, mode);
    clone->QueryInterface(iid, out);
}

float cPointLight::StrengthOverBox(const float *box, AffineTransform3 *toLight)
{
    const float x0 = box[0], y0 = box[1], z0 = box[2], x1 = box[3], y1 = box[4], z1 = box[5];
    float corners[8][3] = {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z0}, {x0, y1, z1},
                           {x1, y0, z0}, {x1, y0, z1}, {x1, y1, z0}, {x1, y1, z1}};
    float strength[8];
    for (int i = 0; i < 8; ++i) {
        float local[3];
        toLight->TransformPoint(local, corners[i]);
        strength[i] = StrengthAt(local);
    }
    // maxss keeps its second operand unless the first is greater (so NaN loses to the running value, which is the second).
    float best = strength[1] > strength[0] ? strength[1] : strength[0];
    for (int i = 2; i < 8; ++i) best = strength[i] > best ? strength[i] : best;
    return best;
}

float cPointLight::StrengthAt(const float *offset)
{
    const float d2 = (offset[0] * offset[0] + offset[1] * offset[1]) + offset[2] * offset[2];
    if (!(mRange * mRange > d2)) return 0.0f;
    return mStrength / (d2 * mFalloff + 1.0f);
}

void cPointLight::SetFalloff(float value)
{
    if (mFalloff == value) return;
    mFalloff = value;
    OnLightChanged();
}

void cPointLight::SetRange(float value)
{
    if (mRange == value) return;
    mRange = value;
    mShape.mData[0] = mRange;
    const void *nullEvent = nGZEvent::NullEventVTable();
    NotifyListeners(kBoundChangedEvent, 0, &nullEvent);
    NotifyListeners(kChangedEvent, 0, &nullEvent);
}

void cPointLight::GetParams(float *out, uint32_t)
{
    out[0] = mColor[0] * mIntensity;
    out[1] = mColor[1] * mIntensity;
    out[2] = mColor[2] * mIntensity;
    out[3] = mFactor;
    out[4] = mFalloff;
    out[5] = mRange;
    out[6] = 0.0f;
    out[7] = 0.0f;
}

void cPointLight::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cStandardLightBase::Read(reader);
    mFalloff = reader->GetFloat32();
    mRange = reader->GetFloat32();
    OnLightChanged();
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cPointLight::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cPointLight");
    info.mWord4 = kClassId;
    info.mVersion = 5;
    return info;
}

void cPointLight::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cPointLight::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cStandardLightBase::Write(writer);
    writer->SetFloat32(mFalloff);
    writer->SetFloat32(mRange);
}

void cPointLight::CopyContentTo(cPointLight *target, uint32_t mode)
{
    CopyTo(target, mode);
    target->mFalloff = mFalloff;
    target->mRange = mRange;
    target->mStrength = mStrength;
    std::memcpy(target->mShape.mData, mShape.mData, sizeof(mShape.mData)); // 0x1920d0: 16 bytes from +4
}

} // namespace nGZSceneGraph
