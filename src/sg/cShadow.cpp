// nTSSG::cShadow (lane 261109e, scenegraph LG7d; src/sg/cShadow.h).
//
// Still the original's here (OriginalFunction): the service getters 0x20defb (the camera's owner: slot 49 the camera,
// 50 the shadow manager, 45 / 38 the grids) and 0x20e0b2 (placement), the scene 0x211ce1; 0x3ad4a0 (a material bound to
// a node's named subset), 0x7e2cc0 (the terrain query of a point,
// its answer unused), 0x7e0920 (a tile quadrant's test), 0x1a77c0 (a quaternion's Euler angles), the render target's
// constructor 0x3c9ed0. The quad geometry maker 0xaf16f0 is ours (nGZSceneGraph::MakeQuadMesh, lane 261111c). Row LG7d2 (lane 261109g) added UpdateSettings,
// PlaceProjectors, FloorTest and their helpers; row LG7d3 (lane 261109h) CreateResources, its two helpers and the
// visibility filter.
#include "sg/cShadow.h"

#include "compgeom/CompGeom.h" // BoundBox, RigidTransform3
#include "compgeom/ConvexMath.h" // MulS, AddS, SubS, Negated: SSE operand order
#include "compgeom/ConvexShapes.h" // ConvexBox
#include "compgeom/Frames.h"   // FrameMath
#include "compgeom/Matrix4.h"  // Matrix4Multiply
#include "compgeom/SeparatingAxes.h" // FramePlacement
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "misc/Quaternion.h"
#include "misc/UtilitiesE.h"   // GetCachedQueriedObject: the material manager
#include "rz/StaticGetters.h"  // GetDirectorFrameworkObject
#include "rz/StaticGetters2.h" // GetCachedSingletonB
#include "rz/cRZString.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h" // RelativeTransform 0xaeb2c0, MakeQuadMesh 0xaf16f0
#include "sg/cIViewerRefNode.h"
#include "sg/cMaterialDefinition.h"
#include "sg/cMaterialManager.h"
#include "sg/cRenderGroupNameIterator.h" // RenderGroupRegistry
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"
#include "sg/cViewerRefNodeImpl.h" // cViewerRefEntry
#include "simantics/SimGeometry.h" // RotateByQuaternion
#include "world/WorldHeights.h"    // WithLevelOfGround
#include "world/WorldTileMath.h"   // WithTileValue, PositionToTileQuadrant

#include <cfloat>
#include <cmath>
#include <cstring>
#include <new>

namespace nTSSG {

namespace {

// The subsets (the game's table 0x100c1a8) and the two materials ("GUOBOff" 0xdb6908, "VisGUOBMaterial" 0xdba354).
constexpr const char *kSubsetNames[cShadow::kSubsets] = {"groundshadow",    "wallshadow",     "northwallshadow",
                                                        "southwallshadow", "eastwallshadow", "westwallshadow"};
constexpr const char *kShadowOff = "GUOBOff";
constexpr const char *kVisibleShadow = "VisGUOBMaterial";

constexpr uint32_t kIidShapeRefNode = 0xa516cd10;

// The shadow manager's option bits (its ChangeCount()[11], +0x4c).
constexpr uint32_t kOptionModeOne = 1u << 0;
constexpr uint32_t kOptionModeFour = 1u << 1;
constexpr uint32_t kOptionMaterials = 1u << 2;
constexpr uint32_t kOptionModeTwo = 1u << 3;
constexpr uint32_t kOptionVisible = 1u << 7;
constexpr uint32_t kOptionAllSubsets = 1u << 9;

void *CameraOwner() { return OriginalFunction<void *(__cdecl *)(), 0x20defb>()(); }
void *Placement() { return OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()(); }
void *Scene() { return OriginalFunction<void *(__cdecl *)(), 0x211ce1>()(); }

cIShadowManager *ShadowManager(void *owner)
{
    return RawSlot<50, cIShadowManager *>(owner); // receiver: the camera's owner (slot 50: the shadow manager)
}
void *Camera(void *owner) { return RawSlot<49, void *>(owner); } // receiver: the camera's owner (slot 49: the camera)

void BindSubsetMaterial(cIGZUnknown *node, const char *material, const char *subset)
{
    OriginalFunction<void(__cdecl *)(cIGZUnknown *, const char *, const char *), 0x3ad4a0>()(node, material, subset);
}
const float *RelativeTransform(float *out, void *bone, cIGZUnknown *node)
{
    // 0xaeb2c0 (ours); both pointers as the game passes them.
    return nGZSceneGraph::RelativeTransform(out, static_cast<nGZSceneGraph::cICompositionTreeNode *>(bone),
                                            reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(node));
}
void TerrainQuery(const void *point) { OriginalFunction<int32_t(__cdecl *)(const void *), 0x7e2cc0>()(point); }
uint8_t QuadrantTest(const worldmath::TileQuadrant *tile)
{
    return OriginalFunction<uint8_t(__cdecl *)(const worldmath::TileQuadrant *), 0x7e0920>()(tile);
}
void *LevelShown() { return OriginalFunction<void *(__cdecl *)(), 0x20df51>()(); }

// A float rounded to the nearest integer the original's way: 1.5 * 2^23 added, the sum's low bits taken.
int32_t RoundToTile(float value)
{
    const float sum = value + 12582912.0f;
    uint32_t bits;
    std::memcpy(&bits, &sum, 4);
    return int32_t(bits + 0xb4c00000u);
}

cIGZCOM *Director() { return static_cast<cIGZCOM *>(GetDirectorFrameworkObject()); }

// The director's classes CreateResources makes (with the interface asked of each).
constexpr uint32_t kViewerClass = 0xdca76dbb, kViewerIid = 0x5ca76db6;
constexpr uint32_t kTransformClass = 0x65246462, kTransformIid = 0xe51c10f5;
constexpr uint32_t kShapeRefClass = 0x65245517;
constexpr uint32_t kShapeClass = 0xfc6eb1f7;

// A counted pointer local to a function: released (when set) at the end of its scope.
struct LocalRef {
    cIGZUnknown *p = nullptr;
    LocalRef() = default;
    LocalRef(const LocalRef &) = delete;
    LocalRef &operator=(const LocalRef &) = delete;
    ~LocalRef()
    {
        if (p) p->Release(); // receiver: the held object (cIGZUnknown, slot 2)
    }
    void **Out() { return reinterpret_cast<void **>(&p); }
};

// A cRZString on the stack (constructed and destroyed with cRZString's own bodies).
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { s.Destruct(); }
    const char *Chars() const { return s.mString.c_str(); }
};

// 0x3c9ed0 (thiscall on 0x18 bytes; returns this): nTSSG::cBlurredRenderTarget's constructor (its texture index, +0x10,
// starts at -1).
cIGZUnknown *ConstructRenderTarget(void *memory)
{
    return OriginalFunction<cIGZUnknown *(__thiscall *)(void *), 0x3c9ed0>()(memory);
}
// The render target's texture index (its +0x10; the original reads it through the shared getter 0x149cf0).
int32_t TargetIndex(const cIGZUnknown *target)
{
    return *reinterpret_cast<const int32_t *>(reinterpret_cast<const uint8_t *>(target) + 0x10);
}
// 0xaf16f0 (ours, nGZSceneGraph::MakeQuadMesh): a quad's geometry into *out (cleared first): the centre, the size,
// then the texture coordinates' two corners.
void MakeQuadGeometry(void **out, const float *center, const float *size, const float *uvLow, const float *uvHigh)
{
    nGZSceneGraph::MakeQuadMesh(reinterpret_cast<nGZSceneGraph::cIGeometryNode **>(out), center, size, uvLow, uvHigh);
}

// 0x1a77c0 (__cdecl; returns out): a quaternion's three Euler angles, through its matrix.
const float *QuaternionAngles(float *out, const float *quaternion)
{
    return OriginalFunction<const float *(__cdecl *)(float *, const float *), 0x1a77c0>()(out, quaternion);
}

nGZSceneGraph::cICompositionTreeNode *TreeNode(void *object) { return static_cast<nGZSceneGraph::cICompositionTreeNode *>(object); }

// An object's cIGZUnknown virtual base: the vbtable at +4, entry 1, is the base's offset from it.
cIGZUnknown *VirtualBase(cIGZUnknown *object)
{
    uint8_t *const vbptr = reinterpret_cast<uint8_t *>(object) + 4;
    return reinterpret_cast<cIGZUnknown *>(vbptr + reinterpret_cast<const int32_t *const *>(vbptr)[0][1]);
}

// Release a pointer member: zeroed first, then released (the original's order).
void Drop(ShadowRef<cIGZUnknown> &ref)
{
    if (cIGZUnknown *const old = ref.p) {
        ref.p = nullptr;
        old->Release(); // receiver: the held object (cIGZUnknown, slot 2)
    }
}

// The new one counted, stored, then the old one released; nothing for the same pointer.
void Assign(ShadowRef<cIGZUnknown> &ref, cIGZUnknown *object)
{
    cIGZUnknown *const old = ref.p;
    if (object == old) return;
    if (object) object->AddRef(); // receiver: the new object (cIGZUnknown, slot 1)
    ref.p = object;
    if (old) old->Release(); // receiver: the old object (cIGZUnknown, slot 2)
}

// The camera's negated position (its slot 30 answers the position through a buffer).
void NegatedCameraPosition(void *camera, float *out)
{
    float buffer[4];
    const float *const position = RawSlot<30, const float *>(camera, buffer); // receiver: the camera (slot 30)
    out[0] = -position[0];
    out[1] = -position[1];
    out[2] = -position[2];
}

// A projector told the camera's view (the camera's slot 31 answers it through a buffer).
void TellView(cIGZUnknown *projector, void *camera)
{
    float buffer[16];
    const void *const view = RawSlot<31, const void *>(camera, buffer); // receiver: the camera (slot 31)
    RawSlot<5>(projector, view);                                        // receiver: the projector (slot 5)
}

// The float's bits as a signed integer mapped so that integer order is float order, compared within 32 steps: the
// original's test that a cached value still matches (the mapped difference d, then ((d >> 26) ^ d) & ~31 == 0).
bool NearlyEqual(float cached, float fresh)
{
    int32_t c, r;
    std::memcpy(&c, &cached, 4);
    std::memcpy(&r, &fresh, 4);
    const uint32_t cs = uint32_t(c >> 31), rs = uint32_t(r >> 31);
    const uint32_t oc = (cs >> 1) ^ uint32_t(c);
    const uint32_t orr = (rs >> 1) ^ uint32_t(r);
    const uint32_t d = oc - orr + rs - cs;
    return ((uint32_t(int32_t(d) >> 26) ^ d) & ~31u) == 0;
}

// The shadow manager's settings as its ChangeCount() hands them out (from its +0x20: cShadowManager.h).
struct ShadowSettings {
    int32_t changeCount;  // +0x00
    float size;           // +0x04
    float distance;       // +0x08
    int32_t blurRadius;   // +0x0c (indoors)
    int32_t outdoorBlur;  // +0x10
    uint32_t resolution;  // +0x14
    uint32_t base;        // +0x18
    float w0, h0, w1, h1; // +0x1c..+0x28
    uint32_t options;     // +0x2c
};
static_assert(offsetof(ShadowSettings, w0) == 0x1c && offsetof(ShadowSettings, options) == 0x2c, "the settings");

const ShadowSettings &Settings(const int32_t *record) { return *reinterpret_cast<const ShadowSettings *>(record); }

// The world axes the settings turn about (the game's .data 0x1004ce0, read only by 0x356e40 and 0x357810).
constexpr float kAxisX[3] = {1.0f, 0.0f, 0.0f};
constexpr float kAxisY[3] = {0.0f, 1.0f, 0.0f};
constexpr float kAxisZ[3] = {0.0f, 0.0f, 1.0f};

constexpr float kNear = -8.0f, kFar = 1.0f; // the settings view's clip range

nGZSceneGraph::cIViewerRefNode *ViewerNode(cIGZUnknown *object)
{
    return reinterpret_cast<nGZSceneGraph::cIViewerRefNode *>(object);
}
nGZSceneGraph::cITransformNode *TransformNode(cIGZUnknown *object)
{
    return reinterpret_cast<nGZSceneGraph::cITransformNode *>(object);
}

// The span [out[0], out[1]] the two W / H rows give along slope t: the four values W1 + t H1, t H1 - W1, t H0 - W0 and
// W0 + t H0, the least and the greatest picked in the original's comparison order (NaNs fall where its comiss puts them).
void SettingsExtent(const ShadowSettings &s, float t, float *out)
{
    const float u = t * s.h1, v = t * s.h0;
    const float a = s.w1 + u, b = u - s.w1, c = v - s.w0, d = s.w0 + v;
    const float *p = a > b ? &b : &a;
    const float *q = c > *p ? p : &c;
    out[0] = *q < d ? *q : d;
    p = b > a ? &b : &a;
    q = *p > c ? p : &c;
    out[1] = *q > d ? *q : d;
}

// The settings view's box in mode 1: across, the larger W; up, from lo to hi; the pixel aspect to match.
void SetSettingsView(cIGZUnknown *const &viewer, float m, float lo, float hi)
{
    ViewerNode(viewer)->SetOrtho(0, -m, m, lo, hi, kNear, kFar); // receiver: nGZSceneGraph::cIViewerRefNode
    ViewerNode(viewer)->SetPixelAspect(0, (hi - lo) / (m + m));   // receiver: nGZSceneGraph::cIViewerRefNode
}

} // namespace

