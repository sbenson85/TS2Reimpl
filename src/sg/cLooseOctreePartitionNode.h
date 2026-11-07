#pragma once
// nGZSceneGraph::cLooseOctreePartitionNode (lane OI, scenegraph N1 step 2): the scene graph's spatial index, a partition
// node (sg/SGNodeTemplates.h cPartitionNodeT over cPartitionNodeImpl) that keeps its content nodes in a loose octree
// of a given size (slot 88) and its sub-partitions in a list. 0xec bytes like the game's (vtables 0xe6e1b4, 90 slots,
// and 0xe6e320 at +8).
//
// The octree: the root cell's edge is the size, centred on the node's origin; a cell at depth d (0-8) has edge
// size / 2^d and holds the nodes whose bounding sphere (the centre and half the diagonal of their box in the node's
// frame) fits in its loose box. A node goes into the deepest cell (children made on the way, from a pool) whose loose
// box holds it; empty leaf cells are pruned when a node leaves. Every content node is in one doubly linked list
// (mHead .. mTail, each cell's nodes a run starting at its mFirst), and its holder words (slot 67) are this node and its
// list item. Sub-partitions get this node and their index; freed indices are reused.
//
// The switch: the factory 0xa51ab0 is hooked to Create, so every octree the game makes is ours. The game's constructor
// 0xa50ef0 (called by the factory only), its vtables and every body only they hold are then never reached.
//
// Still game code (original calls): nGZCompGeom's rigid-transform, box, cuboid shape, slab and frustum code (the .cpp's
// compgeom wrappers). The tree code's world-transform refresh is ours (sg/SGWorldTransformRefresh.h).
#include "compgeom/CompGeom.h"
#include "misc/UtilitiesC.h" // AffineTransform3
#include "sg/SGNodeTemplates.h"
#include "sg/cPartitionNodeImpl.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

