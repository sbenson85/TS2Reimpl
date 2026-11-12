// nGZSceneGraph's free helpers, part 5 (lane 261111f): tree walks and lookups. Declarations in sg/SGUtilities.h.
#include "sg/SGUtilities.h"

#include "game/vtable.h"
#include <string.h>

#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "misc/UtilitiesE.h"   // GetCachedQueriedObject
#include "rz/StringKeyParser.h"
#include "rz/cRZString.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/cDataListExtension.h"
#include "sg/cMaterialDefinition.h"
#include "sg/cMaterialManager.h"
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"

namespace nGZSceneGraph {

namespace {

bool HasFlagBit(cICompositionTreeNode *node, int32_t flag) { return flag >= 0 && node->TestFlag(static_cast<uint32_t>(flag)); }

// The face's virtual base cIExtension (vbptr at +4, the base's offset in its second entry).
cIExtension *ExtensionOf(void *face)
{
    char *p = static_cast<char *>(face) + 4;
    return reinterpret_cast<cIExtension *>(p + (*reinterpret_cast<int32_t **>(p))[1]);
}

constexpr uint32_t kBoundedFlag = 1, kShapeRefFlag = 4, kRenderableFlag = 5;
constexpr int kSetBoundFlag = 66;      // the bounded node's slot 66, passed the raw word
constexpr int kShapeSlot45 = 45;       // a shape's slot 45 (argument 0)
constexpr int kSlot77 = 77;
constexpr uint32_t kSlot77Value = 0x2fa18737;
constexpr int kSlot50 = 50, kSlot39 = 39;                                  // 0xaebc70: a node's slot 50 object, its slot 39
constexpr int kSlot42 = 42, kIsEqualSlot = 4, kGetSlot = 5, kNextSlot = 6; // 0xaed340: the builder's range, its iterator
constexpr int kBindSlot = 34;                                              // 0xaed340: the part's slot 34 (name, item)

} // namespace

// 0xae6870
uint32_t NameCrc(const char *name)
{
    if (!name || !*name) return 0;
    return StringKeyParser::Crc32(name, 1);
}

// 0xae4470
bool Slot77IsNotDefault(void *object) { return RawSlot<kSlot77, uint32_t>(object) != kSlot77Value; }

// 0xaeb6a0
void RemoveDataLists(cICompositionTreeNode *node, const char *name)
{
    cIDataListExtension *list = FindDataList(node, name, nullptr);
    while (list) {
        cIDataListExtension *next = FindDataList(node, name, list);
        node->RemoveExtension(ExtensionOf(list));
        list = next;
    }
}

// 0xaeb520
cIDataListExtension *FindDataListInTree(cICompositionTreeNode *node, const char *name, int32_t skipFlag)
{
    if (cIDataListExtension *found = FindDataList(node, name, nullptr)) return found;
    const cChildRange children = node->GetChildrenConst();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSiblingConst()) {
        if (HasFlagBit(child, skipFlag)) continue;
        if (cIDataListExtension *found = FindDataListInTree(child, name, skipFlag)) return found;
    }
    return nullptr;
}

// 0xaeb070
void SetLayerTagInTree(cICompositionTreeNode *node, uint32_t tag)
{
    if (node->TestFlag(kRenderableFlag)) static_cast<cIRenderableNode *>(node)->SetLayerTag(tag);
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling())
        SetLayerTagInTree(child, tag);
}

// 0xaeb190
void SetLayerBitsInTree(cICompositionTreeNode *node, uint32_t add, uint32_t remove)
{
    if (node->TestFlag(kRenderableFlag)) {
        auto *renderable = static_cast<cIRenderableNode *>(node);
        renderable->AddLayerBits(add);
        renderable->RemoveLayerBits(remove);
    }
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling())
        SetLayerBitsInTree(child, add, remove);
}

// 0xaeb210
void SetBoundFlagInTree(cICompositionTreeNode *node, uint32_t value, uint32_t maskA, uint32_t maskB, uint32_t maskC,
                        uint32_t maskD)
{
    const uint32_t flags = node->GetFlags();
    if ((maskB & flags) && !(maskD & flags)) return;
    if (!(maskA & flags) && (maskC & flags) && node->TestFlag(kBoundedFlag)) RawSlot<kSetBoundFlag, uint32_t>(node, value);
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling())
        SetBoundFlagInTree(child, value, maskA, maskB, maskC, maskD);
}

