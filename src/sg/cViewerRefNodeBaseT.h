#pragma once
// nGZSceneGraph::cViewerRefNodeBaseT: the viewer base's node level (slots 87-122 of cIViewerRefNodeBase) and the
// renderable slots both viewer classes change, over cRenderableNodeT (sg/SGRenderableTemplates.h). The game instantiates
// it for cViewerRefNode (lane TZ, sg/cViewerRefNode.h; Impl cViewerRefNodeImpl, at node+0xc) and cViewerRefNodeRecursive
// (lane VF, sg/cViewerRefNodeRecursive.h; Impl cViewerRefNodeRecursiveImpl, whose viewer base sits at Impl+4 behind its
// own vtable). `Impl` derives from cViewerRefNodeBaseImpl; the slots reach the base's members through it.
//
// The game's copies for cViewerRefNode: 0xa69340 (91), 0xa65550 (104), 0xa69680 (103) and the forwarders listed in
// db/status/cViewerRefNode.jsonl; for cViewerRefNodeRecursive: db/status/cViewerRefNodeRecursive.jsonl.
#include "sg/SGRenderableTemplates.h"
#include "sg/cIViewerRefNode.h"
#include "sg/cTransformNode.h" // node_family's iterators
#include "sg/cViewerRefNodeBaseImpl.h"

#include <cstdint>

namespace nGZSceneGraph {

template <class Interface, class Impl, class Traits>
class cViewerRefNodeBaseT : public cRenderableNodeT<Interface, Impl, Traits> {
protected:
    // The viewer base's part of the Impl (the Recursive's is at Impl+4).
    cViewerRefNodeBaseImpl &Base() { return this->mImpl; }

public:
    enum : uint32_t { kViewerFlag = 8, kUnboundedFlag = 0xb };

    // The level's constructor: flag 8 set and flag 0xb cleared, after the renderable node's.
    cViewerRefNodeBaseT()
    {
        Base().SetFlag(kViewerFlag, true);
        Base().SetFlag(kUnboundedFlag, false);
    }

    // 43 / 44 (the composition family's iterators, on the Impl's composition part).
    void *MakeIterator43(void *out, uint32_t a, uint32_t b) override
    {
        return node_family::BuildIterator43(static_cast<cCompositionTreeNodeImpl *>(&Base()), out, this, a, b);
    }
    void *MakeIterator44(void *out, uint32_t a, uint32_t b) override
    {
        return node_family::BuildIterator44(static_cast<cCompositionTreeNodeImpl *>(&Base()), out, this, a, b);
    }
    // 60: the renderable Impl's SetWorldTransform.
    void OnContentChanged(uint32_t transform, uint32_t b, uint32_t c) override
    {
        Base().SetWorldTransform(reinterpret_cast<const cRigidTransform *>(uintptr_t(transform)), b, c);
    }
    // 70 / 73-75: the held bound node's; 71 / 72 go to the Impl through the bounded template.
    bool IsEverywhere() override { return Base().BoundSlot70(); }
    bool IntersectsBound(uint32_t bound, uint32_t frame) override { return Base().BoundSlot73(bound, frame); }
    bool IntersectsLinear(nGZCompGeom::cILinear3 *linear) override { return Base().IntersectsLinear(linear); }
    bool FirstHitLinear(float *t, nGZCompGeom::cILinear3 *linear) override { return Base().FirstHitLinear(t, linear); }
    // 82: the cull walk (the base Impl's CullAndDraw).
    void Draw(void *viewer, uint32_t planeMask) override
    {
        Base().CullAndDraw(this, static_cast<cViewerView *>(viewer), planeMask);
    }

