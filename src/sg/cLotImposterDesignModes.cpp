// Design modes (lane 261108j). Layouts and members: src/sg/cLotImposterDesignModes.h.
#include "sg/cLotImposterDesignModes.h"

#include "app/AppDebugger.h"  // appdebugger::FreeStringBuffer
#include "app/AppServices2.h" // as2::RawString
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/ServiceGetters.h"  // CachedServiceGetter
#include "rz/StringKeyParser.h" // cRZStringHelpers::EndsWith, LowerCase
#include "rz/cRZString.h"
#include "rz/cRZStringSub.h"
#include "sg/DesignMode.h"        // FormatMaterialName, ListedSlaveSubsets, CollectSubsetNames
#include "sg/SGLightInterfaces.h" // cIReferentNode::GetReferrerCount
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h"
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"
#include "sg/cSkinExtension.h" // NextSkinExtension

#include <vector>

using nGZSceneGraph::cICompositionTreeNode;
using nGZSceneGraph::cIResourceNode;
using nGZSceneGraph::cIShape;
using nGZSceneGraph::cIShapeRefNode;
using nTSWorld::cMapString;

namespace {

constexpr uint32_t kResourceNodeIid = 0xe519c92c; // nGZSceneGraph::cIResourceNode
constexpr uint32_t kShapeRefNodeIid = 0xa516cd10; // nGZSceneGraph::cIShapeRefNode
constexpr uint32_t kShapeIid = 0x3c6eb1f0;        // what a shape's clone is asked for
constexpr uint32_t kNpos = 0xffffffffu;
const char kLodTag[] = "SWX";   // a level-of-detail tag: these and two more characters
constexpr uint32_t kLodTagLength = 5;
const char kLod90[] = "lod90";
const char kSeparator[] = "_";

cIGZString *Face(cMapString &s) { return reinterpret_cast<cIGZString *>(&s); }
const cIGZString &FaceRef(const cMapString &s) { return *reinterpret_cast<const cIGZString *>(&s); }
const cRZString &AsRZ(const cMapString &s) { return reinterpret_cast<const cRZString &>(s); }

// A cRZString built in place by a helper that constructs on raw memory (Mid, Left, Construct, BaseName); destroyed here.
struct BuiltString {
    alignas(cRZString) unsigned char mBytes[sizeof(cRZString)];
    BuiltString() = default;
    BuiltString(const BuiltString &) = delete;
    BuiltString &operator=(const BuiltString &) = delete;
    cRZString *Raw() { return reinterpret_cast<cRZString *>(mBytes); }
    cMapString *Map() { return reinterpret_cast<cMapString *>(mBytes); }
    ~BuiltString() { Raw()->Destruct(); }
};

void *SeasonManager()
{
    void *system = CachedServiceGetter<0x10cc458, 0x9a353ce8, 0xfa353cdb>();
    return RawSlot<66, void *>(system); // receiver: nTSSG::cITSSGSystem (0x254bc0, ret 0)
}

// `*slot` = `node`, counted (the new one held first, the old one released), unless it is already there.
void Assign(cIResourceNode **slot, cIResourceNode *node)
{
    cIResourceNode *old = *slot;
    if (node == old) return;
    if (node) node->AddRef();
    *slot = node;
    if (old) old->Release();
}

// ---- lane 261111u ----

// A game object's slots (cTSObject's model face): 120 the model, 124 its model name.
enum : int { kObjectModelSlot = 120, kObjectModelNameSlot = 124 };
// The model's: 291 the element count, 293 element i, 61 the owner whose slot 76 is the group word.
enum : int { kModelCountSlot = 291, kModelElementSlot = 293, kModelOwnerSlot = 61, kOwnerGroupSlot = 76 };
// An element's: 1 AddRef, 2 Release, 3 its material holder, 6 its subset name (a std::string out); the holder's 13 the
// material's name.
enum : int { kElementAddRef = 1, kElementRelease = 2, kElementHolderSlot = 3, kElementSubsetSlot = 6,
             kHolderMaterialSlot = 13 };
// The "DesignModes" data list's: 11 empties it, 6 sets an item (name, value).
enum : int { kListClearSlot = 11, kListSetSlot = 6 };
// The scene graph system's slot 23: a resource node by name.
enum : int { kResourceByNameSlot = 23 };
constexpr int32_t kFallbackLod = 0x5a; // SetFromObject's level of detail
const char kDesignModesList[] = "DesignModes";

// The data list's AddRef / Release through its virtual base (vbptr at +4, entry 1).
void *ListBase(void *list)
{
    auto *at = static_cast<uint8_t *>(list) + 4;
    return at + (*reinterpret_cast<int32_t *const *>(at))[1];
}

// The slave material name 0x3ac300 builds (four cRZStrings, raw: the original's constructor fills them).
struct MaterialNameParts {
    alignas(cRZString) unsigned char mBytes[4 * sizeof(cRZString)];
    explicit MaterialNameParts(const char *material)
    {
        OriginalFunction<void(__thiscall *)(void *, const char *), 0x3ac300>()(mBytes, material);
    }
    MaterialNameParts(const MaterialNameParts &) = delete;
    MaterialNameParts &operator=(const MaterialNameParts &) = delete;
    ~MaterialNameParts()
    {
        for (int i = 3; i >= 0; --i) reinterpret_cast<cRZString *>(mBytes + i * sizeof(cRZString))->Destruct();
    }
};

} // namespace

