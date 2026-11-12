// nTSSG::cLightingManager, the objects' lights (lane 261110w, scenegraph LG3h): slot 11 readies an object for lighting
// (PrepareObjectLighting), slot 13 lights the light-reference nodes below it (ApplyObjectLighting), with their helpers
// (the extension search up a node's tree, the light-set names' comparison). See sg/cLightingManager.h.
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "misc/SmallVirtuals.h"
#include "misc/X87.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"
#include "rz/cRZStringSub.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/DesignMode.h"
#include "sg/SGUtilities.h"
#include "sg/cIDiffusionLightExtension.h"
#include "sg/cLightRefNode.h"

#include <cstring>

namespace nTSSG {

namespace {

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::SubS;
using nGZSceneGraph::cICompositionTreeNode;
using nGZSceneGraph::cIExtension;
using nGZSceneGraph::cILightRefNode;
using nGZSceneGraph::cISceneNode;

constexpr uint32_t kObjectLighting = 0xdb60bc;  // "ObjectLighting"
constexpr uint32_t kStrength = 0xdb61e8;        // "Strength"
constexpr uint32_t kDesignModeSuffix = 0xdb08c0; // "_designmodelight"
constexpr uint32_t kDesignModeSuffixLength = 16;
constexpr uint32_t kOne = 0xd9c698;             // the game's 1.0f
constexpr uint32_t kDiffusionLighting = 0xffde34; // g_DiffusionLighting (the option byte): clear, objects lit at 0
constexpr uint16_t kOwnerKindA = 8;             // the owners (slot 98) whose objects' lights slot 11 switches off
constexpr uint16_t kOwnerKindB = 9;

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

cRZRandom &Random() { return *GameAddress<cRZRandom>(0x10d93f0); }

cICompositionTreeNode *AsNode(void *object) { return static_cast<cICompositionTreeNode *>(object); }

// An object's "ObjectLighting" extension (0xaeb5b0, nGZSceneGraph::FindDataList), or null.
void *ObjectLighting(void *object)
{
    return nGZSceneGraph::FindDataList(AsNode(object), GameAddress<const char>(kObjectLighting), nullptr);
}
// The extension's slot 15: the value `name` into *value; whether it has one.
bool GetValue(void *extension, uint32_t name, float *value)
{
    return Call<15, bool>(extension, GameAddress<const char>(name), value); // receiver: the "ObjectLighting" extension
}

// 0xaead10 (nGZSceneGraph::SubtreeHasFlag): whether the object's tree (its node flags, slots 48 / 41 / 37) asks for
// `kind`.
bool WantsLighting(void *object, uint32_t kind) { return nGZSceneGraph::SubtreeHasFlag(AsNode(object), kind); }
// 0xaeb3f0 (nGZSceneGraph::GetOrAddDataList): the extension `name` of the object, the one there when `reuse` is set,
// else a new one (the director's class 0x6a836d56); uncounted.
void *FindOrMakeExtension(void *object, const char *name, bool reuse)
{
    return nGZSceneGraph::GetOrAddDataList(AsNode(object), name, reuse);
}
// 0x3aab80 (ours, sg/DesignMode.h): the object's design-mode names into the four strings (the first and third the two
// slot 13 reads).
void DesignModeNames(void *object, cRZString *first, cRZString *third, cRZString *second, cRZString *fourth)
{
    ::DesignMode::GetObjectDesignModeNames(static_cast<nGZSceneGraph::cICompositionTreeNode *>(object), first, third,
                                         second, fourth);
}
// 0x3ab990 (ours, sg/DesignMode.h): the object's shape whose geometry has the subset `name`, or null.
void *FindDesignMode(void *object, cRZString *name)
{
    return ::DesignMode::FindObjectDesignMode(static_cast<nGZSceneGraph::cICompositionTreeNode *>(object),
                                            name->AsInterface());
}

// mulsd a, b: a NaN first operand wins (quieted).
double MulD(double a, double b)
{
    if (a == a) return a * b;
    uint64_t bits;
    std::memcpy(&bits, &a, 8);
    bits |= 0x0008000000000000ull;
    std::memcpy(&a, &bits, 8);
    return a;
}

nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter LightFilter()
{
    return {GameAddress<const void>(nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::kVTable), 0xf, cILightRefNode::kIid};
}

// Whether slot 13 lights a light whose tag is `tag` (see ApplyObjectLighting).
bool Picks(void *object, const char *tag, const char *name)
{
    if (name) return _stricmp(tag, name) == 0;
    if (!tag) return true;
    cRZString lowered;
    lowered.Construct(tag);
    reinterpret_cast<cRZStringHelpers *>(&lowered)->LowerCase();
    if (!reinterpret_cast<cRZStringHelpers *>(&lowered)
             ->EndsWith(GameAddress<const char>(kDesignModeSuffix), kDesignModeSuffixLength, 1)) {
        lowered.Destruct();
        return true;
    }
    cRZString stem;
    Sub(lowered).Left(&stem, static_cast<uint32_t>(std::strlen(tag)) - kDesignModeSuffixLength);
    cRZString first, second, third, fourth;
    first.Construct();
    second.Construct();
    third.Construct();
    fourth.Construct();
    DesignModeNames(object, &first, &third, &second, &fourth);
    bool picked = false;
    for (int i = 0; i < 2 && !picked; ++i) {
        cRZString copy;
        copy.Construct(i ? second : first, 0, 0xffffffff);
        if (copy.Strlen() != 0) {
            void *const mode = FindDesignMode(object, &copy);
            // receiver: the design mode (class not identified): slot 53 maps the name to the mode's light stem
            if (mode && _stricmp(Call<53, const char *>(mode, copy.ToChar()), stem.ToChar()) == 0) picked = true;
        }
        copy.Destruct();
    }
    fourth.Destruct();
    third.Destruct();
    second.Destruct();
    first.Destruct();
    stem.Destruct();
    lowered.Destruct();
    return picked;
}

} // namespace

bool StringsEqual(const char *a, const char *b) { return std::strcmp(a, b) == 0; }

cExtensionSearch *cExtensionSearch::Construct(cICompositionTreeNode *node, uint32_t iid)
{
    mObject = nullptr;
    for (;;) {
        mNode = node;
        for (cIExtension *ext = mNode->GetFirstExtension(); ext; ext = ext->GetNext()) { // receiver: cIObjectGraphNode, cIExtension
            DropHeld(mObject);
            if (ext->QueryInterface(iid, &mObject)) break; // receiver: cIExtension
        }
        cICompositionTreeNode *const parent = mNode->GetParent(); // receiver: cICompositionTreeNode
        if (mObject || !parent) break;
        node = parent;
    }
    return this;
}

void cLightingManager::PrepareObjectLighting(void *object)
{
    void *const owner = Call<120, void *>(object);                     // receiver: the object (its owner)
    const uint16_t kind = static_cast<uint16_t>(Call<98, uint32_t>(owner)); // receiver: the owner (class not identified)
    if (kind != kOwnerKindA && kind != kOwnerKindB) {
        if (WantsLighting(object, 2)) FindOrMakeExtension(object, GameAddress<const char>(kObjectLighting), true);
        return;
    }
    nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter filter = LightFilter();
    {
        NodeRange range;
        // receiver: the object (a cICompositionTreeNode face)
        static_cast<cICompositionTreeNode *>(object)->MakeIterator44(&range, 0, Word(&filter));
        while (!range.AtEnd()) {
            auto *const node = reinterpret_cast<cILightRefNode *>(range.mCursor->Get()); // receiver: cISceneNodeIterator
            const ScalarEvent off{GameAddress<const void>(kScalarEventVTable), 0.0f};
            SetAttribute(node, kAttrOne, &off);
            range.mCursor->Next(); // receiver: cISceneNodeIterator
        }
    }
    filter.Destruct();
}

void cLightingManager::ApplyObjectLighting(void *object, const char *name)
{
    void *const extension = ObjectLighting(object);
    if (!extension) return;
    const cLightSet &set = CurrentSet();
    float strength = 1.0f;
    GetValue(extension, kStrength, &strength);
    const float one = *GameAddress<const float>(kOne);
    const float jitter = mObjectLightJitter;
    const double drawn =
        Random().RandomDoubleRangeUniform(static_cast<double>(SubS(one, jitter)), static_cast<double>(AddS(jitter, one)));
    strength = static_cast<float>(MulD(static_cast<double>(strength), drawn));
    if (!Switch(kDiffusionLighting)) strength = 0.0f;
    const uint32_t key = SampleKey(0);

    nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter filter = LightFilter();
    {
        NodeRange range;
        // receiver: the object (a cICompositionTreeNode face)
        static_cast<cICompositionTreeNode *>(object)->MakeIterator44(&range, 0, Word(&filter));
        while (!range.AtEnd()) {
            cISceneNode *const item = range.mCursor->Get(); // receiver: cISceneNodeIterator
            cILightRefNode *light = nullptr;
            item->QueryInterface(cILightRefNode::kIid, reinterpret_cast<void **>(&light)); // receiver: cISceneNode
            const char *const tag = light->GetTag(); // receiver: cILightRefNode
            if (Picks(object, tag, name)) {
                uint32_t state[2];
                const cLightSet::Modifier m =
                    light->GetLayerState(state)[1] == key ? set.mModifiers[5] : set.mModifiers[4]; // receiver: cILightRefNode
                cExtensionSearch search;
                search.Construct(light, cIDiffusionLightExtension::kIid);
                void *const held = search.mObject;
                // ORIGINAL-BUG: the search's reference is released before the holder takes its own (0x2e55c1, then
                // 0x2e55dc): the last reference could go in between.
                if (search.mObject) ReleaseThroughBase(search.mObject);
                if (held) AddRefThroughBase(held);
                if (held) {
                    auto *const ext = static_cast<cIDiffusionLightExtension *>(held);
                    ext->ResetFlags();                                   // receiver: cIDiffusionLightExtension
                    const float intensity = ext->GetSourceIntensity();   // receiver: cIDiffusionLightExtension
                    ext->SetSourceIntensity(MulS(MulS(MulS(intensity, strength), m.c), mField50));
                    const float falloff = ext->GetSourceFalloff();       // receiver: cIDiffusionLightExtension
                    ext->SetSourceFalloff(MulS(m.d, falloff));
                }
                light->DropLightCopy(); // receiver: cILightRefNode
                if (light->GetLight()) { // receiver: cILightRefNode
                    ScalarEvent one{GameAddress<const void>(kScalarEventVTable), 0.0f};
                    GetAttribute(light, kAttrOne, &one);
                    one.mValue = MulS(MulS(MulS(m.a, strength), mField50), one.mValue);
                    SetAttribute(light, kAttrOne, &one);
                    ScalarEvent value{GameAddress<const void>(kScalarEventVTable), 0.0f};
                    GetAttribute(light, kAttrValue, &value);
                    value.mValue = MulS(value.mValue, m.b);
                    SetAttribute(light, kAttrValue, &value);
                }
                if (held) ReleaseThroughBase(held);
            } else if (light->GetLight()) { // receiver: cILightRefNode
                const ScalarEvent off{GameAddress<const void>(kScalarEventVTable), 0.0f};
                SetAttribute(light, kAttrOne, &off);
                cExtensionSearch search;
                search.Construct(light, cIDiffusionLightExtension::kIid);
                void *const held = search.mObject;
                // ORIGINAL-BUG: as above, released before the holder's AddRef.
                if (search.mObject) ReleaseThroughBase(search.mObject);
                if (held) AddRefThroughBase(held);
                if (held) static_cast<cIDiffusionLightExtension *>(held)->SetSourceIntensity(0.0f); // receiver: cIDiffusionLightExtension
                if (held) ReleaseThroughBase(held);
            }
            if (light) light->Release(); // receiver: cILightRefNode
            range.mCursor->Next();       // receiver: cISceneNodeIterator
        }
    }
    filter.Destruct();
}

} // namespace nTSSG
