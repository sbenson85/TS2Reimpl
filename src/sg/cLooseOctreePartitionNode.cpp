// nGZSceneGraph::cLooseOctreePartitionNode (lane OI). See the header.
#include "sg/cLooseOctreePartitionNode.h"

#include "game/game.h"
#include "game/original.h"
#include "compgeom/Matrix4.h"
#include "compgeom/Rigid.h"
#include "compgeom/ConvexShapes.h"
#include "compgeom/Frustum.h"
#include "compgeom/SeparatingAxes.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"
#include "sg/cTransformNode.h" // node_family

#include <cmath>
#include <cstring>
#include <limits>

namespace nGZSceneGraph {

static_assert(sizeof(cLooseOctreePartitionNode) == cLooseOctreePartitionNode::kSize, "cLooseOctreePartitionNode size");
static_assert(offsetof(cLooseOctreePartitionNode, mFrame) == 0x6c, "mFrame");
static_assert(offsetof(cLooseOctreePartitionNode, mHalf) == 0xa0, "mHalf");
static_assert(offsetof(cLooseOctreePartitionNode, mFreeSlots) == 0xac, "mFreeSlots");
static_assert(offsetof(cLooseOctreePartitionNode, mPartitions) == 0xb8, "mPartitions");
static_assert(offsetof(cLooseOctreePartitionNode, mRoot) == 0xc4, "mRoot");
static_assert(offsetof(cLooseOctreePartitionNode, mSize) == 0xc8, "mSize");
static_assert(offsetof(cLooseOctreePartitionNode, mHead) == 0xcc, "mHead");
static_assert(offsetof(cLooseOctreePartitionNode, mTail) == 0xdc, "mTail");
static_assert(sizeof(cLooseOctreePartitionNode::Item) == 0x10, "Item size");
static_assert(sizeof(cLooseOctreePartitionNode::Cell) == 0x3c, "Cell size");
static_assert(offsetof(cLooseOctreePartitionNode::Cell, mFirst) == 0x24, "Cell::mFirst");
static_assert(offsetof(cLooseOctreePartitionNode::Cell, mDepth) == 0x2c, "Cell::mDepth");
static_assert(offsetof(cLooseOctreePartitionNode::Cell, mCenter) == 0x30, "Cell::mCenter");
static_assert(sizeof(cLooseOctreePartitionNode::RayRecord) == 0x28, "RayRecord size");

namespace {

using nGZCompGeom::Vec3;

// ---- nGZCompGeom: ours where marked, else still game code (thiscall on the first operand unless noted) ----
namespace compgeom {

// 0x19cd80: a rigid transform inverted in place; returns it. Ours.
cRigidTransform *RigidInvert(cRigidTransform *self)
{
    return reinterpret_cast<cRigidTransform *>(reinterpret_cast<nGZCompGeom::RigidTransform3 *>(self)->Invert());
}
// 0x19c900 / 0x19c6d0 / 0x19c390: a point through a rigid transform, a point back through it, a vector back through it
// (each in place); ours (src/compgeom/Rigid.h).
const nGZCompGeom::RigidTransform3 &AsRigid(const cRigidTransform *rigid)
{
    return *reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(rigid);
}
void RigidPoint(cRigidTransform *rigid, Vec3 *point)
{
    AsRigid(rigid).TransformPoint(point);
}
void RigidInversePoint(cRigidTransform *rigid, Vec3 *point)
{
    AsRigid(rigid).InverseTransformPoint(point);
}
void RigidInverseVector(cRigidTransform *rigid, Vec3 *vector)
{
    AsRigid(rigid).InverseTransformVector(vector);
}
// 0x19c1c0: the 4x4 matrix (16 floats, the translation in the last column) of a rigid transform, into out; returns out.
float *RigidToMatrix(cRigidTransform *rigid, float *out)
{
    return AsRigid(rigid).ToMatrix4(out); // ours
}
// 0x19d2f0: `inner` lies in `outer` (both boxes as two corners; no coordinate of inner beyond outer's).
bool BoxContains(const cBoundBox *outer, const cBoundBox *inner)
{
    return reinterpret_cast<const nGZCompGeom::BoundBox *>(outer)->Contains(*reinterpret_cast<const nGZCompGeom::BoundBox *>(inner)); // 0x19d2f0 (ours)
}
// 0x18ff90 / 0x190170: the box of a cuboid (half extents) placed by a frame / by a rigid transform; ours.
void CuboidBoxIn(const float *half, cBoundBox *out, const AffineTransform3 *frame)
{
    *reinterpret_cast<nGZCompGeom::BoundBox *>(out) = reinterpret_cast<const nGZCompGeom::Cuboid *>(half)->BoxInFrame(*frame);
}
void CuboidBox(const float *half, cBoundBox *out, const cRigidTransform *rigid)
{
    *reinterpret_cast<nGZCompGeom::BoundBox *>(out) = reinterpret_cast<const nGZCompGeom::Cuboid *>(half)->BoxInRigid(AsRigid(rigid));
}
// 0x19f800 (cdecl): where a line (origin, direction) enters and leaves a box (half extents, centre); 0 when it misses.
int32_t LineBoxSpan(float *enter, float *leave, const Vec3 *box, const Vec3 *line)
{
    // ours: box is a tree node's half extents then centre, line an origin then a direction
    return nGZCompGeom::TreeBoxLineSpan(enter, leave, reinterpret_cast<const nGZCompGeom::AABBTreeNode *>(box),
                                        reinterpret_cast<const nGZCompGeom::Line3 *>(line));
}
// 0x1a5eb0 (cdecl): the product of two 4x4 matrices (out = view * matrix: the viewer's view (+0x184) after the node's).
void ViewTransform(float *out, const float *matrix, const void *view)
{
    nGZCompGeom::Matrix4Multiply(out, matrix, static_cast<const float *>(view)); // ours
}
// 0x1975a0: a frustum (0x60 bytes: its planes) from a view transform.
void FrustumFromView(void *frustum, const void *view)
{
    static_cast<nGZCompGeom::Frustum *>(frustum)->FromView(static_cast<const float *>(view)); // 0x1975a0 (ours)
}
// 0x197520: where a box (half extents, centre) lies against the frustum: > 0 outside; *planeMask gains the planes the
// box is inside of.
int32_t FrustumClassify(const void *frustum, uint32_t *planeMask, const Vec3 *half, const Vec3 *center)
{
    // 0x197520 (ours): it reads and writes the mask's low byte only.
    return static_cast<const nGZCompGeom::Frustum *>(frustum)->ClassifyBoxAt(reinterpret_cast<uint8_t *>(planeMask), half, center);
}

// nGZCompGeom::cTranslatedCuboidShape (0x20 bytes) on the stack, as the queries build one per cell: ours, constructor
// 0x18fc10 (extents, centre), destructor 0x18fce0, and two of its members called directly: 0x191500
// (cConvexT<cITranslatedCuboidShape>::vf20: meets `shape` in `frame`) and 0x190c40 (meets the line in `frame`).
class TranslatedCuboidShape {
public:
    TranslatedCuboidShape(const Vec3 *half, const Vec3 *center) : mShape(half, center) {}
    TranslatedCuboidShape(const TranslatedCuboidShape &) = delete;
    TranslatedCuboidShape &operator=(const TranslatedCuboidShape &) = delete;

