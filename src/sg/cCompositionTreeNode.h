#pragma once
// nGZSceneGraph::cCompositionTreeNode (lane NZ, scenegraph N0 step 3, the first node pilot): the plain tree node,
// class 0xfc700d40 of the scene graph's COM director. Standalone shape: cCompositionTreeNodeT<cICompositionTreeNode,
// cCompositionTreeNodeImpl> (sg/SGNodeTemplates.h) plus Clone; 0x28 bytes like the game's (vtables 0xe68b2c, 62 slots,
// and 0xe68c28 at +8).
//
// The switch: the factory 0xa37910 is hooked to Create, so every node the director makes is ours, slots and all (the
// game's slot bodies are folded across the node family and stay unhooked). Still made by the game: the constructor
// 0xa36900's other callers (the nTSSG object view 0x25a050, the viewer node 0xa6a750) and the folded Clone 0xa373c0
// (held by 11 vtables) build game nodes; those run the game's template slots on our Impl members.
#include "sg/SGNodeTemplates.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

class cCompositionTreeNode
    : public cCompositionTreeNodeT<cICompositionTreeNode, cCompositionTreeNodeImpl, cCompositionTreeNodeBehaviorTraits> {
public:
    static constexpr size_t kSize = 0x28;
    static constexpr uint32_t kClassId = 0xfc700d40; // the director's class id

    cCompositionTreeNode() = default;

    // 0xa37910 (cdecl; the director's factory, hooked): a new node, uncounted.
    static cCompositionTreeNode *__cdecl Create();

    // 15 (shared 0xa373c0): the context's copy if it has one, else a new node (counted); its content copied by the
    // Impl's CloneInto (CloneCompositionInto, sg/cNodeCloneContext.h) unless mode is 2; viewed as `iid` in *out.
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;

    // 43 / 44 / 51 (0xa37380 / 0xa37360 / 0xa37260): the Impl's iterator builders 0xa37fd0 / 0xa37e40 (thiscall on the
    // Impl: out, the node, a, b), ours (MakeConstTreeIterators / MakeTreeIterators, sg/cCompositionTreeIterators.h), and 0xa37d80 (out, a), ours (MakeEmptyAttributeEntryRange,
    // sg/cNullArrayAttributeEntryIterator.h); each returns out.
    void *MakeIterator43(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator44(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator51(void *out, uint32_t a) override;
};

} // namespace nGZSceneGraph