// ---- free helpers ---------------------------------------------------------------------------------------------------

// Each product and sum in the original's operand order (convex::MulS / AddS): of two NaNs the first operand's wins,
// so out[0] and out[1] both carry -a[3]'s NaN when a[3] and b[3] are NaNs.
uint8_t __cdecl ClipPlanesMeet(const float *a, const float *b, float *out)
{
    using namespace nGZCompGeom::convex;
    const float negA1 = Negated(a[1]), negB0 = Negated(b[0]);
    const float determinant = AddS(MulS(negA1, b[0]), MulS(b[1], a[0]));
    if (double(determinant) > 1e-6) {
        const float inverse = 1.0f / determinant;
        const float negA3 = Negated(a[3]), negB3 = Negated(b[3]);
        out[0] = AddS(MulS(MulS(b[1], inverse), negA3), MulS(MulS(negA1, inverse), negB3));
        out[1] = AddS(MulS(MulS(negB0, inverse), negA3), MulS(MulS(a[0], inverse), negB3));
        return 1;
    }
    return 0;
}

uint8_t __cdecl ShadowFootprint(const float *matrix, float *outMin, float *outMax)
{
    float sumX[4], diffX[4], sumY[4], diffY[4];
    for (int i = 0; i < 4; ++i) {
        // The original's operand orders (addps / subps): row 3 first in sumX only.
        sumX[i] = nGZCompGeom::convex::AddS(matrix[12 + i], matrix[i]);
        diffX[i] = nGZCompGeom::convex::SubS(matrix[i], matrix[12 + i]);
        sumY[i] = nGZCompGeom::convex::AddS(matrix[4 + i], matrix[12 + i]);
        diffY[i] = nGZCompGeom::convex::SubS(matrix[4 + i], matrix[12 + i]);
    }
    float p0[2], p1[2], p2[2], p3[2];
    if (!ClipPlanesMeet(sumX, sumY, p0) || !ClipPlanesMeet(diffX, sumY, p1) || !ClipPlanesMeet(sumX, diffY, p2) ||
        !ClipPlanesMeet(diffX, diffY, p3))
        return 0;
    for (int axis = 0; axis < 2; ++axis) {
        if (p2[axis] > p0[axis]) {
            if (p1[axis] > p0[axis]) {
                outMin[axis] = p0[axis];
                outMax[axis] = p3[axis];
            } else {
                outMin[axis] = p1[axis];
                outMax[axis] = p2[axis];
            }
        } else if (p3[axis] > p2[axis]) {
            outMin[axis] = p2[axis];
            outMax[axis] = p1[axis];
        } else {
            outMin[axis] = p3[axis];
            outMax[axis] = p0[axis];
        }
    }
    return 1;
}

void __cdecl ObjectBounds(float *box, int32_t mask, cIGZUnknown *node)
{
    using namespace nGZSceneGraph;
    auto *const shapes = reinterpret_cast<cIShapeRefNode *>(node);
    // receiver: the node (cIShapeRefNode: TestFlag 48, IsEverywhere 70, AnyShapeSlot52 97)
    if (shapes->TestFlag(4) && !shapes->IsEverywhere() && shapes->AnyShapeSlot52()) {
        const cBoundBox own = shapes->GetBoundBox(); // receiver: cIShapeRefNode
        nGZCompGeom::BoundBox grown;
        std::memcpy(&grown, &own, sizeof(grown));
        reinterpret_cast<nGZCompGeom::BoundBox *>(box)->Grow(grown);
    }
    const cChildRange children = shapes->GetChildren(); // receiver: cICompositionTreeNode
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd;
         child = child->GetNextSibling()) { // receiver: the child (cICompositionTreeNode)
        if (mask < 0 || !child->TestFlag(uint32_t(mask))) // receiver: the child (cICompositionTreeNode)
            ObjectBounds(box, mask, reinterpret_cast<cIGZUnknown *>(child));
    }
}

