// nTSSG::cLightingManager, the config light creators (lane 261109m, scenegraph LG4c): the dispatcher and the ambient,
// bounce and min-side creators that turn a sky sample's six vectors into light-reference nodes. See
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

#include <algorithm>
#include <cmath>
#include <string>

namespace nTSSG {

namespace {

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;
using nGZSceneGraph::cILightRefNode;
using nGZSceneGraph::cIStandardLight;

constexpr uint32_t kBounceX = 0xffd900;       // the "PosX" / "NegX" bounce axis
constexpr uint32_t kBounceY = 0xffd90c;       // the "PosY" / "NegY" bounce axis
constexpr uint32_t kDirections = 0x10d93a8;   // a direction per sample vector (12 bytes each)
constexpr uint32_t kAdjustFirst = 0xdb5d70;   // int32 per vector: the vector CreateAmbientLight's adjustment weighs
constexpr uint32_t kAdjustSecond = 0xdb5d88;  // int32 per vector: the one it is weighed against
constexpr uint32_t kMinSideOne = 0xd9c698;    // the min-side direction's numerator (1)
// The bounce lights' fixed colours, light 0 to 4 (mFixedBounce).
constexpr uint32_t kFixedColors[5] = {0xffd9a0, 0xffd9ac, 0xffd9b8, 0xffd9dc, 0xffd9d0};

constexpr uint32_t kDirectionalLightClass = 0xc9c81ba3; // a director's class (with sg/cLightingManagerDetail.h's)

const float *GameVector(uint32_t rva) { return GameAddress<const float>(rva); }

void SetNodeName(void *node, const char *name)
{
    nGZSceneGraph::SetNodeTag(static_cast<nGZSceneGraph::cICompositionTreeNode *>(node), name);
}
void SetNodeDirection(void *node, const float *direction)
{
    nGZSceneGraph::AimNode(static_cast<nGZSceneGraph::cITransformNode *>(node), direction);
}

cIStandardLight *AsLight(const cBuiltRef &ref) { return static_cast<cIStandardLight *>(ref.mObject); }

void MakeLight(cBuiltRef &light, uint32_t classId) { Make(light, classId, cIStandardLight::kIid); }

void Tag(cILightRefNode *node, uint32_t tag, uint32_t bits)
{
    node->SetLayerTag(tag);
    node->RemoveLayerBits(0xffffffffu);
    node->AddLayerBits(bits);
}

// A directional light's node: tagged, given its light and direction.
void SetUpDirectional(const cBuiltRef &node, const cBuiltRef &light, uint32_t tag, uint32_t bits,
                      const float *direction)
{
    Tag(AsNode(node), tag, bits);
    AsNode(node)->SetLight(AsLight(light));
    SetNodeDirection(node.mObject, direction);
}

void NameNode(const cBuiltRef &node, const cRZString *prefix, const char *suffix)
{
    const std::string name = prefix->mString + suffix;
    SetNodeName(node.mObject, name.c_str());
}

// prefix + the format's text of `number`, through a local string (cRZString::Sprintf).
template <class Use> void WithNumberedName(const cRZString *prefix, const char *format, int32_t number, Use use)
{
    cRZString text;
    text.Construct();
    text.Sprintf(format, number);
    {
        const std::string name = prefix->mString + text.ToChar();
        use(name.c_str());
    }
    text.Destruct();
}

} // namespace

// The node joins the sample's lights and the scene.
static void Join(cLightingManager *manager, cSkySample *sample, const cBuiltRef &node)
{
    sample->mLights.push_back(node);
    RawSlot<7, void>(manager->mSceneManager, node.mObject); // receiver: nTSSG::cSceneManager vf7 0x2bde50
}

void cLightingManager::CreateConfigLight(cSkySample *sample, const cLightConfig *config, uint32_t tag, uint32_t bits,
                                         const cRZString *prefix)
{
    const uint32_t type = config->mType;
    if (type == 0) {
        CreateBounceLight(sample, config, tag, bits, prefix);
    } else if (type == 1) {
        CreateMinSideLight(sample, config, tag, bits, prefix);
    } else if (type - 2 <= 6) {
        CreateAmbientLight(sample, config, tag, bits, prefix, static_cast<int32_t>(type - 2));
    }
}

void cLightingManager::CreateAmbientLight(cSkySample *sample, const cLightConfig *config, uint32_t tag, uint32_t bits,
                                          const cRZString *prefix, int32_t deltas)
{
    const float(*const v)[3] = sample->mVectors;
    const float c = config->mSideSum;
    const float sum[3] = {
        MulS(AddS(AddS(AddS(v[1][0], v[0][0]), v[2][0]), v[3][0]), c),
        MulS(AddS(AddS(AddS(v[0][1], v[1][1]), v[2][1]), v[3][1]), c),
        MulS(AddS(AddS(AddS(v[0][2], v[1][2]), v[2][2]), v[3][2]), c),
    };
    const float s = config->mUpToSides;
    const float up[3] = {MulS(v[4][0], s), MulS(v[4][1], s), MulS(v[4][2], s)};
    const float a = config->mBrightness;
    const float b = config->mSideScale;
    const float d = config->mVerticalScale;
    float out[6][3];
    for (int k = 0; k < 3; ++k) {
        out[0][k] = MulS(MulS(AddS(up[k], v[0][k]), b), a);
        out[1][k] = MulS(MulS(b, AddS(up[k], v[1][k])), a);
        out[2][k] = MulS(MulS(b, AddS(up[k], v[2][k])), a);
        out[3][k] = MulS(MulS(AddS(up[k], v[3][k]), b), a);
    }
    out[4][0] = MulS(MulS(d, AddS(sum[0], v[4][0])), a);
    out[4][1] = MulS(MulS(AddS(sum[1], v[4][1]), d), a);
    out[4][2] = MulS(MulS(AddS(sum[2], v[4][2]), d), a);
    for (int k = 0; k < 3; ++k) out[5][k] = MulS(MulS(AddS(sum[k], v[5][k]), d), a);
    float average[3];
    AverageOfSix(average, &out[0][0]);
    for (int i = 0; i < 6; ++i)
        for (int k = 0; k < 3; ++k) out[i][k] = SubS(out[i][k], average[k]);

    const float quarterScale = MulS(config->mLightMapScale, 0.25f);
    if (config->mLightMapWeight == 0.0f) {
        if (!Switch(kRefreshSwitch)) {
            cBuiltRef node;
            MakeNode(node);
            cBuiltRef light;
            MakeLight(light, kAmbientLightClass);
            AsLight(light)->SetColor(average);
            AsLight(light)->SetIntensity(1.0f);
            AsNode(node)->SetLight(AsLight(light));
            Tag(AsNode(node), tag, bits);
            Join(this, sample, node);
            NameNode(node, prefix, "Ambient");
        } else if (sample->mLerp78) {
            const std::string name = prefix->mString + "Ambient";
            sample->mLerp78->AddLight(bits, name.c_str(), 0, average, 1.0f, 0.0f, 0.0f);
        }
    } else if (!Switch(kRefreshSwitch)) {
        cBuiltRef node;
        MakeNode(node);
        void *const memory = ::operator new(0x7c);
        using Constructor = uint8_t *(__thiscall *)(void *, void *, float, float, uint32_t);
        uint8_t *const made = memory ? reinterpret_cast<Constructor>(const_cast<uint8_t *>(
                                           &cAmbientFromLightMapLight_Constructor))(memory, mRef24,
                                                                                    config->mLightMapWeight,
                                                                                    MulS(config->mLightMapScale, 0.25f),
                                                                                    1u)
                                     : nullptr;
        // The light's standard-light face, used whether or not the light was made; the node is given null without it.
        auto *const face = reinterpret_cast<cIStandardLight *>(reinterpret_cast<uintptr_t>(made) + 0x2c);
        face->SetColor(average);
        face->SetIntensity(1.0f);
        AsNode(node)->SetLight(made ? face : nullptr);
        Tag(AsNode(node), tag, bits);
        Join(this, sample, node);
        NameNode(node, prefix, "AmbientFromLightMap");
    } else if (sample->mLerp78) {
        const std::string name = prefix->mString + "AmbientFromLightMap";
        sample->mLerp78->AddLight(bits, name.c_str(), 1, average, 1.0f, config->mLightMapWeight, quarterScale);
    }

    const float *const directions = GameVector(kDirections);
    const float *const upNormal = GameVector(kUpNormal);
    for (int32_t i = 0; i < deltas; ++i) {
        const int32_t index = deltas > 2 ? LargestOfSix(&out[0][0]) : LargestOfFour(&out[0][0]);
        float direction[3] = {directions[3 * index], directions[3 * index + 1], directions[3 * index + 2]};
        if (static_cast<uint32_t>(index) <= 3) {
            const float h = config->mTilt;
            for (int k = 0; k < 3; ++k) direction[k] = SubS(direction[k], MulS(h, upNormal[k]));
        }
        if (!Switch(kRefreshSwitch)) {
            cBuiltRef node;
            MakeNode(node);
            cBuiltRef light;
            MakeLight(light, kDirectionalLightClass);
            SetUpDirectional(node, light, tag, bits, direction);
            Join(this, sample, node);
            WithNumberedName(prefix, "Delta%d", i, [&](const char *name) { SetNodeName(node.mObject, name); });
            AsLight(light)->SetColor(out[index]);
            AsLight(light)->SetIntensity(config->mDeltaIntensity);
            AsLight(light)->SetFactor(config->mFactor);
        } else if (sample->mLerp78) {
            WithNumberedName(prefix, "Delta%d", i, [&](const char *name) {
                sample->mLerp78->AddDirectional(bits, name, 2, out[index], config->mDeltaIntensity, direction,
                                                config->mLightMapWeight, MulS(config->mLightMapScale, 0.25f));
            });
        }
        CopyZero(out[index]);

        // The first delta light reshapes a pair the tables name: the two about the same size gain the average times
        // mAdjust; else the larger is scaled to mAdjust over its size.
        if (i != 0 || !(config->mAdjust > 0.0f)) continue;
        const float adjust = config->mAdjust;
        const int32_t first = GameAddress<const int32_t>(kAdjustFirst)[index];
        const int32_t second = GameAddress<const int32_t>(kAdjustSecond)[first];
        const float firstSize = SizeOf(out[first]);
        const float secondSize = SizeOf(out[second]);
        if (static_cast<double>(std::fabs(SubS(firstSize, secondSize))) < 0.01) {
            for (int k = 0; k < 3; ++k) out[first][k] = AddS(MulS(adjust, average[k]), out[first][k]);
        } else if (firstSize > secondSize) {
            const float scale = DivS(adjust, firstSize);
            for (int k = 0; k < 3; ++k) out[first][k] = MulS(out[first][k], scale);
        } else {
            const float scale = DivS(adjust, secondSize);
            for (int k = 0; k < 3; ++k) out[second][k] = MulS(out[second][k], scale);
        }
    }
}

void cLightingManager::CreateBounceLight(cSkySample *sample, const cLightConfig *config, uint32_t tag, uint32_t bits,
                                         const cRZString *prefix)
{
    static const char *const kNames[5] = {"Bounce", "PosX", "NegX", "PosY", "NegY"};
    const float *const upNormal = GameVector(kUpNormal);
    // Each light is made before its node; they are let go of in reverse (node 4, light 4, ... light 0).
    struct Pair {
        cBuiltRef light;
        cBuiltRef node;
    };
    Pair p0, p1, p2, p3, p4;
    Pair *const pairs[5] = {&p0, &p1, &p2, &p3, &p4};
    for (int32_t i = 0; i < 5; ++i) {
        Pair &pair = *pairs[i];
        MakeLight(pair.light, kDirectionalLightClass);
        MakeNode(pair.node);
        float direction[3];
        const float *towards = upNormal;
        if (i != 0) {
            const float *const axis = GameVector(i < 3 ? kBounceX : kBounceY);
            const float h = config->mTilt;
            for (int k = 0; k < 3; ++k) {
                const float along = (i & 1) ? axis[k] : Negated(axis[k]);
                direction[k] = SubS(along, MulS(h, upNormal[k]));
            }
            towards = direction;
        }
        SetUpDirectional(pair.node, pair.light, tag, bits, towards);
        Join(this, sample, pair.node);
        NameNode(pair.node, prefix, kNames[i]);
    }

    if (!mFixedBounce) {
        const float(*const v)[3] = sample->mVectors;
        const float c = config->mSideSum;
        const float s = config->mUpToSides;
        float color[3];
        for (int k = 0; k < 3; ++k)
            color[k] = AddS(MulS(AddS(AddS(AddS(v[0][k], v[1][k]), v[2][k]), v[3][k]), c), v[4][k]);
        AsLight(p0.light)->SetColor(color);
        for (int32_t i = 1; i < 5; ++i) {
            for (int k = 0; k < 3; ++k) color[k] = AddS(MulS(v[4][k], s), v[i - 1][k]);
            AsLight(pairs[i]->light)->SetColor(color);
        }
        AsLight(p0.light)->SetIntensity(MulS(config->mVerticalScale, config->mBrightness));
        for (int32_t i = 1; i < 5; ++i)
            AsLight(pairs[i]->light)->SetIntensity(MulS(config->mBrightness, config->mSideScale));
        for (int32_t i = 0; i < 5; ++i) AsLight(pairs[i]->light)->SetFactor(config->mFactor);
    } else {
        for (int32_t i = 0; i < 5; ++i) AsLight(pairs[i]->light)->SetColor(GameVector(kFixedColors[i]));
        for (int32_t i = 0; i < 5; ++i) AsLight(pairs[i]->light)->SetIntensity(1.0f);
    }
}

void cLightingManager::CreateMinSideLight(cSkySample *sample, const cLightConfig *config, uint32_t tag, uint32_t bits,
                                          const cRZString *prefix)
{
    const float(*const v)[3] = sample->mVectors;
    const float c = config->mSideSum;
    float sum[3];
    for (int k = 0; k < 3; ++k) sum[k] = MulS(AddS(AddS(AddS(v[0][k], v[1][k]), v[2][k]), v[3][k]), c);
    const float s = config->mUpToSides;
    const float up[3] = {MulS(v[4][0], s), MulS(v[4][1], s), MulS(v[4][2], s)};
    const float a = config->mBrightness;
    const float b = config->mSideScale;
    const float d = config->mVerticalScale;
    float out[6][3];
    for (int k = 0; k < 3; ++k) {
        out[0][k] = MulS(MulS(b, AddS(v[0][k], up[k])), a);
        out[1][k] = MulS(MulS(b, AddS(v[1][k], up[k])), a);
        out[2][k] = MulS(MulS(b, AddS(v[2][k], up[k])), a);
        out[3][k] = MulS(MulS(AddS(v[3][k], up[k]), b), a);
        out[4][k] = MulS(MulS(d, AddS(sum[k], v[4][k])), a);
        out[5][k] = MulS(MulS(AddS(sum[k], v[5][k]), d), a);
    }
    const float *const upNormal = GameVector(kUpNormal);
    const float *const directions = GameVector(kDirections);

    {
        cBuiltRef node;
        MakeNode(node);
        cBuiltRef light;
        MakeLight(light, kDirectionalLightClass);
        SetUpDirectional(node, light, tag, bits, upNormal);
        Join(this, sample, node);
        NameNode(node, prefix, "Bounce");
        AsLight(light)->SetColor(out[4]);
        AsLight(light)->SetIntensity(1.0f);
        AsLight(light)->SetFactor(config->mFactor);
        CopyZero(out[4]);
        CopyZero(out[5]);
    }

    for (int32_t i = 0; i < 2; ++i) {
        const int32_t index = LargestOfSix(&out[0][0]);
        const float h = config->mTilt;
        const float *const t = directions + 3 * index;
        const float direction[3] = {SubS(t[0], MulS(h, upNormal[0])), SubS(t[1], MulS(upNormal[1], h)),
                                    SubS(t[2], MulS(upNormal[2], h))};
        cBuiltRef node;
        MakeNode(node);
        cBuiltRef light;
        MakeLight(light, kDirectionalLightClass);
        SetUpDirectional(node, light, tag, bits, direction);
        Join(this, sample, node);
        WithNumberedName(prefix, "MaxSide%d", i, [&](const char *name) { SetNodeName(node.mObject, name); });
        AsLight(light)->SetColor(out[index]);
        AsLight(light)->SetIntensity(1.0f);
        AsLight(light)->SetFactor(config->mFactor);
        CopyZero(out[index]);
    }

    // The two smallest vectors left, unless they are a pair (0/1, 2/3, 4/5).
    const int32_t first = SmallestOfSix(&out[0][0]);
    const float m1[3] = {out[first][0], out[first][1], out[first][2]};
    CopyZero(out[first]);
    const int32_t second = SmallestOfSix(&out[0][0]);
    if (((first ^ second) & 0xfe) == 0) return;
    const float m2[3] = {out[second][0], out[second][1], out[second][2]};
    const float z1 = SizeOf(m1);
    const float z2 = SizeOf(m2);
    if (!(z1 > 0.0f) && !(z2 > 0.0f)) return;
    const float *const t1 = directions + 3 * first;
    const float *const t2 = directions + 3 * second;
    float w[3];
    for (int k = 0; k < 3; ++k) w[k] = AddS(MulS(t1[k], z1), MulS(t2[k], z2));
    const float lengthSquared = AddS(AddS(MulS(w[0], w[0]), MulS(w[1], w[1])), MulS(w[2], w[2]));
    const float length = static_cast<float>(std::sqrt(static_cast<double>(lengthSquared)));
    const float inverse = DivS(*GameVector(kMinSideOne), length);
    const float h = config->mTilt;
    const float direction[3] = {SubS(MulS(w[0], inverse), MulS(h, upNormal[0])),
                                SubS(MulS(inverse, w[1]), MulS(h, upNormal[1])),
                                SubS(MulS(inverse, w[2]), MulS(h, upNormal[2]))};
    cBuiltRef node;
    MakeNode(node);
    cBuiltRef light;
    MakeLight(light, kDirectionalLightClass);
    SetUpDirectional(node, light, tag, bits, direction);
    Join(this, sample, node);
    NameNode(node, prefix, "MinSide");
    const float total = AddS(z2, z1);
    float color[3];
    for (int k = 0; k < 3; ++k) color[k] = DivS(AddS(MulS(m2[k], z2), MulS(m1[k], z1)), total);
    AsLight(light)->SetColor(color);
    AsLight(light)->SetIntensity(1.0f);
    AsLight(light)->SetFactor(config->mFactor);
}

} // namespace nTSSG
