// nGZSceneGraph's locomotion helpers (lane 261111k): the path ribbon 0xae6990 and the locomotion request set-up 0xae2280
// (cLocomotable vf52). The arithmetic is SSE in the original's operand order (compgeom/ConvexMath.h); the headings'
// sines and cosines the toolchain's (double, as the original's _libm_sse2 calls).
#include "sg/SGUtilities.h"

#include "compgeom/ConvexMath.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "rz/StaticGetters.h"
#include "sg/cGeometryNode.h"
#include "sg/cIndexedMeshBuilder.h"
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"
#include "sg/SGNodeInterfaces.h"
#include "misc/X87.h"

#include <cmath>
#include <cstdlib> // _itoa
#include <cstring>
#include <emmintrin.h>

namespace nGZSceneGraph {

namespace {

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::SubS;

constexpr uint32_t kGeometryNodeClass = 0x7ba3838c;
constexpr uint32_t kIndexedMeshBuilderClass = 0x9bffc10d;

// The path (class unsettled; MakeLocomotionRequest's arguments 3, 30 and 31): its slots.
constexpr int kPathKnotCount = 16; // (): the number of knots
constexpr int kPathKnotTime = 10;  // (i): knot i's parameter (float, st0)
constexpr int kPathPosition = 18;  // (float out[3], t): the point at t
constexpr int kPathHeading = 20;   // (t): the heading at t (float, st0)

constexpr float kStepLength = 0.1f;   // 0xda74f0
constexpr float kOne = 1.0f;          // 0xd9c698
constexpr float kHalfWidth = 0.02f;   // 0xda74ec
constexpr float kLift = 0.04f;        // 0xdc87bc

template <class T> void Create(uint32_t classId, T **out)
{
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(classId, T::kIid, reinterpret_cast<void **>(out));
}

int32_t KnotCount(void *path) { return VirtualCall<int32_t(__thiscall *)(void *), kPathKnotCount>(path); }
float KnotTime(void *path, int32_t i) { return VirtualCall<float(__thiscall *)(void *, int32_t), kPathKnotTime>(path, i); }
void PositionAt(void *path, float *out, float t)
{
    VirtualCall<void(__thiscall *)(void *, float *, float), kPathPosition>(path, out, t);
}
float HeadingAt(void *path, float t) { return VirtualCall<float(__thiscall *)(void *, float), kPathHeading>(path, t); }

} // namespace

// 0xae6990 (lane 261111k)
void MakePathRibbonMesh(void *path, cIGeometryNode **out)
{
    cIGeometryNode *node = nullptr;
    Create(kGeometryNodeClass, &node);
    cIIndexedMeshBuilder *mesh = nullptr;
    Create(kIndexedMeshBuilderClass, &mesh);
    // ORIGINAL-BUG: neither the node nor the mesh is checked.
    node->AddPart(mesh);
    mesh->Reset(false);

    const int32_t last = KnotCount(path) - 1;
    int32_t knot = 0;
    if (last > 0) {
        do {
            // "part" and the knot's number (a cRZString on the stack in the original; it never outgrows its buffer).
            char name[4 + 12] = {'p', 'a', 'r', 't'};
            _itoa(knot, name + 4, 10);
            mesh->SetGroupName(name);
            const float start = KnotTime(path, knot);
            ++knot;
            const float span = SubS(KnotTime(path, knot), start);
            int32_t steps = _mm_cvtt_ss2si(_mm_set_ss(AddS(DivS(span, kStepLength), kOne)));
            const float step = DivS(span, static_cast<float>(steps));
            float t = start;
            for (; steps > 0; --steps) {
                const uint32_t base = mesh->CountPositions();
                float a[3], b[3];
                PositionAt(path, a, t);
                const float headingA = HeadingAt(path, t);
                t = AddS(step, t);
                PositionAt(path, b, t);
                const float headingB = HeadingAt(path, t);

                const float cosA = MulS(static_cast<float>(std::cos(double(headingA))), kHalfWidth);
                const float sinA = MulS(static_cast<float>(std::sin(double(headingA))), kHalfWidth);
                const float p0[3] = {AddS(a[0], cosA), AddS(a[1], sinA), AddS(a[2], kLift)};
                mesh->AddPosition(p0);
                const float p1[3] = {SubS(a[0], cosA), SubS(a[1], sinA), AddS(a[2], kLift)};
                mesh->AddPosition(p1);
                const float cosB = MulS(static_cast<float>(std::cos(double(headingB))), kHalfWidth);
                const float sinB = MulS(static_cast<float>(std::sin(double(headingB))), kHalfWidth);
                const float p2[3] = {SubS(b[0], cosB), SubS(b[1], sinB), AddS(b[2], kLift)};
                mesh->AddPosition(p2);
                const float p3[3] = {AddS(b[0], cosB), AddS(b[1], sinB), AddS(b[2], kLift)};
                mesh->AddPosition(p3);

                mesh->MarkUnknown();
                mesh->AddPositionIndex(base + 3);
                mesh->AddPositionIndex(base + 2);
                mesh->AddPositionIndex(base + 1);
                mesh->AddPositionIndex(base);
                mesh->CommitGroup();
            }
        } while (knot < last);
    }
    mesh->Finish();
    *out = node;
    node->AddRef();
    if (mesh) mesh->Release();
    if (node) node->Release();
}

namespace {

// The locomotion request (class 0x9c1d008c, iid 0xdc1d0085; not ours): its own face's setters, named by the
// MakeLocomotionRequest argument each gets (meanings unsettled).
constexpr uint32_t kRequestClass = 0x9c1d008c, kRequestIid = 0xdc1d0085;
enum RequestSlot : int {
    kReqPath = 0, kReqA5 = 1, kReqA6 = 2, kReqA3 = 3, kReqA7 = 5, kReqA8 = 7, kReqA13 = 8, kReqA14 = 9, kReqA15 = 10,
    kReqA16 = 13, kReqCurve = 14 /* (i, a21[i]) */, kReqA27 = 15, kReqDisplay = 16 /* (the shape-ref node) */,
};
// ... and its settings' (virtual base 2) setters.
enum SettingsSlot : int {
    kSetA10 = 10, kSetA11 = 11, kSetA12 = 12, kSetA17 = 13, kSetA18 = 14, kSetA19 = 15, kSetA20 = 16, kSetA22 = 17,
    kSetA23 = 18, kSetA24 = 19, kSetA25 = 21 /* (a25, a26) */, kSetA28 = 24, kSetA30 = 25, kSetBlend = 26,
    kSetTransform = 51 /* (): a rigid transform */,
};
constexpr int kTimedValue = 19;    // a8 and a20 (class unsettled): (float t) -> float, st0
constexpr int kPathLength = 22;    // the path: () -> float, st0
constexpr int kPathPointAt = 9;    // the path: (float out[3], knot) -> out
constexpr int kNodeSetSettings = 10; // the node: (the request's settings, or null)
constexpr int kExtensionOwner = 18;  // the node's cIExtension (virtual base 1): GetOwner
constexpr uint32_t kNoTime = 0x80000100u;
constexpr uint32_t kShapeClass = 0xfc6eb1f7;
constexpr uint32_t kShapeRefNodeClass = 0x65245517;

const char *const kPathMaterials[3] = {"Anim_ChoreoLocoRequestPath", "Anim_ChoreoLocoRequestRefPath1",
                                       "Anim_ChoreoLocoRequestRefPath2"};
const char kWayPointMaterial[] = "Anim_ChoreoLocoRequestWayPoint";
const char kWayPointSubset[] = "subset0";

void *VirtualBaseAt(void *face, int index)
{
    char *p = static_cast<char *>(face) + 4;
    return p + (*reinterpret_cast<int32_t **>(p))[index];
}

template <int Slot, class... A> void Set(void *face, A... a)
{
    VirtualCall<void(__thiscall *)(void *, A...), Slot>(face, a...);
}

float TimedValue(void *source, int32_t a9, int32_t a10)
{
    const int32_t at = static_cast<int32_t>(static_cast<uint32_t>(a9) - static_cast<uint32_t>(a10)); // wraps
    return VirtualCall<float(__thiscall *)(void *, float), kTimedValue>(source, static_cast<float>(at));
}

template <class T> void Release(T *object)
{
    if (object) object->Release(); // receiver: the pointer as held (slot 2)
}

} // namespace

// 0xae2280 (lane 261111k)
bool MakeLocomotionRequest(void **out, void *node, float a3, void *path, float a5, float a6, float a7, void *a8,
                           int32_t a9, int32_t a10, float a11, float a12, float a13, float a14, float a15, float a16,
                           uint32_t a17, uint32_t a18, float a19, void *a20, const float *a21, uint32_t a22, uint32_t a23,
                           float a24, void *a25, uint32_t a26, uint32_t a27, uint32_t a28, uint32_t display,
                           uint32_t a30, void *refPath1, void *refPath2)
{
    void *request = nullptr;
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(kRequestClass, kRequestIid, &request);
    if (a8 && static_cast<uint32_t>(a9) != kNoTime)
        a7 = static_cast<float>(x87::Rounded(double(TimedValue(a8, a9, a10)) * 1000.0));
    // ORIGINAL-BUG: the path, and below the request, are called unchecked.
    const float length = VirtualCall<float(__thiscall *)(void *), kPathLength>(path);
    float blend = MulS(length, a11);
    if (0.0f > a24) blend = MulS(SubS(kOne, a11), length);
    if (a20 && static_cast<uint32_t>(a9) != kNoTime) a19 = TimedValue(a20, a9, a10);

    void *const settings = VirtualBaseAt(request, 2);
    Set<kReqPath>(request, path);
    Set<kReqA5>(request, a5);
    Set<kReqA6>(request, a6);
    Set<kReqA3>(request, a3);
    Set<kReqA7>(request, a7);
    Set<kReqA8>(request, a8);
    Set<kSetA10>(settings, a10);
    Set<kSetA11>(settings, a11);
    Set<kSetA12>(settings, a12);
    Set<kReqA13>(request, a13);
    Set<kReqA14>(request, a14);
    Set<kReqA15>(request, a15);
    Set<kReqA16>(request, a16);
    Set<kSetA17>(settings, a17);
    Set<kSetA18>(settings, a18);
    Set<kSetA19>(settings, a19);
    Set<kSetA20>(settings, a20);
    if (a21)
        for (int32_t i = 0; i < 20; ++i) Set<kReqCurve>(request, i, a21[i]);
    Set<kSetA22>(settings, a22);
    Set<kSetA23>(settings, a23);
    Set<kSetA24>(settings, a24);
    Set<kReqA27>(request, a27);
    Set<kSetA28>(settings, a28);
    Set<kSetA30>(settings, a30);
    Set<kSetBlend>(settings, blend);
    if (a25) Set<kSetA25>(settings, a25, a26);

    if (node) {
        Set<kNodeSetSettings>(node, request ? settings : nullptr);
        void *const extension = VirtualBaseAt(node, 1);
        using Owner = cIGZUnknown *(__thiscall *)(void *);
        if ((display & 0xff) && VirtualCall<Owner, kExtensionOwner>(extension)) {
            cIGZUnknown *const owner = VirtualCall<Owner, kExtensionOwner>(extension);
            cITransformNode *scene = nullptr;
            if (owner->QueryInterface(cITransformNode::kIid, reinterpret_cast<void **>(&scene)) && scene->GetParent()) {
                cIShapeRefNode *shown = nullptr;
                Create(kShapeRefNodeClass, &shown);
                // ORIGINAL-BUG: the shape-ref node (and each shape and node below) is used unchecked.
                shown->SetTransform(VirtualCall<const cRigidTransform *(__thiscall *)(void *), kSetTransform>(settings));
                void *const paths[3] = {path, refPath1, refPath2};
                for (int which = 0; which < 3; ++which) {
                    void *const shownPath = paths[which];
                    if (!shownPath) continue;
                    cIShape *shape = nullptr;
                    Create(kShapeClass, &shape);
                    shown->AddShape(shape);
                    cIGeometryNode *ribbon = nullptr;
                    MakePathRibbonMesh(shownPath, &ribbon);
                    shape->AddPart(reinterpret_cast<cIGZUnknown *>(ribbon), 0);
                    const int32_t knots = KnotCount(shownPath);
                    for (int32_t i = 0; i < knots - 1; ++i) {
                        // "part" and the knot's number (a cRZString in the original).
                        char part[4 + 12] = {'p', 'a', 'r', 't'};
                        _itoa(i, part + 4, 10);
                        shape->BindMaterial(kPathMaterials[which], part);
                    }
                    if (which == 0) {
                        for (int32_t k = 0; k < knots; ++k) {
                            cIShapeRefNode *marker = nullptr;
                            Create(kShapeRefNodeClass, &marker);
                            cIShape *box = nullptr;
                            Create(kShapeClass, &box);
                            static constexpr float kLow[3] = {-0.04f, -0.04f, -0.04f};  // 0xbd23d70a
                            static constexpr float kHigh[3] = {0.04f, 0.04f, 0.04f};    // 0x3d23d70a
                            cIGeometryNode *boxMesh = nullptr;
                            MakeBoxMesh(&boxMesh, kLow, kHigh);
                            box->AddPart(reinterpret_cast<cIGZUnknown *>(boxMesh), 0);
                            box->BindMaterial(kWayPointMaterial, kWayPointSubset);
                            float heading = 0.0f;
                            if (knots > 1) heading = HeadingAt(shownPath, KnotTime(path, k));
                            static constexpr float kUp[3] = {0.0f, 0.0f, 1.0f};
                            float rotation[4];
                            QuatFromAxisAngle(rotation, kUp, 1, heading);
                            marker->SetRotation(rotation);
                            float point[3];
                            marker->SetTranslation(
                                VirtualCall<const float *(__thiscall *)(void *, float *, int32_t), kPathPointAt>(
                                    path, point, k));
                            marker->AddShape(box);
                            shown->AddChild(marker);
                            Release(boxMesh);
                            Release(box);
                            Release(marker);
                        }
                    }
                    Release(ribbon);
                    Release(shape);
                }
                scene->GetParent()->AddChild(shown);
                Set<kReqDisplay>(request, static_cast<void *>(shown));
                Release(shown);
            }
            Release(scene);
        }
    }
    *out = request;
    // ORIGINAL-BUG: the reference is added through the request unchecked (released only when set).
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(VirtualBaseAt(request, 1));
    if (request) VirtualCall<uint32_t(__thiscall *)(void *), 2>(VirtualBaseAt(request, 1));
    return true;
}

} // namespace nGZSceneGraph
