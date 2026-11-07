#include "compgeom/TrianglePairs.h"

#include <cfloat>
#include <cstring>
#include <type_traits>

#include "compgeom/Frames.h"
#include "compgeom/Rigid.h"
#include "compgeom/ShapePairs.h"
#include "misc/X87.h"

// The line, ray and segment against a triangle set (src/compgeom/TrianglePairs.h). RVAs 1.18.0.187.
//
// As in ShapePairs.cpp every scalar operation spells out its operand order (SSE returns the first operand's NaN, quieted),
// and the comparisons are the ordered C++ ones the original's comiss/jcc pairs compute.
namespace nGZCompGeom {

namespace {

float MulS(float a, float b) { return a != a ? QuietNaN(a) : a * b; } // mulss a, b
float AddS(float a, float b) { return AddInOrder(a, b); }              // addss a, b
float Absolute(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    bits &= 0x7fffffffu;
    std::memcpy(&f, &bits, 4);
    return f;
}
float FloatOf(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

const float kParallel = FloatOf(0x2f03126f); // 1.19209290e-10: a determinant this small is a parallel line

// x87 arithmetic on two NaNs picks the larger significand where SSE picks the first (lane PH/PI: equal significands take
// the positive one).
double X87Pick(double a, double b)
{
    uint64_t ba, bb;
    std::memcpy(&ba, &a, 8);
    std::memcpy(&bb, &b, 8);
    const uint64_t sa = ba & 0x000fffffffffffffull, sb = bb & 0x000fffffffffffffull;
    if (sa != sb) return sb > sa ? b : a;
    return (ba >> 63) ? b : a;
}
// fdiv dword [mem] with `reg` in st0: reg / mem.
double DivRegByMem87(double reg, float mem)
{
    // The memory operand is converted on its own; a signalling NaN there is quieted before it is compared.
    if (mem != mem && reg != reg) return X87Pick(reg, static_cast<double>(FloatOf(Bits(mem) | 0x00400000u)));
    return x87::Rounded(reg / static_cast<double>(mem));
}

// The corners of triangle `index` as nine floats.
struct Triangle {
    Vec3 a, b, c;
};
Triangle TriangleAt(const TriangleSet *set, uint32_t index)
{
    const Vec3 *vertices = set->mVertices.data();
    const uint16_t *corners = set->mIndices.data() + index * 3;
    return {vertices[corners[0]], vertices[corners[1]], vertices[corners[2]]};
}
inline bool IsTriangle(uint32_t child) { return (child & AABBTreeNode::kTriangleBit) != 0; }
inline const AABBTreeNode *NodeOf(const TriangleSet *set, uint32_t child) { return set->mTree.mNodes.data() + (child >> 1); }

// The test cores of the three wrappers: the edges and direction of the triangle against a line (origin, direction) or a
// segment (start, end - start).
struct Edges {
    float e1[3], e2[3], s[3], dir[3];
};
Edges EdgesOf(const float *tri, const Vec3 &origin, const Vec3 &direction)
{
    Edges e;
    e.e1[0] = tri[3] - tri[0];
    e.e1[1] = tri[4] - tri[1];
    e.e1[2] = tri[5] - tri[2];
    e.e2[0] = tri[6] - tri[0];
    e.e2[1] = tri[7] - tri[1];
    e.e2[2] = tri[8] - tri[2];
    e.s[0] = origin.x - tri[0];
    e.s[1] = origin.y - tri[1];
    e.s[2] = origin.z - tri[2];
    e.dir[0] = direction.x;
    e.dir[1] = direction.y;
    e.dir[2] = direction.z;
    return e;
}

} // namespace

bool __cdecl TriangleCore(float *t, const float *e1, const float *e2, const float *s, const float *dir)
{
    // c = dir x e2 (in the order the original forms the products), det = c . e1
    float c[3];
    c[0] = MulS(e2[2], dir[1]) - MulS(e2[1], dir[2]);
    c[1] = MulS(e2[0], dir[2]) - MulS(e2[2], dir[0]);
    c[2] = MulS(e2[1], dir[0]) - MulS(e2[0], dir[1]);
    const float det = static_cast<float>(Dot87(c, e1));
    if (kParallel >= Absolute(det)) return false;
    const float u = static_cast<float>(Dot87(c, s));
    if (det > 0.0f) {
        if (0.0f > u || u > det) return false;
    } else {
        if (u > 0.0f || det > u) return false;
    }
    // v = s x e1
    float v[3];
    v[0] = MulS(s[1], e1[2]) - MulS(s[2], e1[1]);
    v[1] = MulS(e1[0], s[2]) - MulS(s[0], e1[2]);
    v[2] = MulS(s[0], e1[1]) - MulS(e1[0], s[1]);
    const float sv = AddS(AddS(MulS(v[1], dir[1]), MulS(v[0], dir[0])), MulS(v[2], dir[2]));
    if (det > 0.0f) {
        if (0.0f > sv || AddS(sv, u) > det) return false;
    } else {
        if (sv > 0.0f || det > AddS(sv, u)) return false;
    }
    *t = static_cast<float>(DivRegByMem87(Dot87(e2, v), det));
    return true;
}

bool __cdecl TriangleLineHit(float *t, const float *triangle, const Line3 *line)
{
    const Edges e = EdgesOf(triangle, line->origin, line->direction);
    return TriangleCore(t, e.e1, e.e2, e.s, e.dir);
}
bool __cdecl TriangleLineTest(const float *triangle, const Line3 *line)
{
    float t;
    const Edges e = EdgesOf(triangle, line->origin, line->direction);
    return TriangleCore(&t, e.e1, e.e2, e.s, e.dir);
}
bool __cdecl TriangleRayTest(const float *triangle, const Ray3 *ray)
{
    float t;
    const Edges e = EdgesOf(triangle, ray->origin, ray->direction);
    return TriangleCore(&t, e.e1, e.e2, e.s, e.dir) && t >= 0.0f;
}
bool __cdecl TriangleSegmentTest(const float *triangle, const Segment3 *segment)
{
    float t;
    const Vec3 direction{segment->end.x - segment->start.x, segment->end.y - segment->start.y,
                         segment->end.z - segment->start.z};
    const Edges e = EdgesOf(triangle, segment->start, direction);
    return TriangleCore(&t, e.e1, e.e2, e.s, e.dir) && t >= 0.0f && 1.0f >= t;
}

int __cdecl BoxLineSpan(float *t0, float *t1, const void *cuboid, const Vec3 *centre, const Line3 *line)
{
    Line3 local;
    local.origin = {line->origin.x - centre->x, line->origin.y - centre->y, line->origin.z - centre->z};
    local.direction = line->direction;
    return CuboidLineSpan(t0, t1, cuboid, &local) ? 2 : 0;
}

namespace {

template <class L> bool TriangleTest(const float *triangle, const L *linear)
{
    if constexpr (std::is_same_v<L, Line3>)
        return TriangleLineTest(triangle, linear);
    else if constexpr (std::is_same_v<L, Ray3>)
        return TriangleRayTest(triangle, linear);
    else
        return TriangleSegmentTest(triangle, linear);
}
template <class L> bool BoxTest(const AABBTreeNode *node, const L *linear)
{
    return TestPair<ShapeKind::Cuboid, Placement::Translated, L>(node, &node->centre, linear);
}

} // namespace

template <class L> bool __cdecl TestTreeWalk(const AABBTreeNode *node, const TriangleSet *set, const L *linear)
{
    if (!BoxTest(node, linear)) return false;
    const bool left = IsTriangle(node->left), right = IsTriangle(node->right);
    if (left) {
        const Triangle t = TriangleAt(set, node->left >> 1);
        if (TriangleTest(&t.a.x, linear)) return true;
        if (right) {
            const Triangle r = TriangleAt(set, node->right >> 1);
            return TriangleTest(&r.a.x, linear);
        }
        return TestTreeWalk(NodeOf(set, node->right), set, linear);
    }
    if (right) {
        const Triangle r = TriangleAt(set, node->right >> 1);
        if (TriangleTest(&r.a.x, linear)) return true;
        return TestTreeWalk(NodeOf(set, node->left), set, linear);
    }
    if (TestTreeWalk(NodeOf(set, node->left), set, linear)) return true;
    return TestTreeWalk(NodeOf(set, node->right), set, linear);
}

bool __cdecl HitTreeWalk(float *t, float lo, const AABBTreeNode *node, const TriangleSet *set, const Line3 *line)
{
    bool found = false;
    for (const uint32_t child : {node->left, node->right}) {
        if (!IsTriangle(child)) continue;
        const Triangle tri = TriangleAt(set, child >> 1);
        float hit;
        if (TriangleLineHit(&hit, &tri.a.x, line) && *t >= hit && hit >= lo) {
            *t = hit;
            found = true;
        }
    }
    // The child nodes whose box the line crosses within [lo, *t].
    const AABBTreeNode *first = nullptr, *second = nullptr;
    float a0 = 0.0f, a1 = 0.0f, b0 = 0.0f, b1 = 0.0f;
    if (!IsTriangle(node->left)) {
        const AABBTreeNode *left = NodeOf(set, node->left);
        if (BoxLineSpan(&a0, &a1, left, &left->centre, line) && *t >= a0 && a1 >= lo) {
            if (IsTriangle(node->right)) return HitTreeWalk(t, lo, left, set, line) || found;
            first = left;
        }
    }
    if (!IsTriangle(node->right)) {
        const AABBTreeNode *right = NodeOf(set, node->right);
        if (BoxLineSpan(&b0, &b1, right, &right->centre, line) && *t >= b0 && b1 >= lo) second = right;
    }
    if (first && second) {
        // The nearer box first; both are walked, and the node's own hits do not count towards the result.
        bool a, b;
        if (b0 > a0) {
            a = HitTreeWalk(t, lo, first, set, line);
            b = HitTreeWalk(t, lo, second, set, line);
        } else {
            b = HitTreeWalk(t, lo, second, set, line);
            a = HitTreeWalk(t, lo, first, set, line);
        }
        return a || b;
    }
    if (first) return HitTreeWalk(t, lo, first, set, line) || found;
    if (second) return HitTreeWalk(t, lo, second, set, line) || found;
    return found;
}

template <class L> bool __cdecl TestTriangleSet(const TriangleSet *set, const L *linear)
{
    return TestTreeWalk(set->mTree.mNodes.data(), set, linear);
}

template <class L> bool __cdecl TestTriangleSetInFrame(const TriangleSet *set, const AffineTransform3 *frame, const L *linear)
{
    L local;
    if constexpr (std::is_same_v<L, Segment3>) {
        local.start = FrameInversePoint(*frame, linear->start);
        local.end = FrameInversePoint(*frame, linear->end);
    } else {
        local.origin = FrameInversePoint(*frame, linear->origin);
        local.direction = FrameInverseVector(*frame, linear->direction);
    }
    return TestTreeWalk(set->mTree.mNodes.data(), set, &local);
}

template <class L> bool __cdecl HitTriangleSet(float *t, const TriangleSet *set, const L *linear)
{
    const AABBTreeNode *root = set->mTree.mNodes.data();
    if (!BoxTest(root, linear)) return false;
    Line3 line;
    if constexpr (std::is_same_v<L, Segment3>) {
        line.origin = linear->start;
        line.direction = {linear->end.x - linear->start.x, linear->end.y - linear->start.y, linear->end.z - linear->start.z};
        *t = 1.0f;
        return HitTreeWalk(t, 0.0f, root, set, &line);
    } else {
        line.origin = linear->origin;
        line.direction = linear->direction;
        *t = FLT_MAX;
        return HitTreeWalk(t, std::is_same_v<L, Line3> ? -FLT_MAX : 0.0f, root, set, &line);
    }
}

template <class L>
bool __cdecl HitTriangleSetInFrame(float *t, const TriangleSet *set, const AffineTransform3 *frame, const L *linear)
{
    L local;
    if constexpr (std::is_same_v<L, Segment3>) {
        local.start = FrameInversePoint(*frame, linear->start);
        local.end = FrameInversePoint(*frame, linear->end);
    } else {
        local.origin = FrameInversePoint(*frame, linear->origin);
        local.direction = FrameInverseVector(*frame, linear->direction);
    }
    return HitTriangleSet(t, set, &local);
}

#define TRIANGLE_PAIRS(L)                                                                                                \
    template bool __cdecl TestTreeWalk<L>(const AABBTreeNode *, const TriangleSet *, const L *);                        \
    template bool __cdecl TestTriangleSet<L>(const TriangleSet *, const L *);                                           \
    template bool __cdecl TestTriangleSetInFrame<L>(const TriangleSet *, const AffineTransform3 *, const L *);          \
    template bool __cdecl HitTriangleSet<L>(float *, const TriangleSet *, const L *);                                   \
    template bool __cdecl HitTriangleSetInFrame<L>(float *, const TriangleSet *, const AffineTransform3 *, const L *);
TRIANGLE_PAIRS(Line3)
TRIANGLE_PAIRS(Ray3)
TRIANGLE_PAIRS(Segment3)
#undef TRIANGLE_PAIRS

} // namespace nGZCompGeom
