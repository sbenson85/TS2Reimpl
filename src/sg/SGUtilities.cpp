// nGZSceneGraph's free helpers (lane 261111b). See the header.
#include "sg/SGUtilities.h"

#include "compgeom/CompGeom.h"
#include "compgeom/Frames.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/ExpressionEvaluator.h"
#include "misc/Quaternion.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "sg/SGNodeInterfaces.h"
#include "sg/cDataListExtension.h"
#include "sg/cShapeRefNode.h"

#include <cstring>
#include <string.h> // _stricmp

namespace nGZSceneGraph {

namespace {

// ---- property readers ----

constexpr int kParserGetFunctions = 23, kParserFindVariable = 27;

// The variable's value, or false when the parser is null or has no such variable.
bool EvaluateVariable(void *parser, const char *name, gzexpr::Value *value)
{
    if (!parser) return false;
    // receiver: the parser's cBaseParser face (gz/cBaseParser.h).
    const char *text = RawSlot<kParserFindVariable, const char *>(parser, name);
    if (!text) return false;
    auto *functions = RawSlot<kParserGetFunctions, nGZCommandParser::EvalFunctionTable *>(parser);
    nGZCommandParser::ExpressionEvaluator::Of(functions)->Evaluate(value, text);
    return true;
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

// The float's bits on a two's complement line (sign-magnitude folded: -0 and +0 both 0).
uint32_t OrderedBits(float f)
{
    const uint32_t u = Bits(f), sign = u >> 31;
    return (u ^ ((0u - sign) >> 1)) + sign;
}

// Whether two floats are more than 31 units in the last place apart (the difference taken modulo 2^32).
bool FarApart(float now, float before)
{
    const int32_t d = static_cast<int32_t>(OrderedBits(now) - OrderedBits(before));
    return ((d >> 26) ^ d) & ~31;
}

constexpr float kRoundingBias = 12582912.0f; // 0xd9ff00: 1.5 * 2^23
constexpr uint32_t kRoundingBiasBits = 0x4b400000;

// ---- tree helpers ----

bool HasFlag(cICompositionTreeNode *node, int32_t flag) { return flag >= 0 && node->TestFlag(static_cast<uint32_t>(flag)); }

bool TagIs(cICompositionTreeNode *node, const char *name)
{
    const char *tag = GetNodeTag(node);
    return tag && _stricmp(tag, name) == 0;
}

// A face's virtual base number `index` (vbptr at +4; entry 1 the first base, entry 2 the second).
void *VirtualBase(void *face, int index)
{
    char *p = static_cast<char *>(face) + 4;
    return p + (*reinterpret_cast<int32_t **>(p))[index];
}

// The face's virtual base cIExtension (vbptr at +4, the base's offset in its second entry).
cIExtension *ExtensionBase(void *face) { return static_cast<cIExtension *>(VirtualBase(face, 1)); }

constexpr uint32_t kChannelExtensionIid = 0x48ece762, kChannelOwnerIid = 0x6a6fd04d;
constexpr int kAnimatedFindExtension = 59; // the animated face's (iid, out, start)
constexpr int kChannelQuery = 12;          // the extension's second virtual base: (iid, out, start)

constexpr uint32_t kAnimatedFaceIid = 0x1aff3bd4;
constexpr uint32_t kDataListClass = 0x6a836d56;
constexpr int kFrameworkCreate = 3; // the framework's CreateInstance(class, iid, out)

// The -Z axis: the negated {0, 0, 1} at 0x1073698 (only read, by AimNode).
constexpr float kFromAxis[3] = {-0.0f, -0.0f, -1.0f};

} // namespace

// 0xaf7190
bool ReadFloatProperty(void *parser, const char *name, float *value, uint8_t *changed)
{
    gzexpr::Value v;
    if (!EvaluateVariable(parser, name, &v) || v.type != 1) return false;
    if (changed) *changed = !(v.v[0] == *value);
    *value = v.v[0];
    return true;
}

// 0xaf7210
bool ReadIntProperty(void *parser, const char *name, int32_t *value, uint8_t *changed)
{
    gzexpr::Value v;
    if (!EvaluateVariable(parser, name, &v) || v.type != 1) return false;
    const int32_t rounded = static_cast<int32_t>(Bits(v.v[0] + kRoundingBias) - kRoundingBiasBits);
    if (changed) *changed = rounded != *value;
    *value = rounded;
    return true;
}

// 0xaf7110
bool ReadBoolProperty(void *parser, const char *name, uint8_t *value, uint8_t *changed)
{
    gzexpr::Value v;
    if (!EvaluateVariable(parser, name, &v) || v.type != 1) return false;
    const uint8_t set = !(v.v[0] == 0.0f);
    if (changed) *changed = set != *value;
    *value = set;
    return true;
}

// 0xaf7290
bool ReadVector3Property(void *parser, const char *name, float *value, uint8_t *changed)
{
    gzexpr::Value v;
    if (!EvaluateVariable(parser, name, &v) || (v.type != 3 && v.type != 4)) return false;
    if (changed)
        *changed = FarApart(v.v[0], value[0]) || FarApart(v.v[1], value[1]) || FarApart(v.v[2], value[2]);
    std::memcpy(value, v.v, 3 * sizeof(float));
    return true;
}

// 0xae78f0
void SetNodeTag(cICompositionTreeNode *node, const char *tag) { node->SetTag(tag); }

// 0xae7900
const char *GetNodeTag(cICompositionTreeNode *node) { return node->GetTag(); }

// 0xae7490
cITransformNode *FindNodeByName(const char *name, cICompositionTreeNode *node, int32_t skipFlag)
{
    cITransformNode *held = nullptr;
    const bool isTransform = node->QueryInterface(cITransformNode::kIid, reinterpret_cast<void **>(&held));
    cITransformNode *found = nullptr;
    if (isTransform && TagIs(node, name)) {
        found = held;
    } else {
        const cChildRange children = node->GetChildren();
        for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling()) {
            if (HasFlag(child, skipFlag)) continue;
            if ((found = FindNodeByName(name, child, skipFlag)) != nullptr) break;
        }
    }
    if (held) held->Release();
    return found;
}

// 0xae7820
bool FindNodeOfKindByName(const char *name, cICompositionTreeNode *node, int32_t skipFlag, uint32_t iid, void **out)
{
    if (node->IsKindOf(iid) && TagIs(node, name)) return node->QueryInterface(iid, out);
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling()) {
        if (HasFlag(child, skipFlag)) continue;
        if (FindNodeOfKindByName(name, child, skipFlag, iid, out)) return true;
    }
    return false;
}

