// nGZSceneGraph's skin and CAS model helpers (lane 261111h, scenegraph SU7). See the header's section; the branch filters'
// constructors 0xaea7a0 / 0xaea7c0 are here too (nRZSceneGraph's, src/misc/SmallVirtuals.h).
#include "sg/SGUtilities.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "misc/SmallVirtuals.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/StringKeyParser.h"
#include "sg/SGLocalString.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/cDataListExtension.h"
#include "sg/cFindBonesTraversal.h"
#include "sg/cSceneGraphSystem.h" // cISceneNodeIterator
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"
#include "sg/cSkinExtension.h"

#include <cstring>
#include <string.h> // _stricmp

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kShapeRefNodeClass = 0x65245517;
constexpr uint32_t kCloneDeep = 1; // the shape clone's mode

// The resource manager the service hands out (0x56b906e, 0x656b8efc): slot 3 loads a resource by key.
class cIResourceLoader : public cIGZUnknown {
public:
    virtual bool Load(const void *key, uint32_t iid, void **out, uint32_t a, uint32_t b) = 0; // 3
};
using ServiceQueryFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);

template <class T> void Create(uint32_t classId, uint32_t iid, T **out)
{
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(classId, iid, reinterpret_cast<void **>(out));
}

cIExtension *AsExtension(cISkinExtension *skin) { return skin ? static_cast<cIExtension *>(skin) : nullptr; }
cIExtension *AsExtension(cIDataListExtension *list) { return list ? static_cast<cIExtension *>(list) : nullptr; }

// The pair members as the skin keeps them (raw cIGZUnknown words).
cIShapeRefNode *AsShapeRef(cIGZUnknown *p) { return reinterpret_cast<cIShapeRefNode *>(p); }
cIShape *AsShape(cIGZUnknown *p) { return reinterpret_cast<cIShape *>(p); }
cIGZUnknown *AsUnknown(void *p) { return reinterpret_cast<cIGZUnknown *>(p); }
cITransformNode *AsTransform(cICompositionTreeNode *p) { return reinterpret_cast<cITransformNode *>(p); }

uint32_t WordOf(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The three counted iterators slot 44 fills (the cursor, the begin, the end), released end first.
struct NodeRange {
    cISceneNodeIterator *mCursor = nullptr;
    cISceneNodeIterator *mBegin = nullptr;
    cISceneNodeIterator *mEnd = nullptr;
    NodeRange() = default;
    NodeRange(const NodeRange &) = delete;
    NodeRange &operator=(const NodeRange &) = delete;
    ~NodeRange()
    {
        if (mEnd) mEnd->Release();
        if (mBegin) mBegin->Release();
        if (mCursor) mCursor->Release();
    }
    bool AtEnd() { return mCursor->IsEqual(mEnd); } // receiver: cISceneNodeIterator (slot 4)
    cICompositionTreeNode *Get() { return reinterpret_cast<cICompositionTreeNode *>(mCursor->Get()); } // slot 5
    void Next() { mCursor->Next(); } // slot 6
};

// The template FindSkinExtension and FindSkinExtensionByName share (FindDataList's shape).
template <class Match> cISkinExtension *FindSkin(cIObjectGraphNode *node, cISkinExtension *after, Match match)
{
    cISkinExtension *current = after;
    for (;;) {
        cIExtension *start = nullptr;
        if (current) {
            start = static_cast<cIExtension *>(current)->GetNext();
            if (!start) return nullptr;
        }
        cISkinExtension *found = nullptr;
        if (!node->FindExtension(cISkinExtension::kIid, reinterpret_cast<void **>(&found), start)) {
            if (found) static_cast<cIExtension *>(found)->Release();
            return nullptr;
        }
        const bool hit = match(found);
        if (found) static_cast<cIExtension *>(found)->Release();
        if (hit) return found;
        current = found;
    }
}

// A counted cell released when it goes (null allowed).
template <class T> struct Held {
    T *p = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() { if (p) AsUnknown(p)->Release(); }
    void **Out() { return reinterpret_cast<void **>(&p); }
};

} // namespace

