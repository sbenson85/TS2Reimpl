// nTSSG::cLerpLightSet (lane 261108l). See src/sg/cLerpLightSet.h.
#include "sg/cLerpLightSet.h"

#include "compgeom/CompGeom.h"
#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZString.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/SGLightInterfaces.h"
#include "sg/SGUtilities.h"
#include "sg/cLightRefNode.h"
#include "sg/cLightingManager.h"
#include "sg/cRenderGroupNameIterator.h"
#include "sg/nTSSGLights.h"

#include <cmath>
#include <new>

namespace nTSSG {

namespace {

using nGZCompGeom::NearlyEqual;
using nGZCompGeom::RigidTransform3;
using nGZCompGeom::Vec3;
using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::LengthSquared;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;
using nGZSceneGraph::cILight;
using nGZSceneGraph::cILightRefNode;
using nGZSceneGraph::cIStandardLight;
using nGZSceneGraph::cITextureLight;
using Entry = cLerpLightSet::Entry;

// The director's classes and the interfaces asked of them.
constexpr uint32_t kLightRefNodeClass = 0x253d2018;
constexpr uint32_t kAmbientLightClass = 0xc9c81b9b;
constexpr uint32_t kDirectionalLightClass = 0xc9c81ba3;
constexpr uint32_t kTextureLightClass = 0x6a97042f;

// The attributes a kExisting node's light answers (cScalarEvent<float> / cVectorEvent<cVector<3>> records).
constexpr uint32_t kAttrOne = 0xe9d24d51;
constexpr uint32_t kAttrColor = 0x29de3049;
constexpr uint32_t kAttrValue = 0x49e37821;
constexpr uint32_t kScalarEventVTable = 0xdb07f4; // the game's cScalarEvent<float>
constexpr uint32_t kVectorEventVTable = 0xdb5e70; // the game's cVectorEvent<cVector<3>>

struct ScalarEvent {
    const void *mVTable;
    float mValue;
};
struct VectorEvent {
    const void *mVTable;
    float mValue[3];
};

const void *EventVTable(uint32_t rva) { return GameAddress<const void>(rva); }

void SetAttribute(cILightRefNode *node, uint32_t id, const void *event)
{
    node->SetAttribute(id, 0, reinterpret_cast<uint32_t>(event));
}
void GetAttribute(cILightRefNode *node, uint32_t id, void *event)
{
    node->GetAttribute(reinterpret_cast<uint32_t>(event), id, 0);
}

// The step towards a target: (target - current) * t + current.
float Step(float target, float current, float t) { return AddS(MulS(SubS(target, current), t), current); }

// Game code still called.
void *Service() { return OriginalFunction<void *(__cdecl *)(), 0x20defb>()(); }
void *NodeRegistry() { return OriginalFunction<void *(__cdecl *)(), 0x20df51>()(); }
void RegistryAdd(void *registry, cILightRefNode *node) { RawSlot<7, void>(registry, node); } // receiver: nTSSG::cSceneManager vf7 0x2bde50 (one argument, tail call)
void RegistryRemove(void *registry, cILightRefNode *node) { RawSlot<9, void>(registry, node); } // receiver: nTSSG::cSceneManager vf9 0x2bfae0 (tail call, as vf7)
void *ShadeSource(void *service, bool terrain) // the service's slot 48 (ambient) or 49 (the others), no arguments
{
    // 0x20defb gets service 0x9a353ce8: vf48 0x24b3f0 and vf49 0x155e40, both ret 0.
    return terrain ? RawSlot<49, void *>(service) : RawSlot<48, void *>(service); // receiver: nTSSG::cTSSGSystem
}
void SetNodeName(cILightRefNode *node, const char *name)
{
    // The node's pointer as the helpers take it (the game passes it unadjusted).
    nGZSceneGraph::SetNodeTag(reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(node), name);
}
void SetNodeDirection(cILightRefNode *node, const float *direction)
{
    nGZSceneGraph::AimNode(reinterpret_cast<nGZSceneGraph::cITransformNode *>(node), direction);
}

// One of our nTSSG light constructors (sg/nTSSGLights.h) at its C name, as the game calls them: thiscall, the
// most-derived flag 1 last, returning the object. Not inlined, so tests can stand in for it.
template <class... A> uint8_t *NewLight(const uint8_t &constructor, void *memory, A... args)
{
    using Fn = uint8_t *(__thiscall *)(void *, A..., uint32_t);
    return reinterpret_cast<Fn>(const_cast<uint8_t *>(&constructor))(memory, args..., 1u);
}
cIGZUnknown *Counted(void *source) { return static_cast<cIGZUnknown *>(source); }

// A counted field takes a new value: a different one is counted first, then the old one let go of.
template <class T> void Assign(T *&field, T *value)
{
    if (value == field) return;
    if (value) value->AddRef();
    T *const old = field;
    field = value;
    if (old) old->Release();
}
// A counted field emptied before the director fills it.
template <class T> void Reset(T *&field)
{
    if (!field) return;
    T *const old = field;
    field = nullptr;
    old->Release();
}

cIGZCOM *Framework() { return static_cast<cIGZCOM *>(GetDirectorFrameworkObject()); }

// Every creator starts with a new light-reference node.
void MakeNode(Entry &entry)
{
    cIGZCOM *const com = Framework();
    Reset(entry.mNode);
    com->GetClassObject(kLightRefNodeClass, cILightRefNode::kIid, reinterpret_cast<void **>(&entry.mNode));
}

// The node face of a light the game's constructor built (the constructor returns the object).
cILight *NodeFace(uint8_t *object) { return object ? reinterpret_cast<cILight *>(object + 0x2c) : nullptr; }
cIStandardLight *StandardFace(uint8_t *object)
{
    return object ? reinterpret_cast<cIStandardLight *>(object + 0x2c) : nullptr;
}

// What every creator does to a new node before the entry joins the set.
void TagNode(cILightRefNode *node, const cLerpLightSet *set, uint32_t group)
{
    node->SetLayerTag(set->mLayerTag);
    node->SetLightFlag(static_cast<uint8_t>(set->mLightFlag));
    node->RemoveLayerBits(0xffffffffu);
    node->AddLayerBits(group);
}

// A new standard light starts black at full intensity (the entry fades it in).
void StartLight(cIStandardLight *light)
{
    static const float kBlack[3] = {0.0f, 0.0f, 0.0f};
    light->SetColor(kBlack);
    light->SetIntensity(1.0f);
}

void ScaleInto(float *out, const float *color, float scale)
{
    for (int k = 0; k < 3; ++k) out[k] = MulS(color[k], scale);
}

} // namespace

// ---- Entry ----

Entry::Entry()
    : mKind(0), mNode(nullptr), mLight(nullptr), mTexture(nullptr), mLerpColor(1), mLerpDirection(0), mLerpValue(0),
      mColor{0.0f, 0.0f, 0.0f}, mDirection{0.0f, 0.0f, 1.0f}, mActive(1), mReusable(1)
{
}

Entry::Entry(const Entry &other) : mKind(other.mKind), mGroup(other.mGroup)
{
    mNode = other.mNode;
    if (mNode) mNode->AddRef();
    mLight = other.mLight;
    if (mLight) mLight->AddRef();
    mTexture = other.mTexture;
    if (mTexture) mTexture->AddRef();
    mLerpColor = other.mLerpColor;
    mLerpDirection = other.mLerpDirection;
    mLerpValue = other.mLerpValue;
    for (int k = 0; k < 3; ++k) mColor[k] = other.mColor[k];
    for (int k = 0; k < 3; ++k) mDirection[k] = other.mDirection[k];
    mValue = other.mValue;
    mActive = other.mActive;
    mReusable = other.mReusable;
}

Entry &Entry::operator=(const Entry &other)
{
    mKind = other.mKind;
    mGroup = other.mGroup;
    if (&mNode != &other.mNode) Assign(mNode, other.mNode);
    if (&mLight != &other.mLight) Assign(mLight, other.mLight);
    if (&mTexture != &other.mTexture) Assign(mTexture, other.mTexture);
    mLerpColor = other.mLerpColor;
    mLerpDirection = other.mLerpDirection;
    mLerpValue = other.mLerpValue;
    for (int k = 0; k < 3; ++k) mColor[k] = other.mColor[k];
    for (int k = 0; k < 3; ++k) mDirection[k] = other.mDirection[k];
    mValue = other.mValue;
    mActive = other.mActive;
    mReusable = other.mReusable;
    return *this;
}

Entry::~Entry()
{
    if (mTexture) mTexture->Release();
    if (mLight) mLight->Release();
    if (mNode) mNode->Release();
}

void Entry::Evaluate(float t)
{
    if (static_cast<double>(t) > 0.95) {
        if (mKind == cLerpLightSet::kExisting) cLerpLightSet::SetLightEvents(mNode, mColor, mValue);
        if (mLight) {
            if (mLerpColor) {
                mLight->SetColor(mColor);
                mLight->SetIntensity(1.0f);
            }
            if (mLerpValue) mLight->SetFalloff(mValue);
        }
        if (mTexture && mLerpColor) {
            const float rgba[4] = {1.0f, mColor[0], mColor[1], mColor[2]};
            mTexture->SetColor(0, rgba);
        }
        if (mNode && mLerpDirection) SetNodeDirection(mNode, mDirection);
        if (mLerpColor) {
            const bool black = NearlyEqual(mColor[0], 0.0f) && NearlyEqual(mColor[1], 0.0f) && NearlyEqual(mColor[2], 0.0f);
            if (black && !mTexture) mActive = 0;
        }
        return;
    }

    if (mKind == cLerpLightSet::kExisting) cLerpLightSet::LerpLightEvents(mNode, t, mColor, mValue);
    if (mLight) {
        if (mLerpColor) {
            float current[3];
            const float *c = mLight->GetColor(current);
            float out[3];
            for (int k = 0; k < 3; ++k) out[k] = Step(mColor[k], c[k], t);
            mLight->SetColor(out);
            mLight->SetIntensity(1.0f);
        }
        if (mLerpValue) {
            const float v = mLight->GetFalloff();
            mLight->SetFalloff(Step(mValue, v, t));
        }
    }
    if (mTexture && mLerpColor) {
        float current[4];
        const float *c = mTexture->GetColor(current, 0);
        const float c1 = c[1], c2 = c[2], c3 = c[3];
        const float rgba[4] = {1.0f, Step(mColor[0], c1, t), Step(mColor[1], c2, t), Step(mColor[2], c3, t)};
        mTexture->SetColor(0, rgba);
    }
    if (mNode && mLerpDirection) {
        // The node's current direction: the default one (0, 0, 1) negated, turned by the node's rotation.
        float v[3] = {Negated(0.0f), Negated(0.0f), Negated(1.0f)};
        const nGZSceneGraph::cRigidTransform local = mNode->GetLocalTransform();
        reinterpret_cast<const RigidTransform3 *>(&local)->RotateVector(v);
        const Vec3 l = {Step(mDirection[0], v[0], t), Step(mDirection[1], v[1], t), Step(mDirection[2], v[2], t)};
        const float length = static_cast<float>(std::sqrt(static_cast<double>(LengthSquared(l))));
        if (static_cast<double>(length) > 1e-6) {
            const float unit[3] = {DivS(l.x, length), DivS(l.y, length), DivS(l.z, length)};
            SetNodeDirection(mNode, unit);
        } else {
            SetNodeDirection(mNode, mDirection);
        }
    }
}

// ---- the set ----

cLerpLightSet::~cLerpLightSet() { Clear(); }

cLerpLightSet *cLerpLightSet::Construct() { return ::new (this) cLerpLightSet; }

void cLerpLightSet::Destruct() { this->cLerpLightSet::~cLerpLightSet(); }

cLerpLightSet *cLerpLightSet::DeletingDestructor(uint32_t flags)
{
    this->cLerpLightSet::~cLerpLightSet();
    if (flags & 1) ::operator delete(this, sizeof(cLerpLightSet));
    return this;
}

void cLerpLightSet::ClearActive()
{
    for (uint32_t i = 0; i < mEntries.size(); ++i) mEntries[i].mActive = 0;
}

void cLerpLightSet::FadeOutUnused()
{
    for (uint32_t i = 0; i < mEntries.size(); ++i) {
        Entry &e = mEntries[i];
        if (e.mActive || !e.mReusable) continue;
        e.mActive = 1;
        for (float &c : e.mColor) c = 0.0f;
        e.mLerpDirection = 0;
    }
}

int32_t cLerpLightSet::Find(uint32_t group, uint32_t kind)
{
    for (uint32_t i = 0; i < mEntries.size(); ++i) {
        const Entry &e = mEntries[i];
        if (!e.mActive && e.mReusable && e.mGroup == group && e.mKind == kind) return static_cast<int32_t>(i);
    }
    return -1;
}

int32_t cLerpLightSet::FindPoint(uint32_t group, uint32_t kind, const RigidTransform3 *transform)
{
    const float *const p = reinterpret_cast<const float *>(transform);
    for (uint32_t i = 0; i < mEntries.size(); ++i) {
        const Entry &e = mEntries[i];
        if (e.mActive || !e.mReusable || e.mGroup != group || e.mKind != kind) continue;
        const nGZSceneGraph::cRigidTransform local = e.mNode->GetLocalTransform();
        const float *const t = local.mValues;
        if (!NearlyEqual(t[0], p[0]) || !NearlyEqual(t[1], p[1]) || !NearlyEqual(t[2], p[2])) continue;
        bool same = true;
        for (int k = 3; k < 7 && same; ++k) same = NearlyEqual(t[k], p[k]);
        if (same) return static_cast<int32_t>(i);
        // The same rotation as the negated quaternion.
        bool negated = true;
        for (int k = 3; k < 7 && negated; ++k) negated = NearlyEqual(t[k], Negated(p[k]));
        if (negated) return static_cast<int32_t>(i);
    }
    return -1;
}

void cLerpLightSet::Clear()
{
    void *const registry = NodeRegistry();
    for (uint32_t i = 0; i < mEntries.size(); ++i) {
        const Entry &e = mEntries[i];
        if (e.mKind != kExisting && e.mNode && e.mNode->GetParent()) RegistryRemove(registry, mEntries[i].mNode);
    }
    mEntries.clear();
}

void cLerpLightSet::SetKeys(uint32_t layerTag, uint32_t lightFlag)
{
    if (layerTag == mLayerTag && lightFlag == mLightFlag) return;
    mLayerTag = layerTag;
    mLightFlag = lightFlag;
    Clear();
}

void cLerpLightSet::Evaluate(float t)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < mEntries.size(); ++i) {
        if (mEntries[i].mActive) mEntries[i].Evaluate(t);
        Entry &e = mEntries[i];
        if (e.mActive) {
            if (static_cast<int32_t>(kept) < static_cast<int32_t>(i)) mEntries[kept] = mEntries[i];
            ++kept;
        } else if (e.mKind != kExisting && e.mNode && e.mNode->GetParent()) {
            RegistryRemove(NodeRegistry(), mEntries[i].mNode);
        }
    }
    mEntries.resize(kept);
}