// 0xaead10
bool SubtreeHasFlag(cICompositionTreeNode *node, uint32_t bit)
{
    if (node->TestFlag(bit)) return true;
    const cChildRange children = node->GetChildrenConst();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd;
         child = child->GetNextSiblingConst())
        if (SubtreeHasFlag(child, bit)) return true;
    return false;
}

// 0xaeb0e0
void SetLayersByFlag(cICompositionTreeNode *node, uint32_t bit, const char *add, const char *remove, int32_t skipFlag)
{
    if (node->TestFlag(bit)) {
        // receiver: a renderable node (every node carrying the caller's flag is one).
        auto *renderable = static_cast<cIRenderableNode *>(node);
        if (add) renderable->AddLayer(add);
        if (remove) renderable->RemoveLayer(remove);
    }
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling()) {
        if (HasFlag(child, skipFlag)) continue;
        SetLayersByFlag(child, bit, add, remove, skipFlag);
    }
}

// 0xaed420
void ForEachShapeBind(cICompositionTreeNode *node, void *a, const char *b)
{
    cIShapeRefNode *shapes = nullptr;
    if (node->QueryInterface(cIShapeRefNode::kIid, reinterpret_cast<void **>(&shapes))) {
        const cShapeRange range = shapes->GetShapes();
        for (cIShape **shape = range.mFirst; shape != range.mEnd; ++shape)
            RawSlot<34, uint32_t>(*shape, a, b); // receiver: a cIShape (cShape's slot 34)
    }
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling())
        ForEachShapeBind(child, a, b);
    if (shapes) shapes->Release();
}

// 0xae7d00
void GrowByShapeBounds(float *box, int32_t skipFlag, cICompositionTreeNode *frameNode, cICompositionTreeNode *node)
{
    if (node->TestFlag(cShapeRefNode::kShapeRefFlag)) {
        // receiver: cIShapeRefNode (flag 4 marks the shape-ref nodes).
        auto *shapes = static_cast<cIShapeRefNode *>(node);
        if (!shapes->IsEverywhere() && shapes->AnyShapeSlot52()) {
            cRigidTransform inverse = frameNode->GetWorldTransform();
            auto *rigid = reinterpret_cast<nGZCompGeom::RigidTransform3 *>(&inverse);
            rigid->Invert();
            nGZCompGeom::FrameMath frame;
            frame.mUnknown0 = 1;
            std::memcpy(frame.mOffset, &rigid->translation, sizeof frame.mOffset);
            frame.SetRotation(rigid->rotation);
            const cBoundBox bounds = shapes->GetBoundBoxIn(reinterpret_cast<const cTransform34 *>(&frame));
            reinterpret_cast<nGZCompGeom::BoundBox *>(box)->Grow(
                *reinterpret_cast<const nGZCompGeom::BoundBox *>(&bounds));
        }
    }
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling()) {
        if (HasFlag(child, skipFlag)) continue;
        GrowByShapeBounds(box, skipFlag, frameNode, child);
    }
}