cISkinExtension *FindSkinExtension(cIObjectGraphNode *node, uint32_t word, cISkinExtension *after)
{
    return FindSkin(node, after, [word](cISkinExtension *skin) { return skin->GetWord() == word; });
}

cISkinExtension *FindSkinExtensionByName(cIObjectGraphNode *node, const char *name, cISkinExtension *after)
{
    return FindSkin(node, after, [name](cISkinExtension *skin) { return _stricmp(skin->GetName(), name) == 0; });
}

cISkinExtension *AddSkinExtension(cICompositionTreeNode *node, const char *name, uint32_t word, int32_t filter)
{
    cISkinExtension *skin = nullptr;
    Create(cSkinExtension::kClassId, cISkinExtension::kIid, &skin);
    node->AddExtension(AsExtension(skin));
    // ORIGINAL-BUG: the skin is not checked; a failed creation faults here.
    skin->SetName(name);
    skin->SetWord(word);

    cIFindBonesTraversal *bones = nullptr;
    Create(cFindBonesTraversal::kClassId, cIFindBonesTraversal::kIid, &bones);
    bones->SetFilter(filter);
    bones->SetCollectUntagged(true);
    bones->Traverse(node);
    const uint32_t count = bones->Count();
    for (uint32_t i = 0; i < count; ++i) {
        cICompositionTreeNode *const bone = bones->At(i);
        Held<cIShapeRefNode> shapes;
        if (bone->QueryInterface(cIShapeRefNode::kIid, shapes.Out())) {
            const int32_t n = static_cast<int32_t>(shapes.p->ShapeCount());
            for (int32_t j = 0; j < n; ++j)
                skin->AddPair(AsUnknown(shapes.p), AsUnknown(shapes.p->GetShapes().mFirst[j]));
        }
    }
    if (bones) bones->Release();
    if (skin) static_cast<cIExtension *>(skin)->Release();
    return skin;
}

void RebindSkin(cISkinExtension *skin, const char *material, const char *subset)
{
    const int32_t count = static_cast<int32_t>(skin->GetPairCount());
    for (int32_t i = 0; i < count; ++i) {
        cIShapeRefNode *const holder = AsShapeRef(skin->GetPairFirst(i));
        cIShape *const shape = AsShape(skin->GetPairSecond(i));
        if (shape) shape->AddRef();
        if (holder->HasShape(shape)) {
            if (static_cast<int32_t>(shape->GetReferrerCount()) > 1) {
                cIShape *clone = nullptr;
                shape->Clone(kCloneDeep, cIShape::kIid, reinterpret_cast<void **>(&clone), nullptr);
                holder->RemoveShape(shape);
                holder->AddShape(clone);
                // ORIGINAL-BUG: the clone is not checked.
                if (subset)
                    clone->BindMaterial(material, subset);
                else
                    clone->BindMaterialToAllSubsets(WordOf(material));
                skin->SetPairSecond(i, AsUnknown(clone));
                if (clone) clone->Release();
            } else if (subset) {
                shape->BindMaterial(material, subset);
            } else {
                shape->BindMaterialToAllSubsets(WordOf(material));
            }
        }
        if (shape) shape->Release();
    }
    skin->MapName(material, subset);
}

