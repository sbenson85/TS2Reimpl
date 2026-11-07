#pragma once
// The composition Impl's CloneInto and its clone context (lane OR, scenegraph N0 part 3 step 4).
//
// CloneCompositionInto (0xa384f0) is the composition level's share of a node's Clone (slot 15): the object-graph part
// (extensions, tag) and the flags copied into the clone's Impl, the clone marked as not queued (flag 13) and stale (flag
// 12), and under a deep clone (mode 1) each child cloned in turn and added to the clone. A deep clone shares one
// cNodeCloneContext across the whole subtree: the caller's, or one made here that maps the source to the clone.
//
// cNodeCloneContext (the game's `?A0x56c0fdbf::cNodeCloneContext`, 0x30 bytes, vtables 0xe68d68 +0 and 0xe68d84 +8): a
// counted object (cRZUnknown) with the cINodeCloneContext face at +8, mapping each source node it was asked about to its
// counted clone. Asked about a node it has not seen, it clones the node shallow (mode 2) if the node lies in the cloned
// subtree (the node or an ancestor is the root source), and remembers null otherwise; either answer is kept. Only
// CloneInto builds one (the two vtables are stored by nothing else), and every user reaches it through its slots, so
// the map is our toolchain's std::unordered_map (the game's has the same layout: max load +0xc, list +0x10 / +0x14,
// buckets +0x18..+0x20, mask +0x24, max index +0x28). Its hasher is the game's: the pointer value itself (0xa37980
// passes the key as the hash), not std::hash's FNV-1a. The destructor releases the clones in the map's list order,
// which the hash decides (mostly two buckets of eight, aligned pointers), so the order of those releases follows it.
#include "rz/RZUnknownBase.h"
#include "sg/SGNodeInterfaces.h"

#include <cstdint>
#include <unordered_map>

namespace nGZSceneGraph {

class cCompositionTreeNodeImpl;

class cNodeCloneContext : public rz::cRZUnknown, public cINodeCloneContext {
public:
    static constexpr uint32_t kSize = 0x30;

    // The context of a clone of `root` into `rootClone` (counted). The count starts at 0.
    cNodeCloneContext(cICompositionTreeNode *root, cICompositionTreeNode *rootClone);
    // 0xa371d0 (slot 3, the deleting destructor): each clone released, in the map's order.
    ~cNodeCloneContext() override;

    // 0xa38ce0 (thunk 0xa38d08): cINodeCloneContext (0xecce4c52) answers the +8 face uncounted; anything else is
    // cRZUnknown's answer.
    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override { return rz::cRZUnknown::AddRef(); }
    uint32_t Release() override { return rz::cRZUnknown::Release(); }
    // 0xa37980 (+8 slot 3): the clone of `node` viewed as `iid` in *out (through its QueryInterface), or null.
    void FindClone(cIObjectGraphNode *node, uint32_t iid, void *out) override;

    cNodeCloneContext(const cNodeCloneContext &) = delete;
    cNodeCloneContext &operator=(const cNodeCloneContext &) = delete;

private:
    struct PointerHash {
        size_t operator()(const cICompositionTreeNode *node) const { return reinterpret_cast<size_t>(node); }
    };
    std::unordered_map<cICompositionTreeNode *, cICompositionTreeNode *, PointerHash> mClones; // +0xc source -> clone
    cICompositionTreeNode *mRoot;                                                               // +0x2c the root source
};

// 0xa384f0 (thiscall on the source's composition Impl, ret 0x14): the composition part of `source` cloned into `clone`
// (whose Impl is `cloneImpl`); with mode 1 the children too, through `context` or a context of its own.
void __fastcall CloneCompositionInto(cCompositionTreeNodeImpl *impl, void *edx, cICompositionTreeNode *source,
                                     cICompositionTreeNode *clone, cCompositionTreeNodeImpl *cloneImpl, uint32_t mode,
                                     cINodeCloneContext *context);

} // namespace nGZSceneGraph
