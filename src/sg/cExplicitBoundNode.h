#pragma once
// nGZSceneGraph::cExplicitBoundNode (lane OB, scenegraph N0 part 3 step 3): the explicit-bound node, a transform node whose
// bound is a shape it keeps (nGZCompGeom bounds), placed by the node's cached world transform. Standalone shape:
// cExplicitBoundNodeT<cIExplicitBoundNode, cExplicitBoundNodeImpl, cExplicitBoundNodeBehaviorTraits>
// (sg/SGNodeTemplates.h) plus Clone; 0x70 bytes like the game's (vtables 0xe6c2a4, 80 slots, and 0xe6c3e8 at +8).
//
// The switch: the factory 0xa4bc30 is hooked to Create, so every explicit-bound node the director makes is ours, slots and
// all (the game's slot bodies are folded across the node family and stay unhooked). Still made by the game: whatever
// builds one through the constructor 0xa4b500 directly; those run the game's template slots on our Impl members.
#include "sg/SGNodeTemplates.h"
#include "sg/cExplicitBoundNodeImpl.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

class cExplicitBoundNode
    : public cExplicitBoundNodeT<cIExplicitBoundNode, cExplicitBoundNodeImpl, cExplicitBoundNodeBehaviorTraits> {
public:
    static constexpr size_t kSize = 0x70;

    cExplicitBoundNode() = default;

    // 0xa4bc30 (cdecl; the director's factory, hooked): a new node, uncounted.
    static cExplicitBoundNode *__cdecl Create();

    // 15 (shared 0xa4b920): as cTransformNode's, asking the context for the explicit-bound interface; the Impl's
    // CloneInto (0xa3ae50 through the thunk 0xa4bf40) copies the limit and the transform only, not the shape.
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;

    // 43 / 44 / 51: the Impl's iterator builders (cTransformNode.h), still game code; each returns out.
    void *MakeIterator43(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator44(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator51(void *out, uint32_t a) override;
};

} // namespace nGZSceneGraph
