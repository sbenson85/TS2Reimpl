#pragma once
// nGZSceneGraph::cPartitionNodeImpl (lane OI, scenegraph N1 step 1): the Impl of the partition nodes (cSceneNode,
// cLooseOctreePartitionNode). It adds nothing to the bounded node's state (0x60 bytes: both constructors, cSceneNode's
// 0xa4dcd0 and the octree's 0xa50d60, build it at +0xc and the classes' own members start at +0x6c), only its type
// record. A plain class like its bases; `outer` is the node that holds it. Hooked at its RVAs.
//
// Not functions of their own: the destructor 0xadfe30 is a 5-byte jump to cBoundedNodeImpl's (a thunk, folded with
// cRenderableNodeImpl's), and Read is 0xa4bd60, folded with cExplicitBoundNodeImpl::Read (its type record, then the
// bounded part), which is ours already.
#include "sg/cBoundedNodeImpl.h"

#include <cstdint>

namespace nGZSceneGraph {

class cPartitionNodeImpl : public cBoundedNodeImpl {
public:
    static constexpr uint32_t kVersion = 3;
    static constexpr uint32_t kTypeWord4 = 0;
    static constexpr uint32_t kIid = 0x1c8e36aa; // cIPartitionNode

    cPartitionNodeImpl() = default;
    cPartitionNodeImpl(const cPartitionNodeImpl &) = delete;
    cPartitionNodeImpl &operator=(const cPartitionNodeImpl &) = delete;

    cPartitionNodeImpl *Construct(); // 0xadfe10 (thiscall, returns this)

    // 0xa4bd60 (folded with cExplicitBoundNodeImpl's): the type record, then the bounded part.
    void Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader);
    bool IsKindOf(uint32_t iid);                                         // 0xadfe40
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out);               // 0xadfe60 ("cPartitionNode", 3)
    void Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer); // 0xadff10: the record, then the bounded part
};
static_assert(sizeof(cPartitionNodeImpl) == 0x60, "cPartitionNodeImpl size");

} // namespace nGZSceneGraph