    bool MeetsShapeIn(const AffineTransform3 *frame, nGZCompGeom::cIBound *shape, uint32_t arg)
    {
        return mShape.nGZCompGeom::cTranslatedCuboidBound::DoIntersectsBoundInAt(
            frame, shape, reinterpret_cast<const AffineTransform3 *>(static_cast<uintptr_t>(arg)));
    }
    bool MeetsLinearIn(const AffineTransform3 *frame, nGZCompGeom::cILinear3 *linear)
    {
        return mShape.nGZCompGeom::cConvexT<nGZCompGeom::cTranslatedCuboidBound, nGZCompGeom::cITranslatedCuboidBound,
                                            0x3ce553fa>::IntersectsLinearIn(frame, linear); // 0x190c40
    }

private:
    nGZCompGeom::cTranslatedCuboidShape mShape;
};

} // namespace compgeom

constexpr uint32_t kLineClassId = 0x3ce57cc0;    // nGZCompGeom::cLine3
constexpr uint32_t kSegmentClassId = 0x3ce57f31; // nGZCompGeom::cSegment3
constexpr uint32_t kDrawnFlags = 0x420;          // node flags a culled node needs set ...
constexpr uint32_t kHiddenFlags = 0x4000;        // ... and clear

using Cell = cLooseOctreePartitionNode::Cell;
using Item = cLooseOctreePartitionNode::Item;

// 2^(depth + 1) as the cells' extents divide by it (`2 << depth`, the shift count the depth's low byte).
float CellDivisor(const Cell *cell)
{
    return static_cast<float>(static_cast<int32_t>(2u << (static_cast<uint8_t>(cell->mDepth) & 31u)));
}

uint32_t SignBit(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits >> 31;
}

const nGZCompGeom::Cuboid *AsCuboid(const float *half) { return reinterpret_cast<const nGZCompGeom::Cuboid *>(half); }

uint32_t Word(const void *pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

// The centre of a box and half its diagonal (the radius InsertInto places a node by).
void BoundingSphere(const cBoundBox &box, float *center, float *radius)
{
    for (int i = 0; i < 3; ++i) center[i] = (box.mMax[i] - box.mMin[i]) * 0.5f + box.mMin[i];
    const float d0 = box.mMax[0] - box.mMin[0];
    const float d1 = box.mMax[1] - box.mMin[1];
    const float d2 = box.mMax[2] - box.mMin[2];
    const float squared = (d0 * d0 + d1 * d1) + d2 * d2;
    *radius = static_cast<float>(std::sqrt(static_cast<double>(squared))) * 0.5f;
}

} // namespace

// ---- construction ----

cLooseOctreePartitionNode::cLooseOctreePartitionNode() : mRoot(nullptr), mSize(0.0f)
{
    mFrame.mUnknown0 = 0;
    for (int i = 0; i < 9; ++i) mFrame.mMatrix[i] = (i % 4 == 0) ? 1.0f : 0.0f;
    for (float &offset : mFrame.mOffset) offset = 0.0f;
    mHead = Item{nullptr, nullptr, nullptr, nullptr};
    mTail = Item{nullptr, nullptr, nullptr, nullptr};
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    mRoot = NewCell(nullptr, origin, 0);
    mTail.mPrev = &mHead;
    mHead.mNext = &mTail;
}

cLooseOctreePartitionNode::~cLooseOctreePartitionNode()
{
    cLooseOctreePartitionNode::Clear();
    if (mRoot) FreeCell(mRoot);
    mRoot = nullptr;
    mHead.mNext = nullptr;
    mTail.mPrev = nullptr;
}

// (The original's operator new throws on failure, so its null branch is dead.)
cLooseOctreePartitionNode *__cdecl cLooseOctreePartitionNode::Create() { return new cLooseOctreePartitionNode; }

// The class id is the type record's second word.
uint32_t __cdecl cLooseOctreePartitionNode::GetClassId() { return kTypeWord4; }

// ---- the pools and cells ----

cLooseOctreePartitionNode::Cell *cLooseOctreePartitionNode::NewCell(Cell *parent, const float *center, int16_t depth)
{
    Cell *const cell = static_cast<Cell *>(GameAddress<RZFixedAllocatorBase>(kCellPool)->Alloc());
    cell->mParent = parent;
    cell->mDepth = depth;
    cell->mFirst = nullptr;
    cell->mCount = 0;
    std::memcpy(cell->mCenter, center, sizeof cell->mCenter);
    for (Cell *&child : cell->mChildren) child = nullptr;
    return cell;
}

void cLooseOctreePartitionNode::FreeCell(Cell *cell)
{
    if (cell) GameAddress<RZFastFixed0x3c>(kCellPool)->Free(cell);
}

void cLooseOctreePartitionNode::FreeItem(Item *item)
{
    if (item) GameAddress<RZFastFixed0x10>(kItemPool)->Free(item);
}

bool cLooseOctreePartitionNode::HasChildren(const Cell *cell)
{
    for (const Cell *child : cell->mChildren) {
        if (child) return true;
    }
    return false;
}

void cLooseOctreePartitionNode::Cell::DeleteChild(Cell *child)
{
    for (Cell *&slot : mChildren) {
        if (slot == child) {
            if (slot) FreeCell(slot);
            slot = nullptr;
            return;
        }
    }
}

// ---- cIGZUnknown, cIObjectGraphNode, cICompositionTreeNode ----

bool cLooseOctreePartitionNode::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cILooseOctreePartitionNode::kIid) {
        *out = static_cast<cILooseOctreePartitionNode *>(this);
        AddRef();
        return true;
    }
    return Base::QueryInterface(iid, out);
}

