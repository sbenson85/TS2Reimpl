#pragma once
// nGZSceneGraph::cRenderableNodeImpl (lane PX, scenegraph N3 step 1): a bounded node's state plus what a node that draws
// adds: a mask of render groups ("layers": one bit per group the render-group registry knows by name), a word the
// viewer's draw reads (+0x64, slot 81 sets it) and the detail limit cached from the detail object (+0x68, refreshed
// whenever the mask changes). The base of the viewer-reference, light-reference and shape-reference nodes' state and of
// cTSParticlesRenderable's. A plain class like its bases; `outer` is the node that holds it. Hooked at its RVAs.
// 0xadfe30 (a 5-byte thunk shared with the partition) has no hook.
//
// The render-group registry is ours (0xaeb370, sg/cRenderGroupManager.cpp). Not ours yet (called through
// OriginalFunction): the detail object (0xaead80), whose slot 43 gives the limit.
#include "sg/cBoundedNodeImpl.h"
#include "sg/cRenderGroupNameIterator.h"

#include <cstdint>

namespace nGZSceneGraph {

class cRenderableNodeImpl : public cBoundedNodeImpl {
public:
    static constexpr uint32_t kVersion = 5;      // what Write records
    static constexpr uint32_t kIid = 0x1c585e30; // cIRenderableNode
    static constexpr uint32_t kAllLayers = 0xffffffffu;
    enum : uint32_t { kPersistedFlag = 10 };      // flag 10 is written and read back

    cRenderableNodeImpl() = default;
    cRenderableNodeImpl(const cRenderableNodeImpl &) = delete;
    cRenderableNodeImpl &operator=(const cRenderableNodeImpl &) = delete;

    cRenderableNodeImpl *Construct(); // 0xadfff0 (thiscall, returns this)

    void AddLayer(const char *name);     // 0xae00a0: the name's bit (the registry registers a new name)
    void AddLayerBits(uint32_t bits);    // 0xae00d0
    void RemoveLayer(const char *name);  // 0xae02a0
    void RemoveLayerBits(uint32_t bits); // 0xae02d0
    // 0xae00f0: how many groups the mask holds (the registry's count when it is all ones).
    uint32_t CountLayers();
    // 0xae02f0 (thiscall, returns out): the names' range, each end counted.
    cRenderGroupNameRange *MakeLayerNames(cRenderGroupNameRange *out);
    // 0xae04b0 (the viewer's and light reference's slot 60): flag 12 (stale) cleared, the cached world transform set
    // from the seven floats, the detail limit refreshed.
    void SetWorldTransform(const cRigidTransform *transform, uint32_t, uint32_t); // (ret 0xc: slot 60 takes three words, the other two unread)
    // 0xae06b0: the detail object's limit.
    void RefreshDetailLimit();

    void Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader);   // 0xae0140
    bool IsKindOf(uint32_t iid);                                          // 0xae03d0
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out);                 // 0xae03f0 ("cRenderableNode", 5)
    void Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer);  // 0xae04f0
    // 0xae0630: the transform part cloned, then the clone's layers become this one's and it takes the word.
    void CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone, cRenderableNodeImpl *cloneImpl,
                   uint32_t mode, cINodeCloneContext *context);

    uint32_t mLayers = 1;  // +0x60: the render-group mask
    uint32_t mLayerTag = 0;  // +0x64 (slot 81 SetLayerTag; persisted): matched against a viewer's tag (Pass::Shows)
    uint32_t mDetailLimit; // +0x68: the detail object's limit (the constructor leaves it)
    uint32_t mZero6c = 0;  // +0x6c
};
static_assert(sizeof(cRenderableNodeImpl) == 0x70, "cRenderableNodeImpl size");

} // namespace nGZSceneGraph