class cLooseOctreePartitionNode
    : public cPartitionNodeT<cILooseOctreePartitionNode, cPartitionNodeImpl, cLooseOctreePartitionNodeBehaviorTraits> {
    using Base = cPartitionNodeT<cILooseOctreePartitionNode, cPartitionNodeImpl, cLooseOctreePartitionNodeBehaviorTraits>;

public:
    static constexpr size_t kSize = 0xec;
    static constexpr uint32_t kTypeWord4 = 0xebfda967; // the type record's second word
    static constexpr uint32_t kVersion = 1;
    static constexpr int kMaxDepth = 8;                   // cells below depth 8 are never made
    static constexpr uint32_t kCellPool = 0x10f620c;      // cRZFastFixedAllocator2<0x3c> (0xa51280 / 0xa512e0)
    static constexpr uint32_t kItemPool = 0x10f6218;      // cRZFastFixedAllocator2<0x10> (0xa512b0 / 0xa51360)
    static constexpr uint32_t kViewerFilterOffset = 0x20; // the viewer's cIVisibilityQueryFilter
    static constexpr uint32_t kViewerViewOffset = 0x184;  // the viewer's view (0x1a5eb0's third operand)
    static constexpr uint32_t kAllPlanes = 0x3f;           // a plane mask: inside all six planes, no test needed

    struct Cell;
    // A content node's place in the list (from the 0x10 pool): the node, its cell and the links.
    struct Item {
        cIBoundedNode *mNode; // +0
        Cell *mCell;          // +4
        Item *mNext;          // +8 (null only for mTail)
        Item *mPrev;          // +0xc
    };
    // A cell (from the 0x3c pool; 0xa50e80 fills every word but the padding).
    struct Cell {
        Cell *mParent;       // +0
        Cell *mChildren[8];  // +4, index x | y << 1 | z << 2 (1 = the positive side)
        Item *mFirst;        // +0x24 the cell's first item, or null
        uint32_t mCount;     // +0x28 items in the cell
        int16_t mDepth;      // +0x2c
        uint16_t mPadding;   // +0x2e never written
        float mCenter[3];    // +0x30

        void DeleteChild(Cell *child); // 0xa51730 (thiscall on the parent): the child's slot cleared, the child freed
    };

    cLooseOctreePartitionNode(); // 0xa50ef0
    ~cLooseOctreePartitionNode() override; // 0xa51170

    // 0xa51ab0 (cdecl; the director's factory, hooked): a new node, uncounted.
    static cLooseOctreePartitionNode *__cdecl Create();
    // 0xa51b10 (cdecl): the class id the director registers the factory under (its one caller is the director's
    // constructor 0xa33bf0, ours, which has the id in its table: in effect through callers).
    static uint32_t __cdecl GetClassId();

    bool QueryInterface(uint32_t iid, void **out) override;                              // 0xa52670
    void Clone(uint32_t, uint32_t, void **, cINodeCloneContext *) override {}             // 15: shared 0x16a930, nothing
    bool IsKindOf(uint32_t iid) override;                                                 // 25: 0xa535d0
    void *MakeIterator43(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator44(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator51(void *out, uint32_t a) override;
    void OnContentChanged(uint32_t transform, uint32_t b, uint32_t c) override;           // 60: 0xa536d0
    cBoundBox GetBoundBoxIn(const cTransform34 *frame) override;                          // 71: 0xa51fc0
    cBoundBox GetBoundBox() override;                                                     // 72: 0xa52080
    bool IntersectsBound(uint32_t shape, uint32_t arg) override;                                   // 73: 0xa51bc0
    bool IntersectsLinear(nGZCompGeom::cILinear3 *linear) override;                       // 74: 0xa51c40
    bool FirstHitLinear(float *t, nGZCompGeom::cILinear3 *linear) override;               // 75: 0xa51f10
    void Clear() override;                                                                // 77: 0xa51790
    void RemovePartition(cIPartitionNode *partition) override;                            // 78: 0xa52840
    bool RemoveNode(cIBoundedNode *node) override;                                        // 79: 0xa527a0 (via 0xa52790)
    bool AddPartition(cIPartitionNode *partition) override;                               // 81: 0xa522a0
    bool UpdateNode(cIBoundedNode *node) override;                                        // 82: 0xa524b0
    bool InsertNode(cIBoundedNode *node) override;                                        // 83: 0xa520e0
    uint32_t QueryShape(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cIBound *shape, uint32_t arg) override; // 84: 0xa52a80
    uint32_t QueryLinear(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cILinear3 *linear) override;           // 85: 0xa52b80
    void RayCast(float *t, void *context, cINearerIntersectionQueryStrategy *visitor,
                 nGZCompGeom::cILinear3 *linear) override;                                // 86: 0xa52f30
    void Cull(void *viewer) override;                                                     // 87: 0xa53500
    void SetSize(float size) override;                                                    // 88: 0xa52a10

    // ---- cIGZPersistSerializableReferent (+8): the octree's type record around the partition's ----
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xa526b0
    cPersistTypeInfo GetTypeInfo() override;                // 0xa535f0 ("cLooseOctreePartitionNode", 1)
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa53790

    // The line walk's record (0xa52f30 builds it, 0xa53220 reads it).
    struct RayRecord {
        float mSize2;                                // +0 twice the size
        nGZCompGeom::Vec3 mOrigin;                   // +4 the line's origin in the node's frame
        nGZCompGeom::Vec3 mDirection;                // +0x10
        float mMinT;                                 // +0x1c cells whose box ends before it are skipped
        cINearerIntersectionQueryStrategy *mVisitor; // +0x20
        nGZCompGeom::cILinear3 *mLinear;             // +0x24
    };

private:
    std::vector<cIPartitionNode *>::pointer PartitionsEnd() { return mPartitions.data() + mPartitions.size(); }
    bool FitsBox(cIBoundedNode *node, cBoundBox *box);                                        // the box test of 81-83
    void InsertInto(Cell *cell, cIBoundedNode *node, const float *center, float radius);      // 0xa51cc0
    static Cell *NewCell(Cell *parent, const float *center, int16_t depth);                   // 0xa51280 + 0xa50e80
    static void FreeCell(Cell *cell);                                                         // 0xa512e0
    static void FreeItem(Item *item);                                                         // 0xa51360
    static bool HasChildren(const Cell *cell);                                                // 0xa51ba0
    static uint32_t QueryCells(Cell *cell, float size2, const AffineTransform3 *frame, cIIntersectionQueryStrategy *visitor,
                               nGZCompGeom::cIBound *shape, uint32_t arg);                    // 0xa52c70
    static uint32_t QueryCellsLinear(Cell *cell, float size2, const AffineTransform3 *frame,
                                     cIIntersectionQueryStrategy *visitor, nGZCompGeom::cILinear3 *linear); // 0xa52dd0
    static void RayCells(Cell *cell, const RayRecord *record, float *t, void *context);       // 0xa53220
    static void CullCells(Cell *cell, float size2, void *viewer, const void *frustum, uint32_t planeMask); // 0xa51960

public:
    AffineTransform3 mFrame;                     // +0x6c world -> node frame (the inverse of the world transform)
    float mHalf[3];                              // +0xa0 half the size, three times (unset until slot 88)
    std::vector<uint32_t> mFreeSlots;            // +0xac freed indices of mPartitions
    std::vector<cIPartitionNode *> mPartitions;  // +0xb8 sub-partitions (null where removed)
    Cell *mRoot;                                 // +0xc4
    float mSize;                                 // +0xc8
    Item mHead;                                  // +0xcc the list's sentinels
    Item mTail;                                  // +0xdc
};

} // namespace nGZSceneGraph