    // ---- the viewer base level (87-122) ----
    uint8_t GetLayerTagMatch() override { return Base().mLayerTagMatch; }
    uint32_t Slot88() override { return Base().mDetailLimit; }
    uint32_t Slot89() override { return Base().mZero6c; }
    void Slot90(uint32_t value) override { Base().mZero6c = value; }
    // 91: the clear description stored (the base Impl's +0xcc..+0xe8).
    void Slot91(const cViewerClearInfo *info) override
    {
        cViewerRefNodeBaseImpl &b = Base();
        b.mWordCC = info->mFlags;
        for (int i = 0; i < 4; ++i) b.mVec[i] = info->mColor[i];
        b.mScaleE0 = info->mDepth;
        b.mWordE4 = info->mStencil;
    }
    void Slot92(cIGZUnknown *texture, cIGZUnknown *ref94, float scale) override { Base().SetExplicit(texture, ref94, scale); }
    void Slot93(uint32_t count, cIGZUnknown *const *textures, cIGZUnknown *ref94, float scale) override
    {
        Base().SetTextures(count, textures, ref94, scale);
    }
    void Slot94(uint32_t id) override { Base().SetTexture(id); }
    void Slot95(const char *name, uint32_t group) override { Base().SetTextureByName(name, group); }
    void Slot96(cIGZUnknown *color, cIGZUnknown *depth) override { Base().SetSurfaces(color, depth); }
    void Slot97(uint32_t value) override { Base().mWordC4 = value; }
    void SetLayerTagMatch(uint32_t value) override { Base().mLayerTagMatch = static_cast<uint8_t>(value); }
    void Slot99(cIGZUnknown *ref) override { Base().SetRefA8(ref); }
    void Slot100(cIGZUnknown *ref) override { Base().SetRefA4(ref); }
    void Slot101(cIBoundedNode *bound, const float *plane, uint32_t flags) override
    {
        Base().SetBoundAndPlane(this, bound, plane, flags);
    }
    void Slot102(cIBoundedNode *bound) override { Base().SetBound(this, bound); }
    // 103: the +0x70 reference, counted (nothing when it is the same).
    void Slot103(cIGZUnknown *ref) override
    {
        cIGZUnknown *const old = Base().mRef70;
        if (ref == old) return;
        if (ref) ref->AddRef();
        Base().mRef70 = ref;
        if (old) old->Release();
    }
    // 104: slot 91's description read back.
    void GetClearInfo(cViewerClearInfo *out) override
    {
        cViewerRefNodeBaseImpl &b = Base();
        out->mFlags = b.mWordCC;
        for (int i = 0; i < 4; ++i) out->mColor[i] = b.mVec[i];
        out->mDepth = b.mScaleE0;
        out->mStencil = b.mWordE4;
    }
    uint32_t GetTextureCount() override { return Base().CallTextureSlot7(); }
    cIGZUnknown *GetTexture(int32_t index) override { return Base().GetTextureN(index); }
    cIGZUnknown *GetTextureRef94() override { return Base().GetTextureRef94(); }
    uint32_t Slot108() override { return Base().mTextureHandle; }
    cIGZUnknown *GetHandleTexture() override { return Base().GetHandleTexture(); }
    cViewerTargetSize *GetTargetSize(cViewerTargetSize *out) override
    {
        Base().GetTargetSize(out);
        return out;
    }
    cIGZUnknown *GetColorSurface() override { return Base().GetColorSurface(); }
    cIGZUnknown *GetDepthSurface() override { return Base().GetDepthSurface(); }
    bool Slot113() override { return Base().IsDeviceViewMode(); }
    bool Slot114() override { return Base().UsesDeviceTargets(); }
    uint32_t GetRenderType() override { return Base().mWordC4; }
    cIGZUnknown *Slot116() override { return Base().mRefA8; }
    cIDisplayListRenderStrategy *GetRenderStrategy() override
    {
        return reinterpret_cast<cIDisplayListRenderStrategy *>(Base().mRefA4);
    }
    cIBoundedNode *Slot118() override { return Base().mBound; }
    float *Slot119() override { return Base().mPlane; }
    uint32_t Slot120() override { return Base().mPlaneFlags; }
    cIGZUnknown *GetFrameCacheHolder() override { return Base().mEvent74; }
    // 122: the +0x70 reference (the interface's name for it is older than this reading).
    uint32_t GetTextureMode() override { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Base().mRef70)); }
};

} // namespace nGZSceneGraph