const char *__cdecl FindSubsetMaterial(cIGZUnknown *node, const char *subset)
{
    using namespace nGZSceneGraph;
    cIShapeRefNode *holder = nullptr;
    const char *found = nullptr;
    if (node->QueryInterface(kIidShapeRefNode, reinterpret_cast<void **>(&holder))) { // receiver: the node (slot 0)
        const cShapeRange shapes = holder->GetShapes(); // receiver: cIShapeRefNode
        for (cIShape **shape = shapes.mFirst; shape != shapes.mEnd; ++shape) {
            // receiver: the shape (cIShape)
            found = reinterpret_cast<const char *>(static_cast<uintptr_t>((*shape)->MaterialNameOf(subset)));
            if (found) break;
        }
    }
    if (!found) {
        const cChildRange children = TreeNode(node)->GetChildren(); // receiver: cICompositionTreeNode
        for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd;
             child = child->GetNextSibling()) { // receiver: the child (cICompositionTreeNode)
            found = FindSubsetMaterial(reinterpret_cast<cIGZUnknown *>(child), subset);
            if (found) break;
        }
    }
    if (holder) holder->Release(); // receiver: cIShapeRefNode (slot 2)
    return found;
}

const char *__cdecl SubsetMaterial(cIGZUnknown *node, const char *subset)
{
    const char *const found = FindSubsetMaterial(node, subset);
    if (found && _stricmp(found, kShadowOff) == 0) {
        RawSlot<124>(node); // receiver: the node (slot 124; its answer unused)
        return nullptr;
    }
    return found;
}

namespace {

// A mesh's elements of one subset, as a counted cursor {walker, mark, end}: the walker's slot 4 tells the end, slot 5
// the element's index, slot 6 steps. Each word is released at the end, the last first.
struct SubsetCursor {
    cIGZUnknown *mWalker;
    cIGZUnknown *mMark;
    cIGZUnknown *mEnd;
};

void GrowBy(float *box, const float *element)
{
    nGZCompGeom::BoundBox grown;
    std::memcpy(&grown, element, sizeof(grown));
    reinterpret_cast<nGZCompGeom::BoundBox *>(box)->Grow(grown);
}

} // namespace

void __cdecl SubsetBounds(cIGZUnknown *node, const char *subset, float *out)
{
    using namespace nGZSceneGraph;
    out[0] = out[1] = out[2] = FLT_MAX;
    out[3] = out[4] = out[5] = -FLT_MAX;
    const cShapeRange shapes = reinterpret_cast<cIShapeRefNode *>(node)->GetShapesConst(); // receiver: cIShapeRefNode
    for (cIShape **shape = shapes.mFirst; shape != shapes.mEnd; ++shape) {
        const uint32_t lod = (*shape)->GetShownLod(); // receiver: the shape (cIShape)
        // receiver: the shape (cIShape, slot 47: the part's object at the level)
        void *const mesh = reinterpret_cast<void *>(static_cast<uintptr_t>((*shape)->PartObjectAtLod(lod)));
        if (!mesh) continue;
        void *const geometry = RawSlot<29, void *>(mesh);                       // receiver: the part's mesh (slot 29)
        const SubsetCursor cursor = RawSlot<44, SubsetCursor>(mesh, subset);    // receiver: the part's mesh (slot 44)
        while (!RawSlot<4, uint8_t>(cursor.mWalker, cursor.mEnd)) {               // receiver: the cursor's walker (slot 4)
            const uint32_t index = RawSlot<5, uint32_t>(cursor.mWalker);        // receiver: the cursor's walker (slot 5)
            const uint8_t *const element = RawSlot<5, const uint8_t *>(geometry, index); // receiver: the geometry (slot 5)
            GrowBy(out, reinterpret_cast<const float *>(element + 0x1c));
            RawSlot<6>(cursor.mWalker); // receiver: the cursor's walker (slot 6: the next element)
        }
        if (cursor.mEnd) cursor.mEnd->Release();       // receiver: the cursor's end (cIGZUnknown, slot 2)
        if (cursor.mMark) cursor.mMark->Release();     // receiver: the cursor's mark (cIGZUnknown, slot 2)
        if (cursor.mWalker) cursor.mWalker->Release(); // receiver: the cursor's walker (cIGZUnknown, slot 2)
    }
}

void __cdecl SubsetWorldBounds(float *box, const char *subset, cIGZUnknown *node)
{
    using namespace nGZSceneGraph;
    auto *const shapes = reinterpret_cast<cIShapeRefNode *>(node);
    // receiver: the node (cIShapeRefNode: TestFlag 48, IsEverywhere 70, AnyShapeSlot52 97)
    if (shapes->TestFlag(4) && !shapes->IsEverywhere() && shapes->AnyShapeSlot52()) {
        nGZCompGeom::ConvexBox own;
        SubsetBounds(node, subset, reinterpret_cast<float *>(&own));
        const float *const b = reinterpret_cast<const float *>(&own);
        if (!(b[0] > b[3]) && !(b[1] > b[4]) && !(b[2] > b[5])) {
            const cRigidTransform world = shapes->GetWorldTransform(); // receiver: cICompositionTreeNode
            nGZCompGeom::RigidTransform3 placement;
            std::memcpy(&placement, &world, sizeof(placement));
            own.TransformRigid(&placement);
            reinterpret_cast<nGZCompGeom::BoundBox *>(box)->Grow(own);
        }
    }
    const cChildRange children = shapes->GetChildren(); // receiver: cICompositionTreeNode
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd;
         child = child->GetNextSibling()) { // receiver: the child (cICompositionTreeNode)
        SubsetWorldBounds(box, subset, reinterpret_cast<cIGZUnknown *>(child));
    }
}

void __cdecl TileSpan(const float *box, int32_t *x0, int32_t *x1, int32_t *y0, int32_t *y1)
{
    *x0 = RoundToTile(box[0]);
    *x1 = RoundToTile(box[3]);
    *y0 = RoundToTile(box[1]);
    const int32_t top = RoundToTile(box[4]);
    *y1 = top;
    const int32_t left = *x0, right = *x1;
    if (left != right) return;
    const int32_t bottom = *y0;
    if (bottom != top) return;
    if (0.01f > std::fabs(box[0] - box[3])) {
        // Thin in x: widened in y towards the end further from its tile.
        if (std::fabs(box[4] - float(top)) > std::fabs(box[1] - float(bottom)))
            *y1 = top + 1;
        else
            *y0 = bottom - 1;
    } else if (0.01f > std::fabs(box[1] - box[4])) {
        if (std::fabs(box[3] - float(right)) > std::fabs(box[0] - float(left)))
            *x1 = right + 1;
        else
            *x0 = left - 1;
    }
}

uint8_t __cdecl FloorTest(cIGZUnknown *node, uint32_t subset)
{
    void *const owner = RawSlot<120, void *>(node); // receiver: the node (slot 120: its owner)
    float position[6];
    const float height = RawSlot<42, const float *>(owner, position)[2]; // receiver: the owner (slot 42: its position)
    if (!RawSlot<22, uint8_t>(LevelShown(), height)) return 0; // receiver: the level service 0x20df51 (slot 22)
    // receiver: the cached singleton B (slot 28, no arguments), asked twice
    if (RawSlot<28, uint32_t>(GetCachedSingletonB()) != 2 && RawSlot<28, uint32_t>(GetCachedSingletonB()) != 3) return 0;
    float box[6] = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX};
    SubsetWorldBounds(box, kSubsetNames[subset], node);
    if (box[0] > box[3] || box[1] > box[4] || box[2] > box[5]) return 0;
    int32_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    TileSpan(box, &x0, &x1, &y0, &y1);
    if (x0 == x1 && y0 == y1) return 0;
    void *const walls = RawSlot<39, void *>(Placement()); // receiver: placement (slot 39)
    float edge[6];
    edge[2] = edge[5] = height;
    if (x0 == x1) {
        for (int32_t y = y0; y < y1; ++y) {
            edge[0] = float(x0), edge[1] = float(y);
            edge[3] = float(x0), edge[4] = float(y + 1);
            if (!RawSlot<44, uint8_t>(walls, edge)) return 0; // receiver: placement's slot-39 object (slot 44)
        }
    } else {
        for (int32_t x = x0; x < x1; ++x) {
            edge[0] = float(x + 1), edge[1] = float(y0);
            edge[3] = float(x), edge[4] = float(y0);
            if (!RawSlot<44, uint8_t>(walls, edge)) return 0; // receiver: placement's slot-39 object (slot 44)
        }
    }
    return 1;
}

uint8_t __cdecl PointInView(cIGZUnknown *viewer, const float *point)
{
    float clip[3];
    ViewerNode(viewer)->WorldToClip(clip, point, 0); // receiver: nGZSceneGraph::cIViewerRefNode
    if (!(clip[0] >= -1.0f) || !(1.0f >= clip[0])) return 0;
    if (!(clip[1] >= -1.0f) || !(1.0f >= clip[1])) return 0;
    if (!(clip[2] >= 0.0f) || !(1.0f >= clip[2])) return 0;
    return 1;
}