// 0xae7e60
void ResetShapesInTree(cICompositionTreeNode *node)
{
    if (node->TestFlag(kShapeRefFlag)) {
        const cShapeRange range = static_cast<cIShapeRefNode *>(node)->GetShapes();
        for (cIShape **shape = range.mCurrent; shape != range.mEnd; ++shape) RawSlot<kShapeSlot45, uint32_t>(*shape, 0);
    }
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling())
        ResetShapesInTree(child);
}

namespace {
constexpr uint32_t kShapeRefIid = 0xa516cd10, kTransformIid = 0xe51c10f5;

// The held pointer of a QueryInterface cell: stored null before its Release, as the original does.
void ReleaseCell(cIGZUnknown *&cell)
{
    if (cIGZUnknown *old = cell) {
        cell = nullptr;
        old->Release();
    }
}
} // namespace

// 0xaeb8a0
cIGZUnknown *ShapeRefOfNodeOrChild(cICompositionTreeNode *node)
{
    if (!node) return nullptr;
    cIGZUnknown *held = nullptr;
    if (node->QueryInterface(kShapeRefIid, reinterpret_cast<void **>(&held))) {
        cIGZUnknown *found = held;
        if (found) found->Release();
        return found;
    }
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling()) {
        ReleaseCell(held);
        if (child->QueryInterface(kShapeRefIid, reinterpret_cast<void **>(&held))) {
            cIGZUnknown *found = held;
            if (found) found->Release();
            return found;
        }
    }
    if (held) held->Release();
    return nullptr;
}

// 0xaeb9a0
cIGZUnknown *TransformOfChild(cICompositionTreeNode *node)
{
    if (!node) return nullptr;
    cIGZUnknown *held = nullptr;
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling()) {
        ReleaseCell(held);
        child->QueryInterface(kTransformIid, reinterpret_cast<void **>(&held)); // the answer is not looked at, only the cell
        if (held) {
            cIGZUnknown *found = held;
            found->Release();
            return found;
        }
    }
    if (held) held->Release();
    return nullptr;
}

// 0xaebb00
cIGZUnknown *ShapeRefInSubtree(cICompositionTreeNode *node)
{
    if (!node) return nullptr;
    cIGZUnknown *held = nullptr;
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling()) {
        ReleaseCell(held);
        child->QueryInterface(kShapeRefIid, reinterpret_cast<void **>(&held));
        if (held) {
            cIGZUnknown *found = held;
            found->Release();
            return found;
        }
    }
    const cChildRange again = node->GetChildren();
    for (cICompositionTreeNode *child = again.mCurrent; child != again.mEnd; child = child->GetNextSibling()) {
        cIGZUnknown *const found = ShapeRefInSubtree(child);
        if (found != held) {
            cIGZUnknown *const previous = held;
            if (found) found->AddRef();
            held = found;
            if (previous) previous->Release();
        }
        if (held) {
            cIGZUnknown *result = held;
            result->Release();
            return result;
        }
    }
    if (held) held->Release();
    return nullptr;
}

// 0xaeba70
cIGZUnknown *ShapeRefOfNodeOrSubtree(cICompositionTreeNode *node)
{
    if (!node) return nullptr;
    cIGZUnknown *held = nullptr;
    node->QueryInterface(kShapeRefIid, reinterpret_cast<void **>(&held));
    cIGZUnknown *result = held ? held : ShapeRefInSubtree(node);
    if (held) held->Release();
    return result;
}

// 0xaebc70
bool PassToSlot50Object(cICompositionTreeNode *node, uint32_t argument)
{
    if (!node) return false;
    cIGZUnknown *const target = RawSlot<kSlot50, cIGZUnknown *>(node);
    if (target) target->AddRef();
    bool done = false;
    if (target) {
        RawSlot<kSlot39, uint32_t>(target, argument);
        done = true;
    }
    if (target) target->Release();
    return done;
}

// 0xaed340
namespace {
struct cBuilderRange {
    cIGZUnknown *mCurrent, *mMiddle, *mEnd; // counted iterators
};
} // namespace
void BindNamedBuilderItems(const char *name, cIGZUnknown *builder, cIGZUnknown *part)
{
    if (!builder || !part) return;
    cBuilderRange range;
    RawSlot<kSlot42, cBuilderRange *>(builder, &range);
    while (!RawSlot<kIsEqualSlot, bool>(range.mCurrent, range.mEnd)) {
        const uint32_t item = RawSlot<kGetSlot, uint32_t>(range.mCurrent);
        RawSlot<kBindSlot, uint32_t>(part, name, item);
        RawSlot<kNextSlot, uint32_t>(range.mCurrent);
    }
    if (range.mEnd) range.mEnd->Release();
    if (range.mMiddle) range.mMiddle->Release();
    if (range.mCurrent) range.mCurrent->Release();
}

