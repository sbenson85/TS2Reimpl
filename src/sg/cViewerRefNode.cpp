// nGZSceneGraph::cViewerRefNode (lane TZ, scenegraph N3r). See the header.
#include "sg/cViewerRefNode.h"

#include "sg/SGBehaviorCopies.h" // the cell the game's copies share
#include "sg/SGEventRecords.h"
#include "sg/cTransformNode.h" // node_family, the transform registry
#include "sg/cViewerRefAttributeNameIterator.h"

#include <new>
#include <optional>

namespace nGZSceneGraph {

static_assert(sizeof(cViewerRefNode) == cViewerRefNode::kSize, "cViewerRefNode size");

// ---- slot 51's names (the Impl's, 0xa66440) ----

// "fov" or "nearFarClip" in the group "viewersGroup", one name per entry; any other id answers the empty range.
void *cViewerRefNodeImpl::MakeIterator51(void *outRange, uint32_t id)
{
    auto *const out = static_cast<cArrayAttributeEntryRange *>(outRange);
    const char *name;
    switch (id) {
    case kFovAttribute: name = "fov"; break;
    case kNearFarAttribute: name = "nearFarClip"; break;
    default: return MakeEmptyAttributeEntryRange(this, nullptr, out, id);
    }
    return MakeViewerAttributeNameRange(out, GetEntryCount(), "viewersGroup", name);
}

// ---- the behaviour's cells ----

nGZEvent::cIConnectionManager *&cViewerRefNodeBehaviorTraits::EventManagerCell() { return ts2_rva_10f63e0; }

namespace {

std::optional<nGZEvent::cEventAttributeMap> gViewerRefRegistry;

// The viewer-reference node's own events (the game's data 0x106c0e8 and 0x106c100).
const nGZEvent::cEventAttribute kViewerFovRecord{cViewerRefNodeImpl::kFovAttribute, "ViewerFOV", 0, 0xbb22dd17, 3, 0xc};
const nGZEvent::cEventAttribute kViewerNearFarRecord{cViewerRefNodeImpl::kNearFarAttribute, "ViewerNearFarClip", 0,
                                                     0x9b5b3300, 3, 0xc};

} // namespace

// Built on first use. The original runs its holder 0xa63750 first (on every call, a once byte 0x10f63e4 skipping it after
// the first): the transform family's events (0x257110: the node registry's, 0x257030, then the four bone events through
// the transform registry), then the two viewer events through this function, whose map (0x10f63d0) is made by the first
// of them as a copy of the transform family's map (0xa57e10). (The holder's 0xc-byte object, kept in the static
// 0x10f63e8, is read by nothing.)
nGZEvent::cEventAttributeMap *__cdecl cViewerRefNodeBehaviorTraits::RegistryFor(uint32_t id,
                                                                              const nGZEvent::cEventAttribute *attribute)
{
    if (!gViewerRefRegistry) {
        cCompositionTreeNodeBehaviorTraits::RegisterEvents();
        cTransformNodeBehaviorTraits::RegistryFor(0xc9de8b96, &sgevents::kBoneID);
        cTransformNodeBehaviorTraits::RegistryFor(0x9b59fb00, &sgevents::kBoneTransform);
        cTransformNodeBehaviorTraits::RegistryFor(0xfb5b3383, &sgevents::kBoneTranslation);
        cTransformNodeBehaviorTraits::RegistryFor(0x9b5b3506, &sgevents::kBoneOrientation);
        gViewerRefRegistry.emplace(*TransformNodeEventRegistry());
        nGZEvent::cEventAttributeMap &map = *gViewerRefRegistry;
        map[kViewerFovRecord.mId] = kViewerFovRecord;
        map[kViewerNearFarRecord.mId] = kViewerNearFarRecord;
    }
    if (id != 0) (*gViewerRefRegistry)[id] = *attribute;
    return &*gViewerRefRegistry;
}

nGZEvent::cEventAttributeMap *cViewerRefNodeBehaviorTraits::Registry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return RegistryFor(0, &defaults);
}

void cViewerRefNodeBehaviorTraits::ResetRegistryForTest() { gViewerRefRegistry.reset(); }

// ---- cViewerRefNode ----

// (The original's operator new throws on failure, so its null branch is dead.)
cViewerRefNode *__cdecl cViewerRefNode::Create() { return new cViewerRefNode; }

cViewerRefNode *cViewerRefNode::Construct() { return ::new (this) cViewerRefNode; }
void cViewerRefNode::Destruct() { this->cViewerRefNode::~cViewerRefNode(); }
cViewerRefNode *cViewerRefNode::DeletingDestructor(uint32_t flags)
{
    this->cViewerRefNode::~cViewerRefNode();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

nGZEvent::cEventAttribute cViewerRefNode::GetEventAttribute(uint32_t event)
{
    const auto found = cViewerRefNodeBehaviorTraits::Registry()->find(event);
    if (found == cViewerRefNodeBehaviorTraits::Registry()->end()) return nGZEvent::behavior_mt::gMissingRecord;
    nGZEvent::cEventAttribute record = found->second;
    if (event == cViewerRefNodeImpl::kFovAttribute || event == cViewerRefNodeImpl::kNearFarAttribute)
        record.mWord08 = mImpl.GetEntryCount();
    return record;
}

bool cViewerRefNode::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == kIid || iid == kBaseIid) {
        *out = static_cast<cIViewerRefNode *>(this);
        AddRef();
        return true;
    }
    return Base::QueryInterface(iid, out);
}

void cViewerRefNode::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context)
{
    cICompositionTreeNode *clone = nullptr;
    if (context) {
        context->FindClone(this, kIid, &clone);
    } else {
        clone = new cViewerRefNode;
        clone->AddRef();
    }
    if (mode != 2) {
        auto *const cloneImpl = &static_cast<cViewerRefNode *>(clone)->mImpl;
        mImpl.CloneInto(this, clone, cloneImpl, mode, context);
    }
    clone->QueryInterface(iid, out);
    if (clone) clone->Release();
}

} // namespace nGZSceneGraph