// The masks swap places at each level, so the viewer (taking every bit away, then giving back the group's) gets the group
// alone while its children have the group taken away and every other bit given.
void __cdecl SwapLayerBitsDeep(cIGZUnknown *object, uint32_t remove, uint32_t add)
{
    auto *const node = reinterpret_cast<nGZSceneGraph::cIRenderableNode *>(object);
    if (node->TestFlag(5)) {           // receiver: nGZSceneGraph::cIRenderableNode (cICompositionTreeNode slot 48)
        node->RemoveLayerBits(remove); // receiver: nGZSceneGraph::cIRenderableNode
        node->AddLayerBits(add);       // receiver: nGZSceneGraph::cIRenderableNode
    }
    const nGZSceneGraph::cChildRange children = node->GetChildren(); // receiver: nGZSceneGraph::cIRenderableNode
    for (nGZSceneGraph::cICompositionTreeNode *child = children.mCurrent; child != children.mEnd;
         child = child->GetNextSibling()) { // receiver: nGZSceneGraph::cICompositionTreeNode
        // ORIGINAL-BUG: the recursion passes (add, remove), so the masks swap at every level of the tree.
        SwapLayerBitsDeep(child, add, remove);
    }
}

int32_t __cdecl AddShadowMaterial(const char *name, const char *texture, const char *shader, uint8_t debug)
{
    nGZSceneGraph::cIMaterialDefinition *material = nullptr;
    // receiver: the director (cIGZCOM); its answer unused (the original calls on through the out unchecked)
    Director()->GetClassObject(nGZSceneGraph::cMaterialDefinition::kClassId, nGZSceneGraph::cIMaterialDefinition::kIid,
                               reinterpret_cast<void **>(&material));
    material->SetMaterialName(name);                         // receiver: nGZSceneGraph::cIMaterialDefinition
    material->SetMaterialType(shader);                       // receiver: nGZSceneGraph::cIMaterialDefinition
    material->SetProperty("stdMatBaseTextureName", texture); // receiver: nGZSceneGraph::cIMaterialDefinition
    material->SetProperty("debugMode", debug ? "1" : "0");   // receiver: nGZSceneGraph::cIMaterialDefinition
    auto *const manager = static_cast<nGZSceneGraph::cIMaterialManager *>(GetCachedQueriedObject());
    const uint32_t index = manager->AddMaterial(material, 2); // receiver: nGZSceneGraph::cIMaterialManager
    if (material) material->Release();                        // receiver: nGZSceneGraph::cIMaterialDefinition
    return int32_t(index);
}

// ---- cObjectShadowFilter --------------------------------------------------------------------------------------------

cObjectShadowFilter::~cObjectShadowFilter() = default;

bool cObjectShadowFilter::QueryInterface(uint32_t riid, void **out) { return rz::cRZUnknown::QueryInterface(riid, out); }
uint32_t cObjectShadowFilter::AddRef() { return rz::cRZUnknown::AddRef(); }
uint32_t cObjectShadowFilter::Release() { return rz::cRZUnknown::Release(); }

bool cObjectShadowFilter::Accept(nGZSceneGraph::cIBoundedNode *node, void *)
{
    if (node->TestFlag(8)) return false; // receiver: nGZSceneGraph::cIBoundedNode
    nGZSceneGraph::cICompositionTreeNode *at = node;
    do {
        // The node's own bits each time round (not the parent's).
        if (node->TestFlag(5)) { // receiver: nGZSceneGraph::cIBoundedNode
            uint32_t state[2];
            // receiver: the node (nGZSceneGraph::cIRenderableNode)
            if ((*reinterpret_cast<nGZSceneGraph::cIRenderableNode *>(node)->GetLayerState(state) & mMask) == 0)
                return false;
        }
        if (at->TestFlag(0xf)) {                                // receiver: nGZSceneGraph::cICompositionTreeNode
            if (void *const owner = RawSlot<120, void *>(at)) { // receiver: the node (slot 120: its owner)
                if (uint16_t(RawSlot<47, uint32_t>(owner)) == mOwnerId) return true; // receiver: the owner (slot 47)
            }
        }
        at = at->GetParent(); // receiver: nGZSceneGraph::cICompositionTreeNode
    } while (at);
    return false;
}

// ---- life cycle -----------------------------------------------------------------------------------------------------

ShadowObjectRef::~ShadowObjectRef()
{
    if (p) VirtualBase(p)->Release(); // receiver: the object's cIGZUnknown virtual base (slot 2)
}

cShadow::cShadow() = default;

cShadow::~cShadow()
{
    if (mStarted) Shutdown();
}

cShadow *__cdecl cShadow::Create() { return new (std::nothrow) cShadow; }

__declspec(noinline) cShadow *__fastcall cShadow::Construct(void *memory, void *) { return ::new (memory) cShadow; }

bool cShadow::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != 1 && iid != kIid) return false;
    *out = this;
    // Through the object's own table, as the original (the class is final: a plain call would bind directly).
    using AddRefSlot = uint32_t(__thiscall *)(void *);
    (*reinterpret_cast<AddRefSlot *const *>(this))[1](this); // receiver: this (slot 1, AddRef)
    return true;
}

uint32_t cShadow::AddRef() { return ++mRefCount; }

uint32_t cShadow::Release()
{
    if (--mRefCount) return mRefCount;
    // Through the object's own table (slot 23, flags 1), as the original.
    using DeletingDestructor = void *(__thiscall *)(void *, uint32_t);
    (*reinterpret_cast<DeletingDestructor *const *>(this))[23](this, 1u); // receiver: this (slot 23)
    return 0;
}

// ---- slots ----------------------------------------------------------------------------------------------------------

bool cShadow::Init()
{
    if (mStarted) return false;
    mStarted = 1;
    mShown = 0;
    mEnabled = 0;
    mResourcesMade = 0;
    NegatedCameraPosition(Camera(CameraOwner()), mCamera);
    mChangeId = *ShadowManager(CameraOwner())->ChangeCount(); // receiver: cIShadowManager
    return true;
}

bool cShadow::Shutdown()
{
    if (!mStarted) return false;
    mStarted = 0;
    ReleaseResources();
    UnbindMaterials();
    if (cIGZUnknown *const object = mObject.p) {
        mObject.p = nullptr;
        VirtualBase(object)->Release(); // receiver: the object's cIGZUnknown virtual base (slot 2)
    }
    return true;
}

void cShadow::SetObject(cIGZUnknown *object)
{
    cIGZUnknown *const old = mObject.p;
    if (object != old) {
        if (object) VirtualBase(object)->AddRef(); // receiver: the new object's cIGZUnknown virtual base (slot 1)
        mObject.p = object;
        if (old) VirtualBase(old)->Release(); // receiver: the old object's cIGZUnknown virtual base (slot 2)
    }
    BindMaterials();
    ApplyMaterials();
    UpdateLevel();
    UpdateVisibility();
    UpdateSettings();
}

void cShadow::SetMode(int32_t mode)
{
    if (mRequestedMode == mode) return;
    mRequestedMode = mode;
    SelectMode(1);
}

void cShadow::SetEnabled(uint8_t on)
{
    if (mEnabled == on) return;
    mEnabled = on;
    UpdateVisibility();
    ApplyMaterials();
}

void cShadow::Suspend()
{
    Hide();
    if (mResourcesMade && mMode == 2) ReleaseResources();
    UnbindMaterials();
}

void cShadow::Resume()
{
    BindMaterials();
    ApplyMaterials();
    Show();
}

void cShadow::Show()
{
    if (mShown) return;
    mShown = 1;
    if (mNode.p && !TreeNode(mNode.p)->GetParent()) { // receiver: our node (cICompositionTreeNode)
        void *const root = RawSlot<17, void *>(Scene()); // receiver: the scene (slot 17: its root)
        TreeNode(root)->AddChild(TreeNode(mNode.p));      // receiver: the scene's root (cICompositionTreeNode)
    }
    UpdateVisibility();
}

void cShadow::Hide()
{
    if (!mShown) return;
    mShown = 0;
    if (mNode.p && TreeNode(mNode.p)->GetParent()) {                       // receiver: our node (cICompositionTreeNode)
        TreeNode(mNode.p)->GetParent()->RemoveChild(TreeNode(mNode.p)); // receiver: our node, then its parent
    }
    if (mProjector.p) RawSlot<3>(mProjector.p, uint8_t(0));           // receiver: the projector (slot 3: on)
    if (mFloorProjector.p) RawSlot<3>(mFloorProjector.p, uint8_t(0)); // receiver: the projector (slot 3: on)
}

