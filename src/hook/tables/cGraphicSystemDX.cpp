#include "hook/hooks.h"
#include "gz/cGraphicSystemDX.h"

// The graphic system's creator (the graphics COM director's class 0x855227cd) is the switch: every slot, the devices'
// build and teardown and the canvas handler are reached through our vtable (in effect through callers; trap
// Hooks.nGZGraphic4_cGraphicSystemDX_Create=1).
namespace {

const HookSpec kHooks[] = {
    {"nGZGraphic4_cGraphicSystemDX_Create", 0x159940, 32, 0x1ec0c511, reinterpret_cast<void *>(&nGZGraphic4::cGraphicSystemDX::Create)},
    // The event registry (lane PD): the template instantiation's function-local static, built on first use.
    {"nGZGraphic4_cGraphicSystemDX_EventRegistry", 0x15a250, 32, 0x916383bb,
     reinterpret_cast<void *>(&nGZGraphic4::cGraphicSystemDXBehaviorTraits::RegistryFor)},
    // The connection manager's cached getter (lane WI): the behaviour template's static for this instantiation.
    {"nGZGraphic4_cGraphicSystemDX_GetEventManager", 0x159390, 32, 0xa9501ebd,
     reinterpret_cast<void *>(&nGZEvent::cAbstractBehaviorTypeMT<nGZGraphic4::cIGraphicSystemDX,
                                                                 nGZGraphic4::cGraphicSystemDXBehaviorTraits>::GetEventManager)},
};
REGISTER_HOOKS(kHooks);

} // namespace
