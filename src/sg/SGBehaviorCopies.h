#pragma once
// The game's own copies of nGZEvent::cAbstractBehaviorTypeT<I, cRefSingleThreaded> (lane SV, scenegraph F1): one copy
// of the template's bodies per interface I, each with its connection-manager cell, its event registry and its
// "missing" record. Our template is gz/cAbstractBehaviorTypeST.h; the classes in the standalone shape carry their own
// instantiations in our vtables. These copies are what the game's vtables still hold (the node-template levels, the
// game-derived classes such as cScreenShapeRef, the lights and viewers built inline), so each body is hooked at its RVA
// (src/hook/tables/SGBehaviorCopies.cpp) to cBehaviorCopy's adapters below.
//
// cBehaviorCopy<kDeletingSlot, Cell, RegistryFor> is cAbstractBehaviorTypeST over a face whose deleting destructor lands
// at the copy's slot (Release deletes through it: `call [vtable + slot * 4]` with 1); the face is cIAbstractBehavior
// padded with never-called virtuals, as only slots 0-14 and the destructor are ever reached from these bodies. The
// traits name the copy's cell as an owned static (game/owned.h) and its registry function; the missing record is
// behavior_mt::gMissingRecord (owned, lane SR) for every copy (the image's per-copy records all hold the same words).
//
// The bodies, per copy (db/status/SGBehaviorCopies.jsonl):
//   Release (slot 2), OnEvent (3), NotifyListeners (5), AskListeners (6), Connect (8), Disconnect (9),
//   GetEventAttribute (14), and the cdecl GetEventManager (the cell's getter) the others call.
// Slots 12 / 13 (count, ids) are folded across each registry's families (shared_vf_*) and stay unhooked.
#include "game/original.h"
#include "game/owned.h"
#include "gz/cAbstractBehaviorTypeST.h"
#include "gz/cGZEventReflector.h"
#include "sg/SGNodeTemplates.h"
#include "sg/cDrawStateLight.h"
#include "sg/cEnvironmentCubeLight.h"
#include "sg/cGeometryNode.h"
#include "sg/cLightLevels.h" // the cILight cell 0x10da038
#include "sg/cLinearFogLight.h"
#include "sg/cMaterialManager.h"
#include "sg/cObjectGraphNodeImpl.h"
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"
#include "sg/cStandardLight.h"
#include "sg/cTextureManager.h"

#include <cstdint>
#include <type_traits>

// The copies' connection-manager cells (each copy's GetEventManager caches the manager there). The cells our own classes
// share with a copy are declared with those classes' behaviour traits (EventManagerCell), in the headers included above.
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10fc820); // cIRenderableNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6380); // cILightRefNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f63e0); // cIViewerRefNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6424); // cIViewerRefNodeRecursive
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f63dc); // cIObjectGraphNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6628); // cIDisplayList
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10d9c7c); // nTSSG::cICameraEffectsSupport

namespace nGZSceneGraph::behavior_copies {

using RegistryForFn = nGZEvent::cEventAttributeMap *(__cdecl *)(uint32_t id, const nGZEvent::cEventAttribute *attribute);

// The registries nobody has replaced yet (game code: function-local static maps built on first use; a non-zero id stores
// `attribute`). Not inlined: tests stand in for them.
__declspec(noinline) nGZEvent::cEventAttributeMap *__cdecl ShapeRefRegistryFor(uint32_t id, const nGZEvent::cEventAttribute *attribute);       // 0xa585a0
__declspec(noinline) nGZEvent::cEventAttributeMap *__cdecl LightRefRegistryFor(uint32_t id, const nGZEvent::cEventAttribute *attribute);       // 0xa633d0
__declspec(noinline) nGZEvent::cEventAttributeMap *__cdecl ViewerRefRegistryFor(uint32_t id, const nGZEvent::cEventAttribute *attribute);      // 0xa68db0
__declspec(noinline) nGZEvent::cEventAttributeMap *__cdecl DisplayListRegistryFor(uint32_t id, const nGZEvent::cEventAttribute *attribute);    // 0xaa2210
void ResetDisplayListRegistryForTest(); // ours starts again from nothing (tests only)
__declspec(noinline) nGZEvent::cEventAttributeMap *__cdecl CameraEffectsRegistryFor(uint32_t id, const nGZEvent::cEventAttribute *attribute);  // 0x361720

// cIAbstractBehavior (slots 0-14) followed by N never-called virtuals: a face whose next slot is the copy's deleting
// destructor. Each level adds one slot (distinct parameter types, so none overrides another).
template <int N> struct PaddedFace : PaddedFace<N - 1> {
    virtual void PadSlot(std::integral_constant<int, N> *) = 0;
};
template <> struct PaddedFace<0> : nGZEvent::cIAbstractBehavior {};

template <nGZEvent::cIConnectionManager *&Cell, RegistryForFn RegistryFor> struct CopyTraits {
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return Cell; }
    // The registry with id 0 (nothing stored), as the copies' slot 14 asks it.
    static nGZEvent::cEventAttributeMap *Registry()
    {
        const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
        return RegistryFor(0, &defaults);
    }
};