void cShadow::Update()
{
    if (!mObject.p || !mShown) return;
    if (!mEnabled && (mRequestedMode != 1 || mResourcesMade)) return;
    SelectMode(0);
    if (!mResourcesMade) {
        CreateResources();
        if (!mResourcesMade) return;
        UpdateVisibility();
        UpdateSettings();
        PlaceProjectors();
        ApplyMaterials();
    }
    if (!ObjectVisible()) return;

    if (mMode == 2) {
        cIShadowManager *const manager = ShadowManager(CameraOwner());
        void *const owner = RawSlot<120, void *>(mObject.p); // receiver: the object (slot 120)
        mOwner = RawSlot<43, int32_t>(owner);                 // receiver: the object's slot-120 answer (slot 43)
        if (mLastOwner != mOwner) {
            mLastOwner = mOwner;
            if (mShadowIndex != -1) manager->ReleaseShadow(mShadowIndex); // receiver: cIShadowManager
            mShadowIndex = manager->AcquireShadow(mObject.p, mLastOwner); // receiver: cIShadowManager
            mMatrixStamp = 0;
            if (mShadowIndex != -1) {
                if (const uint32_t texture = manager->ShadowTexture(mShadowIndex)) { // receiver: cIShadowManager
                    if (mProjector.p) RawSlot<7>(mProjector.p, texture);           // receiver: the projector (slot 7)
                    if (mFloorProjector.p) RawSlot<7>(mFloorProjector.p, texture); // receiver: the projector (slot 7)
                }
            }
            UpdateVisibility();
        }
        if (mShadowIndex != -1) {
            void *const bone = FindRootBone(mObject.p);
            float relative[7];
            if (bone) {
                float out[7];
                std::memcpy(relative, RelativeTransform(out, bone, mObject.p), sizeof(relative));
                bool changed = !mBoneValid || !NearlyEqual(mBone[0], relative[0]) ||
                               !NearlyEqual(mBone[1], relative[1]) || !NearlyEqual(mBone[2], relative[2]);
                if (!changed) {
                    // The rotation matches as it is or negated (the same rotation).
                    const bool same = NearlyEqual(mBone[3], relative[3]) && NearlyEqual(mBone[4], relative[4]) &&
                                      NearlyEqual(mBone[5], relative[5]) && NearlyEqual(mBone[6], relative[6]);
                    if (!same)
                        changed = !(NearlyEqual(mBone[3], -relative[3]) && NearlyEqual(mBone[4], -relative[4]) &&
                                    NearlyEqual(mBone[5], -relative[5]) && NearlyEqual(mBone[6], -relative[6]));
                }
                if (changed) {
                    std::memcpy(mBone, relative, sizeof(mBone));
                    mBoneValid = 1;
                    mMatrixStamp = 0;
                }
            }
            float projection[16];
            if (manager->GetShadowMatrix(mShadowIndex, projection, &mMatrixStamp)) { // receiver: cIShadowManager
                // receiver: the object (cICompositionTreeNode)
                const nGZSceneGraph::cRigidTransform world = TreeNode(mObject.p)->GetWorldTransform();
                nGZCompGeom::RigidTransform3 placed;
                std::memcpy(&placed, &world, sizeof(placed));
                const float origin[3] = {world.mValues[0], world.mValues[1], world.mValues[2]};
                TerrainQuery(&placed);
                if (bone) {
                    const cShadowBox box = manager->GetShadowBox(mShadowIndex); // receiver: cIShadowManager
                    float offset[3] = {relative[0] - box.mValues[0], relative[1] - box.mValues[1],
                                       relative[2] - box.mValues[2]};
                    simgeom::RotateByQuaternion(offset, placed.rotation);
                    placed.translation.x += offset[0];
                    placed.translation.y += offset[1];
                    placed.translation.z += offset[2];
                }
                nGZCompGeom::RigidTransform3 lifted;
                lifted.translation.x = placed.translation.x - 0.0f;
                lifted.translation.y = placed.translation.y - 0.0f;
                lifted.translation.z = placed.translation.z - origin[2];
                static constexpr float kIdentity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                std::memcpy(placed.rotation, kIdentity, sizeof(kIdentity));
                std::memcpy(lifted.rotation, kIdentity, sizeof(kIdentity));
                float toPlaced[16], toLifted[16], placedProjection[16], liftedProjection[16];
                placed.Invert()->ToMatrix4(toPlaced);
                lifted.Invert()->ToMatrix4(toLifted);
                nGZCompGeom::Matrix4Multiply(placedProjection, toPlaced, projection);
                nGZCompGeom::Matrix4Multiply(liftedProjection, toLifted, projection);
                float low[2], high[2];
                if (ShadowFootprint(liftedProjection, low, high)) {
                    float scaleOffset[2] = {0.0f, 0.0f};
                    float scale[2] = {1.0f, 1.0f};
                    manager->GetShadowScale(mShadowIndex, scaleOffset, scale); // receiver: cIShadowManager
                    float bounds[6] = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX};
                    ObjectBounds(bounds, -1, mObject.p);
                    // The rectangle kept to the object's side away from the camera.
                    if (0.0f > mCamera[0])
                        low[0] = bounds[0] > low[0] ? bounds[0] : low[0];
                    else
                        high[0] = high[0] > bounds[3] ? bounds[3] : high[0];
                    if (0.0f > mCamera[1])
                        low[1] = bounds[1] > low[1] ? bounds[1] : low[1];
                    else
                        high[1] = high[1] > bounds[4] ? bounds[4] : high[1];
                    const float rect[4] = {low[0], high[0], low[1], high[1]};
                    if (mProjector.p) {
                        // receiver: the projector (slot 9: the rectangle, the projection, the scale)
                        RawSlot<9>(mProjector.p, rect, static_cast<const float *>(placedProjection),
                                   static_cast<const float *>(scaleOffset), static_cast<const float *>(scale), 0u);
                        int32_t ground[3];
                        worldmath::TileQuadrant tile;
                        worldmath::PositionToTileQuadrant(
                            &tile, reinterpret_cast<const float *>(worldmath::WithTileValue(
                                       ground, reinterpret_cast<const int32_t *>(origin))));
                        RawSlot<8>(mProjector.p, uint32_t(QuadrantTest(&tile))); // receiver: the projector (slot 8)
                        TellView(mProjector.p, Camera(CameraOwner()));
                    }
                    if (mFloorProjector.p) {
                        // receiver: the projector (slot 9: the rectangle, the projection, the scale)
                        RawSlot<9>(mFloorProjector.p, rect, static_cast<const float *>(placedProjection),
                                   static_cast<const float *>(scaleOffset), static_cast<const float *>(scale), 0u);
                        TellView(mFloorProjector.p, Camera(CameraOwner()));
                        // receiver: placement (slot 72: a point inside the lot), twice
                        const bool inside = RawSlot<72, uint8_t>(Placement(), low[0], low[1]) &&
                                            RawSlot<72, uint8_t>(Placement(), high[0], high[1]);
                        if (!inside) RawSlot<3>(mFloorProjector.p, uint8_t(mGroundLevel == 0)); // receiver: slot 3
                    }
                }
            }
        }
    }

    if (!mSettingsDone) UpdateSettings();
    void *const camera = Camera(CameraOwner());
    float position[3];
    NegatedCameraPosition(camera, position);
    const float dx = position[0] - mCamera[0], dy = position[1] - mCamera[1], dz = position[2] - mCamera[2];
    if (double(dy * dy + dx * dx + dz * dz) > 1e-6) {
        mCamera[0] = position[0];
        mCamera[1] = position[1];
        mCamera[2] = position[2];
        if (FollowsCamera()) {
            UpdateSettings();
            PlaceProjectors();
            if (mProjector.p) TellView(mProjector.p, camera);
            if (mFloorProjector.p) TellView(mFloorProjector.p, camera);
        }
    } else if (mMode == 1 || mMode == 4) {
        PlaceProjectors();
    }
}

void cShadow::Refresh()
{
    if (!mObject.p || !mShown || !mEnabled) return;
    SelectMode(1);
    UpdateLevel();
    UpdateVisibility();
    UpdateSettings();
    PlaceProjectors();
    ApplyMaterials();
    if (!mGrid.p || !mProjector.p || mLevel == mGroundLevel) return;
    mLevel = mGroundLevel;
    RawSlot<9>(mGrid.p, mProjector.p); // receiver: the grid (slot 9: a projector out)
    cIGZUnknown *grid;
    if (mLevel == 0) {
        grid = RawSlot<45, cIGZUnknown *>(CameraOwner(), uint32_t(mMode == 2 ? 1 : 0)); // receiver: the camera's owner
    } else {
        void *const levels = RawSlot<38, void *>(CameraOwner()); // receiver: the camera's owner (slot 38)
        void *const level = RawSlot<19, void *>(levels, mLevel); // receiver: slot 38's answer (slot 19: a level)
        grid = RawSlot<25, cIGZUnknown *>(level);                // receiver: the level (slot 25: its grid)
    }
    Assign(mGrid, grid);
    RawSlot<6>(mGrid.p, mProjector.p); // receiver: the grid (slot 6: a projector in)
}

void cShadow::Slot14() {}