void UndoSkin(cISkinExtension *skin)
{
    for (int32_t i = static_cast<int32_t>(skin->GetPairCount()) - 1; i >= 0; --i) {
        cIShapeRefNode *const holder = AsShapeRef(skin->GetPairFirst(i));
        if (holder) holder->AddRef();
        cIShape *const shape = AsShape(skin->GetPairSecond(i));
        if (shape) shape->AddRef();
        skin->ErasePair(i);
        // ORIGINAL-BUG: a null node in a pair is not checked.
        if (holder->HasShape(shape)) holder->RemoveShape(shape);
        if (shape) shape->Release();
        if (holder) holder->Release();
    }
    for (int32_t i = static_cast<int32_t>(skin->GetRefCount()) - 1; i >= 0; --i) {
        auto *const ref = reinterpret_cast<cIExtension *>(skin->GetRef(i));
        if (ref && ref->GetOwner()) ref->GetOwner()->RemoveExtension(ref);
        skin->EraseRef(i);
    }
    cIExtension *const self = static_cast<cIExtension *>(skin); // not checked
    self->GetOwner()->RemoveExtension(self);
}

void UndoSkinsWithWord(cIObjectGraphNode *node, uint32_t word)
{
    cIExtension *next = nullptr; // held counted
    for (;;) {
        cISkinExtension *skin = nullptr;
        if (!node->FindExtension(cISkinExtension::kIid, reinterpret_cast<void **>(&skin), next)) {
            if (skin) static_cast<cIExtension *>(skin)->Release();
            break;
        }
        cIExtension *const after = static_cast<cIExtension *>(skin)->GetNext();
        if (after != next) {
            cIExtension *const old = next;
            if (after) after->AddRef();
            next = after;
            if (old) old->Release();
        }
        if (skin->GetWord() == word) UndoSkin(skin);
        const bool last = next == nullptr;
        if (skin) static_cast<cIExtension *>(skin)->Release();
        if (last) break;
    }
    if (next) next->Release();
}

void UndoAllSkins(cIObjectGraphNode *node)
{
    cISkinExtension *skin = nullptr;
    for (;;) {
        if (skin) {
            cISkinExtension *const old = skin;
            skin = nullptr;
            static_cast<cIExtension *>(old)->Release();
        }
        if (!node->FindExtension(cISkinExtension::kIid, reinterpret_cast<void **>(&skin), nullptr)) break;
        UndoSkin(skin);
    }
    if (skin) static_cast<cIExtension *>(skin)->Release();
}

namespace {

// Pass 1 of SetUpSkinning for one transform node `item` tagged `tag`: each bone of that tag gets the node's dynamics lists.
void CloneDynamics(NodeRange &range, const char *tag, SkinVector *skins, cIFindBonesTraversal *bones, uint32_t count,
                   int32_t &type)
{
    for (uint32_t i = 0; i < count; ++i) {
        cICompositionTreeNode *const bone = bones->At(i);
        if (_stricmp(GetNodeTag(bone), tag) != 0) continue;
        for (cIDataListExtension *list = FindDataList(range.Get(), "tsDynamicsBehavior.s", nullptr); list;
             list = FindDataList(range.Get(), "tsDynamicsBehavior.s", list)) {
            if (!list->GetSint32("DynamicsBehaviorSkinType", &type)) continue;
            for (cISkinExtension *const skin : *skins) {
                if (static_cast<uint32_t>(type) != skin->GetWord()) continue;
                cIExtension *const copy = static_cast<cIExtension *>(list)->Clone();
                if (copy) copy->AddRef();
                skin->PushRef(copy);
                bone->AddExtension(copy);
                float translation[3];
                if (list->GetVector3("DynamicsBehaviorRestTrans", translation))
                    AsTransform(bone)->SetTranslation(translation);
                float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                if (list->GetQuaternion("DynamicsBehaviorRestRot", rotation)) AsTransform(bone)->SetRotation(rotation);
                if (copy) copy->Release();
                break;
            }
        }
    }
}

bool HasText(const char *s) { return s && *s; }

} // namespace