bool cLooseOctreePartitionNode::IsKindOf(uint32_t iid)
{
    return iid == cILooseOctreePartitionNode::kIid || Base::IsKindOf(iid);
}

void *cLooseOctreePartitionNode::MakeIterator43(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator43(&mImpl, out, this, a, b);
}
void *cLooseOctreePartitionNode::MakeIterator44(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator44(&mImpl, out, this, a, b);
}
void *cLooseOctreePartitionNode::MakeIterator51(void *out, uint32_t a)
{
    return node_family::BuildIterator51(&mImpl, out, a);
}

// The node frame is the inverse of the new world transform; then the bounded node's slot (the cache).
void cLooseOctreePartitionNode::OnContentChanged(uint32_t transform, uint32_t b, uint32_t c)
{
    cRigidTransform inverse = *reinterpret_cast<const cRigidTransform *>(static_cast<uintptr_t>(transform));
    const cRigidTransform rigid = *compgeom::RigidInvert(&inverse);
    AffineTransform3 frame;
    frame.mUnknown0 = 1;
    std::memcpy(frame.mOffset, rigid.Translation(), sizeof frame.mOffset);
    game_code::SetFrameRotation(&frame, rigid.Rotation());
    mFrame = frame;
    Base::OnContentChanged(transform, b, c);
}