// 0xaea980
void CollectNodes(cICompositionTreeNode *node, std::vector<void *> &out)
{
    if (!node) return;
    out.push_back(node);
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling())
        CollectNodes(child, out);
}

// 0xaea9f0
void CollectShapes(cICompositionTreeNode *node, std::vector<void *> &out)
{
    if (!node) return;
    cIGZUnknown *held = nullptr;
    node->QueryInterface(kShapeRefIid, reinterpret_cast<void **>(&held)); // the answer is not looked at, only the cell
    if (held) {
        const cShapeRange range = static_cast<cIShapeRefNode *>(static_cast<cIGZUnknown *>(held))->GetShapes();
        for (cIShape **shape = range.mCurrent; shape != range.mEnd; ++shape) out.push_back(*shape);
    }
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling())
        CollectShapes(child, out);
    if (held) held->Release();
}

// 0xae6900
void AppendSideSwappedName(const char *name, cIGZUnknown *builder)
{
    constexpr int kAppend = 4, kAppendN = 20; // the builder's slot 4 (a string), 20 (a string and its length)
    const char *const kLeft = "l_", *const kRight = "r_";
    size_t length = 0;
    while (name[length]) ++length;
    if (length >= 2) {
        const bool left = name[0] == kLeft[0] && name[1] == kLeft[1];
        const bool right = name[0] == kRight[0] && name[1] == kRight[1];
        if (left || right) {
            RawSlot<kAppend, uint32_t>(builder, left ? kRight : kLeft);
            RawSlot<kAppendN, uint32_t>(builder, name + 2, static_cast<uint32_t>(length - 2));
            return;
        }
    }
    RawSlot<kAppend, uint32_t>(builder, name);
}

namespace {
struct cNamePair {
    const char *mFirst;
    const char *mSecond;
};
} // namespace

// 0xaea840
int32_t IndexOfNamedPair(cIGZUnknown *owner, uint32_t key, const char *first, const char *second)
{
    constexpr int kRangeSlot = 51;
    cBuilderRange range;
    RawSlot<kRangeSlot, cBuilderRange *>(owner, &range, key);
    int32_t index = 0;
    int32_t result = -1;
    while (!RawSlot<kIsEqualSlot, bool>(range.mCurrent, range.mEnd)) {
        char scratch[16];
        const cNamePair *item = RawSlot<kGetSlot, const cNamePair *>(range.mCurrent, scratch);
        if (_stricmp(second, item->mSecond) == 0) {
            item = RawSlot<kGetSlot, const cNamePair *>(range.mCurrent, scratch);
            if (_stricmp(first, item->mFirst) == 0) {
                result = index;
                break;
            }
        }
        RawSlot<kNextSlot, uint32_t>(range.mCurrent);
        ++index;
    }
    if (range.mEnd) range.mEnd->Release();
    if (range.mMiddle) range.mMiddle->Release();
    if (range.mCurrent) range.mCurrent->Release();
    return result;
}

// 0xaeab90
bool ApplyNamedPairs(uint32_t classArg, uint32_t setupArg, uint32_t mode, const char *first, const char *second,
                     uint32_t value, cIGZUnknown **cell)
{
    constexpr uint32_t kClass = 0x9d054f52, kIid = 0xfd054f4b;
    constexpr int kCreate = 3, kSetup = 16, kGetMode = 19, kClear = 18, kSetMode = 17, kReload = 3, kCount = 20, kEntry = 21,
                  kApply = 7;
    if (!*cell) {
        RawSlot<kCreate, uint32_t>(GetDirectorFrameworkObject(), kClass, kIid, cell);
        RawSlot<kSetup, uint32_t>(*cell, setupArg);
    }
    if (RawSlot<kGetMode, uint32_t>(*cell) != mode) {
        RawSlot<kClear, uint32_t>(*cell);
        RawSlot<kSetMode, uint32_t>(*cell, mode);
        RawSlot<kReload, uint32_t>(*cell, classArg);
    }
    bool applied = false;
    const uint32_t count = RawSlot<kCount, uint32_t>(*cell);
    for (uint32_t i = 0; i < count; ++i) {
        cIGZUnknown *entry = RawSlot<kEntry, cIGZUnknown *>(*cell, i);
        const int32_t index = IndexOfNamedPair(entry, mode, first, second);
        if (index != -1) {
            RawSlot<kApply, uint32_t>(entry, mode, static_cast<uint32_t>(index), value);
            applied = true;
        }
    }
    return applied;
}