void __cdecl cLerpLightSet::SetLightEvents(cILightRefNode *node, const float *color, float value)
{
    const ScalarEvent one = {EventVTable(kScalarEventVTable), 1.0f};
    const VectorEvent rgb = {EventVTable(kVectorEventVTable), {color[0], color[1], color[2]}};
    const ScalarEvent v = {EventVTable(kScalarEventVTable), value};
    SetAttribute(node, kAttrOne, &one);
    SetAttribute(node, kAttrColor, &rgb);
    SetAttribute(node, kAttrValue, &v);
}

void __cdecl cLerpLightSet::LerpLightEvents(cILightRefNode *node, float t, const float *color, float value)
{
    ScalarEvent one;
    one.mVTable = EventVTable(kScalarEventVTable);
    VectorEvent rgb = {EventVTable(kVectorEventVTable), {0.0f, 0.0f, 0.0f}};
    // ORIGINAL-BUG: the value's event is not cleared (the colour's is), so a node without attribute 49e37821 steps from
    // whatever the stack held.
    ScalarEvent v;
    v.mVTable = EventVTable(kScalarEventVTable);
    GetAttribute(node, kAttrOne, &one);
    GetAttribute(node, kAttrColor, &rgb);
    GetAttribute(node, kAttrValue, &v);
    for (int k = 0; k < 3; ++k) rgb.mValue[k] = Step(color[k], rgb.mValue[k], t);
    one.mValue = 1.0f;
    v.mValue = Step(value, v.mValue, t);
    SetAttribute(node, kAttrOne, &one);
    SetAttribute(node, kAttrColor, &rgb);
    SetAttribute(node, kAttrValue, &v);
}