bool SetUpSkinning(cICompositionTreeNode *node, cICompositionTreeNode *tree, SkinVector *skins,
                   cIFindBonesTraversal *bones, int32_t filter)
{
    if (!node || !tree || skins->empty() || !bones) return false;
    const uint32_t count = bones->Count();

    for (cIDataListExtension *adjust = FindDataList(tree, "bone_adjust", nullptr); adjust;) {
        nTSSG::SGLocalString boneTag;
        if (adjust->GetString("bone_adjust_bone_tag", boneTag.value.AsInterface())) {
            for (uint32_t i = 0; i < count; ++i) {
                cICompositionTreeNode *const bone = bones->At(i);
                // ORIGINAL-BUG: a bone without a tag is not checked (strcmp on null).
                if (std::strcmp(GetNodeTag(bone), boneTag.Text()) != 0) continue;
                float translation[3];
                if (adjust->GetVector3("bone_adjust_translation", translation))
                    AsTransform(bone)->SetTranslation(translation);
                float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                if (adjust->GetQuaternion("bone_adjust_rotation", rotation)) AsTransform(bone)->SetRotation(rotation);
                break;
            }
        }
        adjust = FindDataList(tree, "bone_adjust", adjust); // asked before the string goes
    }

    nRZSceneGraph::cBranchNodesIIDFilter byIid{&ts2_rva_e7cf54, cITransformNode::kIid};
    nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter byMask{
        GameAddress<const void>(nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::kVTable), static_cast<uint32_t>(filter),
        cITransformNode::kIid};
    const void *const chosen = filter < 0 ? static_cast<const void *>(&byIid) : static_cast<const void *>(&byMask);
    int32_t type = 0; // the original's is not set first; every read is after a GetSint32 that answered true

    {
        NodeRange range;
        tree->MakeIterator44(&range, 0, WordOf(chosen));
        while (!range.AtEnd()) {
            {
                Held<cITransformNode> transform;
                if (range.Get()->QueryInterface(cITransformNode::kIid, transform.Out())) {
                    const char *const tag = GetNodeTag(range.Get());
                    if (HasText(tag)) CloneDynamics(range, tag, skins, bones, count, type);
                }
            }
            range.Next();
        }
    }

    {
        NodeRange range;
        tree->MakeIterator44(&range, 0, WordOf(chosen));
        while (!range.AtEnd()) {
            cIDataListExtension *base = FindDataList(range.Get(), "IKTargetMorphBase", nullptr);
            if (base && base->GetSint32("IKTargetMorphSkinType", &type)) {
                for (cISkinExtension *const skin : *skins) {
                    const uint32_t word = skin->GetWord();
                    const uint32_t want = static_cast<uint32_t>(type);
                    if (!(word == want || (word == 3 && (want == 7 || want == 8)))) continue;
                    Held<cITransformNode> transform;
                    if (!range.Get()->QueryInterface(cITransformNode::kIid, transform.Out())) continue;
                    const char *const tag = GetNodeTag(range.Get());
                    if (!HasText(tag)) continue;
                    for (uint32_t i = 0; i < count; ++i) {
                        cICompositionTreeNode *const bone = bones->At(i);
                        if (_stricmp(GetNodeTag(bone), tag) != 0) continue;
                        RemoveDataLists(bone, "IKTargetMorphBase");
                        RemoveDataLists(bone, "IKTargetMorphDelta");
                        // ORIGINAL-BUG: the base list is forgotten after the first bone it is cloned onto (the original
                        // reuses its register); a second matching bone, here or for a later skin, faults on null.
                        cIExtension *copy = static_cast<cIExtension *>(base)->Clone();
                        if (copy) copy->AddRef();
                        bone->AddExtension(copy);
                        auto *const holder = reinterpret_cast<cICompositionTreeNode *>(transform.p);
                        for (cIDataListExtension *delta = FindDataList(holder, "IKTargetMorphDelta", nullptr); delta;
                             delta = FindDataList(holder, "IKTargetMorphDelta", delta)) {
                            cIExtension *const next = static_cast<cIExtension *>(delta)->Clone();
                            if (next != copy) {
                                if (next) next->AddRef();
                                if (copy) copy->Release();
                                copy = next;
                            }
                            bone->AddExtension(copy);
                        }
                        if (copy) copy->Release();
                        base = nullptr;
                    }
                }
            }
            range.Next();
        }
    }

    byMask.Destruct();
    byIid.Destruct();
    return true;
}