// 0xae1ed0
void *AnimatedFaceOf(cICompositionTreeNode *node)
{
    void *face = nullptr;
    void *result = node->FindExtension(kAnimatedFaceIid, &face, nullptr) ? face : nullptr;
    if (face) ExtensionBase(face)->Release();
    return result;
}

// 0xae42c0
void *ChannelOwnerOf(void *animated)
{
    using Find = uint8_t(__thiscall *)(void *, uint32_t, void **, void *);
    void *extension = nullptr;
    void *owner = nullptr;
    // receiver: the animated face (slot 59 finds its extensions).
    if (VirtualCall<Find, kAnimatedFindExtension>(animated, kChannelExtensionIid, &extension, nullptr)) {
        void *answer = nullptr;
        // receiver: the channel extension's second virtual base.
        const bool found =
            VirtualCall<Find, kChannelQuery>(VirtualBase(extension, 2), kChannelOwnerIid, &answer, nullptr) != 0;
        if (answer) ExtensionBase(answer)->Release();
        if (found) owner = answer;
    }
    if (extension) ExtensionBase(extension)->Release();
    return owner;
}

// 0xae43a0 (lane 261111l)
void *ChannelSourceOf(void *animated)
{
    constexpr int kOwnerSource = 8; // the channel owner's slot 8
    void *owner = ChannelOwnerOf(animated);
    return owner ? VirtualCall<void *(__thiscall *)(void *), kOwnerSource>(owner) : nullptr;
}

// 0xaeb5b0
cIDataListExtension *FindDataList(cICompositionTreeNode *node, const char *name, cIDataListExtension *after)
{
    cIDataListExtension *current = after;
    for (;;) {
        cIExtension *start = nullptr;
        if (current) {
            start = static_cast<cIExtension *>(current)->GetNext();
            if (!start) return nullptr;
        }
        cIDataListExtension *found = nullptr;
        if (!node->FindExtension(cIDataListExtension::kIid, reinterpret_cast<void **>(&found), start)) {
            if (found) static_cast<cIExtension *>(found)->Release();
            return nullptr;
        }
        const bool match = _stricmp(found->GetListName(), name) == 0;
        static_cast<cIExtension *>(found)->Release();
        if (match) return found;
        current = found;
    }
}

// 0xaeb3f0
cIDataListExtension *GetOrAddDataList(cICompositionTreeNode *node, const char *name, bool reuse)
{
    cIDataListExtension *held = nullptr;
    if (reuse) {
        cIDataListExtension *existing = FindDataList(node, name, nullptr);
        if (existing) static_cast<cIExtension *>(existing)->AddRef();
        held = existing;
        if (held) {
            static_cast<cIExtension *>(held)->Release();
            return held;
        }
    }
    void *framework = GetDirectorFrameworkObject();
    // receiver: the framework (cIGZFrameworkW32's slot 3, CreateInstance).
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **), kFrameworkCreate>(
        framework, kDataListClass, cIDataListExtension::kIid, reinterpret_cast<void **>(&held));
    node->AddExtension(static_cast<cIExtension *>(held));
    // ORIGINAL-BUG: a failed creation is named through null (the original does not check either).
    held->SetListName(name);
    if (held) static_cast<cIExtension *>(held)->Release();
    return held;
}

// 0xaeb2c0
const float *RelativeTransform(float *out, cICompositionTreeNode *bone, cICompositionTreeNode *node)
{
    cRigidTransform inverse = node->GetWorldTransform();
    reinterpret_cast<nGZCompGeom::RigidTransform3 *>(&inverse)->Invert();
    cRigidTransform relative = bone->GetWorldTransform();
    reinterpret_cast<nGZCompGeom::RigidTransform3 *>(&relative)
        ->Compose(reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(&inverse));
    std::memcpy(out, relative.mValues, sizeof relative.mValues);
    return out;
}

// 0xaeee30
void AimNode(cITransformNode *node, const float *direction)
{
    math::Quaternion rotation;
    node->SetRotation(&rotation.SetRotationBetween(kFromAxis, direction)->x);
}

// 0xaf22d0
void ProjectBox(float *lo, float *hi, const void *box, const float *matrix)
{
    BoxProjection p;
    p.mOrthographic = matrix[14] == 0.0f && matrix[13] == 0.0f;
    p.Project(box, matrix);
    std::memcpy(lo, p.mLo, sizeof p.mLo);
    std::memcpy(hi, p.mHi, sizeof p.mHi);
}

} // namespace nGZSceneGraph
