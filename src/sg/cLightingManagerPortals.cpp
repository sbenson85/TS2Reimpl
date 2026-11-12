// nTSSG::cLightingManager, the portal-light builders (lane 261109o, scenegraph LG4f): each sky sample's portal records
// coloured (PortalColors), then either turned into lerp lights and cPortalLight records (BuildPortalLights) or set on
// the records' nodes directly (ApplyPortalLights). See sg/cLightingManager.h.
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "compgeom/CompGeom.h"
#include "compgeom/ConvexMath.h"
#include "compgeom/Rigid.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"
#include "sg/SGLightInterfaces.h"
#include "sg/cIDiffusionLightExtension.h"
#include "sg/cLerpLightSet.h"
#include "sg/cLightRefNode.h"

#include <algorithm>
#include <vector>

namespace nTSSG {

namespace {

using nGZCompGeom::NearlyEqual;
using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;
using nGZSceneGraph::cILight;
using nGZSceneGraph::cILightRefNode;
using nGZSceneGraph::cIStandardLight;

constexpr const char *kPortalDirectName = "LM_PortalDirect";

cRZRandom &Random() { return *GameAddress<cRZRandom>(0x10d93f0); }

const float *ZeroVector() { return GameAddress<const float>(kZeroVector); }

// (value - target) * t + target, as the builders draw a colour towards the ambient one.
float Towards(float value, float target, float t) { return AddS(MulS(SubS(value, target), t), target); }

// A random strength factor in [1 - jitter, jitter + 1].
float Jitter(float jitter)
{
    return static_cast<float>(Random().RandomDoubleRangeUniform(static_cast<double>(SubS(1.0f, jitter)),
                                                                static_cast<double>(AddS(jitter, 1.0f))));
}

// The node's diffusion extension, if any (held keeps it), given a source scaled from its own: intensity * strength *
// weight, colour * `color`, falloff * `falloff`.
void ScaleDiffusionSource(cILightRefNode *node, void *&held, float strength, float weight, const float *color,
                          float falloff)
{
    DropHeld(held);
    if (!node->FindExtension(cIDiffusionLightExtension::kIid, &held, nullptr)) return;
    auto *const ext = static_cast<cIDiffusionLightExtension *>(held);
    ext->ResetFlags();
    const float intensity = ext->GetSourceIntensity();
    const float *const source = ext->GetSourceColor();
    const float sourceColor[3] = {source[0], source[1], source[2]};
    const float sourceFalloff = ext->GetSourceFalloff();
    ext->SetSourceIntensity(MulS(MulS(strength, intensity), weight));
    const float scaled[3] = {MulS(sourceColor[0], color[0]), MulS(sourceColor[1], color[1]),
                             MulS(sourceColor[2], color[2])};
    ext->SetSourceColor(scaled);
    ext->SetSourceFalloff(MulS(falloff, sourceFalloff));
}

// The record's cPortalLight with the colour times `color` * scale and the falloff times `falloff`, appended unless the
// colour is within 32 ulps of the game's zero vector.
void AppendPortalLight(std::vector<cPortalLight> *lights, const cPortalRecord &record, const float *color,
                       bool colorFirst, float scale, float falloff)
{
    cPortalLight light = record.mLight;
    for (int i = 0; i < 3; ++i) {
        const float tinted = colorFirst ? MulS(color[i], record.mLight.mColor[i]) : MulS(record.mLight.mColor[i], color[i]);
        light.mColor[i] = MulS(tinted, scale);
    }
    light.mFalloff = MulS(falloff, record.mLight.mFalloff);
    const float *const zero = ZeroVector();
    if (NearlyEqual(light.mColor[0], zero[0]) && NearlyEqual(light.mColor[1], zero[1]) &&
        NearlyEqual(light.mColor[2], zero[2]))
        return;
    lights->push_back(light);
}

} // namespace

void cLightingManager::PortalColors(int32_t sample, cPortalRecord *record, float *directFactors, float *directColor,
                                    float *directWeight, float *skyFactors, float *skyColor, float *skyWeight)
{
    const int16_t recordSample = record->mSample;
    const cLightSet *const set = mCurrentSet < mLightSets.size() ? &mLightSets[mCurrentSet] : &mDefaultSet;
    const cLightSet::Modifier &direct = set->mModifiers[record->mSample == 0 ? 1 : 0];
    const cLightSet::Modifier &sky = set->mModifiers[record->mSample == 0 ? 3 : 2];
    directFactors[0] = direct.a;
    directFactors[1] = direct.b;
    directFactors[2] = direct.c;
    directFactors[3] = direct.d;
    skyFactors[0] = sky.a;
    skyFactors[1] = sky.b;
    skyFactors[2] = sky.c;
    skyFactors[3] = sky.d;
    directFactors[0] = MulS(directFactors[0], record->mShade);
    skyFactors[0] = MulS(skyFactors[0], record->mShade);
    CopyZero(directColor);
    *directWeight = 1.0f;
    CopyZero(skyColor);
    *skyWeight = 0.0f;

    if (mPortalSky) {
        const bool skylight = ((set->mEnabled >> 7) & 1) != 0;
        if (static_cast<uint16_t>(sample) == 0 && recordSample != 0) {
            const float inward[3] = {Negated(record->mNormal[0]), Negated(record->mNormal[1]), 0.0f};
            if (skylight) {
                const float facing = static_cast<float>(nGZCompGeom::Dot87(inward, &set->mLights[0].mDirection.x));
                *directWeight = facing > 0.0f ? facing : 0.0f; // maxss: NaN gives 0
                const cSetLight &sun = set->mLights[0];
                const float intensity = sun.mIntensity;
                directColor[0] = MulS(intensity, sun.mColour.x);
                directColor[1] = MulS(sun.mColour.y, intensity);
                directColor[2] = MulS(sun.mColour.z, intensity);
                skyColor[0] = set->mAmbient.x;
                skyColor[1] = set->mAmbient.y;
                skyColor[2] = set->mAmbient.z;
                *skyWeight = 1.0f;
                if (1.0f > set->mPortalSaturation) {
                    const float normal[3] = {Negated(record->mNormal[0]), Negated(record->mNormal[1]), 0.0f};
                    float ambient[3] = {0.0f, 0.0f, 0.0f};
                    EvaluateAmbientScaled(normal, ambient);
                    *directWeight = Towards(*directWeight, 1.0f, set->mPortalSaturation);
                    for (int i = 0; i < 3; ++i)
                        directColor[i] = Towards(directColor[i], ambient[i], set->mPortalSaturation);
                    *skyWeight = Towards(*skyWeight, 1.0f, set->mPortalSaturation);
                    for (int i = 0; i < 3; ++i) skyColor[i] = Towards(skyColor[i], ambient[i], set->mPortalSaturation);
                }
            } else {
                EvaluateAmbientScaled(inward, directColor);
            }
            float lit[3];
            RawSlot<24, float *>(mLitObject, lit, record->mSegment); // receiver: the lit object (class not identified)
            for (int i = 0; i < 3; ++i) directColor[i] = AddS(lit[i], directColor[i]);
            for (int i = 0; i < 3; ++i) skyColor[i] = AddS(MulS(lit[i], 0.5f), skyColor[i]);
        } else {
            float buffer[3];
            // receiver: the lit object (class not identified): its colour at the segment
            const float *const lit = RawSlot<24, float *>(mLitObject, buffer, record->mSegment);
            for (int i = 0; i < 3; ++i) directColor[i] = MulS(lit[i], 0.5f);
            if (skylight) {
                Copy3(skyColor, directColor);
                *skyWeight = 1.0f;
            }
        }
    }

    for (int i = 0; i < 3; ++i)
        record->mColor[i] = AddS(MulS(directColor[i], *directWeight), MulS(skyColor[i], *skyWeight));
    if (mShadePortalColors)
        for (int i = 0; i < 3; ++i) record->mColor[i] = MulS(record->mShade, record->mColor[i]);
    if (record->mScaleDirect) *directWeight = MulS(mPortalDirectScale, *directWeight);
}

void cLightingManager::BuildPortalLights(std::vector<cPortalLight> *lights)
{
    const uint32_t group = mPortalGroups[2] | mPortalGroups[1] | mPortalGroups[0];
    float directFactors[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float skyFactors[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float directColor[3] = {0.0f, 0.0f, 0.0f};
    float skyColor[3] = {0.0f, 0.0f, 0.0f};
    float directWeight = 0.0f; // the original leaves both weights unset: PortalColors writes them before any read
    float skyWeight = 0.0f;

    for (uint32_t s = 0; s < SkySampleCount(); ++s) {
        cSkySample &sample = SkySample(s);
        if (!sample.mLerp88) {
            void *const memory = ::operator new(0x1c);
            cLerpLightSet *const made = memory ? static_cast<cLerpLightSet *>(memory)->Construct() : nullptr;
            cLerpLightSet *const old = sample.mLerp88.mSet;
            if (made != old) {
                if (made) RawSlot<2, void>(made); // receiver: nTSSG::cLerpLightSet (cRZRefCount AddRef)
                sample.mLerp88.mSet = made;
                if (old) RawSlot<1, void>(old); // receiver: nTSSG::cLerpLightSet (cRZRefCount Release)
            }
            SkySample(s).mLerp88->SetKeys(SampleKey(static_cast<int32_t>(s)), 0);
        }
        SkySample(s).mLerp88->ClearActive();
    }

    for (uint32_t s = 0; s < SkySampleCount(); ++s) {
        std::vector<cPortalRecord> *const records = &SkySample(s).mPortals;
        if (mPortalShadeAligned > 0.0f) ShadePortals(records);
        for (uint32_t i = 0; i < records->size(); ++i) {
            cPortalRecord *const record = &records->data()[i];
            PortalColors(static_cast<int32_t>(s), record, directFactors, directColor, &directWeight, skyFactors,
                         skyColor, &skyWeight);
            const float jitter = Jitter(mPortalJitter);
            if (record->mSample < 0 || static_cast<uint32_t>(record->mSample) >= SkySampleCount()) continue;

            auto *const node = static_cast<cILightRefNode *>(record->mObject.mObject);
            if (node->GetLight()) {
                cILight *const light = node->GetLight();
                cRZString text;
                text.Construct();
                cRZString *const name = text.Sprintf(kPortalDirectName);
                const float color[3] = {MulS(record->mTint[0], directColor[0]), MulS(record->mTint[1], directColor[1]),
                                        MulS(record->mTint[2], directColor[2])};
                cLerpLightSet *const lerp = SkySample(static_cast<uint32_t>(record->mSample)).mLerp88.mSet;
                const float value = MulS(directFactors[1], record->mValue);
                const float scale = MulS(MulS(MulS(directFactors[0], record->mStrength), directWeight), jitter);
                cIStandardLight *const standard = light->GetStandardLight(); // passed but not read
                lerp->AddExisting(group, name->ToChar(), node, reinterpret_cast<uint32_t>(standard), color, scale,
                                  value);
                text.Destruct();
            }
            AppendPortalLight(lights, *record, directColor, false, MulS(directFactors[2], directWeight),
                              directFactors[3]);

            {
                auto *const object = static_cast<cILightRefNode *>(records->data()[i].mObject.mObject);
                cRZString text;
                text.Construct();
                cRZString *const name = text.Sprintf(kPortalDirectName);
                const float direction[3] = {record->mNormal[0], record->mNormal[1], 0.0f};
                const float color[3] = {MulS(skyColor[0], record->mTint[0]), MulS(skyColor[1], record->mTint[1]),
                                        MulS(skyColor[2], record->mTint[2])};
                cLerpLightSet *const lerp = SkySample(static_cast<uint32_t>(record->mSample)).mLerp88.mSet;
                const float value = MulS(skyFactors[1], record->mValue);
                const float scale = MulS(MulS(MulS(skyFactors[0], record->mStrength), skyWeight), jitter);
                const nGZSceneGraph::cRigidTransform transform = object->GetWorldTransform();
                lerp->AddPoint(group, name->ToChar(), reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(&transform),
                               direction, record->mPosition, color, scale, value);
                text.Destruct();
            }
            AppendPortalLight(lights, *record, skyColor, true, MulS(skyFactors[2], skyWeight), skyFactors[3]);
        }
    }

    for (uint32_t s = 0; s < SkySampleCount(); ++s) SkySample(s).mLerp88->FadeOutUnused();
}

void cLightingManager::ApplyPortalLights()
{
    const cLightSet *const set = mCurrentSet < mLightSets.size() ? &mLightSets[mCurrentSet] : &mDefaultSet;
    const bool skylight = ((set->mEnabled >> 7) & 1) != 0;

    for (uint32_t s = 0; s < SkySampleCount(); ++s) {
        std::vector<cPortalRecord> *const records = &SkySample(s).mPortals;
        if (mPortalShadeAligned > 0.0f) ShadePortals(records);
        for (uint32_t i = 0; i < records->size(); ++i) {
            cPortalRecord *const record = &records->data()[i];
            const int16_t recordSample = record->mSample;
            const cLightSet::Modifier &directMod = set->mModifiers[recordSample == 0 ? 1 : 0];
            const cLightSet::Modifier &skyMod = set->mModifiers[recordSample == 0 ? 3 : 2];
            float direct[4] = {directMod.a, directMod.b, directMod.c, directMod.d};
            float sky[4] = {skyMod.a, skyMod.b, skyMod.c, skyMod.d};
            direct[0] = MulS(direct[0], record->mShade);
            sky[0] = MulS(sky[0], record->mShade);

            float directColor[3];
            float skyColor[3];
            CopyZero(directColor);
            CopyZero(skyColor);
            float directWeight = 1.0f;
            float skyWeight = 0.0f;

            if (mDebugPortals || ((set->mEnabled >> 11) & 1) != 0) {
                const int32_t k = static_cast<int32_t>(s) + 1;
                const float debug[3] = {static_cast<float>((k >> 2) & 1), static_cast<float>((k >> 1) & 1),
                                        static_cast<float>(k & 1)};
                Copy3(directColor, debug);
                Copy3(skyColor, debug);
                skyWeight = 1.0f;
            } else if (mPortalSky) {
                if (static_cast<uint16_t>(s) == 0 && recordSample != 0) {
                    const float inward[3] = {Negated(record->mNormal[0]), Negated(record->mNormal[1]), 0.0f};
                    if (skylight) {
                        const float facing =
                            static_cast<float>(nGZCompGeom::Dot87(inward, &set->mLights[0].mDirection.x));
                        directWeight = facing > 0.0f ? facing : 0.0f; // maxss: NaN gives 0
                        const cSetLight &sun = set->mLights[0];
                        const float intensity = sun.mIntensity;
                        directColor[0] = MulS(sun.mColour.x, intensity);
                        directColor[1] = MulS(sun.mColour.y, intensity);
                        directColor[2] = MulS(sun.mColour.z, intensity);
                        skyColor[0] = set->mAmbient.x;
                        skyColor[1] = set->mAmbient.y;
                        skyColor[2] = set->mAmbient.z;
                        skyWeight = 1.0f;
                        if (1.0f > set->mPortalSaturation) {
                            const float normal[3] = {Negated(record->mNormal[0]), Negated(record->mNormal[1]), 0.0f};
                            float ambient[3] = {0.0f, 0.0f, 0.0f};
                            EvaluateAmbientScaled(normal, ambient);
                            const float t = set->mPortalSaturation;
                            for (int c = 0; c < 3; ++c) directColor[c] = Towards(directColor[c], ambient[c], t);
                            directWeight = Towards(directWeight, 1.0f, t);
                            skyColor[0] = Towards(skyColor[0], ambient[0], t);
                            skyColor[1] = AddS(MulS(t, SubS(skyColor[1], ambient[1])), ambient[1]);
                            skyColor[2] = AddS(MulS(t, SubS(skyColor[2], ambient[2])), ambient[2]);
                            skyWeight = AddS(MulS(t, 0.0f), 1.0f); // (1 - 1) * t + 1, folded
                        }
                    } else {
                        EvaluateAmbientScaled(inward, directColor);
                    }
                    float lit[3];
                    RawSlot<24, float *>(mLitObject, lit, record->mSegment); // receiver: the lit object (class not identified)
                    directColor[0] = AddS(directColor[0], lit[0]);
                    directColor[1] = AddS(directColor[1], lit[1]);
                    directColor[2] = AddS(lit[2], directColor[2]);
                    for (int c = 0; c < 3; ++c) skyColor[c] = AddS(MulS(lit[c], 0.5f), skyColor[c]);
                } else {
                    float buffer[3];
                    // receiver: the lit object (class not identified): its colour at the segment
                    const float *const lit = RawSlot<24, float *>(mLitObject, buffer, record->mSegment);
                    for (int c = 0; c < 3; ++c) directColor[c] = MulS(lit[c], 0.5f);
                    if (skylight) {
                        Copy3(skyColor, directColor);
                        skyWeight = 1.0f;
                    }
                }
            }

            for (int c = 0; c < 3; ++c)
                record->mColor[c] = AddS(MulS(skyColor[c], skyWeight), MulS(directColor[c], directWeight));
            if (mShadePortalColors)
                for (int c = 0; c < 3; ++c) record->mColor[c] = MulS(record->mShade, record->mColor[c]);

            auto *const node = static_cast<cILightRefNode *>(record->mObject.mObject);
            void *held = nullptr;
            float original1 = 1.0f;
            const float *const grey = GameAddress<const float>(kGrey);
            float originalColor[3] = {grey[0], grey[1], grey[2]};
            float original2 = 1.0f;
            const float jitter = Jitter(mPortalJitter);

            if (node->GetLight()) {
                node->DropLightCopy();
                // The original leaves the scalar events' values unset (the node's GetAttribute writes them); ours zero.
                ScalarEvent one{GameAddress<const void>(kScalarEventVTable), 0.0f};
                VectorEvent color{GameAddress<const void>(kVectorEventVTable), {0.0f, 0.0f, 0.0f}};
                ScalarEvent value{GameAddress<const void>(kScalarEventVTable), 0.0f};
                GetAttribute(node, kAttrOne, &one);
                GetAttribute(node, kAttrColor, &color);
                GetAttribute(node, kAttrValue, &value);
                original1 = one.mValue;
                Copy3(originalColor, color.mValue);
                original2 = value.mValue;
                one.mValue = MulS(MulS(MulS(directWeight, direct[0]), jitter), original1);
                for (int c = 0; c < 3; ++c) color.mValue[c] = MulS(color.mValue[c], directColor[c]);
                value.mValue = MulS(direct[1], original2);
                SetAttribute(node, kAttrOne, &one);
                SetAttribute(node, kAttrColor, &color);
                SetAttribute(node, kAttrValue, &value);
            }
            ScaleDiffusionSource(node, held, direct[2], directWeight, directColor, direct[3]);

            auto *const other = static_cast<cILightRefNode *>(record->mOther.mObject);
            if (other) {
                if (other->GetLight()) {
                    cIStandardLight *const light = other->GetLight()->GetStandardLight();
                    if (light) {
                        light->SetIntensity(MulS(MulS(MulS(original1, sky[0]), skyWeight), jitter));
                        const float color[3] = {MulS(originalColor[0], skyColor[0]), MulS(originalColor[1], skyColor[1]),
                                                MulS(originalColor[2], skyColor[2])};
                        light->SetColor(color);
                        light->SetFalloff(MulS(sky[1], original2));
                        light->SetFactor(0.0f);
                    }
                }
                ScaleDiffusionSource(other, held, sky[2], skyWeight, skyColor, sky[3]);
            }
            if (held) ReleaseThroughBase(held);
        }
    }
}

} // namespace nTSSG