// One copy. `this` is the game's object (its vtable the game's); each adapter calls the template's body qualified.
template <int kDeletingSlot, nGZEvent::cIConnectionManager *&Cell, RegistryForFn RegistryFor>
class cBehaviorCopy
    : public nGZEvent::cAbstractBehaviorTypeST<PaddedFace<kDeletingSlot - 15>, CopyTraits<Cell, RegistryFor>> {
    static_assert(kDeletingSlot > 15, "the deleting destructor follows cIAbstractBehavior's 15 slots");
    using Behavior = nGZEvent::cAbstractBehaviorTypeST<PaddedFace<kDeletingSlot - 15>, CopyTraits<Cell, RegistryFor>>;

public:
    static constexpr int kSlot = kDeletingSlot;

    uint32_t HookRelease() { [[msvc::forceinline_calls]] return Behavior::Release(); }
    void HookOnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3)
    {
        [[msvc::forceinline_calls]] Behavior::OnEvent(type, arg1, data, arg3);
    }
    void HookNotifyListeners(uint32_t type, uint32_t arg1, void *data)
    {
        [[msvc::forceinline_calls]] Behavior::NotifyListeners(type, arg1, data);
    }
    bool HookAskListeners(uint32_t type, uint32_t arg1, void *data)
    {
        [[msvc::forceinline_calls]] return Behavior::AskListeners(type, arg1, data);
    }
    void HookConnect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent)
    {
        [[msvc::forceinline_calls]] Behavior::Connect(event, target, targetEvent);
    }
    void HookDisconnect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent)
    {
        [[msvc::forceinline_calls]] Behavior::Disconnect(event, target, targetEvent);
    }
    nGZEvent::cEventAttribute HookGetEventAttribute(uint32_t event)
    {
        [[msvc::forceinline_calls]] return Behavior::GetEventAttribute(event);
    }
    static nGZEvent::cIConnectionManager *__cdecl HookGetEventManager() { return Behavior::GetEventManager(); }
};

// The copies (the deleting destructor's slot, the cell, the registry).
using CompositionTreeNodeCopy = cBehaviorCopy<61, ts2_rva_10f5f98, &cCompositionTreeNodeBehaviorTraits::RegistryFor>;
using TransformNodeCopy = cBehaviorCopy<66, ts2_rva_10f5ffc, &cTransformNodeBehaviorTraits::RegistryFor>;
using ExplicitBoundNodeCopy = cBehaviorCopy<79, ts2_rva_10f616c, &cTransformNodeBehaviorTraits::RegistryFor>;
using RenderableNodeCopy = cBehaviorCopy<91, ts2_rva_10fc820, &cTransformNodeBehaviorTraits::RegistryFor>;
using GeometryNodeCopy = cBehaviorCopy<50, ts2_rva_10f6080, &cGeometryNodeBehaviorTraits::RegistryFor>;
using LightCopy = cBehaviorCopy<40, ts2_rva_10da038, &cSimpleLightBehaviorTraits::RegistryFor>;
using StandardLightCopy = cBehaviorCopy<58, ts2_rva_10da480, &cStandardLightBehaviorTraits::RegistryFor>;
using DrawStateLightCopy = cBehaviorCopy<43, ts2_rva_10f614c, &cSimpleLightBehaviorTraits::RegistryFor>;
using LinearFogLightCopy = cBehaviorCopy<43, ts2_rva_10f6148, &cSimpleLightBehaviorTraits::RegistryFor>;
using TextureLightCopy = cBehaviorCopy<43, ts2_rva_10f6144, &cSimpleLightBehaviorTraits::RegistryFor>;
using ShapeCopy = cBehaviorCopy<61, ts2_rva_10f6324, &cShapeBehaviorTraits::RegistryFor>;
using ShapeRefNodeCopy = cBehaviorCopy<100, ts2_rva_10f62dc, &ShapeRefRegistryFor>;
using LightRefNodeCopy = cBehaviorCopy<98, ts2_rva_10f6380, &LightRefRegistryFor>;
using ViewerRefNodeCopy = cBehaviorCopy<146, ts2_rva_10f63e0, &ViewerRefRegistryFor>;
using ViewerRefNodeRecursiveCopy = cBehaviorCopy<125, ts2_rva_10f6424, &cTransformNodeBehaviorTraits::RegistryFor>;
using ObjectGraphNodeCopy = cBehaviorCopy<29, ts2_rva_10f63dc, &cObjectGraphBehaviorTraits::RegistryFor>;
using DisplayListCopy = cBehaviorCopy<48, ts2_rva_10f6628, &DisplayListRegistryFor>;
using MaterialManagerCopy = cBehaviorCopy<38, ts2_rva_10f6998, &cMaterialManagerBehaviorTraits::RegistryFor>;
using TextureManagerCopy = cBehaviorCopy<29, ts2_rva_10f8c50, &cTextureManagerBehaviorTraits::RegistryFor>;
using CameraEffectsSupportCopy = cBehaviorCopy<19, ts2_rva_10d9c7c, &CameraEffectsRegistryFor>;

} // namespace nGZSceneGraph::behavior_copies
