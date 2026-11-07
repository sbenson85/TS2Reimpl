// nGZSceneGraph::cExplicitBoundNode (lane OB). See the header.
#include "sg/cExplicitBoundNode.h"

#include "sg/cTransformNode.h" // node_family

#include <new>

namespace nGZSceneGraph {

static_assert(sizeof(cExplicitBoundNode) == cExplicitBoundNode::kSize, "cExplicitBoundNode size");

// (The original's operator new throws on failure, so its null branch is dead.)
cExplicitBoundNode *__cdecl cExplicitBoundNode::Create() { return new cExplicitBoundNode; }

void *cExplicitBoundNode::MakeIterator43(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator43(&mImpl, out, this, a, b);
}
void *cExplicitBoundNode::MakeIterator44(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator44(&mImpl, out, this, a, b);
}
void *cExplicitBoundNode::MakeIterator51(void *out, uint32_t a) { return node_family::BuildIterator51(&mImpl, out, a); }

void cExplicitBoundNode::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context)
{
    cICompositionTreeNode *clone = nullptr;
    if (context) {
        context->FindClone(this, cIExplicitBoundNode::kIid, &clone);
    } else {
        clone = new cExplicitBoundNode;
        clone->AddRef();
    }
    if (mode != 2) {
        auto *const cloneImpl = reinterpret_cast<cTransformNodeImpl *>(reinterpret_cast<uint8_t *>(clone) + 0xc);
        mImpl.CloneInto(this, clone, cloneImpl, mode, context);
    }
    clone->QueryInterface(iid, out);
    if (clone) clone->Release();
}

} // namespace nGZSceneGraph