void cShadow::ApplyMaterials()
{
    if (!mObject.p || !mMaterialsBound || mRequestedMode == 1) return;
    void *const owner = RawSlot<120, void *>(mObject.p); // receiver: the object (slot 120)
    if (RawSlot<13, uint8_t>(owner, 0x22u) & 1) return;  // receiver: the object's slot-120 answer (slot 13)
    if (mMode == 3) {
        const uint32_t options = ShadowManager(CameraOwner())->ChangeCount()[11]; // receiver: cIShadowManager
        const uint8_t visible = (options & kOptionVisible) ? 1 : 0;
        const bool allSubsets = (options & kOptionAllSubsets) != 0;
        if (mEnabled || allSubsets) {
            if (allSubsets) {
                for (uint32_t i = 0; i < kSubsets; ++i)
                    if (mMaterials[i].mString.size()) BindSubset(i, 1, visible);
                return;
            }
            if (mMaterials[0].mString.size()) BindSubset(0, 1, visible);
            if (mMaterials[1].mString.size()) {
                // receiver: the cached singleton B (slot 28, no arguments: the visible byte pushed before it is
                // BindSubset's third)
                const uint32_t on = RawSlot<28, uint32_t>(GetCachedSingletonB());
                BindSubset(1, on != 0, visible);
            }
            for (uint32_t i = 2; i < kSubsets; ++i)
                if (mMaterials[i].mString.size()) BindSubset(i, FloorTest(mObject.p, i), visible);
            return;
        }
    }
    for (uint32_t i = 0; i < kSubsets; ++i)
        if (mMaterials[i].mString.size()) BindSubset(i, 0, 0);
}

void cShadow::RestoreMaterials()
{
    for (uint32_t i = 0; i < kSubsets; ++i)
        if (mMaterials[i].mString.size()) BindSubset(i, 1, 0);
}

void cShadow::ApplyMaterialsAgain() { cShadow::ApplyMaterials(); }

uint8_t cShadow::IsShown() { return mShown; }
uint8_t cShadow::IsEnabled() { return mEnabled; }
int32_t cShadow::RequestedMode() { return mRequestedMode; }
int32_t cShadow::Mode() { return mMode; }

bool cShadow::HasMaterial(uint32_t subset) { return mMaterials[subset].mString.size() != 0; }

// ---- helpers --------------------------------------------------------------------------------------------------------