// ---- cIBoundedNode: the octree's own bound, a cuboid of half extents mHalf on the cached world transform ----

cBoundBox cLooseOctreePartitionNode::GetBoundBoxIn(const cTransform34 *frame)
{
    cBoundBox out;
    mImpl.RefreshIfStale(this);
    AffineTransform3 mine;
    mImpl.CachedFrame(&mine);
    const AffineTransform3 composed =
        *game_code::ComposeFrames(&mine, reinterpret_cast<const AffineTransform3 *>(frame));
    compgeom::CuboidBoxIn(mHalf, &out, &composed);
    return out;
}

cBoundBox cLooseOctreePartitionNode::GetBoundBox()
{
    cBoundBox out;
    mImpl.RefreshIfStale(this);
    const cRigidTransform world = mImpl.mWorld;
    compgeom::CuboidBox(mHalf, &out, &world);
    return out;
}

// 73: the shape (a cIBound, the first word) against the cuboid: the shape's slot 19 (arg, cuboid, frame).
bool cLooseOctreePartitionNode::IntersectsBound(uint32_t shape, uint32_t arg)
{
    mImpl.RefreshIfStale(this);
    AffineTransform3 frame;
    mImpl.CachedFrame(&frame);
    auto *const bound = reinterpret_cast<nGZCompGeom::cIBound *>(static_cast<uintptr_t>(shape));
    return static_cast<uint8_t>(bound->Slot19(arg, Word(mHalf), Word(&frame))) != 0;
}

bool cLooseOctreePartitionNode::IntersectsLinear(nGZCompGeom::cILinear3 *linear)
{
    mImpl.RefreshIfStale(this);
    AffineTransform3 frame;
    mImpl.CachedFrame(&frame);
    return linear->IntersectsCuboidIn(AsCuboid(mHalf), &frame);
}

bool cLooseOctreePartitionNode::FirstHitLinear(float *t, nGZCompGeom::cILinear3 *linear)
{
    mImpl.RefreshIfStale(this);
    AffineTransform3 frame;
    mImpl.CachedFrame(&frame);
    float hit;
    if (linear->FirstHitCuboidIn(&hit, AsCuboid(mHalf), &frame) && *t > hit) {
        *t = hit;
        return true;
    }
    return false;
}

// ---- cIPartitionNode ----

void cLooseOctreePartitionNode::Clear()
{
    for (cIPartitionNode **p = mPartitions.data(); p != PartitionsEnd(); ++p) {
        cIPartitionNode *const partition = *p;
        if (partition) {
            RemovePartition(partition);
            NodeBoundFlagChanged(partition);
        }
    }
    while (mHead.mNext != &mTail) {
        NodeBoundFlagChanged(mHead.mNext->mNode);
        RemoveNode(mHead.mNext->mNode);
    }
}

