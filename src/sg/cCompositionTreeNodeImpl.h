#pragma once
// nGZSceneGraph::cCompositionTreeNodeImpl (lane NZ, scenegraph N0 step 1): a tree node's state (the composition node
// templates' member at +0xc) and the work behind the cICompositionTreeNode slots: the links to the parent, the
// siblings and the first child, and a word of flags. A plain class like its base cObjectGraphNodeImpl; `outer` is the
// node that holds it. Hooked at its RVAs (the templates' forwarders, folded across the node family, reach it there).
//
// Links: a node holds its first child and its next sibling counted, its parent and its previous sibling raw. Children
// are added at the front. Adding and removing a child is announced around the change (cObjectGraphNodeImpl.h's
// notifications, list 0x4512dd13); a node that hears that a node it was connected to (or disconnected from) is a
// composition node runs the tree code's propagation on it (OnGraphConnChanged).
#include "sg/cObjectGraphNodeImpl.h"

#include <cstdint>

namespace nGZSceneGraph {

class cCompositionTreeNodeImpl : public cObjectGraphNodeImpl {
public:
    static constexpr uint32_t kVersion = 0xb; // what Write records

    cCompositionTreeNodeImpl() = default;
    ~cCompositionTreeNodeImpl(); // 0xa36bd0
    cCompositionTreeNodeImpl(const cCompositionTreeNodeImpl &) = delete;
    cCompositionTreeNodeImpl &operator=(const cCompositionTreeNodeImpl &) = delete;

    // The constructor and destructor as hookable members (0xa36920, 0xa36bd0; thiscall, no arguments).
    cCompositionTreeNodeImpl *Construct();
    void Destruct();

    void AddChild(cICompositionTreeNode *outer, cICompositionTreeNode *child);    // 0xa37d00
    void RemoveChild(cICompositionTreeNode *outer, cICompositionTreeNode *child); // 0xa38240
    void SetFlag(uint32_t bit, bool value);                                       // 0xa38320
    bool IsKindOf(uint32_t iid);                                                  // 0xa38340
    // 0xa38160: the node's slot 3 after the behaviour's; only cGraphConnChanged (event 0x7b54c99c) matters here.
    void OnEvent(cICompositionTreeNode *outer, uint32_t type, uint32_t arg1, void *data, uint32_t sender);
    // 0xa37be0: a composition node connected to (or about to be disconnected from) a node: the tree code's
    // propagation runs on it (NodeConnected / NodeDisconnecting, sg/SGTreeCode.h).
    void OnGraphConnChanged(cICompositionTreeNode *outer, const cGraphConnChanged *body);
    void Read(cICompositionTreeNode *outer, ifaces::cIGZPersistReader *reader);  // 0xa38180
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out);                       // 0xa38360 ("cCompositionTreeNode", 0xb)
    void Write(cICompositionTreeNode *outer, ifaces::cIGZPersistWriter *writer); // 0xa38430

    cICompositionTreeNode *mParent = nullptr;     // +8 raw
    cICompositionTreeNode *mPrevSibling = nullptr; // +0xc raw
    cICompositionTreeNode *mNextSibling = nullptr; // +0x10 counted
    cICompositionTreeNode *mFirstChild = nullptr;  // +0x14 counted
    uint32_t mFlags = 0;                           // +0x18 (bit 12: read from a stream)
};
static_assert(sizeof(cCompositionTreeNodeImpl) == 0x1c, "cCompositionTreeNodeImpl size");

} // namespace nGZSceneGraph
