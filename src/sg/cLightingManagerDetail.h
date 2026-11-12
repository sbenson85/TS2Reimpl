// The small helpers and game addresses the cLightingManager*.cpp files share (one copy each before lane 261109s).
// Internal: included only by src/sg/cLightingManager*.cpp and cDiffusionManagerPortals.cpp; everything is in an unnamed namespace, so each file keeps its
// own copy, compiled as before. Helpers one file alone uses stay in that file, and so do the wrappers of the game's
// functions (Dot2, SetNodeName): guard checks each OriginalFunction against its own file's status entries.
#pragma once

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "rz/StaticGetters.h"
#include "sg/SGLightingReceivers.h"
#include "sg/cLightRefNode.h"
#include "sg/cLightingManager.h"
#include "sg/cSceneGraphSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nTSSG {

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

// The scene a scene manager (nTSSG::cSceneManager; the lighting manager's mSceneManager) holds: its slot 17, not counted.
__forceinline nGZSceneGraph::cICompositionTreeNode *SceneOf(const void *sceneManager)
{
    return static_cast<cISceneManagerScene *>(const_cast<void *>(sceneManager))->GetScene();
}

constexpr uint32_t kRefreshSwitch =0xffde64; // g_RefreshSwitch (slot 20's): lights go to the lerp sets instead
constexpr uint32_t kZeroVector = 0x10d9424;   // the game's zero vector (read where the original reads it)
constexpr uint32_t kUpNormal = 0xffd918;      // the game's up normal
constexpr uint32_t kGrey = 0xffd994;          // the game's mid-grey ("LM_OutdoorShade"'s colour; a light's colour
                                              // before its attribute is read)

// The director's classes and the interfaces asked of them.
constexpr uint32_t kLightRefNodeClass = 0x253d2018;
constexpr uint32_t kAmbientLightClass = 0xc9c81b9b;

// The attributes a portal's node answers (as the lerp sets' kExisting lights; sg/cLerpLightSet.cpp).
constexpr uint32_t kAttrOne = 0xe9d24d51;
constexpr uint32_t kAttrColor = 0x29de3049;
constexpr uint32_t kAttrValue = 0x49e37821;
constexpr uint32_t kScalarEventVTable = 0xdb07f4; // the game's cScalarEvent<float>
constexpr uint32_t kVectorEventVTable = 0xdb5e70; // the game's cVectorEvent<cVector<3>>

struct ScalarEvent {
    const void *mVTable;
    float mValue;
};
struct VectorEvent {
    const void *mVTable;
    float mValue[3];
};

bool Switch(uint32_t rva) { return *GameAddress<const uint8_t>(rva) != 0; }

void Copy3(float *out, const float *in)
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
}

// The game's zero vector into out (the three floats read from kZeroVector, as the original reads them).
void CopyZero(float *out) { Copy3(out, GameAddress<const float>(kZeroVector)); }

// A vector's size: std::max(|x|, std::max(|y|, |z|)).
float SizeOf(const float *v)
{
    return std::max(std::fabs(v[0]), std::max(std::fabs(v[1]), std::fabs(v[2])));
}

// The lerp sets' key for a sky sample (0x3a97d0, ours: SkySampleKey).
uint32_t SampleKey(int32_t sample)
{
    return static_cast<uint32_t>(SkySampleKey(sample));
}

void GetAttribute(nGZSceneGraph::cILightRefNode *node, uint32_t id, void *event)
{
    node->GetAttribute(reinterpret_cast<uint32_t>(event), id, 0); // receiver: nGZSceneGraph::cILightRefNode
}
void SetAttribute(nGZSceneGraph::cILightRefNode *node, uint32_t id, const void *event)
{
    node->SetAttribute(id, 0, reinterpret_cast<uint32_t>(event)); // receiver: nGZSceneGraph::cILightRefNode
}

// A reference to an interface whose cIGZUnknown is a virtual base released through it: the vbptr at +4, the base's
// offset in its second entry. The interfaces: cIDiffusionLightExtension (the portal records' nodes' extension, the
// portal object's face) and the portal records' objects (class not identified).
void ReleaseThroughBase(void *object)
{
    uint8_t *const vbptr = static_cast<uint8_t *>(object) + 4;
    const int32_t offset = (*reinterpret_cast<int32_t *const *>(vbptr))[1];
    RawSlot<2, void>(vbptr + offset); // receiver: cIGZUnknown (the interface's virtual base; Release)
}

void AddRefThroughBase(void *object)
{
    uint8_t *const vbptr = static_cast<uint8_t *>(object) + 4;
    const int32_t offset = (*reinterpret_cast<int32_t *const *>(vbptr))[1];
    RawSlot<1, void>(vbptr + offset); // receiver: cIGZUnknown (the interface's virtual base; AddRef)
}

void DropHeld(void *&held)
{
    if (held) {
        void *const old = held;
        held = nullptr;
        ReleaseThroughBase(old);
    }
}

// The three counted iterators a node's range is made of (the cursor, the begin, the end), released end first.
struct NodeRange {
    nGZSceneGraph::cISceneNodeIterator *mCursor = nullptr;
    nGZSceneGraph::cISceneNodeIterator *mBegin = nullptr;
    nGZSceneGraph::cISceneNodeIterator *mEnd = nullptr;
    NodeRange() = default;
    NodeRange(const NodeRange &) = delete;
    NodeRange &operator=(const NodeRange &) = delete;
    ~NodeRange()
    {
        if (mEnd) mEnd->Release();       // receiver: cISceneNodeIterator
        if (mBegin) mBegin->Release();   // receiver: cISceneNodeIterator
        if (mCursor) mCursor->Release(); // receiver: cISceneNodeIterator
    }
    bool AtEnd() { return mCursor->IsEqual(mEnd); } // receiver: cISceneNodeIterator
};
static_assert(sizeof(NodeRange) == sizeof(nGZSceneGraph::cSceneNodeRange), "NodeRange: a cSceneNodeRange");

nGZSceneGraph::cILightRefNode *AsNode(const cBuiltRef &ref)
{
    return static_cast<nGZSceneGraph::cILightRefNode *>(ref.mObject);
}

// A new object of one of the director's classes into an empty reference.
void Make(cBuiltRef &ref, uint32_t classId, uint32_t iid)
{
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    com->GetClassObject(classId, iid, &ref.mObject);
}
void MakeNode(cBuiltRef &node) { Make(node, kLightRefNodeClass, nGZSceneGraph::cILightRefNode::kIid); }

} // namespace

} // namespace nTSSG
