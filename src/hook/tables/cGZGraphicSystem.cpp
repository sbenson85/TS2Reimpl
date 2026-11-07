#include "hook/hooks.h"
#include "gz/cGZGraphicSystem.h"

// The GZ graphic system's creator (the graphics COM director's class 0x20732180) is the switch: every slot of the system
// and of its back buffer (cBackbufferAsGZBuffer, made only here) is reached through our vtables (in effect through
// callers; trap Hooks.nGZGraphic4_cGZGraphicSystem_Create=1).
namespace {

const HookSpec kHooks[] = {
    {"nGZGraphic4_cGZGraphicSystem_Create", 0x1475e0, 32, 0xf956bc91, reinterpret_cast<void *>(&nGZGraphic4::cGZGraphicSystem::Create)},
    // The event registry (lane PD): the template instantiation's function-local static, built on first use.
    {"nGZGraphic4_cGZGraphicSystem_EventRegistry", 0x148440, 32, 0x2fcc3d73,
     reinterpret_cast<void *>(&nGZGraphic4::cGZGraphicSystemBehaviorTraits::RegistryFor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