void cLerpLightSet::AddLight(uint32_t group, const char *name, uint32_t kind, const float *color, float scale, float a6,
                             float a7)
{
    const int32_t found = Find(group, kind);
    if (found != -1) {
        Entry &e = mEntries[found];
        ScaleInto(e.mColor, color, scale);
        e.mActive = 1;
        return;
    }
    Entry entry;
    switch (kind) {
    case kAmbient: {
        MakeNode(entry);
        cIGZCOM *const com = Framework();
        Reset(entry.mLight);
        com->GetClassObject(kAmbientLightClass, cIStandardLight::kIid, reinterpret_cast<void **>(&entry.mLight));
        // ORIGINAL-BUG: here and in each case below, the new node is used without a null test (a director that fails
        // to make one faults); the test after the switch only catches a kind this doesn't make.
        entry.mNode->SetLight(entry.mLight);
        break;
    }
    case kLightMapAmbient: {
        MakeNode(entry);
        void *const memory = ::operator new(0x7c);
        uint8_t *made = nullptr;
        if (memory) {
            made = NewLight(cAmbientFromLightMapLight_Constructor, memory, Counted(ShadeSource(Service(), false)), a6,
                             a7);
        }
        Assign(entry.mLight, StandardFace(made));
        entry.mNode->SetLight(entry.mLight);
        break;
    }
    case kShade: {
        MakeNode(entry);
        void *const memory = ::operator new(100);
        uint8_t *made = nullptr;
        if (memory) {
            // The shade light takes the colour unscaled.
            const float rgba[4] = {1.0f, color[0], color[1], color[2]};
            made = NewLight(cShadeLight_Constructor, memory, static_cast<const float *>(rgba));
        }
        entry.mNode->SetLight(NodeFace(made));
        entry.mReusable = 0;
        break;
    }
    case kTerrainShade: {
        MakeNode(entry);
        void *const memory = ::operator new(0x58);
        uint8_t *made = nullptr;
        if (memory) {
            made = NewLight(cTerrainShadeLight_Constructor, memory, Counted(ShadeSource(Service(), true)));
        }
        entry.mNode->SetLight(NodeFace(made));
        entry.mReusable = 0;
        break;
    }
    case kEnvironment: {
        MakeNode(entry);
        cIGZCOM *const com = Framework();
        Reset(entry.mTexture);
        com->GetClassObject(kTextureLightClass, cITextureLight::kIid, reinterpret_cast<void **>(&entry.mTexture));
        entry.mNode->SetLight(entry.mTexture);
        static const float kWhite[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        entry.mTexture->SetColor(0, kWhite);
        break;
    }
    default:
        break;
    }
    if (!entry.mNode) return;
    entry.mKind = kind;
    entry.mGroup = group;
    entry.mLerpColor = 1;
    entry.mLerpDirection = 0;
    ScaleInto(entry.mColor, color, scale);
    if (entry.mLight) StartLight(entry.mLight);
    TagNode(entry.mNode, this, group);
    SetNodeName(entry.mNode, name);
    RegistryAdd(NodeRegistry(), entry.mNode);
    mEntries.push_back(entry);
}

void cLerpLightSet::AddDirectional(uint32_t group, const char *name, uint32_t kind, const float *color, float scale,
                                   const float *direction, float a7, float a8)
{
    const int32_t found = Find(group, kind);
    if (found != -1) {
        Entry &e = mEntries[found];
        ScaleInto(e.mColor, color, scale);
        for (int k = 0; k < 3; ++k) e.mDirection[k] = direction[k];
        e.mLerpDirection = 1;
        e.mActive = 1;
        return;
    }
    Entry entry;
    switch (kind) {
    case kDirectional: {
        MakeNode(entry);
        cIGZCOM *const com = Framework();
        Reset(entry.mLight);
        com->GetClassObject(kDirectionalLightClass, cIStandardLight::kIid, reinterpret_cast<void **>(&entry.mLight));
        entry.mNode->SetLight(entry.mLight);
        break;
    }
    case kShadowDirectional: {
        MakeNode(entry);
        void *const memory = ::operator new(0x80);
        uint8_t *made = nullptr;
        if (memory) {
            made = NewLight(cHeightMapShadowLight_Constructor, memory, Counted(ShadeSource(Service(), true)), a7);
        }
        Assign(entry.mLight, StandardFace(made));
        entry.mNode->SetLight(entry.mLight);
        break;
    }
    case kShadowDirectionalB: {
        MakeNode(entry);
        void *const memory = ::operator new(0xb0);
        uint8_t *made = nullptr;
        if (memory) {
            made = NewLight(cHeightMapBinaryShadowLight_Constructor, memory, Counted(ShadeSource(Service(), true)),
                             direction, a7, a8);
        }
        entry.mNode->SetLight(NodeFace(made));
        entry.mReusable = 0;
        break;
    }
    default:
        break;
    }
    if (!entry.mNode) return;
    entry.mKind = kind;
    entry.mGroup = group;
    entry.mLerpDirection = 1;
    ScaleInto(entry.mColor, color, scale);
    for (int k = 0; k < 3; ++k) entry.mDirection[k] = direction[k];
    if (entry.mLight) StartLight(entry.mLight);
    TagNode(entry.mNode, this, group);
    SetNodeDirection(entry.mNode, direction);
    SetNodeName(entry.mNode, name);
    RegistryAdd(NodeRegistry(), entry.mNode);
    mEntries.push_back(entry);
}

void cLerpLightSet::AddExisting(uint32_t group, const char *name, cILightRefNode *node, uint32_t /*unused*/,
                                const float *color, float scale, float value)
{
    for (uint32_t i = 0; i < mEntries.size(); ++i) {
        Entry &e = mEntries[i];
        if (e.mNode != node) continue;
        ScaleInto(e.mColor, color, scale);
        e.mValue = value;
        e.mActive = 1;
        return;
    }
    Entry entry;
    Assign(entry.mNode, node);
    entry.mKind = kExisting;
    entry.mGroup = group;
    ScaleInto(entry.mColor, color, scale);
    entry.mValue = value;
    entry.mLerpColor = 1;
    entry.mLerpDirection = 0;
    entry.mLerpValue = 1;
    entry.mActive = 1;
    entry.mReusable = 1;
    SetLightEvents(node, entry.mColor, value);
    if (entry.mLight) { // never set here; kept as the original has it
        StartLight(entry.mLight);
        entry.mLight->SetFalloff(value);
    }
    TagNode(entry.mNode, this, group);
    SetNodeName(entry.mNode, name);
    mEntries.push_back(entry);
}

void cLerpLightSet::AddPoint(uint32_t group, const char *name, const RigidTransform3 *transform, const float *direction,
                             const float *position, const float *color, float scale, float value)
{
    if (0.0f >= scale) return;
    const int32_t found = FindPoint(group, kHalfspacePoint, transform);
    if (found != -1) {
        Entry &e = mEntries[found];
        e.mActive = 1;
        ScaleInto(e.mColor, color, scale);
        e.mValue = value;
        e.mLerpValue = 1;
        return;
    }
    Entry entry;
    RigidTransform3 inverse = *transform;
    inverse.Invert();
    MakeNode(entry);
    void *const memory = ::operator new(0xa0);
    uint8_t *made = nullptr;
    if (memory) {
        // The plane's direction and point carried into the node's frame.
        Vec3 p = {position[0], position[1], position[2]};
        inverse.TransformPoint(&p);
        float d[3] = {direction[0], direction[1], direction[2]};
        inverse.RotateVector(d);
        made = NewLight(cHalfspacePointLight_Constructor, memory, static_cast<const float *>(d),
                         reinterpret_cast<const float *>(&p));
    }
    Assign(entry.mLight, StandardFace(made));
    // ORIGINAL-BUG: as in every creator, the new node is used without a null test (a director that fails to make one
    // faults here); AddLight's and AddDirectional's test after their switch only catches a kind they don't make.
    entry.mNode->SetLight(entry.mLight);
    entry.mKind = kHalfspacePoint;
    entry.mGroup = group;
    entry.mLerpValue = 1;
    ScaleInto(entry.mColor, color, scale);
    entry.mValue = value;
    if (entry.mLight) {
        StartLight(entry.mLight);
        entry.mLight->SetFactor(0.0f);
    }
    entry.mNode->SetTransform(reinterpret_cast<const nGZSceneGraph::cRigidTransform *>(transform));
    TagNode(entry.mNode, this, group);
    SetNodeName(entry.mNode, name);
    RegistryAdd(NodeRegistry(), entry.mNode);
    mEntries.push_back(entry);
}

void __cdecl cLerpLightSet::BuildRenderGroupLights(const cLightSet *set, cLerpLightSet *lerp, uint32_t kind0, float a7)
{
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    auto bit = [](const char *name) { return 1u << (nGZSceneGraph::RenderGroupRegistry()->BitOfName(name) & 31); };
    const uint32_t sims = bit("Sims");
    // The preset bit 0 is the original's (render group 0 always lit by the outdoor lights).
    uint32_t objects = 1u | bit("Props");
    objects |= bit("Portals");
    objects |= bit("Roof");
    objects |= bit("LotSkirt");
    uint32_t surfaces = bit("Walls");
    surfaces |= bit("Floors");
    surfaces |= bit("Ground");
    const uint32_t all = objects | sims;

    const uint32_t enabled = set->mEnabled;
    const bool indoor = ((enabled >> 4) & 1) || ((enabled >> 5) & 1) || ((enabled >> 6) & 1);
    float ambient[3];
    if ((enabled >> 7) & 1) {
        lerp->AddLight(all, "LM_Outdoor_Ambient", kAmbient, &set->mAmbient.x, 1.0f, 0.0f, 0.0f);
        const cLightVector &a = set->mAmbient;
        ambient[0] = AddS(a.x, zero[0]);
        ambient[1] = AddS(zero[1], a.y);
        ambient[2] = AddS(zero[2], a.z);
    } else {
        for (int k = 0; k < 3; ++k) ambient[k] = zero[k];
    }

    // Seven directional lights (the header's mLights names the first four).
    const auto *const lights = reinterpret_cast<const cSetLight *>(reinterpret_cast<const uint8_t *>(set) +
                                                                   offsetof(cLightSet, mLights));
    cRZString name;
    name.Construct();
    for (uint32_t i = 0; i < 7; ++i) {
        if (!((enabled >> i) & 1)) continue;
        cRZString *const printed = name.Sprintf("LM_Outdoor_Dir_%i", i);
        reinterpret_cast<cIGZString *>(printed)->ToChar(); // the original's call, its result unused
        const uint32_t mask = indoor ? (i - 4 <= 2 ? sims : objects) : all;
        const cSetLight &light = lights[i];
        const float direction[3] = {Negated(light.mDirection.x), Negated(light.mDirection.y), Negated(light.mDirection.z)};
        const float color[3] = {SubS(light.mColour.x, ambient[0]), SubS(light.mColour.y, ambient[1]),
                                SubS(light.mColour.z, ambient[2])};
        if (i == 0)
            lerp->AddDirectional(mask, name.ToChar(), kind0, color, light.mIntensity, direction, a7, 0.0f);
        else
            lerp->AddDirectional(mask, name.ToChar(), kDirectional, color, light.mIntensity, direction, 0.0f, 0.0f);
    }
    if ((enabled >> 9) & 1)
        lerp->AddLight(all | surfaces, "LM_Environment", kEnvironment, &set->mVector174.x, 1.0f, 0.0f, 0.0f);
    name.Destruct();
}

} // namespace nTSSG
