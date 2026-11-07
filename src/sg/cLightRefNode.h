#pragma once
// nGZSceneGraph::cLightRefNode (lane TE, scenegraph N2r part 2): the light-reference node, the renderable node that
// places a light (nGZSceneGraph::cILight, sg/SGLightInterfaces.h) in the scene graph. Standalone shape:
// cRenderableNodeT<cILightRefNode, cLightRefNodeImpl, cLightRefNodeBehaviorTraits> (sg/SGRenderableTemplates.h) plus the
// light-reference level (the game's cLightRefNodeT) and Clone; 0x90 bytes like the game's (vtables 0xe70334, 99 slots,
// and 0xe704c4 at +8).
//
// The switch: the factory 0xa61a80 (the director's, class id 0x253d2018) is hooked to Create, the complete constructor
// 0xa60c00 to Construct, the destructor 0xa60ea0 to Destruct and the deleting destructor 0xa61150 (slot 98) to
// DeletingDestructor. No class derives from the game's cLightRefNode, so once the constructor is ours no object carries
// its vtables, and every slot body and template-level copy only those vtables and the constructor chain reach is in
// effect through its callers (db/status/cLightRefNode.jsonl, db/status/SGNodeCopies.jsonl).
//
// Slot 51's builder 0xa61d90 is hooked (lane TN; its name iterators in sg/cLightRefAttributeNameIterator.h). What stays the game's: the light-ref registry 0xa633d0
// (behavior_copies::LightRefRegistryFor). The display list the draw slots hand the light to is reached by its slots.
#include "sg/SGRenderableTemplates.h"
#include "sg/cLightRefNodeImpl.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// nGZSceneGraph::cILightRefNode (interface id 0x053d200a): a renderable node referring to one light (counted), with a
// copy of it the attribute setters write. Names are ours and tentative.
class cILightRefNode : public cIRenderableNode {
public:
    static constexpr uint32_t kIid = cLightRefNodeImpl::kIid;

    virtual uint8_t GetLightFlag() = 0;            // 87 (shared 0x4c9600: the Impl's +0x7c byte)
    virtual uint32_t GetDetailLimit() = 0;         // 88 (shared 0x251bf0: the Impl's +0x68)
    virtual uint32_t GetWord6c() = 0;              // 89 (shared 0x24bdc0: the Impl's +0x6c)
    virtual void SetWord6c(uint32_t value) = 0;    // 90 (shared 0x4daa20)
    virtual void SetLightFlag(uint8_t value) = 0;  // 91 (shared 0x625390)
    virtual void DropLightCopy() = 0;              // 92 (0xa63520): the copy released, the transform propagated
    virtual void SetLight(cILight *light) = 0;     // 93 (0xa636c0 -> 0xa62d80)
    virtual cILight *GetLightConst() = 0;          // 94 (shared 0x24a310: the Impl's +0x70)
    virtual cILight *GetLight() = 0;               // 95 (shared 0x24a310)
    virtual void SetWord78(int32_t value) = 0;     // 96 (shared 0x239740: the Impl's +0x78)
    virtual int32_t GetWord78() = 0;               // 97 (shared 0x238f10)
};

// The behaviour cells of cAbstractBehaviorTypeT<cILightRefNode, cRefSingleThreaded>.
struct cLightRefNodeBehaviorTraits {
    // 0xa63550's cache (an owned static, sg/SGBehaviorCopies.h: the game's copies use the same cell).
    static nGZEvent::cIConnectionManager *&EventManagerCell();
    // 0xa633d0 (game code: a function-local static map built on first use), asked with id 0.
    static nGZEvent::cEventAttributeMap *Registry();
};

class cLightRefNode : public cRenderableNodeT<cILightRefNode, cLightRefNodeImpl, cLightRefNodeBehaviorTraits> {
    using Base = cRenderableNodeT<cILightRefNode, cLightRefNodeImpl, cLightRefNodeBehaviorTraits>;
    using Behavior = nGZEvent::cAbstractBehaviorTypeST<cILightRefNode, cLightRefNodeBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x90;
    enum : uint32_t { kLightRefFlag = 2 };

    // (0xa60c00 / 0xa60a20): flag 2 set, after the renderable node's.
    cLightRefNode() { mImpl.SetFlag(kLightRefFlag, true); }
    // (0xa60ea0 / 0xa60de0): the light let go of through SetLight (its notifications included) before the renderable
    // level's destructor.
    ~cLightRefNode() override;

