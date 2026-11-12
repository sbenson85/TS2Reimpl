// nTSSG::cLightingManager, the top light builders (lane 261109p, scenegraph LG4d): the current set's lights with the
// sun's height-map shadow, the rooms' config and environment lights, and the flat outdoor / indoor lights. See
// sg/cLightingManager.h.
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/SGLightInterfaces.h"
#include "sg/SGUtilities.h"
#include "sg/cLerpLightSet.h"
#include "sg/cLightRefNode.h"
#include "sg/nTSSGLights.h"

#include <string>
#include <vector>

namespace nTSSG {

namespace {

using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;
using nGZSceneGraph::cILight;
using nGZSceneGraph::cILightRefNode;
using nGZSceneGraph::cIStandardLight;
using nGZSceneGraph::cITextureLight;

constexpr uint32_t kNoFlatLights = 0xffde34;  // set: BuildFlatOutdoorLights adds no light
constexpr uint32_t kOne = 0xd9c698;           // the game's 1.0f (the flat lights' 1 - mFlatSunFraction)

constexpr uint32_t kTextureLightClass = 0x6a97042f; // a director's class (with sg/cLightingManagerDetail.h's)

// The light kinds BuildLightSetNodes (0xb3f770) gives for the set's lights (the ones the builder tells apart).
constexpr int32_t kKindSun = 0;
constexpr int32_t kKindAll = 8;
constexpr int32_t kKindEnvironment = 9;

void SetNodeName(void *node, const char *name)
{
    nGZSceneGraph::SetNodeTag(static_cast<nGZSceneGraph::cICompositionTreeNode *>(node), name);
}

// A light the constructor at `constructor` builds in `size` bytes of new memory; its standard-light face (+0x2c), or
// null when there was no memory.
template <class Construct> cIStandardLight *NewLight(size_t size, Construct construct)
{
    void *const memory = ::operator new(size);
    uint8_t *const made = memory ? construct(memory) : nullptr;
    return made ? reinterpret_cast<cIStandardLight *>(made + 0x2c) : nullptr;
}

void AddToScene(cLightingManager *manager, void *node)
{
    RawSlot<7, void>(manager->mSceneManager, node); // receiver: nTSSG::cSceneManager vf7 0x2bde50
}

} // namespace

void cLightingManager::BuildSunHeightMapLights()
{
    const cLightSet &set = CurrentSet();
    if (Switch(kRefreshSwitch)) {
        mLerp228->ClearActive();
        const uint32_t sunKind = (mSunMode == 1 && mShadeTiles && mSunHMLight) ? 3u : 2u;
        // The game also passes mField64, which BuildRenderGroupLights does not read.
        cLerpLightSet::BuildRenderGroupLights(&set, mLerp228, sunKind, mSunLightValue);
        if (mSunMode == 2)
            mLerp228->AddLight(mShadeGroup, "LM_OutdoorShade", 7, GameAddress<const float>(kGrey), 1.0f, 0.0f, 0.0f);
        mLerp228->FadeOutUnused();
        return;
    }

    std::vector<int32_t> kinds;
    BuildLightSetNodes(&set, &mBuiltLights, &kinds);
    const uint32_t enabled = set.mEnabled;
    const bool indoor = ((enabled >> 4) & 1) || ((enabled >> 5) & 1) || ((enabled >> 6) & 1);
    if (!mBuiltLights.empty()) {
        const uint8_t lightFlag = mSunMode == 2 ? 2 : 0;
        for (uint32_t i = 0; i < mBuiltLights.size(); ++i) {
            cILightRefNode *const node = AsNode(mBuiltLights[i]);
            const int32_t kind = kinds[i];
            node->SetLayerTag(SampleKey(0));
            node->SetLightFlag(lightFlag);
            if (kind == kKindAll) {
                node->AddLayerBits(0xffffffffu);
            } else if (kind == kKindEnvironment) {
                node->AddLayerBits(mEnvironmentGroup);
            } else {
                if (static_cast<uint32_t>(kind - 4) <= 2) {
                    node->RemoveLayerBits(1);
                    node->AddLayerBits(mPortalGroups[0]);
                } else {
                    uint32_t bits = mOtherGroup | mPortalGroups[2] | mPortalGroups[1];
                    if (!indoor || static_cast<uint32_t>(kind - 2) > 1) bits |= mPortalGroups[0];
                    node->AddLayerBits(bits);
                }
                if (kind == kKindSun && mSunMode == 1 && mShadeTiles && mSunHMLight) {
                    // The sun becomes a height-map shadow light of the old light's standard colour and intensity.
                    cIStandardLight *const face = NewLight(0x80, [&](void *memory) {
                        using Constructor = uint8_t *(__thiscall *)(void *, void *, float, uint32_t);
                        return reinterpret_cast<Constructor>(const_cast<uint8_t *>(
                            &cHeightMapShadowLight_Constructor))(memory, this, mSunLightValue, 1u);
                    });
                    face->SetLightName("LM_sunHM");
                    cBuiltRef standard;
                    cILight *const old = node->GetLight();
                    if (old->QueryInterface(cIStandardLight::kIid, &standard.mObject)) {
                        auto *const from = static_cast<cIStandardLight *>(standard.mObject);
                        float color[3];
                        face->SetColor(from->GetColor(color));
                        face->SetIntensity(from->GetIntensity());
                    }
                    node->SetLight(face);
                }
            }
            AddToScene(this, node);
        }
    }

    if (mSunMode == 2) {
        cBuiltRef node;
        MakeNode(node);
        cIStandardLight *const face = NewLight(0x58, [&](void *memory) {
            using Constructor = uint8_t *(__thiscall *)(void *, void *, uint32_t);
            return reinterpret_cast<Constructor>(const_cast<uint8_t *>(&cTerrainShadeLight_Constructor))(memory, this,
                                                                                                         1u);
        });
        AsNode(node)->SetLight(face);
        AsNode(node)->RemoveLayerBits(0xffffffffu);
        AsNode(node)->AddLayerBits(mShadeGroup);
        AddToScene(this, node.mObject);
        mBuiltLights.push_back(node);
    }
}

void cLightingManager::BuildRoomLights()
{
    cRZString name;
    name.Construct();
    for (uint32_t i = 0; i < mSkySamples.size(); ++i) {
        const uint16_t room = static_cast<uint16_t>(i);
        if (room == 0) continue;
        // receiver: nTSSG::cLightingManager (vf36 LotValue 0x2d9480)
        const uint32_t value = RawSlot<36, uint32_t>(this, static_cast<uint32_t>(room));
        if (static_cast<int16_t>(value) != 100) continue;
        name.Sprintf("LM_Room%d", i);
        cSkySample &sample = mSkySamples.data()[i];
        const uint32_t key = SampleKey(room);
        if (Switch(kRefreshSwitch)) {
            if (!sample.mLerp78) {
                void *const memory = ::operator new(0x1c);
                cLerpLightSet *const made = memory ? static_cast<cLerpLightSet *>(memory)->Construct() : nullptr;
                cLerpLightSet *const old = sample.mLerp78.mSet;
                if (made != old) {
                    if (made) RawSlot<2, void>(made); // receiver: nTSSG::cLerpLightSet (cRZRefCount AddRef)
                    sample.mLerp78.mSet = made;
                    if (old) RawSlot<1, void>(old); // receiver: nTSSG::cLerpLightSet (cRZRefCount Release)
                }
                sample.mLerp78->SetKeys(key, 0);
            }
            if (Switch(kRefreshSwitch)) sample.mLerp78->ClearActive();
        }
        const uint8_t *const self = reinterpret_cast<const uint8_t *>(this);
        CreateConfigLight(&sample, reinterpret_cast<const cLightConfig *>(self + 0xdc), key, mPortalGroups[1], &name);
        CreateConfigLight(&sample, reinterpret_cast<const cLightConfig *>(self + 0x12c), key, mPortalGroups[0], &name);
        CreateConfigLight(&sample, reinterpret_cast<const cLightConfig *>(self + 0x17c), key, mPortalGroups[2], &name);

        float average[3];
        if (!Switch(kRefreshSwitch)) {
            cBuiltRef node;
            MakeNode(node);
            cBuiltRef texture;
            Make(texture, kTextureLightClass, cITextureLight::kIid);
            auto *const light = static_cast<cITextureLight *>(texture.mObject);
            AsNode(node)->SetLight(light);
            const float *const a = AverageOfSix(average, sample.mVectors[0]);
            const float color[4] = {1.0f, a[0], a[1], a[2]};
            light->SetColor(0, color);
            AsNode(node)->RemoveLayerBits(0xffffffffu);
            AsNode(node)->AddLayerBits(mEnvironmentGroup);
            AsNode(node)->SetLayerTag(key);
            {
                const std::string text = name.mString + "Env";
                SetNodeName(node.mObject, text.c_str());
            }
            sample.mLights.push_back(node);
            AddToScene(this, node.mObject);
        } else {
            const std::string text = name.mString + "Environment";
            sample.mLerp78->AddLight(mEnvironmentGroup, text.c_str(), 8, AverageOfSix(average, sample.mVectors[0]),
                                     1.0f, 0.0f, 0.0f);
        }
        if (Switch(kRefreshSwitch)) sample.mLerp78->FadeOutUnused();
    }
    name.Destruct();
}

void cLightingManager::BuildFlatOutdoorLights(uint8_t lerp)
{
    mLerp22C->ClearActive();
    mLerp230->ClearActive();
    if (!Switch(kRefreshSwitch) || !lerp) {
        if (!Switch(kNoFlatLights)) {
            cBuiltRef node;
            MakeNode(node);
            cBuiltRef light;
            Make(light, kAmbientLightClass, cIStandardLight::kIid);
            auto *const ambient = static_cast<cIStandardLight *>(light.mObject);
            ambient->SetColor(&CurrentSet().mVector19C.x);
            ambient->SetIntensity(1.0f);
            AsNode(node)->SetLight(ambient);
            AsNode(node)->SetLightFlag(2);
            mBuiltLights.push_back(node);
            AddToScene(this, node.mObject);
        }
    } else if (!Switch(kNoFlatLights)) {
        const cLightSet &set = CurrentSet();
        const float sun = mFlatSunFraction;
        const float rest = SubS(*GameAddress<const float>(kOne), sun);
        const float *const color = &set.mVector19C.x;
        const uint32_t sims = mPortalGroups[0];
        const uint32_t objects = mPortalGroups[1] | mPortalGroups[2] | 1u;
        const uint32_t other = mOtherGroup;

        mLerp22C->AddLight(sims, "LM_FlatOutdoorSims", 0, color, MulS(rest, set.mSimsScale), 0.0f, 0.0f);
        mLerp22C->AddLight(objects, "LM_FlatOutdoorObjects", 0, color, MulS(rest, set.mObjectsScale), 0.0f, 0.0f);
        mLerp22C->AddLight(other, "LM_FlatOutdoorOther", 0, color, rest, 0.0f, 0.0f);
        if (sun > 0.0f) {
            const cLightVector &d = set.mLights[0].mDirection;
            const float down[3] = {Negated(d.x), Negated(d.y), Negated(d.z)};
            mLerp22C->AddDirectional(sims, "LM_FlatOutdoorSimsDir", 2, color, MulS(sun, set.mSimsScale), down, 0.0f,
                                     0.0f);
            mLerp22C->AddDirectional(objects, "LM_FlatOutdoorObjectsDir", 2, color, MulS(sun, set.mObjectsScale), down,
                                     0.0f, 0.0f);
            mLerp22C->AddDirectional(other, "LM_FlatOutdoorOtherDir", 2, color, sun, down, 0.0f, 0.0f);
        }

        const float indoor = set.mIndoorScale;
        mLerp230->AddLight(sims, "LM_FlatIndoorSims", 0, color, MulS(MulS(rest, set.mSimsScale), indoor), 0.0f, 0.0f);
        mLerp230->AddLight(objects, "LM_FlatIndoorObjects", 0, color, MulS(MulS(rest, set.mObjectsScale), indoor), 0.0f,
                           0.0f);
        mLerp230->AddLight(other, "LM_FlatIndoorOther", 0, color, MulS(rest, indoor), 0.0f, 0.0f);
        if (sun > 0.0f) {
            const cLightVector &d = set.mLights[1].mDirection;
            const float down[3] = {Negated(d.x), Negated(d.y), Negated(d.z)};
            mLerp230->AddDirectional(sims, "LM_FlatIndoorSimsDir", 2, color, MulS(MulS(sun, set.mSimsScale), indoor),
                                     down, 0.0f, 0.0f);
            mLerp230->AddDirectional(objects, "LM_FlatIndoorObjectsDir", 2, color,
                                     MulS(MulS(sun, set.mObjectsScale), indoor), down, 0.0f, 0.0f);
            mLerp230->AddDirectional(other, "LM_FlatIndoorOtherDir", 2, color, MulS(sun, indoor), down, 0.0f, 0.0f);
        }
    }
    mLerp22C->FadeOutUnused();
    mLerp230->FadeOutUnused();
}

} // namespace nTSSG
