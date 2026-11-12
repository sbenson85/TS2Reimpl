// The light set's light nodes (lane 261111n, scenegraph LG4d leftovers): 0xb3f770 and 0xb3f770's worker 0xb3f7f0, which
// turn a light set's enabled parts into light-reference nodes for cLightingManager::BuildSunHeightMapLights (0x2cfd60).
// See sg/cLightingManager.h.
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "rz/cRZString.h"
#include "sg/SGLightInterfaces.h"
#include "sg/SGUtilities.h"
#include "sg/cLightRefNode.h"

#include <vector>

namespace nTSSG {

namespace {

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::SubS;
using nGZSceneGraph::cILinearFogLight;
using nGZSceneGraph::cIStandardLight;
using nGZSceneGraph::cITextureLight;

constexpr uint32_t kWhite = 0x10fb040;               // the game's white (1, 1, 1): the lights' starting offset
constexpr uint32_t kDirectionalLightClass = 0xc9c81ba3;
constexpr uint32_t kFogLightClass = 0xac06a66f;
constexpr uint32_t kTextureLightClass = 0x6a97042f;

// The kinds the builder reads back (0..6 the set's lights by index).
constexpr int32_t kKindSkylight = 7;
constexpr int32_t kKindFog = 8;
constexpr int32_t kKindEnvironment = 9;
constexpr int32_t kKindParticles = 10;

bool Enabled(const cLightSet *set, uint32_t bit) { return ((set->mEnabled >> bit) & 1) != 0; }

void Tag(const cBuiltRef &node, const char *name)
{
    nGZSceneGraph::SetNodeTag(static_cast<nGZSceneGraph::cICompositionTreeNode *>(node.mObject), name);
}

void Append(const cBuiltRef &node, int32_t kind, std::vector<cBuiltRef> *lights, std::vector<int32_t> *kinds)
{
    lights->push_back(node);
    if (kinds) kinds->push_back(kind);
}

// A texture light (the environment's and the particles') of colour (1, rgb) on a new node.
void TextureLight(cBuiltRef &node, cBuiltRef &light, const cLightVector &rgb)
{
    MakeNode(node);
    Make(light, kTextureLightClass, cITextureLight::kIid);
    auto *const texture = static_cast<cITextureLight *>(light.mObject);
    AsNode(node)->SetLight(texture);
    const float colour[4] = {1.0f, rgb.x, rgb.y, rgb.z};
    texture->SetColor(0, colour);
}

} // namespace

void __cdecl BuildLightSetNodes(const cLightSet *set, std::vector<cBuiltRef> *lights, std::vector<int32_t> *kinds)
{
    if (!lights) return;
    // Released in order, each cell left as it was (the references' destructors); the kinds are kept (the caller passes
    // them empty).
    lights->clear();
    AppendLightSetNodes(set, lights, kinds);
}

void __cdecl AppendLightSetNodes(const cLightSet *set, std::vector<cBuiltRef> *lights, std::vector<int32_t> *kinds)
{
    const float *const white = GameAddress<const float>(kWhite);
    float offset[3] = {white[0], white[1], white[2]};

    if (Enabled(set, 7)) {
        cBuiltRef light;
        Make(light, kAmbientLightClass, cIStandardLight::kIid);
        cBuiltRef node;
        MakeNode(node);
        auto *const ambient = static_cast<cIStandardLight *>(light.mObject);
        AsNode(node)->SetLight(ambient);
        ambient->SetColor(&set->mAmbient.x);
        ambient->SetIntensity(1.0f);
        Tag(node, "LM_Outdoor_Ambient");
        Append(node, kKindSkylight, lights, kinds);
        const cLightVector sky = set->mAmbient;
        offset[0] = AddS(offset[0], sky.x);
        offset[1] = AddS(offset[1], sky.y);
        offset[2] = AddS(offset[2], sky.z);
    }

    for (int32_t i = 0; i < 7; ++i) {
        if (!Enabled(set, uint32_t(i))) continue;
        const cSetLight &source = set->mLights[i];
        cBuiltRef light;
        Make(light, kDirectionalLightClass, cIStandardLight::kIid);
        cBuiltRef node;
        MakeNode(node);
        auto *const directional = static_cast<cIStandardLight *>(light.mObject);
        AsNode(node)->SetLight(directional);
        const float towards[3] = {-source.mDirection.x, -source.mDirection.y, -source.mDirection.z};
        nGZSceneGraph::AimNode(static_cast<nGZSceneGraph::cITransformNode *>(node.mObject), towards);
        const cLightVector colour = source.mColour;
        const float shifted[3] = {SubS(colour.x, offset[0]), SubS(colour.y, offset[1]), SubS(colour.z, offset[2])};
        directional->SetColor(shifted);
        directional->SetIntensity(source.mIntensity);
        {
            cRZString name;
            Tag(node, name.Sprintf("LM_Outdoor_Dir_%i", i)->ToChar());
        }
        Append(node, i, lights, kinds);
    }

    if (Enabled(set, 8)) {
        cBuiltRef node;
        MakeNode(node);
        cBuiltRef light;
        Make(light, kFogLightClass, cILinearFogLight::kIid);
        auto *const fog = static_cast<cILinearFogLight *>(light.mObject);
        AsNode(node)->SetLight(fog);
        fog->SetColor(&set->mFogColour.x);
        fog->SetNearFar(set->mFogNear, set->mFogFar);
        Tag(node, "LM_Fog");
        Append(node, kKindFog, lights, kinds);
    }

    if (Enabled(set, 9)) {
        cBuiltRef node;
        cBuiltRef light;
        TextureLight(node, light, set->mVector174);
        Tag(node, "LM_Environment");
        Append(node, kKindEnvironment, lights, kinds);
    }

    if (Enabled(set, 10)) {
        cBuiltRef node;
        cBuiltRef light;
        TextureLight(node, light, set->mParticles);
        Tag(node, "LM_Particles");
        AsNode(node)->RemoveLayerBits(0xffffffffu);
        AsNode(node)->AddLayer("Particles");
        Append(node, kKindParticles, lights, kinds);
    }
}

} // namespace nTSSG