cISkinExtension *LoadObjectModel(cICompositionTreeNode *node, const char *name, cIResourceNode *resource, uint32_t word,
                                 int32_t filter)
{
    if (!resource) {
        nTSSG::SGLocalString text(name);
        text.value.mString.append("_cres", 5);
        StringKeyParser::Result key = {0, 0, 0, 0};
        StringKeyParser::ParseKey(&key, text.Text(), 0xe519c933, nullptr);
        cIResourceLoader *loader = nullptr;
        if (void *service = GetServiceObject())
            VirtualCall<ServiceQueryFn, 5>(service, 0x056b906e, 0x656b8efc, reinterpret_cast<void **>(&loader)); // receiver: cIGZFrameWork (GetSystemService, slot 5, ret 12)
        cIResourceNode *loaded = nullptr;
        // ORIGINAL-BUG: a service that gave no manager is dereferenced all the same.
        const bool found = loader->Load(&key, cIResourceNode::kIid, reinterpret_cast<void **>(&loaded), 0, 0);
        if (loader) loader->Release();
        if (!found) {
            if (loaded) loaded->Release();
            return nullptr;
        }
        resource = loaded;
        word = resource->Slot64();
        // The count is dropped here; the resource is used below uncounted (the manager keeps it).
        if (loaded) loaded->Release();
    }

    cISkinExtension *skin = nullptr;
    Create(cSkinExtension::kClassId, cISkinExtension::kIid, &skin);
    node->AddExtension(AsExtension(skin));
    skin->SetName(name); // not checked
    skin->SetWord(word);

    cIFindBonesTraversal *bones = nullptr;
    Create(cFindBonesTraversal::kClassId, cIFindBonesTraversal::kIid, &bones);
    bones->SetFilter(filter);
    bones->SetCollectUntagged(false);
    bones->Traverse(node);
    const uint32_t count = bones->Count();

    {
        NodeRange range;
        resource->MakeIterator44(&range, 0, 0);
        while (!range.AtEnd()) {
            {
            Held<cITransformNode> transform;
            cICompositionTreeNode *const item = range.Get();
            if (item->QueryInterface(cITransformNode::kIid, transform.Out())) {
                auto *const at = reinterpret_cast<cICompositionTreeNode *>(transform.p);
                if (const char *const tag = GetNodeTag(at)) {
                    nTSSG::SGLocalString lower(tag);
                    lower.Helpers()->LowerCase();
                    auto *const shapes = AsShapeRef(ShapeRefOfNodeOrChild(at));
                    if (shapes) shapes->AddRef();
                    if (shapes && static_cast<int32_t>(shapes->ShapeCount()) > 0 && count != 0) {
                        for (uint32_t i = 0; i < count; ++i) {
                            cICompositionTreeNode *const bone = bones->At(i);
                            const char *const boneTag = GetNodeTag(bone);
                            if (!boneTag || std::strcmp(boneTag, lower.Text()) != 0) continue;
                            auto *target = AsShapeRef(ShapeRefOfNodeOrChild(bone));
                            if (target) target->AddRef();
                            if (!target) {
                                Create(kShapeRefNodeClass, cIShapeRefNode::kIid, &target);
                                bone->AddChild(target);
                            }
                            const cShapeRange list = shapes->GetShapes();
                            for (cIShape **p = list.mFirst; p != list.mEnd; ++p) {
                                cIShape *const shape = *p;
                                target->AddShape(shape);
                                skin->AddPair(AsUnknown(target), AsUnknown(shape));
                            }
                            if (target) target->Release();
                        }
                    }
                    if (shapes) shapes->Release();
                }
            }
            }
            range.Next();
        }
    }

    {
        SkinVector list;
        list.push_back(skin);
        SetUpSkinning(node, resource, &list, bones, filter);
    }
    if (bones) bones->Release();
    if (skin) static_cast<cIExtension *>(skin)->Release();
    return skin;
}

