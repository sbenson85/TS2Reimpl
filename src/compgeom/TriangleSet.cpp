#include "compgeom/TriangleSet.h"

#include <algorithm>
#include <cfloat>
#include <utility>

#include "compgeom/Frames.h"
#include "compgeom/SeparatingAxes.h"
#include "compgeom/TrianglePairs.h"

// nGZCompGeom's triangle sets and their bound shape (src/compgeom/TriangleSet.h). RVAs 1.18.0.187.
//
// The intersection tests the slots forward to are ours: the walkers of lines, rays and segments (TrianglePairs.h) and the
// separating-axis tests against a cuboid (0x19f8e0) and another set (0x1a01c0, SeparatingAxes.h), called with the
// arguments the original slots pass. The cuboid boxes (0x18ff90 / 0x190170), the rigid transform's point (0x19c900) and
// the frame's rotation (0x19d0d0) are ours too (src/compgeom/Rigid.h).
namespace nGZCompGeom {

namespace {

// ---- the separating-axis tests (SeparatingAxes.h) ----

// 0x19f8e0: the set (placed by setFrame) meets the cuboid (half extents, placed by cuboidFrame).
bool SetMeetsCuboid(const TriangleSet *set, const AffineTransform3 *setFrame, const void *cuboid,
                    const AffineTransform3 *cuboidFrame)
{
    return SetMeetsCuboidTest(set, setFrame, cuboid, cuboidFrame);
}
// 0x1a01c0: two sets, each placed by its frame, meet.
bool SetMeetsSet(const TriangleSet *set, const AffineTransform3 *setFrame, const TriangleSet *other,
                 const AffineTransform3 *otherFrame)
{
    return SetMeetsSetTest(set, setFrame, other, otherFrame);
}

// The frame with no rotation, scale or translation (flags 0).
AffineTransform3 IdentityFrame()
{
    AffineTransform3 frame{};
    frame.mMatrix[0] = 1.0f;
    frame.mMatrix[4] = 1.0f;
    frame.mMatrix[8] = 1.0f;
    return frame;
}
constexpr uint32_t kFrameTranslates = 1; // AffineTransform3 flag bit 0: the offset is not zero

// The data of the cuboid shapes the slots take: half extents, then the position (and an oriented cuboid's rotation).
struct TranslatedCuboidData {
    Cuboid cuboid;  // +0
    Vec3 position;  // +0xc
};
struct OrientedCuboidData {
    Cuboid cuboid;        // +0
    Vec3 position;        // +0xc
    float rotation[4];    // +0x18: a quaternion
};

// ---- building the tree (0x195c80 and its helpers, inside the tree's Build) ----

// A node of the builder (0x24 bytes in the original, 0x195550): the box, its two children and, for a leaf, its triangle.
// The original links children by pointer; ours by index into the builder's list.
struct BuildNode {
    Cuboid box;
    Vec3 centre;
    uint32_t left;
    uint32_t right;
    int32_t triangle; // -1 for an inner node
    BuildNode() : left(0), right(0), triangle(-1) {}
};
static_assert(sizeof(BuildNode) == 0x24, "the builder's node size decides its allocations");

// One triangle's box and leaf (0x1c bytes in the original).
struct BuildRecord {
    BoundBox box;
    uint32_t node;
};

// The box with nothing in it: every min FLT_MAX, every max -FLT_MAX.
BoundBox EmptyBox()
{
    return BoundBox{{FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX}};
}

float Add(float a, float b)
{
    return AddInOrder(a, b); // src/compgeom/Frames.h: the original's operand order, NaNs included
}

// Half extents and centre of a box, as the original computes them: half = (max - min) * 0.5, centre = half + min.
void SetHalfAndCentre(Cuboid &box, Vec3 &centre, const BoundBox &b)
{
    box.half = {(b.max.x - b.min.x) * 0.5f, (b.max.y - b.min.y) * 0.5f, (b.max.z - b.min.z) * 0.5f};
    centre = {Add(box.half.x, b.min.x), Add(box.half.y, b.min.y), Add(box.half.z, b.min.z)};
}

float Axis(const Vec3 &v, uint32_t axis)
{
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

class TreeBuilder {
public:
    // 0x196650: room for `count` nodes (length_error above the vector's maximum), then the root.
    explicit TreeBuilder(uint32_t count)
    {
        mNodes.reserve(count);
        mNodes.resize(1);
    }

    // 0x195d90: a leaf and a record per triangle, then (two triangles or more) the split from the root.
    void AddTriangles(const TriangleSet &set)
    {
        const uint32_t triangles = set.TriangleCount();
        std::vector<BuildRecord> records(triangles); // operator new, zeroed
        for (uint32_t i = 0; i < triangles; ++i) {
            const uint32_t leaf = NewNode();
            records[i].node = leaf;
            const BoundBox box = set.TriangleBox(i);
            records[i].box = box;
            BuildNode &node = mNodes[leaf];
            node.box.half = {(box.max.x - box.min.x) * 0.5f, (box.max.y - box.min.y) * 0.5f, (box.max.z - box.min.z) * 0.5f};
            node.centre = {Add(box.min.x, node.box.half.x), Add(node.box.half.y, box.min.y),
                           Add(node.box.half.z, box.min.z)};
            node.triangle = static_cast<int32_t>(i);
        }
        if (set.mIndices.size() > 5) Split(0, triangles, records.data());
    }

    // 0x196060: the node's box takes in the records'; they are split at the box's centre along its longest side (half
    // each way when every record falls on one side), and each side becomes a leaf (one record) or a new node.
    void Split(uint32_t nodeIndex, uint32_t count, BuildRecord *records)
    {
        BoundBox box = EmptyBox();
        for (uint32_t i = 0; i < count; ++i) box.Grow(records[i].box);
        SetHalfAndCentre(mNodes[nodeIndex].box, mNodes[nodeIndex].centre, box);

        const float dx = box.max.x - box.min.x, dy = box.max.y - box.min.y, dz = box.max.z - box.min.z;
        uint32_t axis;
        if (dx > dy) axis = dx > dz ? 0 : 2;
        else axis = dy > dz ? 1 : 2;
        const float split = Axis(mNodes[nodeIndex].centre, axis);

        BuildRecord *first = records;
        BuildRecord *last = records + count;
        while (first != last) {
            if (split > Axis(mNodes[first->node].centre, axis)) {
                ++first;
                continue;
            }
            do {
                --last;
                if (first == last) goto partitioned;
            } while (!(split > Axis(mNodes[last->node].centre, axis)));
            std::swap(*first, *last);
            ++first;
        }
    partitioned:
        uint32_t below = static_cast<uint32_t>(first - records);
        if (below == 0 || below == count) below = count >> 1;

        if (below == 1) {
            mNodes[nodeIndex].left = records[0].node;
        } else {
            const uint32_t child = NewNode();
            mNodes[nodeIndex].left = child;
            Split(child, below, records);
        }
        if (count - below == 1) {
            mNodes[nodeIndex].right = records[below].node;
        } else {
            const uint32_t child = NewNode();
            mNodes[nodeIndex].right = child;
            Split(child, count - below, records + below);
        }
    }

    // 0x196310: the builder's inner nodes into the tree, root first, a node's left subtree before its right child
    // (numbered in that order from `next`); leaves become triangle codes.
    void Flatten(AABBTreeNode *tree, uint32_t index, uint32_t &next, uint32_t from) const
    {
        for (;;) {
            const BuildNode &node = mNodes[from];
            AABBTreeNode &out = tree[index];
            out.box = node.box;
            out.centre = node.centre;
            const BuildNode &left = mNodes[node.left];
            if (left.triangle == -1) {
                const uint32_t child = next++;
                out.left = child * 2;
                Flatten(tree, child, next, node.left);
            } else {
                out.left = static_cast<uint32_t>(left.triangle) * 2 | AABBTreeNode::kTriangleBit;
            }
            const BuildNode &right = mNodes[node.right];
            if (right.triangle != -1) {
                out.right = static_cast<uint32_t>(right.triangle) * 2 | AABBTreeNode::kTriangleBit;
                return;
            }
            index = next++;
            out.right = index * 2;
            from = node.right;
        }
    }

    uint32_t NodeCount() const { return static_cast<uint32_t>(mNodes.size()); }
    const BuildNode &Node(uint32_t index) const { return mNodes[index]; }

private:
    // 0x196480: a new node at the end (the list grows as std::vector does when the reserve is short).
    uint32_t NewNode()
    {
        mNodes.resize(mNodes.size() + 1);
        return static_cast<uint32_t>(mNodes.size() - 1);
    }

    ServiceVector<BuildNode> mNodes;
};

} // namespace

// ---- boxes ----

BoundBox TriangleBounds(const Vec3 &a, const Vec3 &b, const Vec3 &c)
{
    BoundBox box;
    box.max = {std::max(std::max(a.x, b.x), c.x), std::max(std::max(a.y, b.y), c.y), std::max(std::max(a.z, b.z), c.z)};
    box.min = {std::min(std::min(a.x, b.x), c.x), std::min(std::min(a.y, b.y), c.y), std::min(std::min(a.z, b.z), c.z)};
    return box;
}

BoundBox TriangleSet::TriangleBox(uint32_t triangle) const
{
    const uint16_t *corners = mIndices.data() + triangle * 3;
    const TriangleSetVertex *v = mVertices.data();
    return TriangleBounds(v[corners[0]], v[corners[1]], v[corners[2]]);
}

// ---- the tree ----

void cAABBNoLeafTree::Build(const TriangleSet &set)
{
    const uint32_t triangles = set.TriangleCount();
    // ORIGINAL-BUG: a set with no triangles asks for 0xffffffff nodes and throws length_error (0x196650).
    TreeBuilder builder(triangles * 2 - 1);
    builder.AddTriangles(set);
    if (builder.NodeCount() == triangles * 2 - 1) {
        if (mNodes.size() != triangles - 1) {
            mNodes.clear(); // 0x1965e0
            mNodes.resize(triangles - 1);
        }
        uint32_t next = 1;
        builder.Flatten(mNodes.data(), 0, next, 0);
    } else if (triangles == 1 && builder.NodeCount() == 2) {
        // One triangle: a single node whose children are both the triangle.
        mNodes.clear();
        mNodes.resize(1);
        const BuildNode &leaf = builder.Node(1);
        mNodes[0].box = leaf.box;
        mNodes[0].centre = leaf.centre;
        mNodes[0].left = AABBTreeNode::kTriangleBit;
        mNodes[0].right = AABBTreeNode::kTriangleBit;
    }
}

void cAABBNoLeafTree::Refit(const TriangleSet &set, uint32_t)
{
    AABBTreeNode *const nodes = mNodes.data();
    for (size_t i = mNodes.size(); i-- > 0;) {
        AABBTreeNode &node = nodes[i];
        BoundBox box = EmptyBox();
        for (const uint32_t code : {node.left, node.right}) {
            const uint32_t index = code >> 1;
            if (code & AABBTreeNode::kTriangleBit) box.Grow(set.TriangleBox(index));
            else box.Grow(nodes[index].box.BoxAt(nodes[index].centre));
        }
        SetHalfAndCentre(node.box, node.centre, box);
    }
}

// ---- the set ----

void TriangleSet::Resize(uint32_t vertexCount, uint32_t triangleCount)
{
    mVertices.resize(vertexCount);
    mIndices.resize(triangleCount * 3);
}

void TriangleSet::SetVertex(uint16_t index, const Vec3 *vertex)
{
    static_cast<Vec3 &>(mVertices.data()[index]) = *vertex;
}

void TriangleSet::SetTriangle(int32_t index, uint16_t a, uint16_t b, uint16_t c)
{
    uint16_t *const corners = mIndices.data() + index * 3;
    corners[0] = a;
    corners[1] = b;
    corners[2] = c;
}

void TriangleSet::Build()
{
    mTree.Build(*this);
}

void TriangleSet::Refit(uint32_t unused)
{
    mTree.Refit(*this, unused);
}

// ---- the shape ----

cTriangleSetBound::cTriangleSetBound(const TriangleSet &set) : mShape(set), mRefCount(0) {}

cTriangleSetShape *__cdecl cTriangleSetShape::Create()
{
    return new cTriangleSetShape;
}

bool cTriangleSetBound::QueryInterface(uint32_t id, void **out)
{
    if (!out) return false;
    if (id == 1 || id == cIShape::kInterfaceId || id == cIBound::kInterfaceId || id == kInterfaceId) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

uint32_t cTriangleSetBound::AddRef()
{
    return ++mRefCount;
}

uint32_t cTriangleSetBound::Release()
{
    const uint32_t count = --mRefCount;
    if (count == 0) {
        delete this;
        return 0;
    }
    return count;
}

// A null allocation is not tested: the original then reads the vtable of address zero.
bool cTriangleSetBound::Clone(uint32_t id, void **out)
{
    // ORIGINAL-BUG: a copy the query refuses (an id the shape doesn't answer) is never released: it leaks.
    cTriangleSetBound *const copy = new cTriangleSetBound(mShape);
    return copy->QueryInterface(id, out);
}

uint32_t cTriangleSetBound::GetClassID()
{
    return kClassId;
}

// Cones and spheres: no test exists for a triangle set (the original builds the other shape's frame, then answers false).
bool cTriangleSetBound::IntersectsOrientedConeIn(const AffineTransform3 *, const OrientedCone *) { return false; }
bool cTriangleSetBound::IntersectsTranslatedConeIn(const AffineTransform3 *, const TranslatedCone *) { return false; }
bool cTriangleSetBound::IntersectsTranslatedSphereIn(const AffineTransform3 *, const TranslatedSphere *) { return false; }
bool cTriangleSetBound::IntersectsConeAt(const Cone *, const AffineTransform3 *) { return false; }
bool cTriangleSetBound::IntersectsSphereAt(const Sphere *, const AffineTransform3 *) { return false; }
bool cTriangleSetBound::IntersectsConeInAt(const AffineTransform3 *, const Cone *, const AffineTransform3 *) { return false; }
bool cTriangleSetBound::IntersectsSphereInAt(const AffineTransform3 *, const Sphere *, const AffineTransform3 *)
{
    return false;
}

bool cTriangleSetBound::IntersectsOrientedCuboidIn(const AffineTransform3 *frame, const OrientedCuboid *cuboid)
{
    const auto *data = reinterpret_cast<const OrientedCuboidData *>(cuboid);
    AffineTransform3 cuboidFrame{};
    cuboidFrame.mUnknown0 = kFrameTranslates;
    cuboidFrame.mOffset[0] = data->position.x;
    cuboidFrame.mOffset[1] = data->position.y;
    cuboidFrame.mOffset[2] = data->position.z;
    static_cast<FrameMath &>(cuboidFrame).SetRotation(data->rotation); // 0x19d0d0: the frame's rotation from the quaternion
    return SetMeetsCuboid(&mShape, frame, &data->cuboid, &cuboidFrame);
}

bool cTriangleSetBound::IntersectsTranslatedCuboidIn(const AffineTransform3 *frame, const TranslatedCuboid *cuboid)
{
    const auto *data = reinterpret_cast<const TranslatedCuboidData *>(cuboid);
    AffineTransform3 cuboidFrame = IdentityFrame();
    cuboidFrame.mUnknown0 = kFrameTranslates;
    cuboidFrame.mOffset[0] = data->position.x;
    cuboidFrame.mOffset[1] = data->position.y;
    cuboidFrame.mOffset[2] = data->position.z;
    return SetMeetsCuboid(&mShape, frame, &data->cuboid, &cuboidFrame);
}

bool cTriangleSetBound::IntersectsBoundIn(const AffineTransform3 *frame, cIBound *bound)
{
    return bound->IntersectsTriangleSetAt(&mShape, frame);
}

bool cTriangleSetBound::IntersectsTriangleSetAt(const TriangleSet *set, const AffineTransform3 *frame)
{
    const AffineTransform3 identity = IdentityFrame();
    return SetMeetsSet(&mShape, &identity, set, frame);
}

bool cTriangleSetBound::IntersectsCuboidAt(const Cuboid *cuboid, const AffineTransform3 *frame)
{
    const AffineTransform3 identity = IdentityFrame();
    return SetMeetsCuboid(&mShape, &identity, cuboid, frame);
}

bool cTriangleSetBound::IntersectsBoundAt(cIBound *bound, const AffineTransform3 *frame)
{
    const AffineTransform3 identity = IdentityFrame();
    return bound->IntersectsTriangleSetInAt(frame, &mShape, &identity);
}

bool cTriangleSetBound::IntersectsTriangleSetInAt(const AffineTransform3 *frame, const TriangleSet *set,
                                                  const AffineTransform3 *otherFrame)
{
    return SetMeetsSet(&mShape, frame, set, otherFrame);
}

bool cTriangleSetBound::IntersectsCuboidInAt(const AffineTransform3 *frame, const Cuboid *cuboid,
                                             const AffineTransform3 *otherFrame)
{
    return SetMeetsCuboid(&mShape, frame, cuboid, otherFrame);
}

bool cTriangleSetBound::IntersectsBoundInAt(const AffineTransform3 *frame, cIBound *bound, const AffineTransform3 *otherFrame)
{
    return bound->IntersectsTriangleSetInAt(otherFrame, &mShape, frame);
}

bool cTriangleSetBound::IntersectsSegmentIn(const AffineTransform3 *frame, const Segment3 *segment)
{
    return nGZCompGeom::TestTriangleSetInFrame<Segment3>(&mShape, frame, segment);
}
bool cTriangleSetBound::IntersectsRayIn(const AffineTransform3 *frame, const Ray3 *ray)
{
    return nGZCompGeom::TestTriangleSetInFrame<Ray3>(&mShape, frame, ray);
}
bool cTriangleSetBound::IntersectsLineIn(const AffineTransform3 *frame, const Line3 *line)
{
    return nGZCompGeom::TestTriangleSetInFrame<Line3>(&mShape, frame, line);
}
bool cTriangleSetBound::IntersectsLinearIn(const AffineTransform3 *frame, cILinear3 *linear)
{
    return linear->IntersectsBoundIn(this, frame);
}

bool cTriangleSetBound::IntersectsSegment(const Segment3 *segment)
{
    return nGZCompGeom::TestTriangleSet<Segment3>(&mShape, segment);
}
bool cTriangleSetBound::IntersectsRay(const Ray3 *ray)
{
    return nGZCompGeom::TestTriangleSet<Ray3>(&mShape, ray);
}
bool cTriangleSetBound::IntersectsLine(const Line3 *line)
{
    return nGZCompGeom::TestTriangleSet<Line3>(&mShape, line);
}
bool cTriangleSetBound::IntersectsLinear(cILinear3 *linear)
{
    return linear->IntersectsBound(this);
}

bool cTriangleSetBound::FirstHitSegmentIn(float *t, const AffineTransform3 *frame, const Segment3 *segment)
{
    return nGZCompGeom::HitTriangleSetInFrame<Segment3>(t, &mShape, frame, segment);
}
bool cTriangleSetBound::FirstHitRayIn(float *t, const AffineTransform3 *frame, const Ray3 *ray)
{
    return nGZCompGeom::HitTriangleSetInFrame<Ray3>(t, &mShape, frame, ray);
}
bool cTriangleSetBound::FirstHitLineIn(float *t, const AffineTransform3 *frame, const Line3 *line)
{
    return nGZCompGeom::HitTriangleSetInFrame<Line3>(t, &mShape, frame, line);
}
bool cTriangleSetBound::FirstHitLinearIn(float *t, const AffineTransform3 *frame, cILinear3 *linear)
{
    return linear->FirstHitBoundIn(t, this, frame);
}

bool cTriangleSetBound::FirstHitSegment(float *t, const Segment3 *segment)
{
    return nGZCompGeom::HitTriangleSet<Segment3>(t, &mShape, segment);
}
bool cTriangleSetBound::FirstHitRay(float *t, const Ray3 *ray)
{
    return nGZCompGeom::HitTriangleSet<Ray3>(t, &mShape, ray);
}
bool cTriangleSetBound::FirstHitLine(float *t, const Line3 *line)
{
    return nGZCompGeom::HitTriangleSet<Line3>(t, &mShape, line);
}
bool cTriangleSetBound::FirstHitLinear(float *t, cILinear3 *linear)
{
    return linear->FirstHitBound(t, this);
}

// The three box slots read the tree's root without checking for one (a set never built has none).
BoundBox *cTriangleSetBound::GetBoundBoxIn(BoundBox *out, const AffineTransform3 *frame)
{
    const AABBTreeNode &root = mShape.mTree.mNodes.data()[0];
    AffineTransform3 placed = *frame;
    placed.mUnknown0 |= kFrameTranslates;
    const Vec3 centre = FramePoint(*frame, root.centre);
    placed.mOffset[0] = centre.x;
    placed.mOffset[1] = centre.y;
    placed.mOffset[2] = centre.z;
    *out = root.box.BoxInFrame(placed); // 0x18ff90
    return out;
}

BoundBox *cTriangleSetBound::GetBoundBoxRigid(BoundBox *out, const RigidTransform3 *rigid)
{
    const AABBTreeNode &root = mShape.mTree.mNodes.data()[0];
    RigidTransform3 placed;
    placed.translation = root.centre;
    rigid->TransformPoint(&placed.translation); // 0x19c900: a point through it, in place
    std::copy(rigid->rotation, rigid->rotation + 4, placed.rotation);
    *out = root.box.BoxInRigid(placed); // 0x190170
    return out;
}

BoundBox *cTriangleSetBound::GetBoundBox(BoundBox *out)
{
    const AABBTreeNode &root = mShape.mTree.mNodes.data()[0];
    *out = root.box.BoxAt(root.centre);
    return out;
}

TriangleSet *cTriangleSetBound::ShapeData()
{
    return &mShape;
}
const TriangleSet *cTriangleSetBound::ShapeDataConst()
{
    return &mShape;
}

} // namespace nGZCompGeom
