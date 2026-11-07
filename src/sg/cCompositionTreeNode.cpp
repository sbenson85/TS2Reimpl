// nGZSceneGraph::cCompositionTreeNode and the node family's shared pieces (lane NZ). See the headers.
#include "sg/SGEventRecords.h"
#include "sg/cCompositionTreeNode.h"

#include "game/game.h"
#include "game/original.h"
#include "sg/cCompositionTreeIterators.h"
#include "sg/cNodeCloneContext.h"
#include "sg/cNullArrayAttributeEntryIterator.h"
#include "sg/cObjectGraphNodeImpl.h"

#include <new>
#include <optional>

namespace nGZSceneGraph {

static_assert(sizeof(cCompositionTreeNode) == cCompositionTreeNode::kSize, "cCompositionTreeNode size");

namespace {
std::optional<nGZEvent::cEventAttributeMap> gNodeRegistry;
}

// Built on first use. The original runs its holder 0x255860 first (on every call, a once byte skipping it after the first):
// 0x257030, the object-graph events (0x2570a0) and then 0x0a884a15 and 0xcc6c9992 through this function, whose map is made
// by the first of those as a copy of the object-graph registry (0x25f030). (The holder's 6-byte object, kept in the static
// 0x10cca2c, is read by nothing.)
nGZEvent::cEventAttributeMap *__cdecl cCompositionTreeNodeBehaviorTraits::RegistryFor(uint32_t id,
                                                                                    const nGZEvent::cEventAttribute *attribute)
{
    if (!gNodeRegistry) {
        cObjectGraphBehaviorTraits::RegisterEvents();
        const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
        gNodeRegistry.emplace(*cObjectGraphBehaviorTraits::RegistryFor(0, &defaults));
        (*gNodeRegistry)[0x0a884a15] = sgevents::kPartitioningUpdateReqStateChanged;
        (*gNodeRegistry)[0xcc6c9992] = sgevents::kIncludeInPartitionStateChanged;
    }
    if (id != 0) (*gNodeRegistry)[id] = *attribute;
    return &*gNodeRegistry;
}

void cCompositionTreeNodeBehaviorTraits::RegisterEvents()
{
    cObjectGraphBehaviorTraits::RegisterEvents();
    RegistryFor(0x0a884a15, &sgevents::kPartitioningUpdateReqStateChanged);
    RegistryFor(0xcc6c9992, &sgevents::kIncludeInPartitionStateChanged);
}

void *__fastcall cCompositionTreeNodeBehaviorTraits::RegisterEventsAdapter(void *self, void *)
{
    RegisterEvents();
    return self;
}

void cCompositionTreeNodeBehaviorTraits::ResetRegistryForTest() { gNodeRegistry.reset(); }

nGZEvent::cEventAttributeMap *NodeEventRegistry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return cCompositionTreeNodeBehaviorTraits::RegistryFor(0, &defaults);
}

// (The original's operator new throws on failure, so its null branch is dead.)
cCompositionTreeNode *__cdecl cCompositionTreeNode::Create() { return new cCompositionTreeNode; }

void *cCompositionTreeNode::MakeIterator43(void *out, uint32_t a, uint32_t b)
{
    return MakeConstTreeIterators(&mImpl, nullptr, static_cast<cCompositionTreeNodeRange *>(out), this, a,
                                  reinterpret_cast<cIBranchNodesStrategy *>(uintptr_t(b)));
}

void *cCompositionTreeNode::MakeIterator44(void *out, uint32_t a, uint32_t b)
{
    return MakeTreeIterators(&mImpl, nullptr, static_cast<cCompositionTreeNodeRange *>(out), this, a,
                             reinterpret_cast<cIBranchNodesStrategy *>(uintptr_t(b)));
}

void *cCompositionTreeNode::MakeIterator51(void *out, uint32_t a)
{
    return MakeEmptyAttributeEntryRange(&mImpl, nullptr, static_cast<cArrayAttributeEntryRange *>(out), a);
}

void cCompositionTreeNode::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context)
{
    cICompositionTreeNode *clone = nullptr;
    if (context) {
        context->FindClone(this, cICompositionTreeNode::kIid, &clone);
    } else {
        clone = new cCompositionTreeNode;
        clone->AddRef();
    }
    if (mode != 2) {
        // The clone is a composition node like this one: its Impl sits at +0xc.
        auto *const cloneImpl = reinterpret_cast<cCompositionTreeNodeImpl *>(reinterpret_cast<uint8_t *>(clone) + 0xc);
        CloneCompositionInto(&mImpl, nullptr, this, clone, cloneImpl, mode, context);
    }
    clone->QueryInterface(iid, out);
    if (clone) clone->Release();
}

} // namespace nGZSceneGraph