namespace nTSSG {

// ======================================== cDesignModes ========================================

void __fastcall cDesignModes::Destruct(cDesignModes *self, void *) { self->~cDesignModes(); }

void cDesignModes::Clear()
{
    mTag.mString.clear();
    mName.mString.clear();
    mModes.clear();
}

bool cDesignModes::Load(cIResourceNode *node)
{
    Clear();
    if (!node) return false;
    SetName(node->GetTag(), false);
    void *list = nGZSceneGraph::FindDataList(node, "DesignModes", nullptr);
    if (list) {
        const uint32_t count = RawSlot<26>(list);
        for (uint32_t i = 0; i < count; ++i) {
            const char *key = RawSlot<22, const char *>(list, i);
            cMapString value;
            RawSlot<18>(list, key, Face(value));
            AddDesignMode(key, value.mString.c_str());
        }
    }
    return true;
}

void cDesignModes::AddDesignMode(const char *key, const char *value)
{
    if (!key || !value) return;
    cMapString k;
    k.mString.assign(key);
    mModes[k].mString.assign(value);
}

void cDesignModes::SetName(const char *text, bool stripLod)
{
    Face(mName)->FromChar(text);
    if (stripLod) StripLod(Face(mName));
}

void cDesignModes::RewriteForLod()
{
    if (mModes.empty()) return;
    cMapString lod;
    FindLod(Face(mName), Face(lod));
    cMapString first;
    FindLod(Face(const_cast<cMapString &>(mModes.begin()->first)), Face(first));
    if (AsRZ(lod).IsEqualForward(FaceRef(first), false)) return;

    const cDesignModeMap modes(mModes);
    mModes.clear();
    for (const auto &entry : modes) {
        cMapString key(entry.first);
        StripLod(Face(key));
        if (lod.mString.size() != 0) {
            if (reinterpret_cast<cRZStringHelpers *>(&key)->EndsWith(kLod90, 5, 0))
                Face(key)->Insert(Face(key)->Strlen() - 5, FaceRef(lod));
            else
                key.mString.append(lod.mString);
        }
        cMapString value(entry.second);
        StripLod(Face(value));
        const int32_t at = Face(value)->Find(kSeparator, 0, true);
        if (at != -1) {
            Face(value)->Insert(static_cast<uint32_t>(at), FaceRef(lod));
            const int32_t next =
                Face(value)->Find(kSeparator, static_cast<uint32_t>(at) + Face(lod)->Strlen() + 1, true);
            if (next != -1) {
                // ORIGINAL-BUG: with "lod90" in the value and its second '_' fewer than five characters in, the position
                // is before the start and the string's Insert throws out_of_range.
                const int32_t lod90 = Face(value)->Find(kLod90, 0, false);
                Face(value)->Insert(static_cast<uint32_t>(next - (lod90 != -1 ? 5 : 0)), FaceRef(lod));
            }
        }
        mModes[key].mString = value.mString;
    }
}

bool cDesignModes::LoadFromObject(cIGZUnknown *object)
{
    Clear();
    if (!object) return false;
    void *const model = RawSlot<kObjectModelSlot, void *>(object);
    if (!model) return false;
    Face(mTag)->FromChar(nGZSceneGraph::GetNodeTag(reinterpret_cast<cICompositionTreeNode *>(object)));
    SetName(RawSlot<kObjectModelNameSlot, const char *>(object), false);
    cMapString lod;
    FindLod(Face(mName), Face(lod));
    reinterpret_cast<cRZStringHelpers *>(&lod)->LowerCase();
    StripLod(Face(mName));
    const bool noLod = lod.mString.empty();

    const uint32_t count = RawSlot<kModelCountSlot>(model);
    for (uint32_t i = 0; i < count; ++i) {
        void *const element = RawSlot<kModelElementSlot, void *>(model, i);
        if (element) RawSlot<kElementAddRef>(element);
        if (element && RawSlot<kElementHolderSlot, void *>(element)) {
            cMapString subset;
            {
                as2::RawString name;
                const auto *const got = RawSlot<kElementSubsetSlot, const as2::RawString *>(element, &name);
                subset.mString.assign(got->mCapacity > 15 ? got->mPtr : got->mBuf);
                if (name.mCapacity > 15) appdebugger::FreeStringBuffer(&name, name.mPtr, name.mCapacity);
            }
            cMapString material;
            material.mString.assign(
                RawSlot<kHolderMaterialSlot, const char *>(RawSlot<kElementHolderSlot, void *>(element)));
            mModes[subset].mString.assign(material.mString.data(), material.mString.size());
            if (!noLod) subset.mString.append(lod.mString);

            auto *const graph = reinterpret_cast<nGZSceneGraph::cIObjectGraphNode *>(object);
            nGZSceneGraph::cISkinExtension *extension = nGZSceneGraph::NextSkinExtension(graph, nullptr);
            while (extension) {
                DesignMode::SubsetList slaves;
                DesignMode::ListedSlaveSubsets(reinterpret_cast<cIGZUnknown *>(extension), subset.mString.c_str(),
                                               &slaves);
                for (const cRZString &slave : slaves) {
                    cMapString copy;
                    copy.mString = slave.mString;
                    if (!noLod) StripLod(Face(copy));
                    MaterialNameParts parts(material.mString.c_str());
                    OriginalFunction<bool(__thiscall *)(void *, int32_t, const char *), 0x3ac840>()(
                        parts.mBytes, 1, copy.mString.c_str());
                    BuiltString joined;
                    OriginalFunction<cRZString *(__thiscall *)(void *, cRZString *), 0x3ac3e0>()(parts.mBytes,
                                                                                                joined.Raw());
                    mModes[copy].mString.assign(joined.Raw()->mString.data(), joined.Raw()->mString.size());
                }
                extension = nGZSceneGraph::NextSkinExtension(graph, extension);
            }
        }
        if (element) RawSlot<kElementRelease>(element);
    }
    return true;
}

// ======================================== cResourceNodeRef / cDesignModeNode ========================================

cResourceNodeRef::~cResourceNodeRef()
{
    if (mNode) mNode->Release();
}

void cDesignModeNode::Reset()
{
    if (cIResourceNode *node = mNode.mNode) {
        mNode.mNode = nullptr;
        node->Release();
    }
    mModes.Clear();
}

bool cDesignModeNode::Set(cIGZUnknown *object)
{
    Reset();
    cIResourceNode *node = nullptr;
    bool result = false;
    if (object && object->QueryInterface(kResourceNodeIid, reinterpret_cast<void **>(&node))) {
        Assign(&mNode.mNode, node);
        mModes.Load(mNode.mNode);
        result = true;
    }
    if (node) node->Release();
    return result;
}

cMapString *cDesignModeNode::BaseName(cMapString *out)
{
    const int32_t at = Face(mModes.mName)->RFind("_cres", kNpos, true);
    cRZString *raw = reinterpret_cast<cRZString *>(out);
    if (at == -1)
        raw->Construct(AsRZ(mModes.mName), 0, kNpos);
    else
        Sub(AsRZ(mModes.mName)).Left(raw, static_cast<uint32_t>(at));
    return out;
}

void cDesignModeNode::ApplySeason(void *seasons)
{
    if (!mNode.mNode) return;
    if (!seasons) {
        seasons = SeasonManager();
        if (!seasons) return;
    }
    BuiltString base;
    BaseName(base.Map());
    cMapString variant;
    RawSlot<7>(seasons, base.Raw(), &variant, 0u, 1u); // receiver: nTSSG::cISeasonManager (0x336ad0, ret 16)
    if (AsRZ(variant).IsEqualForward(*reinterpret_cast<const cIGZString *>(base.Raw()), false)) return;

    // the scene graph system again (service 0x9a353ce8): slot 23 0x24a650, ret 24
    void *resources = OriginalFunction<void *(__cdecl *)(), 0x20defb>()(); // receiver: nTSSG::cITSSGSystem
    cIResourceNode *found = nullptr;
    RawSlot<23>(resources, variant.mString.c_str(), 0u, 0u, kResourceNodeIid, reinterpret_cast<void **>(&found), 0u);
    if (found) {
        if (cICompositionTreeNode *parent = mNode.mNode->GetParent()) {
            parent->RemoveChild(mNode.mNode);
            parent->AddChild(found);
        }
        Assign(&mNode.mNode, found);
        mModes.SetName(mNode.mNode->GetTag(), false);
        if (mModes.mModes.size() != 0) {
            mModes.RewriteForLod();
            ApplyDesignModesToTree();
        }
        found->Release();
    }
}

void cDesignModeNode::ApplyDesignModesToTree()
{
    if (!mNode.mNode || mModes.mModes.size() == 0) return;
    MakeShapesUnique(mNode.mNode);
    for (const auto &entry : mModes.mModes) BindDesignModeInTree(mNode.mNode, &entry.first, &entry.second);
}

bool cDesignModeNode::LoadByName(const char *name, const void *group)
{
    Reset();
    cMapString model;
    reinterpret_cast<cRZString *>(&model)->FromChar(name);
    StripLod(Face(model));
    void *const resources = OriginalFunction<void *(__cdecl *)(), 0x20defb>()(); // receiver: nTSSG::cITSSGSystem
    if (resources && !model.mString.empty()) {
        if (cIResourceNode *old = mNode.mNode) {
            mNode.mNode = nullptr;
            old->Release();
        }
        RawSlot<kResourceByNameSlot>(resources, model.mString.c_str(), group, 0u, kResourceNodeIid,
                                     reinterpret_cast<void **>(&mNode.mNode), 0u);
    }
    if (mNode.mNode) mModes.SetName(model.mString.c_str(), false);
    return mNode.mNode != nullptr;
}

bool cDesignModeNode::LoadForObject(cIGZUnknown *object, int32_t lod, const void *group)
{
    BuiltString name;
    DesignMode::FormatMaterialName(name.Raw(), object, lod, true);
    if (!LoadByName(name.Raw()->mString.c_str(), group))
        LoadByName(RawSlot<kObjectModelNameSlot, const char *>(object), group);
    return mNode.mNode != nullptr;
}

bool cDesignModeNode::Take(cDesignModes *mapped)
{
    if (!LoadByName(mapped->mName.ToChar(), nullptr)) return false;
    if (&mModes.mTag != &mapped->mTag) mModes.mTag.mString.assign(mapped->mTag.mString.data(), mapped->mTag.mString.size());
    if (&mModes.mName != &mapped->mName)
        mModes.mName.mString.assign(mapped->mName.mString.data(), mapped->mName.mString.size());
    if (&mModes.mModes != &mapped->mModes) mModes.mModes = mapped->mModes;
    if (mapped->mModes.size() != 0) {
        void *const list = nGZSceneGraph::GetOrAddDataList(mNode.mNode, kDesignModesList, true);
        if (list) RawSlot<1>(ListBase(list));
        // ORIGINAL-BUG: the list is emptied and filled without a null check (only the hold and release have one).
        RawSlot<kListClearSlot>(list);
        for (const auto &entry : mapped->mModes) {
            const char *const value = entry.second.ToChar();
            const char *const key = entry.first.ToChar();
            RawSlot<kListSetSlot>(list, key, value);
        }
        ApplyDesignModesToTree();
        if (list) RawSlot<2>(ListBase(list));
    }
    return true;
}

bool cDesignModeNode::SetFromObject(cIGZUnknown *object)
{
    Reset();
    if (!object || !OriginalFunction<bool(__cdecl *)(cIGZUnknown *, int32_t), 0x3a97f0>()(object, kFallbackLod))
        return false;
    cDesignModes local;
    if (!local.LoadFromObject(object)) return false;
    uint32_t groupWord;
    const void *group = nullptr;
    if (void *const model = RawSlot<kObjectModelSlot, void *>(object)) {
        groupWord = RawSlot<kOwnerGroupSlot>(RawSlot<kModelOwnerSlot, void *>(model));
        group = &groupWord;
    }
    if (!LoadForObject(object, kFallbackLod, group)) return false;
    if (local.mModes.size() == 0) return true;

    void *const list = nGZSceneGraph::GetOrAddDataList(mNode.mNode, kDesignModesList, true);
    if (list) RawSlot<1>(ListBase(list));
    // ORIGINAL-BUG: as in Take, a null list is still emptied and filled.
    RawSlot<kListClearSlot>(list);
    {
        DesignMode::SubsetNameSet names;
        DesignMode::CollectSubsetNames(mNode.mNode, &names);
        for (const auto &entry : local.mModes) {
            const cMapString key(entry.first);
            const cMapString value(entry.second);
            if (names.find(reinterpret_cast<const cRZString &>(key)) == names.end()) continue;
            mModes.AddDesignMode(key.mString.c_str(), value.mString.c_str());
            RawSlot<kListSetSlot>(list, key.mString.c_str(), value.mString.c_str());
        }
        ApplyDesignModesToTree();
    }
    if (list) RawSlot<2>(ListBase(list));
    return true;
}

// ======================================== free functions ========================================

void __cdecl MakeShapesUnique(cICompositionTreeNode *node)
{
    if (!node) return;
    cIShapeRefNode *ref = nullptr;
    if (node->QueryInterface(kShapeRefNodeIid, reinterpret_cast<void **>(&ref))) MakeMaterialsUnique(ref);
    const nGZSceneGraph::cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling())
        MakeShapesUnique(child);
    if (ref) ref->Release();
}

