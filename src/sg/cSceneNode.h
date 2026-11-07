#pragma once
// nGZSceneGraph::cSceneNode (lane OK, scenegraph N1 step 3): class 0x25232b11 of the scene graph's COM director, the root
// partition of a scene and a scene-graph resource. Standalone shape:
//
//   class cSceneNode : public cPartitionNodeT<cISceneNode, cPartitionNodeImpl, Traits>, public cSGResource
//
//   +0x00 the node (vtable 0xe6d0b4, 98 slots: 88-96 cISceneNode's, 97 the deleting destructor), +0x04 its count
//   +0x08 the persist face (vtable 0xe6d240, 7 slots), +0x0c the partition Impl (0x60 bytes)
//   +0x6c the cSGResource part (vtable 0xe6d260, 13 slots; vbptr +0x88, name +0x8c, segment +0x90, flags +0x94 / +0x95)
//   +0x98 mInQuery       set around the scene queries (90-93); bytes 0x99-0x9b padding
//   +0x9c mPartitions    sub-partitions, null where removed (std::vector, 3 words)
//   +0xa8 mFreePartitions freed indices of mPartitions
//   +0xb4 mNodes         content nodes, null where removed
//   +0xc0 mFreeNodes     freed indices of mNodes
//   +0xcc mPendingPartitions nodes the tree code queued (slot 95) with flag 6 set (partitions)
//   +0xd8 mPendingNodes  the other queued nodes
//   +0xe4 the vtordisp, +0xe8 the virtual base cISGResource (vtable 0xe6d298, 25 slots)
//
// No spatial index: a query asks every sub-partition (those that meet the shape or line first) and hands every content
// node to the visitor. Each held node's holder words (slot 67) are this node and its index. The tree code (NodeBoundFlagChanged,
// sg/SGTreeCode.h) queues a node whose bound changed on the root of its tree (slot 95); the scene queries place the queued nodes
// again (94) before they run.
//
// The scene graph system (service 0xe5074b6b, interface 0x894978cd: cSceneGraphSystem's +0 face) keeps the set of scene
// nodes: the constructor adds the node (its slot 13), the destructor removes it (slot 14).
//
// The switch: the factory 0xa4e9b0 is hooked to Create, so every scene node the director makes is ours. The game's
// constructor 0xa4ddf0 has that factory as its one caller, so once the factory is ours the game's constructor, destructor,
// vtables and the bodies only they hold never run (in effect through callers).
//
// The tree code's world-transform refresh (0xa39b70) is ours (sg/SGWorldTransformRefresh.h).
#include "game/owned.h"
#include "sg/SGNodeTemplates.h"
#include "sg/cPartitionNodeImpl.h"
#include "sg/cSGResource.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

// The behaviour cells of cAbstractBehaviorTypeT<cISceneNode, cRefSingleThreaded>; the registry is the transform family's.
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f61e8); // the cell below (game/owned.h)
struct cSceneNodeBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f61e8; // 0xa4e740's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f61e8; }
    static nGZEvent::cEventAttributeMap *Registry() { return TransformNodeEventRegistry(); }
};

class cSceneNode : public cPartitionNodeT<cISceneNode, cPartitionNodeImpl, cSceneNodeBehaviorTraits>, public cSGResource {
    using Node = cPartitionNodeT<cISceneNode, cPartitionNodeImpl, cSceneNodeBehaviorTraits>;

public:
    static constexpr size_t kSize = 0xec;
    static constexpr uint32_t kClassId = 0x25232b11; // the director's class id, and the type-info record's word
    static constexpr uint32_t kVersion = 4;          // the type-info record's version
    static constexpr uint32_t kSystemService = 0xe5074b6b; // the scene graph system
    static constexpr uint32_t kSystemIid = 0x894978cd;     // its cISceneGraphSystemPrivate face (tentative name)
    static constexpr uint32_t kViewerFilterOffset = 0x20;  // the viewer's cIVisibilityQueryFilter
    static constexpr uint32_t kDrawnFlags = 0x420;         // node flags a culled node needs set ...
    static constexpr uint32_t kHiddenFlags = 0x4000;       // ... and clear
    static constexpr float kCompactFactor = 6.6666665f;    // 0xe6d310: compact when free * this > size (free > 15%)

