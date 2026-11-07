#pragma once
// The game's TS-side copies of nGZEvent::cAbstractBehaviorTypeT<I, cRefSingleThreaded> (lane FD): one copy of the
// template's bodies per interface I (camera controller manager, configuration change manager, smooth first-person
// camera controller, config device settings, main toolbar event sink, screen grabber, win scene graph), each with its
// connection-manager cell, its event registry and its "missing" record. Our template is gz/cAbstractBehaviorTypeST.h;
// the game's vtables (the classes deriving from these, none of them ours yet) still hold the copies' bodies, so each
// body is hooked at its RVA (src/hook/tables/TSBehaviorCopies.cpp) to cTSBehaviorCopy's adapters below.
//
// The same shape as scenegraph's copies (sg/SGBehaviorCopies.h, lane SV), with two differences:
// - The deleting destructor may be slot 15 (the slot right after cIAbstractBehavior's), as in the smooth first-person
//   controller and config device settings copies.
// - Three copies were built /O1 (the toolbar sink, the screen grabber, the win scene graph): their bodies ask a
//   no-argument registry getter (0x61fea4, 0x60e453, and the base registry 0x1480d0) where the /O2 copies call their
//   registry function with id 0 and the default record. The traits take either form (RegistryFn).
//
// The bodies, per copy (db/status/TSBehaviorCopies.jsonl): Release (slot 2), OnEvent (3), NotifyListeners (5),
// AskListeners (6), Connect (8), Disconnect (9), GetEventCount (12), GetEventAttribute (14), and the cdecl
// GetEventManager (the cell's getter) the others call. Not here: GetEventIds (13, the id list's table is the image's),
// the destructors (they store the image's vtable), and the win scene graph copy's GetEventCount (folded with the base
// registry's other users).
#include "game/original.h"
#include "game/owned.h"
#include "gz/cAbstractBehaviorTypeST.h"
#include "gz/cGZEventReflector.h"

#include <cstdint>
#include <type_traits>

// The copies' connection-manager cells (each copy's GetEventManager caches the manager there).
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 1107858); // cITSCameraControllerManager
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 1107a5c); // cITSConfigurationChangeManager
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10fb424); // cITSCameraSmoothFirstPersonController
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10fc5fc); // cITSConfigDeviceSettings
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10dc5a8); // cITSMainToolbarEventSink
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10dc430); // cITSScreenGrabber
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10dc090); // cITSWinSceneGraph

namespace nTS::behavior_copies {

using Map = nGZEvent::cEventAttributeMap;
using Attribute = nGZEvent::cEventAttribute;
using RegistryForFn = Map *(__cdecl *)(uint32_t id, const Attribute *attribute);
using RegistryGetFn = Map *(__cdecl *)();

// The registries nobody has replaced yet (game code: function-local static maps built on first use; a non-zero id stores
// `attribute`). Not inlined: tests stand in for them.
__declspec(noinline) Map *__cdecl CameraControllerManagerRegistryFor(uint32_t id, const Attribute *attribute); // 0xcc3550
__declspec(noinline) Map *__cdecl ConfigurationChangeManagerRegistryFor(uint32_t id, const Attribute *attribute); // 0xcd6c00
__declspec(noinline) Map *__cdecl SmoothFirstPersonRegistryFor(uint32_t id, const Attribute *attribute); // 0xb4f2a0
__declspec(noinline) Map *__cdecl ConfigDeviceSettingsRegistryFor(uint32_t id, const Attribute *attribute); // 0xb6f910
// The /O1 copies' getters (each the registry with id 0 and the default record).
__declspec(noinline) Map *__cdecl MainToolbarRegistry(); // 0x61fea4
__declspec(noinline) Map *__cdecl ScreenGrabberRegistry(); // 0x60e453

// cIAbstractBehavior (slots 0-14) followed by N never-called virtuals: a face whose next slot is the copy's deleting
// destructor. Each level adds one slot (distinct parameter types, so none overrides another).
template <int N> struct PaddedFace : PaddedFace<N - 1> {
    virtual void PadSlot(std::integral_constant<int, N> *) = 0;
};
template <> struct PaddedFace<0> : nGZEvent::cIAbstractBehavior {};

template <nGZEvent::cIConnectionManager *&Cell, auto RegistryFn> struct CopyTraits {
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return Cell; }
    // The registry as the copy's bodies ask it: the getter, or the function with id 0 (nothing stored).
    static Map *Registry()
    {
        if constexpr (std::is_same_v<decltype(RegistryFn), RegistryGetFn>) {
            return RegistryFn();
        } else {
            const Attribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
            return RegistryFn(0, &defaults);
        }
    }
};

// One copy. `this` is the game's object (its vtable the game's); each adapter calls the template's body qualified.
template <int kDeletingSlot, nGZEvent::cIConnectionManager *&Cell, auto RegistryFn>
class cTSBehaviorCopy
    : public nGZEvent::cAbstractBehaviorTypeST<PaddedFace<kDeletingSlot - 15>, CopyTraits<Cell, RegistryFn>> {
    static_assert(kDeletingSlot >= 15, "the deleting destructor follows cIAbstractBehavior's 15 slots");
    using Behavior = nGZEvent::cAbstractBehaviorTypeST<PaddedFace<kDeletingSlot - 15>, CopyTraits<Cell, RegistryFn>>;

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
    uint32_t HookGetEventCount() { [[msvc::forceinline_calls]] return Behavior::GetEventCount(); }
    Attribute HookGetEventAttribute(uint32_t event)
    {
        [[msvc::forceinline_calls]] return Behavior::GetEventAttribute(event);
    }
    static nGZEvent::cIConnectionManager *__cdecl HookGetEventManager() { return Behavior::GetEventManager(); }
};

// The copies (the deleting destructor's slot, the cell, the registry).
using CameraControllerManagerCopy = cTSBehaviorCopy<41, ts2_rva_1107858, &CameraControllerManagerRegistryFor>;
using ConfigurationChangeManagerCopy = cTSBehaviorCopy<27, ts2_rva_1107a5c, &ConfigurationChangeManagerRegistryFor>;
using SmoothFirstPersonCopy = cTSBehaviorCopy<15, ts2_rva_10fb424, &SmoothFirstPersonRegistryFor>;
using ConfigDeviceSettingsCopy = cTSBehaviorCopy<15, ts2_rva_10fc5fc, &ConfigDeviceSettingsRegistryFor>;
using MainToolbarCopy = cTSBehaviorCopy<16, ts2_rva_10dc5a8, &MainToolbarRegistry>;
using ScreenGrabberCopy = cTSBehaviorCopy<16, ts2_rva_10dc430, &ScreenGrabberRegistry>;
using WinSceneGraphCopy = cTSBehaviorCopy<50, ts2_rva_10dc090, &nGZEvent::cAbstractBehaviorType::Registry>;

} // namespace nTS::behavior_copies
