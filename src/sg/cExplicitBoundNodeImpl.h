#pragma once
// nGZSceneGraph::cExplicitBoundNodeImpl (lane OB, scenegraph N0 part 3 step 1): a bounded node's state plus a shape (a
// nGZCompGeom bound the node keeps a counted copy of). A plain class like its bases; `outer` is the node that holds it.
// The constructor is inlined into the node's own (0xa4b430), so only the destructor 0xa4b630 and the persistence members
// have functions here. 0xa4bf40 (the clone) jumps to cTransformNodeImpl's CloneInto: the shape is not cloned.
#include "sg/cBoundedNodeImpl.h"

#include <cstdint>

namespace nGZSceneGraph {

class cExplicitBoundNodeImpl : public cBoundedNodeImpl {
public:
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kTypeWord4 = 0x5d3a239c;
    static constexpr uint32_t kIid = 0x7d3a2385; // cIExplicitBoundNode

    cExplicitBoundNodeImpl() = default;
    cExplicitBoundNodeImpl(const cExplicitBoundNodeImpl &) = delete;
    cExplicitBoundNodeImpl &operator=(const cExplicitBoundNodeImpl &) = delete;
    ~cExplicitBoundNodeImpl(); // 0xa4b630: releases the shape, then the bases'

    void Destruct(); // the destructor as a hookable member
    // 0xa4bd60 / 0xa4bec0: the type record, then the bounded part (the shape is not persisted).
    void Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader);
    bool IsKindOf(uint32_t iid);                                                  // 0xa4bdd0
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out);                        // 0xa4bdf0 ("cExplicitBoundNode", 1)
    void Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer);          // 0xa4bec0

    cIGZUnknown *mShape = nullptr; // +0x60: a nGZCompGeom::cIBound (counted)
};
static_assert(sizeof(cExplicitBoundNodeImpl) == 0x64, "cExplicitBoundNodeImpl size");

} // namespace nGZSceneGraph
