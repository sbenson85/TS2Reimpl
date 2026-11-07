#include "hook/hooks.h"
#include "gz/cCanvasW32.h"

// The window service's creator (the graphics COM director's class table) is the switch: every slot of cCanvasW32 is
// reached through our vtables (in effect through callers; trap Hooks.nGZGraphic4_cCanvasW32_Create=1). Only the creator
// and the deleting destructor store the vtables.
namespace {

const HookSpec kHooks[] = {
    {"nGZGraphic4_cCanvasW32_Create", 0x155c70, 32, 0x7f0dd62d, reinterpret_cast<void *>(&nGZGraphic4::cCanvasW32::Create)},
    // The event registry (lane PD): the template instantiation's function-local static, built on first use.
    {"nGZGraphic4_cCanvasW32_EventRegistry", 0x156a70, 32, 0x07ac9e48,
     reinterpret_cast<void *>(&nGZGraphic4::cCanvasW32Traits::RegistryFor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