void __cdecl BindDesignModeInTree(cICompositionTreeNode *node, const cMapString *key, const cMapString *value)
{
    if (!node) return;
    cIShapeRefNode *ref = nullptr;
    if (node->QueryInterface(kShapeRefNodeIid, reinterpret_cast<void **>(&ref))) {
        const nGZSceneGraph::cShapeRange shapes = ref->GetShapes();
        for (cIShape **shape = shapes.mFirst; shape != shapes.mEnd; ++shape) {
            const char *subset = key->ToChar();
            const char *material = value->ToChar();
            (*shape)->BindMaterial(material, subset);
        }
    }
    const nGZSceneGraph::cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling())
        BindDesignModeInTree(child, key, value);
    if (ref) ref->Release();
}

bool __cdecl FindLod(cIGZString *text, cIGZString *out)
{
    const int32_t at = text->Find(kLodTag, 0, false);
    if (at < 0) return false;
    const uint32_t length = text->Strlen();
    const char *data = text->Data();
    cMapString copy;
    copy.mString.assign(data, length);
    BuiltString tag;
    Sub(AsRZ(copy)).Mid(tag.Raw(), static_cast<uint32_t>(at), kLodTagLength);
    *out = *reinterpret_cast<const cIGZString *>(tag.Raw());
    return true;
}