void cLooseOctreePartitionNode::RemovePartition(cIPartitionNode *partition)
{
    const uint32_t index = partition->GetHolderData();
    mFreeSlots.push_back(index);
    mPartitions[index] = nullptr;
    partition->SetHolder(nullptr, 0);
}

bool cLooseOctreePartitionNode::RemoveNode(cIBoundedNode *node)
{
    Item *const item = reinterpret_cast<Item *>(static_cast<uintptr_t>(node->GetHolderData()));
    Item *const prev = item->mPrev;
    Item *const next = item->mNext;
    Cell *cell = item->mCell;
    prev->mNext = next;
    next->mPrev = prev;
    if (--cell->mCount == 0) {
        cell->mFirst = nullptr;
    } else if (cell->mFirst == item) {
        cell->mFirst = next;
    }
    *item = Item{nullptr, nullptr, nullptr, nullptr};
    FreeItem(item);
    node->SetHolder(nullptr, 0);
    // Empty leaves go, up to the first cell that still holds something.
    for (Cell *parent = cell->mParent; parent; parent = cell->mParent) {
        if (cell->mCount != 0 || HasChildren(cell)) break;
        parent->DeleteChild(cell);
        cell = parent;
    }
    return true;
}

bool cLooseOctreePartitionNode::FitsBox(cIBoundedNode *node, cBoundBox *box)
{
    *box = node->GetBoundBoxIn(reinterpret_cast<const cTransform34 *>(&mFrame));
    cBoundBox octree;
    for (int i = 0; i < 3; ++i) {
        octree.mMin[i] = -mHalf[i];
        octree.mMax[i] = mHalf[i];
    }
    return compgeom::BoxContains(&octree, box);
}

bool cLooseOctreePartitionNode::AddPartition(cIPartitionNode *partition)
{
    cBoundBox box;
    if (!FitsBox(partition, &box)) return false;
    for (cIPartitionNode **p = mPartitions.data(); p != PartitionsEnd(); ++p) {
        if (*p && (*p)->AddPartition(partition)) return true;
    }
    // The new partition takes in the sub-partitions that fit in it.
    uint32_t index = 0;
    for (cIPartitionNode **p = mPartitions.data(); p != PartitionsEnd(); ++p, ++index) {
        if (!*p) continue;
        (*p)->SetHolder(nullptr, 0);
        if (partition->AddPartition(*p)) {
            *p = nullptr;
            mFreeSlots.push_back(index);
        } else {
            (*p)->SetHolder(this, index);
        }
    }
    if (mFreeSlots.empty()) {
        mPartitions.push_back(partition);
        partition->SetHolder(this, static_cast<uint32_t>(mPartitions.size() - 1));
    } else {
        mPartitions[mFreeSlots.back()] = partition;
        partition->SetHolder(this, mFreeSlots.back());
        mFreeSlots.pop_back();
    }
    for (Item *item = mHead.mNext; item->mNext; item = item->mNext) {
        NodeBoundFlagChanged(item->mNode);
    }
    return true;
}

bool cLooseOctreePartitionNode::UpdateNode(cIBoundedNode *node)
{
    cBoundBox box;
    const bool fits = FitsBox(node, &box);
    cLooseOctreePartitionNode::RemoveNode(node);
    if (!fits) return false;
    for (cIPartitionNode **p = mPartitions.data(); p != PartitionsEnd(); ++p) {
        if (*p && (*p)->InsertNode(node)) return true;
    }
    float center[3];
    float radius;
    BoundingSphere(box, center, &radius);
    InsertInto(mRoot, node, center, radius);
    return true;
}

bool cLooseOctreePartitionNode::InsertNode(cIBoundedNode *node)
{
    cBoundBox box;
    if (!FitsBox(node, &box)) return false;
    for (cIPartitionNode **p = mPartitions.data(); p != PartitionsEnd(); ++p) {
        if (*p && (*p)->InsertNode(node)) return true;
    }
    float center[3];
    float radius;
    BoundingSphere(box, center, &radius);
    InsertInto(mRoot, node, center, radius);
    return true;
}

