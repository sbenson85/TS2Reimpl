#pragma once
// nGZSceneGraph::cViewerRefNode (lane TZ, scenegraph N3r): the viewer-reference node, the renderable node that draws the
// scene through one or more views (cViewerRefEntry, sg/cViewerRefNodeImpl.h) into a target (the device's back buffer or
// a texture: cViewerRefNodeBaseImpl). Standalone shape: cViewerRefNodeBaseT<cIViewerRefNode, cViewerRefNodeImpl,
// cViewerRefNodeBehaviorTraits> (sg/cViewerRefNodeBaseT.h: the renderable template plus the viewer base level, slots
// 87-122) and the cViewerRefNodeT level (123-145); 0x110 bytes like the game's (vtables 0xe71b10, 147 slots, and
// 0xe71d60 at +8).
//
// The switch: the factory 0xa65fb0 (the director's, class id 0xdca76dbb) is hooked to Create, the complete constructor
// 0xa644a0 to Construct, the destructor 0xa64820 to Destruct and the deleting destructor 0xa64bd0 (slot 146) to
// DeletingDestructor. Once the constructor is ours no object carries the game's vtable, and the slot bodies only it
// reaches are in effect through callers (cViewerRefNodeRecursive's vtable 0xe71e10 has its own copies: it shares none
// of them). The Impl's methods are hooked at their RVAs, so the Recursive's copies use ours.
//
// The viewer registry 0xa68db0 is ours too (cViewerRefNodeBehaviorTraits::RegistryFor, hooked: the game's behaviour
// copies 0xa64f20 / 0xa687d0 still call it).
#include "sg/cIViewerRefNode.h"
#include "sg/cViewerRefNodeBaseT.h"
#include "sg/cViewerRefNodeImpl.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// The behaviour cells of cAbstractBehaviorTypeT<cIViewerRefNode, cRefSingleThreaded>.
struct cViewerRefNodeBehaviorTraits {
    // The connection-manager cache (an owned static, sg/SGBehaviorCopies.h: the game's copies use the same cell).
    static nGZEvent::cIConnectionManager *&EventManagerCell();
    // 0xa68db0 (cdecl (id, attribute), hooked; its holder 0xa63750 folded in): the map built on first use, a copy of the
    // transform family's map holding the viewer events ViewerFOV and ViewerNearFarClip; a non-zero id stores `attribute`.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                  const nGZEvent::cEventAttribute *attribute);
    // The map the slots use (RegistryFor with id 0).
    static nGZEvent::cEventAttributeMap *Registry();
    static void ResetRegistryForTest();
};

class cViewerRefNode : public cViewerRefNodeBaseT<cIViewerRefNode, cViewerRefNodeImpl, cViewerRefNodeBehaviorTraits> {
    using Base = cViewerRefNodeBaseT<cIViewerRefNode, cViewerRefNodeImpl, cViewerRefNodeBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x110;
    static constexpr uint32_t kIid = cViewerRefNodeImpl::kIid;
    static constexpr uint32_t kBaseIid = cViewerRefNodeBaseImpl::kIid;

    // (0xa644a0 / 0xa64250 / 0xa641f0): the viewer base level's flags (cViewerRefNodeBaseT) are all it sets.
    cViewerRefNode() = default;
    // (0xa64820 / 0xa64750): the entries' display-list slots freed (ResizeEntries(0)) before the bases' destructors.
    ~cViewerRefNode() override { mImpl.ResizeEntries(0); }

    // 0xa65fb0 (cdecl; the director's factory, hooked): a new node, uncounted.
    static cViewerRefNode *__cdecl Create();

    // ---- the game's bodies as hookable members (thiscall on the node) ----
    cViewerRefNode *Construct();                           // 0xa644a0 (returns this)
    void Destruct();                                       // 0xa64820
    cViewerRefNode *DeletingDestructor(uint32_t flags);    // 0xa64bd0 (slot 146: sized delete of 0x110 on bit 0)

    // ---- the behaviour ----
    // 14 (0xa65510 -> 0xa66600): the registry's record, or the missing one; for "fov" and "nearFarClip" the record's
    // word +8 is the number of entries.
    nGZEvent::cEventAttribute GetEventAttribute(uint32_t event) override;