bool LoadPersonPart(cICompositionTreeNode *node, SkinVector *sources, int32_t filter)
{
    if (!node || sources->empty()) return false;

    cIFindBonesTraversal *bones = nullptr;
    Create(cFindBonesTraversal::kClassId, cIFindBonesTraversal::kIid, &bones);
    bones->SetFilter(filter);
    bones->SetCollectUntagged(false);
    bones->Traverse(node);
    const uint32_t count = bones->Count();

    SkinVector made;
    cICompositionTreeNode *tree = nullptr; // counted
    for (cISkinExtension *const source : *sources) {
        cISkinExtension *skin = nullptr;
        Create(cSkinExtension::kClassId, cISkinExtension::kIid, &skin);
        node->AddExtension(AsExtension(skin));
        // ORIGINAL-BUG: the new skin is not checked.
        skin->SetName(source->GetName());
        skin->SetWord(source->GetWord());
        made.push_back(skin);

        const uint32_t pairs = source->GetPairCount();
        for (uint32_t i = 0; i < pairs; ++i) {
            auto *const first = reinterpret_cast<cICompositionTreeNode *>(source->GetPairFirst(i));
            if (first) first->AddRef();
            cIShape *const shape = AsShape(source->GetPairSecond(i));
            if (shape) shape->AddRef();
            const char *tag = GetNodeTag(first);
            if (!tag && first->GetParent()) tag = GetNodeTag(first->GetParent());
            if (tag) {
                nTSSG::SGLocalString lower(tag);
                lower.Helpers()->LowerCase();
                for (uint32_t b = 0; b < count; ++b) {
                    cICompositionTreeNode *const bone = bones->At(b);
                    const char *const boneTag = GetNodeTag(bone);
                    if (!boneTag || std::strcmp(boneTag, lower.Text()) != 0) continue;
                    auto *target = AsShapeRef(ShapeRefOfNodeOrChild(bone));
                    if (target) target->AddRef();
                    if (!target) {
                        Create(kShapeRefNodeClass, cIShapeRefNode::kIid, &target);
                        bone->AddChild(target);
                    }
                    target->AddShape(shape); // not checked
                    skin->AddPair(AsUnknown(target), AsUnknown(shape));
                    if (target) target->Release();
                }
            }
            if (shape) shape->Release();
            if (first) first->Release();
        }

        const uint32_t names = source->GetNameMapCount();
        for (uint32_t j = 0; j < names; ++j) {
            const char *const mapped = source->GetMappedName(j);
            skin->MapName(mapped, source->GetMappedKey(j));
        }
        if (!tree) // the owner is not checked
            static_cast<cIExtension *>(source)->GetOwner()->QueryInterface(cICompositionTreeNode::kIid,
                                                                          reinterpret_cast<void **>(&tree));
        if (skin) static_cast<cIExtension *>(skin)->Release();
    }

    const bool done = SetUpSkinning(node, tree, &made, bones, filter);
    made = SkinVector();
    if (tree) tree->Release();
    if (bones) bones->Release();
    return done;
}

} // namespace nGZSceneGraph

// ---- the branch filters' constructors ----------------------------------------------------------------------------------

namespace nRZSceneGraph {

cBranchNodesIIDFilter *IidFilterConstructor::Construct(uint32_t iid)
{
    mIID = iid;
    mVTable = &ts2_rva_e7cf54;
    return this;
}

cBranchNodesMaskBitAndIIDFilter *MaskFilterConstructor::Construct(uint32_t maskBit, uint32_t iid)
{
    mMaskBit = maskBit;
    mIID = iid;
    mVTable = GameAddress<const void>(kVTable);
    return this;
}

} // namespace nRZSceneGraph
