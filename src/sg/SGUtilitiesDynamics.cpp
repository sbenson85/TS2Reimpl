// nGZSceneGraph's dynamics behaviour set-up (lane 261111k): 0xae5840. A skeleton's dynamics: the behaviour set
// (class 0xa9de45ba) under a new dynamics object (class 0xfb00791d), and one behaviour (class 0x8c1eb8dc) for every
// transform node below the skeleton's owner whose "tsDynamicsBehavior.s" data list is of skin type 10. The skeleton's
// kind comes from the first name of the image's static table at 0x1071460 (cRZString, a scale; 0x24 bytes,
// built by the static initialiser 0x13f3a0) the skeleton answers to; its scale sets the springs' gravity.
//
// Every object here is reached through its interface slots; none of the classes is ours yet, so the slots are called
// by number (the interface names are in the comments).
#include "sg/SGUtilities.h"

#include "compgeom/ConvexMath.h" // Mul87: the x87's NaN choice
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "misc/SmallVirtuals.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject: the COM
#include "rz/cRZString.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kDynamicsClass = 0xfb00791d, kDynamicsIid = 0x7b007916;
constexpr uint32_t kBehaviorSetClass = 0xa9de45ba, kBehaviorSetIid = 0xa9de45ae;
constexpr uint32_t kBehaviorClass = 0x8c1eb8dc, kBehaviorIid = 0x8c1eb8e1;
constexpr uint32_t kSpringClass = 0x1b115b24, kSpringIid = 0x1b115b28;     // values 0 and 2
constexpr uint32_t kDamperClass = 0x5b0aff38, kDamperIid = 0xbb0aff2f;     // value 1
constexpr uint32_t kForceSlotId = 0x9b5b3506;                              // the behaviour's slot 7 key
constexpr uint32_t kTreeIid = 0x4512dd13;                                  // the owner's composition tree
constexpr uint32_t kTransformIid = 0xe51c10f5;                             // cITransformNode

// The image's skeleton table (0x1071460): four entries (a cRZString, a scale), "auskel" 1.0, "tuskel" 1.0,
// "cuskel" 0.6, "puskel" 0.4.
struct SkeletonKind {
    cRZString name; // +0
    float scale;    // +0x20
};
static_assert(sizeof(SkeletonKind) == 0x24);
constexpr uint32_t kSkeletonKinds = 0x1071460;
constexpr int kSkeletonKindCount = 4;

constexpr float F(uint32_t bits) { return std::bit_cast<float>(bits); }
constexpr float kDegree = F(0x3c8efa35); // pi / 180

template <class R, int Slot, class... A> R Call(void *object, A... a)
{
    return VirtualCall<R(__thiscall *)(void *, A...), Slot>(object, a...);
}

// A face whose virtual base table pointer follows its vtable (+4): base `index`.
void *VirtualBaseAt(void *face, int index)
{
    char *p = static_cast<char *>(face) + 4;
    return p + (*reinterpret_cast<int32_t **>(p))[index];
}
// A face that is only a virtual base table pointer (+0): base `index`.
void *VirtualBaseOfBare(void *face, int index)
{
    char *p = static_cast<char *>(face);
    return p + (*reinterpret_cast<int32_t **>(p))[index];
}

void Release(void *face) { Call<uint32_t, 2>(face); }

void Create(uint32_t classId, uint32_t iid, void **out)
{
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(classId, iid, out);
}

// What a transform node's slot 53 hands back: the translation, then the rotation (a quaternion).
struct Pose {
    float translation[3];
    float rotation[4];
};
static_assert(sizeof(Pose) == 0x1c);

float Dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// One tagged transform node's behaviour (`cursor` is the iterator the node came from; it is asked again).
void AddBehavior(void *behaviors, void *cursor, void *node, const char *tag, uint32_t enable, uint32_t freeze,
                 float scale)
{
    int32_t value = -1;
    Pose scratch; // slot 53's buffer: both reads use it, and the behaviour's rest pose is built in it later
    float translation[3];
    std::memcpy(translation, Call<const Pose *, 53>(node, &scratch)->translation, sizeof translation);
    float rotation[4];
    std::memcpy(rotation, Call<const Pose *, 53>(node, &scratch)->rotation, sizeof rotation);
    float lower[3] = {F(0xff7fffff), F(0xff7fffff), F(0xff7fffff)};
    float upper[3] = {F(0x7f7fffff), F(0x7f7fffff), F(0x7f7fffff)};

    auto *const list = FindDataList(Call<cICompositionTreeNode *, 5>(cursor), "tsDynamicsBehavior.s", nullptr);
    int32_t skinType;
    if (!list || !Call<uint8_t, 14>(list, "DynamicsBehaviorSkinType", &skinType) || skinType != 10) return;
    Call<uint8_t, 14>(list, "DynamicsBehaviorValue", &value);
    Call<uint8_t, 17>(list, "DynamicsBehaviorRestTrans", translation);
    Call<uint8_t, 19>(list, "DynamicsBehaviorRestRot", rotation);

    // The joint limits (radians) by kind: 0 a loose joint (50 degrees), 1 a slider (0.03), 2 a hinge-like joint
    // (20 / 4 / 20 degrees) narrowed to 5 degrees on the side the rest offset points to (8 / 2 for z-led offsets).
    switch (value) {
    case -1: return;
    case 0:
        for (int i = 0; i < 3; ++i) lower[i] = F(0xbf5f66f3), upper[i] = F(0x3f5f66f3);
        break;
    case 1:
        for (int i = 0; i < 3; ++i) lower[i] = F(0xbcf5c28f), upper[i] = F(0x3cf5c28f);
        break;
    case 2: {
        lower[0] = F(0xbeb2b8c2), lower[1] = F(0xbd8efa35), lower[2] = F(0xbeb2b8c2);
        upper[0] = F(0x3eb2b8c2), upper[1] = F(0x3d8efa35), upper[2] = F(0x3eb2b8c2);
        static constexpr float kAxisX[3] = {1.0f, 0.0f, 0.0f}, kAxisZ[3] = {0.0f, 0.0f, 1.0f};
        float axis[3];
        const float along = Dot(QuatRotate(axis, kAxisX, rotation), translation);
        const float across = Dot(QuatRotate(axis, kAxisZ, rotation), translation);
        if (std::fabs(across) > std::fabs(along)) {
            if (across > 0.0f) {
                lower[0] = F(0xbdb2b8c2);
            } else {
                lower[0] = F(0xbe0efa35);
                upper[0] = F(0x3d0efa35);
            }
        } else if (along > 0.0f) {
            upper[2] = F(0x3db2b8c2);
        } else {
            lower[2] = F(0xbdb2b8c2);
        }
        break;
    }
    default: break;
    }

    // DynamicsBehaviorLimit0..5: lower x, upper x, lower y, ... in degrees (value 1's as they are).
    for (int i = 0; i < 6; ++i) {
        cRZString key;
        key.Construct();
        key.Sprintf("%s%1d", "DynamicsBehaviorLimit", i);
        float limit;
        if (Call<uint8_t, 15>(list, key.ToChar(), &limit)) {
            if (value != 1) limit *= kDegree;
            (i % 2 == 0 ? lower : upper)[i / 2] = limit;
        }
        key.Destruct();
    }

    void *behavior = nullptr;
    Create(kBehaviorClass, kBehaviorIid, &behavior);
    // ORIGINAL-BUG: the new behaviour is used unchecked.
    Call<void, 0>(behavior, value);
    Call<void, 9>(VirtualBaseAt(behavior, 1), tag);
    Call<void, 1>(behavior, 1.0f);
    std::memcpy(scratch.translation, translation, sizeof translation);
    std::memcpy(scratch.rotation, rotation, sizeof rotation);
    Call<void, 2>(behavior, &scratch);
    Call<void, 3>(behavior, lower);
    Call<void, 4>(behavior, upper);
    const float stiffness[3] = {35.0f, 35.0f, 35.0f};
    Call<void, 6>(behavior, stiffness);
    const float damping[3] = {10.0f, 10.0f, 10.0f};
    Call<void, 7>(behavior, damping);
    Call<void, 8>(behavior, uint32_t((enable & 0xff) != 0 && (freeze & 0xff) == 0));
    Call<void, 9>(behavior, freeze);

    if (value == 0 || value == 1 || value == 2) {
        void *force = nullptr;
        if (value == 1)
            Create(kDamperClass, kDamperIid, &force);
        else
            Create(kSpringClass, kSpringIid, &force);
        Call<void, 7>(VirtualBaseAt(behavior, 1), force ? VirtualBaseOfBare(force, 2) : nullptr, kForceSlotId);
        float pull[3] = {0.0f, 0.0f, 0.0f};
        if (value == 2)
            pull[1] = scale * 0.2f;
        else if (value == 0)
            pull[1] = scale * 0.3f;
        Call<void, 5>(behavior, pull);
        if (force) Release(VirtualBaseOfBare(force, 1));
    }

    Call<void, 10>(behaviors, behavior ? VirtualBaseAt(behavior, 1) : nullptr);
    if (behavior) Release(VirtualBaseAt(behavior, 1));
}

} // namespace

