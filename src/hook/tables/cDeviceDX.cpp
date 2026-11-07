#include "hook/hooks.h"
#include "gz/cDeviceDX.h"

// The device's constructor and Init (the graphic system's 0x159410 calls both directly). Every slot is reached through
// our vtable (in effect through callers; trap Hooks.nGZGraphic4_cDeviceDX_Construct=1).
namespace {

const HookSpec kHooks[] = {
    {"nGZGraphic4_cDeviceDX_Construct", 0x167200, 32, 0xf99ba7bc, reinterpret_cast<void *>(&nGZGraphic4::cDeviceDXAdapters::Construct)},
    // The multisample clamp (lane RB): hooked at its RVA, BuildPresentParameters calls ours.
    {"nGZGraphic4_ClampMultisample", 0x18b7d0, 32, 0xb17dc7d5, reinterpret_cast<void *>(&nGZGraphic4::ClampMultisample)},
    {"nGZGraphic4_cDeviceDX_Init", 0x169cc0, 32, 0x8726bce1, reinterpret_cast<void *>(&nGZGraphic4::cDeviceDXAdapters::Init)},
    // The event registry (lane RJ): our own map; the original's callers are all ours.
    // The connection manager's cached getter (lane WI): ours of the slots call ours; the original's last caller is the
    // behaviour base's slot 3 0x169bb0, on a vtable only the original constructor's unwind could store.
    {"nGZGraphic4_cDeviceDX_GetEventManager", 0x167e00, 32, 0x204ca3d1, reinterpret_cast<void *>(&nGZGraphic4::cDeviceDX::GetEventManager)},
    {"nGZGraphic4_cDeviceDX_EventRegistry", 0x16a170, 32, 0xc8161b4c, reinterpret_cast<void *>(&nGZGraphic4::cDeviceDX::EventRegistryFor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
