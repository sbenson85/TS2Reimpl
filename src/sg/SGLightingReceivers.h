#pragma once
// The receivers the lighting and diffusion code calls by slot (lane 261110n; the table "Receivers the lighting code calls
// by slot" in docs/subsystems/scenegraph.md, LD1b, checked in the image). Each interface declares ONLY the slots that
// table gives; the slots before them are padding virtuals (vtable_detail::Pad<N>, game/vtable.h: N pure virtuals, the
// way VirtualCall puts a slot at its index), so the declared ones sit at the original's vtable index. Nothing derives
// from these but cIPortalNode (a view on the portal face): they are views on game objects, called through the object's
// own vtable, so a typed call compiles to the call the RawSlot<N, R> it replaces made. The bodies' RVAs are in the
// comments; `ret` is the callee's pop (tools/arity.py skips these classes: their base is not a src/ class).
//
// Slots of nTSSG::cTSObject (42, 101) are ifaces::cTSObject's (GetDefinitionKey, GetTileBytes), nTSWorld::cWorldDB's
// (69, 71) world/cWorldDB.h's, and the portal's light reference node is nGZSceneGraph::cILightRefNode (sg/cLightRefNode.h).
#include "game/vtable.h"
#include "sg/SGNodeInterfaces.h"

#include <cstdint>

namespace nTSSG {

class cLightingManager;

namespace lightingreceivers_detail {
// N more pure virtuals after Base's slots (vtable_detail::Pad<N> puts them first, in front of the declared slot).
template <class Base, int N> struct PadAfter : PadAfter<Base, N - 1> {
    virtual void pad(PadAfter *) = 0;
};
template <class Base> struct PadAfter<Base, 0> : Base {};
} // namespace lightingreceivers_detail

// nTSSG::cTSSGSystem (the primary face, vtable 0xdae264, 100 slots).
class cITSSGSystemSceneManager : public vtable_detail::Pad<33> {
public:
    virtual void *GetSceneManager() = 0; // 33: 0x254bb0 (folded), ret: +0x68, the scene manager (nTSSG::cSceneManager)
};
class cITSSGSystemManagers : public lightingreceivers_detail::PadAfter<cITSSGSystemSceneManager, 15> {
public:
    virtual cLightingManager *GetLightingManager() = 0; // 49: 0x155e40 (folded), ret: +0xa4, the lighting manager
};

// nTSSG::cSceneManager (vtable 0xdb5344, 34 slots; sg/cSceneManager.h holds its layout).
class cISceneManagerScene : public vtable_detail::Pad<17> {
public:
    virtual nGZSceneGraph::cICompositionTreeNode *GetScene() = 0; // 17: 0x46390, ret: +8, the scene
};

// nTSSG::cObject's portal face (interface id 0xfa1c39ed, vtable 0xdb0534): a composition node (slots 0-60 are
// cICompositionTreeNode's, 54 its world transform) with, at 120, the owner object (slots 61-119 not declared).
class cIPortalNode : public lightingreceivers_detail::PadAfter<nGZSceneGraph::cICompositionTreeNode, 59> {
public:
    // 120: shared_vf_2675ca (`sub ecx,[ecx-4]`, then 0x2675e0), ret: the owner object ([ecx-0xbc] after the adjustment),
    // the portal kind object (an nTSSG::cTSObject; its slots 42 and 101 are ifaces::cTSObject's).
    virtual void *GetOwner() = 0;
};

// nTSWorld::cPartitionManager (the world's partitions; the placement service's +0x24).
class cIPartitionManager : public vtable_detail::Pad<80> {
public:
    // 80: 0x818640, ret 0xc: eax a partition chosen by 0x818680(a), its slot 14 with b at the position's quadrant
    // (0x7dd6c0); the callers keep the low word, the level at the position. `position` is a WorldPoint, a vector of
    // three floats or a level-of-ground triple, as the callers pass it.
    virtual uint32_t GetLevelAt(const void *position, uint32_t a, uint32_t b) = 0;
};

} // namespace nTSSG