// ---- lane 261111l ----

// 0xae68a0
void SwapSidePrefix(cRZString *name)
{
    constexpr int kReplace = 25; // cIGZString's Replace(pos, text, count)
    using Replace = void *(__thiscall *)(void *, uint32_t, const char *, uint32_t);
    const char *const kLeft = "l_", *const kRight = "r_";
    if (name->mString.size() < 2) return;
    if (name->StartsWith(kLeft, 2, true))
        VirtualCall<Replace, kReplace>(name, 0u, kRight, 2u);
    else if (name->StartsWith(kRight, 2, true))
        VirtualCall<Replace, kReplace>(name, 0u, kLeft, 2u);
}

// 0xaeaae0
bool IsAncestorOf(cICompositionTreeNode *ancestor, cICompositionTreeNode *node)
{
    cICompositionTreeNode *parent = node;
    do parent = parent->GetParentConst();
    while (parent && parent != ancestor);
    return parent == ancestor;
}

// 0xaeab10
const char *NodeKindName(uint32_t kind)
{
    static const char *const kNames[] = {"TransformNode", "BoundedNode",   "LightRefNode",      "ResourceNode",
                                         "ShapeRefNode",  "RenderableNode", "PartitionNode",    "SceneNode",
                                         "ViewerRefNodeBase", "ExplicitBoundNode"};
    return kind < sizeof(kNames) / sizeof(kNames[0]) ? kNames[kind] : "UNKNOWN";
}

// 0xaeac50
void SetFlagInTree(cICompositionTreeNode *node, uint32_t bit, uint32_t value)
{
    constexpr int kSetFlag = 29; // SetFlag(bit, value), given the caller's whole word
    RawSlot<kSetFlag, void>(node, bit, value);
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling())
        SetFlagInTree(child, bit, value);
}

CountedServiceCell::CountedServiceCell(const char *text)
{
    Open(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text)), 0);
}

namespace {

// A shape's binding range (its slot 54 with no subset): three counted cursors (first, a middle one, end). Cursor slots:
// 2 Release, 4 at the given end, 5 the current binding, 6 next (as cShapeRefNode.cpp reads them).
struct cBindingRange {
    void *mCursor;
    void *mMiddle;
    void *mEnd;
};
struct cBinding {
    uint32_t mWord0;
    uint32_t mMaterialIndex; // +0x04
    uint32_t mRest[2];
};
bool CursorAtEnd(void *cursor, void *end) { return VirtualCall<bool(__thiscall *)(void *, void *), 4>(cursor, end); }
void CursorGet(void *cursor, cBinding *out) { VirtualCall<void(__thiscall *)(void *, cBinding *), 5>(cursor, out); }
void CursorNext(void *cursor) { VirtualCall<void(__thiscall *)(void *), 6>(cursor); }
void ReleaseCursor(void *cursor)
{
    if (cursor) VirtualCall<void(__thiscall *)(void *), 2>(cursor);
}

} // namespace

// 0xae8090
void CollectTextureNames(std::vector<CountedServiceCell> *out, cICompositionTreeNode *node)
{
    if (node->TestFlag(kShapeRefFlag)) {
        const cShapeRange shapes = static_cast<cIShapeRefNode *>(node)->GetShapesConst();
        for (cIShape **shape = shapes.mFirst; shape != shapes.mEnd; ++shape) {
            cBindingRange range;
            (*shape)->GetSubsetBindings(&range, nullptr);
            while (!CursorAtEnd(range.mCursor, range.mEnd)) {
                cIMaterialManager *manager = static_cast<cIMaterialManager *>(GetCachedQueriedObject());
                cBinding binding;
                CursorGet(range.mCursor, &binding);
                const MaterialInfo info = manager->GetInfo(static_cast<int32_t>(binding.mMaterialIndex));
                // ORIGINAL-BUG: a binding whose index has no material (GetInfo's null) is read through.
                cIMaterialDefinition *material = static_cast<cIMaterialDefinition *>(info.material);
                const uint32_t count = material->CountTextures();
                for (uint32_t i = 0; i < count; ++i) {
                    const CountedServiceCell name(material->GetTextureName(i));
                    out->push_back(name);
                }
                CursorNext(range.mCursor);
            }
            ReleaseCursor(range.mEnd);
            ReleaseCursor(range.mMiddle);
            ReleaseCursor(range.mCursor);
        }
    }
    const cChildRange children = node->GetChildrenConst();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSiblingConst())
        CollectTextureNames(out, child);
}

} // namespace nGZSceneGraph