bool cShadow::CreateResources()
{
    void *const owner = CameraOwner();
    const ShadowSettings &settings = Settings(ShadowManager(owner)->ChangeCount()); // receiver: cIShadowManager
    const uint8_t debug = uint8_t(settings.options >> 8 & 1);
    LocalString name;
    if (mMode != 4 && mMode != 1) {
        mSettingsDone = 1;
    } else {
        name.s.Sprintf("shadowTexture%x", uint32_t(reinterpret_cast<uintptr_t>(this)));
        // The game's operator new (0x824632) throws rather than return null, so the original's null test is dead.
        Assign(mTarget, ConstructRenderTarget(::operator new(0x18)));
        // receiver: the render target (nTSSG::cBlurredRenderTarget slot 6: its texture, by name and size)
        RawSlot<6>(mTarget.p, name.Chars(), settings.resolution, settings.resolution);
        if (TargetIndex(mTarget.p) == -1) {
            RawSlot<7>(mTarget.p); // receiver: the render target (nTSSG::cBlurredRenderTarget slot 7)
            Drop(mTarget);
            return false;
        }

        LocalRef viewer, node, holder;
        Director()->GetClassObject(kViewerClass, kViewerIid, viewer.Out());     // receiver: the director (cIGZCOM)
        Director()->GetClassObject(kTransformClass, kTransformIid, node.Out()); // receiver: the director (cIGZCOM)
        nGZSceneGraph::cIViewerRefNode *const view = ViewerNode(viewer.p);
        view->ResizeEntries(1);                                   // receiver: nGZSceneGraph::cIViewerRefNode
        view->Slot94(uint32_t(TargetIndex(mTarget.p)));           // receiver: nGZSceneGraph::cIViewerRefNode
        view->Slot100(mTarget.p);                                 // receiver: nGZSceneGraph::cIViewerRefNode
        view->SetOrtho(0, -2.0f, 2.0f, -2.0f, 2.0f, -6.0f, 1.0f); // receiver: nGZSceneGraph::cIViewerRefNode
        view->SetBoundFlag(true);                                 // receiver: nGZSceneGraph::cIViewerRefNode
        view->Slot97(1);                                          // receiver: nGZSceneGraph::cIViewerRefNode
        Assign(mNode, node.p);
        Assign(mViewer, viewer.p);

        // receiver: nGZSceneGraph::cIRenderGroupRegistry
        const uint32_t bit = nGZSceneGraph::RenderGroupRegistry()->BitOfName(mMode == 1 ? "Sims" : "Props");
        const uint32_t mask = 1u << (bit & 31);
        auto *const filter = new cObjectShadowFilter(mask);
        void *const objectOwner = RawSlot<120, void *>(mObject.p);       // receiver: the object (slot 120)
        filter->mOwnerId = uint16_t(RawSlot<47, uint32_t>(objectOwner)); // receiver: the object's slot-120 answer
        view->Slot103(static_cast<nGZSceneGraph::cIVisibilityQueryFilter *>(filter)); // receiver: cIViewerRefNode
        SwapLayerBitsDeep(mViewer.p, 0xffffffffu, mask);

        Director()->GetClassObject(kTransformClass, kTransformIid, holder.Out()); // receiver: the director (cIGZCOM)
        TreeNode(holder.p)->AddChild(TreeNode(viewer.p)); // receiver: nGZSceneGraph::cICompositionTreeNode
        const float lift[3] = {0.0f, 0.125f, 0.0f};
        TransformNode(holder.p)->SetTranslation(lift);  // receiver: nGZSceneGraph::cITransformNode
        TreeNode(node.p)->AddChild(TreeNode(holder.p)); // receiver: nGZSceneGraph::cICompositionTreeNode

        if (mMode == 1) {
            using namespace nGZCompGeom::convex;
            const float size = settings.size;
            const float across = AddS(size, size);
            const float along = float(double(size) * 1.25);
            const uint32_t resolution = settings.resolution;
            const uint32_t inner = resolution - 2 * uint32_t(settings.blurRadius);
            const float ratio = DivS(float(double(inner)), float(double(resolution)));
            const float half = float(double(ratio) * 0.5);

            LocalRef shapeRef, shape, geometry;
            // receiver: the director (cIGZCOM)
            Director()->GetClassObject(kShapeRefClass, nGZSceneGraph::cIShapeRefNode::kIid, shapeRef.Out());
            Director()->GetClassObject(kShapeClass, nGZSceneGraph::cIShape::kIid, shape.Out()); // receiver: cIGZCOM
            const float extent[2] = {MulS(across, ratio), MulS(along, ratio)};
            const float corner[3] = {0.0f, 0.0f, 0.0f};
            const float high = AddS(half, 0.5f), low = SubS(0.5f, half);
            const float uvLow[2] = {low, low}, uvHigh[2] = {high, high};
            MakeQuadGeometry(geometry.Out(), corner, extent, uvLow, uvHigh);
            auto *const shapes = reinterpret_cast<nGZSceneGraph::cIShapeRefNode *>(shapeRef.p);
            auto *const quad = reinterpret_cast<nGZSceneGraph::cIShape *>(shape.p);
            shapes->AddShape(quad);       // receiver: nGZSceneGraph::cIShapeRefNode
            quad->AddPart(geometry.p, 0); // receiver: nGZSceneGraph::cIShape
            shapes->AddLayer("Sims");     // receiver: nGZSceneGraph::cIShapeRefNode
            LocalString material;
            material.s.Sprintf("shadowMaterial%x", uint32_t(reinterpret_cast<uintptr_t>(this)));
            const nGZSceneGraph::ShapeMaterialHandle handle(
                AddShadowMaterial(material.Chars(), name.Chars(), "ShadowShader", debug));
            quad->BindMaterial(material.Chars(), "layer"); // receiver: nGZSceneGraph::cIShape
            Assign(mDrawable, shapeRef.p);
            // receiver: nGZSceneGraph::cIViewerRefNode
            view->Slot102(reinterpret_cast<nGZSceneGraph::cIBoundedNode *>(shapeRef.p));
            TreeNode(holder.p)->AddChild(TreeNode(mDrawable.p)); // receiver: nGZSceneGraph::cICompositionTreeNode
        }
        if (mShown) {
            void *const root = RawSlot<17, void *>(Scene()); // receiver: the scene (slot 17: its root)
            TreeNode(root)->AddChild(TreeNode(mNode.p));      // receiver: the scene's root (cICompositionTreeNode)
        }
    }

    if (mMode == 2) {
        mLastOwner = 8;
        mBoneValid = 0;
        name.s.mString.assign("reggrid2");
    }
    if (mMode == 2 || mMode == 4 || mMode == 1) {
        UpdateLevel();
        mLevel = mGroundLevel;
        cIGZUnknown *grid;
        if (mLevel == 0) {
            grid = RawSlot<45, cIGZUnknown *>(owner, uint32_t(mMode == 2 ? 1 : 0)); // receiver: the camera's owner
        } else {
            void *const levels = RawSlot<38, void *>(owner);         // receiver: the camera's owner (slot 38)
            void *const level = RawSlot<19, void *>(levels, mLevel); // receiver: slot 38's answer (slot 19: a level)
            grid = RawSlot<25, cIGZUnknown *>(level);                // receiver: the level (slot 25: its grid)
        }
        Assign(mGrid, grid);
        const float origin[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const float identity[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        // receiver: the grid (slot 8: a projector on it, by name)
        Assign(mProjector, RawSlot<8, cIGZUnknown *>(mGrid.p, name.Chars(), origin, identity));
        if (debug) {
            RawSlot<6>(mProjector.p, 0u); // receiver: the projector (slot 6: its kind)
        } else if (settings.options >> 5 & 1) {
            const float white[3] = {1.0f, 1.0f, 1.0f};
            RawSlot<5>(mProjector.p, white); // receiver: the projector (slot 5)
            RawSlot<6>(mProjector.p, 6u);    // receiver: the projector (slot 6: its kind)
        } else if (settings.options >> 4 & 1) {
            TellView(mProjector.p, Camera(owner));
            RawSlot<6>(mProjector.p, 5u); // receiver: the projector (slot 6: its kind)
        } else {
            RawSlot<6>(mProjector.p, 2u); // receiver: the projector (slot 6: its kind)
        }
        if (mMode == 2 || ((mMode == 4 || mMode == 1) && (settings.options >> 6 & 1))) {
            cIGZUnknown *const floorGrid = RawSlot<45, cIGZUnknown *>(owner, 8u); // receiver: the camera's owner
            // receiver: the floor grid (slot 8: a projector on it, by name)
            Assign(mFloorProjector, RawSlot<8, cIGZUnknown *>(floorGrid, name.Chars(), origin, identity));
            if (debug) {
                RawSlot<6>(mFloorProjector.p, 0u); // receiver: the projector (slot 6: its kind)
            } else if (settings.options >> 4 & 1) {
                TellView(mFloorProjector.p, Camera(owner));
                RawSlot<6>(mFloorProjector.p, 5u); // receiver: the projector (slot 6: its kind)
            } else {
                RawSlot<6>(mFloorProjector.p, 2u); // receiver: the projector (slot 6: its kind)
            }
        }
    }
    mResourcesMade = 1;
    return true;
}

void cShadow::ReleaseResources()
{
    if (mNode.p && TreeNode(mNode.p)->GetParent()) {                       // receiver: our node (cICompositionTreeNode)
        TreeNode(mNode.p)->GetParent()->RemoveChild(TreeNode(mNode.p)); // receiver: our node, then its parent
    }
    if (mTarget.p) {
        RawSlot<7>(mTarget.p); // receiver: the render target (slot 7)
        Drop(mTarget);
    }
    Drop(mDrawable);
    Drop(mViewer);
    Drop(mNode);
    void *const owner = CameraOwner();
    if (mShadowIndex != -1) {
        ShadowManager(owner)->ReleaseShadow(mShadowIndex); // receiver: cIShadowManager
        mShadowIndex = -1;
    }
    if (mGrid.p && mProjector.p) {
        RawSlot<9>(mGrid.p, mProjector.p); // receiver: the grid (slot 9: a projector out)
        Drop(mProjector);
    }
    Drop(mGrid);
    if (mFloorProjector.p) {
        void *const grid = RawSlot<45, void *>(owner, 8u); // receiver: the camera's owner (slot 45: a grid)
        RawSlot<9>(grid, mFloorProjector.p);              // receiver: that grid (slot 9: a projector out)
        Drop(mFloorProjector);
    }
    mResourcesMade = 0;
}

void cShadow::UnbindMaterials()
{
    if (!mMaterialsBound) return;
    for (uint32_t i = 0; i < kSubsets; ++i) {
        if (mMaterials[i].mString.size()) {
            BindSubset(i, 1, 0);
            mMaterials[i].mString.clear();
        }
    }
    mMaterialsBound = 0;
}

void cShadow::BindMaterials()
{
    if (!mMaterialsBound) {
        for (uint32_t i = 0; i < kSubsets; ++i)
            mMaterials[i].AsInterface()->FromChar(SubsetMaterial(mObject.p, kSubsetNames[i])); // receiver: the string
        mMaterialsBound = 1;
        for (uint32_t i = 2; i < kSubsets; ++i)
            if (mMaterials[i].mString.size()) BindSubset(i, 0, 0);
    }
}

void cShadow::BindSubset(uint32_t index, uint8_t on, uint8_t visible)
{
    const char *const material = !on ? kShadowOff : visible ? kVisibleShadow : mMaterials[index].mString.c_str();
    BindSubsetMaterial(mObject.p, material, kSubsetNames[index]);
}

void cShadow::UpdateLevel()
{
    if (!mObject.p) {
        mGroundLevel = 0;
        return;
    }
    // receiver: the object (cIShapeRefNode: GetBoundBox 72)
    const nGZSceneGraph::cBoundBox box = reinterpret_cast<nGZSceneGraph::cIShapeRefNode *>(mObject.p)->GetBoundBox();
    float centre[3];
    for (int i = 0; i < 3; ++i) centre[i] = (box.mMax[i] - box.mMin[i]) * 0.5f + box.mMin[i];
    int32_t ground[3];
    worldheights::WithLevelOfGround(ground, centre);
    mGroundLevel = ground[2];
}

void cShadow::UpdateVisibility()
{
    const bool visible = ObjectVisible();
    if (mDrawable.p) RawSlot<29>(mDrawable.p, 10u, uint8_t(mEnabled && !visible)); // receiver: the drawable (slot 29)
    const bool on = mEnabled && visible && !(mMode == 2 && mShadowIndex == -1);
    if (mProjector.p) RawSlot<3>(mProjector.p, uint8_t(on)); // receiver: the projector (slot 3: on)
    if (mFloorProjector.p) RawSlot<3>(mFloorProjector.p, uint8_t(on && mGroundLevel == 0)); // receiver: slot 3
}

bool cShadow::SelectMode(uint8_t force)
{
    const int32_t *const record = ShadowManager(CameraOwner())->ChangeCount(); // receiver: cIShadowManager
    const int32_t id = record[0];
    const int32_t seen = mChangeId;
    if (id != seen) mChangeId = id;
    if (!force && id == seen && mResourcesMade) return false;
    const uint32_t options = uint32_t(record[11]);
    int32_t mode = mRequestedMode;
    bool check = false; // mode 3: the object may still refuse it
    if ((options & kOptionMaterials) && (options & kOptionAllSubsets)) {
        mode = 3;
        check = true;
    } else {
        switch (mode) {
        case 1: mode = (options & kOptionModeOne) ? 1 : 0; break;
        case 2:
            if ((options & kOptionModeFour) && (options & kOptionModeTwo)) break;
            if (options & kOptionMaterials) {
                mode = 3;
                check = true;
            } else {
                mode = 0;
            }
            break;
        case 3:
            if (options & kOptionMaterials)
                check = true;
            else
                mode = 0;
            break;
        case 4: mode = (options & kOptionModeFour) ? 4 : 0; break;
        default: break;
        }
    }
    if (check && mObject.p && ObjectVisible()) {
        if (void *const owner = RawSlot<120, void *>(mObject.p)) { // receiver: the object (slot 120)
            const int32_t kind = RawSlot<98, int32_t>(owner);    // receiver: the object's slot-120 answer (slot 98)
            if (kind < 8 || kind > 12) {
                // receiver: the object's slot-120 answer (slots 189 and 144)
                if (!RawSlot<189, uint8_t>(owner) || !RawSlot<144, uint8_t>(owner)) mode = 0;
            }
        }
    }
    if (mode != mMode) {
        mMode = mode;
        ModeChanged();
        return true;
    }
    if (id != seen) ModeChanged();
    return false;
}

void cShadow::ModeChanged()
{
    if (mResourcesMade) {
        ReleaseResources();
        CreateResources();
        UpdateVisibility();
        UpdateSettings();
        PlaceProjectors();
    }
    ApplyMaterials(); // through the table, as the original's tail jump
}

bool cShadow::FollowsCamera() { return mMode == 4 || mMode == 1; }

bool cShadow::ObjectVisible() { return RawSlot<112, uint8_t>(mObject.p) != 0; } // receiver: the object (slot 112)

void cShadow::UpdateSettings()
{
    if (!mViewer.p) return;
    const ShadowSettings &settings = Settings(ShadowManager(CameraOwner())->ChangeCount()); // receiver: cIShadowManager
    if (ObjectVisible()) {
        RawSlot<8>(mTarget.p, settings.outdoorBlur); // receiver: nTSSG::cBlurredRenderTarget (slot 8: the blur radius)
        // The view along the camera's negated position: its yaw about z, then its pitch about x.
        float n[3] = {mCamera[0], mCamera[1], mCamera[2]};
        const float length = float(std::sqrt(double(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])));
        if (length > 0.0f) n[0] /= length, n[1] /= length, n[2] /= length;
        const float negY = -n[1];
        const float yaw = float(std::atan2(double(n[0]), double(negY)));
        const math::Quaternion turn = math::Quaternion::FromAxisHalfAngle(kAxisZ, double(yaw * 0.5f));
        const float pitch = float(std::asin(double(-n[2])));
        math::Quaternion tilt = math::Quaternion::FromAxisHalfAngle(kAxisX, double(pitch * 0.5f));
        const math::Quaternion rotation = *tilt.PreMultiplyNormalised(&turn);
        ViewerNode(mViewer.p)->SetRotation(&rotation.x); // receiver: nGZSceneGraph::cIViewerRefNode
        if (mMode == 1) {
            const float across = float(std::sqrt(double(1.0f - n[2] * n[2])));
            const float m = settings.w1 > settings.w0 ? settings.w1 : settings.w0;
            const float hi1 = across * settings.h1 + n[2] * settings.w1, hi0 = across * settings.h0 + n[2] * settings.w0;
            const float lo1 = across * settings.h1 - n[2] * settings.w1, lo0 = across * settings.h0 - n[2] * settings.w0;
            SetSettingsView(mViewer.p, m, lo1 < lo0 ? lo1 : lo0, hi1 > hi0 ? hi1 : hi0);
            SettingsExtent(settings, -n[0] / n[2], mExtents);
            SettingsExtent(settings, negY / n[2], mExtents + 2);
            mSettingsDone = 1;
            return;
        }
        // The node at the root bone's position (unrotated), the camera fitted to the object.
        void *const bone = FindRootBone(mObject.p);
        nGZSceneGraph::cICompositionTreeNode *const root = TreeNode(bone ? bone : mObject.p);
        const nGZSceneGraph::cRigidTransform where = root->GetWorldTransform(); // receiver: nGZSceneGraph::cICompositionTreeNode
        const float position[3] = {where.mValues[0], where.mValues[1], where.mValues[2]};
        static constexpr float kIdentity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        TransformNode(mNode.p)->SetRotation(kIdentity);   // receiver: nGZSceneGraph::cITransformNode
        TransformNode(mNode.p)->SetTranslation(position); // receiver: nGZSceneGraph::cITransformNode
        FitShadowCamera(mViewer.p, mObject.p);
        if (!mSettingsDone) mSettingsDone = PointInView(mViewer.p, position);
        return;
    }
    RawSlot<8>(mTarget.p, settings.blurRadius); // receiver: nTSSG::cBlurredRenderTarget (slot 8: the blur radius)
    // Straight down: the rotation taking y to -z.
    const float *const a = kAxisZ;
    float n[3] = {a[0], a[1], a[2]};
    const float length = float(std::sqrt(double(a[1] * a[1] + a[0] * a[0] + a[2] * a[2])));
    if (length > 0.0f) n[0] = a[0] / length, n[1] = a[1] / length, n[2] = a[2] / length;
    const float down[3] = {-a[0], -a[1], -a[2]};
    math::Quaternion rotation;
    const float *const turned = &rotation.SetRotationBetween(kAxisY, down)->x;
    ViewerNode(mViewer.p)->SetRotation(turned); // receiver: nGZSceneGraph::cIViewerRefNode
    if (mMode != 1) return;
    const float e = n[2];
    const float m = settings.w1 > settings.w0 ? settings.w1 : settings.w0;
    const float hi1 = settings.w1 * e, hi0 = settings.w0 * e;
    const float lo1 = -settings.w1 * e, lo0 = -settings.w0 * e;
    SetSettingsView(mViewer.p, m, lo1 < lo0 ? lo1 : lo0, hi1 > hi0 ? hi1 : hi0);
    SettingsExtent(settings, -n[0] / e, mExtents);
    SettingsExtent(settings, -n[1] / e, mExtents + 2);
    mSettingsDone = 1;
}

void cShadow::PlaceProjectors()
{
    void *const bone = FindRootBone(mObject.p);
    nGZSceneGraph::cICompositionTreeNode *const root = TreeNode(bone ? bone : mObject.p);
    const bool visible = ObjectVisible();
    if (mNode.p) {
        const nGZSceneGraph::cRigidTransform where = root->GetWorldTransform(); // receiver: nGZSceneGraph::cICompositionTreeNode
        // receiver: nGZSceneGraph::cICompositionTreeNode (the object itself, not its bone)
        const float objectHeight = TreeNode(mObject.p)->GetWorldTransform().mValues[2];
        if (visible) {
            static constexpr float kIdentity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            const float position[3] = {where.mValues[0], where.mValues[1], where.mValues[2]};
            TransformNode(mNode.p)->SetRotation(kIdentity);   // receiver: nGZSceneGraph::cITransformNode
            TransformNode(mNode.p)->SetTranslation(position); // receiver: nGZSceneGraph::cITransformNode
        } else {
            // Turned by the bone's yaw only, just above the object (the game's 0.005 at 0xdba3e8).
            float angles[3];
            const float yaw = QuaternionAngles(angles, where.mValues + 3)[2];
            const math::Quaternion turn = math::Quaternion::FromAxisHalfAngle(kAxisZ, double(yaw * 0.5f));
            TransformNode(mNode.p)->SetRotation(&turn.x); // receiver: nGZSceneGraph::cITransformNode
            const float position[3] = {where.mValues[0], where.mValues[1], objectHeight + 0.005f};
            TransformNode(mNode.p)->SetTranslation(position); // receiver: nGZSceneGraph::cITransformNode
        }
    }
    if ((mMode == 4 || mMode == 1) && mViewer.p && visible) {
        // The viewer's view-projection: the inverse of its view frame, then its projection.
        float projection[16];
        // receiver: nGZSceneGraph::cIViewerRefNode
        std::memcpy(projection, ViewerNode(mViewer.p)->GetEntry(0)->mProjection, sizeof(projection));
        nGZSceneGraph::cRigidTransform buffer;
        const float *const view = ViewerNode(mViewer.p)->GetView(&buffer, 0)->mValues; // receiver: cIViewerRefNode
        nGZCompGeom::FramePlacement frame;
        frame.mUnknown0 = 1;
        std::memcpy(frame.mOffset, view, sizeof(frame.mOffset));
        static_cast<nGZCompGeom::FrameMath &>(static_cast<AffineTransform3 &>(frame)).SetRotation(view + 3);
        float toView[16], matrix[16];
        nGZCompGeom::AsFrame(*frame.Invert()).ToMatrix4(toView);
        nGZCompGeom::Matrix4Multiply(matrix, toView, projection);
        // receiver: the object (cIShapeRefNode: GetBoundBox 72)
        const nGZSceneGraph::cBoundBox box = reinterpret_cast<nGZSceneGraph::cIShapeRefNode *>(mObject.p)->GetBoundBox();
        // In the original's operand order (a NaN pair keeps the first operand's NaN): the half size first, then the centre.
        using nGZCompGeom::convex::AddS;
        using nGZCompGeom::convex::SubS;
        float centre[3];
        for (int i = 0; i < 3; ++i) centre[i] = AddS(SubS(box.mMax[i], box.mMin[i]) * 0.5f, box.mMin[i]);
        float rect[4];
        if (mMode == 1) {
            rect[0] = AddS(centre[0], mExtents[0]);
            rect[1] = AddS(centre[0], mExtents[1]);
            rect[2] = AddS(centre[1], mExtents[2]);
            rect[3] = AddS(centre[1], mExtents[3]);
        } else {
            float low[2], high[2];
            if (!ShadowFootprint(matrix, low, high)) return;
            rect[0] = low[0];
            rect[1] = high[0];
            rect[2] = low[1];
            rect[3] = high[1];
        }
        if (mProjector.p) {
            // receiver: the projector (slot 10: the rectangle, the view-projection)
            RawSlot<10>(mProjector.p, static_cast<const float *>(rect), static_cast<const float *>(matrix), 0u);
            int32_t ground[3];
            worldmath::TileQuadrant tile;
            worldmath::PositionToTileQuadrant(
                &tile, reinterpret_cast<const float *>(
                           worldmath::WithTileValue(ground, reinterpret_cast<const int32_t *>(centre))));
            RawSlot<8>(mProjector.p, uint32_t(QuadrantTest(&tile))); // receiver: the projector (slot 8)
        }
        if (mFloorProjector.p) {
            // receiver: the projector (slot 10: the rectangle, the view-projection)
            RawSlot<10>(mFloorProjector.p, static_cast<const float *>(rect), static_cast<const float *>(matrix), 0u);
        }
    }
    if (mMode == 2) mMatrixStamp = 0;
}

} // namespace nTSSG