// Down from `cell` while the child on the centre's side (made if missing) holds the sphere in its loose box (twice the
// child's edge), to depth 8 at most; the node's item goes in at the front of the cell's run.
void cLooseOctreePartitionNode::InsertInto(Cell *cell, cIBoundedNode *node, const float *center, float radius)
{
    while (static_cast<uint32_t>(cell->mDepth + 1) < static_cast<uint32_t>(kMaxDepth + 1)) {
        const uint32_t sx = SignBit(cell->mCenter[0] - center[0]);
        const uint32_t sy = SignBit(cell->mCenter[1] - center[1]);
        const uint32_t sz = SignBit(cell->mCenter[2] - center[2]);
        const float edge = mSize / CellDivisor(cell);
        const float half = edge * 0.5f;
        const float extent = (edge + edge) * 0.5f;
        float child[3];
        child[0] = static_cast<float>(static_cast<int32_t>(2 * sx - 1)) * half + cell->mCenter[0];
        child[1] = static_cast<float>(static_cast<int32_t>(2 * sy - 1)) * half + cell->mCenter[1];
        child[2] = static_cast<float>(static_cast<int32_t>(2 * sz - 1)) * half + cell->mCenter[2];
        bool fits = true;
        for (int i = 0; i < 3 && fits; ++i) {
            fits = (center[i] - radius) >= (child[i] - extent) && (extent + child[i]) >= (center[i] + radius);
        }
        if (!fits) break;
        const uint32_t index = (((sz << 1) | sy) << 1) | sx;
        Cell *next = cell->mChildren[index];
        if (!next) {
            next = NewCell(cell, child, static_cast<int16_t>(cell->mDepth + 1));
            cell->mChildren[index] = next;
        }
        cell = next;
    }
    Item *const item = static_cast<Item *>(GameAddress<RZFixedAllocatorBase>(kItemPool)->Alloc());
    *item = Item{nullptr, nullptr, nullptr, nullptr};
    item->mNode = node;
    item->mCell = cell;
    Item *const at = cell->mFirst ? cell->mFirst : &mHead;
    Item *const after = at->mNext;
    at->mNext = item;
    item->mPrev = at;
    item->mNext = after;
    after->mPrev = item;
    if (!cell->mFirst) cell->mFirst = item;
    ++cell->mCount;
    node->SetHolder(this, Word(item));
}

// ---- the queries ----

uint32_t cLooseOctreePartitionNode::QueryShape(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cIBound *shape,
                                               uint32_t arg)
{
    for (cIPartitionNode **p = mPartitions.data(); p != PartitionsEnd(); ++p) {
        if (*p && (*p)->IntersectsBound(Word(shape), arg) && (*p)->QueryShape(visitor, shape, arg) == 1) return 1;
    }
    mImpl.RefreshIfStale(this);
    AffineTransform3 frame;
    mImpl.CachedFrame(&frame);
    return QueryCells(mRoot, mSize + mSize, &frame, visitor, shape, arg);
}

// The cell's loose box as a cuboid shape with half extents twice its edge (size2 is twice the octree's size); its
// items when the shape meets it, then its children; 1 from the visitor ends the walk.
uint32_t cLooseOctreePartitionNode::QueryCells(Cell *cell, float size2, const AffineTransform3 *frame,
                                               cIIntersectionQueryStrategy *visitor, nGZCompGeom::cIBound *shape,
                                               uint32_t arg)
{
    {
        const float e = (size2 + size2) / CellDivisor(cell);
        const Vec3 half = {e, e, e};
        compgeom::TranslatedCuboidShape cuboid(&half, reinterpret_cast<const Vec3 *>(cell->mCenter));
        if (!cuboid.MeetsShapeIn(frame, shape, arg)) return 0;
        Item *item = cell->mFirst;
        for (uint32_t i = 0; i < cell->mCount; ++i) {
            if (item->mNode && visitor->VisitShape(item->mNode, shape, arg) == 1) return 1;
            item = item->mNext;
        }
    }
    for (Cell *child : cell->mChildren) {
        if (child && QueryCells(child, size2, frame, visitor, shape, arg) == 1) return 1;
    }
    return 0;
}

