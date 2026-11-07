#pragma once
// nGZSceneGraph::cBoundedNodeImpl (lane OB, scenegraph N0 part 3 step 1): a transform node's state plus what a bounded
// node adds: two words the partition keeps (a holder object and its data), the node's cached world transform (brought up
// to date by the tree code's refresh when flag 12 is set: RefreshWorldTransforms, sg/SGWorldTransformRefresh.h) and a flag (11) the node
// announces when it changes. A plain class like its base; `outer` is the node that holds it. Hooked at its RVAs.
// 0xa3b700 (the clone) is a jump to cTransformNodeImpl's CloneInto: the bounded part is not cloned.
#include "sg/cTransformNodeImpl.h"

#include <cstdint>

namespace nGZSceneGraph {

class cBoundedNodeImpl : public cTransformNodeImpl {
public:
    static constexpr uint32_t kVersion = 5;  // what Write records
    static constexpr uint32_t kIid = 0x7d14eb66; // cIBoundedNode
    static constexpr uint32_t kBoundChangedEvent = 0xcc6c9992; // announced (a cNullEvent) when the bound flag changes
    enum : uint32_t { kStaleFlag = 12, kBoundFlag = 11, kReadSkipsBoundFlag = 6 };

    cBoundedNodeImpl() = default;
    cBoundedNodeImpl(const cBoundedNodeImpl &) = delete;
    cBoundedNodeImpl &operator=(const cBoundedNodeImpl &) = delete;

    cBoundedNodeImpl *Construct(); // 0xa3b290 (thiscall, returns this)

    // Not functions of the original (the node templates' slots inline them): the stale cache brought up to date by the
    // tree code, and the frame of the cached world transform (flags 1, the rotation from the quaternion, the offset the
    // translation).
    void RefreshIfStale(cICompositionTreeNode *node);
    void CachedFrame(AffineTransform3 *frame);

    // 0xa3b300 / 0xa3b3b0 (thiscall: out, the node, [the frame]; return out): the box of a node without a shape: its
    // cached world position twice (in `frame`, a position moved by the frame). The stale cache is refreshed first.
    cBoundBox *GetBoundBox(cBoundBox *out, cICompositionTreeNode *node);
    cBoundBox *GetBoundBoxIn(cBoundBox *out, cICompositionTreeNode *node, const cTransform34 *frame);
    // 0xa3b4a0: the transform node's part, then the bound flag byte (through the node's SetBoundFlag, slot 66) unless
    // flag 6 is set.
    void Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader);
    // 0xa3b530: flag 11 changes -> the tree code is told (NodeBoundFlagChanged) and the node's listeners hear
    // kBoundChangedEvent (a cNullEvent).
    void SetBoundFlag(cIBoundedNode *outer, bool value);
    bool IsKindOf(uint32_t iid);                                                  // 0xa3b5a0
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out);                        // 0xa3b5c0 ("cBoundedNode", 5)
    void Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer);  // 0xa3b670

    void *mHolder = nullptr;                              // +0x3c (slot 69; tentative: the partition's handle)
    uint32_t mHolderData = 0;                             // +0x40 (slot 76)
    cRigidTransform mWorld = {{0, 0, 0, 0, 0, 0, 1}};     // +0x44 the cached world transform
};
static_assert(sizeof(cBoundedNodeImpl) == 0x60, "cBoundedNodeImpl size");

} // namespace nGZSceneGraph