int32_t __cdecl StripLod(cIGZString *text)
{
    int32_t count = 0;
    for (int32_t at = text->Find(kLodTag, 0, false); at != -1 && at >= 0; at = text->Find(kLodTag, 0, false)) {
        ++count;
        text->Erase(static_cast<uint32_t>(at), kLodTagLength);
    }
    return count;
}

void __cdecl MakeMaterialsUnique(cIShapeRefNode *node)
{
    // The original keeps these in a function-static vector (emptied on the way out); a local one holds the same.
    std::vector<cIShape *> shared;
    const nGZSceneGraph::cShapeRange shapes = node->GetShapes();
    for (cIShape **shape = shapes.mFirst; shape != shapes.mEnd; ++shape) {
        if (static_cast<int32_t>((*shape)->GetReferrerCount()) > 1) {
            cIShape *held = *shape;
            if (held) held->AddRef();
            shared.push_back(held);
            if (held) held->AddRef();
            if (held) held->Release();
        }
    }
    for (cIShape *shape : shared) node->RemoveShape(shape);
    for (cIShape *shape : shared) {
        cIShape *copy = nullptr;
        shape->Clone(1, kShapeIid, reinterpret_cast<void **>(&copy), nullptr);
        node->AddShape(copy);
        if (copy) copy->Release();
    }
    for (cIShape *shape : shared)
        if (shape) shape->Release();
}

} // namespace nTSSG
