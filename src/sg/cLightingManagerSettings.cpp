// nTSSG::cLightingManager::ReadRoomSettings 0x2e3710 (lane 261110p, scenegraph LG3d). See sg/cLightingManager.h.
//
// The three property readers, ours (sg/SGUtilities.h, lane 261111b), all cdecl (parser, name, member, found):
//   0xaf7190 float (116 bytes): 0 and nothing written when the parser is null, the lookup fails or the value does not
//            evaluate; else 4 bytes written, *found = (new != old) when found is set, and 1.
//   0xaf7210 int (119 bytes): the same, the 4 bytes the value rounded to an integer.
//   0xaf7110 bool (114 bytes): the same, ONE byte written: 1 when the value differs from 0.0 (NaN too), else 0.
// The result is tested through al only (the callers' `test al, al`).
#include "sg/cLightingManager.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h" // gzany::kFloat32VTable
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "sg/SGUtilities.h"

#include <cstring>

namespace nTSSG {

namespace {


constexpr int kSectionEnter = 20; // the parser's slot 20 (name), 21 leaves the section
constexpr int kSectionLeave = 21;
constexpr uint32_t kSettingsClass = 0x0ac1887e; // the framework's settings object (slot 5: class, iid, out)
constexpr uint32_t kSettingsIid = 0x4bb0d269;
constexpr int kFrameworkGetService = 5;
constexpr int kSettingsSet = 3;        // the settings object's SetProperty(key, any)
constexpr uint32_t kCutoffKey = 0xdb6090; // "lightRadianceRangeCutoff"
constexpr float kDefaultCutoff = 0.25f;   // 0x3e800000, kept when the reader writes nothing

// The reads' state: `acc` is whether any earlier read found a change; each reader gets it as its `found` byte on
// entry (the value is only observable through that), and the reader may overwrite it.
struct Reads {
    void *mParser;
    bool mAcc = false;

    template <class Fn, class T> bool Read(Fn reader, const char *name, T *member)
    {
        uint8_t found = mAcc ? 1 : 0;
        const bool ok = reader(mParser, name, member, &found) != 0;
        mAcc = found != 0 || mAcc;
        return ok;
    }
    bool Float(const char *name, float *member) { return Read(&nGZSceneGraph::ReadFloatProperty, name, member); }
    bool Int(const char *name, int32_t *member) { return Read(&nGZSceneGraph::ReadIntProperty, name, member); }
    bool Bool(const char *name, uint8_t *member) { return Read(&nGZSceneGraph::ReadBoolProperty, name, member); }
};

// One lighting section: the 12 room* names in the cLightConfig's order but for the type (an int) read eighth.
void ReadSection(Reads &r, const char *section, cLightConfig &c)
{
    RawSlot<kSectionEnter, uint32_t>(r.mParser, section);
    r.Float("roomOverallStrength", &c.mBrightness);
    r.Float("roomBounceStrength", &c.mVerticalScale);
    r.Float("roomBounceCrossover", &c.mSideSum);
    r.Float("roomSideStrength", &c.mSideScale);
    r.Float("roomSideCrossover", &c.mUpToSides);
    r.Float("roomSideSlope", &c.mTilt);
    r.Float("roomSpecular", &c.mFactor);
    r.Int("roomLightingType", reinterpret_cast<int32_t *>(&c.mType));
    r.Float("roomFromLightMapMix", &c.mLightMapWeight);
    r.Float("roomFromLightMapStrength", &c.mLightMapScale);
    r.Float("roomDirectionalBoost", &c.mDeltaIntensity);
    r.Float("roomCrossBias", &c.mAdjust);
    RawSlot<kSectionLeave, uint32_t>(r.mParser);
}

} // namespace

void cLightingManager::ReadRoomSettings(void *parser)
{
    Reads r{parser};
    r.Float("roomSaturation", &mField1D4);
    r.Float("roomCeilingStrength", &mField1CC);
    r.Float("roomPortalStrength", &mPortalScale);
    ReadSection(r, "sim", mSimSettings.mConfig);
    ReadSection(r, "object", mObjectSettings.mConfig);
    ReadSection(r, "wall", mWallSettings.mConfig);
    r.Bool("thresholdLightsByArea", &mThresholdLightsByArea);
    r.Float("portalSuppressionRadius", &mPortalReach);
    r.Float("portalSuppressionAmount", &mPortalShadeAligned);
    if (!r.Float("portalCornerSuppressionAmount", &mPortalShadeCrossed))
        std::memcpy(&mPortalShadeCrossed, &mPortalShadeAligned, sizeof(float)); // 0x2e3f11: a 32-bit copy
    r.Bool("portalSuppressionAffectsRoomLighting", &mShadePortalColors);
    r.Float("hmLotShadowStrength", &mBlendScale240);
    r.Float("hmNHoodShadowStrength", &mShadowBlendScale);
    r.Float("hmSunLightTransitionZone", &mSunFalloff);
    r.Float("hmObjectShadowingBias", &mSunLightValue);
    r.Float("hmBinaryShadowThreshold", reinterpret_cast<float *>(&mField64));
    r.Float("outdoorStrength", &mAmbientScale1EC);
    r.Float("terrainStrength", &mAmbientScale1F0);
    r.Bool("wallDrivenPortals", &mWallDrivenPortals);
    r.Bool("morningEvening", &mMorningEvening);
    r.Float("objectLightJitter", &mObjectLightJitter);
    r.Float("portalLightJitter", &mPortalJitter);
    r.Float("spillPlaneOffset", &mPortalLightOffset);
    r.Float("shadowStrengthLimit", &mShadeFactor7C);
    r.Float("hmBlobHeightModifier", &mSampleScale);
    r.Float("hmBlobWidthModifier", &mSampleMargin);
    r.Bool("incrementalUpdates", &mField87);
    r.Float("objectLightTransitionTime", &mField508);
    r.Float("doorOrArchDirectMultiplier", &mPortalDirectScale);
    r.Float("minimalLightingDirectionalMix", &mFlatSunFraction);

    // lightRangeCutoff, passed on to the framework's settings object as lightRadianceRangeCutoff.
    ScopedRef settings{nullptr};
    if (void *framework = GetServiceObject())
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **), kFrameworkGetService>(
            framework, kSettingsClass, kSettingsIid, &settings.mObject);
    float cutoff = kDefaultCutoff;
    if (r.Float("lightRangeCutoff", &cutoff)) {
        AnyFloat32 value{GameAddress<const void>(gzany::kFloat32VTable), cutoff};
        GZServiceString key(GameAddress<const char>(kCutoffKey));
        // ORIGINAL-BUG: a missing settings object is called through (the original does the same).
        VirtualCall<uint32_t(__thiscall *)(void *, GZServiceString *, AnyFloat32 *), kSettingsSet>(settings.mObject, &key,
                                                                                                    &value);
    }
    settings.ReleaseObject();
}

} // namespace nTSSG
