#pragma once
// nGZSceneGraph::cViewerRefNodeRecursive (lane VF, scenegraph N3r): the viewer reference that draws the scene again
// from a mirrored eye into its target (a reflection: water, mirrors), with materials of its own. Standalone shape:
// cViewerRefNodeBaseT<cIViewerRefNodeRecursive, cViewerRefNodeRecursiveImpl, cViewerRefNodeRecursiveBehaviorTraits>
// (sg/cViewerRefNodeBaseT.h) plus the game's cViewerRefNodeRecursiveT level (slots 123 / 124); 0x10c bytes like the
// game's (vtables 0xe72eac, 126 slots, and 0xe730a8 at +8). The Impl (sg/cViewerRefNodeRecursiveImpl.h) has a vtable of
// its own, so the composition part every template slot reaches is at node+0x10.
//
// The switch: the factory 0xa6ac70 is hooked to Create, the complete constructor 0xa69f50 to Construct and the
// destructor 0xa6a130 to Destruct. Once the constructor is ours no object carries the game's vtables, and the slot bodies
// only they reach are in effect through callers.
#include "sg/cIViewerRefNode.h"
#include "sg/cViewerRefNodeBaseT.h"
#include "sg/cViewerRefNodeRecursiveImpl.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// The behaviour cells of cAbstractBehaviorTypeT<cIViewerRefNodeRecursive, cRefSingleThreaded>: the transform family's
// registry, and a connection-manager cache of its own.
struct cViewerRefNodeRecursiveBehaviorTraits {
    static nGZEvent::cIConnectionManager *&EventManagerCell(); // the owned static 0x10f6424 (sg/SGBehaviorCopies.h)
    static nGZEvent::cEventAttributeMap *Registry();
};

class cViewerRefNodeRecursive
    : public cViewerRefNodeBaseT<cIViewerRefNodeRecursive, cViewerRefNodeRecursiveImpl,
                                 cViewerRefNodeRecursiveBehaviorTraits> {
    using Base = cViewerRefNodeBaseT<cIViewerRefNodeRecursive, cViewerRefNodeRecursiveImpl,
                                     cViewerRefNodeRecursiveBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x10c;
    static constexpr uint32_t kIid = cViewerRefNodeRecursiveImpl::kIid;
    static constexpr uint32_t kBaseIid = cViewerRefNodeBaseImpl::kIid;

    // (0xa69f50 / 0xa69f30): nothing past the viewer base level's.
    cViewerRefNodeRecursive() = default;
    ~cViewerRefNodeRecursive() override = default;

    // 0xa6ac70 (cdecl; hooked): a new node, uncounted.
    static cViewerRefNodeRecursive *__cdecl Create();

    // ---- the game's bodies as hookable members (thiscall on the node) ----
    cViewerRefNodeRecursive *Construct();                        // 0xa69f50 (returns this)
    void Destruct();                                             // 0xa6a130
    cViewerRefNodeRecursive *DeletingDestructor(uint32_t flags); // 0xa6a330 (slot 125: sized delete of 0x10c on bit 0)
    // 0xa6ca60 (slot 97; hooked: cTSFenceEntry's slots 24 / 26 call it directly): the viewer base's +0xc4 word set.
    void SetWordC4(uint32_t value) { Base::Slot97(value); }

    // 0 (0xa6c160, then shared 0xa688a0): the recursive interface counted, then the viewer base's, else the renderable
    // node's.
    bool QueryInterface(uint32_t iid, void **out) override;
    // 15 (0xa6a930): as cViewerRefNode's, asking the context for the recursive interface; the Impl's CloneInto (0xa6bde0).
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;
    // 30 / 50 (shared_vf_a696e0): the traversal's slot 10.
    uint32_t Accept(cICompositionTreeTraversal *traversal) override { return traversal->TraversalSlot10(this); }
    uint32_t AcceptReadOnly(cICompositionTreeTraversal *traversal) override { return traversal->TraversalSlot10(this); }
    // 51 (0xa6a3e0 -> 0xa37d80): the composition family's empty attribute range, on the viewer base's composition part.
    void *MakeIterator51(void *out, uint32_t a) override
    {
        return node_family::BuildIterator51(static_cast<cCompositionTreeNodeImpl *>(static_cast<cViewerRefNodeBaseImpl *>(&mImpl)), out, a);
    }
    // 83 (0xa6a370 -> 0xa6aec0): the reflected view drawn.
    void Slot83(void *a, void *b) override
    {
        mImpl.Cull(this, static_cast<cViewerView *>(a), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b)));
    }

    // ---- the recursive level (123 / 124) ----
    void SetMaterialCount(uint32_t count) override { mImpl.SetMaterialCount(count); }          // 0xa6c950
    void SetMaterial(uint32_t index, const char *name) override { mImpl.SetMaterial(index, name); } // 0xa6cbb0
};

} // namespace nGZSceneGraph