uint32_t cLooseOctreePartitionNode::QueryLinear(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cILinear3 *linear)
{
    for (cIPartitionNode **p = mPartitions.data(); p != PartitionsEnd(); ++p) {
        if (*p && (*p)->IntersectsLinear(linear) && (*p)->QueryLinear(visitor, linear) == 1) return 1;
    }
    mImpl.RefreshIfStale(this);
    AffineTransform3 frame;
    mImpl.CachedFrame(&frame);
    return QueryCellsLinear(mRoot, mSize + mSize, &frame, visitor, linear);
}

uint32_t cLooseOctreePartitionNode::QueryCellsLinear(Cell *cell, float size2, const AffineTransform3 *frame,
                                                     cIIntersectionQueryStrategy *visitor,
                                                     nGZCompGeom::cILinear3 *linear)
{
    {
        const float e = (size2 + size2) / CellDivisor(cell);
        const Vec3 half = {e, e, e};
        compgeom::TranslatedCuboidShape cuboid(&half, reinterpret_cast<const Vec3 *>(cell->mCenter));
        if (!cuboid.MeetsLinearIn(frame, linear)) return 0;
        Item *item = cell->mFirst;
        for (uint32_t i = 0; i < cell->mCount; ++i) {
            if (item->mNode && visitor->VisitLinear(item->mNode, linear) == 1) return 1;
            item = item->mNext;
        }
    }
    for (Cell *child : cell->mChildren) {
        if (child && QueryCellsLinear(child, size2, frame, visitor, linear) == 1) return 1;
    }
    return 0;
}

// Sub-partitions the line meets first (all of them, nothing stops early); then, if the line meets the root cell's box,
// the cells in the node's frame from the root down.
void cLooseOctreePartitionNode::RayCast(float *t, void *context, cINearerIntersectionQueryStrategy *visitor,
                                        nGZCompGeom::cILinear3 *linear)
{
    for (cIPartitionNode **p = mPartitions.data(); p != PartitionsEnd(); ++p) {
        if (*p && (*p)->IntersectsLinear(linear)) (*p)->RayCast(t, context, visitor, linear);
    }
    mImpl.RefreshIfStale(this);
    cRigidTransform world = mImpl.mWorld;
    const float h = (mSize + mSize) * 0.5f;
    const float extent[3] = {h, h, h};
    Vec3 center = {mRoot->mCenter[0], mRoot->mCenter[1], mRoot->mCenter[2]};
    compgeom::RigidPoint(&world, &center);
    AffineTransform3 frame;
    frame.mUnknown0 = 1;
    frame.mOffset[0] = center.x;
    frame.mOffset[1] = center.y;
    frame.mOffset[2] = center.z;
    float rotation[4];
    std::memcpy(rotation, world.Rotation(), sizeof rotation);
    game_code::SetFrameRotation(&frame, rotation);
    if (!linear->IntersectsCuboidIn(AsCuboid(extent), &frame)) return;

    RayRecord record;
    record.mSize2 = mSize + mSize;
    const Vec3 direction = linear->GetDirection();
    const Vec3 origin = linear->GetOrigin();
    record.mOrigin = origin;
    compgeom::RigidInversePoint(&world, &record.mOrigin);
    record.mDirection = direction;
    compgeom::RigidInverseVector(&world, &record.mDirection);
    record.mMinT = 0.0f;
    record.mVisitor = visitor;
    record.mLinear = linear;
    if (linear->GetClassID() == kLineClassId) {
        record.mMinT = -std::numeric_limits<float>::max(); // a line runs both ways
    } else if (linear->GetClassID() == kSegmentClassId) {
        *t = (*t < 1.0f) ? *t : 1.0f; // a segment ends at 1
    }
    RayCells(mRoot, &record, t, context);
}