// 0xae5840 (lane 261111k)
bool MakeDynamicsBehaviors(void *skeleton, uint32_t enable, uint32_t freeze, void **out)
{
    Create(kDynamicsClass, kDynamicsIid, out);
    void *behaviors = nullptr;
    Create(kBehaviorSetClass, kBehaviorSetIid, &behaviors);
    // ORIGINAL-BUG: both new objects are used unchecked.
    Call<void, 18>(*out, behaviors);
    Call<void, 9>(behaviors, 1u);
    Call<void, 3>(*out, (freeze & 0xff) != 0 ? 0.0f : 2147483392.0f);
    Call<void, 14>(*out, 2u);

    float scale = 0.0f;
    auto *const kinds = GameAddress<SkeletonKind>(kSkeletonKinds);
    for (int i = 0; i < kSkeletonKindCount; ++i) {
        const char *const name = kinds[i].name.ToChar();
        if (Call<uint32_t, 52>(skeleton, NameCrc(name))) {
            Call<void, 8>(behaviors, name);
            Call<void, 0>(*out, name);
            // fld (the returned scale) times the table's float on the x87: of two NaNs, the x87's pick.
            scale = nGZCompGeom::convex::Stored(nGZCompGeom::convex::Mul87(Call<float, 67>(skeleton), kinds[i].scale));
            break;
        }
    }

    bool result = false;
    void *tree = nullptr;
    if (scale != 0.0f && Call<void *, 18>(VirtualBaseAt(skeleton, 1))) {
        void *const owner = Call<void *, 18>(VirtualBaseAt(skeleton, 1));
        if (Call<uint8_t, 0>(owner, kTreeIid, &tree)) {
            nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter filter{
                GameAddress<const void>(nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::kVTable),
                Call<uint32_t, 66>(skeleton), kTransformIid};
            void *range[3] = {}; // the cursor, the begin, the end
            Call<void, 44>(tree, range, 0u, &filter);
            while (!Call<uint8_t, 4>(range[0], range[2])) {
                void *node = nullptr;
                if (Call<uint8_t, 0>(Call<void *, 5>(range[0]), kTransformIid, &node)) {
                    const char *const tag = GetNodeTag(Call<cICompositionTreeNode *, 5>(range[0]));
                    if (tag && *tag) AddBehavior(behaviors, range[0], node, tag, enable, freeze, scale);
                }
                if (node) Release(node);
                Call<void, 6>(range[0]);
            }
            for (int i = 2; i >= 0; --i)
                if (range[i]) Release(range[i]);
            result = Call<uint32_t, 31>(behaviors) != 0;
        }
    }
    if (tree) Release(tree);
    if (behaviors) Release(behaviors);
    return result;
}

} // namespace nGZSceneGraph