    // 0 (0xa688e0, then shared 0xa688a0): the viewer interface counted, then the viewer base's, else the renderable
    // node's.
    bool QueryInterface(uint32_t iid, void **out) override;
    // 15 (0xa655a0): as cLightRefNode's, asking the context for the viewer interface; the Impl's CloneInto (0xa68660).
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;
    // 30 / 50 (0xa696f0): the traversal's slot 11.
    uint32_t Accept(cICompositionTreeTraversal *traversal) override { return traversal->TraversalSlot11(this); }
    uint32_t AcceptReadOnly(cICompositionTreeTraversal *traversal) override { return traversal->TraversalSlot11(this); }
    // 51 (0xa64c80 -> 0xa66440): the Impl's attribute names (43 / 44: the viewer base template's).
    void *MakeIterator51(void *out, uint32_t a) override { return mImpl.MakeIterator51(out, a); }
    // 83 (0xa64c30 -> 0xa661a0): the entries drawn once per frame of the view (82: the viewer base template's).
    void Slot83(void *a, void *b) override
    {
        mImpl.Cull(this, static_cast<cViewerView *>(a), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b)));
    }

    // ---- the viewer level (123-145) ----
    void Render() override { mImpl.Render(this); } // 0xa68f90
    void ResizeEntries(uint32_t count) override { mImpl.ResizeEntries(count); }
    void SetEntryPlacement(uint32_t index, const cRigidTransform *placement) override
    {
        mImpl.SetEntryPlacement(index, placement);
    }
    void SetMaterialCount(uint32_t index, uint32_t count) override { mImpl.SetMaterialCount(index, count); }
    void SetMaterial(uint32_t index, uint32_t slot, const char *name) override { mImpl.SetMaterial(index, slot, name); }
    void SetViewport(uint32_t index, float x, float y, float w, float h) override { mImpl.SetViewport(index, x, y, w, h); }
    void SetSubrect(uint32_t index, float x, float y, float w, float h) override { mImpl.SetSubrect(index, x, y, w, h); }
    void SetFixedAspect(uint32_t index, uint8_t fixed) override { mImpl.SetFixedAspect(index, fixed); }
    void SetPixelAspect(uint32_t index, float aspect) override { mImpl.SetPixelAspect(index, aspect); }
    void SetPerspective(uint32_t index, float fov) override { mImpl.SetPerspective(index, fov); }
    void SetDepthRange(uint32_t index, float min, float max) override { mImpl.SetDepthRange(index, min, max); }
    void SetNearFar(uint32_t index, float nearClip, float farClip) override { mImpl.SetNearFar(index, nearClip, farClip); }
    void SetOffsets(uint32_t index, float x, float y) override { mImpl.SetOffsets(index, x, y); }
    void SetOrtho(uint32_t index, float left, float right, float bottom, float top, float nearClip,
                  float farClip) override
    {
        mImpl.SetOrtho(index, left, right, bottom, top, nearClip, farClip);
    }
    void SetProjection(uint32_t index, const float *matrix) override { mImpl.SetProjection(index, matrix); }
    uint32_t GetEntryCount() override { return mImpl.GetEntryCount(); } // 0xa68830
    cViewerRefEntry *GetEntry(uint32_t index) override { return mImpl.GetEntry(index); }
    cRigidTransform *GetView(cRigidTransform *out, uint32_t index) override { return mImpl.GetView(out, this, index); }
    float *GetViewport(float *out, uint32_t index) override { return mImpl.GetViewport(out, index); }
    float *WorldToScreen(float *out, const float *point, uint32_t index) override
    {
        return mImpl.WorldToScreen(out, this, point, index);
    }
    float *WorldToClip(float *out, const float *point, uint32_t index) override
    {
        return mImpl.WorldToClip(out, this, point, index);
    }
    float *ScreenToWorld(float *out, const float *point, uint32_t index) override
    {
        return mImpl.ScreenToWorld(out, this, point, index);
    }
    float *ClipToWorld(float *out, const float *point, uint32_t index) override
    {
        return mImpl.ClipToWorld(out, this, point, index);
    }
};

} // namespace nGZSceneGraph
