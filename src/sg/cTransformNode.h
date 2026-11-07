#pragma once
// nGZSceneGraph::cTransformNode (lane OB, scenegraph N0 part 3 step 3): the transform node, a tree node with a rigid
// transform relative to its parent and a limit. Standalone shape: cTransformNodeT<cITransformNode, cTransformNodeImpl,
// cTransformNodeBehaviorTraits> (sg/SGNodeTemplates.h) plus Clone; 0x48 bytes like the game's (vtables 0xe692ac, 67 slots,
// and 0xe693bc at +8).
//
// The switch: the factory 0xa3a6c0 is hooked to Create, so every transform node the director makes is ours, slots and all
// (the game's slot bodies are folded across the node family and stay unhooked). Still made by the game: whatever builds a
// cTransformNode through the constructor 0xa3a000 directly, and the folded Clone 0xa3a3b0's other holders; those run the
// game's template slots on our Impl members.
#include "sg/SGNodeTemplates.h"
#include "sg/cTransformNodeImpl.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

class cTransformNode
    : public cTransformNodeT<cITransformNode, cTransformNodeImpl, cTransformNodeBehaviorTraits> {
public:
    static constexpr size_t kSize = 0x48;

    cTransformNode() = default;

    // 0xa3a6c0 (cdecl; the director's factory, hooked): a new node, uncounted.
    static cTransformNode *__cdecl Create();

    // 15 (shared 0xa3a3b0): the context's copy if it has one (asked for the transform interface), else a new node
    // (counted); its content copied by the Impl's CloneInto (0xa3ae50, ours) unless mode is 2; viewed as `iid` in *out.
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;

    // 43 / 44 / 51: the Impl's iterator builders (see cCompositionTreeNode.h), ours; each returns out.
    void *MakeIterator43(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator44(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator51(void *out, uint32_t a) override;
};

namespace node_family {
// The builders every node of the composition family's slots 43 / 44 / 51 call (0xa37fd0 / 0xa37e40 thiscall on the Impl:
// out, the node, a, b, ours: sg/cCompositionTreeIterators.h; 0xa37d80: out, a, ours: MakeEmptyAttributeEntryRange, sg/cNullArrayAttributeEntryIterator.h).
// Defined with cTransformNode and shared by the family's pilots.
void *BuildIterator43(void *impl, void *out, cICompositionTreeNode *node, uint32_t a, uint32_t b);
void *BuildIterator44(void *impl, void *out, cICompositionTreeNode *node, uint32_t a, uint32_t b);
void *BuildIterator51(void *impl, void *out, uint32_t a);
} // namespace node_family

} // namespace nGZSceneGraph