    // 0xa61a80 (cdecl; the director's factory, hooked): a new node, uncounted.
    static cLightRefNode *__cdecl Create();

    // ---- the game's bodies as hookable members (thiscall on the node) ----
    // The constructor 0xa60c00 (returns this), the destructor 0xa60ea0 (qualified, so non-virtual), the deleting
    // destructor 0xa61150 (slot 98: the sized delete of 0x90 bytes on bit 0, returns this).
    cLightRefNode *Construct();
    void Destruct();
    cLightRefNode *DeletingDestructor(uint32_t flags);

    // ---- the behaviour ----
    // 3 (0xa61b70): the behaviour's (the active bit), then the Impl's.
    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t sender) override;
    // 14 (0xa61620 -> 0xa62230): the registry's record, or the missing one; a light attribute's record found there
    // says (word +8) whether the node has a light.
    nGZEvent::cEventAttribute GetEventAttribute(uint32_t event) override;

    // 0 (0xa63360): the light-reference interface counted, else the renderable node's.
    bool QueryInterface(uint32_t iid, void **out) override;
    // 15 (0xa61720): as cShapeRefNode's, asking the context for the light-reference interface; the Impl's CloneInto
    // (0xa631f0) does the copying.
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;
    // 30 / 50 (0xa636e0): the traversal's slot 9.
    uint32_t Accept(cICompositionTreeTraversal *traversal) override { return traversal->TraversalSlot9(this); }
    uint32_t AcceptReadOnly(cICompositionTreeTraversal *traversal) override { return traversal->TraversalSlot9(this); }
    // 43 / 44 (shared 0xa37380 / 0xa37360) and 51 (0xa611d0).
    void *MakeIterator43(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator44(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator51(void *out, uint32_t a) override { return mImpl.MakeIterator51(out, a); }

    // 60 (shared 0xa59320 -> 0xae04b0): the renderable Impl's SetWorldTransform.
    void OnContentChanged(uint32_t transform, uint32_t b, uint32_t c) override
    {
        mImpl.SetWorldTransform(reinterpret_cast<const cRigidTransform *>(uintptr_t(transform)), b, c);
    }
    // 70 (0xa631d0): a light without a bound shape.
    bool IsEverywhere() override { return mImpl.mLight != nullptr && mImpl.mLight->GetBoundShape() == nullptr; }
    // 71 / 72 go to the Impl through the bounded template; 73-75 (0xa63260 / 0xa63280 / 0xa63290).
    bool IntersectsBound(uint32_t bound, uint32_t frame) override
    {
        return mImpl.IntersectsBound(this, reinterpret_cast<nGZCompGeom::cIBound *>(uintptr_t(bound)),
                                     reinterpret_cast<const AffineTransform3 *>(uintptr_t(frame)));
    }
    bool IntersectsLinear(nGZCompGeom::cILinear3 *linear) override { return mImpl.IntersectsLinear(this, linear); }
    bool FirstHitLinear(float *t, nGZCompGeom::cILinear3 *linear) override { return mImpl.FirstHitLinear(this, t, linear); }

    // 82 / 83 (0xa611a0 / 0xa61180): the view (the viewer's cViewerView) and the plane mask (83: a byte).
    void Draw(void *viewer, uint32_t planeMask) override { mImpl.Draw(this, static_cast<cViewerView *>(viewer), planeMask); }
    void Slot83(void *a, void *b) override
    {
        mImpl.Cull(this, static_cast<cViewerView *>(a), static_cast<uint8_t>(reinterpret_cast<uintptr_t>(b)));
    }

    // ---- cILightRefNode ----
    uint8_t GetLightFlag() override { return mImpl.mFlag7c; }
    uint32_t GetDetailLimit() override { return mImpl.mDetailLimit; }
    uint32_t GetWord6c() override { return mImpl.mZero6c; }
    void SetWord6c(uint32_t value) override { mImpl.mZero6c = value; }
    void SetLightFlag(uint8_t value) override { mImpl.mFlag7c = value; }
    void DropLightCopy() override;
    void SetLight(cILight *light) override { mImpl.SetLight(this, light); }
    cILight *GetLightConst() override { return mImpl.mLight; }
    cILight *GetLight() override { return mImpl.mLight; }
    void SetWord78(int32_t value) override { mImpl.mWord78 = value; }
    int32_t GetWord78() override { return mImpl.mWord78; }
};

} // namespace nGZSceneGraph
