#pragma once
// nGZSceneGraph::cRenderableNodeT<I, Impl> (lane SN, scenegraph H2a): the level a node that draws adds to the bounded
// template (sg/SGNodeTemplates.h). The game instantiates it for the shape-reference, viewer-reference and
// light-reference nodes and for cTSParticlesRenderable, each its own copy; the slot bodies of the level are folded across
// them (shared_vf_*). `Impl` is cRenderableNodeImpl or a class derived from it (the shape reference's cShapeRefNodeImpl).
//
// The level's constructor (0xa54010 in the shape reference's instantiation) sets flags 5 and 10 after the bounded
// node's; it has no destructor of its own. Slots 82 (Draw) and 83 are each class's; slot 60 too (the viewer and light
// references' go to the Impl's SetWorldTransform, the shape reference's to its own Impl body), so the concrete class
// names them.
#include "sg/SGNodeTemplates.h"
#include "sg/cRenderableNodeImpl.h"

#include <cstdint>

namespace nGZSceneGraph {

template <class Iface, class Impl, class Traits>
class cRenderableNodeT : public cBoundedNodeT<Iface, Impl, Traits> {
    using Base = cBoundedNodeT<Iface, Impl, Traits>;

public:
    enum : uint32_t { kRenderableFlag = 5 };

    // (0xa54010): flags 5 and 10 (the persisted one, cRenderableNodeImpl::kPersistedFlag) set, after the bounded node's.
    cRenderableNodeT()
    {
        this->mImpl.SetFlag(kRenderableFlag, true);
        this->mImpl.SetFlag(cRenderableNodeImpl::kPersistedFlag, true);
    }

    // shared 0xa58020: the renderable interface counted, else the bounded node's.
    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (!out) return false;
        if (iid == cRenderableNodeImpl::kIid) {
            *out = static_cast<Iface *>(this);
            this->AddRef();
            return true;
        }
        return Base::QueryInterface(iid, out);
    }

    // 77-81 (shared 0xa54900, 0xa54910, 0xa586f0, 0xa58700, 0x1d44f0): the render-group mask and the word, on the Impl.
    void AddLayer(const char *name) override { this->mImpl.AddLayer(name); }
    void AddLayerBits(uint32_t bits) override { this->mImpl.AddLayerBits(bits); }
    void RemoveLayer(const char *name) override { this->mImpl.RemoveLayer(name); }
    void RemoveLayerBits(uint32_t bits) override { this->mImpl.RemoveLayerBits(bits); }
    void SetLayerTag(uint32_t value) override { this->mImpl.mLayerTag = value; }
    // 84-86 (shared 0xa57ff0, 0xa58740, 0xa58720).
    uint32_t CountLayers() override { return this->mImpl.CountLayers(); }
    cRenderGroupNameRange *MakeLayerNames(cRenderGroupNameRange *out) override
    {
        this->mImpl.MakeLayerNames(out);
        return out;
    }
    uint32_t *GetLayerState(uint32_t *out) override
    {
        out[0] = this->mImpl.mLayers;
        out[1] = this->mImpl.mLayerTag;
        return out;
    }
};

} // namespace nGZSceneGraph