// The cell's items, then its children whose loose box the line crosses within [mMinT, *t], nearest entry first (an
// insertion sort on the entry parameter); the walk stops at the first child that starts past *t.
void cLooseOctreePartitionNode::RayCells(Cell *cell, const RayRecord *record, float *t, void *context)
{
    Item *item = cell->mFirst;
    for (uint32_t i = 0; i < cell->mCount; ++i) {
        if (item->mNode) record->mVisitor->Visit(context, t, item->mNode, record->mLinear);
        item = item->mNext;
    }
    struct Entry {
        Cell *mCell;
        float mT;
    };
    Entry entries[8] = {};
    uint32_t count = 0;
    for (Cell *child : cell->mChildren) {
        if (!child) continue;
        const float e = (record->mSize2 + record->mSize2) / CellDivisor(child);
        const float h = e * 0.5f;
        const Vec3 box[2] = {{h, h, h}, {child->mCenter[0], child->mCenter[1], child->mCenter[2]}};
        float enter;
        float leave;
        if (compgeom::LineBoxSpan(&enter, &leave, box, &record->mOrigin) == 0) continue;
        if (enter > *t || record->mMinT > leave) continue;
        uint32_t at = 0;
        for (uint32_t n = count; n != 0;) {
            const uint32_t half = n >> 1;
            if (enter > entries[at + half].mT) {
                at += half + 1;
                n -= half + 1;
            } else {
                n = half;
            }
        }
        for (uint32_t k = count; k > at; --k) entries[k] = entries[k - 1];
        entries[at] = Entry{child, enter};
        ++count;
    }
    for (uint32_t k = 0; k < count; ++k) {
        RayCells(entries[k].mCell, record, t, context);
        if (entries[k].mT > *t) break;
    }
}

void cLooseOctreePartitionNode::Cull(void *viewer)
{
    for (cIPartitionNode **p = mPartitions.data(); p != PartitionsEnd(); ++p) {
        if (*p) (*p)->Cull(viewer);
    }
    mImpl.RefreshIfStale(this);
    cRigidTransform world = mImpl.mWorld;
    alignas(16) float matrixBuffer[16];
    const float *const matrix = compgeom::RigidToMatrix(&world, matrixBuffer);
    alignas(16) float view[16];
    compgeom::ViewTransform(view, matrix, static_cast<const uint8_t *>(viewer) + kViewerViewOffset);
    alignas(16) uint8_t frustum[0x60];
    compgeom::FrustumFromView(frustum, view);
    CullCells(mRoot, mSize + mSize, viewer, frustum, 0);
}

// Cells outside the frustum are skipped with their subtrees; a mask of all six planes skips the test below it.
void cLooseOctreePartitionNode::CullCells(Cell *cell, float size2, void *viewer, const void *frustum, uint32_t planeMask)
{
    if (static_cast<uint8_t>(planeMask) != kAllPlanes) {
        const float e = (size2 + size2) / CellDivisor(cell);
        const float h = e * 0.5f;
        const Vec3 half = {h, h, h};
        const Vec3 center = {cell->mCenter[0], cell->mCenter[1], cell->mCenter[2]};
        if (compgeom::FrustumClassify(frustum, &planeMask, &half, &center) > 0) return;
    }
    Item *item = cell->mFirst;
    for (uint32_t i = 0; i < cell->mCount; ++i) {
        cIRenderableNode *const node = static_cast<cIRenderableNode *>(item->mNode);
        if ((node->GetFlags() & kDrawnFlags) == kDrawnFlags && (node->GetFlags() & kHiddenFlags) == 0) {
            cIVisibilityQueryFilter *const filter = *reinterpret_cast<cIVisibilityQueryFilter *const *>(
                static_cast<const uint8_t *>(viewer) + kViewerFilterOffset);
            if (!filter || filter->Accept(node, viewer)) node->Draw(viewer, planeMask);
        }
        item = item->mNext;
    }
    for (Cell *child : cell->mChildren) {
        if (child) CullCells(child, size2, viewer, frustum, planeMask);
    }
}

void cLooseOctreePartitionNode::SetSize(float size)
{
    Clear();
    const float half = size * 0.5f;
    mHalf[0] = half;
    mHalf[1] = half;
    mHalf[2] = half;
    mSize = size;
    NodeTransformChanged(this);
}

// ---- the persist face ----

void cLooseOctreePartitionNode::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    Base::Read(reader);
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cLooseOctreePartitionNode::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cLooseOctreePartitionNode");
    info.mWord4 = kTypeWord4;
    info.mVersion = kVersion;
    return info;
}

void cLooseOctreePartitionNode::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cLooseOctreePartitionNode::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    Base::Write(writer);
}

} // namespace nGZSceneGraph