    // 0xa4ddf0: the partition node, the cSGResource part, empty lists; flags 7 set and 12 clear, limit 0; the node added
    // to the scene graph system.
    cSceneNode();
    // 0xa4e0a0: Clear (77), each queued node through the tree code's NodeDisconnecting until the queues are empty, the node removed
    // from the system (when there is one); then the lists, the cSGResource part and the node.
    ~cSceneNode() override;

    // 0xa4e9b0 (cdecl; the director's factory, hooked): a new node, uncounted.
    static cSceneNode *__cdecl Create();
    // 0xa4ea10 (cdecl, hooked): the class id cSceneGraphSystem's Init registers the factory under.
    static uint32_t __cdecl GetClassId();

    // ---- cIGZUnknown (every face) ----
    // 0xa4ef70 (ret 8): a null out gives false; cISceneNode is this node, counted; then the partition node's answers
    // (shared 0xa4ef20); then the cSGResource part's (0xa3c4d0).
    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override;  // shared 0x3aecf0: the node's count
    uint32_t Release() override; // 0xa4f180 -> 0xa4f110: the node's count; the last one deletes

    // ---- cIObjectGraphNode / cICompositionTreeNode ----
    void Clone(uint32_t, uint32_t, void **, cINodeCloneContext *) override {} // 15: shared 0x16a930, nothing
    bool IsKindOf(uint32_t iid) override;                                       // 25: 0xa4f6e0
    uint32_t Accept(cICompositionTreeTraversal *traversal) override;           // 30: 0xa4f710, the traversal's slot 14
    uint32_t AcceptReadOnly(cICompositionTreeTraversal *traversal) override;   // 50: 0xa4f710
    // 31 / 32 (shared 0x1413e0, `return this`): the scene of a node is the first ancestor whose slot 32 answers, so the
    // scene node ends the climb (the tree code's NodeBoundFlagChanged queues a node on it).
    uint32_t GetFromParent31() override;
    uint32_t GetFromParent32() override;
    // 43 / 44 / 51 (shared 0xa37380 / 0xa37360 / 0xa37260): the Impl's iterator builders (ours).
    void *MakeIterator43(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator44(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator51(void *out, uint32_t a) override;

    // ---- cIBoundedNode: a scene is everywhere, and no partition holds it ----
    void SetHolder(void *, uint32_t) override {}                               // 67: shared 0x1b9a50, nothing
    bool IsEverywhere() override { return true; }                                    // 70: shared 0x144910
    cBoundBox GetBoundBoxIn(const cTransform34 *frame) override;               // 71: 0xa4eaf0, the largest box
    cBoundBox GetBoundBox() override;                                           // 72: 0xa4eb20
    bool IntersectsBound(uint32_t, uint32_t) override { return true; }                  // 73: shared 0x1477b0
    bool IntersectsLinear(nGZCompGeom::cILinear3 *) override { return true; }  // 74: shared 0x148260
    bool FirstHitLinear(float *t, nGZCompGeom::cILinear3 *linear) override;    // 75: 0xa4eae0, a hit at 0

    // ---- cIPartitionNode ----
    void Clear() override;                                                      // 77: 0xa4e7e0
    void RemovePartition(cIPartitionNode *partition) override;                  // 78: 0xa4f280
    // 79: 0xa4f1a0. The original returns what the node's SetHolder left in eax (no value of its own); every caller in
    // the image ignores it. Ours says true.
    bool RemoveNode(cIBoundedNode *node) override;
    bool AddPartition(cIPartitionNode *partition) override;                     // 81: 0xa4ec10
    bool UpdateNode(cIBoundedNode *node) override;                              // 82: 0xa4eda0
    bool InsertNode(cIBoundedNode *node) override;                              // 83: 0xa4eb50
    uint32_t QueryShape(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cIBound *shape, uint32_t arg) override; // 84: 0xa4f470
    uint32_t QueryLinear(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cILinear3 *linear) override;           // 85: 0xa4f510
    void RayCast(float *t, void *context, cINearerIntersectionQueryStrategy *visitor,
                 nGZCompGeom::cILinear3 *linear) override;                      // 86: 0xa4f5a0
    void Cull(void *viewer) override;                                           // 87: 0xa4f630

    // ---- cISceneNode ----
    cISGResource *GetResourceConst() override; // 88: 0xa4e540, the virtual base
    cISGResource *GetResource() override;      // 89: 0xa4e540
    uint32_t SceneQueryShape(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cIBound *shape, uint32_t arg) override; // 90: 0xa4ee80
    uint32_t SceneQueryLinear(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cILinear3 *linear) override;           // 91: 0xa4eed0
    uint32_t SceneRayCast(float *t, cINearerIntersectionQueryStrategy *visitor, nGZCompGeom::cILinear3 *linear) override; // 92: 0xa4efd0
    void SceneCull(void *viewer) override;      // 93: 0xa4f040
    void FlushPending() override;               // 94: 0xa4f800
    void QueueNode(cIBoundedNode *node) override;   // 95: 0xa4e4b0
    void UnqueueNode(cIBoundedNode *node) override; // 96: 0xa4f200

    // ---- cIGZPersistSerializableReferent (+8, and the virtual base's 3-6) ----
    // 0xa4f0a0: the record (read and dropped: no version check), the cSGResource part (0xa3c540), the partition part
    // (0xa4c3b0: its record, then the bounded part).
    void Read(ifaces::cIGZPersistReader *reader) override;
    cPersistTypeInfo GetTypeInfo() override; // 0xa4f750: {"cSceneNode", 0x25232b11, 4}
    // 0xa4fc00: the record, the cSGResource part (0xa3ced0), the partition part (0xa4fbd0 -> 0xadff10).
    void Write(ifaces::cIGZPersistWriter *writer) override;
    uint32_t GetOwnerInfo() override; // 0xa4f2f0: the cSGResource part's (0xa3c750)

    // ---- cIGZPersistResource (+0x6c) ----
    uint32_t GetRefCount() override; // 0xa4ea20 -> 0x3a56a0: the node's count

    // ---- cISGResource (the virtual base) ----
    const void *GetRecord18() override; // 18 (0xa4eac6 -> 0xa4ead0): "_scen"
    const void *GetRecord19() override; // 19 (0xa4eaad -> 0xa4eac0): "5sc"

    uint8_t mInQuery = 0;                               // +0x98
    std::vector<cIPartitionNode *> mPartitions;         // +0x9c
    std::vector<uint32_t> mFreePartitions;              // +0xa8
    std::vector<cIBoundedNode *> mNodes;                // +0xb4
    std::vector<uint32_t> mFreeNodes;                   // +0xc0
    std::vector<cIBoundedNode *> mPendingPartitions;    // +0xcc
    std::vector<cIBoundedNode *> mPendingNodes;         // +0xd8

private:
    // A node placed in mNodes: at the last freed index, else at the end; its holder words set to this node and the index.
    void PlaceNode(cIBoundedNode *node);
    // The scene queries' common start: the queues flushed when either holds a node.
    void FlushIfPending();
    // 94's compaction of one list (sub-partitions or content nodes): when free * 6.67 > size, the free indices dropped,
    // the null entries erased (from the back) and every entry from the first erased index on told its new index.
    template <class T> void Compact(std::vector<T *> &list, std::vector<uint32_t> &free);
};

} // namespace nGZSceneGraph
