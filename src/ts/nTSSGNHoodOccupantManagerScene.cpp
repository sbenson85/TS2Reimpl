// nTSSG::cNHoodOccupantManager (lane GO): building the scene: the layers, the merged layers and the flora node.
// Split from one file along these concerns; the shared helpers are in nTSSGNHoodOccupantManagerInternal.h.
#include "sg/DetailObject.h"
#include "std/StdInstances.h"
#include "ts/nTSSGNHoodOccupantManager.h"
#include "ts/nTSSGNHoodOccupantManagerInternal.h"

namespace nTSSG {

using namespace occupant_detail;

namespace {

constexpr unsigned kMergeResetSlot = 5;      // a layer's merge node (+0x98)
constexpr unsigned kLayerRebuildSlot = 34;   // a layer's +0xa8 object
constexpr unsigned kAppOccupantsSlot = 11;   // app slot 12's object: its occupants into a counted vector

constexpr unsigned kHasChildrenSlot = 52;     // -> bool

constexpr unsigned kMergeAddSlot = 3;         // a layer's merge node (+0x98): (occupant, option bit)

constexpr unsigned kAppFaceBOccupantsSlot = 13;    // app slot 12's object: the face-B occupants into a counted vector
constexpr unsigned kAppFaceCOccupantsSlot = 15;    // app slot 12's object: the face-C occupants into a counted vector

constexpr uint32_t kFloraSettingA = 0x1b;          // 0xaeac50(node, 0x1b, 1)

constexpr uint32_t kMergeNodeClass = 0x4ef8f0c0;   // a layer's merge node (+0x98)
constexpr uint32_t kMergeNodeIid = 0x4ef8f0b9;

constexpr unsigned kTargetAttachSlot = 41;         // a layer's target: (builder, 0)
constexpr unsigned kNodeFinishSlot = 91;           // (0x80)
constexpr uint32_t kFloraFinishFlags = 0x80;

constexpr uint32_t kRootPriority = 999;

constexpr unsigned kRootPrioritySlot = 81;
constexpr unsigned kLotManagerLotSlot = 9;     // GetCachedSlot23's object: the current lot
constexpr unsigned kStateEditorSlot = 43;      // 0x211da3's object
constexpr unsigned kEditorAcceptsLotSlot = 12; // (lot) -> the lot can be built for
constexpr unsigned kStateViewSlot = 37;        // 0x211da3's object: the view rectangle's owner, or none
constexpr unsigned kViewRectSlot = 13;         // (int rect[4])
constexpr unsigned kLotByteSlot = 23;
constexpr unsigned kLotRectSlot = 12;          // (int rect[4])
constexpr unsigned kSingletonNeighborhoodSlot = 6;
constexpr unsigned kNeighborhoodNameSlot = 65;
constexpr unsigned kSingletonFloraSourceSlot = 12; // -> +0x28
constexpr unsigned kSingletonSlot11 = 11;          // -> +0x2c
constexpr unsigned kOccupantRectSlot = 3;          // the occupant's footprint (for 0x2a8570)
constexpr unsigned kAppOthersSlot = 17;            // app slot 12's object: the other occupants into a counted vector
constexpr unsigned kLayerNodeChildrenSlot = 34;    // layer 0's node: children count
constexpr unsigned kSceneAddSlot = 7;              // the scene (0x10cc460): (root)
constexpr uint32_t kSceneCache = 0x10cc460;

// A counted vector's ReleaseAndFree when the scope ends (the original's 0x1f1f70 in its unwind order).
struct FreedAtExit {
    ServiceRefVector &mVector;
    ~FreedAtExit() { mVector.ReleaseAndFree(); }
};

void *Footprint(void *occupant)
{
    return reinterpret_cast<void *>(uintptr_t(VirtualCall<Get0, kOccupantRectSlot>(occupant)));
}

} // namespace

// 0x2a8780.
void cNHoodOccupantManager::BuildScene(uint32_t all)
{
    mBuildAll = uint8_t(all);
    int32_t lotRect[4]; // the lot's interior; not set without a view owner (read only when building one lot)
    if (mBuildAll) {
        mLotByte = 0;
    } else {
        DetachScene();
        void *lot = reinterpret_cast<void *>(uintptr_t(VirtualCall<Get0, kLotManagerLotSlot>(GetCachedSlot23())));
        if (lot) {
            void *state = OriginalFunction<void *(__cdecl *)(), 0x211da3>()();
            void *editor = reinterpret_cast<void *>(uintptr_t(VirtualCall<Get0, kStateEditorSlot>(state)));
            if (!(VirtualCall<uint32_t(__thiscall *)(void *, void *), kEditorAcceptsLotSlot>(editor, lot) & 0xff)) return;
            mLotByte = VirtualCall<Get0, kLotByteSlot>(lot) & 0xff;
            state = OriginalFunction<void *(__cdecl *)(), 0x211da3>()();
            if (void *owner = reinterpret_cast<void *>(uintptr_t(VirtualCall<Get0, kStateViewSlot>(state)))) {
                VirtualCall<uint32_t(__thiscall *)(void *, int32_t *), kViewRectSlot>(owner, lotRect);
                ++lotRect[0];
                --lotRect[2];
                ++lotRect[1];
                --lotRect[3];
            }
            VirtualCall<uint32_t(__thiscall *)(void *, int32_t *), kLotRectSlot>(lot, mViewRect);
            mViewRect[0] -= 3;
            mViewRect[2] += 3;
            mViewRect[1] -= 3;
            mViewRect[3] += 3;
        }
    }
    void *singleton = GetCachedSingleton();
    using Create = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void *);
    void *framework = GetDirectorFrameworkObject();
    mRoot.Reset();
    VirtualCall<Create, kCreateSlot>(framework, kGroupNodeClass, kShapeNodeIid, &mRoot.mObject);
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), kRootPrioritySlot>(mRoot.mObject, kRootPriority);
    using Child = uint32_t(__thiscall *)(void *, cIGZUnknown *);
    for (uint32_t i = 0; i < 4; ++i) {
        framework = GetDirectorFrameworkObject();
        mLayerNodes[i].Reset();
        VirtualCall<Create, kCreateSlot>(framework, kGroupNodeClass, kShapeNodeIid, &mLayerNodes[i].mObject);
        if (mLayerVisible[i]) VirtualCall<Child, kAddChildSlot>(mRoot.mObject, mLayerNodes[i].mObject);
    }
    void *neighborhood = reinterpret_cast<void *>(uintptr_t(VirtualCall<Get0, kSingletonNeighborhoodSlot>(singleton)));
    mNeighborhoodName.mString.mString.assign(
        reinterpret_cast<const char *>(uintptr_t(VirtualCall<Get0, kNeighborhoodNameSlot>(neighborhood))));
    AssignCounted(mAppSlot12,
        reinterpret_cast<cIGZUnknown *>(uintptr_t(VirtualCall<Get0, kSingletonFloraSourceSlot>(singleton))));
    AssignCounted(mAppSlot11, reinterpret_cast<cIGZUnknown *>(uintptr_t(VirtualCall<Get0, kSingletonSlot11>(singleton))));
    AssignCounted(mSgSystem, static_cast<cIGZUnknown *>(CachedServiceGetter<kSgSystemCache, kSgSystemIdA, kSgSystemIdB>()));
    if (mBuildAll) PrepareBuildAll();
    bool builtInView = false;
    BuildFloraNode();
    // the flora: everything (all), or what touches the lot (added where it lies wholly in the view, else merged)
    ServiceRefVector flora{};
    FreedAtExit floraFreed{flora};
    VirtualCall<uint32_t(__thiscall *)(void *, ServiceRefVector *), kAppOccupantsSlot>(mAppSlot12.mObject, &flora);
    for (uint32_t i = 0, n = flora.Size(); i < n; ++i) {
        auto *occupant = static_cast<cIGZUnknown *>(flora.mFirst[i]);
        const bool near = mBuildAll || FootprintIn(Footprint(occupant), lotRect, 1);
        if (mBuildAll) {
            AddFloraOccupant(occupant);
            if (near) MergeOccupant(occupant);
        } else if (near) {
            if (FootprintIn(Footprint(occupant), mViewRect, 0)) {
                AddFloraOccupant(occupant);
                builtInView = true;
            } else {
                MergeOccupant(occupant);
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (mLayerDirty[i]) {
            VirtualCall<Get0, kLayerRebuildSlot>(mLayerBuilders[i].mObject);
            mLayerDirty[i] = 0;
        }
    }
    for (uint32_t i = 0, n = mPending.Size(); i < n; ++i)
        HandOnPending(mPending.mFirst[i]);
    HeldService properties(kPropertyClass, kPropertySetIid);
    AnyBooleanLocal dontMerge;
    {
        PropertyName name("dontMergeNHFlora");
        using Get = uint32_t(__thiscall *)(void *, const as2::RawString *, AnyBooleanLocal *);
        VirtualCall<Get, kGetPropertySlot>(properties.mRef.mObject, &name.mText, &dontMerge);
    }
    mPending.Clear();
    // layer 0 shown unless its flora is merged (and nothing was built in the view of one lot)
    mLayerVisible[0] = !mMergeFlora || (builtInView && !mBuildAll);
    if (dontMerge.mValue && mFloraNode.mObject) VirtualCall<Child, kRemoveChildSlot>(mRoot.mObject, mFloraNode.mObject);
    if (!mLayerVisible[0] && VirtualCall<Get0, kLayerNodeChildrenSlot>(mLayerNodes[0].mObject))
        VirtualCall<Child, kRemoveChildSlot>(mRoot.mObject, mLayerNodes[0].mObject);
    RebuildMergedLayers();
    ServiceRefVector others{};
    VirtualCall<uint32_t(__thiscall *)(void *, ServiceRefVector *), kAppOthersSlot>(mAppSlot12.mObject, &others);
    for (uint32_t i = 0, n = others.Size(); i < n; ++i) {
        auto *occupant = static_cast<cIGZUnknown *>(others.mFirst[i]);
        if (mBuildAll || FootprintIn(Footprint(occupant), lotRect, 0)) AddOtherOccupant(occupant);
    }
    void **sceneCache = GameAddress<void *>(kSceneCache);
    if (!*sceneCache) {
        ScopedRef held{nullptr};
        if (void *service = GetServiceObject()) {
            using Query4 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
            VirtualCall<Query4, kServiceQuerySlot>(service, kSgSystemIdA, kSgSystemIdB, &held.mObject);
        }
        *sceneCache = reinterpret_cast<void *>(uintptr_t(VirtualCall<Get0, kSceneOfSystemSlot>(held.mObject)));
        held.ReleaseObject();
    }
    VirtualCall<uint32_t(__thiscall *)(void *, cIGZUnknown *), kSceneAddSlot>(*sceneCache, mRoot.mObject);
    OriginalFunction<void(__cdecl *)(cIGZUnknown *), 0xae7e60>()(mRoot.mObject);
    mDirty = 1;
    others.ReleaseAndFree();
    // then, leaving the scope, dontMerge, properties and the flora list (the original's order)
}

// 0x2a93c0.
void cNHoodOccupantManager::SetMergeFlora(uint32_t merge)
{
    HeldService properties(kPropertyClass, kPropertySetIid);
    AnyBooleanLocal dontMerge;
    {
        PropertyName name("dontMergeNHFlora");
        using Get = uint32_t(__thiscall *)(void *, const as2::RawString *, AnyBooleanLocal *);
        VirtualCall<Get, kGetPropertySlot>(properties.mRef.mObject, &name.mText, &dontMerge);
    }
    uint8_t wanted = uint8_t(merge);
    if (dontMerge.mValue) wanted = 0;
    if (wanted == mMergeFlora) return;
    using Child = uint32_t(__thiscall *)(void *, cIGZUnknown *);
    if (!wanted) {
        if (mFloraNode.mObject) VirtualCall<Child, kRemoveChildSlot>(mRoot.mObject, mFloraNode.mObject);
        if (mLayerNodes[0].mObject) VirtualCall<Child, kAddChildSlot>(mRoot.mObject, mLayerNodes[0].mObject);
        mLayerVisible[0] = 1;
    } else {
        if (mLayerNodes[0].mObject) VirtualCall<Child, kRemoveChildSlot>(mRoot.mObject, mLayerNodes[0].mObject);
        for (int i = 0; i < 4; ++i)
            if (mLayerDirty[i]) VirtualCall<Get0, kMergeResetSlot>(mMergeNodes[i].mObject);
        ServiceRefVector occupants{};
        VirtualCall<uint32_t(__thiscall *)(void *, ServiceRefVector *), kAppOccupantsSlot>(mAppSlot12.mObject, &occupants);
        for (uint32_t i = 0, n = occupants.Size(); i < n; ++i) {
            // ORIGINAL-BUG: a null occupant's layer -1 reads the byte before the flags (+0xb7).
            if (mLayerDirty[LayerOf(occupants.mFirst[i])]) MergeOccupant(static_cast<cIGZUnknown *>(occupants.mFirst[i]));
        }
        for (int i = 0; i < 4; ++i) {
            if (mLayerDirty[i]) {
                VirtualCall<Get0, kLayerRebuildSlot>(mLayerBuilders[i].mObject);
                mLayerDirty[i] = 0;
            }
        }
        for (uint32_t i = 0, n = mPending.Size(); i < n; ++i)
            HandOnPending(mPending.mFirst[i]);
        mPending.Clear();
        if (mFloraNode.mObject) VirtualCall<Child, kAddChildSlot>(mRoot.mObject, mFloraNode.mObject);
        mLayerVisible[0] = 0;
        occupants.ReleaseAndFree();
    }
    mMergeFlora = wanted;
}

// 0x2a5340 (EH): the occupant added to its layer's merge node (slot 3, with the option bit) and queued at +0xbc (counted;
// the queue's growth is RefPtrVector's reallocating insert, the image's instance 0x18c0c0 of the same template).
void cNHoodOccupantManager::MergeOccupant(cIGZUnknown *occupant)
{
    const int32_t layer = LayerOf(occupant);
    if (layer == -1) return;
    VirtualCall<uint32_t(__thiscall *)(void *, cIGZUnknown *, uint32_t), kMergeAddSlot>(mMergeNodes[layer].mObject, occupant,
        mOptionBit7);
    CountedLocal held(occupant); // the original's counted local around the push (released on unwinding too)
    if (mPending.mLast == mPending.mEnd) {
        mPending.EmplaceReallocate(mPending.mLast, &held.mObject);
    } else {
        *mPending.mLast = occupant;
        if (occupant) occupant->AddRef();
        ++mPending.mLast;
    }
}

// 0x2a5860: layers 1 and 2 emptied: while a layer's node has children (slot 52), its first child (slot 42) removed (slot 46).
void cNHoodOccupantManager::ClearLayers1And2()
{
    using Child = uint32_t(__thiscall *)(void *, void *);
    while (mLayerNodes[1].mObject && VirtualCall<bool(__thiscall *)(void *), kHasChildrenSlot>(mLayerNodes[1].mObject)) {
        void *node = mLayerNodes[1].mObject;
        NodeChildren children;
        NodeChildren *range = VirtualCall<NodeChildren *(__thiscall *)(void *, NodeChildren *), kChildrenSlot>(node, &children);
        VirtualCall<Child, kRemoveChildSlot>(node, range->mFirst);
    }
    while (mLayerNodes[2].mObject && VirtualCall<bool(__thiscall *)(void *), kHasChildrenSlot>(mLayerNodes[2].mObject)) {
        void *node = mLayerNodes[2].mObject;
        NodeChildren children;
        NodeChildren *range = VirtualCall<NodeChildren *(__thiscall *)(void *, NodeChildren *), kChildrenSlot>(node, &children);
        VirtualCall<Child, kRemoveChildSlot>(node, range->mFirst);
    }
}

// 0x2a5d70 (EH): when building everything, layers 1 and 2 rebuilt: app slot 12's slot 13 occupants through 0x2a69b0 and its
// slot 15 occupants through 0x2a7130, each layer's children merged into one node (0x2a7be0), the layers emptied
// (ClearLayers1And2) and given the merged nodes, then 0xaeb070(layer, 999) on each.
void cNHoodOccupantManager::RebuildMergedLayers()
{
    if (!mBuildAll) return;
    using List = uint32_t(__thiscall *)(void *, ServiceRefVector *);
    ServiceRefVector listB{};
    FreedAtExit freeB{listB};
    VirtualCall<List, kAppFaceBOccupantsSlot>(mAppSlot12.mObject, &listB);
    for (uint32_t i = 0, n = listB.Size(); i < n; ++i) AddRoadOccupant(static_cast<cIGZUnknown *>(listB.mFirst[i]));
    ServiceRefVector listC{};
    FreedAtExit freeC{listC};
    VirtualCall<List, kAppFaceCOccupantsSlot>(mAppSlot12.mObject, &listC);
    for (uint32_t i = 0, n = listC.Size(); i < n; ++i) AddFaceCOccupant(static_cast<cIGZUnknown *>(listC.mFirst[i]));
    SGRef merged1;
    MergeLayerChildren(mLayerNodes[1].mObject, &merged1.mObject);
    SGRef merged2;
    MergeLayerChildren(mLayerNodes[2].mObject, &merged2.mObject);
    ClearLayers1And2();
    using Child = uint32_t(__thiscall *)(void *, cIGZUnknown *);
    VirtualCall<Child, kAddChildSlot>(mLayerNodes[1].mObject, merged1.mObject);
    VirtualCall<Child, kAddChildSlot>(mLayerNodes[2].mObject, merged2.mObject);
    OriginalFunction<void(__cdecl *)(cIGZUnknown *, uint32_t), 0xaeb070>()(mLayerNodes[1].mObject, kMergedLayerPriority);
    OriginalFunction<void(__cdecl *)(cIGZUnknown *, uint32_t), 0xaeb070>()(mLayerNodes[2].mObject, kMergedLayerPriority);
}

// 0x2a5a40 (EH): the merged flora node built: a group node (framework slot 3) with four children of class 0xfc6eb1f7 (slot
// 94); for one lot, the node placed (0x2a8640's three words, slot 64) and turned by the lot's byte times 90 degrees about z
// (0x1a72c0's rotation, slot 65); put under the root (through the scene's slot 11, or the root's add-child without a scene);
// "Props" and "Shade" (slot 77); 0xaeac50 (0x1b, 1) and, for one lot, (0x15, 1); kept at +0x94; per layer a merge node (class
// 0x4ef8f0c0, +0x98) and its builder face (QI 0x5ba38381, +0xa8) attached to the layer's target (slot 41), the layer marked
// dirty; slot 91(0x80) last.
void cNHoodOccupantManager::BuildFloraNode()
{
    using Create = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void *);
    using Child = uint32_t(__thiscall *)(void *, cIGZUnknown *);
    SGRef flora;
    void *framework = GetDirectorFrameworkObject();
    flora.Reset();
    VirtualCall<Create, kCreateSlot>(framework, kGroupNodeClass, kShapeNodeIid, &flora.mObject);
    for (int i = 0; i < 4; ++i) {
        SGRef child;
        framework = GetDirectorFrameworkObject();
        child.Reset();
        VirtualCall<Create, kCreateSlot>(framework, kFloraChildClass, kFloraChildIid, &child.mObject);
        VirtualCall<Child, kFloraAddPartSlot>(flora.mObject, child.mObject);
    }
    void **sceneCache = GameAddress<void *>(kSceneCache);
    void *scene = *sceneCache;
    if (!scene) {
        ScopedRef held{nullptr};
        if (void *service = GetServiceObject()) {
            using Query4 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
            VirtualCall<Query4, kServiceQuerySlot>(service, kSgSystemIdA, kSgSystemIdB, &held.mObject);
        }
        *sceneCache = reinterpret_cast<void *>(uintptr_t(VirtualCall<Get0, kSceneOfSystemSlot>(held.mObject)));
        held.ReleaseObject();
        scene = *sceneCache;
    }
    if (!mBuildAll) {
        uint32_t placement[3] = {0, 0, 0};
        OriginalFunction<void(__cdecl *)(uint32_t *), 0x2a8640>()(placement);
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t *), kNodePlaceSlot>(flora.mObject, placement);
        float angles[3] = {0.0f, 0.0f, float(int32_t(mLotByte)) * kQuarterTurnDegrees * kDegreesToRadians};
        float rotation[4];
        OriginalFunction<void(__cdecl *)(float *, const float *), 0x1a72c0>()(rotation, angles);
        VirtualCall<uint32_t(__thiscall *)(void *, float *), kNodeRotateSlot>(flora.mObject, rotation);
    }
    if (scene)
        VirtualCall<uint32_t(__thiscall *)(void *, cIGZUnknown *, cIGZUnknown *), kSceneAddUnderSlot>(scene, mRoot.mObject,
            flora.mObject);
    else
        VirtualCall<Child, kAddChildSlot>(mRoot.mObject, flora.mObject);
    using Group = uint32_t(__thiscall *)(void *, const char *);
    VirtualCall<Group, kNodeGroupSlot>(flora.mObject, GameAddress<const char>(kPropsText));
    VirtualCall<Group, kNodeGroupSlot>(flora.mObject, GameAddress<const char>(kShadeText));
    using Setting = void(__cdecl *)(cIGZUnknown *, uint32_t, uint32_t);
    OriginalFunction<Setting, 0xaeac50>()(flora.mObject, kFloraSettingA, 1);
    if (!mBuildAll) OriginalFunction<Setting, 0xaeac50>()(flora.mObject, kFloraSettingB, 1);
    AssignCounted(mFloraNode, flora.mObject);
    uint32_t targets[3]; // slot 93's out: the first word is the per-layer targets' table
    VirtualCall<void ***(__thiscall *)(void *, uint32_t *), kFloraTargetsSlot>(flora.mObject, targets);
    for (int i = 0; i < 4; ++i) {
        framework = GetDirectorFrameworkObject();
        mMergeNodes[i].Reset();
        VirtualCall<Create, kCreateSlot>(framework, kMergeNodeClass, kMergeNodeIid, &mMergeNodes[i].mObject);
        cIGZUnknown *merge = mMergeNodes[i].mObject;
        mLayerBuilders[i].Reset();
        VirtualCall<Query, 0>(merge, kLayerBuilderIid, &mLayerBuilders[i].mObject);
        void *target = reinterpret_cast<void **>(uintptr_t(targets[0]))[i];
        VirtualCall<uint32_t(__thiscall *)(void *, cIGZUnknown *, uint32_t), kTargetAttachSlot>(target,
            mLayerBuilders[i].mObject, 0);
        mLayerDirty[i] = 1;
    }
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), kNodeFinishSlot>(flora.mObject, kFloraFinishFlags);
}

// ---- adapters ----

using Adapters = cNHoodOccupantManagerAdapters;
using Self = cNHoodOccupantManager;

void __fastcall Adapters::SetMergeFlora(cINHoodOccupantManager *face, void *, uint32_t merge)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::SetMergeFlora(merge);
}

void __fastcall Adapters::BuildScene(cINHoodOccupantManager *face, void *, uint32_t all)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::BuildScene(all);
}

} // namespace nTSSG
