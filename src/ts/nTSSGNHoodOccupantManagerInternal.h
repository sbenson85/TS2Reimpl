// Shared private declarations of nTSSG::cNHoodOccupantManager's implementation files (nTSSGNHoodOccupantManager*.cpp):
// the slot numbers, ids, small value types and helpers that more than one of them uses. Not part of the class's interface.
#pragma once

#include "ts/nTSSGNHoodOccupantManager.h"

#include "app/AppDebugger.h"
#include "app/AppInit.h"
#include "app/AppServices2.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "misc/UtilitiesC.h"
#include "misc/UtilitiesE.h"
#include "rz/MiscHelpersB.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "simantics/ServiceRefVector.h"
#include <bit>
#include <cmath>
#include <new>

namespace nTSSG::occupant_detail {

inline constexpr uint32_t kAnyBooleanVTable = 0xd9c670; // cGZAny<bool>
inline constexpr uint32_t kAnyVTable = 0xd9c654;        // cIGZAny (what the value's destructor leaves)

// Slots of the objects this code calls (tentative names where nothing else says).
inline constexpr unsigned kServiceQuerySlot = 5;   // framework object: (class id, interface id, out) -> counted
inline constexpr unsigned kGetPropertySlot = 9;    // property set: (name, value) -> found

inline constexpr unsigned kSceneOfSystemSlot = 33; // scene-graph system: the scene (not counted)

inline constexpr unsigned kAddChildSlot = 45;      // group node: (child)
inline constexpr unsigned kRemoveChildSlot = 46;   // group node: (child)

using Get0 = uint32_t(__thiscall *)(void *);

// A boolean value object on the stack (vtable and a byte), filled by the property set.
struct AnyBooleanLocal {
    const void *mVTable = GameAddress(kAnyBooleanVTable);
    uint8_t mValue = 0;
    uint8_t mPad[3];
    AnyBooleanLocal() = default;
    AnyBooleanLocal(const AnyBooleanLocal &) = delete;
    AnyBooleanLocal &operator=(const AnyBooleanLocal &) = delete;
    ~AnyBooleanLocal() { mVTable = GameAddress(kAnyVTable); }
};

// A property name: a std::string built by the game's constructor (0x1773e0, ours: ServiceStringObject::ConstructFromText, src/std/StdInstances.h), its heap buffer (above 15
// characters) freed through FreeStringBuffer (0x152b80), as the original's inlined destructor does.
struct PropertyName {
    as2::RawString mText;
    explicit PropertyName(const char *text)
    {
        reinterpret_cast<ServiceStringObject *>(&mText)->ConstructFromText(text); // 0x1773e0
    }
    PropertyName(const PropertyName &) = delete;
    PropertyName &operator=(const PropertyName &) = delete;
    ~PropertyName()
    {
        if (mText.mCapacity > 15) appdebugger::FreeStringBuffer(&mText, mText.mPtr, mText.mCapacity);
    }
};

// A service from the framework object (game global 0x10c9378) through its slot 5, held for the scope and released through
// 0x1cbd79. Null when there is no framework object or it answers none (the callers then call through null, as the original).
struct HeldService {
    ScopedRef mRef{nullptr};
    HeldService(uint32_t classId, uint32_t iid)
    {
        if (void *framework = GetServiceObject()) {
            using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
            VirtualCall<Query, kServiceQuerySlot>(framework, classId, iid, &mRef.mObject);
        }
    }
    HeldService(const HeldService &) = delete;
    HeldService &operator=(const HeldService &) = delete;
    ~HeldService() noexcept(false) { mRef.ReleaseObject(); }
};

inline constexpr uint32_t kShapeNodeIid = 0xa516cd10;

// Slots of the occupant, the cached singleton's mode object and the shape-colour registry.
inline constexpr unsigned kOccupantTypeSlot = 4;
inline constexpr unsigned kOccupantNodeSlot = 11;

inline constexpr unsigned kRegistryIndexSlot = 3;

using Query = uint32_t(__thiscall *)(void *, uint32_t, void *);
// The bit of the "ShapeColor" flag in the cached shape registry (defined in nTSSGNHoodOccupantManagerMessages.cpp).
uint32_t ShapeColorMask();

inline void *OccupantNode(cIGZUnknown *occupant)
{
    return reinterpret_cast<void *>(uintptr_t(VirtualCall<Get0, kOccupantNodeSlot>(occupant)));
}

inline constexpr unsigned kChildrenSlot = 42;        // (out range) -> the range

inline constexpr unsigned kPlacementPositionSlot = 6;

// A node's children as slot 42 gives them: the first child and the end (a child's slot 38 steps to the next).
struct NodeChildren {
    void *mFirst;
    void *mUnknown;
    void *mEnd;
};

// A counted local: AddRef when made, Release when it goes (also when unwinding).
struct CountedLocal {
    void *mObject;
    explicit CountedLocal(cIGZUnknown *object) : mObject(object)
    {
        if (object) object->AddRef();
    }
    CountedLocal(const CountedLocal &) = delete;
    CountedLocal &operator=(const CountedLocal &) = delete;
    ~CountedLocal()
    {
        if (mObject) static_cast<cIGZUnknown *>(mObject)->Release();
    }
};

inline constexpr unsigned kFloraTargetsSlot = 93;         // the merged flora node: (scratch) -> the per-layer targets' table

inline constexpr unsigned kTargetPairSlot = 34;           // a target: (second name, first name)
inline constexpr unsigned kTargetGroupSlot = 36;          // a target: (group text, name)

inline constexpr uint32_t kMergedLayerPriority = 999;     // 0xaeb070's second argument for the rebuilt layers
// The merged flora node (BuildFloraNode 0x2a5a40).
inline constexpr uint32_t kFloraChildClass = 0xfc6eb1f7;  // its four parts (framework slot 3 with 0x3c6eb1f0)
inline constexpr uint32_t kFloraChildIid = 0x3c6eb1f0;
inline constexpr unsigned kFloraAddPartSlot = 94;         // (part)
inline constexpr unsigned kNodePlaceSlot = 64;            // (three words from 0x2a8640)
inline constexpr unsigned kNodeRotateSlot = 65;           // (rotation from 0x1a72c0)
inline constexpr float kQuarterTurnDegrees = 90.0f;       // image 0xdb35ac
inline constexpr float kDegreesToRadians = 0.017453292f;  // image 0xdb0998
inline constexpr unsigned kSceneAddUnderSlot = 11;        // the scene: (parent, node)
inline constexpr unsigned kNodeGroupSlot = 77;            // (group text)

inline constexpr uint32_t kFloraSettingB = 0x15;          // and, for one lot, (0x15, 1)

inline constexpr uint32_t kLayerBuilderIid = 0x5ba38381;  // the merge node's face kept at +0xa8

inline constexpr uint32_t kPropsText = 0xdb076c;          // game text
inline constexpr uint32_t kShadeText = 0xdb34ec;          // game text

// A reference counted through the object's virtual base (the vbtable at +4, its second entry the base's offset from +4).
inline cIGZUnknown *VirtualBase(void *object)
{
    auto *bytes = static_cast<uint8_t *>(object);
    const int32_t *vbtable = *reinterpret_cast<int32_t *const *>(bytes + 4);
    return reinterpret_cast<cIGZUnknown *>(bytes + 4 + vbtable[1]);
}
struct VirtualBaseRef {
    void *mObject = nullptr;
    VirtualBaseRef() = default;
    explicit VirtualBaseRef(void *object) : mObject(object)
    {
        if (object) VirtualBase(object)->AddRef();
    }
    VirtualBaseRef(const VirtualBaseRef &) = delete;
    VirtualBaseRef &operator=(const VirtualBaseRef &) = delete;
    ~VirtualBaseRef()
    {
        if (mObject) VirtualBase(mObject)->Release();
    }
};

// An entry of a merge node's props list (0x34 bytes, filled by game code): two strings of the allocator service's kind and a
// word.
struct PropsEntry {
    as2::RawString mFirst;  // +0
    as2::RawString mSecond; // +0x18
    uint32_t mWord;         // +0x30
};
static_assert(sizeof(PropsEntry) == 0x34);
// The list as a vector; its destruction is the shared tidy 0x2a4ef0 (strings emptied and their buffers freed through
// 0x152b80, second first; the block freed through the allocator service's slot 4).
struct PropsList {
    PropsEntry *mFirst = nullptr;
    PropsEntry *mLast = nullptr;
    PropsEntry *mEnd = nullptr;
    PropsList() = default;
    PropsList(const PropsList &) = delete;
    PropsList &operator=(const PropsList &) = delete;
    ~PropsList()
    {
        if (!mFirst) return;
        for (PropsEntry *entry = mFirst; entry != mLast; ++entry) {
            for (as2::RawString *text : {&entry->mSecond, &entry->mFirst}) {
                if (text->mCapacity > 15) appdebugger::FreeStringBuffer(text, text->mPtr, text->mCapacity);
                text->mSize = 0;
                text->mCapacity = 15;
                text->mBuf[0] = 0;
            }
        }
        as2::FreeViaAllocatorService(mFirst, 0);
        mFirst = mLast = mEnd = nullptr;
    }
};

inline constexpr uint32_t kGroupNodeClass = 0x65245517; // created through the framework's slot 3 with kShapeNodeIid

inline constexpr unsigned kCreateSlot = 3;            // framework object: (class, iid, out)

// Assigns a counted pointer as the original's inline sequence: nothing when equal, else AddRef the new one, store, Release
// the old one.
inline void AssignCounted(SGRef &ref, cIGZUnknown *object)
{
    cIGZUnknown *old = ref.mObject;
    if (object == old) return;
    if (object) object->AddRef();
    ref.mObject = object;
    if (old) old->Release();
}

} // namespace nTSSG::occupant_detail
