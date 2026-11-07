// nGZSceneGraph::cTransformNode and the transform family's shared pieces (lane OB). See the header.
#include "sg/SGEventRecords.h"
#include "sg/cTransformNode.h"

#include "game/game.h"
#include "game/original.h"
#include "sg/cCompositionTreeIterators.h"
#include "sg/cNullArrayAttributeEntryIterator.h"

#include <new>
#include <optional>

namespace nGZSceneGraph {

static_assert(sizeof(cTransformNode) == cTransformNode::kSize, "cTransformNode size");

namespace {
std::optional<nGZEvent::cEventAttributeMap> gTransformRegistry;
}

// Built on first use. The original runs its holder 0x2559c0 first (on every call, a once byte skipping it after the first):
// 0x257110, the node registry's events (0x257030) and then the four transform events through this function, whose map is
// made by the first of those as a copy of the node registry (0x25efe0). (The holder's 10-byte object, kept in the static
// 0x10cca38, is read by nothing.)
nGZEvent::cEventAttributeMap *__cdecl cTransformNodeBehaviorTraits::RegistryFor(uint32_t id,
                                                                              const nGZEvent::cEventAttribute *attribute)
{
    if (!gTransformRegistry) {
        cCompositionTreeNodeBehaviorTraits::RegisterEvents();
        const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
        gTransformRegistry.emplace(*cCompositionTreeNodeBehaviorTraits::RegistryFor(0, &defaults));
        nGZEvent::cEventAttributeMap &map = *gTransformRegistry;
        map[0xc9de8b96] = sgevents::kBoneID;
        map[0x9b59fb00] = sgevents::kBoneTransform;
        map[0xfb5b3383] = sgevents::kBoneTranslation;
        map[0x9b5b3506] = sgevents::kBoneOrientation;
    }
    if (id != 0) (*gTransformRegistry)[id] = *attribute;
    return &*gTransformRegistry;
}

void cTransformNodeBehaviorTraits::RegisterEvents()
{
    cCompositionTreeNodeBehaviorTraits::RegisterEvents();
    RegistryFor(0xc9de8b96, &sgevents::kBoneID);
    RegistryFor(0x9b59fb00, &sgevents::kBoneTransform);
    RegistryFor(0xfb5b3383, &sgevents::kBoneTranslation);
    RegistryFor(0x9b5b3506, &sgevents::kBoneOrientation);
}

void *__fastcall cTransformNodeBehaviorTraits::RegisterEventsAdapter(void *self, void *)
{
    RegisterEvents();
    return self;
}

void cTransformNodeBehaviorTraits::ResetRegistryForTest() { gTransformRegistry.reset(); }

nGZEvent::cEventAttributeMap *TransformNodeEventRegistry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return cTransformNodeBehaviorTraits::RegistryFor(0, &defaults);
}

namespace node_family {
void *BuildIterator43(void *impl, void *out, cICompositionTreeNode *node, uint32_t a, uint32_t b)
{
    return MakeConstTreeIterators(impl, nullptr, static_cast<cCompositionTreeNodeRange *>(out), node, a,
                                  reinterpret_cast<cIBranchNodesStrategy *>(uintptr_t(b)));
}

void *BuildIterator44(void *impl, void *out, cICompositionTreeNode *node, uint32_t a, uint32_t b)
{
    return MakeTreeIterators(impl, nullptr, static_cast<cCompositionTreeNodeRange *>(out), node, a,
                             reinterpret_cast<cIBranchNodesStrategy *>(uintptr_t(b)));
}

void *BuildIterator51(void *impl, void *out, uint32_t a)
{
    return MakeEmptyAttributeEntryRange(impl, nullptr, static_cast<cArrayAttributeEntryRange *>(out), a);
}
} // namespace node_family

// (The original's operator new throws on failure, so its null branch is dead.)
cTransformNode *__cdecl cTransformNode::Create() { return new cTransformNode; }

void *cTransformNode::MakeIterator43(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator43(&mImpl, out, this, a, b);
}
void *cTransformNode::MakeIterator44(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator44(&mImpl, out, this, a, b);
}
void *cTransformNode::MakeIterator51(void *out, uint32_t a) { return node_family::BuildIterator51(&mImpl, out, a); }

void cTransformNode::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context)
{
    cICompositionTreeNode *clone = nullptr;
    if (context) {
        context->FindClone(this, cITransformNode::kIid, &clone);
    } else {
        clone = new cTransformNode;
        clone->AddRef();
    }
    if (mode != 2) {
        // This node's Impl copies into the clone's (the clone's Impl is the 0xc bytes after its start).
        auto *const cloneImpl = reinterpret_cast<cTransformNodeImpl *>(reinterpret_cast<uint8_t *>(clone) + 0xc);
        mImpl.CloneInto(this, clone, cloneImpl, mode, context);
    }
    clone->QueryInterface(iid, out);
    if (clone) clone->Release();
}

} // namespace nGZSceneGraph
